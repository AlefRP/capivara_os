/*
 * Driver de som AC'97 (Intel ICH e compatíveis): é a placa que o VirtualBox
 * ("ICH AC97") e o QEMU ("-device AC97") emulam.
 *
 * O controlador tem duas faixas de portas de E/S:
 *   NAM  (BAR0): o mixer do codec, o chip que converte os números em sinal
 *                analógico: volumes e taxa de amostragem;
 *   NABM (BAR1): o "bus master", que copia o som da RAM para o codec por DMA,
 *                sem a CPU.
 *
 * A saída (PCM out) segue uma lista de 32 descritores, a BDL. Cada descritor
 * aponta para um pedaço de som na memória FÍSICA. O controlador toca os
 * pedaços em círculo, do CIV (o que toca agora) até o LVI (o último que o
 * driver marcou como pronto). Quem escreve vai enchendo pedaços à frente do
 * CIV e avançando o LVI. Se o som acaba (o pedaço do LVI termina antes do
 * próximo chegar), o DMA para, e a próxima escrita reinicia do zero.
 *
 *        CIV            LVI   wr
 *         v              v    v
 *   [ ][#][#][#][#][#][#][#][~][ ][ ] ...   # = na fila, ~ = enchendo
 *
 * Não usamos interrupções: a fila é curta (32 × ~10,7 ms) e quem toca som,
 * como um jogo, escreve a cada quadro e consulta audio_queued() para manter
 * a fila no tamanho certo.
 */
#include "dev/audio.h"
#include "dev/pci.h"
#include "arch/x86_64/cpu.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/task.h"
#include "lib/printf.h"
#include "lib/string.h"

/* mixer (NAM) */
#define NAM_RESET     0x00
#define NAM_MASTER    0x02    /* atenuação: bits 12..8 esquerdo, 4..0 direito; bit 15 = mudo */
#define NAM_PCM_OUT   0x18    /* ganho do PCM: 0x0808 = 0 dB */
#define NAM_EXT_ID    0x28    /* bit 0: taxa variável (VRA) */
#define NAM_EXT_CTRL  0x2A
#define NAM_PCM_RATE  0x2C

/* bus master (NABM): a "caixa" da saída PCM começa em 0x10 */
#define PO_BDBAR      0x10    /* endereço físico da BDL */
#define PO_CIV        0x14    /* descritor que está tocando */
#define PO_LVI        0x15    /* último descritor válido */
#define PO_SR         0x16    /* estado */
#define PO_PICB       0x18    /* amostras que faltam no descritor atual */
#define PO_CR         0x1B    /* controle */
#define GLOB_CNT      0x2C
#define GLOB_STA      0x30

#define SR_DCH        0x01    /* o DMA está parado */
#define SR_EVENTS     0x1C    /* "aconteceu": escrever 1 limpa */
#define CR_RUN        0x01
#define CR_RESET      0x02
#define GLOB_COLD     0x02    /* 1 = tira o codec do reset */
#define STA_CODEC_OK  0x100   /* codec primário pronto */
#define BDL_BUP       0x4000  /* depois do último pedaço, manda silêncio */

#define BDL_N         32
#define BUF_FRAMES    512                 /* ~10,7 ms por pedaço a 48 kHz */
#define BUF_BYTES     (BUF_FRAMES * 4)    /* 2 canais × 16 bits */

typedef struct __attribute__((packed)) {
    uint32_t addr;            /* físico, abaixo de 4 GiB */
    uint16_t samples;         /* em amostras de 16 bits (2 por quadro estéreo) */
    uint16_t flags;
} bdl_entry_t;

static bool present;
static char name[40];
static uint16_t nam, nabm;
static bdl_entry_t *bdl;
static uint64_t bdl_phys;
static int16_t *buf[BDL_N];
static int wr;                /* pedaço que está sendo enchido */
static int fill;              /* quadros que já estão nele */
static bool running;
static int volume = 80;
static mutex_t lock;

bool audio_present(void) { return present; }
const char *audio_name(void) { return present ? name : NULL; }
int audio_volume(void) { return volume; }

/* Espera curta e limitada. Aqui ainda pode ser cedo demais para o relógio
 * (as interrupções só ligam depois do boot), então contamos acessos de E/S,
 * que levam ~1 µs cada. */
static bool wait_until(uint16_t port, uint32_t mask, bool set, int us) {
    for (int i = 0; i < us; i++) {
        bool on = (mask > 0xFF ? inl(port) : inb(port)) & mask;
        if (on == set) return true;
        io_wait();
    }
    return false;
}

/* Para o DMA e zera os registradores da caixa de saída: CIV e LVI voltam a 0. */
static void po_reset(void) {
    outb(nabm + PO_CR, 0);
    wait_until(nabm + PO_SR, SR_DCH, true, 10000);
    outb(nabm + PO_CR, CR_RESET);
    wait_until(nabm + PO_CR, CR_RESET, false, 10000);
    outl(nabm + PO_BDBAR, (uint32_t)bdl_phys);
    outw(nabm + PO_SR, SR_EVENTS);
    running = false;
}

/* O som acabou (o DMA parou): recomeça do descritor 0, levando junto o
 * pedaço que estava pela metade. */
static void restart(void) {
    po_reset();
    if (fill && wr) memcpy(buf[0], buf[wr], (size_t)fill * 4);
    wr = 0;
}

/* Quantos pedaços já foram entregues ao DMA e ainda não acabaram de tocar
 * (contando o que toca agora). */
static int busy_bufs(void) {
    if (!running) return wr;
    if (inw(nabm + PO_SR) & SR_DCH) return 0;
    return (wr - (inb(nabm + PO_CIV) & (BDL_N - 1)) + BDL_N) % BDL_N;
}

/* O pedaço "wr" encheu: vira o novo último válido. O DMA só começa com dois
 * pedaços na fila, para não acabar logo no primeiro. */
static void submit(void) {
    outb(nabm + PO_LVI, (uint8_t)wr);
    wr = (wr + 1) % BDL_N;
    fill = 0;
    outw(nabm + PO_SR, SR_EVENTS);
    if (!running && wr >= 2) {
        outb(nabm + PO_CR, CR_RUN);
        running = true;
    }
}

size_t audio_write(const int16_t *frames, size_t n) {
    if (!present) return 0;
    mutex_lock(&lock);
    if (running && (inw(nabm + PO_SR) & SR_DCH)) restart();
    size_t done = 0;
    while (done < n && busy_bufs() < BDL_N - 1) {
        size_t k = BUF_FRAMES - fill;
        if (k > n - done) k = n - done;
        memcpy(buf[wr] + fill * 2, frames + done * 2, k * 4);
        fill += (int)k;
        done += k;
        if (fill == BUF_FRAMES) submit();
    }
    mutex_unlock(&lock);
    return done;
}

size_t audio_queued(void) {
    if (!present) return 0;
    mutex_lock(&lock);
    size_t q = (size_t)fill;
    int busy = busy_bufs();
    if (!running) q += (size_t)busy * BUF_FRAMES;
    else if (busy) q += (size_t)(busy - 1) * BUF_FRAMES + inw(nabm + PO_PICB) / 2;
    mutex_unlock(&lock);
    return q;
}

/* O mixer mede volume como atenuação, em passos de 1,5 dB (0 = máximo).
 * Usamos 0 a 31, que todo codec aceita: de 0 dB a -46,5 dB. */
void audio_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume = percent;
    if (!present) return;
    uint16_t att = (uint16_t)((100 - percent) * 31 / 100);
    outw(nam + NAM_MASTER, percent ? (uint16_t)(att << 8 | att) : 0x8000);
}

/* Procura uma placa de som "AC'97" (classe 04h, multimídia; subclasse 01h, áudio). */
static bool find_card(pci_device_t *out) {
    pci_device_t list[64];
    int n = pci_scan(list, 64);
    for (int i = 0; i < n; i++)
        if (list[i].class_code == 0x04 && list[i].subclass == 0x01) {
            *out = list[i];
            return true;
        }
    return false;
}

void audio_init(void) {
    pci_device_t d;
    if (!find_card(&d)) {
        kprintf("som: nenhuma placa AC'97\n");
        return;
    }
    uint32_t b0 = pci_bar(&d, 0), b1 = pci_bar(&d, 1);
    if (!(b0 & 1) || !(b1 & 1)) {
        kprintf("som: placa %04x:%04x sem portas de E/S\n", d.vendor, d.device);
        return;
    }
    nam = (uint16_t)(b0 & ~3u);
    nabm = (uint16_t)(b1 & ~3u);
    pci_enable(&d);

    /* tira o codec do reset e espera ele avisar que está pronto */
    outl(nabm + GLOB_CNT, GLOB_COLD);
    if (!wait_until(nabm + GLOB_STA, STA_CODEC_OK, true, 200000)) {
        kprintf("som: o codec não respondeu\n");
        return;
    }
    outw(nam + NAM_RESET, 0);
    outw(nam + NAM_PCM_OUT, 0x0808);
    /* 48 kHz já é o padrão; com taxa variável, dizemos explicitamente */
    if (inw(nam + NAM_EXT_ID) & 1) {
        outw(nam + NAM_EXT_CTRL, inw(nam + NAM_EXT_CTRL) | 1);
        outw(nam + NAM_PCM_RATE, AUDIO_RATE);
    }

    /* Memória para o DMA: a BDL numa página e dois pedaços de 2 KiB por
     * página. O AC'97 só enxerga endereços de 32 bits. */
    bdl_phys = pmm_alloc_zero();
    if (!bdl_phys || bdl_phys >= 0x100000000ull) goto sem_memoria;
    bdl = phys_to_virt(bdl_phys);
    for (int i = 0; i < BDL_N; i += 2) {
        uint64_t p = pmm_alloc_zero();
        if (!p || p >= 0x100000000ull) goto sem_memoria;
        for (int k = 0; k < 2; k++) {
            buf[i + k] = (int16_t *)((uint8_t *)phys_to_virt(p) + k * BUF_BYTES);
            bdl[i + k].addr = (uint32_t)(p + k * BUF_BYTES);
            bdl[i + k].samples = BUF_FRAMES * 2;
            bdl[i + k].flags = BDL_BUP;
        }
    }
    po_reset();
    present = true;
    snprintf(name, sizeof(name), "AC'97 (%04x:%04x)", d.vendor, d.device);
    audio_set_volume(volume);
    kprintf("som: %s, portas %x/%x, %d Hz estéreo\n", name, nam, nabm, AUDIO_RATE);
    return;

sem_memoria:
    kprintf("som: sem memória abaixo de 4 GiB para o DMA\n");
}
