/* Fonte bitmap (GNU Unifont 8x16) e desenho de texto UTF-8. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t cp;            /* ponto de código Unicode */
    uint8_t  wide;          /* 1 = glifo 16x16 */
    uint8_t  left, right;   /* primeira e última coluna com pixels */
    uint8_t  has_pixels;
    uint16_t rows[16];      /* bit 15 = coluna 0 */
} glyph_t;

extern const glyph_t font_glyphs[];
extern const int font_glyph_count;

#define FONT_H 16

/* flags de estilo */
#define FONT_MONO  0x01     /* largura fixa (terminal, editor) */
#define FONT_BOLD  0x02
#define FONT_2X    0x04     /* tamanho dobrado, suavizado */
#define FONT_4X    0x08     /* 4 vezes (relógio da tela de bloqueio) */

struct canvas;
void font_init(void);
const glyph_t *font_glyph(uint32_t cp);
int  font_draw(struct canvas *c, int x, int y, const char *s, uint32_t color, int flags);
int  font_draw_n(struct canvas *c, int x, int y, const char *s, int bytes, uint32_t color, int flags);
int  font_width(const char *s, int flags);
int  font_width_n(const char *s, int bytes, int flags);
int  font_char_width(uint32_t cp, int flags);
int  font_height(int flags);
int  font_fit(const char *s, int max_width, int flags);     /* quantos bytes cabem */
void font_draw_ellipsis(struct canvas *c, int x, int y, int max_width, const char *s, uint32_t color, int flags);
