/* Fotos: visualizador de imagens BMP (capturas de tela, desenhos do Paint). */
#include "apps/apps.h"
#include "apps/trash.h"
#include "gui/ui.h"
#include "gui/bmp.h"
#include "fs/vfs.h"
#include "dev/keyboard.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

#define BAR_H 48

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

enum { B_OPEN = 1, B_PREV, B_NEXT, B_ZOOM_OUT, B_ZOOM_IN, B_FIT, B_DELETE, B_COUNT };

typedef struct {
    window_t *win;
    char path[VFS_PATH_MAX];
    canvas_t *img;
    int zoom;                 /* em %, 0 = ajustar à janela */
    int pan_x, pan_y;
    bool dragging;
    int drag_x, drag_y, pan_x0, pan_y0;
    int hover, press;
    rect_t btn[B_COUNT];
    bool quit;
} ph_t;

static bool is_bmp(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, ".bmp");
}

static void load(ph_t *p, const char *path) {
    canvas_destroy(p->img);
    p->img = bmp_load(path);
    strlcpy(p->path, path, sizeof(p->path));
    p->zoom = 0;
    p->pan_x = p->pan_y = 0;
    const char *name = strrchr(path, '/');
    char t[96];
    snprintf(t, sizeof(t), "%s - Fotos", name ? name + 1 : path);
    win_set_title(p->win, p->img ? t : "Fotos");
}

/* vizinha na mesma pasta (dir = +1 próxima, -1 anterior) */
static void step(ph_t *p, int dir) {
    char folder[VFS_PATH_MAX];
    strlcpy(folder, p->path, sizeof(folder));
    char *slash = strrchr(folder, '/');
    if (!slash) return;
    const char *cur = slash + 1;
    char curname[VFS_NAME_MAX];
    strlcpy(curname, cur, sizeof(curname));
    *slash = 0;
    vnode_t *d = vfs_lookup(NULL, folder[0] ? folder : "/");
    if (!d) return;
    int n = vfs_child_count(d), idx = -1, count = 0;
    char names[64][VFS_NAME_MAX];
    for (int i = 0; i < n && count < 64; i++) {
        vnode_t *c = vfs_child_at(d, i);
        if (c && c->type == VFS_FILE && is_bmp(c->name)) {
            if (!strcmp(c->name, curname)) idx = count;
            strlcpy(names[count++], c->name, VFS_NAME_MAX);
        }
    }
    if (count < 2) return;
    idx = ((idx < 0 ? 0 : idx) + dir + count) % count;
    char next[VFS_PATH_MAX];
    snprintf(next, sizeof(next), "%s/%s", folder, names[idx]);
    load(p, next);
}

static rect_t view_rect(ph_t *p) {
    canvas_t *c = win_canvas(p->win);
    return R(0, BAR_H, c->w, c->h - BAR_H);
}

static int effective_zoom(ph_t *p) {
    if (!p->img) return 100;
    if (p->zoom) return p->zoom;
    rect_t v = view_rect(p);
    int zx = (v.w - 32) * 100 / p->img->w, zy = (v.h - 32) * 100 / p->img->h;
    return imax(1, imin(100, imin(zx, zy)));
}

static void layout(ph_t *p) {
    canvas_t *c = win_canvas(p->win);
    int x = 8;
    p->btn[B_OPEN] = R(x, 8, 32 + font_width("Abrir", 0) + 12, 32); x += p->btn[B_OPEN].w + 12;
    p->btn[B_PREV] = R(x, 8, 32, 32); x += 34;
    p->btn[B_NEXT] = R(x, 8, 32, 32);
    int rx = c->w - 8 - 32;
    p->btn[B_DELETE] = R(rx, 8, 32, 32); rx -= 44;
    p->btn[B_FIT] = R(rx - font_width("Ajustar", 0) - 12, 8, font_width("Ajustar", 0) + 24, 32); rx = p->btn[B_FIT].x - 4;
    p->btn[B_ZOOM_IN] = R(rx - 32, 8, 32, 32); rx -= 34;
    p->btn[B_ZOOM_OUT] = R(rx - 32, 8, 32, 32);
}

static ui_state_t st(ph_t *p, int b) {
    return p->hover == b ? (p->press == b ? UI_PRESSED : UI_HOVER) : UI_NORMAL;
}

static void draw(ph_t *p) {
    canvas_t *c = win_canvas(p->win);
    layout(p);
    rect_t v = view_rect(p);
    gfx_fill(c, v.x, v.y, v.w, v.h, theme.dark ? HEX(0x1A1A1A) : HEX(0xE9E9E9));
    if (p->img) {
        int z = effective_zoom(p);
        int w = p->img->w * z / 100, h = p->img->h * z / 100;
        rect_t r = R(v.x + (v.w - w) / 2 + p->pan_x, v.y + (v.h - h) / 2 + p->pan_y, w, h);
        rect_t saved = c->clip;
        canvas_clip(c, v);
        gfx_shadow(c, r, 0, 10, 60);
        gfx_blit_scaled(c, r, p->img, R(0, 0, p->img->w, p->img->h), z < 100);
        c->clip = saved;
    } else {
        ui_text_center(c, R(v.x, v.y + v.h / 2 - 40, v.w, 30),
                       p->path[0] ? "Não foi possível abrir esta imagem (use BMP de 24 ou 32 bits)." : "Nenhuma imagem aberta.",
                       theme.text_dim, 0);
        ui_text_center(c, R(v.x, v.y + v.h / 2 - 10, v.w, 30), "Use \"Abrir\" ou dê dois cliques num arquivo .bmp no Explorador.",
                       theme.text_dim, 0);
    }

    gfx_fill(c, 0, 0, c->w, BAR_H, theme.window_bg);
    gfx_hline(c, 0, BAR_H - 1, c->w, theme.border);
    rect_t o = p->btn[B_OPEN];
    if (p->hover == B_OPEN) gfx_round_fill(c, o.x, o.y, o.w, o.h, 4, theme.hover);
    glyph_draw(c, GLYPH_OPEN, o.x + 8, o.y + 8, 16, theme.text);
    font_draw(c, o.x + 32, o.y + 8, "Abrir", theme.text, 0);
    ui_glyph_button(c, p->btn[B_PREV], GLYPH_BACK, p->img ? st(p, B_PREV) : UI_DISABLED);
    ui_glyph_button(c, p->btn[B_NEXT], GLYPH_FORWARD, p->img ? st(p, B_NEXT) : UI_DISABLED);
    ui_glyph_button(c, p->btn[B_ZOOM_OUT], GLYPH_ZOOM_OUT, p->img ? st(p, B_ZOOM_OUT) : UI_DISABLED);
    ui_glyph_button(c, p->btn[B_ZOOM_IN], GLYPH_ZOOM_IN, p->img ? st(p, B_ZOOM_IN) : UI_DISABLED);
    ui_button(c, p->btn[B_FIT], "Ajustar", BTN_FLAT, p->img ? st(p, B_FIT) : UI_DISABLED);
    ui_glyph_button(c, p->btn[B_DELETE], GLYPH_DELETE, p->img ? st(p, B_DELETE) : UI_DISABLED);
    if (p->img) {
        char info[64];
        snprintf(info, sizeof(info), "%d x %d   %d%%", p->img->w, p->img->h, effective_zoom(p));
        int iw = font_width(info, 0);
        int cx = (p->btn[B_NEXT].x + 40 + p->btn[B_ZOOM_OUT].x) / 2;
        font_draw(c, cx - iw / 2, 16, info, theme.text_dim, 0);
    }
    win_invalidate(p->win);
}

static void set_zoom(ph_t *p, int z) {
    p->zoom = imax(10, imin(800, z));
    p->pan_x = p->pan_y = 0;
}

static void zoom_by(ph_t *p, int dir) {
    static const int steps[] = { 10, 25, 33, 50, 67, 100, 150, 200, 300, 400, 800 };
    int z = effective_zoom(p);
    int pick = dir > 0 ? 800 : 10;
    for (int i = 0; i < 11; i++) {
        if (dir > 0 && steps[i] > z) { pick = steps[i]; break; }
        if (dir < 0 && steps[10 - i] < z) { pick = steps[10 - i]; break; }
    }
    set_zoom(p, pick);
}

static void open_dialog(ph_t *p) {
    char path[VFS_PATH_MAX];
    strlcpy(path, p->path[0] ? p->path : "/home/usuario/Imagens/", sizeof(path));
    if (ui_file_dialog(p->win, false, "Abrir imagem", path, sizeof(path))) load(p, path);
}

static void delete_current(ph_t *p) {
    if (!p->img) return;
    char old[VFS_PATH_MAX];
    strlcpy(old, p->path, sizeof(old));
    step(p, 1);
    trash_move(old);
    if (!strcmp(old, p->path)) { canvas_destroy(p->img); p->img = NULL; p->path[0] = 0; win_set_title(p->win, "Fotos"); }
}

static void action(ph_t *p, int b) {
    switch (b) {
    case B_OPEN: open_dialog(p); break;
    case B_PREV: step(p, -1); break;
    case B_NEXT: step(p, 1); break;
    case B_ZOOM_IN: zoom_by(p, 1); break;
    case B_ZOOM_OUT: zoom_by(p, -1); break;
    case B_FIT: p->zoom = 0; p->pan_x = p->pan_y = 0; break;
    case B_DELETE: delete_current(p); break;
    }
}

void photos_main(void *arg) {
    ph_t *p = kcalloc(1, sizeof(ph_t));
    p->win = win_create("Fotos", ICON_PHOTOS, WIN_AUTO, WIN_AUTO, 860, 560, WF_APP);
    win_set_min_size(p->win, 480, 300);
    if (arg) {
        load(p, arg);
        kfree(arg);
    }
    draw(p);
    while (!p->quit) {
        event_t e;
        gui_wait(&e);
        int at = 0;
        for (int b = 1; b < B_COUNT; b++) if (ui_hit(p->btn[b], &e)) at = b;
        switch (e.type) {
        case EV_MOUSE_MOVE:
            if (p->dragging) {
                p->pan_x = p->pan_x0 + e.x - p->drag_x;
                p->pan_y = p->pan_y0 + e.y - p->drag_y;
                break;
            }
            if (at == p->hover) continue;
            p->hover = at;
            break;
        case EV_MOUSE_LEAVE: p->hover = 0; break;
        case EV_MOUSE_DOWN:
            p->press = at;
            if (!at && ui_hit(view_rect(p), &e) && p->img) {
                if (e.clicks == 2) { if (p->zoom) p->zoom = 0; else set_zoom(p, 100); p->pan_x = p->pan_y = 0; break; }
                p->dragging = true;
                p->drag_x = e.x;
                p->drag_y = e.y;
                p->pan_x0 = p->pan_x;
                p->pan_y0 = p->pan_y;
            }
            break;
        case EV_MOUSE_UP:
            p->dragging = false;
            if (at && at == p->press && (p->img || at == B_OPEN)) action(p, at);
            p->press = 0;
            break;
        case EV_MOUSE_WHEEL:
            if (p->img) zoom_by(p, e.wheel);
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_LEFT) step(p, -1);
            else if (e.key == KEY_RIGHT) step(p, 1);
            else if (e.key == KEY_DELETE) delete_current(p);
            else if (e.key == KEY_CHAR && (e.ch == '+' || e.ch == '=')) zoom_by(p, 1);
            else if (e.key == KEY_CHAR && e.ch == '-') zoom_by(p, -1);
            else if (e.key == KEY_CHAR && e.ch == '0') action(p, B_FIT);
            else if ((e.mods & MOD_CTRL) && e.key == KEY_CHAR && tolower((int)e.ch) == 'o') open_dialog(p);
            else continue;
            break;
        case EV_RESIZE: case EV_THEME: break;
        case EV_CLOSE: p->quit = true; continue;
        default: continue;
        }
        draw(p);
    }
    win_destroy(p->win);
    canvas_destroy(p->img);
    kfree(p);
}
