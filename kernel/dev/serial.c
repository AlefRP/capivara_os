/*
 * Porta serial COM1 (UART 16550). Primeira coisa a funcionar no boot: tudo que
 * o kernel registra com kprintf() sai aqui. No QEMU, "-serial stdio" mostra
 * esse texto no terminal do computador hospedeiro.
 */
#include "dev/serial.h"
#include "arch/x86_64/cpu.h"

#define COM1 0x3F8

static bool present;

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* sem interrupções */
    outb(COM1 + 3, 0x80);   /* DLAB = 1 para programar a velocidade */
    outb(COM1 + 0, 0x01);   /* divisor 1 = 115200 bps */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 bits, sem paridade, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* FIFO ligada */
    outb(COM1 + 4, 0x0B);
    present = inb(COM1 + 5) != 0xFF;
}

void serial_putc(char c) {
    if (!present) return;
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++)
        cpu_pause();
    outb(COM1, c);
}
