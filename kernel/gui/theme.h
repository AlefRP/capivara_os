/* Cores da interface. Mudar o tema recalcula tudo e avisa os aplicativos. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool dark;
    int accent_index;
    int wallpaper;

    uint32_t accent, accent_hover, accent_pressed, accent_text, accent_light;
    uint32_t window_bg;          /* fundo de janelas (estilo "mica") */
    uint32_t surface;            /* áreas de conteúdo (listas, editor) */
    uint32_t surface_alt;        /* barras laterais, linhas alternadas */
    uint32_t text, text_dim, text_disabled;
    uint32_t border, border_strong;
    uint32_t hover, pressed, selection, selection_text;
    uint32_t control, control_hover, control_border;
    uint32_t title_active, title_inactive, title_text, title_text_inactive;
    uint32_t frame_active, frame_inactive;
    uint32_t taskbar_bg, popup_bg, popup_border;
    uint32_t dock_bg, dock_border;
    uint32_t close_hover;
} theme_t;

extern theme_t theme;

#define ACCENT_COUNT 6
#define WALLPAPER_COUNT 6

extern const uint32_t accent_colors[ACCENT_COUNT];
extern const char *accent_names[ACCENT_COUNT];
extern const char *wallpaper_names[WALLPAPER_COUNT];

void theme_apply(bool dark, int accent_index, int wallpaper);
