/* Heap do kernel: kmalloc/kfree (o "malloc" de dentro do kernel). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void  heap_init(void);
void *kmalloc(size_t size);           /* alinhado em 16 bytes */
void *kcalloc(size_t n, size_t size); /* zerado */
void *krealloc(void *p, size_t size);
void  kfree(void *p);

typedef struct { uint64_t size, used, blocks; } heap_stats_t;
void heap_stats(heap_stats_t *out);

/* Cópia da lista de blocos, em ordem de endereço (para o laboratório "heap").
 * Preenche até max e devolve quantos blocos existem ao todo. */
typedef struct { uint64_t addr, size; bool free; int owner; } heap_block_t;
int  heap_snapshot(heap_block_t *out, int max);
