/* Paint: desenho livre com pincel, borracha e paleta de cores. */
#include "apps/apps.h"
#include "gui/ui.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "gui/bmp.h"
#include "fs/vfs.h"

#define TOOL_H 56
#define PAPER_W 1600
#define PAPER_H 1000

static const uint32_t colors[] = {
    HEX(0x000000), HEX(0x7F7F7F), HEX(0xFFFFFF), HEX(0xE53935), HEX(0xFB8C00), HEX(0xFDD835),
    HEX(0x43A047), HEX(0x00ACC1), HEX(0x1E88E5), HEX(0x5E35B1), HEX(0xD81B60), HEX(0x6D4C41),
};
#define NCOLORS ((int)(sizeof(colors) / sizeof(colors[0])))
static const int sizes[] = { 2, 6, 14 };

typedef struct {
    window_t *win;
    char path[VFS_PATH_MAX];
    canvas_t *paper;
    int color, size, hover;
    bool eraser, drawing, quit;
    int lx, ly;
} paint_t;

static rect_t color_rect(int i) { return R(12 + i * 30, 14, 26, 26); }
static rect_t size_rect(int i) { return R(12 + NCOLORS * 30 + 16 + i * 36, 12, 32, 32); }
static rect_t eraser_rect(void) { return R(size_rect(2).x + 52, 12, 104, 32); }
static rect_t clear_rect(void) { return R(eraser_rect().x + 112, 12, 88, 32); }
static rect_t open_rect(void) { return R(clear_rect().x + 104, 12, 80, 32); }
static rect_t save_rect(void) { return R(open_rect().x + 88, 12, 80, 32); }

static void draw(paint_t *p) {
    canvas_t *c = win_canvas(p->win);
    gfx_fill(c, 0, 0, c->w, TOOL_H, theme.window_bg);
    gfx_hline(c, 0, TOOL_H - 1, c->w, theme.border);
    for (int i = 0; i < NCOLORS; i++) {
        rect_t r = color_rect(i);
        if (i == p->color && !p->eraser) gfx_ring(c, r.x + 13, r.y + 13, 14, 2, theme.accent);
        gfx_circle(c, r.x + 13, r.y + 13, 10, colors[i]);
        if (colors[i] == HEX(0xFFFFFF)) gfx_ring(c, r.x + 13, r.y + 13, 10, 1, theme.border_strong);
    }
    for (int i = 0; i < 3; i++) {
        rect_t r = size_rect(i);
        if (i == p->size) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, theme.pressed);
        else if (p->hover == 10 + i) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, theme.hover);
        gfx_circle(c, r.x + 16, r.y + 16, sizes[i] / 2.0f + 1, theme.text);
    }
    ui_button(c, eraser_rect(), "Borracha", p->eraser ? BTN_ACCENT : BTN_STANDARD, p->hover == 20 ? UI_HOVER : UI_NORMAL);
    ui_button(c, clear_rect(), "Limpar", BTN_STANDARD, p->hover == 21 ? UI_HOVER : UI_NORMAL);
    gfx_vline(c, open_rect().x - 12, 16, 24, theme.border);
    ui_button(c, open_rect(), "Abrir", BTN_STANDARD, p->hover == 22 ? UI_HOVER : UI_NORMAL);
    ui_button(c, save_rect(), "Salvar", BTN_ACCENT, p->hover == 23 ? UI_HOVER : UI_NORMAL);
    gfx_blit(c, 0, TOOL_H, p->paper, 0, 0, c->w, c->h - TOOL_H);
    win_invalidate(p->win);
}

static void stroke(paint_t *p, int x0, int y0, int x1, int y1) {
    uint32_t col = p->eraser ? HEX(0xFFFFFF) : colors[p->color];
    float w = p->eraser ? 24 : sizes[p->size];
    gfx_line(p->paper, x0, y0 - TOOL_H, x1, y1 - TOOL_H, w, col);
    int minx = (x0 < x1 ? x0 : x1) - (int)w - 2, miny = (y0 < y1 ? y0 : y1) - (int)w - 2;
    int maxx = (x0 > x1 ? x0 : x1) + (int)w + 2, maxy = (y0 > y1 ? y0 : y1) + (int)w + 2;
    canvas_t *c = win_canvas(p->win);
    rect_t r = rect_intersect(R(minx, miny, maxx - minx, maxy - miny), R(0, TOOL_H, c->w, c->h - TOOL_H));
    gfx_blit(c, r.x, r.y, p->paper, r.x, r.y - TOOL_H, r.w, r.h);
    win_invalidate_rect(p->win, r);
}

static int hit(event_t *e) {
    for (int i = 0; i < NCOLORS; i++) if (ui_hit(color_rect(i), e)) return 100 + i;
    for (int i = 0; i < 3; i++) if (ui_hit(size_rect(i), e)) return 10 + i;
    if (ui_hit(eraser_rect(), e)) return 20;
    if (ui_hit(clear_rect(), e)) return 21;
    if (ui_hit(open_rect(), e)) return 22;
    if (ui_hit(save_rect(), e)) return 23;
    return -1;
}

static void set_title(paint_t *p) {
    char t[96];
    snprintf(t, sizeof(t), "%s - Paint", p->path[0] ? strrchr(p->path, '/') + 1 : "Sem título");
    win_set_title(p->win, t);
}

static void save(paint_t *p) {
    char path[VFS_PATH_MAX];
    strlcpy(path, p->path[0] ? p->path : "/home/usuario/Imagens/Desenho.bmp", sizeof(path));
    if (!ui_file_dialog(p->win, true, "Salvar desenho", path, sizeof(path))) return;
    if (!strrchr(path, '.')) strlcat(path, ".bmp", sizeof(path));
    canvas_t *c = win_canvas(p->win);
    if (!bmp_save(path, p->paper, R(0, 0, c->w, c->h - TOOL_H))) {
        ui_message(p->win, "Paint", "Não foi possível salvar.", "OK", NULL, NULL);
        return;
    }
    strlcpy(p->path, path, sizeof(p->path));
    set_title(p);
}

static void open_image(paint_t *p, const char *path) {
    canvas_t *img = bmp_load(path);
    if (!img) {
        ui_message(p->win, "Paint", "Não foi possível abrir (use imagens BMP de 24 ou 32 bits).", "OK", NULL, NULL);
        return;
    }
    gfx_clear(p->paper, HEX(0xFFFFFF));
    gfx_blit(p->paper, 0, 0, img, 0, 0, img->w, img->h);
    canvas_destroy(img);
    strlcpy(p->path, path, sizeof(p->path));
    set_title(p);
}

void paint_main(void *arg) {
    if (arg) kfree(arg);
    paint_t *p = kcalloc(1, sizeof(paint_t));
    p->size = 1;
    p->hover = -1;
    p->paper = canvas_create(PAPER_W, PAPER_H);
    gfx_clear(p->paper, HEX(0xFFFFFF));
    p->win = win_create("Sem título - Paint", ICON_PAINT, WIN_AUTO, WIN_AUTO, 820, 540, WF_APP);
    win_set_min_size(p->win, 640, 300);
    draw(p);
    while (!p->quit) {
        event_t e;
        gui_wait(&e);
        switch (e.type) {
        case EV_MOUSE_DOWN: {
            int h = hit(&e);
            if (h >= 100) { p->color = h - 100; p->eraser = false; }
            else if (h >= 10 && h < 13) p->size = h - 10;
            else if (h == 20) p->eraser = !p->eraser;
            else if (h == 21) {
                if (ui_message(p->win, "Paint", "Apagar todo o desenho?", "Apagar", "Cancelar", NULL) == 1)
                    gfx_clear(p->paper, HEX(0xFFFFFF));
            } else if (h == 22) {
                char path[VFS_PATH_MAX];
                strlcpy(path, p->path[0] ? p->path : "/home/usuario/Imagens/", sizeof(path));
                if (ui_file_dialog(p->win, false, "Abrir imagem", path, sizeof(path))) open_image(p, path);
            } else if (h == 23) {
                save(p);
            } else if (e.y >= TOOL_H) {
                p->drawing = true;
                p->lx = e.x;
                p->ly = e.y;
                stroke(p, e.x, e.y, e.x, e.y);
                continue;
            }
            break;
        }
        case EV_MOUSE_MOVE:
            win_set_cursor(p->win, e.y >= TOOL_H ? CURSOR_IBEAM : CURSOR_ARROW);
            if (p->drawing) {
                stroke(p, p->lx, p->ly, e.x, e.y);
                p->lx = e.x;
                p->ly = e.y;
                continue;
            } else {
                int h = hit(&e);
                if (h == p->hover) continue;
                p->hover = h;
            }
            break;
        case EV_MOUSE_UP: p->drawing = false; continue;
        case EV_MOUSE_LEAVE: p->hover = -1; break;
        case EV_RESIZE: case EV_THEME: break;
        case EV_CLOSE: p->quit = true; continue;
        default: continue;
        }
        draw(p);
    }
    win_destroy(p->win);
    canvas_destroy(p->paper);
    kfree(p);
}
