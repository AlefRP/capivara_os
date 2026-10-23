/*
 * Desligar e reiniciar.
 *
 * Desligar exige ACPI: o firmware descreve o hardware em tabelas na memória.
 *   RSDP → RSDT/XSDT → FADT ("FACP") → endereço do registrador PM1a_CNT
 *                                   → DSDT (bytecode AML) contém o objeto \_S5
 * Escrever SLP_TYP (lido de \_S5) | SLP_EN em PM1a_CNT desliga a máquina.
 * Não temos um interpretador AML completo: procuramos o padrão de bytes de
 * "_S5_" no DSDT, técnica simples que funciona nas máquinas virtuais comuns.
 */
#include "dev/power.h"
#include "boot/bootinfo.h"
#include "mm/vmm.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"
#include "lib/printf.h"

struct sdt_header {
    char sig[4];
    uint32_t length;
    uint8_t rev, checksum;
    char oem[6], oem_table[8];
    uint32_t oem_rev, creator, creator_rev;
} __attribute__((packed));

static uint16_t pm1a_cnt, pm1b_cnt;
static uint16_t slp_typa, slp_typb;
static uint32_t smi_cmd;
static uint8_t acpi_enable_val;
static bool acpi_ok;

static struct sdt_header *find_table(const char *sig) {
    uint8_t *rsdp = bootinfo.rsdp;
    uint8_t rev = rsdp[15];
    if (rev >= 2) {
        uint64_t xsdt_phys = *(uint64_t *)(rsdp + 24);
        struct sdt_header *xsdt = phys_to_virt(xsdt_phys);
        int n = (xsdt->length - sizeof(*xsdt)) / 8;
        uint64_t *entries = (uint64_t *)(xsdt + 1);
        for (int i = 0; i < n; i++) {
            struct sdt_header *h = phys_to_virt(entries[i]);
            if (!memcmp(h->sig, sig, 4)) return h;
        }
    } else {
        uint32_t rsdt_phys = *(uint32_t *)(rsdp + 16);
        struct sdt_header *rsdt = phys_to_virt(rsdt_phys);
        int n = (rsdt->length - sizeof(*rsdt)) / 4;
        uint32_t *entries = (uint32_t *)(rsdt + 1);
        for (int i = 0; i < n; i++) {
            struct sdt_header *h = phys_to_virt(entries[i]);
            if (!memcmp(h->sig, sig, 4)) return h;
        }
    }
    return NULL;
}

/* Lê um inteiro AML pequeno: BytePrefix (0x0A) n, ou as constantes Zero/One */
static int aml_small_int(uint8_t **p) {
    uint8_t *s = *p;
    if (*s == 0x0A) { *p = s + 2; return s[1]; }
    if (*s == 0x00) { *p = s + 1; return 0; }
    if (*s == 0x01) { *p = s + 1; return 1; }
    *p = s + 1;
    return *s;
}

void acpi_init(void) {
    if (!bootinfo.has_rsdp) {
        kprintf("acpi: RSDP não encontrado\n");
        return;
    }
    uint8_t *fadt = (uint8_t *)find_table("FACP");
    if (!fadt) {
        kprintf("acpi: FADT não encontrada\n");
        return;
    }
    uint32_t dsdt_phys = *(uint32_t *)(fadt + 40);
    smi_cmd = *(uint32_t *)(fadt + 48);
    acpi_enable_val = fadt[52];
    pm1a_cnt = *(uint32_t *)(fadt + 64);
    pm1b_cnt = *(uint32_t *)(fadt + 68);

    struct sdt_header *dsdt = phys_to_virt(dsdt_phys);
    uint8_t *aml = (uint8_t *)(dsdt + 1);
    uint32_t len = dsdt->length - sizeof(*dsdt);
    for (uint32_t i = 0; i + 4 < len; i++) {
        if (memcmp(aml + i, "_S5_", 4)) continue;
        /* esperado: [08 NameOp | '\\'] _S5_ 12 PkgLength NumElements SLP_TYPa SLP_TYPb */
        uint8_t *p = aml + i + 4;
        if (*p != 0x12) continue;
        p++;
        p += ((*p & 0xC0) >> 6) + 1;          /* pula PkgLength (1 a 4 bytes) */
        p++;                                  /* NumElements */
        slp_typa = aml_small_int(&p);
        slp_typb = aml_small_int(&p);
        acpi_ok = true;
        break;
    }
    kprintf("acpi: PM1a_CNT=%x S5=%s (SLP_TYPa=%d)\n", pm1a_cnt, acpi_ok ? "ok" : "não achado", slp_typa);
}

void power_off(void) {
    kprintf("Desligando...\n");
    cli();
    if (acpi_ok && pm1a_cnt) {
        if (smi_cmd && acpi_enable_val && !(inw(pm1a_cnt) & 1)) {
            outb(smi_cmd, acpi_enable_val);          /* liga o modo ACPI */
            for (int i = 0; i < 1000000 && !(inw(pm1a_cnt) & 1); i++) cpu_pause();
        }
        outw(pm1a_cnt, (slp_typa << 10) | (1 << 13));
        if (pm1b_cnt) outw(pm1b_cnt, (slp_typb << 10) | (1 << 13));
    }
    /* Alternativas conhecidas de máquinas virtuais */
    outw(0x604, 0x2000);     /* QEMU */
    outw(0xB004, 0x2000);    /* Bochs e QEMU antigo */
    outw(0x4004, 0x3400);    /* VirtualBox */
    for (;;) hlt();
}

void power_reboot(void) {
    kprintf("Reiniciando...\n");
    cli();
    /* Pulso na linha de reset através do controlador de teclado */
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) cpu_pause();
    outb(0x64, 0xFE);
    /* Se não funcionou: "triple fault" com uma IDT vazia */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) empty = { 0, 0 };
    __asm__ volatile("lidt %0; int3" :: "m"(empty));
    for (;;) hlt();
}
