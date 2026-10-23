/* Papéis de parede desenhados por código. */
#pragma once
#include "gui/gfx.h"

/* Beira de brejo em pixel art, com capivaras. Funciona em qualquer
 * tamanho (tela inteira ou miniatura); "hud" desenha o placar no topo. */
void wallpaper_capivara(canvas_t *dst, bool hud);
