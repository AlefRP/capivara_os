/*
 * Papel de parede "Capivara": beira de brejo no fim de tarde, em pixel art.
 * Uma capivara na margem de barro (com a laranjinha na cabeça e um
 * passarinho nas costas), outra nadando no rio, aguapés, taboas, mata com
 * buritis e um ipê-amarelo do outro lado, tuiuiús voando e o sol se pondo.
 *
 * Tudo é desenhado numa grade pequena (256 pixels de largura) com uma paleta
 * limitada e depois ampliado SEM suavização (vizinho mais próximo), para que
 * cada "pixel" vire um quadradinho nítido, como nos jogos antigos.
 *
 * Técnicas de pixel art usadas aqui:
 *   - pontilhado ordenado (matriz de Bayer 4x4) para fazer degradês com
 *     poucas cores;
 *   - reflexo na água: cada linha do rio copia uma linha do céu "espelhada",
 *     escurecida, com ondinhas que quebram o reflexo;
 *   - sprites desenhados como texto (cada letra é uma cor da paleta);
 *   - ruído pseudoaleatório de semente fixa (o desenho é sempre igual).
 */
#include "gui/wallpaper.h"
#include "gui/font.h"
#include "gui/wm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/utf8.h"

#define LW 256

static canvas_t *g;
static int LH, horizon;
static int tree_top[LW], shore[LW];
static int sun_x;
static uint32_t seed;

static uint32_t rnd(void) {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
}

static inline void px(int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < LW && y < LH) g->px[y * g->stride + x] = c;
}

static void rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) px(x + i, y + j, c);
}

static const int bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

/* desenha um sprite em texto; mirror = olhar para o outro lado */
typedef struct { char key; uint32_t color; } pal_t;

static void sprite(int x0, int y0, const char **art, int rows, const pal_t *pal, int npal, bool mirror) {
    for (int j = 0; j < rows; j++) {
        int w = (int)strlen(art[j]);
        for (int i = 0; i < w; i++) {
            char a = art[j][mirror ? w - 1 - i : i];
            for (int k = 0; k < npal; k++)
                if (pal[k].key == a) { px(x0 + i, y0 + j, pal[k].color); break; }
        }
    }
}

/* ---------------------------------------------------------------- céu */
static const uint32_t SKY[7] = {
    HEX(0x22305E), HEX(0x3D3F7C), HEX(0x6B4A8C), HEX(0xA9588A),
    HEX(0xDE7A6E), HEX(0xF6A35C), HEX(0xFFCF7E),
};

static uint32_t sky_at(int x, int y) {
    if (y < 0) y = 0;
    int t = y * 6 * 16 / horizon;
    int band = t / 16, frac = t % 16;
    int b = band + (frac > bayer[y & 3][x & 3] ? 1 : 0);
    return SKY[b > 6 ? 6 : b];
}

static void sky(void) {
    for (int y = 0; y < horizon; y++)
        for (int x = 0; x < LW; x++) px(x, y, sky_at(x, y));
    /* primeiras estrelas, só no alto */
    for (int i = 0; i < 24; i++) px(rnd() % LW, rnd() % (horizon / 4), rnd() % 2 ? HEX(0xFFFFFF) : HEX(0xC9CCFF));
}

static void sun(void) {
    int cx = sun_x, cy = horizon - 7, r = 13;
    for (int y = cy - r - 5; y <= cy + r + 5; y++)
        for (int x = cx - r - 5; x <= cx + r + 5; x++) {
            if (y >= horizon) continue;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 <= r * r) px(x, y, d2 < (r - 3) * (r - 3) ? HEX(0xFFF6CC) : HEX(0xFFE39A));
            else if (d2 <= (r + 5) * (r + 5) && bayer[y & 3][x & 3] < 6) px(x, y, HEX(0xFFC27A));   /* halo */
        }
}

/* nuvens finas, iluminadas por baixo pelo sol */
static void clouds(void) {
    static const int c[][3] = { { 12, 30, 46 }, { 30, 36, 30 }, { 150, 22, 54 }, { 170, 28, 34 }, { 96, 54, 40 },
                                { 200, 60, 44 }, { 20, 64, 36 } };
    for (unsigned k = 0; k < sizeof(c) / sizeof(c[0]); k++) {
        int x = c[k][0], y = c[k][1], w = c[k][2];
        rect(x + 4, y - 1, w - 10, 1, HEX(0xC56C8C));
        rect(x, y, w, 1, HEX(0xF2A07E));
        rect(x + 6, y + 1, w - 14, 1, HEX(0xFFC896));
    }
}

/* tuiuiús voando: corpo branco, cabeça e pescoço pretos, colar vermelho */
static void birds(void) {
    static const char *art[] = {
        "WW.......WW",
        ".WWW...WWW.",
        "...WWWWW...",
        "....KKR....",
        "....K......",
    };
    static const pal_t pal[] = { { 'W', HEX(0xF4F1EC) }, { 'K', HEX(0x1A1A1A) }, { 'R', HEX(0xD8333A) } };
    sprite(46, 30, art, 5, pal, 3, false);
    sprite(64, 22, art, 5, pal, 3, false);
    sprite(80, 34, art, 5, pal, 3, false);
}

/* ------------------------------------------------- mata do outro lado */
static void far_bank(void) {
    for (int x = 0; x < LW; x++) {
        int h = 8 + (int)(4 * gfx_sin(x / 9.0f)) + (int)(3 * gfx_sin(x / 3.7f + 1)) + (int)(2 * gfx_sin(x / 27.0f));
        tree_top[x] = horizon - h;
        for (int y = tree_top[x]; y < horizon; y++) px(x, y, HEX(0x1F3A30));
        px(x, tree_top[x], HEX(0x4C7A47));                         /* borda iluminada */
        if ((x * 7) % 5 == 0) px(x, tree_top[x] + 1, HEX(0x34573C));
    }
    /* buritis: tronco fino e copa em leque */
    static const int palms[] = { 112, 128, 214, 236 };
    for (int p = 0; p < 4; p++) {
        int x = palms[p], top = horizon - 24 - (p % 2) * 5;
        for (int y = top; y < tree_top[x] + 2; y++) px(x, y, HEX(0x2A3B2E));
        static const int leaf[][2] = { { -5, 2 }, { -4, -1 }, { -2, -3 }, { 0, -4 }, { 2, -3 }, { 4, -1 }, { 5, 2 } };
        for (int l = 0; l < 7; l++)
            for (int s = 1; s <= 4; s++)
                px(x + leaf[l][0] * s / 4, top + leaf[l][1] * s / 4, l % 2 ? HEX(0x2D5238) : HEX(0x1F3A30));
    }
    /* um ipê-amarelo florido no meio da mata */
    int ix = 54, iy = horizon - 21;
    for (int y = iy + 4; y < horizon; y++) px(ix, y, HEX(0x3A2A1C));
    for (int y = -7; y <= 7; y++)
        for (int x = -9; x <= 9; x++) {
            if (x * x * 49 + y * y * 81 > 49 * 81 + (int)(rnd() % 400)) continue;
            uint32_t c = (x + y < -3) ? HEX(0xFFD84A) : HEX(0xEDB012);
            if (rnd() % 5 == 0) c = HEX(0xC98A00);
            px(ix + x, iy + y, c);
        }
}

/* ------------------------------------------------------------ rio */
/* Água calma = espelho: cada linha copia a linha simétrica acima do
 * horizonte (céu, mata, buritis, ipê, sol), escurecida e azulada conforme
 * fica mais perto de quem olha. Deslocamentos de 1 pixel "quebram" o reflexo. */
static void river(void) {
    for (int y = horizon; y < LH; y++) {
        int d = y - horizon;
        int wob = (d % 3 == 1) ? (int)(rnd() % 3) - 1 : 0;
        int sy = horizon - 1 - d;
        if (sy < 0) sy = 0;
        int dark = 80 + d * 2;
        if (dark > 175) dark = 175;
        for (int x = 0; x < LW; x++) {
            int sx = x + wob;
            if (sx < 0) sx = 0;
            if (sx >= LW) sx = LW - 1;
            uint32_t c = color_mix(g->px[sy * g->stride + sx], HEX(0x162640), dark);
            if (d % 4 == 3 && bayer[y & 3][x & 3] < 5) c = color_mix(c, HEX(0x0E1A2E), 90);   /* faixas de ondulação */
            px(x, y, c | 0xFF000000u);
        }
        /* caminho de luz do sol: tracinhos que se alargam e espaçam */
        int half = 2 + d / 3;
        if (d % 2 == 0)
            for (int x = sun_x - half; x <= sun_x + half; x++)
                if (((x * 7 + y * 13) % 9) < 3) px(x, y, (x - sun_x) * (x - sun_x) < 6 ? HEX(0xFFF1C2) : HEX(0xF7B26E));
        /* ondinhas soltas */
        if (d > 3 && rnd() % 3 == 0) {
            int x = rnd() % LW, len = 3 + rnd() % (3 + d / 5);
            rect(x, y, len, 1, rnd() % 3 ? HEX(0x8FA6BF) : HEX(0x0E1A2E));
        }
    }
}

/* aguapés: folhas redondas vistas de lado (achatadas) com flor rosa */
static void lilies(void) {
    static const int pads[][3] = { { 24, 112, 8 }, { 40, 118, 6 }, { 150, 126, 9 }, { 206, 110, 6 },
                                   { 226, 120, 8 }, { 70, 131, 7 }, { 186, 136, 7 } };
    for (unsigned p = 0; p < sizeof(pads) / sizeof(pads[0]); p++) {
        int cx = pads[p][0], cy = pads[p][1], rx = pads[p][2], ry = rx / 3 + 1;
        for (int y = -ry; y <= ry; y++)
            for (int x = -rx; x <= rx; x++) {
                if (x * x * ry * ry + y * y * rx * rx > rx * rx * ry * ry) continue;
                if (x > rx / 3 && y < 0 && x < rx / 3 + 2) continue;          /* recorte da folha */
                px(cx + x, cy + y, y < 0 ? HEX(0x5DAE4E) : HEX(0x2F7A3A));
            }
        rect(cx - rx + 1, cy + ry + 1, rx * 2 - 1, 1, HEX(0x10283C));        /* sombra na água */
        if (p % 2 == 0) {
            px(cx - 1, cy - ry - 1, HEX(0xF07AA8));
            px(cx, cy - ry - 2, HEX(0xFFB3CF));
            px(cx + 1, cy - ry - 1, HEX(0xF07AA8));
            px(cx, cy - ry - 1, HEX(0xFFE26B));
        }
    }
}

/* capivara nadando: só a cabeça e o lombo fora d'água, com marolas */
static void swimmer(int x0, int y0) {
    static const char *art[] = {
        "..........KK...",
        "...KKKKKKKLLK..",
        "..KLLLLLLLLLLK.",
        ".KBBBBBBBBBBEBK",
        "KBBBBBBBBBBBBBSK",
        "WWWBBBBBBBBBBSNK",
    };
    static const pal_t pal[] = {
        { 'K', HEX(0x3A2312) }, { 'B', HEX(0x8E6040) }, { 'L', HEX(0xAE7E52) }, { 'S', HEX(0x6E4629) },
        { 'N', HEX(0x2A170B) }, { 'E', HEX(0x140B05) }, { 'W', HEX(0xBFD8E8) },
    };
    sprite(x0, y0, art, 6, pal, 7, false);
    /* marolas em volta */
    rect(x0 - 6, y0 + 6, 5, 1, HEX(0xBFD8E8));
    rect(x0 + 17, y0 + 6, 6, 1, HEX(0xBFD8E8));
    rect(x0 - 10, y0 + 8, 6, 1, HEX(0x8FB4CC));
    rect(x0 + 22, y0 + 8, 7, 1, HEX(0x8FB4CC));
    rect(x0 - 2, y0 + 7, 20, 1, HEX(0x10283C));
}

/* ------------------------------------------------- margem de barro */
static void near_bank(void) {
    for (int x = 0; x < LW; x++) {
        int s = horizon + 60 + (int)(4 * gfx_sin(x / 23.0f)) + (int)(3 * gfx_sin(x / 61.0f + 1));
        if (x < 70) s -= (70 - x) / 6;              /* margem mais alta à esquerda */
        shore[x] = s;
        int phase = rnd() % 4;
        for (int y = s; y < LH; y++) {
            int d = y - s;
            uint32_t c;
            if (d == 0) c = (rnd() % 4 == 0) ? HEX(0xB59A7A) : HEX(0x3A2414);     /* lama molhada, com brilho */
            else if (d < 3) c = (bayer[y & 3][x & 3] < 7) ? HEX(0x4E321E) : HEX(0x5A3A22);
            else if (d < 5) c = (bayer[y & 3][x & 3] < 9) ? HEX(0x6B4628) : HEX(0x5A3A22);
            else if (d < 7) c = (bayer[y & 3][x & 3] < 8) ? HEX(0x4E8E3A) : HEX(0x6B4628);
            else {
                /* capim: degradê calmo, escurecendo para baixo */
                int depth = (d - 7) * 20 / (LH - s + 1);
                c = (bayer[y & 3][x & 3] < depth) ? HEX(0x2F5E2A) : HEX(0x3E7A34);
            }
            px(x, y, c);
        }
        /* tufos de capim espaçados: folhinhas verticais de 2 a 4 pixels */
        if (x % 3 == phase % 3) {
            int ty = s + 8 + (int)(rnd() % (LH - s - 8 > 1 ? LH - s - 8 : 1));
            int th = 2 + (int)(rnd() % 3);
            for (int k = 0; k < th; k++) px(x, ty - k, k == th - 1 ? HEX(0x7DBA54) : HEX(0x5E9E44));
        }
        if (rnd() % 19 == 0) px(x, s + 2, HEX(0x2E1C10));          /* pegadas */
        if (rnd() % 23 == 0) px(x, s + 3, HEX(0x8A6A4A));          /* pedrinhas */
        if (rnd() % 31 == 0) {                                     /* florzinhas no capim */
            int fy = s + 9 + (int)(rnd() % (LH - s - 9 > 0 ? LH - s - 9 : 1));
            px(x, fy, rnd() % 2 ? HEX(0xFFE26B) : HEX(0xF4F1EC));
        }
        if (rnd() % 3 == 0) px(x, s - 1, HEX(0x6E86A0));           /* borda da água batendo no barro */
    }
}

/* taboas: hastes verdes, folhas finas e a "salsicha" marrom no alto */
static void reeds(int x0, int x1, int count, int base_off, int min_h, int max_h) {
    for (int i = 0; i < count; i++) {
        int x = x0 + (int)(rnd() % (x1 - x0));
        int base = shore[x < 0 ? 0 : x >= LW ? LW - 1 : x] + base_off + (int)(rnd() % 6);
        int h = min_h + (int)(rnd() % (max_h - min_h));
        uint32_t stem = rnd() % 2 ? HEX(0x4E8E3A) : HEX(0x3E7A34);
        for (int y = base - h; y <= base; y++) px(x, y, stem);
        px(x, base - h, HEX(0x8CC25A));
        /* folha inclinada */
        int lean = rnd() % 2 ? 1 : -1;
        for (int k = 0; k < h / 2; k++) px(x + lean * (k / 3 + 1), base - k - 2, HEX(0x5E9E44));
        if (rnd() % 5 < 2) {
            rect(x, base - h + 3, 2, 5, HEX(0x6B3E22));
            px(x, base - h + 3, HEX(0x8E5A34));
            px(x, base - h + 1, HEX(0x8CC25A));
        }
    }
}

/* a capivara-símbolo: de perfil, com laranja na cabeça e um passarinho nas costas */
static void capybara(int x0, int y0, bool mirror) {
    static const char *art[] = {
        "....gG......................",
        "...OOOO.....................",
        "..OooOOO..............YK....",
        "..OOOOOO.............KYYK...",
        "..KOOOOK.KK...........KK....",
        ".KLLLLLLKLLK....KKKKKKKK....",
        "KLLLLLLLLLLLKKKKLLLLLLLLKK..",
        "KBBBEBBBBBBBBBBBBBBBBBBBBLK.",
        "KBBBBBBBBBBBBBBBBBBBBBBBBBBK",
        "KSBBBBBBBBBBBBBBBBBBBBBBBBBK",
        "KNSBBBBBBBBBBBBBBBBBBBBBBBBK",
        "KSSBBBBBKBBBBBBBBBBBBBBBBBBK",
        ".KKKKKKKBBBBBBBBBBBBBBBBBBDK",
        "......KDBBBBBBBBBBBBBBBBBDDK",
        "......KDDDDDDDDDDDDDDDDDDDK.",
        "......KDDKKKDDDDDDDDDKKDDK..",
        "......KSSK.KKKKKKKKKK.KSSK..",
        "......KKKK...........KKKK...",
    };
    static const pal_t pal[] = {
        { 'K', HEX(0x3A2312) }, { 'B', HEX(0x9C6B42) }, { 'L', HEX(0xBB8B5B) }, { 'D', HEX(0x7A5032) },
        { 'S', HEX(0x6E4629) }, { 'N', HEX(0x2A170B) }, { 'E', HEX(0x140B05) }, { 'O', HEX(0xFF8A1E) },
        { 'o', HEX(0xFFC066) }, { 'G', HEX(0x4CB83A) }, { 'g', HEX(0x2E8F36) }, { 'Y', HEX(0xF2D04A) },
    };
    for (int i = 4; i < 28; i++) px(x0 + i, y0 + 18, HEX(0x2E1C10));   /* sombra no barro */
    sprite(x0, y0, art, 18, pal, 12, mirror);
    px(mirror ? x0 + 22 : x0 + 5, y0 + 7, HEX(0xFFFFFF));            /* brilho no olho */
}

/* corações (vidas) no canto, em pixel art */
static void hearts(int x, int y, int n) {
    static const char *art[] = { ".XX.XX.", "XWXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..." };
    for (int k = 0; k < n; k++)
        for (int pass = 0; pass < 2; pass++)              /* 0 = sombra, 1 = coração */
            for (int j = 0; j < 6; j++)
                for (int i = 0; i < 7; i++) {
                    char a = art[j][i];
                    if (a == '.') continue;
                    if (pass == 0) px(x + k * 9 + i + 1, y + j + 1, HEX(0x1A1030));
                    else px(x + k * 9 + i, y + j, a == 'W' ? HEX(0xFFC2CC) : HEX(0xFF3B5C));
                }
}

/* texto do placar: fonte Unifont ampliada em blocos (sem suavizar) */
static int hud_text(canvas_t *d, int x, int y, const char *s, uint32_t color, int scale) {
    int start = x;
    while (*s) {
        uint32_t cp = utf8_decode(&s);
        const glyph_t *gl = font_glyph(cp);
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++)
                if (gl->rows[row] & (0x8000 >> col)) {
                    gfx_set(d, x + col * scale + scale / 2, y + row * scale + scale / 2, scale, scale, HEX(0x1A1030));
                    gfx_set(d, x + col * scale, y + row * scale, scale, scale, color);
                }
        x += 8 * scale;
    }
    return x - start;
}

void wallpaper_capivara(canvas_t *dst, bool hud) {
    LH = LW * dst->h / dst->w;
    if (LH > 200) LH = 200;
    g = canvas_create(LW, LH);
    if (!g) return;
    seed = 2026;
    horizon = LH / 2;
    sun_x = LW * 66 / 100;

    sky();
    sun();
    clouds();
    birds();
    far_bank();
    river();
    lilies();
    swimmer(118, horizon + 30);
    near_bank();
    reeds(0, 46, 26, -2, 10, 30);                  /* touceira da esquerda */
    reeds(200, 256, 22, -2, 10, 28);               /* touceira da direita */
    capybara(84, shore[98] - 17, true);            /* olhando para o rio e o sol */
    reeds(60, 84, 5, 1, 8, 16);
    reeds(124, 140, 4, 1, 8, 14);
    if (hud) hearts(4, (MENUBAR_H + 44) * LW / dst->w, 3);

    gfx_blit_scaled(dst, R(0, 0, dst->w, dst->h), g, R(0, 0, LW, LH), dst->w < LW);
    canvas_destroy(g);

    if (hud) {
        int s = dst->w / LW >= 4 ? 2 : 1;
        int y = MENUBAR_H + 10;
        int x = 16;
        x += hud_text(dst, x, y, "1UP ", HEX(0xFF4D6D), s);
        hud_text(dst, x, y, "002026", HEX(0xFFFFFF), s);
        int cx = dst->w / 2 - 7 * 8 * s;
        cx += hud_text(dst, cx, y, "HI ", HEX(0xFFD84A), s);
        hud_text(dst, cx, y, "999999", HEX(0xFFFFFF), s);
    }
}
