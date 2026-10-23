/*
 * PIT 8254: o relógio do sistema. Programado para 1000 interrupções por
 * segundo, então "ticks" = milissegundos desde o boot.
 */
#include "arch/x86_64/pit.h"
#include "arch/x86_64/idt.h"
#include "sched/task.h"

#define PIT_FREQ 1193182

static volatile uint64_t ticks;
static pit_hook_t hooks[4];
static int nhooks;

/* Funções chamadas a cada tick (dentro da interrupção: precisam ser rápidas). */
void pit_add_hook(pit_hook_t fn) {
    if (nhooks < 4) hooks[nhooks++] = fn;
}

static void pit_irq(regs_t *r) {
    ticks++;
    for (int i = 0; i < nhooks; i++) hooks[i](ticks);
    sched_tick(ticks);
}

void pit_init(void) {
    uint16_t divisor = PIT_FREQ / TIMER_HZ;
    outb(0x43, 0x34);                   /* canal 0, lo/hi, modo 2 (rate generator) */
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
    irq_register(0, pit_irq);
}

uint64_t timer_ms(void) {
    return ticks;
}
