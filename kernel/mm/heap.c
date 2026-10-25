/*
 * Alocador "first fit" com lista de blocos contíguos.
 *
 *   [cabeçalho|dados......][cabeçalho|dados..][cabeçalho|dados.........]
 *
 * Cada cabeçalho guarda o tamanho total do bloco, se está livre e ponteiros
 * para o bloco anterior/seguinte (em ordem de endereço). Ao liberar, o bloco
 * se funde com vizinhos livres para evitar fragmentação. Quando falta espaço,
 * novas páginas físicas são mapeadas no fim do heap.
 */
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/x86_64/cpu.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "panic.h"
#include "sched/task.h"

#define MAGIC_USED 0xA110CA7E
#define MAGIC_FREE 0xF7EEB10C
#define MIN_SPLIT  64

typedef struct block {
    uint64_t size;            /* inclui o cabeçalho */
    uint32_t magic;
    int32_t owner;            /* id da tarefa que alocou (estatística) */
    struct block *prev, *next;
} block_t;                    /* 32 bytes: mantém os dados alinhados em 16 */

static block_t *first, *last;
static uint64_t heap_end = HEAP_BASE;
static uint64_t used_bytes, block_count;

/* Mapeia mais memória no fim do heap. */
static bool grow(uint64_t bytes) {
    bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t start = heap_end;
    for (uint64_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint64_t phys = pmm_alloc();
        if (!phys || !vmm_map(start + off, phys, PTE_WRITE | PTE_GLOBAL)) {
            kprintf("heap: sem memória física\n");
            return false;
        }
    }
    heap_end += bytes;

    if (last && last->magic == MAGIC_FREE) {
        last->size += bytes;            /* estende o último bloco livre */
    } else {
        block_t *b = (block_t *)start;
        b->size = bytes;
        b->magic = MAGIC_FREE;
        b->prev = last;
        b->next = NULL;
        if (last) last->next = b; else first = b;
        last = b;
        block_count++;
    }
    return true;
}

void heap_init(void) {
    if (!grow(4 << 20)) panic("heap_init: sem memória");
    kprintf("heap: iniciado em %p\n", (void *)HEAP_BASE);
}

static void split(block_t *b, uint64_t size) {
    if (b->size < size + MIN_SPLIT) return;
    block_t *n = (block_t *)((uint8_t *)b + size);
    n->size = b->size - size;
    n->magic = MAGIC_FREE;
    n->prev = b;
    n->next = b->next;
    if (b->next) b->next->prev = n; else last = n;
    b->next = n;
    b->size = size;
    block_count++;
}

void *kmalloc(size_t size) {
    if (!size) return NULL;
    uint64_t need = ((size + 15) & ~15ULL) + sizeof(block_t);
    uint64_t f = irq_save();
    for (;;) {
        for (block_t *b = first; b; b = b->next) {
            if (b->magic == MAGIC_FREE && b->size >= need) {
                split(b, need);
                b->magic = MAGIC_USED;
                used_bytes += b->size;
                task_t *cur = task_current();
                b->owner = cur ? cur->id : -1;
                if (cur) cur->mem_bytes += b->size;
                irq_restore(f);
                return b + 1;
            }
        }
        if (!grow(need + (1 << 20))) {
            irq_restore(f);
            return NULL;
        }
    }
}

void *kcalloc(size_t n, size_t size) {
    void *p = kmalloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    uint64_t f = irq_save();
    block_t *b = (block_t *)p - 1;
    if (b->magic != MAGIC_USED)
        panic("kfree: ponteiro inválido ou liberado duas vezes (%p)", p);
    b->magic = MAGIC_FREE;
    used_bytes -= b->size;
    if (b->owner >= 0) task_account_memory(b->owner, -(int64_t)b->size);

    /* funde com o seguinte e com o anterior, se livres */
    if (b->next && b->next->magic == MAGIC_FREE) {
        block_t *n = b->next;
        b->size += n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b; else last = b;
        n->magic = 0;
        block_count--;
    }
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        block_t *pv = b->prev;
        pv->size += b->size;
        pv->next = b->next;
        if (b->next) b->next->prev = pv; else last = pv;
        b->magic = 0;
        block_count--;
    }
    irq_restore(f);
}

void *krealloc(void *p, size_t size) {
    if (!p) return kmalloc(size);
    block_t *b = (block_t *)p - 1;
    uint64_t cap = b->size - sizeof(block_t);
    if (size <= cap) return p;
    void *n = kmalloc(size);
    if (n) {
        memcpy(n, p, cap);
        kfree(p);
    }
    return n;
}

int heap_snapshot(heap_block_t *out, int max) {
    uint64_t f = irq_save();
    int n = 0;
    for (block_t *b = first; b; b = b->next, n++)
        if (n < max) out[n] = (heap_block_t){ (uint64_t)b, b->size, b->magic == MAGIC_FREE, b->owner };
    irq_restore(f);
    return n;
}

void heap_stats(heap_stats_t *out) {
    out->size = heap_end - HEAP_BASE;
    out->used = used_bytes;
    out->blocks = block_count;
}
