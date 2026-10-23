/*
 * IDT (Interrupt Descriptor Table) e despacho de interrupções.
 */
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/gdt.h"
#include "sched/task.h"
#include "panic.h"
#include "lib/printf.h"

struct idt_entry {
    uint16_t off_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

static struct idt_entry idt[256];
static irq_handler_t irq_handlers[16];

extern uint64_t isr_stub_table[48];
extern uint64_t isr_stub_syscall, isr_stub_yield;

static void set_gate(int vec, uint64_t handler, uint8_t ist, uint8_t dpl) {
    idt[vec].off_lo = handler & 0xFFFF;
    idt[vec].selector = SEL_KCODE;
    idt[vec].ist = ist;
    idt[vec].type_attr = 0x8E | (dpl << 5);   /* presente, interrupt gate de 64 bits */
    idt[vec].off_mid = (handler >> 16) & 0xFFFF;
    idt[vec].off_hi = handler >> 32;
    idt[vec].zero = 0;
}

void idt_init(void) {
    for (int i = 0; i < 48; i++)
        set_gate(i, isr_stub_table[i], i == 8 ? 1 : 0, 0);  /* double fault usa IST1 */
    set_gate(0x80, isr_stub_syscall, 0, 3);
    set_gate(0x81, isr_stub_yield, 0, 0);

    struct idtr idtr = { sizeof(idt) - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" :: "m"(idtr));
}

void irq_register(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

static const char *exception_names[32] = {
    "Divisão por zero", "Depuração", "NMI", "Breakpoint", "Overflow",
    "Fora dos limites", "Instrução inválida", "Dispositivo indisponível (FPU)",
    "Falha dupla (double fault)", "Coprocessor segment overrun", "TSS inválida",
    "Segmento ausente", "Falha na pilha", "Proteção geral (GPF)",
    "Falha de página (page fault)", "Reservada", "Erro de ponto flutuante x87",
    "Verificação de alinhamento", "Machine check", "Erro de ponto flutuante SIMD",
    "Virtualização", "Proteção de controle", "Reservada", "Reservada", "Reservada",
    "Reservada", "Reservada", "Reservada", "Injeção do hypervisor",
    "Comunicação VMM", "Segurança", "Reservada",
};

regs_t *isr_dispatch(regs_t *r) {
    if (r->int_no < 32) {
        panic_exception(exception_names[r->int_no], r);
        return r;   /* não chega aqui */
    }

    if (r->int_no < 48) {
        int irq = (int)r->int_no - 32;
        if (pic_is_spurious(irq))
            return r;
        if (irq_handlers[irq])
            irq_handlers[irq](r);
        pic_eoi(irq);
        /* Algum handler acordou uma tarefa ou a fatia de tempo acabou? */
        if (sched_need_resched())
            return sched_switch(r);
        return r;
    }

    if (r->int_no == 0x81)       /* task_yield() */
        return sched_switch(r);

    if (r->int_no == 0x80) {     /* chamadas de sistema: fase 2 (modo usuário) */
        kprintf("syscall %lu ainda não implementada\n", r->rax);
        r->rax = (uint64_t)-1;
        return r;
    }
    return r;
}
