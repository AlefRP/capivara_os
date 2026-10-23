/*
 * Script do linker (GNU ld).
 *
 * O GRUB carrega o kernel no endereço FÍSICO 1 MiB. O código de boot (32 bits)
 * roda nesse endereço; todo o resto é linkado na metade superior do espaço de
 * endereçamento (0xFFFFFFFF80000000 + físico), mas carregado logo depois do
 * código de boot (diretiva AT = endereço de carga).
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 1M;
    _kernel_phys_start = .;

    .boot : {
        KEEP(*(.multiboot2))
        *(.boot.text)
        *(.boot.rodata)
    }

    . = ALIGN(4K);
    . += KERNEL_VMA;

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    }

    . = ALIGN(4K);
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4K);
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data .data.*)
    }

    . = ALIGN(4K);
    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    . = ALIGN(4K);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
