/*
 * Primitivas de desenho.
 *
 * Antisserrilhado: para formas curvas calculamos, para cada pixel, a
 * distância do centro do pixel até a borda da forma. Essa distância vira a
 * "cobertura" (0 a 1) e a cor é misturada com o fundo nessa proporção.
 */
#include "gui/gfx.h"
#include "mm/heap.h"
#include "lib/string.h"

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

float gfx_sqrt(float x) { return __builtin_sqrtf(x); }

/* seno por série de Taylor depois de reduzir o ângulo para [-pi, pi] */
float gfx_sin(float x) {
    while (x > GFX_PI) x -= 2 * GFX_PI;
    while (x < -GFX_PI) x += 2 * GFX_PI;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
float gfx_cos(float x) { return gfx_sin(x + GFX_PI / 2); }

rect_t rect_intersect(rect_t a, rect_t b) {
    int x0 = imax(a.x, b.x), y0 = imax(a.y, b.y);
    int x1 = imin(a.x + a.w, b.x + b.w), y1 = imin(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return R(0, 0, 0, 0);
    return R(x0, y0, x1 - x0, y1 - y0);
}

rect_t rect_union(rect_t a, rect_t b) {
    if (rect_empty(a)) return b;
    if (rect_empty(b)) return a;
    int x0 = imin(a.x, b.x), y0 = imin(a.y, b.y);
    int x1 = imax(a.x + a.w, b.x + b.w), y1 = imax(a.y + a.h, b.y + b.h);
    return R(x0, y0, x1 - x0, y1 - y0);
}

canvas_t *canvas_create(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    canvas_t *c = kmalloc(sizeof(canvas_t));
    if (!c) return NULL;
    c->px = kmalloc((uint64_t)w * h * 4);
    if (!c->px) { kfree(c); return NULL; }
    canvas_wrap(c, c->px, w, h, w);
    return c;
}

void canvas_destroy(canvas_t *c) {
    if (!c) return;
    kfree(c->px);
    kfree(c);
}

void canvas_wrap(canvas_t *c, uint32_t *px, int w, int h, int stride) {
    c->px = px;
    c->w = w;
    c->h = h;
    c->stride = stride;
    c->clip = R(0, 0, w, h);
}

void canvas_clip(canvas_t *c, rect_t r) { c->clip = rect_intersect(r, R(0, 0, c->w, c->h)); }
void canvas_unclip(canvas_t *c) { c->clip = R(0, 0, c->w, c->h); }

/* x/255 com arredondamento exato, sem divisão */
static inline uint32_t div255(uint32_t x) { x += 128; return (x + (x >> 8)) >> 8; }

uint32_t color_blend(uint32_t d, uint32_t s) {
    uint32_t a = s >> 24;
    if (a == 255) return s;
    if (a == 0) return d;
    uint32_t inv = 255 - a;
    uint32_t r = div255(((s >> 16) & 255) * a + ((d >> 16) & 255) * inv);
    uint32_t g = div255(((s >> 8) & 255) * a + ((d >> 8) & 255) * inv);
    uint32_t b = div255((s & 255) * a + (d & 255) * inv);
    uint32_t da = d >> 24;
    uint32_t oa = da + div255(a * (255 - da));
    return (oa << 24) | (r << 16) | (g << 8) | b;
}

uint32_t color_mix(uint32_t a, uint32_t b, int t) {
    uint32_t inv = 255 - t;
    uint32_t r = div255(((a >> 16) & 255) * inv + ((b >> 16) & 255) * t);
    uint32_t g = div255(((a >> 8) & 255) * inv + ((b >> 8) & 255) * t);
    uint32_t bl = div255((a & 255) * inv + (b & 255) * t);
    uint32_t al = div255((a >> 24) * inv + (b >> 24) * t);
    return (al << 24) | (r << 16) | (g << 8) | bl;
}

uint32_t color_with_alpha(uint32_t c, int a) {
    return ((uint32_t)a << 24) | (c & 0xFFFFFF);
}

void gfx_set(canvas_t *c, int x, int y, int w, int h, uint32_t color) {
    rect_t r = rect_intersect(R(x, y, w, h), c->clip);
    for (int j = 0; j < r.h; j++)
        memset32(c->px + (r.y + j) * c->stride + r.x, color, r.w);
}

void gfx_fill(canvas_t *c, int x, int y, int w, int h, uint32_t color) {
    uint32_t a = color >> 24;
    if (a == 0) return;
    if (a == 255) { gfx_set(c, x, y, w, h, color); return; }
    rect_t r = rect_intersect(R(x, y, w, h), c->clip);
    for (int j = 0; j < r.h; j++) {
        uint32_t *p = c->px + (r.y + j) * c->stride + r.x;
        for (int i = 0; i < r.w; i++) p[i] = color_blend(p[i], color);
    }
}

void gfx_clear(canvas_t *c, uint32_t color) { gfx_set(c, 0, 0, c->w, c->h, color); }
void gfx_hline(canvas_t *c, int x, int y, int w, uint32_t color) { gfx_fill(c, x, y, w, 1, color); }
void gfx_vline(canvas_t *c, int x, int y, int h, uint32_t color) { gfx_fill(c, x, y, 1, h, color); }

void gfx_rect(canvas_t *c, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    gfx_hline(c, x, y, w, color);
    if (h > 1) gfx_hline(c, x, y + h - 1, w, color);
    if (h > 2) {
        gfx_vline(c, x, y + 1, h - 2, color);
        if (w > 1) gfx_vline(c, x + w - 1, y + 1, h - 2, color);
    }
}

void gfx_pixel(canvas_t *c, int x, int y, uint32_t color) {
    if (!rect_has(c->clip, x, y)) return;
    uint32_t *p = c->px + y * c->stride + x;
    *p = color_blend(*p, color);
}

void gfx_pixel_cov(canvas_t *c, int x, int y, uint32_t color, float cov) {
    if (cov <= 0.0f || !rect_has(c->clip, x, y)) return;
    if (cov > 1.0f) cov = 1.0f;
    uint32_t a = (uint32_t)((color >> 24) * cov + 0.5f);
    uint32_t *p = c->px + y * c->stride + x;
    *p = color_blend(*p, (a << 24) | (color & 0xFFFFFF));
}

/* Retângulo arredondado: linha a linha, só os cantos usam cálculo por pixel. */
void gfx_round_fill_corners(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color, int corners) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    if (r < 0.5f || !corners) { gfx_fill(c, x, y, w, h, color); return; }
    if (rect_empty(rect_intersect(R(x, y, w, h), c->clip))) return;
    int ri = (int)(r + 0.999f);

    for (int j = 0; j < h; j++) {
        float py = j + 0.5f;
        bool top = py < r, bottom = py > h - r;
        if (!top && !bottom) { gfx_fill(c, x, y + j, w, 1, color); continue; }
        float dy = top ? r - py : py - (h - r);
        bool cl = top ? (corners & CORNER_TL) : (corners & CORNER_BL);
        bool cr = top ? (corners & CORNER_TR) : (corners & CORNER_BR);
        int left = cl ? ri : 0, right = cr ? ri : 0;
        if (w - left - right > 0) gfx_fill(c, x + left, y + j, w - left - right, 1, color);
        for (int i = 0; i < ri; i++) {
            float dx = r - (i + 0.5f);
            if (dx < 0) dx = 0;
            float cov = clamp01(r - gfx_sqrt(dx * dx + dy * dy) + 0.5f);
            if (cl) gfx_pixel_cov(c, x + i, y + j, color, cov);
            if (cr && w - 1 - i != i) gfx_pixel_cov(c, x + w - 1 - i, y + j, color, cov);
        }
    }
}

void gfx_round_fill(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color) {
    gfx_round_fill_corners(c, x, y, w, h, r, color, CORNER_ALL);
}

void gfx_round_rect(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    if (r < 0.5f) { gfx_rect(c, x, y, w, h, color); return; }
    int ri = (int)(r + 0.999f);
    gfx_hline(c, x + ri, y, w - 2 * ri, color);
    gfx_hline(c, x + ri, y + h - 1, w - 2 * ri, color);
    gfx_vline(c, x, y + ri, h - 2 * ri, color);
    gfx_vline(c, x + w - 1, y + ri, h - 2 * ri, color);
    for (int j = 0; j < ri; j++) {
        for (int i = 0; i < ri; i++) {
            float dx = r - (i + 0.5f), dy = r - (j + 0.5f);
            float d = gfx_sqrt(dx * dx + dy * dy);
            float cov = clamp01(r - d + 0.5f) - clamp01(r - 1.0f - d + 0.5f);
            if (cov <= 0) continue;
            gfx_pixel_cov(c, x + i, y + j, color, cov);
            gfx_pixel_cov(c, x + w - 1 - i, y + j, color, cov);
            gfx_pixel_cov(c, x + i, y + h - 1 - j, color, cov);
            gfx_pixel_cov(c, x + w - 1 - i, y + h - 1 - j, color, cov);
        }
    }
}

void gfx_circle(canvas_t *c, float cx, float cy, float r, uint32_t color) {
    int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 1);
    int y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            gfx_pixel_cov(c, x, y, color, r - gfx_sqrt(dx * dx + dy * dy) + 0.5f);
        }
}

void gfx_ring(canvas_t *c, float cx, float cy, float r, float thick, uint32_t color) {
    float o = r + thick / 2 + 1;
    int x0 = (int)(cx - o), x1 = (int)(cx + o);
    int y0 = (int)(cy - o), y1 = (int)(cy + o);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = gfx_sqrt(dx * dx + dy * dy) - r;
            if (d < 0) d = -d;
            gfx_pixel_cov(c, x, y, color, thick / 2 - d + 0.5f);
        }
}

/* Linha grossa com pontas arredondadas: distância do pixel ao segmento. */
void gfx_line(canvas_t *c, float x0, float y0, float x1, float y1, float width, uint32_t color) {
    float hw = width / 2;
    int bx0 = (int)((x0 < x1 ? x0 : x1) - hw - 1), bx1 = (int)((x0 > x1 ? x0 : x1) + hw + 1);
    int by0 = (int)((y0 < y1 ? y0 : y1) - hw - 1), by1 = (int)((y0 > y1 ? y0 : y1) + hw + 1);
    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = len2 > 0 ? (px * vx + py * vy) / len2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float dx = px - t * vx, dy = py - t * vy;
            gfx_pixel_cov(c, x, y, color, hw - gfx_sqrt(dx * dx + dy * dy) + 0.5f);
        }
}

/* Triângulo com 16 amostras por pixel (super-amostragem). */
void gfx_triangle(canvas_t *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color) {
    float minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;
    float sign = area > 0 ? 1.0f : -1.0f;
    for (int y = (int)miny; y <= (int)maxy; y++)
        for (int x = (int)minx; x <= (int)maxx; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sign;
                    float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sign;
                    float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sign;
                    if (e0 >= 0 && e1 >= 0 && e2 >= 0) hits++;
                }
            if (hits) gfx_pixel_cov(c, x, y, color, hits / 16.0f);
        }
}

void gfx_vgradient(canvas_t *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    for (int j = 0; j < h; j++)
        gfx_fill(c, x, y + j, w, 1, color_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

void gfx_blit(canvas_t *dst, int dx, int dy, const canvas_t *src, int sx, int sy, int w, int h) {
    rect_t r = rect_intersect(R(dx, dy, w, h), dst->clip);
    r = rect_intersect(r, R(dx - sx, dy - sy, src->w, src->h));
    for (int j = 0; j < r.h; j++)
        memcpy(dst->px + (r.y + j) * dst->stride + r.x,
               src->px + (r.y - dy + sy + j) * src->stride + (r.x - dx + sx), r.w * 4);
}

void gfx_blit_alpha(canvas_t *dst, int dx, int dy, const canvas_t *src, int sx, int sy, int w, int h) {
    rect_t r = rect_intersect(R(dx, dy, w, h), dst->clip);
    r = rect_intersect(r, R(dx - sx, dy - sy, src->w, src->h));
    for (int j = 0; j < r.h; j++) {
        uint32_t *d = dst->px + (r.y + j) * dst->stride + r.x;
        const uint32_t *s = src->px + (r.y - dy + sy + j) * src->stride + (r.x - dx + sx);
        for (int i = 0; i < r.w; i++) d[i] = color_blend(d[i], s[i]);
    }
}

/* Sombra suave ao redor de r: opacidade cai com o quadrado da distância. */
void gfx_shadow(canvas_t *c, rect_t r, float radius, int size, int strength) {
    rect_t outer = R(r.x - size, r.y - size + 2, r.w + 2 * size, r.h + 2 * size);
    rect_t area = rect_intersect(outer, c->clip);
    float inner = radius;
    for (int y = area.y; y < area.y + area.h; y++) {
        float py = y + 0.5f - 2;                  /* sombra deslocada 2 px para baixo */
        for (int x = area.x; x < area.x + area.w; x++) {
            if (rect_has(r, x, y)) { x = r.x + r.w - 1; continue; }
            float px = x + 0.5f;
            /* distância a um retângulo arredondado de raio "inner" */
            float qx = px < r.x + inner ? r.x + inner - px : (px > r.x + r.w - inner ? px - (r.x + r.w - inner) : 0);
            float qy = py < r.y + inner ? r.y + inner - py : (py > r.y + r.h - inner ? py - (r.y + r.h - inner) : 0);
            float d = gfx_sqrt(qx * qx + qy * qy) - inner;
            if (d < 0) d = 0;
            if (d >= size) continue;
            float t = 1.0f - d / size;
            int a = (int)(strength * t * t);
            if (a > 0) {
                uint32_t *p = c->px + y * c->stride + x;
                *p = color_blend(*p, (uint32_t)a << 24);
            }
        }
    }
}

void gfx_blit_scaled(canvas_t *dst, rect_t dr, const canvas_t *src, rect_t sr, bool smooth) {
    if (rect_empty(dr) || rect_empty(sr)) return;
    rect_t area = rect_intersect(dr, dst->clip);
    for (int y = area.y; y < area.y + area.h; y++) {
        int sy = sr.y + (int)((int64_t)(y - dr.y) * sr.h / dr.h);
        int sy2 = sy + 1 < sr.y + sr.h ? sy + 1 : sy;
        uint32_t *d = dst->px + y * dst->stride;
        const uint32_t *row = src->px + sy * src->stride;
        const uint32_t *row2 = src->px + sy2 * src->stride;
        for (int x = area.x; x < area.x + area.w; x++) {
            int sx = sr.x + (int)((int64_t)(x - dr.x) * sr.w / dr.w);
            if (!smooth) { d[x] = row[sx]; continue; }
            int sx2 = sx + 1 < sr.x + sr.w ? sx + 1 : sx;
            uint32_t a = row[sx], b = row[sx2], c = row2[sx], e = row2[sx2];
            uint32_t r = (((a >> 16) & 255) + ((b >> 16) & 255) + ((c >> 16) & 255) + ((e >> 16) & 255)) >> 2;
            uint32_t g = (((a >> 8) & 255) + ((b >> 8) & 255) + ((c >> 8) & 255) + ((e >> 8) & 255)) >> 2;
            uint32_t bl = ((a & 255) + (b & 255) + (c & 255) + (e & 255)) >> 2;
            d[x] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
    }
}

void gfx_blur(canvas_t *dst, const canvas_t *src, int f) {
    int sw = (src->w + f - 1) / f, sh = (src->h + f - 1) / f;
    canvas_t *small = canvas_create(sw, sh);
    if (!small) return;
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y * f; yy < (y + 1) * f && yy < src->h; yy += 2)
                for (int xx = x * f; xx < (x + 1) * f && xx < src->w; xx += 2) {
                    uint32_t p = src->px[yy * src->stride + xx];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++;
                }
            small->px[y * sw + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    /* ampliação bilinear (interpolação entre os 4 vizinhos) */
    for (int y = 0; y < dst->h; y++) {
        int fy = (y * 256) / f - 128;
        if (fy < 0) fy = 0;
        int y0 = fy >> 8, y1 = y0 + 1 < sh ? y0 + 1 : y0, ty = fy & 255;
        if (y0 >= sh) y0 = y1 = sh - 1;
        for (int x = 0; x < dst->w; x++) {
            int fx = (x * 256) / f - 128;
            if (fx < 0) fx = 0;
            int x0 = fx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0, tx = fx & 255;
            if (x0 >= sw) x0 = x1 = sw - 1;
            uint32_t top = color_mix(small->px[y0 * sw + x0], small->px[y0 * sw + x1], tx);
            uint32_t bot = color_mix(small->px[y1 * sw + x0], small->px[y1 * sw + x1], tx);
            dst->px[y * dst->stride + x] = color_mix(top, bot, ty) | 0xFF000000u;
        }
    }
    canvas_destroy(small);
}

/* Retângulo arredondado preenchido com degradê de cima para baixo. */
void gfx_round_gradient(canvas_t *c, int x, int y, int w, int h, float r, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2.0f) r = w / 2.0f;
    if (r > h / 2.0f) r = h / 2.0f;
    int ri = (int)(r + 0.999f);
    for (int j = 0; j < h; j++) {
        uint32_t color = color_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0);
        float py = j + 0.5f;
        bool edge = py < r || py > h - r;
        if (!edge) { gfx_fill(c, x, y + j, w, 1, color); continue; }
        float dy = py < r ? r - py : py - (h - r);
        if (w - 2 * ri > 0) gfx_fill(c, x + ri, y + j, w - 2 * ri, 1, color);
        for (int i = 0; i < ri; i++) {
            float dx = r - (i + 0.5f);
            if (dx < 0) dx = 0;
            float cov = r - gfx_sqrt(dx * dx + dy * dy) + 0.5f;
            gfx_pixel_cov(c, x + i, y + j, color, cov);
            if (w - 1 - i != i) gfx_pixel_cov(c, x + w - 1 - i, y + j, color, cov);
        }
    }
}
