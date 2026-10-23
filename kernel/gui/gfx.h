/*
 * Desenho 2D em memória. Um "canvas" é uma matriz de pixels ARGB 32 bits
 * (alfa no byte mais alto) com um retângulo de recorte (clip): nenhuma
 * primitiva escreve fora dele.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct { int x, y, w, h; } rect_t;

typedef struct canvas {
    uint32_t *px;
    int w, h, stride;          /* stride em pixels */
    rect_t clip;
} canvas_t;

#define HEX(h)      (0xFF000000u | (uint32_t)(h))
#define ARGB(a, c)  ((((uint32_t)(a)) << 24) | ((c) & 0xFFFFFFu))
#define ALPHA(c)    ((uint32_t)(c) >> 24)

static inline rect_t R(int x, int y, int w, int h) { return (rect_t){ x, y, w, h }; }
static inline bool rect_empty(rect_t r) { return r.w <= 0 || r.h <= 0; }
static inline bool rect_has(rect_t r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static inline rect_t rect_inset(rect_t r, int d) { return R(r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d); }
rect_t rect_intersect(rect_t a, rect_t b);
rect_t rect_union(rect_t a, rect_t b);

canvas_t *canvas_create(int w, int h);
void canvas_destroy(canvas_t *c);
void canvas_wrap(canvas_t *c, uint32_t *px, int w, int h, int stride);
void canvas_clip(canvas_t *c, rect_t r);     /* recorte = r ∩ canvas */
void canvas_unclip(canvas_t *c);

uint32_t color_blend(uint32_t dst, uint32_t src);       /* src (com alfa) sobre dst */
uint32_t color_mix(uint32_t a, uint32_t b, int t);      /* t: 0 = a, 255 = b */
uint32_t color_with_alpha(uint32_t c, int a);

/* cantos para gfx_round_fill_corners */
#define CORNER_TL 1
#define CORNER_TR 2
#define CORNER_BL 4
#define CORNER_BR 8
#define CORNER_ALL 15

void gfx_clear(canvas_t *c, uint32_t color);
void gfx_fill(canvas_t *c, int x, int y, int w, int h, uint32_t color);
void gfx_set(canvas_t *c, int x, int y, int w, int h, uint32_t color);   /* sem mistura de alfa */
void gfx_hline(canvas_t *c, int x, int y, int w, uint32_t color);
void gfx_vline(canvas_t *c, int x, int y, int h, uint32_t color);
void gfx_rect(canvas_t *c, int x, int y, int w, int h, uint32_t color);
void gfx_pixel(canvas_t *c, int x, int y, uint32_t color);
void gfx_pixel_cov(canvas_t *c, int x, int y, uint32_t color, float coverage);
void gfx_round_fill(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color);
void gfx_round_fill_corners(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color, int corners);
void gfx_round_rect(canvas_t *c, int x, int y, int w, int h, float r, uint32_t color);
void gfx_circle(canvas_t *c, float cx, float cy, float r, uint32_t color);
void gfx_ring(canvas_t *c, float cx, float cy, float r, float thick, uint32_t color);
void gfx_line(canvas_t *c, float x0, float y0, float x1, float y1, float width, uint32_t color);
void gfx_triangle(canvas_t *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);
void gfx_vgradient(canvas_t *c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_round_gradient(canvas_t *c, int x, int y, int w, int h, float r, uint32_t top, uint32_t bottom);
void gfx_blit(canvas_t *dst, int dx, int dy, const canvas_t *src, int sx, int sy, int w, int h);
void gfx_blit_alpha(canvas_t *dst, int dx, int dy, const canvas_t *src, int sx, int sy, int w, int h);
void gfx_shadow(canvas_t *c, rect_t r, float radius, int size, int strength);
/* copia src (área sr) esticando/encolhendo para dr; "smooth" faz média 2x2 */
void gfx_blit_scaled(canvas_t *dst, rect_t dr, const canvas_t *src, rect_t sr, bool smooth);
/* desfoque rápido: reduz por "factor" tirando médias e amplia de novo com interpolação */
void gfx_blur(canvas_t *dst, const canvas_t *src, int factor);

float gfx_sqrt(float x);
float gfx_sin(float x);
float gfx_cos(float x);
#define GFX_PI 3.14159265f
