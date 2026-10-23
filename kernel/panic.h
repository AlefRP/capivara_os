#pragma once
#include "arch/x86_64/cpu.h"

__attribute__((noreturn, format(printf, 1, 2))) void panic(const char *fmt, ...);
__attribute__((noreturn)) void panic_exception(const char *name, regs_t *r);

/* A interface gráfica registra aqui uma função que desenha a tela de erro. */
typedef void (*panic_screen_fn)(const char *title, const char *details);
void panic_set_screen(panic_screen_fn fn);

#define ASSERT(x) do { if (!(x)) panic("Falha em %s:%d: %s", __FILE__, __LINE__, #x); } while (0)
