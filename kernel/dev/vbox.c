/*
 * Integração com o VirtualBox (dispositivo PCI "VMMDev", 80EE:CAFE).
 *
 * O convidado monta uma "requisição" na memória e escreve o endereço FÍSICO
 * dela na porta de E/S do dispositivo; o VirtualBox lê, executa e escreve a
 * resposta na mesma estrutura. Usamos estas requisições:
 *   - informar a versão do protocolo do convidado (obrigatório no início);
 *   - dizer que sabemos receber a posição ABSOLUTA do mouse;
 *   - esconder o ponteiro do hospedeiro (nós desenhamos o nosso);
 *   - ler a posição do cursor.
 *
 * A posição é lida 100 vezes por segundo pelo relógio do sistema (e também na
 * interrupção do dispositivo, quando ela chega). Ler periodicamente é mais
 * robusto: não depende do roteamento da interrupção PCI, que varia conforme a
 * configuração da máquina virtual.
 *
 * Os botões continuam chegando pelo mouse PS/2; daqui vem só a posição.
 */
#include "dev/vbox.h"
#include "dev/pci.h"
#include "dev/ps2.h"
#include "dev/input.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pit.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "lib/printf.h"
#include "lib/string.h"

#define REQ_HEADER_VERSION   0x10001
#define REQ_GET_MOUSE        1
#define REQ_SET_MOUSE        2
#define REQ_SET_POINTER      3
#define REQ_ACK_EVENTS       41
#define REQ_FILTER_MASK      42
#define REQ_GUEST_INFO       50
#define VMMDEV_VERSION       0x00010003

#define MOUSE_GUEST_CAN_ABSOLUTE  (1 << 0)
#define MOUSE_HOST_WANTS_ABSOLUTE (1 << 1)
#define MOUSE_NEW_PROTOCOL        (1 << 4)
#define EVENT_MOUSE_POSITION      (1 << 9)

#define POLL_MS 10

struct header {
    uint32_t size, version, type;
    int32_t rc;
    uint32_t reserved1, reserved2;
} __attribute__((packed));

struct guest_info { struct header h; uint32_t version, ostype; } __attribute__((packed));
struct mouse_req  { struct header h; uint32_t features; int32_t x, y; } __attribute__((packed));
struct ack_req    { struct header h; uint32_t events; } __attribute__((packed));
struct filter_req { struct header h; uint32_t or_mask, not_mask; } __attribute__((packed));
struct pointer_req {
    struct header h;
    uint32_t flags, xhot, yhot, width, height;
    uint8_t data[4];
} __attribute__((packed));

static uint16_t port;
static uint64_t req_phys;
static uint8_t *req;          /* uma página só para as requisições */
static bool active;
static int32_t last_x = -1, last_y = -1;
static uint64_t irq_count;

static void send(void *r, uint32_t size, uint32_t type) {
    struct header *h = r;
    h->size = size;
    h->version = REQ_HEADER_VERSION;
    h->type = type;
    h->rc = 0x7FFF0000;                 /* sentinela: "não processada" */
    h->reserved1 = h->reserved2 = 0;
    outl(port, (uint32_t)(req_phys + ((uint8_t *)r - req)));
}

/* Lê a posição e, se mudou, envia um evento absoluto ao compositor.
 * Só roda dentro de interrupções (que não se aninham), então o buffer de
 * requisição compartilhado é seguro. */
static void read_position(void) {
    struct mouse_req *m = (struct mouse_req *)(req + 256);
    memset(m, 0, sizeof(*m));
    send(m, sizeof(*m), REQ_GET_MOUSE);
    if (m->h.rc < 0 || !(m->features & MOUSE_HOST_WANTS_ABSOLUTE)) {
        /* integração desligada no VirtualBox: volta ao mouse PS/2 relativo */
        mouse_set_external_absolute(false);
        last_x = last_y = -1;
        return;
    }
    mouse_set_external_absolute(true);
    int32_t x = m->x & 0xFFFF, y = m->y & 0xFFFF;
    if (x == last_x && y == last_y) return;
    last_x = x;
    last_y = y;
    input_event_t e = { .type = INPUT_MOUSE, .absolute = true, .ax = x, .ay = y };
    e.buttons = mouse_buttons();
    input_push(&e);
}

static void vbox_irq(regs_t *r) {
    struct ack_req *a = (struct ack_req *)(req + 512);
    memset(a, 0, sizeof(*a));
    send(a, sizeof(*a), REQ_ACK_EVENTS);
    if (!irq_count++) kprintf("vbox: primeira interrupção recebida\n");
    if (a->events & EVENT_MOUSE_POSITION) read_position();
}

static void vbox_tick(uint64_t now) {
    if (now % POLL_MS == 0) read_position();
}

void vbox_poll_mouse(void) {
    if (active) read_position();
}

void vbox_init(void) {
    pci_device_t d;
    if (!pci_find(0x80EE, 0xCAFE, &d)) return;
    uint32_t bar0 = pci_bar(&d, 0);
    if (!(bar0 & 1)) return;                       /* esperávamos uma porta de E/S */
    port = bar0 & 0xFFFC;
    pci_enable(&d);

    req_phys = pmm_alloc_zero();
    if (!req_phys || req_phys >= (1ULL << 32)) return;
    req = phys_to_virt(req_phys);

    struct guest_info *gi = (struct guest_info *)req;
    gi->version = VMMDEV_VERSION;
    gi->ostype = 0;
    send(gi, sizeof(*gi), REQ_GUEST_INFO);
    if (gi->h.rc < 0) {
        kprintf("vbox: o VMMDev recusou o protocolo (rc=%d)\n", gi->h.rc);
        return;
    }

    struct filter_req *f = (struct filter_req *)(req + 128);
    f->or_mask = EVENT_MOUSE_POSITION;
    f->not_mask = 0;
    send(f, sizeof(*f), REQ_FILTER_MASK);

    struct mouse_req *m = (struct mouse_req *)(req + 256);
    memset(m, 0, sizeof(*m));
    m->features = MOUSE_GUEST_CAN_ABSOLUTE | MOUSE_NEW_PROTOCOL;
    send(m, sizeof(*m), REQ_SET_MOUSE);

    /* ponteiro do hospedeiro invisível: só o nosso cursor aparece */
    struct pointer_req *p = (struct pointer_req *)(req + 1024);
    memset(p, 0, sizeof(*p));
    send(p, sizeof(*p), REQ_SET_POINTER);

    active = true;
    if (d.irq && d.irq < 16) irq_register(d.irq, vbox_irq);
    pit_add_hook(vbox_tick);
    kprintf("vbox: integração de mouse ativa (porta %x, IRQ %u)\n", port, d.irq);
}
