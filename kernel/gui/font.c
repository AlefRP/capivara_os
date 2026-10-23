/*
 * Texto com a fonte GNU Unifont.
 *
 * Modo proporcional (padrão): as colunas vazias de cada glifo são cortadas e
 * os caracteres ficam separados por 1 pixel — o texto fica bem mais natural
 * que a largura fixa de 8 pixels.
 *
 * Modo 2X: cada pixel vira um bloco 2x2 suavizado com o algoritmo EPX
 * (as diagonais ficam lisas em vez de "escadinha").
 */
#include "gui/font.h"
#include "gui/gfx.h"
#include "lib/utf8.h"
#include "lib/string.h"

static const glyph_t *latin[256];

void font_init(void) {
    for (int i = 0; i < font_glyph_count; i++)
        if (font_glyphs[i].cp < 256) latin[font_glyphs[i].cp] = &font_glyphs[i];
}

const glyph_t *font_glyph(uint32_t cp) {
    if (cp < 256) return latin[cp] ? latin[cp] : latin['?'];
    int lo = 0, hi = font_glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_glyphs[mid].cp == cp) return &font_glyphs[mid];
        if (font_glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return latin['?'];
}

static int scale_of(int flags) { return (flags & FONT_4X) ? 4 : (flags & FONT_2X) ? 2 : 1; }

int font_height(int flags) { return 16 * scale_of(flags); }

static int glyph_cols(const glyph_t *g, int flags, int *first) {
    if ((flags & FONT_MONO) || !g->has_pixels) {
        *first = 0;
        return g->wide ? 16 : 8;
    }
    *first = g->left;
    return g->right - g->left + 1;
}

int font_char_width(uint32_t cp, int flags) {
    const glyph_t *g = font_glyph(cp);
    int scale = scale_of(flags);
    int w;
    if (flags & FONT_MONO) w = g->wide ? 16 : 8;
    else if (!g->has_pixels) w = (cp == ' ' || cp == 0xA0) ? 4 : 8;
    else w = g->right - g->left + 2;
    if (flags & FONT_BOLD) w++;
    return w * scale;
}

/* EPX: amplia uma grade de pixels (0/1) em 2x, arredondando as diagonais.
 * Para cada pixel P com vizinhos A (cima), B (direita), C (esquerda), D (baixo)
 * gera um bloco 2x2; um canto copia o vizinho quando os dois lados concordam. */
static void epx(const uint8_t *in, int w, int h, uint8_t *out) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t P = in[y * w + x];
            uint8_t A = y > 0 ? in[(y - 1) * w + x] : 0;
            uint8_t B = x < w - 1 ? in[y * w + x + 1] : 0;
            uint8_t C = x > 0 ? in[y * w + x - 1] : 0;
            uint8_t D = y < h - 1 ? in[(y + 1) * w + x] : 0;
            uint8_t p1 = P, p2 = P, p3 = P, p4 = P;
            if (C == A && C != D && A != B) p1 = A;
            if (A == B && A != C && B != D) p2 = B;
            if (D == C && D != B && C != A) p3 = C;
            if (B == D && B != A && D != C) p4 = D;
            out[(2 * y) * 2 * w + 2 * x] = p1;
            out[(2 * y) * 2 * w + 2 * x + 1] = p2;
            out[(2 * y + 1) * 2 * w + 2 * x] = p3;
            out[(2 * y + 1) * 2 * w + 2 * x + 1] = p4;
        }
}

static void draw_glyph(canvas_t *c, int x, int y, const glyph_t *g, uint32_t color, int flags) {
    if (!g->has_pixels) return;
    int first;
    int ncols = glyph_cols(g, flags, &first);
    int scale = scale_of(flags);
    int passes = (flags & FONT_BOLD) ? 2 : 1;

    if (scale == 1) {
        for (int pass = 0; pass < passes; pass++)
            for (int row = 0; row < 16; row++) {
                if (!g->rows[row]) continue;
                for (int col = 0; col < ncols; col++)
                    if (g->rows[row] & (0x8000 >> (first + col))) gfx_pixel(c, x + pass + col, y + row, color);
            }
        return;
    }

    /* grade base (com 1 coluna de folga de cada lado) ampliada por EPX */
    uint8_t base[16 * 18], g2[32 * 36], g4[64 * 72];
    int w = ncols + 2;
    memset(base, 0, sizeof(base));
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < ncols; col++)
            base[row * w + col + 1] = (g->rows[row] & (0x8000 >> (first + col))) ? 1 : 0;
    epx(base, w, 16, g2);
    const uint8_t *grid = g2;
    int gw = w * 2, gh = 32;
    if (scale == 4) {
        epx(g2, gw, gh, g4);
        grid = g4;
        gw *= 2;
        gh *= 2;
    }
    for (int pass = 0; pass < passes; pass++)
        for (int yy = 0; yy < gh; yy++)
            for (int xx = 0; xx < gw; xx++)
                if (grid[yy * gw + xx]) gfx_pixel(c, x + xx - scale + pass * (scale / 2), y + yy, color);
}

int font_draw_n(canvas_t *c, int x, int y, const char *s, int bytes, uint32_t color, int flags) {
    const char *end = bytes < 0 ? NULL : s + bytes;
    int start = x;
    while ((!end || s < end) && *s) {
        uint32_t cp = utf8_decode(&s);
        if (cp == '\n' || cp == '\r') continue;
        const glyph_t *g = font_glyph(cp);
        draw_glyph(c, x, y, g, color, flags);
        x += font_char_width(cp, flags);
    }
    return x - start;
}

int font_draw(canvas_t *c, int x, int y, const char *s, uint32_t color, int flags) {
    return font_draw_n(c, x, y, s, -1, color, flags);
}

int font_width_n(const char *s, int bytes, int flags) {
    const char *end = bytes < 0 ? NULL : s + bytes;
    int w = 0;
    while ((!end || s < end) && *s) {
        uint32_t cp = utf8_decode(&s);
        if (cp == '\n' || cp == '\r') continue;
        w += font_char_width(cp, flags);
    }
    return w;
}

int font_width(const char *s, int flags) { return font_width_n(s, -1, flags); }

int font_fit(const char *s, int max_width, int flags) {
    const char *start = s;
    int w = 0;
    while (*s) {
        const char *prev = s;
        uint32_t cp = utf8_decode(&s);
        w += font_char_width(cp, flags);
        if (w > max_width) return (int)(prev - start);
    }
    return (int)(s - start);
}

/* Desenha o texto; se não couber, corta e termina com "…" */
void font_draw_ellipsis(canvas_t *c, int x, int y, int max_width, const char *s, uint32_t color, int flags) {
    if (font_width(s, flags) <= max_width) {
        font_draw(c, x, y, s, color, flags);
        return;
    }
    int n = font_fit(s, max_width - font_width("…", flags), flags);
    int w = font_draw_n(c, x, y, s, n, color, flags);
    font_draw(c, x + w, y, "…", color, flags);
}
