#include "lib/utf8.h"

uint32_t utf8_decode(const char **sp) {
    const uint8_t *s = (const uint8_t *)*sp;
    uint32_t cp;
    int extra;
    if (s[0] < 0x80)      { cp = s[0];        extra = 0; }
    else if (s[0] < 0xC0) { cp = 0xFFFD;      extra = 0; }   /* byte de continuação solto */
    else if (s[0] < 0xE0) { cp = s[0] & 0x1F; extra = 1; }
    else if (s[0] < 0xF0) { cp = s[0] & 0x0F; extra = 2; }
    else                  { cp = s[0] & 0x07; extra = 3; }
    s++;
    for (int i = 0; i < extra; i++) {
        if ((*s & 0xC0) != 0x80) { cp = 0xFFFD; break; }
        cp = (cp << 6) | (*s++ & 0x3F);
    }
    *sp = (const char *)s;
    return cp;
}

int utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80)    { out[0] = cp; return 1; }
    if (cp < 0x800)   { out[0] = 0xC0 | (cp >> 6); out[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000) { out[0] = 0xE0 | (cp >> 12); out[1] = 0x80 | ((cp >> 6) & 0x3F);
                        out[2] = 0x80 | (cp & 0x3F); return 3; }
    out[0] = 0xF0 | (cp >> 18); out[1] = 0x80 | ((cp >> 12) & 0x3F);
    out[2] = 0x80 | ((cp >> 6) & 0x3F); out[3] = 0x80 | (cp & 0x3F);
    return 4;
}

size_t utf8_prev(const char *s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((uint8_t)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

size_t utf8_next(const char *s, size_t len, size_t pos) {
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((uint8_t)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

size_t utf8_count(const char *s, size_t bytes) {
    size_t n = 0;
    for (size_t i = 0; i < bytes; i++)
        if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
    return n;
}
