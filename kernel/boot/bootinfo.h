/* Informações entregues pelo GRUB, já copiadas para estruturas do kernel. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MB2_MAGIC 0x36D76289

#define MEM_AVAILABLE 1
#define MEM_ACPI_RECLAIM 3

typedef struct {
    uint64_t base, length;
    uint32_t type;
} mem_region_t;

typedef struct {
    uint64_t start, end;     /* físico */
    char name[64];
} boot_module_t;

typedef struct {
    mem_region_t mmap[64];
    int mmap_count;

    uint64_t fb_phys;
    uint32_t fb_width, fb_height, fb_pitch;
    uint8_t  fb_bpp;
    bool     has_fb;

    boot_module_t modules[8];
    int module_count;

    uint8_t rsdp[36];        /* cópia do ponteiro ACPI */
    bool has_rsdp;

    char cmdline[128];
    char bootloader[64];

    uint64_t info_phys, info_size;   /* a própria estrutura (para não sobrescrever) */
} bootinfo_t;

extern bootinfo_t bootinfo;

void bootinfo_parse(uint32_t magic, uint64_t info_phys);
bool bootinfo_has_arg(const char *arg);
