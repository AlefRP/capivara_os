#include "dev/ps2.h"
#include "arch/x86_64/cpu.h"
#include "lib/printf.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

static bool wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(PS2_STATUS) & 2)) return true;
    return false;
}

static bool wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS) & 1) return true;
    return false;
}

void ps2_cmd(uint8_t cmd)    { wait_write(); outb(PS2_STATUS, cmd); }
void ps2_write(uint8_t data) { wait_write(); outb(PS2_DATA, data); }

bool ps2_read(uint8_t *out) {
    if (!wait_read()) return false;
    *out = inb(PS2_DATA);
    return true;
}

bool ps2_mouse_cmd(uint8_t data) {
    ps2_cmd(0xD4);              /* próximo byte vai para o 2º dispositivo (mouse) */
    ps2_write(data);
    uint8_t ack;
    return ps2_read(&ack) && ack == 0xFA;
}

static bool present;

bool ps2_present(void) { return present; }

void ps2_init(void) {
    /* PCs só-UEFI podem não ter o 8042: a porta de status "flutua" em 0xFF.
     * Sem esse teste, o laço que esvazia o buffer nunca terminaria. */
    if (inb(PS2_STATUS) == 0xFF) {
        kprintf("ps2: controlador ausente\n");
        return;
    }
    ps2_cmd(0xAD);              /* desliga teclado */
    ps2_cmd(0xA7);              /* desliga mouse */
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & 1); i++) inb(PS2_DATA);   /* esvazia o buffer */

    ps2_cmd(0x20);              /* lê o byte de configuração */
    uint8_t cfg = 0;
    ps2_read(&cfg);
    cfg |= 0x01 | 0x02;         /* interrupções do teclado (IRQ1) e mouse (IRQ12) */
    cfg |= 0x40;                /* tradução para scancode set 1 */
    cfg &= ~0x30;               /* liga o clock das duas portas */
    ps2_cmd(0x60);
    ps2_write(cfg);

    ps2_cmd(0xAE);              /* liga teclado */
    ps2_cmd(0xA8);              /* liga mouse */
    present = true;
    kprintf("ps2: controlador configurado (cfg=%x)\n", cfg);
}
