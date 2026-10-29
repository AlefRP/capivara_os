/*
 * Escalonador.
 *
 * A troca de contexto acontece sempre dentro de uma interrupção:
 *   - o stub em assembly salvou os registradores da tarefa atual na pilha dela;
 *   - sched_switch() guarda esse ponteiro, escolhe a próxima tarefa PRONTA e
 *     devolve o regs_t* dela;
 *   - o stub restaura os registradores da nova tarefa e executa iretq.
 *
 * Uma tarefa nova "nasce" com um regs_t fabricado no topo da sua pilha, como
 * se tivesse sido interrompida exatamente no início da função de entrada.
 */
#include "sched/task.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/pit.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "panic.h"

#define STACK_SIZE (64 * 1024)
#define TIMESLICE_MS 10

static task_t *task_head;          /* lista de todas as tarefas */
static task_t *current;
static task_t *idle_task;
static int next_id = 1;
static volatile bool resched;
static uint32_t slice_left = TIMESLICE_MS;
static uint8_t fpu_template[512] __attribute__((aligned(16)));
static void (*exit_hook)(task_t *t);
static sched_stats_t counters;

/* Compare o endereço na lista antes de acessar o nó. O chamador continua
 * responsável por descartar um handle consumido por join/detach. */
static bool registered(task_t *target) {
    for (task_t *t = task_head; t; t = t->next)
        if (t == target) return true;
    return false;
}

/* Estas duas rotinas são chamadas com interrupções desligadas. Um crédito
 * já concedido ainda pertence à espera até sem_wait voltar; se a tarefa for
 * morta nesse intervalo, ele passa ao próximo em vez de desaparecer. */
static void sem_release(sem_t *s) {
    task_t *t = s->head;
    if (t) {
        s->head = t->sem_next;
        if (!s->head) s->tail = NULL;
        t->sem_next = NULL;
        t->sem_granted = true;
        task_wake(t);
    } else {
        ASSERT(s->count != UINT64_MAX);
        s->count++;
    }
}

static void cancel_wait(task_t *t) {
    sem_t *s = t->waiting_sem;
    if (s) {
        bool granted = t->sem_granted;
        if (!granted) {
            task_t *prev = NULL, *p = s->head;
            while (p && p != t) { prev = p; p = p->sem_next; }
            if (p) {
                if (prev) prev->sem_next = p->sem_next; else s->head = p->sem_next;
                if (s->tail == p) s->tail = prev;
            }
        }
        t->waiting_sem = NULL;
        t->sem_next = NULL;
        t->sem_granted = false;
        if (granted) sem_release(s);
    }
    if (t->join_target) {
        t->join_target->joiner = NULL;
        t->join_target = NULL;
    }
}

static void task_info(task_t *t, task_info_t *i) {
    i->id = t->id;
    memcpy(i->name, t->name, sizeof(i->name));
    i->state = t->state;
    i->cpu_percent = t->cpu_percent;
    i->cpu_ms = t->cpu_ticks;
    i->started_at = t->started_at;
    i->finished_at = t->finished_at;
    i->stack_size = t->stack_size;
    i->mem_bytes = t->mem_bytes;
    i->runs = t->runs;
    i->yields = t->yields;
    i->blocks = t->blocks;
    i->system = t->system;
}

/* Nunca liberar a pilha corrente; join só chama isto para um alvo morto. */
static void task_free(task_t *t) {
    task_t **link = &task_head;
    while (*link && *link != t) link = &(*link)->next;
    if (*link) *link = t->next;
    kfree(t->stack);
    kfree(t->fpu);
    kfree(t);
}

task_t *task_current(void) { return current; }

void sched_stats(sched_stats_t *out) {
    uint64_t f = irq_save();
    *out = counters;
    irq_restore(f);
}
void sched_set_exit_hook(void (*hook)(task_t *t)) { exit_hook = hook; }

static void add_task(task_t *t) {
    uint64_t f = irq_save();
    t->next = NULL;
    if (!task_head) task_head = t;
    else {
        task_t *p = task_head;
        while (p->next) p = p->next;
        p->next = t;
    }
    irq_restore(f);
}

void sched_init(void) {
    /* Estado inicial "limpo" do FPU/SSE, copiado para toda tarefa nova */
    __asm__ volatile("fninit; fxsave %0" : "=m"(fpu_template));

    /* A execução atual (kmain) vira a tarefa "ociosa" */
    idle_task = kcalloc(1, sizeof(task_t));
    idle_task->id = 0;
    strlcpy(idle_task->name, "ocioso", sizeof(idle_task->name));
    idle_task->state = TASK_RUNNING;
    idle_task->fpu = kmalloc(512);
    idle_task->system = true;
    memcpy(idle_task->fpu, fpu_template, 512);
    add_task(idle_task);
    current = idle_task;
}

static void task_trampoline_exit(void) {
    task_exit();
}

static task_t *create(const char *name, task_entry_t entry, void *arg, bool joinable) {
    task_t *t = kcalloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->stack = kmalloc(STACK_SIZE);
    t->fpu = kmalloc(512);
    if (!t->stack || !t->fpu) {
        kfree(t->stack); kfree(t->fpu); kfree(t);
        return NULL;
    }
    t->stack_size = STACK_SIZE;
    memcpy(t->fpu, fpu_template, 512);
    strlcpy(t->name, name, sizeof(t->name));
    uint64_t f = irq_save();
    t->id = next_id++;
    irq_restore(f);
    t->started_at = timer_ms();
    t->joinable = joinable;

    /* Topo da pilha: endereço de retorno falso (se a função retornar, a tarefa
     * termina) e, abaixo dele, o regs_t que o iretq vai consumir. */
    uint64_t top = ((uint64_t)t->stack + STACK_SIZE) & ~15ULL;
    uint64_t *ret = (uint64_t *)(top - 8);
    *ret = (uint64_t)task_trampoline_exit;
    regs_t *r = (regs_t *)((uint64_t)ret - sizeof(regs_t));
    memset(r, 0, sizeof(*r));
    r->rip = (uint64_t)entry;
    r->rdi = (uint64_t)arg;
    r->cs = SEL_KCODE;
    r->ss = SEL_KDATA;
    r->rflags = 0x202;              /* IF = 1: interrupções ligadas */
    r->rsp = (uint64_t)ret;         /* rsp % 16 == 8, como logo após um "call" */
    t->regs = r;
    t->state = TASK_READY;

    add_task(t);
    kprintf("tarefa %d criada: %s\n", t->id, t->name);
    return t;
}

task_t *task_create(const char *name, task_entry_t entry, void *arg) {
    return create(name, entry, arg, false);
}

task_t *task_create_joinable(const char *name, task_entry_t entry, void *arg) {
    return create(name, entry, arg, true);
}

bool task_join(task_t *target, task_info_t *info) {
    uint64_t f = irq_save();
    if (!registered(target) || target == current || !target->joinable || target->joiner) {
        irq_restore(f);
        return false;
    }
    /* Uma cadeia termina num alvo sem join_target; fechar um ciclo faria
     * todos dormirem para sempre, então a última ligação é recusada. */
    for (task_t *t = target; t; t = t->join_target) {
        if (t == current) { irq_restore(f); return false; }
    }
    target->joiner = current;
    current->join_target = target;
    while (target->state != TASK_DEAD) {
        current->state = TASK_BLOCKED;
        task_yield();
    }
    if (info) task_info(target, info);
    current->join_target = NULL;
    target->joiner = NULL;
    task_free(target);
    irq_restore(f);
    return true;
}

bool task_detach(task_t *target) {
    uint64_t f = irq_save();
    bool ok = registered(target) && target->joinable && !target->joiner;
    if (ok) target->joinable = false;
    irq_restore(f);
    return ok;
}

task_t *task_find(int id) {
    uint64_t f = irq_save();
    task_t *t = task_head;
    while (t && t->id != id) t = t->next;
    irq_restore(f);
    return t;
}

int task_list(task_t **out, int max) {
    uint64_t f = irq_save();
    int n = 0;
    for (task_t *t = task_head; t && n < max; t = t->next)
        if (t->state != TASK_DEAD) out[n++] = t;
    irq_restore(f);
    return n;
}

int task_snapshot(task_info_t *out, int max) {
    uint64_t f = irq_save();
    int n = 0;
    for (task_t *t = task_head; t && n < max; t = t->next) {
        if (t->state == TASK_DEAD) continue;
        task_info(t, &out[n++]);
    }
    irq_restore(f);
    return n;
}

/* chamada pelo heap (com interrupções desligadas) */
void task_account_memory(int task_id, int64_t delta) {
    for (task_t *t = task_head; t; t = t->next) {
        if (t->id != task_id || t->state == TASK_DEAD) continue;
        if (delta < 0 && (uint64_t)-delta > t->mem_bytes) t->mem_bytes = 0;
        else t->mem_bytes += delta;
        return;
    }
}

void task_yield(void) {
    uint64_t f = irq_save();
    if (current->state == TASK_RUNNING) current->yields++;
    else if (current->state == TASK_BLOCKED) current->blocks++;
    __asm__ volatile("int $0x81" ::: "memory");
    irq_restore(f);
}

void task_sleep(uint64_t ms) {
    uint64_t f = irq_save();
    current->wake_at = timer_ms() + ms;
    current->state = TASK_SLEEPING;
    task_yield();
    irq_restore(f);
}

void task_block(void) {
    uint64_t f = irq_save();
    current->state = TASK_BLOCKED;
    task_yield();
    irq_restore(f);
}

void task_wake(task_t *t) {
    uint64_t f = irq_save();
    if (t->state == TASK_BLOCKED || t->state == TASK_SLEEPING) {
        t->state = TASK_READY;
        resched = true;
    }
    irq_restore(f);
}

void task_exit(void) {
    task_t *t = current;
    kprintf("tarefa %d (%s) terminou\n", t->id, t->name);
    if (exit_hook) exit_hook(t);       /* fecha as janelas da tarefa */
    cli();
    cancel_wait(t);
    t->finished_at = timer_ms();
    t->state = TASK_DEAD;
    if (t->joiner) task_wake(t->joiner);
    task_yield();
    panic("tarefa morta voltou a rodar");
}

bool task_kill(task_t *t) {
    uint64_t f = irq_save();
    if (!registered(t) || t->system || t->state == TASK_DEAD) {
        irq_restore(f);
        return false;
    }
    if (t == current) { irq_restore(f); task_exit(); }
    t->kill_pending = true;
    if (t->state == TASK_BLOCKED || t->state == TASK_SLEEPING)
        t->state = TASK_READY;
    resched = true;
    irq_restore(f);
    return true;
}

/* Se uma tarefa marcada para morrer vai rodar e não segura nenhum lock,
 * trocamos o contexto salvo dela por uma chamada a task_exit(). */
static void apply_kill(task_t *t) {
    if (!t->kill_pending || t->lock_depth > 0) return;
    cancel_wait(t);
    t->kill_pending = false;
    uint64_t top = ((uint64_t)t->stack + t->stack_size) & ~15ULL;
    uint64_t *ret = (uint64_t *)(top - 8);
    *ret = 0;
    regs_t *r = (regs_t *)((uint64_t)ret - sizeof(regs_t));
    memset(r, 0, sizeof(*r));
    r->rip = (uint64_t)task_exit;
    r->cs = SEL_KCODE;
    r->ss = SEL_KDATA;
    r->rflags = 0x202;
    r->rsp = (uint64_t)ret;
    t->regs = r;
}

/* Recolhe tarefas mortas (não dá para liberar a pilha de quem está rodando nela) */
static void reap(void) {
    uint64_t f = irq_save();
    task_t *prev = NULL, *t = task_head;
    while (t) {
        task_t *next = t->next;
        if (t->state == TASK_DEAD && t != current && !t->joinable) {
            if (prev) prev->next = next; else task_head = next;
            kfree(t->stack);
            kfree(t->fpu);
            kfree(t);
        } else {
            prev = t;
        }
        t = next;
    }
    irq_restore(f);
}

void task_idle_loop(void) {
    for (;;) {
        reap();
        sti();
        hlt();
    }
}

/* ---- chamado pela interrupção do timer (1 kHz) ---- */
void sched_tick(uint64_t now) {
    current->cpu_ticks++;

    for (task_t *t = task_head; t; t = t->next) {
        if (t->state == TASK_SLEEPING && now >= t->wake_at) {
            t->state = TASK_READY;
            resched = true;
        }
        if (t->timer_interval && now >= t->timer_next && t->state != TASK_DEAD) {
            t->timer_next = now + t->timer_interval;
            if (!t->timer_pending) {
                event_t e = { .type = EV_TIMER };
                t->timer_pending = true;
                ev_post(t, &e);
            }
        }
    }

    /* estatística de uso de CPU a cada segundo */
    if (now % 1000 == 0) {
        for (task_t *t = task_head; t; t = t->next) {
            uint64_t d = t->cpu_ticks - t->cpu_ticks_prev;
            t->cpu_percent = (uint32_t)(d / 10);
            t->cpu_ticks_prev = t->cpu_ticks;
        }
    }

    if (--slice_left == 0) {
        slice_left = TIMESLICE_MS;
        resched = true;
    }
}

bool sched_need_resched(void) { return resched; }
void sched_request_resched(void) { resched = true; }

regs_t *sched_switch(regs_t *regs) {
    resched = false;
    current->regs = regs;
    if (current->state == TASK_RUNNING) current->state = TASK_READY;

    /* round-robin: próxima tarefa PRONTA depois da atual; ociosa só se não houver outra */
    task_t *next = NULL;
    task_t *t = current->next ? current->next : task_head;
    for (int guard = 0; guard < 1024; guard++) {
        if (t->state == TASK_READY && t != idle_task) { next = t; break; }
        if (t == current) break;
        t = t->next ? t->next : task_head;
    }
    if (!next) next = (current->state == TASK_READY && current != idle_task) ? current : idle_task;

    if (next != current) {
        counters.switches++;
        if (current->state == TASK_READY) counters.involuntary++;
        next->runs++;
        __asm__ volatile("fxsave (%0)" :: "r"(current->fpu) : "memory");
        __asm__ volatile("fxrstor (%0)" :: "r"(next->fpu) : "memory");
    }
    apply_kill(next);
    next->state = TASK_RUNNING;
    current = next;
    slice_left = TIMESLICE_MS;
    tss_set_kernel_stack(((uint64_t)next->stack + next->stack_size) & ~15ULL);
    return next->regs;
}

/* ---- mutex ---- */
void mutex_lock(mutex_t *m) {
    for (;;) {
        uint64_t f = irq_save();
        if (!m->locked) {
            m->locked = 1;
            m->owner = current;
            current->lock_depth++;
            irq_restore(f);
            return;
        }
        irq_restore(f);
        task_yield();
    }
}

void mutex_unlock(mutex_t *m) {
    uint64_t f = irq_save();
    ASSERT(m->owner == current);
    m->locked = 0;
    m->owner = NULL;
    current->lock_depth--;
    irq_restore(f);
}

/* ---- semáforo bloqueante ---- */
void sem_init(sem_t *s, uint64_t count) {
    uint64_t f = irq_save();
    s->count = count;
    s->head = s->tail = NULL;
    irq_restore(f);
}

void sem_wait(sem_t *s) {
    uint64_t f = irq_save();
    if (s->count) {
        s->count--;
        irq_restore(f);
        return;
    }
    task_t *t = current;
    ASSERT(!t->waiting_sem && !t->join_target);
    t->waiting_sem = s;
    t->sem_granted = false;
    t->sem_next = NULL;
    if (s->tail) s->tail->sem_next = t; else s->head = t;
    s->tail = t;
    /* Testar o crédito, entrar na fila e bloquear são uma só região atômica.
     * Um wake genérico não concede crédito: o laço volta a dormir nesse caso. */
    while (!t->sem_granted) {
        t->state = TASK_BLOCKED;
        task_yield();
    }
    t->waiting_sem = NULL;
    t->sem_granted = false;
    irq_restore(f);
}

void sem_post(sem_t *s) {
    uint64_t f = irq_save();
    sem_release(s);
    irq_restore(f);
}
