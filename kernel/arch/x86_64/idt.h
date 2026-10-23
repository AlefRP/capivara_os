#pragma once
#include "arch/x86_64/cpu.h"

typedef void (*irq_handler_t)(regs_t *r);

void idt_init(void);
void irq_register(int irq, irq_handler_t handler);   /* também desmascara a IRQ */
