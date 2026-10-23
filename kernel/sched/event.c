/*
 * Fila de eventos por tarefa (buffer circular).
 *
 * Eventos repetitivos são fundidos para a fila não transbordar:
 * vários EV_MOUSE_MOVE seguidos viram um só (vale a posição mais recente),
 * e só existe um EV_RESIZE / EV_TIMER / EV_WINLIST pendente por vez.
 */
#include "sched/event.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"

static bool coalescible(uint16_t type) {
    return type == EV_MOUSE_MOVE || type == EV_RESIZE || type == EV_WINLIST;
}

bool ev_post(task_t *t, const event_t *e) {
    if (!t || t->state == TASK_DEAD) return false;
    uint64_t f = irq_save();

    if (coalescible(e->type)) {
        for (int i = 0; i < t->evq_count; i++) {
            event_t *q = &t->evq[(t->evq_head + i) % EVQ_SIZE];
            if (q->type == e->type && q->win == e->win) {
                /* movimento só funde se for o último da fila (preserva a ordem com cliques) */
                if (e->type != EV_MOUSE_MOVE || i == t->evq_count - 1) {
                    *q = *e;
                    irq_restore(f);
                    return true;
                }
            }
        }
    }

    if (t->evq_count == EVQ_SIZE) {
        irq_restore(f);
        return false;                 /* fila cheia: descarta */
    }
    t->evq[(t->evq_head + t->evq_count) % EVQ_SIZE] = *e;
    t->evq_count++;
    if (t->waiting_event) task_wake(t);
    irq_restore(f);
    return true;
}

bool ev_poll(event_t *e) {
    task_t *t = task_current();
    uint64_t f = irq_save();
    if (!t->evq_count) {
        irq_restore(f);
        return false;
    }
    *e = t->evq[t->evq_head];
    t->evq_head = (t->evq_head + 1) % EVQ_SIZE;
    t->evq_count--;
    if (e->type == EV_TIMER) t->timer_pending = false;
    irq_restore(f);
    return true;
}

bool ev_wait(event_t *e) {
    task_t *t = task_current();
    for (;;) {
        if (ev_poll(e)) return true;
        uint64_t f = irq_save();
        if (!t->evq_count) {
            t->waiting_event = true;
            t->state = TASK_BLOCKED;
            task_yield();
            t->waiting_event = false;
        }
        irq_restore(f);
    }
}

void ev_set_timer(uint32_t interval_ms) {
    task_t *t = task_current();
    uint64_t f = irq_save();
    t->timer_interval = interval_ms;
    t->timer_next = timer_ms() + interval_ms;
    irq_restore(f);
}
