/* Gerência de memória FÍSICA: quais páginas de 4 KiB da RAM estão livres. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE ((uint64_t)4096)

void     pmm_init(void);
uint64_t pmm_alloc(void);            /* endereço físico de uma página, 0 se acabou */
uint64_t pmm_alloc_zero(void);
void     pmm_free(uint64_t phys);
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
uint64_t pmm_max_phys(void);         /* maior endereço de RAM utilizável */
