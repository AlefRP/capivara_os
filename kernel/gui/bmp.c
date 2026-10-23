/*
 * BMP: cabeçalho de 14 bytes ("BM", tamanho, deslocamento dos pixels) +
 * cabeçalho DIB de 40 bytes (largura, altura, bits por pixel...) + pixels.
 * As linhas são gravadas de BAIXO para CIMA e cada uma é completada até um
 * múltiplo de 4 bytes. Cada pixel de 24 bits é B, G, R.
 */
#include "gui/bmp.h"
#include "fs/vfs.h"
#include "mm/heap.h"
#include "lib/string.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

uint8_t *bmp_encode(const canvas_t *c, rect_t r, size_t *out_size) {
    r = rect_intersect(r, R(0, 0, c->w, c->h));
    if (rect_empty(r)) return NULL;
    uint32_t stride = (r.w * 3 + 3) & ~3u;
    uint32_t size = 54 + stride * r.h;
    uint8_t *buf = kcalloc(1, size);
    if (!buf) return NULL;
    buf[0] = 'B';
    buf[1] = 'M';
    put32(buf + 2, size);
    put32(buf + 10, 54);
    put32(buf + 14, 40);
    put32(buf + 18, r.w);
    put32(buf + 22, r.h);
    put16(buf + 26, 1);
    put16(buf + 28, 24);
    put32(buf + 34, stride * r.h);
    put32(buf + 38, 2835);                  /* 72 DPI */
    put32(buf + 42, 2835);
    for (int y = 0; y < r.h; y++) {
        const uint32_t *src = c->px + (r.y + r.h - 1 - y) * c->stride + r.x;
        uint8_t *dst = buf + 54 + y * stride;
        for (int x = 0; x < r.w; x++) {
            dst[x * 3 + 0] = src[x] & 0xFF;
            dst[x * 3 + 1] = (src[x] >> 8) & 0xFF;
            dst[x * 3 + 2] = (src[x] >> 16) & 0xFF;
        }
    }
    *out_size = size;
    return buf;
}

canvas_t *bmp_decode(const uint8_t *d, size_t size) {
    if (size < 54 || d[0] != 'B' || d[1] != 'M') return NULL;
    uint32_t offset = get32(d + 10), dib = get32(d + 14);
    int32_t w = (int32_t)get32(d + 18), h = (int32_t)get32(d + 22);
    uint16_t bpp = get16(d + 28);
    uint32_t comp = get32(d + 30);
    if (dib < 40 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) return NULL;
    bool top_down = h < 0;
    if (top_down) h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return NULL;
    uint32_t stride = (w * (bpp / 8) + 3) & ~3u;
    if (offset + (uint64_t)stride * h > size) return NULL;
    canvas_t *c = canvas_create(w, h);
    if (!c) return NULL;
    for (int y = 0; y < h; y++) {
        const uint8_t *src = d + offset + (top_down ? y : h - 1 - y) * stride;
        uint32_t *dst = c->px + y * c->stride;
        for (int x = 0; x < w; x++) {
            const uint8_t *p = src + x * (bpp / 8);
            dst[x] = 0xFF000000u | (p[2] << 16) | (p[1] << 8) | p[0];
        }
    }
    return c;
}

canvas_t *bmp_load(const char *path) {
    vnode_t *f = vfs_lookup(NULL, path);
    if (!f || f->type != VFS_FILE || !f->size) return NULL;
    uint8_t *buf = kmalloc(f->size);
    if (!buf) return NULL;
    long n = vfs_read(f, 0, buf, f->size);
    canvas_t *c = n > 0 ? bmp_decode(buf, n) : NULL;
    kfree(buf);
    return c;
}

bool bmp_save(const char *path, const canvas_t *c, rect_t area) {
    size_t size;
    uint8_t *buf = bmp_encode(c, area, &size);
    if (!buf) return false;
    bool ok = vfs_write_all(path, buf, size);
    kfree(buf);
    return ok;
}
