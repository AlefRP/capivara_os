/* Ícones desenhados por código (vetoriais, qualquer tamanho). */
#pragma once
#include <stdint.h>

enum {
    ICON_NONE,
    ICON_LOGO,          /* capivara: logo do sistema */
    ICON_FOLDER,
    ICON_FILE,
    ICON_TEXT,
    ICON_TERMINAL,
    ICON_NOTEPAD,
    ICON_CALC,
    ICON_SETTINGS,
    ICON_COMPUTER,
    ICON_TASKMGR,
    ICON_HOME,
    ICON_DOCUMENTS,
    ICON_DOWNLOADS,
    ICON_PICTURES,
    ICON_MUSIC,
    ICON_DESKTOP,
    ICON_INFO,
    ICON_USER,
    ICON_PAINT,
    ICON_CLOCK,
    ICON_TRASH,
    ICON_TRASH_FULL,
    ICON_IMAGE,
    ICON_PHOTOS,
    ICON_SNIP,
    ICON_EXPLORER,      /* ícone do aplicativo (as pastas usam ICON_FOLDER) */
    ICON_LAUNCHPAD,
    ICON_DISK,
    ICON_GAME,          /* controle de videogame: jogos */
    ICON_BOOK,          /* livro aberto: app Estudos */
    ICON_TATU,          /* tatu sobre camadas de dados: cliente Toca */
    ICON_COUNT,
};

/* ícones monocromáticos (usam a cor passada) */
enum {
    GLYPH_BACK = 100, GLYPH_FORWARD, GLYPH_UP, GLYPH_REFRESH, GLYPH_SEARCH, GLYPH_POWER,
    GLYPH_RESTART, GLYPH_NEW_FOLDER, GLYPH_DELETE, GLYPH_RENAME, GLYPH_COPY, GLYPH_PASTE,
    GLYPH_CUT, GLYPH_CHEVRON_RIGHT, GLYPH_CHEVRON_DOWN, GLYPH_CHECK, GLYPH_NEW_FILE,
    GLYPH_SAVE, GLYPH_OPEN, GLYPH_LOCK, GLYPH_SPEAKER, GLYPH_NETWORK, GLYPH_CHEVRON_UP,
    GLYPH_TASKVIEW, GLYPH_LOGO, GLYPH_BELL, GLYPH_ZOOM_IN, GLYPH_ZOOM_OUT, GLYPH_RESTORE, GLYPH_CLOSE,
};

/* Ícone feito de uma imagem, para os programas extras (docs/extras.md). Os
 * ícones do sistema são desenhados em código, mas um extra pode trazer a
 * arte dele: uma imagem quadrada, ARGB de 32 bits (sem pré-multiplicar). */
typedef struct icon_image {
    int size;
    const uint32_t *argb;
} icon_image_t;
#define ICON_IMAGE_BASE 1000
int icon_from_image(const icon_image_t *img);   /* um id de ícone novo (ICON_FILE se não couber) */

struct canvas;
void icon_draw(struct canvas *c, int icon, int x, int y, int size);
void glyph_draw(struct canvas *c, int glyph, int x, int y, int size, uint32_t color);
