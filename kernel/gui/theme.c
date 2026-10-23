#include "gui/theme.h"
#include "gui/gfx.h"

theme_t theme;

const uint32_t accent_colors[ACCENT_COUNT] = {
    HEX(0x0A84FF), HEX(0xA550A7), HEX(0xF74F9E), HEX(0xF7821B), HEX(0x62BA46), HEX(0x8C8C8C),
};
const char *accent_names[ACCENT_COUNT] = { "Azul", "Roxo", "Rosa", "Laranja", "Verde", "Grafite" };
const char *wallpaper_names[WALLPAPER_COUNT] = { "Capivara", "Ondas", "Ipê", "Aurora", "Entardecer", "Cor sólida" };

void theme_apply(bool dark, int accent_index, int wallpaper) {
    theme.dark = dark;
    theme.accent_index = accent_index;
    theme.wallpaper = wallpaper;
    uint32_t acc = accent_colors[accent_index % ACCENT_COUNT];
    theme.accent = acc;
    theme.accent_hover = color_mix(acc, dark ? HEX(0xFFFFFF) : HEX(0x000000), 30);
    theme.accent_pressed = color_mix(acc, dark ? HEX(0xFFFFFF) : HEX(0x000000), 60);
    theme.accent_text = HEX(0xFFFFFF);

    if (!dark) {
        theme.window_bg = HEX(0xF5F5F4);
        theme.surface = HEX(0xFFFFFF);
        theme.surface_alt = HEX(0xF2F2F2);
        theme.text = HEX(0x1D1D1F);
        theme.text_dim = HEX(0x6E6E73);
        theme.text_disabled = HEX(0xB0B0B5);
        theme.border = HEX(0xE3E3E3);
        theme.border_strong = HEX(0xC7C7CC);
        theme.hover = ARGB(0x12, 0x000000);
        theme.pressed = ARGB(0x20, 0x000000);
        theme.control = HEX(0xFFFFFF);
        theme.control_hover = HEX(0xF5F5F5);
        theme.control_border = HEX(0xD2D2D7);
        theme.title_active = HEX(0xF5F5F4);
        theme.title_inactive = HEX(0xFAFAFA);
        theme.title_text = HEX(0x1D1D1F);
        theme.title_text_inactive = HEX(0xA0A0A5);
        theme.frame_active = HEX(0xC4C4C6);
        theme.frame_inactive = HEX(0xD6D6D8);
        theme.taskbar_bg = ARGB(0xC0, 0xF4F4F6);
        theme.popup_bg = ARGB(0xF2, 0xF4F4F5);
        theme.popup_border = HEX(0xD0D0D2);
        theme.dock_bg = ARGB(0x8C, 0xF4F4F6);
        theme.dock_border = ARGB(0x70, 0xFFFFFF);
        theme.accent_light = color_mix(acc, HEX(0xFFFFFF), 190);
    } else {
        theme.window_bg = HEX(0x2B2B2D);
        theme.surface = HEX(0x1E1E1E);
        theme.surface_alt = HEX(0x262628);
        theme.text = HEX(0xF5F5F7);
        theme.text_dim = HEX(0x98989D);
        theme.text_disabled = HEX(0x5A5A5E);
        theme.border = HEX(0x3A3A3C);
        theme.border_strong = HEX(0x4A4A4C);
        theme.hover = ARGB(0x18, 0xFFFFFF);
        theme.pressed = ARGB(0x10, 0xFFFFFF);
        theme.control = HEX(0x3A3A3C);
        theme.control_hover = HEX(0x444446);
        theme.control_border = HEX(0x4A4A4C);
        theme.title_active = HEX(0x2B2B2D);
        theme.title_inactive = HEX(0x323234);
        theme.title_text = HEX(0xF5F5F7);
        theme.title_text_inactive = HEX(0x7C7C80);
        theme.frame_active = HEX(0x555557);
        theme.frame_inactive = HEX(0x444446);
        theme.taskbar_bg = ARGB(0xB4, 0x1C1C1E);
        theme.popup_bg = ARGB(0xF2, 0x2C2C2E);
        theme.popup_border = HEX(0x4A4A4C);
        theme.dock_bg = ARGB(0x90, 0x2A2A2C);
        theme.dock_border = ARGB(0x38, 0xFFFFFF);
        theme.accent_light = color_mix(acc, HEX(0x000000), 110);
    }
    theme.selection = theme.accent_light;
    theme.selection_text = theme.text;
    theme.close_hover = HEX(0xFF5F57);
}
