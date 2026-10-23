/* Imagens BMP (formato simples, sem compressão): capturas de tela, Paint, Fotos. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gui/gfx.h"

uint8_t  *bmp_encode(const canvas_t *c, rect_t area, size_t *out_size);   /* kmalloc */
canvas_t *bmp_decode(const uint8_t *data, size_t size);                   /* NULL se inválido */
canvas_t *bmp_load(const char *path);
bool      bmp_save(const char *path, const canvas_t *c, rect_t area);
