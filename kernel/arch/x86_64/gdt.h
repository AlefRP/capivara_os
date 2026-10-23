#pragma once
#include <stdint.h>

#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UDATA 0x1B
#define SEL_UCODE 0x23

void gdt_init(void);
void tss_set_kernel_stack(uint64_t rsp0);
