/*
 * kmain: a primeira função em C. Inicializa cada subsistema na ordem certa.
 */
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "boot/bootinfo.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "sched/task.h"
#include "dev/serial.h"
#include "dev/ps2.h"
#include "dev/rtc.h"
#include "dev/power.h"
#include "dev/vbox.h"
#include "dev/audio.h"
#include "fs/vfs.h"
#include "gui/wm.h"
#include "gui/theme.h"
#include "apps/apps.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "panic.h"

void kmain(uint32_t magic, uint64_t info_phys) {
    serial_init();
    kprintf("\n=== Capivara OS: iniciando o kernel ===\n");

    bootinfo_parse(magic, info_phys);
    kprintf("boot: %s, cmdline='%s'\n", bootinfo.bootloader, bootinfo.cmdline);
    if (!bootinfo.has_fb) panic("O GRUB não forneceu um framebuffer gráfico");
    kprintf("video: %ux%u %u bpp em %p\n", bootinfo.fb_width, bootinfo.fb_height,
            bootinfo.fb_bpp, (void *)bootinfo.fb_phys);

    cpu_init();
    gdt_init();
    idt_init();
    pmm_init();
    vmm_init();
    heap_init();

    pic_init();
    pit_init();
    sched_init();
    rtc_init();
    acpi_init();

    vfs_init();
    for (int i = 0; i < bootinfo.module_count; i++) {
        boot_module_t *m = &bootinfo.modules[i];
        if (!strcmp(m->name, "initrd"))
            vfs_load_initrd(phys_to_virt(m->start), m->end - m->start);
    }

    ps2_init();
    keyboard_init();
    mouse_init(!bootinfo_has_arg("novmmouse"));
    if (!bootinfo_has_arg("novbox")) vbox_init();
    if (!bootinfo_has_arg("nosom")) audio_init();

    /* interface gráfica: compositor + shell (área de trabalho e barra de tarefas) */
    apps_init();
    theme_apply(false, 0, 0);
    wm_init();
    task_t *shell = task_create("shell", shell_main, NULL);
    shell->system = true;

    kprintf("kmain: pronto, entrando no laço ocioso\n");
    task_idle_loop();
}
