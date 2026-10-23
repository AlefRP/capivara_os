#include "dev/input.h"
#include "sched/task.h"

#define INPUT_QUEUE 256

static input_event_t queue[INPUT_QUEUE];
static volatile int head, count;
static task_t *consumer;

void input_set_consumer(task_t *t) { consumer = t; }

void input_push(const input_event_t *e) {
    uint64_t f = irq_save();
    /* movimentos relativos seguidos podem ser somados */
    if (e->type == INPUT_MOUSE && count) {
        input_event_t *last = &queue[(head + count - 1) % INPUT_QUEUE];
        if (last->type == INPUT_MOUSE && last->buttons == e->buttons && !e->wheel && !last->wheel
            && last->absolute == e->absolute) {
            if (e->absolute) { last->ax = e->ax; last->ay = e->ay; }
            else { last->dx += e->dx; last->dy += e->dy; }
            irq_restore(f);
            return;
        }
    }
    if (count < INPUT_QUEUE) {
        queue[(head + count) % INPUT_QUEUE] = *e;
        count++;
    }
    if (consumer) task_wake(consumer);
    irq_restore(f);
}

bool input_pop(input_event_t *e) {
    uint64_t f = irq_save();
    if (!count) {
        irq_restore(f);
        return false;
    }
    *e = queue[head];
    head = (head + 1) % INPUT_QUEUE;
    count--;
    irq_restore(f);
    return true;
}

bool input_pending(void) {
    return count > 0;
}
