/*
 * Leitura da estrutura de informações Multiboot2: uma sequência de "tags"
 * {tipo, tamanho, dados...}, cada uma alinhada em 8 bytes, terminando no tipo 0.
 */
#include "boot/bootinfo.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"
#include "panic.h"

bootinfo_t bootinfo;

struct tag { uint32_t type, size; };

void bootinfo_parse(uint32_t magic, uint64_t info_phys) {
    if (magic != MB2_MAGIC)
        panic("Não fui carregado por um bootloader Multiboot2 (mágico=%x)", magic);

    /* O GRUB coloca a estrutura abaixo de 1 GiB, que está mapeado em KERNEL_VMA */
    uint8_t *base = (uint8_t *)(KERNEL_VMA + info_phys);
    uint32_t total = *(uint32_t *)base;
    bootinfo.info_phys = info_phys;
    bootinfo.info_size = total;

    for (uint8_t *p = base + 8; p < base + total; ) {
        struct tag *t = (struct tag *)p;
        if (t->type == 0) break;
        uint8_t *data = p + 8;

        switch (t->type) {
        case 1:   /* linha de comando */
            strlcpy(bootinfo.cmdline, (char *)data, sizeof(bootinfo.cmdline));
            break;
        case 2:   /* nome do bootloader */
            strlcpy(bootinfo.bootloader, (char *)data, sizeof(bootinfo.bootloader));
            break;
        case 3:   /* módulo (nosso initrd) */
            if (bootinfo.module_count < 8) {
                boot_module_t *m = &bootinfo.modules[bootinfo.module_count++];
                m->start = ((uint32_t *)data)[0];
                m->end = ((uint32_t *)data)[1];
                strlcpy(m->name, (char *)(data + 8), sizeof(m->name));
            }
            break;
        case 6: { /* mapa de memória */
            uint32_t entry_size = ((uint32_t *)data)[0];
            for (uint8_t *e = data + 8; e < p + t->size && bootinfo.mmap_count < 64; e += entry_size) {
                mem_region_t *r = &bootinfo.mmap[bootinfo.mmap_count++];
                r->base = *(uint64_t *)e;
                r->length = *(uint64_t *)(e + 8);
                r->type = *(uint32_t *)(e + 16);
            }
            break;
        }
        case 8:   /* framebuffer */
            bootinfo.fb_phys = *(uint64_t *)data;
            bootinfo.fb_pitch = *(uint32_t *)(data + 8);
            bootinfo.fb_width = *(uint32_t *)(data + 12);
            bootinfo.fb_height = *(uint32_t *)(data + 16);
            bootinfo.fb_bpp = data[20];
            bootinfo.has_fb = data[21] == 1;   /* tipo 1 = RGB direto */
            break;
        case 14:  /* ACPI RSDP 1.0 */
        case 15:  /* ACPI RSDP 2.0+ */
            if (!bootinfo.has_rsdp || t->type == 15) {
                memcpy(bootinfo.rsdp, data, sizeof(bootinfo.rsdp));
                bootinfo.has_rsdp = true;
            }
            break;
        }
        p += (t->size + 7) & ~7u;
    }
}

bool bootinfo_has_arg(const char *arg) {
    size_t n = strlen(arg);
    for (const char *s = bootinfo.cmdline; (s = strstr(s, arg)); s += n) {
        bool start_ok = s == bootinfo.cmdline || s[-1] == ' ';
        bool end_ok = s[n] == 0 || s[n] == ' ';
        if (start_ok && end_ok) return true;
    }
    return false;
}
