/* Heap do kernel: kmalloc/kfree (o "malloc" de dentro do kernel). */
#pragma once
#include <stddef.h>
#include <stdint.h>

void  heap_init(void);
void *kmalloc(size_t size);           /* alinhado em 16 bytes */
void *kcalloc(size_t n, size_t size); /* zerado */
void *krealloc(void *p, size_t size);
void  kfree(void *p);

typedef struct { uint64_t size, used, blocks; } heap_stats_t;
void heap_stats(heap_stats_t *out);
