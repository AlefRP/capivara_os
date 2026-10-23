#include "lib/string.h"
#include "lib/utf8.h"
#include "mm/heap.h"

/* "rep movsb/stosb" são instruções de cópia em bloco do x86: simples e rápidas. */
void *memcpy(void *dst, const void *src, size_t n) {
    void *d = dst;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) :: "memory");
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);
    /* sobreposição com destino depois da origem: copia de trás para frente */
    d += n - 1;
    s += n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    void *d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

void memset32(uint32_t *dst, uint32_t v, size_t count) {
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(count) : "a"(v) : "memory");
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower(*a) == tolower(*b)) { a++; b++; }
    return tolower((uint8_t)*a) - tolower((uint8_t)*b);
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dl = strlen(dst);
    if (dl >= size) return dl + strlen(src);
    return dl + strlcpy(dst + dl, src, size - dl);
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (; *s; s++)
        if (*s == (char)c) last = s;
    return (char *)(c == 0 ? s : last);
}

char *strstr(const char *h, const char *n) {
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (!strncmp(h, n, nl)) return (char *)h;
    return NULL;
}

/* Busca "sem frescura": "configuracoes" encontra "Configurações". */
static uint32_t fold(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xFF) {
        static const char map[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyps";
        uint32_t lo = cp >= 0xE0 ? cp - 0xE0 : cp - 0xC0;
        return (uint8_t)map[lo];
    }
    return cp;
}

const char *strcasestr_utf8(const char *h, const char *n) {
    if (!*n) return h;
    for (const char *start = h; *start; ) {
        const char *a = start, *b = n;
        for (;;) {
            if (!*b) return start;
            if (!*a) break;
            uint32_t ca = utf8_decode(&a), cb = utf8_decode(&b);
            if (fold(ca) != fold(cb)) break;
        }
        utf8_decode(&start);
    }
    return NULL;
}

char *kstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = kmalloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

long strtol(const char *s, const char **end, int base) {
    while (isspace(*s)) s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    }
    if (base == 0) base = 10;
    long v = 0;
    for (;;) {
        int d;
        if (isdigit(*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = s;
    return neg ? -v : v;
}
