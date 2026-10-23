/*
 * Tabelas de página x86_64: PML4 → PDPT → PD → PT → página de 4 KiB.
 * Cada nível tem 512 entradas de 8 bytes; 9 bits do endereço virtual
 * escolhem a entrada em cada nível:
 *
 *   47      39 38      30 29      21 20      12 11         0
 *   [ PML4   ][  PDPT   ][   PD    ][   PT    ][ deslocamento]
 */
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "boot/bootinfo.h"
#include "arch/x86_64/cpu.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "panic.h"

extern uint64_t boot_pml4[512];
static uint64_t *kernel_pml4 = boot_pml4;
static bool physmap_ready;

void *phys_to_virt(uint64_t phys) {
    if (physmap_ready) return (void *)(PHYSMAP_BASE + phys);
    ASSERT(phys < (1ULL << 30));          /* antes do physmap, só o 1º GiB */
    return (void *)(KERNEL_VMA + phys);
}

/* Devolve a tabela do próximo nível, criando-a se não existir. */
static uint64_t *next_level(uint64_t *table, int index, bool create) {
    if (!(table[index] & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = pmm_alloc_zero();
        if (!phys) return NULL;
        table[index] = phys | PTE_PRESENT | PTE_WRITE;
    }
    ASSERT(!(table[index] & PTE_HUGE));
    return phys_to_virt(table[index] & PTE_ADDR);
}

bool vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t f = irq_save();
    uint64_t *pdpt = next_level(kernel_pml4, (virt >> 39) & 511, true);
    uint64_t *pd   = pdpt ? next_level(pdpt, (virt >> 30) & 511, true) : NULL;
    uint64_t *pt   = pd   ? next_level(pd,   (virt >> 21) & 511, true) : NULL;
    if (pt) {
        pt[(virt >> 12) & 511] = (phys & PTE_ADDR) | flags | PTE_PRESENT;
        invlpg(virt);
    }
    irq_restore(f);
    return pt != NULL;
}

void vmm_unmap(uint64_t virt) {
    uint64_t f = irq_save();
    uint64_t *pdpt = next_level(kernel_pml4, (virt >> 39) & 511, false);
    uint64_t *pd   = pdpt ? next_level(pdpt, (virt >> 30) & 511, false) : NULL;
    uint64_t *pt   = pd   ? next_level(pd,   (virt >> 21) & 511, false) : NULL;
    if (pt) {
        pt[(virt >> 12) & 511] = 0;
        invlpg(virt);
    }
    irq_restore(f);
}

/* Mapeia [0, limite) em PHYSMAP_BASE usando páginas grandes de 2 MiB. */
static void build_physmap(uint64_t limit) {
    for (uint64_t addr = 0; addr < limit; addr += 2ULL << 20) {
        uint64_t virt = PHYSMAP_BASE + addr;
        uint64_t *pdpt = next_level(kernel_pml4, (virt >> 39) & 511, true);
        uint64_t *pd = next_level(pdpt, (virt >> 30) & 511, true);
        if (!pd) panic("Sem memória para montar o physmap");
        pd[(virt >> 21) & 511] = addr | PTE_PRESENT | PTE_WRITE | PTE_HUGE | PTE_GLOBAL;
    }
}

void vmm_init(void) {
    /* Cobre toda a RAM e, no mínimo, 4 GiB (onde ficam framebuffer, ACPI, APIC) */
    uint64_t limit = pmm_max_phys();
    if (limit < (4ULL << 30)) limit = 4ULL << 30;
    if (bootinfo.has_fb) {
        uint64_t fb_end = bootinfo.fb_phys + (uint64_t)bootinfo.fb_pitch * bootinfo.fb_height;
        if (fb_end > limit) limit = fb_end;
    }
    limit = (limit + (1ULL << 30) - 1) & ~((1ULL << 30) - 1);
    build_physmap(limit);
    physmap_ready = true;

    /* Remove o mapeamento identidade do boot: a partir de agora, ponteiro nulo
     * ou endereço baixo gera page fault em vez de corromper memória. */
    kernel_pml4[0] = 0;
    write_cr3(read_cr3());

    kprintf("vmm: physmap de %lu GiB em %p\n", limit >> 30, (void *)PHYSMAP_BASE);
}
