/* Gerência de memória VIRTUAL: tabelas de página de 4 níveis. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define PHYSMAP_BASE 0xFFFF800000000000ULL   /* toda a RAM física mapeada aqui */
#define HEAP_BASE    0xFFFFA00000000000ULL   /* heap do kernel */

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_HUGE     (1ULL << 7)
#define PTE_GLOBAL   (1ULL << 8)
#define PTE_ADDR     0x000FFFFFFFFFF000ULL

void  vmm_init(void);
bool  vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void  vmm_unmap(uint64_t virt);
void *phys_to_virt(uint64_t phys);
