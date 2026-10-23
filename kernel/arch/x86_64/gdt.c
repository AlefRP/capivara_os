/*
 * GDT (Global Descriptor Table) e TSS (Task State Segment).
 *
 * Em 64 bits a segmentação quase não existe, mas a CPU ainda exige:
 *   - descritores de código/dados para kernel (anel 0) e usuário (anel 3);
 *   - uma TSS, que diz qual pilha usar quando uma interrupção chega
 *     (RSP0 = pilha de kernel da tarefa atual; IST1 = pilha de emergência
 *     para "double fault").
 */
#include "arch/x86_64/gdt.h"
#include "lib/string.h"

struct tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

static uint64_t gdt[7];
static struct tss tss;
static uint8_t df_stack[8192] __attribute__((aligned(16)));

struct gdtr { uint16_t limit; uint64_t base; } __attribute__((packed));

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;     /* 0x08 código kernel (L=1, DPL=0) */
    gdt[2] = 0x00CF92000000FFFFULL;     /* 0x10 dados kernel */
    gdt[3] = 0x00CFF2000000FFFFULL;     /* 0x18 dados usuário (DPL=3) */
    gdt[4] = 0x00AFFA000000FFFFULL;     /* 0x20 código usuário (DPL=3) */

    memset(&tss, 0, sizeof(tss));
    tss.ist[0] = (uint64_t)(df_stack + sizeof(df_stack));
    tss.iopb_offset = sizeof(tss);

    /* Descritor de TSS ocupa 16 bytes (duas entradas) */
    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40) |
             (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    struct gdtr gdtr = { sizeof(gdt) - 1, (uint64_t)gdt };
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "xor %%ax, %%ax\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        /* recarrega CS com um "retorno distante" */
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :: "m"(gdtr) : "rax", "memory");
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
