#pragma once
#include <stdarg.h>
#include <stddef.h>

/* Formatos: %d %i %u %x %X %p %s %c %%, com l/ll/z, largura, '0', '-' e %.Ns */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Log do kernel: vai para a porta serial e para um buffer circular (dmesg). */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
size_t klog_read(char *out, size_t size);
