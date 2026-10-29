/* Regressões das primitivas reais: os gates fixam a ordem dos acontecimentos,
 * sem depender de uma corrida aparecer por sorte ou do tempo de CPU do host. */
#include "apps/ostep.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

#define SYNC_TIMEOUT 3000
#define SYNC_TASKS 24

typedef struct {
    sem_t *gate;
    task_t *target;
    int done, joined;
    task_info_t info;
} actor_t;

typedef struct {
    sem_t gates[2];
    actor_t actors[3];
    int ids[SYNC_TASKS], nids;
} sync_t;

typedef enum { WAIT_SEM, WAIT_JOIN, WAIT_DEAD, WAIT_GONE, WAIT_AGAIN } phase_t;

static bool wait_for(int id, phase_t phase, const void *object) {
    uint64_t start = timer_ms();
    do {
        uint64_t f = irq_save();
        task_t *t = task_find(id);
        bool ok = !t && phase == WAIT_GONE;
        if (t) {
            if (phase == WAIT_DEAD) ok = t->state == TASK_DEAD;
            else if (phase == WAIT_SEM || phase == WAIT_AGAIN)
                ok = t->state == TASK_BLOCKED && t->waiting_sem == object && !t->sem_granted
                     && (phase != WAIT_AGAIN || t->blocks >= 2);
            else if (phase == WAIT_JOIN)
                ok = t->state == TASK_BLOCKED && t->join_target == object;
        }
        irq_restore(f);
        if (ok) return true;
        task_sleep(1);
    } while (timer_ms() - start < SYNC_TIMEOUT);
    return false;
}

static task_t *start(sync_t *s, const char *name, task_entry_t entry, actor_t *a) {
    if (s->nids == SYNC_TASKS) return NULL;
    /* Os testes, e não a interface, decidem quando matar estes workers. A
     * flag fica pronta antes de a tarefa poder ser escalonada pela primeira vez. */
    uint64_t f = irq_save();
    task_t *t = task_create_joinable(name, entry, a);
    if (t) { t->system = true; s->ids[s->nids++] = t->id; }
    irq_restore(f);
    return t;
}

static bool stop(task_t *t) {
    uint64_t f = irq_save();
    t->system = false;
    bool ok = task_kill(t);
    irq_restore(f);
    return ok;
}

static bool collect(int id, task_info_t *info) {
    if (!wait_for(id, WAIT_DEAD, NULL)) return false;
    uint64_t f = irq_save();
    task_t *t = task_find(id);
    bool ok = t && t->state == TASK_DEAD && task_join(t, info);
    irq_restore(f);
    return ok;
}

static bool empty(const sem_t *s, uint64_t count) {
    uint64_t f = irq_save();
    bool ok = s->count == count && !s->head && !s->tail;
    irq_restore(f);
    return ok;
}

static void consume(void *arg) {
    actor_t *a = arg;
    sem_wait(a->gate);
    a->done++;
}

static void finish(void *arg) {
    actor_t *a = arg;
    for (int i = 0; i < 3; i++) task_yield();
    a->done++;
}

static void join_worker(void *arg) {
    actor_t *a = arg;
    if (a->gate) sem_wait(a->gate);
    a->joined = task_join(a->target, &a->info) ? 1 : -1;
    a->done++;
}

static bool post_first(sync_t *s) {
    uint64_t blocks = task_current()->blocks;
    sem_post(&s->gates[0]);
    if (!empty(&s->gates[0], 1)) return false;
    sem_wait(&s->gates[0]);
    return empty(&s->gates[0], 0) && task_current()->blocks == blocks;
}

static bool wait_first(sync_t *s) {
    actor_t *a = &s->actors[0]; a->gate = &s->gates[0];
    task_t *t = start(s, "sync espera", consume, a);
    if (!t || !wait_for(t->id, WAIT_SEM, a->gate)) return false;
    int id = t->id;
    sem_post(a->gate);
    task_info_t info;
    return collect(id, &info) && a->done == 1 && info.blocks == 1
           && info.state == TASK_DEAD && info.finished_at >= info.started_at && empty(a->gate, 0);
}

static bool wake_without_credit(sync_t *s) {
    actor_t *a = &s->actors[0]; a->gate = &s->gates[0];
    task_t *t = start(s, "sync wake", consume, a);
    if (!t || !wait_for(t->id, WAIT_SEM, a->gate)) return false;
    int id = t->id;
    task_wake(t);
    if (!wait_for(id, WAIT_AGAIN, a->gate) || a->done) return false;
    sem_post(a->gate);
    task_info_t info;
    return collect(id, &info) && a->done == 1 && info.blocks == 2 && empty(a->gate, 0);
}

static bool two_waiters(sync_t *s, task_t **first, task_t **second) {
    for (int i = 0; i < 2; i++) s->actors[i].gate = &s->gates[0];
    *first = start(s, "sync primeiro", consume, &s->actors[0]);
    if (!*first || !wait_for((*first)->id, WAIT_SEM, &s->gates[0])) return false;
    *second = start(s, "sync segundo", consume, &s->actors[1]);
    return *second && wait_for((*second)->id, WAIT_SEM, &s->gates[0]);
}

static bool kill_queued(sync_t *s) {
    task_t *a, *b;
    if (!two_waiters(s, &a, &b)) return false;
    int aid = a->id, bid = b->id;
    if (!stop(a) || !collect(aid, NULL)) return false;
    uint64_t f = irq_save();
    bool linked = s->gates[0].head == b && s->gates[0].tail == b && !b->sem_next;
    irq_restore(f);
    if (!linked) return false;
    sem_post(&s->gates[0]);
    return collect(bid, NULL) && !s->actors[0].done && s->actors[1].done == 1
           && empty(&s->gates[0], 0);
}

static bool kill_granted(sync_t *s) {
    task_t *a, *b;
    if (!two_waiters(s, &a, &b)) return false;
    int aid = a->id, bid = b->id;
    /* Nenhuma instrução do waiter pode rodar entre receber o grant e kill. */
    uint64_t f = irq_save();
    sem_post(&s->gates[0]);
    bool granted = a->sem_granted && !b->sem_granted && s->gates[0].head == b;
    bool killed = stop(a);
    irq_restore(f);
    return granted && killed && collect(aid, NULL) && collect(bid, NULL)
           && !s->actors[0].done && s->actors[1].done == 1 && empty(&s->gates[0], 0);
}

static bool kill_grant_to_count(sync_t *s) {
    actor_t *a = &s->actors[0]; a->gate = &s->gates[0];
    task_t *t = start(s, "sync credito", consume, a);
    if (!t || !wait_for(t->id, WAIT_SEM, a->gate)) return false;
    int id = t->id;
    uint64_t f = irq_save();
    sem_post(a->gate);
    bool granted = t->sem_granted;
    bool killed = stop(t);
    irq_restore(f);
    if (!granted || !killed || !collect(id, NULL) || a->done || !empty(a->gate, 1)) return false;
    sem_wait(a->gate);
    return empty(a->gate, 0);
}

static bool join_dead(sync_t *s) {
    actor_t *a = &s->actors[0];
    task_t *t = start(s, "sync metricas", finish, a);
    if (!t || !wait_for(t->id, WAIT_DEAD, NULL)) return false;
    int id = t->id;
    uint64_t blocks = task_current()->blocks;
    task_info_t info;
    return !task_join(task_current(), NULL) && collect(id, &info) && a->done == 1
           /* O gancho da GUI ao sair pode ceder a CPU para obter seu mutex. */
           && info.id == id && info.yields >= 3 && !info.blocks && info.runs > 0
           && info.finished_at >= info.started_at && task_current()->blocks == blocks;
}

static bool kill_joiner(sync_t *s) {
    actor_t *a = &s->actors[0], *j = &s->actors[1]; a->gate = &s->gates[0];
    task_t *target = start(s, "sync alvo", consume, a);
    if (!target || !wait_for(target->id, WAIT_SEM, a->gate)) return false;
    int tid = target->id; j->target = target;
    task_t *joiner = start(s, "sync joiner", join_worker, j);
    if (!joiner || !wait_for(joiner->id, WAIT_JOIN, target)) return false;
    int jid = joiner->id;
    if (task_join(target, NULL) || task_detach(target)) return false;
    if (!stop(joiner) || !collect(jid, NULL)) return false;
    uint64_t f = irq_save();
    bool released = !target->joiner;
    irq_restore(f);
    if (!released || j->done || j->joined) return false;
    sem_post(a->gate);
    return collect(tid, NULL) && a->done == 1 && empty(a->gate, 0);
}

static bool join_cycle(sync_t *s) {
    actor_t *a = &s->actors[0], *b = &s->actors[1];
    a->gate = &s->gates[0]; b->gate = &s->gates[1];
    task_t *ta = start(s, "sync ciclo A", join_worker, a);
    task_t *tb = start(s, "sync ciclo B", join_worker, b);
    if (!ta || !tb) return false;
    a->target = tb; b->target = ta;
    int aid = ta->id, bid = tb->id;
    sem_post(a->gate);
    if (!wait_for(aid, WAIT_JOIN, tb)) return false;
    /* Quem executa a suíte ainda não reclama A: B é recusado pelo ciclo, não
     * pela existência de um segundo joiner. A recolhe B antes de terminar. */
    sem_post(b->gate);
    return collect(aid, NULL) && a->joined == 1 && b->joined == -1
           && a->done == 1 && b->done == 1 && wait_for(bid, WAIT_GONE, NULL)
           && empty(a->gate, 0) && empty(b->gate, 0);
}

static bool detach_dead(sync_t *s) {
    task_t *t = start(s, "sync detach", finish, &s->actors[0]);
    if (!t || !wait_for(t->id, WAIT_DEAD, NULL)) return false;
    int id = t->id;
    if (!task_detach(t)) return false;
    return wait_for(id, WAIT_GONE, NULL) && s->actors[0].done == 1;
}

static bool cleanup(sync_t *s) {
    /* IDs continuam válidos para busca mesmo quando outro worker já consumiu
     * um handle. Primeiro paramos todos, inclusive possíveis joiners. */
    for (int i = 0; i < s->nids; i++) {
        uint64_t f = irq_save();
        task_t *t = task_find(s->ids[i]);
        if (t && t->state != TASK_DEAD) stop(t);
        irq_restore(f);
    }
    bool safe = true;
    for (int i = 0; i < s->nids; i++) {
        uint64_t f = irq_save();
        task_t *t = task_find(s->ids[i]);
        bool live = t && t->state != TASK_DEAD;
        irq_restore(f);
        if (live && !wait_for(s->ids[i], WAIT_DEAD, NULL)) safe = false;
    }
    if (!safe) return false;       /* não liberar memória que um worker ainda usa */
    for (int i = 0; i < s->nids; i++) {
        uint64_t f = irq_save();
        task_t *t = task_find(s->ids[i]);
        if (t && t->joinable && !task_join(t, NULL)) safe = false;
        irq_restore(f);
    }
    return safe;
}

void lab_sync_test(lab_out_t *o) {
    static const struct { const char *name; bool (*run)(sync_t *s); } tests[] = {
        { "post antes de wait conserva um crédito", post_first },
        { "wait antes de post bloqueia e recebe uma vez", wait_first },
        { "wake sem crédito volta a bloquear", wake_without_credit },
        { "kill enfileirado preserva o próximo waiter", kill_queued },
        { "kill com grant transfere o crédito ao próximo", kill_granted },
        { "kill com grant sem waiter devolve a contagem", kill_grant_to_count },
        { "join morto coleta métricas finais sem bloquear", join_dead },
        { "claim exclusivo e kill do joiner liberam o alvo", kill_joiner },
        { "ciclo de joins é recusado sem travar as tarefas", join_cycle },
        { "detach permite reap automático", detach_dead },
    };
    /* A proteção antecede a alocação: até uma falha de criação ou um kill
     * pendente precisa passar pelo mesmo caminho de coleta e liberação. */
    mutex_t pin = {0};
    mutex_lock(&pin);
    sync_t *s = kcalloc(1, sizeof(*s));
    int total = 0, failed = 0;
    if (!s) {
        o->write(o->ctx, LAB_RED, "Sincronização: memória insuficiente para os testes.\n");
        kprintf("sincronizacao: testes FALHOU 0\n");
        mutex_unlock(&pin);
        return;
    }
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        memset(s->actors, 0, sizeof(s->actors));
        for (int g = 0; g < 2; g++) sem_init(&s->gates[g], 0);
        bool ok = tests[i].run(s);
        total++;
        char line[180];
        snprintf(line, sizeof(line), "%s %s\n", ok ? "OK" : "FALHOU", tests[i].name);
        o->write(o->ctx, ok ? LAB_GREEN : LAB_RED, line);
        kprintf("sincronizacao: %s", line);
        if (!ok) { failed++; break; }
    }
    bool safe = cleanup(s);
    if (!safe) failed++;
    if (safe) kfree(s);
    char summary[160];
    snprintf(summary, sizeof(summary), "Sincronização: %d casos, %d falha(s).\n", total, failed);
    o->write(o->ctx, failed ? LAB_RED : LAB_GREEN, summary);
    kprintf("sincronizacao: testes %s %d\n", failed ? "FALHOU" : "OK", total);
    mutex_unlock(&pin);
}
