#include "lib/printf.h"
#include "lib/string.h"
#include "dev/serial.h"
#include "arch/x86_64/cpu.h"

struct out {
    char *buf;
    size_t size, pos;
};

static void put(struct out *o, char c) {
    if (o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}

static void put_num(struct out *o, unsigned long long v, int base, bool upper,
                    bool neg, int width, bool zero, bool left) {
    char tmp[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { tmp[n++] = digits[v % base]; v /= base; } while (v);
    int len = n + (neg ? 1 : 0);
    if (!left && !zero) for (; width > len; width--) put(o, ' ');
    if (neg) put(o, '-');
    if (!left && zero) for (; width > len; width--) put(o, '0');
    while (n) put(o, tmp[--n]);
    if (left) for (; width > len; width--) put(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct out o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        bool left = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '0') zero = true;
            else break;
        }
        int width = 0, prec = -1;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        while (isdigit(*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            while (isdigit(*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        if (*fmt == 'z') { lng = 1; fmt++; }

        switch (*fmt) {
        case 'd': case 'i': {
            long long v = lng ? va_arg(ap, long long) : va_arg(ap, int);
            bool neg = v < 0;
            put_num(&o, neg ? -(unsigned long long)v : (unsigned long long)v, 10, false, neg, width, zero, left);
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned long long v = lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
            put_num(&o, v, *fmt == 'u' ? 10 : 16, *fmt == 'X', false, width, zero, left);
            break;
        }
        case 'p':
            put(&o, '0'); put(&o, 'x');
            put_num(&o, (unsigned long long)va_arg(ap, void *), 16, false, false, 16, true, false);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (prec >= 0 && prec < len) len = prec;
            if (!left) for (int w = width; w > len; w--) put(&o, ' ');
            for (int i = 0; i < len; i++) put(&o, s[i]);
            if (left) for (int w = width; w > len; w--) put(&o, ' ');
            break;
        }
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case '%':
            put(&o, '%');
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if (size) buf[o.pos < size ? o.pos : size - 1] = 0;
    return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* Buffer circular com o histórico do log (comando "dmesg" do terminal) */
#define KLOG_SIZE 16384
static char klog[KLOG_SIZE];
static size_t klog_head, klog_len;

int kprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    uint64_t f = irq_save();
    for (char *p = buf; *p; p++) {
        if (*p == '\n') serial_putc('\r');
        serial_putc(*p);
        klog[(klog_head + klog_len) % KLOG_SIZE] = *p;
        if (klog_len < KLOG_SIZE) klog_len++;
        else klog_head = (klog_head + 1) % KLOG_SIZE;
    }
    irq_restore(f);
    return n;
}

size_t klog_read(char *out, size_t size) {
    uint64_t f = irq_save();
    size_t n = klog_len < size - 1 ? klog_len : size - 1;
    size_t start = klog_head + (klog_len - n);
    for (size_t i = 0; i < n; i++)
        out[i] = klog[(start + i) % KLOG_SIZE];
    out[n] = 0;
    irq_restore(f);
    return n;
}
