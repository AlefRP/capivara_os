/*
 * PIC 8259 (controlador de interrupções programável), mestre + escravo.
 * Por padrão as IRQs 0-7 usam os vetores 8-15, que colidem com exceções da
 * CPU; por isso "remapeamos" para 0x20-0x2F.
 */
#include "arch/x86_64/pic.h"
#include "arch/x86_64/cpu.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init(void) {
    outb(PIC1_CMD, 0x11);  io_wait();   /* ICW1: inicializar, ICW4 virá */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, 0x20); io_wait();   /* ICW2: vetor base do mestre */
    outb(PIC2_DATA, 0x28); io_wait();   /*        vetor base do escravo */
    outb(PIC1_DATA, 4);    io_wait();   /* ICW3: escravo ligado na IRQ2 */
    outb(PIC2_DATA, 2);    io_wait();
    outb(PIC1_DATA, 0x01); io_wait();   /* ICW4: modo 8086 */
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFB);              /* tudo mascarado, exceto a cascata (IRQ2) */
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}

void pic_mask(int irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) | (1 << irq));
}

void pic_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

/* IRQ7/IRQ15 podem ser "fantasmas": o PIC sinaliza mas nenhum bit está em
 * serviço (registrador ISR). Nesse caso não se manda EOI (ou só ao mestre). */
bool pic_is_spurious(int irq) {
    if (irq == 7) {
        outb(PIC1_CMD, 0x0B);
        return !(inb(PIC1_CMD) & 0x80);
    }
    if (irq == 15) {
        outb(PIC2_CMD, 0x0B);
        if (!(inb(PIC2_CMD) & 0x80)) {
            outb(PIC1_CMD, 0x20);
            return true;
        }
    }
    return false;
}
