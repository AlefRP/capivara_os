/*
 * Ícones. Cada um é desenhado numa grade de 32x32 "unidades" e escalado
 * para o tamanho pedido, usando as primitivas antisserrilhadas de gfx.c.
 * Nenhuma imagem externa: tudo é geometria.
 */
#include "gui/icons.h"
#include "gui/gfx.h"
#include "gui/theme.h"

static canvas_t *C;
static float ox, oy, sc;

/* conversões de unidades (0..32) para pixels */
#define X(v) (ox + (v) * sc)
#define Y(v) (oy + (v) * sc)
#define S(v) ((v) * sc)

static void rrect(float x, float y, float w, float h, float r, uint32_t col) {
    gfx_round_fill(C, (int)(X(x) + 0.5f), (int)(Y(y) + 0.5f), (int)(S(w) + 0.5f), (int)(S(h) + 0.5f), S(r), col);
}
static void circle(float cx, float cy, float r, uint32_t col) { gfx_circle(C, X(cx), Y(cy), S(r), col); }
static void ring(float cx, float cy, float r, float t, uint32_t col) { gfx_ring(C, X(cx), Y(cy), S(r), S(t), col); }
static void line(float x0, float y0, float x1, float y1, float w, uint32_t col) {
    float pw = S(w);
    if (pw < 1.0f) pw = 1.0f;
    gfx_line(C, X(x0), Y(y0), X(x1), Y(y1), pw, col);
}
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col) {
    gfx_triangle(C, X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), col);
}
static void grad(float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom) {
    gfx_round_gradient(C, (int)(X(x) + 0.5f), (int)(Y(y) + 0.5f), (int)(S(w) + 0.5f), (int)(S(h) + 0.5f), S(r), top, bottom);
}
static void outline(float x, float y, float w, float h, float r, uint32_t col) {
    gfx_round_rect(C, (int)(X(x) + 0.5f), (int)(Y(y) + 0.5f), (int)(S(w) + 0.5f), (int)(S(h) + 0.5f), S(r), col);
}
/* Contorno que só aparece em cima e vai sumindo até a altura "ate": o "fio
 * de luz" que bate no topo. São 4 faixas, cada uma mais transparente. */
static void outline_top(float x, float y, float w, float h, float r, float ate, uint32_t col) {
    rect_t old = C->clip;
    int a = ALPHA(col);
    for (int k = 0; k < 4; k++) {
        int y0 = k ? (int)(Y(y + (ate - y) * k / 4) + 0.5f) : old.y;
        int y1 = (int)(Y(y + (ate - y) * (k + 1) / 4) + 0.5f);
        C->clip = rect_intersect(old, R(old.x, y0, old.w, y1 - y0));
        outline(x, y, w, h, r, ARGB(a * (4 - k) / 4, col));
    }
    C->clip = old;
}

/* Polígono qualquer, inclusive côncavo (regra par-ímpar), com 4x4 amostras
 * por pixel. Desenhar a forma inteira de uma vez evita as "costuras" que
 * aparecem quando se junta vários triângulos antisserrilhados. */
static void poly(const float *p, int n, uint32_t col) {
    float q[2 * 16], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        q[2 * i] = X(p[2 * i]);
        q[2 * i + 1] = Y(p[2 * i + 1]);
        if (q[2 * i] < minx) minx = q[2 * i];
        if (q[2 * i] > maxx) maxx = q[2 * i];
        if (q[2 * i + 1] < miny) miny = q[2 * i + 1];
        if (q[2 * i + 1] > maxy) maxy = q[2 * i + 1];
    }
    for (int y = (int)miny; y <= (int)maxy; y++)
        for (int x = (int)minx; x <= (int)maxx; x++) {
            int hits = 0;
            for (int s = 0; s < 16; s++) {
                float px = x + ((s & 3) + 0.5f) / 4, py = y + ((s >> 2) + 0.5f) / 4;
                bool in = false;
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    float xi = q[2 * i], yi = q[2 * i + 1], xj = q[2 * j], yj = q[2 * j + 1];
                    if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
                }
                hits += in;
            }
            if (hits) gfx_pixel_cov(C, x, y, col, hits / 16.0f);
        }
}

/* Pasta azul (estilo das pastas modernas): aba atrás + corpo com degradê. */
static void folder(void) {
    rrect(3, 6.5f, 11, 6, 2, HEX(0x4A9DE6));
    gfx_round_gradient(C, (int)(X(3) + 0.5f), (int)(Y(9) + 0.5f), (int)(S(26) + 0.5f), (int)(S(19) + 0.5f), S(2.5f),
                       HEX(0x6FB6F4), HEX(0x4F9DE8));
    gfx_round_gradient(C, (int)(X(3) + 0.5f), (int)(Y(11) + 0.5f), (int)(S(26) + 0.5f), (int)(S(17) + 0.5f), S(2.5f),
                       HEX(0x9AD3FD), HEX(0x62AEF2));
}
#define ENGRAVE ARGB(0xC8, 0x2C6CB8)     /* símbolo "gravado" nas pastas especiais */

static void page(uint32_t paper, uint32_t edge) {
    rrect(6, 2, 20, 28, 2.5f, edge);
    rrect(7, 3, 18, 26, 2, paper);
    tri(19, 3, 25, 3, 25, 9, edge);
}

/* "Squircle": quadrado de cantos bem arredondados, com degradê vertical,
 * sombra suave embaixo e um fio de luz na borda (estilo dos ícones de app). */
static void squircle(uint32_t top, uint32_t bottom) {
    rrect(2.6f, 3.8f, 26.8f, 26.8f, 6.6f, ARGB(0x16, 0x000000));
    rrect(3, 3.6f, 26, 26.4f, 6.4f, ARGB(0x22, 0x000000));
    gfx_round_gradient(C, (int)(X(3) + 0.5f), (int)(Y(3) + 0.5f), (int)(S(26) + 0.5f), (int)(S(26) + 0.5f), S(6.2f),
                       top, bottom);
    gfx_round_rect(C, (int)(X(3) + 0.5f), (int)(Y(3) + 0.5f), (int)(S(26) + 0.5f), (int)(S(26) + 0.5f), S(6.2f),
                   ARGB(0x38, 0xFFFFFF));
}

/* Logo do Capivara OS: uma capivara de perfil (olhando para a esquerda) com
 * uma laranjinha na cabeça. Acabamento fosco: cores chapadas, sem brilho. */
static void logo(void) {
    uint32_t fur = HEX(0xA97A50), dark = HEX(0x86603F), deep = HEX(0x4A3020);
    rrect(10, 21, 3.6f, 7, 1.5f, dark);                 /* patas */
    rrect(23, 21, 3.6f, 7, 1.5f, dark);
    rrect(8, 11, 21.5f, 13.5f, 6.6f, fur);              /* corpo */
    rrect(9.5f, 19.5f, 18.5f, 4.5f, 2.2f, HEX(0x9A6E47));
    rrect(2.5f, 8, 13, 11.5f, 4.2f, fur);               /* cabeça */
    rrect(2.5f, 11.2f, 4.8f, 8, 2.3f, dark);            /* focinho */
    circle(3.8f, 12.8f, 0.6f, deep);                    /* narina */
    line(3.4f, 17, 6.2f, 17, 0.7f, deep);               /* boca */
    circle(12.4f, 8.4f, 1.9f, dark);                    /* orelha */
    circle(7.6f, 11.4f, 1.0f, deep);                    /* olho */
    circle(8.6f, 5.3f, 2.6f, HEX(0xE98A2E));            /* laranjinha */
    tri(8.6f, 2.9f, 11.2f, 1.6f, 10.4f, 3.6f, HEX(0x5E9E48));
}

/* O ">_" do Terminal: divisa de pontas retas e o cursor, deslocados de (dx, dy). */
static void prompt(float dx, float dy, uint32_t chev, uint32_t cursor) {
    float p[12] = { 8.0f, 7.6f, 13.0f, 11.1f, 8.0f, 14.6f, 8.0f, 12.65f, 10.21f, 11.1f, 8.0f, 9.55f };
    for (int i = 0; i < 12; i += 2) {
        p[i] += dx;
        p[i + 1] += dy;
    }
    poly(p, 6, chev);
    rrect(14.0f + dx, 13.0f + dy, 5.6f, 1.6f, 0.3f, cursor);
}

/* Tecla da calculadora: redonda (ou pílula), com degradê e a lateral mais
 * escura aparecendo embaixo, como uma tecla de verdade. */
static void key(float x, float y, float w, float h, uint32_t top, uint32_t bottom, uint32_t side) {
    rrect(x, y + 0.5f, w, h, h / 2, side);
    grad(x, y, w, h, h / 2, top, bottom);
}

void icon_draw(canvas_t *c, int icon, int x, int y, int size) {
    C = c;
    ox = x;
    oy = y;
    sc = size / 32.0f;

    switch (icon) {
    case ICON_LOGO:
        logo();
        break;

    /* ---------------- pastas ---------------- */
    case ICON_FOLDER:
        folder();
        break;
    case ICON_DOCUMENTS:
        folder();
        rrect(12, 14, 8, 10, 1, ENGRAVE);
        line(13.8f, 17, 18.2f, 17, 1, HEX(0x9AD3FD));
        line(13.8f, 19.5f, 18.2f, 19.5f, 1, HEX(0x9AD3FD));
        break;
    case ICON_DOWNLOADS:
        folder();
        line(16, 13.5f, 16, 22, 2.2f, ENGRAVE);
        line(12.5f, 18.5f, 16, 22.2f, 2.2f, ENGRAVE);
        line(19.5f, 18.5f, 16, 22.2f, 2.2f, ENGRAVE);
        break;
    case ICON_PICTURES:
        folder();
        circle(19.5f, 15.5f, 1.7f, ENGRAVE);
        tri(10, 24, 14.5f, 16.5f, 19, 24, ENGRAVE);
        tri(15, 24, 19, 19, 23, 24, ENGRAVE);
        break;
    case ICON_MUSIC:
        folder();
        line(18.5f, 13.5f, 18.5f, 21.5f, 1.8f, ENGRAVE);
        line(18.5f, 13.5f, 22, 15, 1.8f, ENGRAVE);
        circle(16.3f, 22, 2.6f, ENGRAVE);
        break;
    case ICON_DESKTOP:
        folder();
        rrect(10.5f, 14, 11, 7.5f, 1, ENGRAVE);
        line(14, 23.5f, 18, 23.5f, 1.5f, ENGRAVE);
        break;
    case ICON_HOME:
        folder();
        tri(16, 12.5f, 9.5f, 18.5f, 22.5f, 18.5f, ENGRAVE);
        rrect(11.5f, 18, 9, 6, 0.5f, ENGRAVE);
        break;

    /* ---------------- arquivos ---------------- */
    case ICON_FILE:
        page(HEX(0xFFFFFF), HEX(0xB8B8B8));
        break;
    case ICON_TEXT:
        page(HEX(0xFFFFFF), HEX(0xB8B8B8));
        for (int i = 0; i < 5; i++)
            line(10, 11 + i * 3.6f, i == 4 ? 17 : 22, 11 + i * 3.6f, 1.3f, HEX(0x8A96A3));
        break;
    case ICON_IMAGE:
        page(HEX(0xFFFFFF), HEX(0xB8B8B8));
        rrect(9, 9, 14, 15, 1, HEX(0x7EC8F0));
        circle(19, 12.5f, 1.8f, HEX(0xFFE066));
        tri(9, 24, 14, 15, 19, 24, HEX(0x3E9B5F));
        tri(14, 24, 19, 18, 23, 24, HEX(0x58B97A));
        break;
    case ICON_DISK:
    case ICON_COMPUTER:
        rrect(3, 10.6f, 26, 14, 3, ARGB(0x28, 0x000000));
        gfx_round_gradient(C, (int)(X(3) + 0.5f), (int)(Y(9.5f) + 0.5f), (int)(S(26) + 0.5f), (int)(S(14) + 0.5f), S(3),
                           HEX(0xF1F1F3), HEX(0xB9B9BE));
        gfx_round_rect(C, (int)(X(3) + 0.5f), (int)(Y(9.5f) + 0.5f), (int)(S(26) + 0.5f), (int)(S(14) + 0.5f), S(3),
                       HEX(0x8E8E93));
        line(6.5f, 19.5f, 20, 19.5f, 1, HEX(0x9C9CA1));
        circle(24.5f, 19.5f, 1.2f, HEX(0x34C759));
        break;

    /* ---------------- aplicativos ---------------- */
    case ICON_EXPLORER:
        squircle(HEX(0x7CD0FF), HEX(0x1D74F0));
        rrect(8, 10.5f, 7, 3, 1, ARGB(0xC8, 0xFFFFFF));
        rrect(8, 12.5f, 16, 11.5f, 2, HEX(0xFFFFFF));
        rrect(8, 15, 16, 9, 2, HEX(0xE4F1FF));
        break;
    case ICON_TERMINAL: {
        /* Um monitorzinho: moldura preta com espessura cinza embaixo, tela
         * embutida um tom mais clara e o prompt ">_" com o cursor laranja. */
        rrect(2.5f, 4.2f, 27, 26.4f, 6.7f, ARGB(0x10, 0x000000));
        rrect(2.8f, 3.8f, 26.4f, 26.2f, 6.4f, ARGB(0x1C, 0x000000));
        rrect(3, 3.45f, 26, 26, 6.2f, HEX(0x55555B));
        grad(3, 3, 26, 26, 6.2f, HEX(0x2A2A2E), HEX(0x09090A));
        outline_top(3, 3, 26, 26, 6.2f, 11, ARGB(0x50, 0xD6E4F0));
        /* tela: um sulco escuro em volta, degradê e um fio de luz na borda */
        int sx = (int)(X(5.4f) + 0.5f), sy = (int)(Y(5.4f) + 0.5f), sw = (int)(S(21.2f) + 0.5f);
        float sr = S(4.0f);
        gfx_round_fill(C, sx - 1, sy - 1, sw + 2, sw + 2, sr + 1, ARGB(0xC0, 0x000000));
        gfx_round_gradient(C, sx, sy, sw, sw, sr, HEX(0x323236), HEX(0x1A1A1C));
        gfx_round_rect(C, sx, sy, sw, sw, sr, ARGB(0x1A, 0xFFFFFF));
        /* sombra difusa do prompt: a mesma forma, transparente, espalhada num disco */
        for (int i = 0; i < 8; i++) {
            float a = i * GFX_PI / 4;
            prompt(0.35f + 0.6f * gfx_cos(a), 0.8f + 0.6f * gfx_sin(a), ARGB(0x16, 0x000000), ARGB(0x16, 0x000000));
        }
        rrect(13.6f, 12.6f, 6.4f, 2.4f, 0.8f, ARGB(0x30, 0xFF9F0A));     /* brilho do cursor */
        prompt(0, 0, HEX(0xF5F5F7), HEX(0xFF9F0A));
        break;
    }
    case ICON_NOTEPAD:
        squircle(HEX(0xFFFFFF), HEX(0xEDEDED));
        gfx_round_fill_corners(C, (int)(X(3) + 0.5f), (int)(Y(3) + 0.5f), (int)(S(26) + 0.5f), (int)(S(8) + 0.5f), S(6.2f),
                               HEX(0xFFCC00), CORNER_TL | CORNER_TR);
        line(3.5f, 11.2f, 28.5f, 11.2f, 0.8f, HEX(0xE0B000));
        for (int i = 0; i < 4; i++) line(7, 15 + i * 3.6f, 25, 15 + i * 3.6f, 0.9f, HEX(0xD2D2D7));
        line(7, 15 - 0.2f, 18, 15 - 0.2f, 1.2f, HEX(0x8E8E93));
        break;
    case ICON_CALC: {
        /* Calculadora de pé: corpo grafite com espessura embaixo, visor cinza
         * embutido, teclas claras e a coluna de operações em laranja. */
        rrect(5.2f, 4.2f, 21.6f, 27.4f, 5.2f, ARGB(0x14, 0x000000));
        rrect(5.6f, 3.6f, 20.8f, 27, 4.8f, ARGB(0x28, 0x000000));
        rrect(6, 3.2f, 20, 27, 4.4f, HEX(0x15161A));
        grad(6, 2.5f, 20, 27, 4.4f, HEX(0x40424B), HEX(0x24252B));
        outline_top(6, 2.5f, 20, 27, 4.4f, 8, ARGB(0x48, 0xFFFFFF));
        /* visor: sulco escuro em cima (fica "afundado") e um reflexo na diagonal */
        rrect(8.6f, 4.8f, 14.8f, 6, 1.6f, ARGB(0xA0, 0x101114));
        grad(8.6f, 5.2f, 14.8f, 5.6f, 1.5f, HEX(0x7D8188), HEX(0x9A9EA5));
        tri(8.8f, 5.4f, 15.5f, 5.4f, 8.8f, 10.2f, ARGB(0x16, 0xFFFFFF));
        /* teclas: 3 colunas; a última fileira tem a tecla larga do zero */
        uint32_t lt = HEX(0xEFEFF2), lb = HEX(0xCBCBD1), ls = HEX(0x8A8B93);
        uint32_t ot = HEX(0xFFB648), ob = HEX(0xFF9500), os = HEX(0xB45A00);
        float d = 3.8f, xs[3] = { 8.6f, 14.1f, 19.6f }, ys[3] = { 12.6f, 18.1f, 23.6f };
        for (int r = 0; r < 3; r++) {
            if (r < 2) {
                key(xs[0], ys[r], d, d, lt, lb, ls);
                key(xs[1], ys[r], d, d, lt, lb, ls);
            } else {
                key(xs[0], ys[r], xs[1] + d - xs[0], d, lt, lb, ls);
            }
            key(xs[2], ys[r], d, d, ot, ob, os);
        }
        break;
    }
    case ICON_SETTINGS: {
        squircle(HEX(0xB0B0B5), HEX(0x6C6C70));
        uint32_t g = HEX(0x2C2C2E);
        for (int i = 0; i < 12; i++) {
            float a = i * GFX_PI / 6;
            line(16 + 6.4f * gfx_cos(a), 16 + 6.4f * gfx_sin(a), 16 + 9.4f * gfx_cos(a), 16 + 9.4f * gfx_sin(a), 2.3f, g);
        }
        ring(16, 16, 6.2f, 3.4f, g);
        circle(16, 16, 4.3f, HEX(0x8E8E93));
        circle(16, 16, 1.8f, g);
        break;
    }
    case ICON_TASKMGR:
        squircle(HEX(0x2C2C2E), HEX(0x121214));
        for (int i = 0; i < 3; i++) line(6, 11 + i * 5, 26, 11 + i * 5, 0.6f, ARGB(0x40, 0xFFFFFF));
        tri(6, 25, 6, 20, 11, 15, ARGB(0x48, 0x32D74B));
        tri(6, 25, 11, 15, 11, 25, ARGB(0x48, 0x32D74B));
        tri(11, 25, 11, 15, 15, 19, ARGB(0x48, 0x32D74B));
        tri(11, 25, 15, 19, 15, 25, ARGB(0x48, 0x32D74B));
        tri(15, 25, 15, 19, 20, 9, ARGB(0x48, 0x32D74B));
        tri(15, 25, 20, 9, 20, 25, ARGB(0x48, 0x32D74B));
        tri(20, 25, 20, 9, 26, 14, ARGB(0x48, 0x32D74B));
        tri(20, 25, 26, 14, 26, 25, ARGB(0x48, 0x32D74B));
        line(6, 20, 11, 15, 1.6f, HEX(0x32D74B));
        line(11, 15, 15, 19, 1.6f, HEX(0x32D74B));
        line(15, 19, 20, 9, 1.6f, HEX(0x32D74B));
        line(20, 9, 26, 14, 1.6f, HEX(0x32D74B));
        break;
    case ICON_PAINT:
        squircle(HEX(0xFFFFFF), HEX(0xEBEBEB));
        circle(14.5f, 17.5f, 8.8f, HEX(0xF0D2A0));
        circle(17.5f, 21.5f, 2.6f, HEX(0xFFFFFF));
        circle(10, 14, 1.9f, HEX(0xE53935));
        circle(14, 11.4f, 1.9f, HEX(0xFDD835));
        circle(9.8f, 19.4f, 1.9f, HEX(0x1E88E5));
        circle(18.6f, 13.6f, 1.9f, HEX(0x43A047));
        line(19.5f, 18, 26, 7, 1.8f, HEX(0x8D5A2B));
        line(18.6f, 19.6f, 20, 17.4f, 2.6f, HEX(0x37474F));
        break;
    case ICON_PHOTOS:
        squircle(HEX(0x9BD8FF), HEX(0x3A86F2));
        circle(21.5f, 10.5f, 2.6f, HEX(0xFFE066));
        tri(4, 26, 13, 13.5f, 22, 26, HEX(0xFFFFFF));
        tri(14, 26, 21, 17.5f, 28, 26, HEX(0xDCEBFA));
        break;
    case ICON_SNIP:
        squircle(HEX(0xC08CFF), HEX(0x6E3BD6));
        for (int i = 0; i < 4; i++) {
            line(8 + i * 4.5f, 10, 10 + i * 4.5f, 10, 1.4f, HEX(0xFFFFFF));
            line(8 + i * 4.5f, 22, 10 + i * 4.5f, 22, 1.4f, HEX(0xFFFFFF));
        }
        line(8, 10, 8, 22, 1.4f, HEX(0xFFFFFF));
        line(24, 10, 24, 22, 1.4f, HEX(0xFFFFFF));
        line(16, 13, 16, 19, 1.3f, HEX(0xFFD43B));
        line(13, 16, 19, 16, 1.3f, HEX(0xFFD43B));
        break;
    case ICON_LAUNCHPAD: {
        squircle(HEX(0x9EA4AE), HEX(0x5D636E));
        static const uint32_t tiles[9] = { 0xFF5F57, 0xFEBC2E, 0x28C840, 0x0A84FF, 0xBF5AF2, 0xFF9F0A,
                                           0x64D2FF, 0xFF375F, 0x30D158 };
        for (int i = 0; i < 9; i++)
            rrect(8 + (i % 3) * 6, 8 + (i / 3) * 6, 4.2f, 4.2f, 1.2f, HEX(tiles[i]));
        break;
    }
    case ICON_INFO:
        circle(16, 16, 13, HEX(0x0A84FF));
        line(16, 14, 16, 23, 3, HEX(0xFFFFFF));
        circle(16, 9.5f, 1.9f, HEX(0xFFFFFF));
        break;
    case ICON_USER:
        circle(16, 16, 15, HEX(0xA9B4C2));
        circle(16, 12.5f, 5.5f, HEX(0xFFFFFF));
        rrect(7, 19.5f, 18, 12, 9, HEX(0xFFFFFF));
        break;
    case ICON_CLOCK:
        circle(16, 16, 13, HEX(0x1C1C1E));
        circle(16, 16, 11, HEX(0xFFFFFF));
        line(16, 16, 16, 9, 2, HEX(0x1B1B1B));
        line(16, 16, 21, 18, 2, HEX(0x1B1B1B));
        circle(16, 16, 1.2f, HEX(0xFF9F0A));
        break;
    case ICON_TRASH:
    case ICON_TRASH_FULL: {
        if (icon == ICON_TRASH_FULL) {
            circle(12, 9, 4.2f, HEX(0xFFFFFF));
            circle(18, 7.5f, 4.5f, HEX(0xF2F2F2));
            circle(22, 10, 3.6f, HEX(0xE8E8E8));
            gfx_ring(C, X(18), Y(7.5f), S(4.5f), S(0.8f), HEX(0xC8C8C8));
        }
        uint32_t body = ARGB(0xE6, 0xE9EBEE), edge = HEX(0x8E949C);
        static const float cesto[8] = { 7, 11.5f, 25, 11.5f, 23, 29, 9, 29 };   /* uma peça só: sem costura */
        poly(cesto, 4, body);
        line(7, 11.5f, 9, 29, 1, edge);
        line(25, 11.5f, 23, 29, 1, edge);
        line(9, 29, 23, 29, 1, edge);
        rrect(5.5f, 10, 21, 3, 1.5f, HEX(0xB0B6BE));
        for (int i = 0; i < 4; i++) line(11 + i * 3.4f, 15, 11.4f + i * 3.1f, 26.5f, 1, ARGB(0x90, 0x8E949C));
        break;
    }
    }
}

void glyph_draw(canvas_t *c, int glyph, int x, int y, int size, uint32_t col) {
    C = c;
    ox = x;
    oy = y;
    sc = size / 32.0f;
    float w = 2.6f;

    switch (glyph) {
    case GLYPH_BACK:
        line(8, 16, 24, 16, w, col); line(8, 16, 15, 9, w, col); line(8, 16, 15, 23, w, col);
        break;
    case GLYPH_FORWARD:
        line(8, 16, 24, 16, w, col); line(24, 16, 17, 9, w, col); line(24, 16, 17, 23, w, col);
        break;
    case GLYPH_UP:
        line(16, 8, 16, 24, w, col); line(16, 8, 9, 15, w, col); line(16, 8, 23, 15, w, col);
        break;
    case GLYPH_REFRESH:
        for (int i = 0; i < 28; i++) {
            float a0 = -GFX_PI * 0.2f + i * GFX_PI * 1.55f / 28, a1 = a0 + GFX_PI * 1.55f / 28;
            line(16 + 8 * gfx_cos(a0), 16 + 8 * gfx_sin(a0), 16 + 8 * gfx_cos(a1), 16 + 8 * gfx_sin(a1), w, col);
        }
        tri(18, 4, 25, 9.5f, 17.5f, 12.5f, col);
        break;
    case GLYPH_SEARCH:
        ring(13.5f, 13.5f, 7.5f, w, col);
        line(19, 19, 26, 26, w + 0.4f, col);
        break;
    case GLYPH_POWER:
        for (int i = 0; i < 30; i++) {
            float a0 = -GFX_PI / 2 + 0.75f + i * (2 * GFX_PI - 1.5f) / 30;
            float a1 = a0 + (2 * GFX_PI - 1.5f) / 30;
            line(16 + 10 * gfx_cos(a0), 17 + 10 * gfx_sin(a0), 16 + 10 * gfx_cos(a1), 17 + 10 * gfx_sin(a1), w, col);
        }
        line(16, 4, 16, 15, w, col);
        break;
    case GLYPH_RESTART:
        for (int i = 0; i < 30; i++) {
            float a0 = -GFX_PI / 2 + 0.5f + i * (2 * GFX_PI - 1.0f) / 30;
            float a1 = a0 + (2 * GFX_PI - 1.0f) / 30;
            line(16 + 10 * gfx_cos(a0), 16 + 10 * gfx_sin(a0), 16 + 10 * gfx_cos(a1), 16 + 10 * gfx_sin(a1), w, col);
        }
        tri(11, 2.5f, 17, 6, 11, 10, col);
        break;
    case GLYPH_NEW_FOLDER:
        line(4, 9, 4, 26, w, col); line(4, 26, 26, 26, w, col); line(26, 26, 26, 16, w, col);
        line(4, 9, 12, 9, w, col); line(12, 9, 14, 12, w, col); line(14, 12, 18, 12, w, col);
        line(24, 4, 24, 12, w, col); line(20, 8, 28, 8, w, col);
        break;
    case GLYPH_NEW_FILE:
        line(7, 4, 7, 28, w, col); line(7, 28, 25, 28, w, col); line(25, 28, 25, 18, w, col);
        line(7, 4, 15, 4, w, col);
        line(22, 3, 22, 13, w, col); line(17, 8, 27, 8, w, col);
        break;
    case GLYPH_DELETE:
        line(6, 8, 26, 8, w, col); line(12, 8, 13, 4, w, col); line(13, 4, 19, 4, w, col); line(19, 4, 20, 8, w, col);
        line(9, 8, 10, 27, w, col); line(10, 27, 22, 27, w, col); line(22, 27, 23, 8, w, col);
        line(14, 13, 14, 22, w * 0.8f, col); line(18, 13, 18, 22, w * 0.8f, col);
        break;
    case GLYPH_RENAME:
        line(6, 26, 9, 18, w, col); line(9, 18, 22, 5, w, col); line(22, 5, 27, 10, w, col);
        line(27, 10, 14, 23, w, col); line(14, 23, 6, 26, w, col);
        break;
    case GLYPH_COPY:
        line(11, 4, 26, 4, w, col); line(26, 4, 26, 23, w, col);
        line(26, 23, 11, 23, w, col); line(11, 23, 11, 4, w, col);
        line(6, 10, 6, 28, w, col); line(6, 28, 20, 28, w, col);
        break;
    case GLYPH_CUT:
        ring(9, 23, 4, w, col); ring(23, 23, 4, w, col);
        line(11.5f, 20, 22, 4, w, col); line(20.5f, 20, 10, 4, w, col);
        break;
    case GLYPH_PASTE:
        line(7, 7, 7, 28, w, col); line(7, 28, 25, 28, w, col); line(25, 28, 25, 7, w, col);
        line(7, 7, 11, 7, w, col); line(21, 7, 25, 7, w, col);
        rrect(11, 4, 10, 6, 2, col);
        break;
    case GLYPH_CHEVRON_RIGHT:
        line(12, 8, 20, 16, w, col); line(20, 16, 12, 24, w, col);
        break;
    case GLYPH_CHEVRON_DOWN:
        line(8, 12, 16, 20, w, col); line(16, 20, 24, 12, w, col);
        break;
    case GLYPH_CHEVRON_UP:
        line(8, 20, 16, 12, w, col); line(16, 12, 24, 20, w, col);
        break;
    case GLYPH_CHECK:
        line(6, 17, 13, 24, w, col); line(13, 24, 26, 9, w, col);
        break;
    case GLYPH_SAVE:
        line(5, 5, 5, 27, w, col); line(5, 27, 27, 27, w, col); line(27, 27, 27, 10, w, col);
        line(27, 10, 22, 5, w, col); line(22, 5, 5, 5, w, col);
        line(10, 5, 10, 12, w, col); line(10, 12, 20, 12, w, col); line(20, 12, 20, 5, w, col);
        rrect(10, 17, 12, 7, 1, col);
        break;
    case GLYPH_OPEN:
        line(4, 7, 4, 26, w, col); line(4, 7, 11, 7, w, col); line(11, 7, 13, 10, w, col);
        line(13, 10, 24, 10, w, col); line(24, 10, 24, 14, w, col);
        line(4, 26, 9, 15, w, col); line(9, 15, 29, 15, w, col); line(29, 15, 24, 26, w, col); line(24, 26, 4, 26, w, col);
        break;
    case GLYPH_LOCK:
        rrect(7, 14, 18, 14, 2.5f, col);
        for (int i = 0; i < 16; i++) {
            float a0 = GFX_PI + i * GFX_PI / 16, a1 = a0 + GFX_PI / 16;
            line(16 + 6 * gfx_cos(a0), 13 + 6 * gfx_sin(a0), 16 + 6 * gfx_cos(a1), 13 + 6 * gfx_sin(a1), w, col);
        }
        line(10, 13, 10, 15, w, col); line(22, 13, 22, 15, w, col);
        break;
    case GLYPH_SPEAKER:
        tri(4, 12, 4, 20, 10, 16, col);
        rrect(4, 12, 6, 8, 0.5f, col);
        tri(9, 12, 16, 5, 16, 27, col);
        tri(9, 12, 9, 20, 16, 27, col);
        for (int i = 0; i < 10; i++) {
            float a0 = -0.8f + i * 1.6f / 10, a1 = a0 + 0.16f;
            line(16 + 6 * gfx_cos(a0), 16 + 6 * gfx_sin(a0), 16 + 6 * gfx_cos(a1), 16 + 6 * gfx_sin(a1), 2, col);
            line(16 + 11 * gfx_cos(a0), 16 + 11 * gfx_sin(a0), 16 + 11 * gfx_cos(a1), 16 + 11 * gfx_sin(a1), 2, col);
        }
        break;
    case GLYPH_LOGO:
        /* silhueta monocromática da capivara para a barra de menus; a laranja
         * fica separada da cabeça por uma fresta, para ser reconhecível */
        rrect(10, 21, 4, 6, 1.6f, col);                 /* patas curtas */
        rrect(23.5f, 21, 4, 6, 1.6f, col);
        rrect(8.5f, 12.5f, 22.5f, 11.5f, 5.6f, col);    /* corpo comprido e baixo */
        rrect(1.5f, 9.5f, 13, 11, 3.8f, col);           /* cabeça "quadrada", focinho reto */
        circle(12.2f, 9.6f, 2.1f, col);                 /* orelha */
        circle(7.4f, 5.2f, 2.6f, col);                  /* laranjinha */
        break;
    case GLYPH_TASKVIEW:
        line(4, 9, 20, 9, w, col); line(20, 9, 20, 25, w, col);
        line(20, 25, 4, 25, w, col); line(4, 25, 4, 9, w, col);
        line(10, 4, 28, 4, w, col); line(28, 4, 28, 19, w, col);
        break;
    case GLYPH_BELL:
        for (int i = 0; i < 16; i++) {
            float a0 = GFX_PI + i * GFX_PI / 16, a1 = a0 + GFX_PI / 16;
            line(16 + 8 * gfx_cos(a0), 14 + 8 * gfx_sin(a0), 16 + 8 * gfx_cos(a1), 14 + 8 * gfx_sin(a1), w, col);
        }
        line(8, 14, 8, 21, w, col); line(24, 14, 24, 21, w, col);
        line(5, 23, 27, 23, w, col);
        line(13, 27, 19, 27, w, col);
        break;
    case GLYPH_ZOOM_IN:
        ring(13.5f, 13.5f, 8, w, col); line(19.5f, 19.5f, 26, 26, w + 0.4f, col);
        line(9.5f, 13.5f, 17.5f, 13.5f, w, col); line(13.5f, 9.5f, 13.5f, 17.5f, w, col);
        break;
    case GLYPH_ZOOM_OUT:
        ring(13.5f, 13.5f, 8, w, col); line(19.5f, 19.5f, 26, 26, w + 0.4f, col);
        line(9.5f, 13.5f, 17.5f, 13.5f, w, col);
        break;
    case GLYPH_RESTORE:
        for (int i = 0; i < 28; i++) {
            float a0 = GFX_PI * 1.15f + i * GFX_PI * 1.55f / 28, a1 = a0 + GFX_PI * 1.55f / 28;
            line(16 + 9 * gfx_cos(a0), 17 + 9 * gfx_sin(a0), 16 + 9 * gfx_cos(a1), 17 + 9 * gfx_sin(a1), w, col);
        }
        tri(3, 9, 11, 7, 7, 15, col);
        break;
    case GLYPH_CLOSE:
        line(9, 9, 23, 23, w, col); line(23, 9, 9, 23, w, col);
        break;
    case GLYPH_NETWORK:
        line(4, 5, 28, 5, w, col); line(28, 5, 28, 21, w, col);
        line(28, 21, 4, 21, w, col); line(4, 21, 4, 5, w, col);
        line(16, 21, 16, 26, w, col);
        line(9, 27, 23, 27, w, col);
        break;
    }
}
