/*
 * Gera as imagens da documentação (docs/img) com o próprio código gráfico do
 * Capivara OS, compilado como um programa comum: o logo, os ícones dos
 * aplicativos, o papel de parede e o banner do README. Nada é desenhado à
 * mão fora do sistema: se um ícone mudar no kernel, basta rodar de novo.
 *
 * Uso: make arte   (precisa de um gcc comum e do ImageMagick; o container
 *                   de ferramentas já tem os dois)
 */
#include <stdio.h>
#include <stdlib.h>
#include "gui/gfx.h"
#include "gui/icons.h"
#include "gui/font.h"
#include "gui/theme.h"
#include "gui/wallpaper.h"

theme_t theme;      /* icons.c inclui theme.h, mas os ícones não dependem do tema */

static const char *dir;

/* Salva em PAM (RGBA, sem compressão); o Makefile converte para PNG. */
static void save(const char *name, canvas_t *c) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pam", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", c->w, c->h);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[y * c->stride + x];
            uint8_t px[4] = { p >> 16, p >> 8, p, p >> 24 };
            fwrite(px, 1, 4, f);
        }
    fclose(f);
}

/* Ícone com fundo transparente. O desenho é feito duas vezes, sobre preto e
 * sobre branco: o quanto o fundo "vaza" em cada pixel é a transparência dele,
 * e a versão sobre preto, dividida pelo alfa, é a cor verdadeira. */
static void icon(const char *name, int id, int size) {
    canvas_t *b = canvas_create(size, size), *w = canvas_create(size, size);
    gfx_clear(b, HEX(0x000000));
    gfx_clear(w, HEX(0xFFFFFF));
    icon_draw(b, id, 0, 0, size);
    icon_draw(w, id, 0, 0, size);
    for (int i = 0; i < size * size; i++) {
        uint32_t pb = b->px[i], pw = w->px[i], out = 0;
        int a = 255 - (int)((((pw >> 16) & 255) - ((pb >> 16) & 255) + ((pw >> 8) & 255) - ((pb >> 8) & 255) +
                             (pw & 255) - (pb & 255)) / 3);
        if (a > 0) {
            uint32_t ch[3];
            for (int k = 0; k < 3; k++) {
                uint32_t v = ((pb >> (16 - 8 * k)) & 255) * 255 / a;
                ch[k] = v > 255 ? 255 : v;
            }
            out = ARGB(a > 255 ? 255 : a, (ch[0] << 16) | (ch[1] << 8) | ch[2]);
        }
        b->px[i] = out;
    }
    save(name, b);
    canvas_destroy(b);
    canvas_destroy(w);
}

/* Texto com sombra suave (desenhado deslocado, translúcido, antes do texto) */
static void title(canvas_t *c, int x, int y, const char *s, uint32_t col, int flags) {
    font_draw(c, x + 2, y + 3, s, ARGB(0x70, 0x1A0F2E), flags);
    font_draw(c, x + 1, y + 1, s, ARGB(0x90, 0x1A0F2E), flags);
    font_draw(c, x, y, s, col, flags);
}

/* Banner do README: um recorte do papel de parede com o logo e o nome */
static void banner(void) {
    canvas_t *full = canvas_create(1280, 800);
    wallpaper_capivara(full, false);
    save("papel-de-parede", full);

    int y0 = 80, h = 640;
    canvas_t *c = canvas_create(1280, h);
    gfx_blit(c, 0, 0, full, 0, y0, 1280, h);

    /* placa translúcida atrás do título (no céu, à direita dos tuiuiús) */
    int px = 540, py = 36, pw = 560, ph = 150;
    gfx_round_fill(c, px, py + 4, pw, ph, 22, ARGB(0x30, 0x10081C));
    gfx_round_fill(c, px, py, pw, ph, 22, ARGB(0x8C, 0x1B1230));
    gfx_round_rect(c, px, py, pw, ph, 22, ARGB(0x40, 0xFFFFFF));
    icon_draw(c, ICON_LOGO, px + 26, py + 22, 104);
    title(c, px + 150, py + 30, "Capivara OS", HEX(0xFFFFFF), FONT_4X | FONT_BOLD);
    title(c, px + 152, py + 104, "um sistema operacional feito do zero", HEX(0xFFD9A0), FONT_2X);
    save("banner", c);
    canvas_destroy(c);
    canvas_destroy(full);
}

int main(int argc, char **argv) {
    dir = argc > 1 ? argv[1] : ".";
    font_init();
    banner();
    icon("logo", ICON_LOGO, 256);
    static const struct { const char *name; int id; } apps[] = {
        { "explorador", ICON_EXPLORER }, { "terminal", ICON_TERMINAL }, { "bloco-de-notas", ICON_NOTEPAD },
        { "calculadora", ICON_CALC }, { "paint", ICON_PAINT }, { "fotos", ICON_PHOTOS },
        { "captura", ICON_SNIP }, { "gerenciador", ICON_TASKMGR }, { "configuracoes", ICON_SETTINGS },
        { "launchpad", ICON_LAUNCHPAD }, { "lixeira", ICON_TRASH }, { "disco", ICON_DISK },
        { "pasta", ICON_FOLDER }, { "jogo", ICON_GAME }, { "estudos", ICON_BOOK },
    };
    for (size_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) icon(apps[i].name, apps[i].id, 128);
    return 0;
}
