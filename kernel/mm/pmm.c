/*
 * Alocador de páginas físicas com bitmap (1 bit por página; 1 = ocupada).
 *
 * Estratégia de inicialização:
 *   1. marca TUDO como ocupado;
 *   2. libera as regiões que o mapa de memória diz serem RAM disponível;
 *   3. volta a ocupar o que já está em uso: o primeiro 1 MiB, o kernel,
 *      a estrutura do GRUB, os módulos e o próprio bitmap.
 */
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "boot/bootinfo.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "panic.h"

#define MAX_PHYS (64ULL << 30)   /* suportamos até 64 GiB */

extern char _kernel_end[];

static uint8_t *bitmap;
static uint64_t bitmap_pages;    /* nº de páginas cobertas pelo bitmap */
static uint64_t free_pages, total_pages;
static uint64_t max_phys;
static uint64_t next_hint;

static inline void set_used(uint64_t p) { bitmap[p / 8] |= 1 << (p % 8); }
static inline void set_free(uint64_t p) { bitmap[p / 8] &= ~(1 << (p % 8)); }
static inline int  is_used(uint64_t p)  { return bitmap[p / 8] & (1 << (p % 8)); }

static void mark_range(uint64_t start, uint64_t end, int used) {
    for (uint64_t p = start / PAGE_SIZE; p < (end + PAGE_SIZE - 1) / PAGE_SIZE && p < bitmap_pages; p++) {
        if (used && !is_used(p)) { set_used(p); free_pages--; }
        else if (!used && is_used(p)) { set_free(p); free_pages++; }
    }
}

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

void pmm_init(void) {
    for (int i = 0; i < bootinfo.mmap_count; i++) {
        mem_region_t *r = &bootinfo.mmap[i];
        if (r->type != MEM_AVAILABLE) continue;
        uint64_t end = r->base + r->length;
        if (end > MAX_PHYS) end = MAX_PHYS;
        if (end > max_phys) max_phys = end;
    }
    bitmap_pages = max_phys / PAGE_SIZE;
    uint64_t bitmap_bytes = align_up(bitmap_pages / 8 + 1, PAGE_SIZE);

    /* O bitmap vai logo depois de tudo que o GRUB carregou (kernel, info, módulos) */
    uint64_t kernel_end_phys = (uint64_t)_kernel_end - KERNEL_VMA;
    uint64_t place = kernel_end_phys;
    uint64_t info_end = bootinfo.info_phys + bootinfo.info_size;
    if (info_end > place) place = info_end;
    for (int i = 0; i < bootinfo.module_count; i++)
        if (bootinfo.modules[i].end > place) place = bootinfo.modules[i].end;
    place = align_up(place, PAGE_SIZE);
    if (place + bitmap_bytes > (1ULL << 30))
        panic("Sem espaço para o bitmap de memória abaixo de 1 GiB");

    bitmap = (uint8_t *)(KERNEL_VMA + place);
    memset(bitmap, 0xFF, bitmap_bytes);
    free_pages = 0;

    for (int i = 0; i < bootinfo.mmap_count; i++) {
        mem_region_t *r = &bootinfo.mmap[i];
        if (r->type != MEM_AVAILABLE) continue;
        uint64_t start = align_up(r->base, PAGE_SIZE);
        uint64_t end = (r->base + r->length) & ~(PAGE_SIZE - 1);
        if (end > max_phys) end = max_phys;
        if (end > start) mark_range(start, end, 0);
    }
    total_pages = free_pages;

    mark_range(0, 0x100000, 1);                                  /* BIOS, VGA, etc. */
    mark_range(0x100000, kernel_end_phys, 1);                    /* kernel */
    mark_range(bootinfo.info_phys, info_end, 1);                 /* info do GRUB */
    for (int i = 0; i < bootinfo.module_count; i++)
        mark_range(bootinfo.modules[i].start, bootinfo.modules[i].end, 1);
    mark_range(place, place + bitmap_bytes, 1);                  /* o bitmap */

    kprintf("pmm: %lu MiB de RAM, %lu MiB livres (bitmap em %p)\n",
            total_pages * PAGE_SIZE >> 20, free_pages * PAGE_SIZE >> 20, (void *)place);
}

uint64_t pmm_alloc(void) {
    uint64_t f = irq_save();
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        uint64_t p = (next_hint + i) % bitmap_pages;
        if (bitmap[p / 8] == 0xFF) { i += 7 - (p % 8); continue; }   /* byte cheio: pula */
        if (!is_used(p)) {
            set_used(p);
            free_pages--;
            next_hint = p + 1;
            irq_restore(f);
            return p * PAGE_SIZE;
        }
    }
    irq_restore(f);
    return 0;
}

uint64_t pmm_alloc_zero(void) {
    uint64_t p = pmm_alloc();
    if (p) memset(phys_to_virt(p), 0, PAGE_SIZE);
    return p;
}

void pmm_free(uint64_t phys) {
    uint64_t f = irq_save();
    uint64_t p = phys / PAGE_SIZE;
    if (p < bitmap_pages && is_used(p)) {
        set_free(p);
        free_pages++;
        if (p < next_hint) next_hint = p;
    }
    irq_restore(f);
}

uint64_t pmm_total_bytes(void) { return total_pages * PAGE_SIZE; }
uint64_t pmm_free_bytes(void)  { return free_pages * PAGE_SIZE; }
uint64_t pmm_max_phys(void)    { return max_phys; }
