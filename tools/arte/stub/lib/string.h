/* Fora do kernel, as funções de string são as da libc. */
#pragma once
#include <string.h>
#include <stdint.h>
#include <stddef.h>
static inline void memset32(uint32_t *d, uint32_t v, size_t n) { while (n--) *d++ = v; }
