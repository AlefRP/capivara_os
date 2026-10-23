/*
 * Ponto de entrada do kernel.
 *
 * O GRUB (protocolo Multiboot2) nos entrega a CPU em modo protegido de 32 bits,
 * paginação desligada, EAX = número mágico e EBX = endereço físico das
 * informações de boot. Aqui montamos tabelas de página mínimas, entramos no
 * modo longo (64 bits) e saltamos para kmain() na metade superior da memória.
 */

#define KERNEL_VMA 0xFFFFFFFF80000000
#define V2P(x) ((x) - KERNEL_VMA)

/* ------------------------------------------------------------------------- */
/* Cabeçalho Multiboot2: precisa estar nos primeiros 32 KiB do arquivo.      */
/* ------------------------------------------------------------------------- */
    .section .multiboot2, "a"
    .align 8
mb2_start:
    .long 0xE85250D6                     /* número mágico */
    .long 0                              /* arquitetura: i386 (32 bits) */
    .long mb2_end - mb2_start            /* tamanho do cabeçalho */
    .long 0x100000000 - (0xE85250D6 + (mb2_end - mb2_start))   /* checksum */

    /* Tag 5: queremos um framebuffer gráfico 1024x768 com 32 bits por pixel */
    .align 8
    .short 5
    .short 0
    .long 20
    .long 1024
    .long 768
    .long 32

    /* Tag 0: fim da lista */
    .align 8
    .short 0
    .short 0
    .long 8
mb2_end:

/* ------------------------------------------------------------------------- */
/* Código de 32 bits (roda no endereço físico, antes da paginação)            */
/* ------------------------------------------------------------------------- */
    .section .boot.text, "ax"
    .code32
    .global _start
_start:
    cli
    mov %eax, %ebp                       /* guarda o número mágico */
    mov %ebx, %esi                       /* guarda o ponteiro de informações */
    mov $V2P(boot_stack_top), %esp

    /* PML4[0] → PDPT baixa (identidade) e PML4[511] → PDPT alta */
    mov $V2P(boot_pdpt_low), %eax
    or $0x3, %eax                        /* presente | escrita */
    mov %eax, V2P(boot_pml4)
    mov $V2P(boot_pdpt_high), %eax
    or $0x3, %eax
    mov %eax, V2P(boot_pml4) + 511 * 8

    /* As duas PDPTs apontam para o mesmo diretório:
     *   0x0000000000000000 + x  → físico x
     *   0xFFFFFFFF80000000 + x  → físico x   (entrada 510 da PDPT alta) */
    mov $V2P(boot_pd), %eax
    or $0x3, %eax
    mov %eax, V2P(boot_pdpt_low)
    mov %eax, V2P(boot_pdpt_high) + 510 * 8

    /* Diretório com 512 páginas grandes de 2 MiB = primeiro 1 GiB */
    mov $V2P(boot_pd), %edi
    xor %ecx, %ecx
1:  mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax                       /* presente | escrita | página grande */
    mov %eax, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 1b

    /* CR3 = PML4, liga PAE, liga modo longo (EFER.LME) e a paginação */
    mov $V2P(boot_pml4), %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $(1 << 5), %eax                   /* CR4.PAE */
    mov %eax, %cr4

    mov $0xC0000080, %ecx                /* MSR EFER */
    rdmsr
    or $(1 << 8), %eax                   /* EFER.LME */
    wrmsr

    mov %cr0, %eax
    or $((1 << 31) | (1 << 16) | 1), %eax  /* CR0.PG | CR0.WP | CR0.PE */
    mov %eax, %cr0

    /* GDT com segmento de código 64 bits e salto longo para ativá-lo */
    lgdt boot_gdtr                       /* .boot já está em endereço físico */
    ljmp $0x08, $long_mode_entry

    .code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %ax, %ax
    mov %ax, %fs
    mov %ax, %gs
    movabs $higher_half_entry, %rax
    jmp *%rax

    .section .boot.rodata, "a"
    .align 16
boot_gdt:
    .quad 0                              /* descritor nulo */
    .quad 0x00AF9A000000FFFF             /* 0x08: código de kernel, 64 bits */
    .quad 0x00CF92000000FFFF             /* 0x10: dados de kernel */
boot_gdt_end:
boot_gdtr:
    .short boot_gdt_end - boot_gdt - 1
    .quad boot_gdt

/* ------------------------------------------------------------------------- */
/* Já em 64 bits e na metade superior                                          */
/* ------------------------------------------------------------------------- */
    .section .text
higher_half_entry:
    movabs $boot_stack_top, %rsp
    mov %ebp, %edi                       /* 1º argumento: número mágico */
    mov %esi, %esi                       /* 2º argumento: info (zera bits altos) */
    xor %ebp, %ebp                       /* fim da cadeia de frames (backtrace) */
    call kmain
2:  cli
    hlt
    jmp 2b

/* ------------------------------------------------------------------------- */
/* Tabelas de página e pilha inicial (zeradas pelo GRUB, ficam no .bss)       */
/* ------------------------------------------------------------------------- */
    .section .bss
    .align 4096
    .global boot_pml4
boot_pml4:      .skip 4096
boot_pdpt_low:  .skip 4096
boot_pdpt_high: .skip 4096
boot_pd:        .skip 4096
    .align 16
    .global boot_stack, boot_stack_top
boot_stack:     .skip 32768
boot_stack_top:
