#include "arch/x86_64/cpu.h"
#include "lib/string.h"

static char brand[49];

void cpu_init(void) {
    /* Liga SSE: CR0.EM = 0 (sem emulação), CR0.MP = 1;
     * CR4.OSFXSR = 1 (FXSAVE/FXRSTOR), CR4.OSXMMEXCPT = 1 (exceções SIMD). */
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    write_cr0(cr0);
    write_cr4(read_cr4() | (1ULL << 9) | (1ULL << 10));
    __asm__ volatile("fninit");

    uint32_t a, b, c, d;
    cpuid(0x80000000, 0, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        uint32_t *p = (uint32_t *)brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            cpuid(leaf, 0, &a, &b, &c, &d);
            *p++ = a; *p++ = b; *p++ = c; *p++ = d;
        }
        brand[48] = 0;
    } else {
        cpuid(0, 0, &a, &b, &c, &d);
        memcpy(brand, &b, 4);
        memcpy(brand + 4, &d, 4);
        memcpy(brand + 8, &c, 4);
        brand[12] = 0;
    }
}

const char *cpu_brand(void) {
    const char *s = brand;
    while (*s == ' ') s++;
    return s;
}
