/*
 * Mouse PS/2 (pacotes de 3 bytes, ou 4 com roda) e, quando disponível, o
 * protocolo "vmmouse" (porta de comunicação VMware, também emulada pelo QEMU)
 * que entrega a posição ABSOLUTA do cursor: o mouse entra e sai da janela da
 * máquina virtual sem precisar ser "capturado".
 *
 * Pacote PS/2:
 *   byte 0: bit0 esquerdo, bit1 direito, bit2 meio, bit3 sempre 1,
 *           bit4 sinal de X, bit5 sinal de Y
 *   byte 1: deslocamento X   byte 2: deslocamento Y (positivo = para cima)
 *   byte 3: roda (só no modo IntelliMouse)
 */
#include "dev/ps2.h"
#include "dev/input.h"
#include "arch/x86_64/idt.h"
#include "lib/printf.h"
#include "dev/vbox.h"

/* ---- vmmouse ---- */
#define VMW_MAGIC 0x564D5868
#define VMW_PORT  0x5658
#define VMW_CMD_GETVERSION   10
#define VMW_CMD_ABS_DATA     39
#define VMW_CMD_ABS_STATUS   40
#define VMW_CMD_ABS_COMMAND  41
#define VMMOUSE_READ_ID      0x45414552
#define VMMOUSE_REQ_ABSOLUTE 0x53424152
#define VMMOUSE_VERSION_ID   0x3442554A

static void vmware_call(uint32_t cmd, uint32_t arg, uint32_t out[4]) {
    uint32_t a, b, c, d;
    __asm__ volatile("inl %%dx, %%eax"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(VMW_MAGIC), "b"(arg), "c"(cmd), "d"(VMW_PORT) : "memory");
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
}

static bool vmmouse_active;
static bool has_wheel;
static uint8_t packet[4];
static int packet_len;

static bool vmmouse_init(void) {
    uint32_t r[4];
    vmware_call(VMW_CMD_GETVERSION, ~VMW_MAGIC, r);
    if (r[1] != VMW_MAGIC || r[0] == 0xFFFFFFFF) return false;

    vmware_call(VMW_CMD_ABS_COMMAND, VMMOUSE_READ_ID, r);
    vmware_call(VMW_CMD_ABS_STATUS, 0, r);
    if ((r[0] & 0xFFFF) == 0) return false;
    vmware_call(VMW_CMD_ABS_DATA, 1, r);
    if (r[0] != VMMOUSE_VERSION_ID) return false;

    vmware_call(VMW_CMD_ABS_COMMAND, VMMOUSE_REQ_ABSOLUTE, r);
    return true;
}

static void vmmouse_poll(void) {
    for (;;) {
        uint32_t r[4];
        vmware_call(VMW_CMD_ABS_STATUS, 0, r);
        if ((r[0] & 0xFFFF0000) == 0xFFFF0000) {      /* erro: reinicia o modo */
            vmmouse_init();
            return;
        }
        if ((r[0] & 0xFFFF) < 4) return;
        vmware_call(VMW_CMD_ABS_DATA, 4, r);
        input_event_t e = { .type = INPUT_MOUSE, .absolute = true };
        e.buttons = ((r[0] & 0x20) ? 1 : 0) | ((r[0] & 0x10) ? 2 : 0) | ((r[0] & 0x08) ? 4 : 0);
        e.ax = r[1] & 0xFFFF;
        e.ay = r[2] & 0xFFFF;
        e.wheel = -(int8_t)(r[3] & 0xFF);
        input_push(&e);
    }
}

/* Quando outra fonte (integração do VirtualBox) fornece a posição absoluta,
 * os pacotes PS/2 só informam botões e roda. */
static bool external_absolute;
static uint8_t last_buttons;

void mouse_set_external_absolute(bool on) { external_absolute = on; }
uint8_t mouse_buttons(void) { return last_buttons; }

static void handle_packet(void) {
    if (vmmouse_active) {
        vmmouse_poll();
        return;
    }
    uint8_t b0 = packet[0];
    if (b0 & 0xC0) return;                            /* overflow: descarta */
    input_event_t e = { .type = INPUT_MOUSE };
    e.buttons = last_buttons = b0 & 7;
    e.dx = (int)packet[1] - ((b0 << 4) & 0x100);
    e.dy = -((int)packet[2] - ((b0 << 3) & 0x100));   /* tela: Y cresce para baixo */
    if (has_wheel) e.wheel = -(int8_t)((packet[3] & 0x08) ? (packet[3] | 0xF0) : (packet[3] & 0x0F));
    vbox_poll_mouse();                                /* atualiza external_absolute */
    if (external_absolute) e.dx = e.dy = 0;
    input_push(&e);
}

void mouse_feed_byte(uint8_t b) {
    if (packet_len == 0 && !(b & 0x08)) return;       /* fora de sincronia */
    packet[packet_len++] = b;
    if (packet_len == (has_wheel ? 4 : 3)) {
        packet_len = 0;
        handle_packet();
    }
}

static void mouse_irq(regs_t *r) {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    uint8_t b = inb(0x60);
    if (status & 0x20) mouse_feed_byte(b);
}

void mouse_init(bool try_vmmouse) {
    if (!ps2_present()) return;                       /* cada comando esperaria o timeout */
    ps2_mouse_cmd(0xF6);                              /* configurações padrão */
    /* "Batida secreta" do IntelliMouse: taxas 200, 100, 80 ativam a roda */
    ps2_mouse_cmd(0xF3); ps2_mouse_cmd(200);
    ps2_mouse_cmd(0xF3); ps2_mouse_cmd(100);
    ps2_mouse_cmd(0xF3); ps2_mouse_cmd(80);
    uint8_t id = 0;
    if (ps2_mouse_cmd(0xF2)) ps2_read(&id);
    has_wheel = id == 3;
    ps2_mouse_cmd(0xF4);                              /* liga o envio de pacotes */

    if (try_vmmouse) vmmouse_active = vmmouse_init();
    irq_register(12, mouse_irq);
    kprintf("mouse: PS/2%s%s\n", has_wheel ? " com roda" : "",
            vmmouse_active ? " + vmmouse (posição absoluta)" : "");
}

bool mouse_is_absolute(void) { return vmmouse_active; }
