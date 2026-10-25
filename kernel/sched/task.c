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

task_t *task_create(const char *name, task_entry_t entry, void *arg) {
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
    t->id = next_id++;
    t->started_at = timer_ms();

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
        task_info_t *i = &out[n++];
        i->id = t->id;
        memcpy(i->name, t->name, sizeof(i->name));
        i->state = t->state;
        i->cpu_percent = t->cpu_percent;
        i->cpu_ms = t->cpu_ticks;
        i->started_at = t->started_at;
        i->stack_size = t->stack_size;
        i->mem_bytes = t->mem_bytes;
        i->runs = t->runs;
        i->system = t->system;
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
    __asm__ volatile("int $0x81" ::: "memory");
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
    t->state = TASK_DEAD;
    task_yield();
    panic("tarefa morta voltou a rodar");
}

bool task_kill(task_t *t) {
    if (!t || t->system || t->state == TASK_DEAD) return false;
    if (t == current) task_exit();
    uint64_t f = irq_save();
    t->kill_pending = true;
    if (t->state == TASK_BLOCKED || t->state == TASK_SLEEPING)
        t->state = TASK_READY;
    irq_restore(f);
    return true;
}

/* Se uma tarefa marcada para morrer vai rodar e não segura nenhum lock,
 * trocamos o contexto salvo dela por uma chamada a task_exit(). */
static void apply_kill(task_t *t) {
    if (!t->kill_pending || t->lock_depth > 0) return;
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
        if (t->state == TASK_DEAD && t != current) {
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
