/*
 * Compositor e gerenciador de janelas.
 *
 * Roda como uma tarefa própria ("compositor"). A cada volta do laço:
 *   1. consome a fila de entrada (teclado/mouse vindos das interrupções);
 *   2. decide o destino de cada evento (arrastar janela, botão da barra de
 *      título, ou entregar ao aplicativo);
 *   3. redesenha somente os retângulos "sujos" num back buffer e os copia
 *      para o framebuffer (sem piscar, no máximo ~60 quadros por segundo).
 *
 * Um mutex protege a lista de janelas: aplicativos (outras tarefas) criam,
 * movem e destroem janelas enquanto o compositor desenha.
 */
#include "gui/wm.h"
#include "gui/font.h"
#include "gui/theme.h"
#include "gui/icons.h"
#include "dev/input.h"
#include "dev/keyboard.h"
#include "boot/bootinfo.h"
#include "arch/x86_64/pit.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "panic.h"
#include "gui/bmp.h"
#include "apps/notify.h"
#include "fs/vfs.h"
#include "dev/rtc.h"

#define MAX_WIN   64
#define MAX_DIRTY 32
#define FRAME_MS  16

enum { HT_NONE, HT_CLIENT, HT_CAPTION, HT_MIN, HT_MAX, HT_CLOSE, HT_RESIZE };
enum { EDGE_L = 1, EDGE_R = 2, EDGE_T = 4, EDGE_B = 8 };
enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_BUTTON };

static window_t *zorder[MAX_WIN];      /* do fundo para o topo */
static int nwin;
static mutex_t lock;
static int next_win_id = 1;

static canvas_t *scr;                  /* back buffer */
static uint32_t *fb;
static int fb_stride, scr_w, scr_h;

static rect_t dirty[MAX_DIRTY];
static volatile int ndirty;

static task_t *gui_task, *shell_task;
static window_t *focused, *hover_win, *capture_win, *hover_btn_win;
static int mouse_x, mouse_y;
static uint8_t mouse_btn;
static int cursor_shape;

static int drag, drag_edges, drag_mx, drag_my;
static window_t *drag_win;
static rect_t drag_frame;
static bool drag_moved;
static int snap_zone;
static rect_t snap_rect;

static uint64_t last_click_ms;
static window_t *last_click_win;
static int last_click_x, last_click_y, last_click_btn, click_count;

static bool alttab;
static int alttab_ids[MAX_WIN], alttab_n, alttab_sel;
static rect_t alttab_rect;

static bool super_down, super_used;
static int hidden_by_desktop[MAX_WIN], n_hidden_by_desktop;

static int corner_inset[RADIUS];

/* tela de bloqueio */
static window_t *lock_win;

/* áreas de trabalho virtuais */
static int cur_desk, ndesks = 1;
static uint64_t desk_label_until;

/* Visão de Tarefas (Super+Tab) */
static bool taskview;
static int tv_hover = -1, tv_press = -1;
static bool tv_dragging;
static int tv_drag_id;

/* Snap Layouts: painel que aparece ao parar o mouse no botão maximizar */
static window_t *fly_win;
static bool fly_open;
static uint64_t fly_due;
static rect_t fly_rect;
static int fly_hover = -1;

/* captura de tela (Super+Shift+S) */
static bool snip, snip_drag, hide_cursor;
static canvas_t *snip_img;
static int snip_x0, snip_y0;
static rect_t snip_sel;

/* ------------------------------------------------------------------ */
/* Cursores (desenhos em texto: X = preto, . = branco; os demais        */
/* recebem automaticamente um contorno branco para aparecer em fundos   */
/* escuros)                                                             */
/* ------------------------------------------------------------------ */
typedef struct { int w, h, hx, hy; uint32_t px[34 * 34]; } cursor_img_t;
static cursor_img_t cursors[CURSOR_COUNT];

static const char *arrow_art[] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
    "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
    "X.........X ", "X......XXXXX", "X...X..X    ", "X..XX..X    ", "X.X  X..X   ",
    "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   ",
};
static const char *ibeam_art[] = {
    "XX XX", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ",
    "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "  X  ", "XX XX",
};
static const char *size_h_art[] = {
    "    X       X    ", "   XX       XX   ", "  XXX       XXX  ", " XXXX       XXXX ",
    "XXXXXXXXXXXXXXXXX", " XXXX       XXXX ", "  XXX       XXX  ", "   XX       XX   ",
    "    X       X    ",
};
static const char *cross_art[] = {
    "      X      ", "      X      ", "      X      ", "      X      ", "      X      ",
    "             ", "XXXXX   XXXXX", "             ", "      X      ", "      X      ",
    "      X      ", "      X      ", "      X      ",
};
static const char *size_d_art[] = {
    "XXXXX      ", "XXXX       ", "XXX        ", "XX X       ", "X   X      ",
    "     X     ", "      X   X", "       X XX", "        XXX", "       XXXX", "      XXXXX",
};

/* transform: 0 = normal, 1 = transposto, 2 = espelhado na horizontal */
static void build_cursor(int id, const char **art, int rows, int hx, int hy, int transform, bool outline) {
    int cols = (int)strlen(art[0]);
    int w = transform == 1 ? rows : cols, h = transform == 1 ? cols : rows;
    cursor_img_t *c = &cursors[id];
    c->w = w + 2;
    c->h = h + 2;
    c->hx = (transform == 1 ? hy : (transform == 2 ? cols - 1 - hx : hx)) + 1;
    c->hy = (transform == 1 ? hx : hy) + 1;
    memset(c->px, 0, sizeof(c->px));
    char grid[34][34];
    memset(grid, ' ', sizeof(grid));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx = transform == 1 ? y : (transform == 2 ? cols - 1 - x : x);
            int sy = transform == 1 ? x : y;
            grid[y + 1][x + 1] = art[sy][sx];
        }
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            char g = grid[y][x];
            uint32_t col = 0;
            if (g == 'X') col = HEX(0x000000);
            else if (g == '.') col = HEX(0xFFFFFF);
            else if (outline) {
                for (int dy = -1; dy <= 1 && !col; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int ny = y + dy, nx = x + dx;
                        if (ny >= 0 && nx >= 0 && ny < 34 && nx < 34 && grid[ny][nx] == 'X') {
                            col = HEX(0xFFFFFF);
                            break;
                        }
                    }
            }
            c->px[y * c->w + x] = col;
        }
}

static void build_cursors(void) {
    build_cursor(CURSOR_ARROW, arrow_art, 19, 0, 0, 0, false);
    build_cursor(CURSOR_HAND, arrow_art, 19, 0, 0, 0, false);
    build_cursor(CURSOR_IBEAM, ibeam_art, 16, 2, 8, 0, true);
    build_cursor(CURSOR_SIZE_H, size_h_art, 9, 8, 4, 0, true);
    build_cursor(CURSOR_SIZE_V, size_h_art, 9, 8, 4, 1, true);
    build_cursor(CURSOR_SIZE_D1, size_d_art, 11, 5, 5, 0, true);
    build_cursor(CURSOR_SIZE_D2, size_d_art, 11, 5, 5, 2, true);
    build_cursor(CURSOR_CROSS, cross_art, 13, 6, 6, 0, true);
}

static rect_t cursor_rect(void) {
    cursor_img_t *c = &cursors[cursor_shape];
    return R(mouse_x - c->hx, mouse_y - c->hy, c->w, c->h);
}

static void draw_cursor(void) {
    cursor_img_t *c = &cursors[cursor_shape];
    rect_t r = cursor_rect();
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[y * c->w + x];
            if (p) gfx_pixel(scr, r.x + x, r.y + y, p);
        }
}

/* ------------------------------------------------------------------ */
/* Geometria e utilitários                                              */
/* ------------------------------------------------------------------ */
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int iabs(int a) { return a < 0 ? -a : a; }

static bool is_deco(window_t *w) { return w->flags & WF_DECORATED; }
static bool sticky(window_t *w) {
    return !(w->flags & WF_DECORATED) || (w->flags & (WF_DESKTOP | WF_TOPMOST));
}
static bool on_desk(window_t *w) { return sticky(w) || w->desk == cur_desk; }
static bool shown(window_t *w) { return w->visible && !w->minimized && on_desk(w); }
static bool rounded(window_t *w) { return is_deco(w) && w->state != WS_MAXIMIZED; }
static bool can_max(window_t *w) { return (w->flags & WF_RESIZABLE) && !(w->flags & WF_NOMAXIMIZE); }
static int layer(window_t *w) { return (w->flags & WF_DESKTOP) ? 0 : (w->flags & WF_TOPMOST) ? 2 : 1; }

static rect_t client_rect(window_t *w) {
    rect_t f = w->frame;
    if (!is_deco(w)) return f;
    return R(f.x + BORDER, f.y + BORDER + TITLE_H, f.w - 2 * BORDER, f.h - 2 * BORDER - TITLE_H);
}

static rect_t outer_rect(window_t *w) {
    rect_t f = w->frame;
    if ((is_deco(w) || (w->flags & WF_SHADOW)) && w->state != WS_MAXIMIZED)
        return R(f.x - SHADOW, f.y - SHADOW, f.w + 2 * SHADOW, f.h + 2 * SHADOW + 2);
    return f;
}

static rect_t title_rect(window_t *w) {
    return R(w->frame.x, w->frame.y, w->frame.w, TITLE_H + BORDER);
}

rect_t wm_workarea(void) { return R(0, MENUBAR_H, scr_w, scr_h - MENUBAR_H - DOCK_H); }
int wm_screen_w(void) { return scr_w; }
int wm_screen_h(void) { return scr_h; }

static void mark_dirty(rect_t r) {
    r = rect_intersect(r, R(0, 0, scr_w, scr_h));
    if (rect_empty(r)) return;
    uint64_t f = irq_save();
    bool merged = false;
    for (int i = 0; i < ndirty && !merged; i++) {
        if (!rect_empty(rect_intersect(dirty[i], r))) {
            dirty[i] = rect_union(dirty[i], r);
            merged = true;
        }
    }
    if (!merged) {
        if (ndirty < MAX_DIRTY) {
            dirty[ndirty++] = r;
        } else {
            for (int i = 1; i < ndirty; i++) dirty[0] = rect_union(dirty[0], dirty[i]);
            dirty[0] = rect_union(dirty[0], r);
            ndirty = 1;
        }
    }
    irq_restore(f);
    if (gui_task) task_wake(gui_task);
}

static void post(window_t *w, event_t e) {
    e.win = w;
    ev_post(w->owner, &e);
}

static void notify_shell(void) {
    if (shell_task) {
        event_t e = { .type = EV_WINLIST };
        ev_post(shell_task, &e);
    }
}

static void shell_cmd(int cmd) {
    if (shell_task) {
        event_t e = { .type = EV_SHELL, .key = (uint32_t)cmd };
        ev_post(shell_task, &e);
    }
}

static window_t *find_by_id(int id) {
    for (int i = 0; i < nwin; i++)
        if (zorder[i]->id == id) return zorder[i];
    return NULL;
}

static bool alive(window_t *w) {
    for (int i = 0; i < nwin; i++)
        if (zorder[i] == w) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Pilha de janelas, foco e estados                                     */
/* ------------------------------------------------------------------ */
static void z_remove(window_t *w) {
    for (int i = 0; i < nwin; i++) {
        if (zorder[i] != w) continue;
        memmove(&zorder[i], &zorder[i + 1], (nwin - i - 1) * sizeof(window_t *));
        nwin--;
        return;
    }
}

static void z_insert_top(window_t *w) {
    int pos = nwin;
    while (pos > 0 && layer(zorder[pos - 1]) > layer(w)) pos--;
    memmove(&zorder[pos + 1], &zorder[pos], (nwin - pos) * sizeof(window_t *));
    zorder[pos] = w;
    nwin++;
}

static void raise_window(window_t *w) {
    z_remove(w);
    z_insert_top(w);
    mark_dirty(outer_rect(w));
}

static void set_focus(window_t *w) {
    if (w && (w->flags & WF_NOFOCUS)) return;
    if (focused == w) return;
    window_t *old = focused;
    focused = w;
    if (old) {
        post(old, (event_t){ .type = EV_BLUR });
        mark_dirty(outer_rect(old));
    }
    if (w) {
        post(w, (event_t){ .type = EV_FOCUS });
        mark_dirty(outer_rect(w));
    }
    notify_shell();
}

static void focus_next(window_t *except) {
    for (int i = nwin - 1; i >= 0; i--) {
        window_t *w = zorder[i];
        if (w != except && shown(w) && is_deco(w) && !(w->flags & WF_NOFOCUS)) {
            set_focus(w);
            return;
        }
    }
    set_focus(NULL);
}

static void switch_desk(int d);

static void activate(window_t *w) {
    if (!sticky(w) && w->desk != cur_desk) switch_desk(w->desk);
    if (w->minimized) {
        w->minimized = false;
        notify_shell();
    }
    if (!(w->flags & WF_DESKTOP)) raise_window(w);
    set_focus(w);
    mark_dirty(outer_rect(w));
}

static void set_frame(window_t *w, rect_t f) {
    mark_dirty(outer_rect(w));
    bool size_changed = f.w != w->frame.w || f.h != w->frame.h;
    w->frame = f;
    mark_dirty(outer_rect(w));
    if (size_changed) {
        w->resize_pending = true;
        rect_t c = client_rect(w);
        post(w, (event_t){ .type = EV_RESIZE, .x = c.w, .y = c.h });
    }
}

static void minimize(window_t *w) {
    w->minimized = true;
    mark_dirty(outer_rect(w));
    if (capture_win == w) capture_win = NULL;
    if (hover_win == w) hover_win = NULL;
    if (focused == w) focus_next(w);
    notify_shell();
}

static void maximize(window_t *w) {
    if (!can_max(w)) return;
    if (w->state == WS_NORMAL) w->restore = w->frame;
    w->state = WS_MAXIMIZED;
    set_frame(w, wm_workarea());
}

static void snap(window_t *w, win_state_t side) {
    if (!(w->flags & WF_RESIZABLE)) return;
    if (w->state == WS_NORMAL) w->restore = w->frame;
    w->state = side;
    rect_t wa = wm_workarea();
    if (side == WS_SNAP_LEFT) set_frame(w, R(wa.x, wa.y, wa.w / 2, wa.h));
    else set_frame(w, R(wa.x + wa.w / 2, wa.y, wa.w - wa.w / 2, wa.h));
}

static void restore_window(window_t *w) {
    if (w->state == WS_NORMAL) return;
    w->state = WS_NORMAL;
    set_frame(w, w->restore);
}

static void toggle_max(window_t *w) {
    if (w->state == WS_MAXIMIZED) restore_window(w);
    else maximize(w);
}

static void show_desktop_locked(void) {
    int visible = 0;
    for (int i = 0; i < nwin; i++)
        if (is_deco(zorder[i]) && shown(zorder[i])) visible++;
    if (visible) {
        n_hidden_by_desktop = 0;
        for (int i = 0; i < nwin; i++) {
            window_t *w = zorder[i];
            if (is_deco(w) && shown(w)) {
                hidden_by_desktop[n_hidden_by_desktop++] = w->id;
                w->minimized = true;
                mark_dirty(outer_rect(w));
            }
        }
        set_focus(NULL);
    } else {
        for (int i = 0; i < n_hidden_by_desktop; i++) {
            window_t *w = find_by_id(hidden_by_desktop[i]);
            if (w) activate(w);
        }
        n_hidden_by_desktop = 0;
    }
    notify_shell();
}


static rect_t button_rect(window_t *w, int which);

/* ------------------------------------------------------------------ */
/* Áreas de trabalho virtuais                                           */
/* ------------------------------------------------------------------ */
static void switch_desk(int d) {
    if (d < 0 || d >= ndesks || d == cur_desk) return;
    cur_desk = d;
    capture_win = hover_win = NULL;
    if (hover_btn_win) hover_btn_win->hover_btn = 0;
    hover_btn_win = NULL;
    focused = NULL;
    focus_next(NULL);
    if (!taskview) desk_label_until = timer_ms() + 900;
    mark_dirty(R(0, 0, scr_w, scr_h));
    notify_shell();
}

static void new_desk(void) {
    if (ndesks >= MAX_DESKTOPS) return;
    ndesks++;
    switch_desk(ndesks - 1);
}

static void close_desk(int d) {
    if (ndesks <= 1) return;
    int target = d > 0 ? d - 1 : 0;
    for (int i = 0; i < nwin; i++) {
        window_t *w = zorder[i];
        if (w->desk == d) w->desk = target;
        else if (w->desk > d) w->desk--;
    }
    ndesks--;
    if (cur_desk > d || cur_desk >= ndesks) cur_desk = cur_desk > 0 ? cur_desk - 1 : 0;
    focused = NULL;
    focus_next(NULL);
    mark_dirty(R(0, 0, scr_w, scr_h));
    notify_shell();
}

/* ------------------------------------------------------------------ */
/* Snap Layouts                                                         */
/* ------------------------------------------------------------------ */
/* zonas em uma grade de 12x12 */
static const struct { int n; uint8_t z[4][4]; } layouts[6] = {
    { 2, { { 0, 0, 6, 12 }, { 6, 0, 12, 12 } } },
    { 2, { { 0, 0, 8, 12 }, { 8, 0, 12, 12 } } },
    { 3, { { 0, 0, 4, 12 }, { 4, 0, 8, 12 }, { 8, 0, 12, 12 } } },
    { 3, { { 0, 0, 6, 12 }, { 6, 0, 12, 6 }, { 6, 6, 12, 12 } } },
    { 4, { { 0, 0, 6, 6 }, { 6, 0, 12, 6 }, { 0, 6, 6, 12 }, { 6, 6, 12, 12 } } },
    { 3, { { 0, 0, 3, 12 }, { 3, 0, 9, 12 }, { 9, 0, 12, 12 } } },
};
#define FLY_CELL_W 72
#define FLY_CELL_H 48

static rect_t zone_rect(int layout, int zone) {
    rect_t wa = wm_workarea();
    const uint8_t *z = layouts[layout].z[zone];
    int x0 = wa.x + wa.w * z[0] / 12, x1 = wa.x + wa.w * z[2] / 12;
    int y0 = wa.y + wa.h * z[1] / 12, y1 = wa.y + wa.h * z[3] / 12;
    return R(x0, y0, x1 - x0, y1 - y0);
}

static rect_t fly_cell(int layout) {
    return R(fly_rect.x + 14 + (layout % 3) * (FLY_CELL_W + 12), fly_rect.y + 14 + (layout / 3) * (FLY_CELL_H + 12),
             FLY_CELL_W, FLY_CELL_H);
}

static rect_t fly_zone(int layout, int zone) {
    rect_t c = fly_cell(layout);
    const uint8_t *z = layouts[layout].z[zone];
    int x0 = c.x + c.w * z[0] / 12, x1 = c.x + c.w * z[2] / 12;
    int y0 = c.y + c.h * z[1] / 12, y1 = c.y + c.h * z[3] / 12;
    return R(x0 + 2, y0 + 2, x1 - x0 - 4, y1 - y0 - 4);
}

static rect_t fly_outer(void) {
    return R(fly_rect.x - SHADOW, fly_rect.y - SHADOW, fly_rect.w + 2 * SHADOW, fly_rect.h + 2 * SHADOW + 2);
}

static void open_flyout(window_t *w) {
    if (!w || !can_max(w)) return;
    fly_win = w;
    fly_open = true;
    fly_due = 0;
    fly_hover = -1;
    int fw = 3 * FLY_CELL_W + 4 * 12 + 4, fh = 2 * FLY_CELL_H + 3 * 12 + 4;
    rect_t b = button_rect(w, HT_MAX);
    int x = b.x + b.w / 2 - fw / 2, y = b.y + b.h + 4;
    if (x + fw > scr_w - 8) x = scr_w - 8 - fw;
    if (x < 8) x = 8;
    if (y + fh > scr_h - DOCK_H) y = b.y - fh - 4;
    fly_rect = R(x, y, fw, fh);
    mark_dirty(fly_outer());
}

static void close_flyout(void) {
    if (fly_open) mark_dirty(fly_outer());
    fly_open = false;
    fly_due = 0;
    fly_win = NULL;
}

static void snap_to(window_t *w, rect_t r) {
    if (w->state == WS_NORMAL) w->restore = w->frame;
    w->state = WS_SNAP_CUSTOM;
    set_frame(w, r);
}

static int fly_hit(int x, int y) {
    for (int l = 0; l < 6; l++)
        for (int z = 0; z < layouts[l].n; z++)
            if (rect_has(fly_zone(l, z), x, y)) return l * 8 + z;
    return -1;
}

static void draw_flyout(void) {
    rect_t r = fly_rect;
    gfx_shadow(scr, r, 8, SHADOW, 70);
    gfx_round_fill(scr, r.x, r.y, r.w, r.h, 8, color_with_alpha(theme.popup_bg, 0xFA));
    gfx_round_rect(scr, r.x, r.y, r.w, r.h, 8, theme.popup_border);
    for (int l = 0; l < 6; l++) {
        rect_t c = fly_cell(l);
        gfx_round_rect(scr, c.x, c.y, c.w, c.h, 5, color_with_alpha(theme.text, 0x50));
        for (int z = 0; z < layouts[l].n; z++) {
            rect_t zr = fly_zone(l, z);
            bool hot = fly_hover == l * 8 + z;
            gfx_round_fill(scr, zr.x, zr.y, zr.w, zr.h, 3, hot ? theme.accent : color_with_alpha(theme.text, 0x28));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Captura de tela                                                      */
/* ------------------------------------------------------------------ */
static void compose_rect(rect_t r);

/* cópia da tela inteira, sem o cursor */
static canvas_t *grab_screen(void) {
    canvas_t *img = canvas_create(scr_w, scr_h);
    if (!img) return NULL;
    hide_cursor = true;
    compose_rect(R(0, 0, scr_w, scr_h));
    hide_cursor = false;
    gfx_blit(img, 0, 0, scr, 0, 0, scr_w, scr_h);
    mark_dirty(cursor_rect());
    return img;
}

static void save_capture(canvas_t *img, rect_t area) {
    const char *dir = "/home/usuario/Imagens/Capturas de tela";
    vfs_create(NULL, dir, VFS_DIR);
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    char path[VFS_PATH_MAX], body[160];
    snprintf(path, sizeof(path), "%s/Captura %04d-%02d-%02d %02d%02d%02d.bmp", dir,
             dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    if (bmp_save(path, img, area)) {
        snprintf(body, sizeof(body), "%dx%d, salva em Imagens > Capturas de tela. Clique para abrir.", area.w, area.h);
        notify("Ferramenta de Captura", "Captura de tela salva", body, ICON_PICTURES, path);
    } else {
        notify("Ferramenta de Captura", "Não foi possível salvar a captura", "Memória insuficiente.", ICON_PICTURES, NULL);
    }
}

static void capture_full(void) {
    canvas_t *img = grab_screen();
    if (!img) return;
    save_capture(img, R(0, 0, scr_w, scr_h));
    canvas_destroy(img);
}

static void start_snip(void) {
    if (snip) return;
    snip_img = grab_screen();
    if (!snip_img) return;
    snip = true;
    snip_drag = false;
    snip_sel = R(0, 0, 0, 0);
    cursor_shape = CURSOR_CROSS;
    mark_dirty(R(0, 0, scr_w, scr_h));
}

static void end_snip(bool save, bool full) {
    if (!snip) return;
    if (save) save_capture(snip_img, full ? R(0, 0, scr_w, scr_h) : snip_sel);
    canvas_destroy(snip_img);
    snip_img = NULL;
    snip = false;
    snip_drag = false;
    cursor_shape = CURSOR_ARROW;
    mark_dirty(R(0, 0, scr_w, scr_h));
}

static rect_t snip_dirty(rect_t sel) {
    return R(sel.x - 4, sel.y - 4, sel.w + 8, sel.h + 40);
}

static void draw_snip(rect_t clip) {
    gfx_blit(scr, 0, 0, snip_img, 0, 0, scr_w, scr_h);
    rect_t s = snip_sel;
    uint32_t dim = ARGB(0x90, 0x000000);
    /* escurece tudo, menos a seleção */
    gfx_fill(scr, 0, 0, scr_w, s.y, dim);
    gfx_fill(scr, 0, s.y + s.h, scr_w, scr_h - s.y - s.h, dim);
    gfx_fill(scr, 0, s.y, s.x, s.h, dim);
    gfx_fill(scr, s.x + s.w, s.y, scr_w - s.x - s.w, s.h, dim);
    if (!rect_empty(s)) {
        gfx_rect(scr, s.x - 1, s.y - 1, s.w + 2, s.h + 2, HEX(0xFFFFFF));
        gfx_rect(scr, s.x - 2, s.y - 2, s.w + 4, s.h + 4, theme.accent);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d × %d", s.w, s.h);
        int tw = font_width(buf, 0) + 16;
        int ly = s.y + s.h + 8 + 24 > scr_h ? s.y + s.h - 32 : s.y + s.h + 8;
        gfx_round_fill(scr, s.x, ly, tw, 24, 4, ARGB(0xE0, 0x202020));
        font_draw(scr, s.x + 8, ly + 4, buf, HEX(0xFFFFFF), 0);
    }
    const char *hint = "Arraste para recortar uma área  •  Clique: tela inteira  •  Esc: cancelar";
    int hw = font_width(hint, 0) + 32;
    rect_t hr = R((scr_w - hw) / 2, 16, hw, 36);
    if (!rect_empty(rect_intersect(hr, clip))) {
        gfx_round_fill(scr, hr.x, hr.y, hr.w, hr.h, 8, ARGB(0xF0, 0x202020));
        font_draw(scr, hr.x + 16, hr.y + 10, hint, HEX(0xFFFFFF), 0);
    }
}

/* ------------------------------------------------------------------ */
/* Visão de Tarefas                                                     */
/* ------------------------------------------------------------------ */
#define TV_BAR_H 128
#define DESK_BOX_W 144
#define DESK_BOX_H 81

typedef struct { window_t *w; rect_t r; } tv_thumb_t;

static int tv_thumbs(tv_thumb_t *out, int max) {
    int n = 0;
    for (int i = nwin - 1; i >= 0 && n < max; i--) {
        window_t *w = zorder[i];
        if (is_deco(w) && w->visible && !(w->flags & WF_NOTASKBAR) && w->desk == cur_desk) out[n++].w = w;
    }
    if (!n) return 0;
    rect_t area = R(40, TV_BAR_H + 24, scr_w - 80, scr_h - TV_BAR_H - 24 - DOCK_H - 16);
    int cols = 1;
    while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;
    int cw = area.w / cols, ch = area.h / rows;
    for (int i = 0; i < n; i++) {
        rect_t f = out[i].w->frame;
        int maxw = cw - 40, maxh = ch - 56;
        int tw = maxw, th = (int)((int64_t)f.h * maxw / f.w);
        if (th > maxh) { th = maxh; tw = (int)((int64_t)f.w * maxh / f.h); }
        if (tw > f.w) { tw = f.w; th = f.h; }
        int col = i % cols, row = i / cols;
        int rowcount = (row == rows - 1) ? n - row * cols : cols;
        int offset = (area.w - rowcount * cw) / 2;
        out[i].r = R(area.x + offset + col * cw + (cw - tw) / 2, area.y + row * ch + 32 + (ch - 32 - th) / 2, tw, th);
    }
    return n;
}

static rect_t desk_box(int i) {
    int total = (ndesks + (ndesks < MAX_DESKTOPS ? 1 : 0)) * (DESK_BOX_W + 20) - 20;
    int x0 = (scr_w - total) / 2;
    return R(x0 + i * (DESK_BOX_W + 20), 16, DESK_BOX_W, DESK_BOX_H);
}

static rect_t thumb_close(rect_t r) { return R(r.x + r.w - 28, r.y - 30, 28, 28); }
static rect_t desk_close(rect_t r) { return R(r.x + r.w - 26, r.y + 2, 24, 24); }

/* códigos: 0..63 miniatura, 100+i fechar janela, 200+i área, 300+i fechar área, 400 nova */
static int tv_hit(int x, int y) {
    tv_thumb_t t[64];
    int n = tv_thumbs(t, 64);
    for (int i = 0; i < n; i++) {
        if (rect_has(thumb_close(t[i].r), x, y)) return 100 + i;
        if (rect_has(R(t[i].r.x, t[i].r.y - 32, t[i].r.w, t[i].r.h + 32), x, y)) return i;
    }
    for (int i = 0; i < ndesks; i++) {
        rect_t b = desk_box(i);
        if (ndesks > 1 && rect_has(desk_close(b), x, y)) return 300 + i;
        if (rect_has(R(b.x, b.y, b.w, b.h + 24), x, y)) return 200 + i;
    }
    if (ndesks < MAX_DESKTOPS && rect_has(desk_box(ndesks), x, y)) return 400;
    return -1;
}

static window_t *desktop_window(void) {
    for (int i = 0; i < nwin; i++)
        if (zorder[i]->flags & WF_DESKTOP) return zorder[i];
    return NULL;
}

static void draw_taskview(void) {
    gfx_fill(scr, 0, 0, scr_w, scr_h, ARGB(0xA8, 0x0B0F14));
    tv_thumb_t t[64];
    int n = tv_thumbs(t, 64);
    for (int i = 0; i < n; i++) {
        window_t *w = t[i].w;
        rect_t r = t[i].r;
        bool hot = tv_hover == i || tv_hover == 100 + i;
        if (tv_dragging && w->id == tv_drag_id) continue;
        if (hot) gfx_round_fill(scr, r.x - 8, r.y - 38, r.w + 16, r.h + 46, 10, ARGB(0x40, 0xFFFFFF));
        icon_draw(scr, w->icon, r.x, r.y - 28, 20);
        font_draw_ellipsis(scr, r.x + 28, r.y - 26, r.w - 60, w->title, HEX(0xFFFFFF), 0);
        gfx_shadow(scr, r, 6, 12, 90);
        gfx_blit_scaled(scr, r, w->canvas, R(0, 0, w->canvas->w, w->canvas->h), true);
        gfx_round_rect(scr, r.x, r.y, r.w, r.h, 4, hot ? theme.accent : ARGB(0x60, 0xFFFFFF));
        if (hot) {
            rect_t cb = thumb_close(r);
            gfx_round_fill(scr, cb.x, cb.y, cb.w, cb.h, 4, tv_hover == 100 + i ? theme.close_hover : ARGB(0x50, 0xFFFFFF));
            gfx_line(scr, cb.x + 9.5f, cb.y + 9.5f, cb.x + 18.5f, cb.y + 18.5f, 1.4f, HEX(0xFFFFFF));
            gfx_line(scr, cb.x + 18.5f, cb.y + 9.5f, cb.x + 9.5f, cb.y + 18.5f, 1.4f, HEX(0xFFFFFF));
        }
    }
    if (!n) {
        const char *msg = "Nenhuma janela aberta nesta área de trabalho";
        font_draw(scr, (scr_w - font_width(msg, FONT_2X)) / 2, scr_h / 3, msg, HEX(0xFFFFFF), FONT_2X);
    }

    /* faixa das áreas de trabalho */
    gfx_fill(scr, 0, 0, scr_w, TV_BAR_H, ARGB(0x50, 0x000000));
    window_t *dw = desktop_window();
    for (int i = 0; i < ndesks; i++) {
        rect_t b = desk_box(i);
        bool cur = i == cur_desk, hot = tv_hover == 200 + i || tv_hover == 300 + i;
        if (dw) gfx_blit_scaled(scr, b, dw->canvas, R(0, 0, dw->canvas->w, dw->canvas->h), false);
        for (int k = 0; k < nwin; k++) {
            window_t *w = zorder[k];
            if (!is_deco(w) || !w->visible || w->minimized || w->desk != i) continue;
            rect_t f = w->frame;
            rect_t m = R(b.x + f.x * b.w / scr_w, b.y + f.y * b.h / scr_h, f.w * b.w / scr_w, f.h * b.h / scr_h);
            gfx_fill(scr, m.x, m.y, m.w, m.h, color_with_alpha(theme.window_bg, 0xE0));
            gfx_rect(scr, m.x, m.y, m.w, m.h, ARGB(0x80, 0x000000));
        }
        gfx_round_rect(scr, b.x - 2, b.y - 2, b.w + 4, b.h + 4, 6, cur ? theme.accent : (hot ? ARGB(0xC0, 0xFFFFFF) : ARGB(0x50, 0xFFFFFF)));
        if (cur) gfx_round_rect(scr, b.x - 3, b.y - 3, b.w + 6, b.h + 6, 7, theme.accent);
        char name[32];
        snprintf(name, sizeof(name), "Área de trabalho %d", i + 1);
        font_draw(scr, b.x, b.y + b.h + 6, name, HEX(0xFFFFFF), cur ? FONT_BOLD : 0);
        if (hot && ndesks > 1) {
            rect_t cb = desk_close(b);
            gfx_round_fill(scr, cb.x, cb.y, cb.w, cb.h, 4, tv_hover == 300 + i ? theme.close_hover : ARGB(0xA0, 0x000000));
            gfx_line(scr, cb.x + 8.5f, cb.y + 8.5f, cb.x + 15.5f, cb.y + 15.5f, 1.4f, HEX(0xFFFFFF));
            gfx_line(scr, cb.x + 15.5f, cb.y + 8.5f, cb.x + 8.5f, cb.y + 15.5f, 1.4f, HEX(0xFFFFFF));
        }
    }
    if (ndesks < MAX_DESKTOPS) {
        rect_t b = desk_box(ndesks);
        gfx_round_fill(scr, b.x, b.y, b.w, b.h, 6, tv_hover == 400 ? ARGB(0x50, 0xFFFFFF) : ARGB(0x28, 0xFFFFFF));
        gfx_line(scr, b.x + b.w / 2.0f, b.y + 30, b.x + b.w / 2.0f, b.y + 66, 2.5f, HEX(0xFFFFFF));
        gfx_line(scr, b.x + b.w / 2.0f - 18, b.y + 48, b.x + b.w / 2.0f + 18, b.y + 48, 2.5f, HEX(0xFFFFFF));
        font_draw(scr, b.x, b.y + b.h + 6, "Nova área de trabalho", HEX(0xFFFFFF), 0);
    }
    /* miniatura sendo arrastada */
    if (tv_dragging) {
        window_t *w = find_by_id(tv_drag_id);
        if (w) {
            rect_t r = R(mouse_x - 80, mouse_y - 50, 160, 100);
            gfx_blit_scaled(scr, r, w->canvas, R(0, 0, w->canvas->w, w->canvas->h), false);
            gfx_rect(scr, r.x, r.y, r.w, r.h, theme.accent);
        }
    }
}

static void open_taskview(bool open) {
    if (open == taskview) return;
    taskview = open;
    tv_hover = tv_press = -1;
    tv_dragging = false;
    if (open) {
        close_flyout();
        if (alttab) { alttab = false; }
    }
    mark_dirty(R(0, 0, scr_w, scr_h));
}

static void tv_motion(void) {
    if (tv_press >= 0 && tv_press < 64 && (mouse_btn & 1) && !tv_dragging) {
        tv_thumb_t t[64];
        int n = tv_thumbs(t, 64);
        if (tv_press < n) {
            rect_t r = t[tv_press].r;
            if (!rect_has(R(r.x - 8, r.y - 40, r.w + 16, r.h + 48), mouse_x, mouse_y) ||
                iabs(mouse_x - drag_mx) > 8 || iabs(mouse_y - drag_my) > 8) {
                tv_dragging = true;
                tv_drag_id = t[tv_press].w->id;
            }
        }
    }
    int h = tv_hit(mouse_x, mouse_y);
    if (h != tv_hover || tv_dragging) {
        tv_hover = h;
        mark_dirty(R(0, 0, scr_w, scr_h));
    }
}

static void tv_click(bool pressed) {
    int h = tv_hit(mouse_x, mouse_y);
    if (pressed) {
        tv_press = h;
        drag_mx = mouse_x;
        drag_my = mouse_y;
        return;
    }
    tv_thumb_t t[64];
    int n = tv_thumbs(t, 64);
    if (tv_dragging) {
        window_t *w = find_by_id(tv_drag_id);
        if (w && h >= 200 && h < 200 + ndesks) w->desk = h - 200;
        else if (w && h == 400 && ndesks < MAX_DESKTOPS) w->desk = ndesks++;
        if (w && w == focused && w->desk != cur_desk) { focused = NULL; focus_next(NULL); }
        tv_dragging = false;
        tv_press = -1;
        mark_dirty(R(0, 0, scr_w, scr_h));
        notify_shell();
        return;
    }
    int p = tv_press;
    tv_press = -1;
    if (h != p) return;
    if (h < 0) open_taskview(false);
    else if (h < 64 && h < n) { activate(t[h].w); open_taskview(false); }
    else if (h >= 100 && h < 164 && h - 100 < n) post(t[h - 100].w, (event_t){ .type = EV_CLOSE });
    else if (h >= 200 && h < 300) switch_desk(h - 200);
    else if (h >= 300 && h < 400) close_desk(h - 300);
    else if (h == 400) new_desk();
    mark_dirty(R(0, 0, scr_w, scr_h));
}

static void draw_desk_label(void) {
    char buf[40];
    snprintf(buf, sizeof(buf), "Área de trabalho %d", cur_desk + 1);
    int w = font_width(buf, FONT_2X) + 48, h = 64;
    rect_t r = R((scr_w - w) / 2, (scr_h - h) / 2, w, h);
    gfx_round_fill(scr, r.x, r.y, r.w, r.h, 10, ARGB(0xE0, 0x202020));
    font_draw(scr, r.x + 24, r.y + 16, buf, HEX(0xFFFFFF), FONT_2X);
}

static rect_t desk_label_rect(void) { return R(scr_w / 2 - 260, scr_h / 2 - 40, 520, 80); }

/* ------------------------------------------------------------------ */
/* Desenho                                                              */
/* ------------------------------------------------------------------ */
static rect_t button_rect(window_t *w, int which) {
    rect_t f = w->frame;
    int idx = which == HT_CLOSE ? 0 : which == HT_MIN ? 1 : 2;
    int cx = f.x + 18 + idx * 20, cy = f.y + BORDER + TITLE_H / 2;
    return R(cx - 8, cy - 8, 16, 16);
}

/* Fechar (vermelho), minimizar (amarelo) e zoom (verde). Ao passar o mouse
 * sobre qualquer um deles, os três mostram seus símbolos. */
static void draw_lights(window_t *w, bool active) {
    static const uint32_t colors[3] = { 0xFF5F57, 0xFEBC2E, 0x28C840 };
    static const uint32_t symbols[3] = { 0x8C1A10, 0x985712, 0x0A6517 };
    static const int which[3] = { HT_CLOSE, HT_MIN, HT_MAX };
    bool hover = w->hover_btn != 0;
    for (int i = 0; i < 3; i++) {
        rect_t b = button_rect(w, which[i]);
        float cx = b.x + 8, cy = b.y + 8, r = LIGHT_D / 2.0f;
        bool enabled = which[i] != HT_MAX || can_max(w);
        uint32_t col = ((active || hover) && enabled) ? HEX(colors[i]) : (theme.dark ? HEX(0x4E4E50) : HEX(0xD3D3D5));
        if (w->press_btn == which[i] && w->hover_btn == which[i]) col = color_mix(col, HEX(0x000000), 60);
        gfx_circle(scr, cx, cy, r, col);
        gfx_ring(scr, cx, cy, r - 0.5f, 1.0f, ARGB(0x28, 0x000000));
        if (!hover || !enabled) continue;
        uint32_t g = HEX(symbols[i]);
        if (which[i] == HT_CLOSE) {
            gfx_line(scr, cx - 2.6f, cy - 2.6f, cx + 2.6f, cy + 2.6f, 1.3f, g);
            gfx_line(scr, cx + 2.6f, cy - 2.6f, cx - 2.6f, cy + 2.6f, 1.3f, g);
        } else if (which[i] == HT_MIN) {
            gfx_line(scr, cx - 3.2f, cy, cx + 3.2f, cy, 1.4f, g);
        } else {
            /* dois triângulos em diagonal: "ampliar" */
            gfx_triangle(scr, cx - 3.5f, cy - 3.5f, cx + 1.5f, cy - 3.5f, cx - 3.5f, cy + 1.5f, g);
            gfx_triangle(scr, cx + 3.5f, cy + 3.5f, cx - 1.5f, cy + 3.5f, cx + 3.5f, cy - 1.5f, g);
        }
    }
}

static void draw_window(window_t *w) {
    rect_t f = w->frame;
    bool deco = is_deco(w);
    bool round = rounded(w);
    bool active = w == focused;
    canvas_t *cv = w->canvas;
    rect_t c = client_rect(w);

    if ((deco || (w->flags & WF_SHADOW)) && w->state != WS_MAXIMIZED)
        gfx_shadow(scr, f, RADIUS, SHADOW, (active || !deco) ? 72 : 36);

    if (!deco) {
        if (w->flags & WF_ALPHA) gfx_blit_alpha(scr, c.x, c.y, cv, 0, 0, c.w, c.h);
        else gfx_blit(scr, c.x, c.y, cv, 0, 0, c.w, c.h);
        return;
    }

    gfx_round_fill(scr, f.x, f.y, f.w, f.h, round ? RADIUS : 0, active ? theme.title_active : theme.title_inactive);

    /* título centralizado, sem invadir os botões da esquerda */
    rect_t saved = scr->clip;
    rect_t title_area = R(f.x + 76, f.y + BORDER, f.w - 152, TITLE_H);
    canvas_clip(scr, rect_intersect(saved, title_area));
    int tw = imin(font_width(w->title, 0), title_area.w);
    font_draw_ellipsis(scr, f.x + (f.w - tw) / 2, f.y + BORDER + (TITLE_H - 16) / 2, title_area.w, w->title,
                       active ? theme.title_text : theme.title_text_inactive, 0);
    scr->clip = saved;
    draw_lights(w, active);

    /* durante um redimensionamento o canvas pode ainda ser menor que a janela:
     * o que ele não cobre recebe a cor de fundo (sempre dentro da área cliente) */
    if (cv->w < c.w || cv->h < c.h)
        gfx_round_fill_corners(scr, c.x, c.y, c.w, c.h, round ? RADIUS - 1 : 0, theme.window_bg,
                               CORNER_BL | CORNER_BR);

    /* conteúdo do aplicativo; as últimas linhas respeitam os cantos arredondados */
    int cw = imin(cv->w, c.w), ch = imin(cv->h, c.h);
    int curve_start = round ? f.y + f.h - RADIUS : f.y + f.h;
    int straight = imin(ch, curve_start - c.y);
    if (straight > 0) gfx_blit(scr, c.x, c.y, cv, 0, 0, cw, straight);
    for (int y = c.y + imax(straight, 0); y < c.y + ch; y++) {
        int j = f.y + f.h - 1 - y;
        int ins = (j >= 0 && j < RADIUS) ? imax(corner_inset[j] - BORDER, 0) : 0;
        gfx_blit(scr, c.x + ins, y, cv, ins, y - c.y, cw - 2 * ins, 1);
    }
    gfx_round_rect(scr, f.x, f.y, f.w, f.h, round ? RADIUS : 0, active ? theme.frame_active : theme.frame_inactive);
}

static rect_t alttab_layout(void) {
    int item = 96;
    int n = imin(alttab_n, (scr_w - 80) / item);
    int w = n * item + 24, h = 128;
    return R((scr_w - w) / 2, (scr_h - h) / 2 - 40, w, h + 30);
}

static void draw_alttab(void) {
    rect_t r = alttab_rect;
    rect_t panel = R(r.x, r.y, r.w, r.h - 30);
    gfx_shadow(scr, panel, 18, SHADOW, 90);
    gfx_round_fill(scr, panel.x, panel.y, panel.w, panel.h, 18, ARGB(0xE0, 0x262628));
    gfx_round_rect(scr, panel.x, panel.y, panel.w, panel.h, 18, ARGB(0x40, 0xFFFFFF));
    int item = 96;
    int n = (r.w - 24) / item;
    for (int i = 0; i < n && i < alttab_n; i++) {
        window_t *w = find_by_id(alttab_ids[i]);
        if (!w) continue;
        int x = r.x + 12 + i * item;
        if (i == alttab_sel) gfx_round_fill(scr, x + 4, panel.y + 12, item - 8, panel.h - 24, 14, ARGB(0x48, 0xFFFFFF));
        icon_draw(scr, w->icon, x + (item - 64) / 2, panel.y + (panel.h - 64) / 2, 64);
        if (i == alttab_sel) {
            int tw = imin(font_width(w->title, 0), 400);
            rect_t lr = R(r.x + (r.w - tw) / 2 - 10, panel.y + panel.h + 6, tw + 20, 24);
            gfx_round_fill(scr, lr.x, lr.y, lr.w, lr.h, 6, ARGB(0xD0, 0x262628));
            font_draw_ellipsis(scr, lr.x + 10, lr.y + 4, 400, w->title, HEX(0xFFFFFF), 0);
        }
    }
}

/* Parte da janela que com certeza cobre tudo o que está atrás dela. */
static rect_t opaque_rect(window_t *w) {
    if (w->flags & WF_ALPHA) return R(0, 0, 0, 0);
    rect_t f = w->frame;
    if (rounded(w)) return R(f.x, f.y + RADIUS, f.w, f.h - 2 * RADIUS);
    return f;
}

static bool rect_contains(rect_t outer, rect_t inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

static void compose_rect(rect_t r) {
    canvas_clip(scr, r);
    if (snip) {
        draw_snip(r);
        draw_cursor();
        goto present;
    }
    if (lock_win && shown(lock_win)) {
        draw_window(lock_win);
        if (!hide_cursor) draw_cursor();
        goto present;
    }
    /* oclusão: se uma janela opaca cobre toda a área, nada abaixo dela aparece */
    int start = 0;
    for (int i = nwin - 1; i >= 0; i--) {
        if (shown(zorder[i]) && rect_contains(opaque_rect(zorder[i]), r)) {
            start = i;
            break;
        }
    }
    if (taskview) start = 0;
    if (start == 0) gfx_set(scr, r.x, r.y, r.w, r.h, HEX(0x16324F));
    bool tv_drawn = false;
    for (int i = start; i < nwin; i++) {
        window_t *w = zorder[i];
        if (taskview && layer(w) == 1) continue;           /* janelas normais: só miniaturas */
        if (taskview && !tv_drawn && layer(w) == 2) { draw_taskview(); tv_drawn = true; }
        if (shown(w) && !rect_empty(rect_intersect(outer_rect(w), r))) draw_window(w);
    }
    if (taskview && !tv_drawn) draw_taskview();
    if (fly_open) draw_flyout();
    if (desk_label_until && !taskview) draw_desk_label();
    if (snap_zone) {
        rect_t s = rect_inset(snap_rect, 6);
        gfx_round_fill(scr, s.x, s.y, s.w, s.h, 8, color_with_alpha(theme.accent, 0x40));
        gfx_round_rect(scr, s.x, s.y, s.w, s.h, 8, color_with_alpha(theme.accent, 0xB0));
    }
    if (alttab) draw_alttab();
    if (!hide_cursor) draw_cursor();
present:
    canvas_unclip(scr);
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(fb + y * fb_stride + r.x, scr->px + y * scr_w + r.x, r.w * 4);
}

static void compose_all(void) {
    rect_t list[MAX_DIRTY];
    uint64_t f = irq_save();
    int n = ndirty;
    memcpy(list, dirty, n * sizeof(rect_t));
    ndirty = 0;
    irq_restore(f);

    mutex_lock(&lock);
    for (int i = 0; i < n; i++) compose_rect(list[i]);
    mutex_unlock(&lock);
}

/* ------------------------------------------------------------------ */
/* Mouse                                                                */
/* ------------------------------------------------------------------ */
static window_t *hit_test(int x, int y, int *ht, int *edges) {
    *edges = 0;
    if (lock_win) {
        *ht = rect_has(lock_win->frame, x, y) ? HT_CLIENT : HT_NONE;
        return *ht == HT_CLIENT ? lock_win : NULL;
    }
    for (int i = nwin - 1; i >= 0; i--) {
        window_t *w = zorder[i];
        if (!shown(w)) continue;
        rect_t f = w->frame;
        if (is_deco(w) && (w->flags & WF_RESIZABLE) && w->state != WS_MAXIMIZED &&
            rect_has(R(f.x - 6, f.y - 6, f.w + 12, f.h + 12), x, y)) {
            int e = 0;
            if (x < f.x + 4) e |= EDGE_L;
            else if (x >= f.x + f.w - 4) e |= EDGE_R;
            if (y < f.y + 4) e |= EDGE_T;
            else if (y >= f.y + f.h - 4) e |= EDGE_B;
            if (e) {
                if ((e & (EDGE_L | EDGE_R)) && !(e & (EDGE_T | EDGE_B))) {
                    if (y < f.y + 16) e |= EDGE_T;
                    else if (y >= f.y + f.h - 16) e |= EDGE_B;
                } else if ((e & (EDGE_T | EDGE_B)) && !(e & (EDGE_L | EDGE_R))) {
                    if (x < f.x + 16) e |= EDGE_L;
                    else if (x >= f.x + f.w - 16) e |= EDGE_R;
                }
                *ht = HT_RESIZE;
                *edges = e;
                return w;
            }
        }
        if (!rect_has(f, x, y)) continue;
        if ((w->flags & WF_CLICKTHRU) && x - f.x < w->canvas->w && y - f.y < w->canvas->h &&
            !(w->canvas->px[(y - f.y) * w->canvas->stride + (x - f.x)] >> 24))
            continue;
        if (is_deco(w) && y < f.y + BORDER + TITLE_H) {
            if (rect_has(button_rect(w, HT_CLOSE), x, y)) *ht = HT_CLOSE;
            else if (can_max(w) && rect_has(button_rect(w, HT_MAX), x, y)) *ht = HT_MAX;
            else if (rect_has(button_rect(w, HT_MIN), x, y)) *ht = HT_MIN;
            else *ht = HT_CAPTION;
        } else {
            *ht = HT_CLIENT;
        }
        return w;
    }
    *ht = HT_NONE;
    return NULL;
}

static void set_cursor(int shape) {
    if (shape == cursor_shape) return;
    mark_dirty(cursor_rect());
    cursor_shape = shape;
    mark_dirty(cursor_rect());
}

static int edges_cursor(int e) {
    if ((e & EDGE_L && e & EDGE_T) || (e & EDGE_R && e & EDGE_B)) return CURSOR_SIZE_D1;
    if ((e & EDGE_R && e & EDGE_T) || (e & EDGE_L && e & EDGE_B)) return CURSOR_SIZE_D2;
    if (e & (EDGE_L | EDGE_R)) return CURSOR_SIZE_H;
    return CURSOR_SIZE_V;
}

static void set_hover_button(window_t *w, int btn) {
    if (hover_btn_win && (hover_btn_win != w || hover_btn_win->hover_btn != btn)) {
        hover_btn_win->hover_btn = 0;
        mark_dirty(title_rect(hover_btn_win));
        hover_btn_win = NULL;
    }
    if (w && btn && w->hover_btn != btn) {
        w->hover_btn = btn;
        hover_btn_win = w;
        mark_dirty(title_rect(w));
    }
}

static rect_t snap_zone_rect(int zone) {
    rect_t wa = wm_workarea();
    if (zone == 1) return R(wa.x, wa.y, wa.w / 2, wa.h);
    if (zone == 2) return R(wa.x + wa.w / 2, wa.y, wa.w - wa.w / 2, wa.h);
    return wa;
}

static void do_move(void) {
    window_t *w = drag_win;
    int dx = mouse_x - drag_mx, dy = mouse_y - drag_my;
    if (!drag_moved && iabs(dx) < 4 && iabs(dy) < 4) return;
    drag_moved = true;

    if (w->state != WS_NORMAL) {
        /* arrastar uma janela maximizada/encaixada: volta ao tamanho original,
         * mantendo o cursor na mesma proporção da barra de título */
        rect_t r = w->restore;
        float rel = (float)(drag_mx - w->frame.x) / (float)w->frame.w;
        r.x = mouse_x - (int)(rel * r.w);
        r.y = mouse_y - (drag_my - w->frame.y);
        w->state = WS_NORMAL;
        set_frame(w, r);
        drag_frame = r;
        drag_mx = mouse_x;
        drag_my = mouse_y;
        dx = dy = 0;
    }

    rect_t f = drag_frame;
    f.x += dx;
    f.y += dy;
    rect_t wa = wm_workarea();
    if (f.y < MENUBAR_H) f.y = MENUBAR_H;
    if (f.y > wa.y + wa.h - TITLE_H) f.y = wa.y + wa.h - TITLE_H;
    set_frame(w, f);

    int zone = 0;
    if (w->flags & WF_RESIZABLE) {
        if (mouse_x <= 1) zone = 1;
        else if (mouse_x >= scr_w - 2) zone = 2;
        else if (mouse_y <= 1 && can_max(w)) zone = 3;
    }
    if (zone != snap_zone) {
        if (snap_zone) mark_dirty(snap_rect);
        snap_zone = zone;
        if (zone) {
            snap_rect = snap_zone_rect(zone);
            mark_dirty(snap_rect);
        }
    }
}

static void do_resize(void) {
    window_t *w = drag_win;
    int dx = mouse_x - drag_mx, dy = mouse_y - drag_my;
    rect_t f = drag_frame;
    int minw = imax(w->min_w + 2 * BORDER, 200);
    int minh = w->min_h + TITLE_H + 2 * BORDER;
    if (drag_edges & EDGE_L) {
        int nw = imax(minw, f.w - dx);
        f.x += f.w - nw;
        f.w = nw;
    }
    if (drag_edges & EDGE_R) f.w = imax(minw, f.w + dx);
    if (drag_edges & EDGE_T) {
        int nh = imax(minh, f.h - dy);
        f.y += f.h - nh;
        f.h = nh;
    }
    if (drag_edges & EDGE_B) f.h = imax(minh, f.h + dy);
    w->state = WS_NORMAL;
    set_frame(w, f);
}

static void on_motion(void) {
    if (snip) {
        if (snip_drag) {
            rect_t old = snip_sel;
            int x0 = imin(snip_x0, mouse_x), y0 = imin(snip_y0, mouse_y);
            snip_sel = R(x0, y0, iabs(mouse_x - snip_x0), iabs(mouse_y - snip_y0));
            mark_dirty(snip_dirty(rect_union(old, snip_sel)));
        }
        return;
    }
    if (taskview) {
        int ht0, e0;
        window_t *over = hit_test(mouse_x, mouse_y, &ht0, &e0);
        if (!over || layer(over) != 2 || tv_dragging) {
            if (cursor_shape != CURSOR_ARROW) set_cursor(CURSOR_ARROW);
            tv_motion();
            return;
        }
    }
    if (fly_open) {
        int fh = fly_hit(mouse_x, mouse_y);
        if (fh != fly_hover) { fly_hover = fh; mark_dirty(fly_rect); }
        rect_t keep = rect_union(R(fly_rect.x - 12, fly_rect.y - 12, fly_rect.w + 24, fly_rect.h + 24),
                                 button_rect(fly_win, HT_MAX));
        if (rect_has(keep, mouse_x, mouse_y)) return;
        close_flyout();
    }
    if (drag == DRAG_MOVE) { do_move(); return; }
    if (drag == DRAG_RESIZE) { do_resize(); return; }

    int ht, edges;
    window_t *w = hit_test(mouse_x, mouse_y, &ht, &edges);

    if (drag == DRAG_BUTTON) {
        int hb = (w == drag_win && ht == drag_win->press_btn) ? ht : 0;
        if (drag_win->hover_btn != hb) {
            drag_win->hover_btn = hb;
            mark_dirty(title_rect(drag_win));
        }
        return;
    }

    set_hover_button(w, (ht == HT_MIN || ht == HT_MAX || ht == HT_CLOSE) ? ht : 0);
    /* parar no botão maximizar abre o painel de Snap Layouts depois de um instante */
    if (ht == HT_MAX && can_max(w) && !fly_open) {
        if (fly_win != w || !fly_due) { fly_win = w; fly_due = timer_ms() + 450; }
    } else if (!fly_open) {
        fly_due = 0;
        fly_win = NULL;
    }

    int cur = CURSOR_ARROW;
    if (capture_win) cur = capture_win->cursor;
    else if (ht == HT_RESIZE) cur = edges_cursor(edges);
    else if (ht == HT_CLIENT) cur = w->cursor;
    set_cursor(cur);

    window_t *target = capture_win ? capture_win : (ht == HT_CLIENT ? w : NULL);
    if (hover_win && hover_win != target) post(hover_win, (event_t){ .type = EV_MOUSE_LEAVE });
    hover_win = target;
    if (target) {
        rect_t c = client_rect(target);
        post(target, (event_t){ .type = EV_MOUSE_MOVE, .x = mouse_x - c.x, .y = mouse_y - c.y,
                                .button = mouse_btn });
    }
}

static void finish_drag(void) {
    window_t *w = drag_win;
    if (drag == DRAG_MOVE && snap_zone) {
        mark_dirty(snap_rect);
        if (snap_zone == 3) {
            w->restore = drag_frame;
            w->state = WS_NORMAL;
            maximize(w);
        } else {
            w->state = WS_NORMAL;
            w->restore = w->frame;
            snap(w, snap_zone == 1 ? WS_SNAP_LEFT : WS_SNAP_RIGHT);
        }
        snap_zone = 0;
    }
    if (drag == DRAG_BUTTON) {
        int btn = w->press_btn;
        bool still = w->hover_btn == btn;
        w->press_btn = 0;
        mark_dirty(title_rect(w));
        drag = DRAG_NONE;
        drag_win = NULL;
        if (still) {
            if (btn == HT_MIN) minimize(w);
            else if (btn == HT_MAX) toggle_max(w);
            else if (btn == HT_CLOSE) post(w, (event_t){ .type = EV_CLOSE });
        }
    }
    drag = DRAG_NONE;
    drag_win = NULL;
    on_motion();
}

static void on_button(int btn, bool pressed) {
    if (snip) {
        if (btn != 1) return;
        if (pressed) {
            snip_drag = true;
            snip_x0 = mouse_x;
            snip_y0 = mouse_y;
            snip_sel = R(mouse_x, mouse_y, 0, 0);
        } else if (snip_drag) {
            bool region = snip_sel.w >= 4 && snip_sel.h >= 4;
            end_snip(true, !region);
        }
        return;
    }
    if (fly_open && pressed && btn == 1) {
        int fh = fly_hit(mouse_x, mouse_y);
        window_t *fw = fly_win;
        if (fh >= 0) {
            close_flyout();
            snap_to(fw, zone_rect(fh / 8, fh % 8));
            activate(fw);
            return;
        }
        if (!rect_has(fly_rect, mouse_x, mouse_y)) close_flyout();
        else return;
    }
    if (taskview) {
        int ht0, e0;
        window_t *over = hit_test(mouse_x, mouse_y, &ht0, &e0);
        if (!over || layer(over) != 2 || tv_dragging || (!pressed && tv_press >= 0)) {
            if (btn == 1) tv_click(pressed);
            return;
        }
    }
    if (!pressed) {
        if (btn == 1 && drag != DRAG_NONE) {
            finish_drag();
            return;
        }
        if (capture_win) {
            rect_t c = client_rect(capture_win);
            post(capture_win, (event_t){ .type = EV_MOUSE_UP, .button = (uint16_t)btn,
                                         .x = mouse_x - c.x, .y = mouse_y - c.y });
            if (!mouse_btn) {
                capture_win = NULL;
                on_motion();
            }
        }
        return;
    }

    int ht, edges;
    window_t *w = hit_test(mouse_x, mouse_y, &ht, &edges);

    /* clique fora de um popup (ex.: menu Iniciar) fecha o popup */
    for (int i = 0; i < nwin && !lock_win; i++) {
        window_t *p = zorder[i];
        if ((p->flags & WF_POPUP) && shown(p) && p != w && (!w || w->owner != p->owner))
            post(p, (event_t){ .type = EV_POPUP_CLOSE });
    }
    if (!w || drag != DRAG_NONE) return;

    uint64_t now = timer_ms();
    if (w == last_click_win && btn == last_click_btn && now - last_click_ms < 450 &&
        iabs(mouse_x - last_click_x) < 5 && iabs(mouse_y - last_click_y) < 5)
        click_count++;
    else
        click_count = 1;
    last_click_ms = now;
    last_click_win = w;
    last_click_btn = btn;
    last_click_x = mouse_x;
    last_click_y = mouse_y;

    if (!(w->flags & WF_NOFOCUS)) activate(w);

    if (btn == 1) {
        switch (ht) {
        case HT_CAPTION:
            if (click_count == 2) {
                if (can_max(w)) toggle_max(w);
                click_count = 0;
                return;
            }
            drag = DRAG_MOVE;
            drag_win = w;
            drag_mx = mouse_x;
            drag_my = mouse_y;
            drag_frame = w->frame;
            drag_moved = false;
            return;
        case HT_MIN: case HT_MAX: case HT_CLOSE:
            drag = DRAG_BUTTON;
            drag_win = w;
            w->press_btn = ht;
            w->hover_btn = ht;
            mark_dirty(title_rect(w));
            return;
        case HT_RESIZE:
            drag = DRAG_RESIZE;
            drag_win = w;
            drag_edges = edges;
            drag_mx = mouse_x;
            drag_my = mouse_y;
            drag_frame = w->frame;
            return;
        }
    }
    if (ht == HT_CLIENT) {
        capture_win = w;
        rect_t c = client_rect(w);
        post(w, (event_t){ .type = EV_MOUSE_DOWN, .button = (uint16_t)btn, .x = mouse_x - c.x,
                           .y = mouse_y - c.y, .clicks = (uint32_t)click_count });
    }
}

static void on_wheel(int delta) {
    int ht, edges;
    window_t *w = capture_win;
    if (!w) {
        w = hit_test(mouse_x, mouse_y, &ht, &edges);
        if (!w || ht != HT_CLIENT) return;
    }
    rect_t c = client_rect(w);
    post(w, (event_t){ .type = EV_MOUSE_WHEEL, .wheel = delta, .x = mouse_x - c.x, .y = mouse_y - c.y });
}

static void handle_mouse(input_event_t *ie) {
    int nx = mouse_x, ny = mouse_y;
    if (ie->absolute) {
        nx = (int)((int64_t)ie->ax * scr_w / 65536);
        ny = (int)((int64_t)ie->ay * scr_h / 65536);
    } else {
        nx += ie->dx;
        ny += ie->dy;
    }
    nx = imax(0, imin(scr_w - 1, nx));
    ny = imax(0, imin(scr_h - 1, ny));
    if (nx != mouse_x || ny != mouse_y) {
        mark_dirty(cursor_rect());
        mouse_x = nx;
        mouse_y = ny;
        mark_dirty(cursor_rect());
        on_motion();
    }
    uint8_t changed = ie->buttons ^ mouse_btn;
    for (int b = 0; b < 3; b++) {
        if (!(changed & (1 << b))) continue;
        mouse_btn ^= (1 << b);
        on_button(b + 1, ie->buttons & (1 << b));
    }
    if (ie->wheel) on_wheel(ie->wheel);
}

/* ------------------------------------------------------------------ */
/* Teclado e atalhos globais                                            */
/* ------------------------------------------------------------------ */
static rect_t alttab_outer(void) {
    rect_t r = alttab_rect;
    return R(r.x - SHADOW, r.y - SHADOW, r.w + 2 * SHADOW, r.h + 2 * SHADOW + 2);
}

static void finish_alttab(bool apply) {
    mark_dirty(alttab_outer());
    alttab = false;
    if (apply) {
        window_t *w = find_by_id(alttab_ids[alttab_sel]);
        if (w) activate(w);
    }
}

static void do_alttab(bool back) {
    if (!alttab) {
        alttab_n = 0;
        for (int i = nwin - 1; i >= 0; i--) {
            window_t *w = zorder[i];
            if (is_deco(w) && w->visible && !(w->flags & WF_NOTASKBAR) && w->desk == cur_desk)
                alttab_ids[alttab_n++] = w->id;
        }
        if (!alttab_n) return;
        alttab = true;
        alttab_sel = back ? alttab_n - 1 : (alttab_n > 1 ? 1 : 0);
    } else {
        alttab_sel = (alttab_sel + (back ? alttab_n - 1 : 1)) % alttab_n;
    }
    alttab_rect = alttab_layout();
    mark_dirty(alttab_outer());
}

static void handle_key(input_event_t *ie) {
    uint32_t key = ie->key, mods = ie->mods, ch = ie->ch;

    /* tela de bloqueio: nenhum atalho funciona, tudo vai para ela */
    if (lock_win) {
        post(lock_win, (event_t){ .type = ie->pressed ? EV_KEY_DOWN : EV_KEY_UP, .key = key, .ch = ch, .mods = mods });
        return;
    }
    if (snip) {
        if (ie->pressed && key == KEY_ESC) end_snip(false, false);
        else if (ie->pressed && key == KEY_ENTER) end_snip(true, !(snip_sel.w >= 4 && snip_sel.h >= 4));
        return;
    }
    if (ie->pressed && key == KEY_PRINT) {
        capture_full();
        return;
    }
    if (taskview && ie->pressed && key == KEY_ESC) {
        open_taskview(false);
        return;
    }
    if (fly_open && ie->pressed && key == KEY_ESC) {
        close_flyout();
        return;
    }

    if (key == KEY_SUPER) {
        if (ie->pressed && !super_down) {
            super_down = true;
            super_used = false;
        } else if (!ie->pressed) {
            if (super_down && !super_used) shell_cmd(SHELL_START);
            super_down = false;
        }
        return;
    }
    if (key == KEY_LALT && !ie->pressed && alttab) {
        finish_alttab(true);
        return;
    }

    if (ie->pressed) {
        if (super_down) super_used = true;
        if (alttab && key == KEY_ESC) { finish_alttab(false); return; }
        if (key == KEY_TAB && (mods & MOD_ALT) && !taskview) { do_alttab(mods & MOD_SHIFT); return; }
        if (key == KEY_F4 && (mods & MOD_ALT)) {
            if (focused && is_deco(focused)) post(focused, (event_t){ .type = EV_CLOSE });
            return;
        }
        if (key == KEY_ESC && (mods & MOD_CTRL) && (mods & MOD_SHIFT)) { shell_cmd(SHELL_TASKMGR); return; }
        if (mods & MOD_SUPER) {
            uint32_t c = tolower((int)ch);
            if (key == KEY_TAB) { open_taskview(!taskview); return; }
            if ((mods & MOD_CTRL) && key == KEY_LEFT) { switch_desk(cur_desk - 1); return; }
            if ((mods & MOD_CTRL) && key == KEY_RIGHT) { switch_desk(cur_desk + 1); return; }
            if ((mods & MOD_CTRL) && c == 'd') { new_desk(); return; }
            if ((mods & MOD_CTRL) && key == KEY_F4) { close_desk(cur_desk); return; }
            if ((mods & MOD_SHIFT) && c == 's') { start_snip(); return; }
            if (c == 'v') { shell_cmd(SHELL_CLIPBOARD); return; }
            if (c == ' ' || (c == 's' && !(mods & MOD_SHIFT))) { shell_cmd(SHELL_SEARCH); return; }
            if (c == 'n' || c == 'a') { shell_cmd(SHELL_NOTIFICATIONS); return; }
            if (c == 'z') { if (focused && is_deco(focused)) open_flyout(focused); return; }
            if (taskview) return;
            if (c == 'd') { show_desktop_locked(); return; }
            if (c == 'e') { shell_cmd(SHELL_EXPLORER); return; }
            if (c == 't') { shell_cmd(SHELL_RUN_TERMINAL); return; }
            if (c == 'l') { shell_cmd(SHELL_LOCK); return; }
            window_t *w = (focused && is_deco(focused)) ? focused : NULL;
            rect_t wa = wm_workarea();
            int hw = wa.w / 2, hh = wa.h / 2;
            /* metade -> quarto: Super+Seta para cima/baixo depois de encaixar */
            if (w && key == KEY_UP && w->state == WS_SNAP_LEFT) snap_to(w, R(wa.x, wa.y, hw, hh));
            else if (w && key == KEY_UP && w->state == WS_SNAP_RIGHT) snap_to(w, R(wa.x + hw, wa.y, wa.w - hw, hh));
            else if (w && key == KEY_DOWN && w->state == WS_SNAP_LEFT) snap_to(w, R(wa.x, wa.y + hh, hw, wa.h - hh));
            else if (w && key == KEY_DOWN && w->state == WS_SNAP_RIGHT) snap_to(w, R(wa.x + hw, wa.y + hh, wa.w - hw, wa.h - hh));
            else if (w && key == KEY_UP) maximize(w);
            else if (w && key == KEY_DOWN) {
                if (w->state != WS_NORMAL) restore_window(w);
                else minimize(w);
            } else if (w && key == KEY_LEFT) {
                if (w->state == WS_SNAP_RIGHT) restore_window(w);
                else snap(w, WS_SNAP_LEFT);
            } else if (w && key == KEY_RIGHT) {
                if (w->state == WS_SNAP_LEFT) restore_window(w);
                else snap(w, WS_SNAP_RIGHT);
            }
            return;
        }
    }
    if (alttab || taskview) return;
    if (focused)
        post(focused, (event_t){ .type = ie->pressed ? EV_KEY_DOWN : EV_KEY_UP, .key = key, .ch = ch, .mods = mods });
}

/* ------------------------------------------------------------------ */
/* Laço do compositor                                                   */
/* ------------------------------------------------------------------ */
static void gui_main(void *arg) {
    input_set_consumer(gui_task);
    mark_dirty(R(0, 0, scr_w, scr_h));
    uint64_t last_frame = 0;
    for (;;) {
        input_event_t ie;
        mutex_lock(&lock);
        while (input_pop(&ie)) {
            if (ie.type == INPUT_KEY) handle_key(&ie);
            else handle_mouse(&ie);
        }
        mutex_unlock(&lock);

        if (ndirty) {
            uint64_t now = timer_ms();
            if (now - last_frame < FRAME_MS) {
                task_sleep(FRAME_MS - (now - last_frame));
                continue;
            }
            compose_all();
            last_frame = timer_ms();
            continue;
        }
        /* temporizadores do próprio compositor */
        uint64_t now = timer_ms();
        mutex_lock(&lock);
        if (fly_due && now >= fly_due) {
            int ht, edges;
            window_t *w = hit_test(mouse_x, mouse_y, &ht, &edges);
            if (w == fly_win && ht == HT_MAX && drag == DRAG_NONE) open_flyout(w);
            else { fly_due = 0; fly_win = NULL; }
        }
        if (desk_label_until && now >= desk_label_until) {
            desk_label_until = 0;
            mark_dirty(desk_label_rect());
        }
        uint64_t deadline = 0;
        if (fly_due) deadline = fly_due;
        if (desk_label_until && (!deadline || desk_label_until < deadline)) deadline = desk_label_until;
        mutex_unlock(&lock);
        if (ndirty) continue;

        /* nada a fazer: dorme até chegar entrada, alguém sujar a tela ou o próximo prazo */
        uint64_t f = irq_save();
        if (!ndirty && !input_pending()) {
            if (deadline) {
                gui_task->wake_at = deadline;
                gui_task->state = TASK_SLEEPING;
            } else {
                gui_task->state = TASK_BLOCKED;
            }
            task_yield();
        }
        irq_restore(f);
    }
}

/* ------------------------------------------------------------------ */
/* API para aplicativos                                                 */
/* ------------------------------------------------------------------ */
window_t *win_create(const char *title, int icon, int x, int y, int cw, int ch, uint32_t flags) {
    window_t *w = kcalloc(1, sizeof(window_t));
    if (!w) return NULL;
    w->owner = task_current();
    strlcpy(w->title, title, sizeof(w->title));
    w->icon = icon;
    w->flags = flags;
    w->min_w = 160;
    w->min_h = 80;
    int fw = cw, fh = ch;
    if (flags & WF_DECORATED) {
        fw += 2 * BORDER;
        fh += 2 * BORDER + TITLE_H;
    }
    rect_t wa = wm_workarea();
    if (flags & WF_CENTER) {
        x = wa.x + (wa.w - fw) / 2;
        y = wa.y + (wa.h - fh) / 2;
    } else if (x == WIN_AUTO) {
        static int cascade;
        x = wa.x + 70 + (cascade % 8) * 32;
        y = wa.y + 40 + (cascade % 8) * 32;
        cascade++;
        if (x + fw > wa.x + wa.w) x = imax(wa.x, wa.x + wa.w - fw);
        if (y + fh > wa.y + wa.h) y = imax(wa.y, wa.y + wa.h - fh);
    }
    w->frame = R(x, y, fw, fh);
    w->canvas = canvas_create(cw, ch);
    if (!w->canvas) {
        kfree(w);
        return NULL;
    }
    gfx_clear(w->canvas, (flags & WF_ALPHA) ? 0 : theme.window_bg);
    w->visible = !(flags & WF_HIDDEN);

    mutex_lock(&lock);
    if (nwin >= MAX_WIN) {
        mutex_unlock(&lock);
        canvas_destroy(w->canvas);
        kfree(w);
        return NULL;
    }
    w->id = next_win_id++;
    w->desk = cur_desk;
    z_insert_top(w);
    if (w->visible && !(flags & (WF_NOFOCUS | WF_DESKTOP))) set_focus(w);
    mark_dirty(outer_rect(w));
    mutex_unlock(&lock);
    notify_shell();
    return w;
}

void win_destroy(window_t *w) {
    if (!w) return;
    mutex_lock(&lock);
    if (!alive(w)) {
        mutex_unlock(&lock);
        return;
    }
    z_remove(w);
    mark_dirty(outer_rect(w));
    if (hover_win == w) hover_win = NULL;
    if (capture_win == w) capture_win = NULL;
    if (hover_btn_win == w) hover_btn_win = NULL;
    if (last_click_win == w) last_click_win = NULL;
    if (drag_win == w) {
        drag = DRAG_NONE;
        drag_win = NULL;
        if (snap_zone) mark_dirty(snap_rect);
        snap_zone = 0;
    }
    if (fly_win == w) close_flyout();
    if (lock_win == w) {
        lock_win = NULL;
        mark_dirty(R(0, 0, scr_w, scr_h));
    }
    if (focused == w) {
        focused = NULL;
        focus_next(w);
    }
    if (taskview) mark_dirty(R(0, 0, scr_w, scr_h));
    mutex_unlock(&lock);
    canvas_destroy(w->canvas);
    kfree(w);
    notify_shell();
}

canvas_t *win_canvas(window_t *w) { return w->canvas; }

void win_invalidate(window_t *w) {
    if (shown(w)) mark_dirty(client_rect(w));
}

void win_invalidate_rect(window_t *w, rect_t r) {
    if (!shown(w)) return;
    rect_t c = client_rect(w);
    mark_dirty(rect_intersect(R(c.x + r.x, c.y + r.y, r.w, r.h), c));
}

void win_set_title(window_t *w, const char *title) {
    mutex_lock(&lock);
    strlcpy(w->title, title, sizeof(w->title));
    mark_dirty(title_rect(w));
    mutex_unlock(&lock);
    notify_shell();
}

void win_show(window_t *w, bool show) {
    mutex_lock(&lock);
    if (show) {
        w->visible = true;
        w->minimized = false;
        if (!(w->flags & (WF_NOFOCUS | WF_DESKTOP))) {
            raise_window(w);
            set_focus(w);
        }
    } else {
        w->visible = false;
        if (capture_win == w) capture_win = NULL;
        if (hover_win == w) hover_win = NULL;
        if (focused == w) focus_next(w);
    }
    mark_dirty(outer_rect(w));
    mutex_unlock(&lock);
    notify_shell();
}

void win_move(window_t *w, int x, int y) {
    mutex_lock(&lock);
    set_frame(w, R(x, y, w->frame.w, w->frame.h));
    mutex_unlock(&lock);
}

/* Troca o canvas pelo tamanho atual da área cliente (só na tarefa dona). */
static void apply_resize(window_t *w) {
    rect_t c = client_rect(w);
    w->resize_pending = false;
    if (c.w == w->canvas->w && c.h == w->canvas->h) return;
    canvas_t *n = canvas_create(c.w, c.h);
    if (!n) return;
    gfx_clear(n, (w->flags & WF_ALPHA) ? 0 : theme.window_bg);
    gfx_blit(n, 0, 0, w->canvas, 0, 0, imin(c.w, w->canvas->w), imin(c.h, w->canvas->h));
    canvas_destroy(w->canvas);
    w->canvas = n;
}

void win_resize(window_t *w, int cw, int ch) {
    mutex_lock(&lock);
    int fw = cw, fh = ch;
    if (is_deco(w)) {
        fw += 2 * BORDER;
        fh += 2 * BORDER + TITLE_H;
    }
    w->state = WS_NORMAL;
    set_frame(w, R(w->frame.x, w->frame.y, fw, fh));
    apply_resize(w);
    mutex_unlock(&lock);
}

void win_set_min_size(window_t *w, int cw, int ch) {
    w->min_w = cw;
    w->min_h = ch;
}

void win_set_cursor(window_t *w, int cursor) {
    if (w->cursor == cursor) return;
    w->cursor = cursor;
    mutex_lock(&lock);
    on_motion();
    mutex_unlock(&lock);
}

void win_focus(window_t *w) {
    mutex_lock(&lock);
    activate(w);
    mutex_unlock(&lock);
}

bool win_is_focused(window_t *w) { return focused == w; }

rect_t win_client_rect(window_t *w) { return client_rect(w); }

/* Eventos de janelas já destruídas são descartados. */
static bool filter_event(event_t *e) {
    if (!e->win) return true;
    mutex_lock(&lock);
    bool ok = alive(e->win);
    if (ok && e->type == EV_RESIZE) {
        apply_resize(e->win);
        rect_t c = client_rect(e->win);
        e->x = c.w;
        e->y = c.h;
    }
    mutex_unlock(&lock);
    return ok;
}

bool gui_wait(event_t *e) {
    for (;;) {
        ev_wait(e);
        if (filter_event(e)) return true;
    }
}

bool gui_poll(event_t *e) {
    while (ev_poll(e))
        if (filter_event(e)) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* API para o shell                                                     */
/* ------------------------------------------------------------------ */
int wm_list(win_info_t *out, int max) {
    mutex_lock(&lock);
    window_t *tmp[MAX_WIN];
    int n = 0;
    for (int i = 0; i < nwin; i++) {
        window_t *w = zorder[i];
        if (is_deco(w) && w->visible && !(w->flags & WF_NOTASKBAR) && w->desk == cur_desk) tmp[n++] = w;
    }
    /* ordem de criação, para os botões não trocarem de lugar */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && tmp[j - 1]->id > tmp[j]->id; j--) {
            window_t *t = tmp[j]; tmp[j] = tmp[j - 1]; tmp[j - 1] = t;
        }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        out[i].id = tmp[i]->id;
        strlcpy(out[i].title, tmp[i]->title, sizeof(out[i].title));
        out[i].icon = tmp[i]->icon;
        out[i].minimized = tmp[i]->minimized;
        out[i].focused = tmp[i] == focused;
    }
    mutex_unlock(&lock);
    return n;
}

void wm_activate(int id) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) activate(w);
    mutex_unlock(&lock);
}

void wm_minimize(int id) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) minimize(w);
    mutex_unlock(&lock);
}

void wm_toggle(int id) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) {
        if (w == focused && !w->minimized) minimize(w);
        else activate(w);
    }
    mutex_unlock(&lock);
}

void wm_close(int id) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) post(w, (event_t){ .type = EV_CLOSE });
    mutex_unlock(&lock);
}

void wm_show_desktop(void) {
    mutex_lock(&lock);
    show_desktop_locked();
    mutex_unlock(&lock);
}

void wm_register_shell(task_t *t) { shell_task = t; }

void wm_shell_command(int cmd) { shell_cmd(cmd); }

void wm_lock(window_t *w) {
    mutex_lock(&lock);
    lock_win = w;
    if (w) {
        open_taskview(false);
        close_flyout();
        alttab = false;
        drag = DRAG_NONE;
        drag_win = capture_win = hover_win = NULL;
        w->visible = true;
        raise_window(w);
        focused = NULL;
        set_focus(w);
    } else {
        focused = NULL;
        focus_next(NULL);
    }
    mark_dirty(R(0, 0, scr_w, scr_h));
    mutex_unlock(&lock);
}

bool wm_is_locked(void) { return lock_win != NULL; }

void wm_task_view(void) {
    mutex_lock(&lock);
    if (!lock_win) open_taskview(!taskview);
    mutex_unlock(&lock);
}

int wm_desktop_count(void) { return ndesks; }
int wm_current_desktop(void) { return cur_desk; }

bool wm_thumbnail(int id, canvas_t *dst, rect_t area) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) {
        canvas_t *cv = w->canvas;
        int tw = area.w, th = (int)((int64_t)cv->h * area.w / cv->w);
        if (th > area.h) { th = area.h; tw = (int)((int64_t)cv->w * area.h / cv->h); }
        rect_t r = R(area.x + (area.w - tw) / 2, area.y + (area.h - th) / 2, tw, th);
        gfx_blit_scaled(dst, r, cv, R(0, 0, cv->w, cv->h), true);
        gfx_rect(dst, r.x, r.y, r.w, r.h, theme.border);
    }
    mutex_unlock(&lock);
    return w != NULL;
}

void win_set_menus(window_t *w, const char *titles) {
    mutex_lock(&lock);
    strlcpy(w->menus, titles ? titles : "", sizeof(w->menus));
    mutex_unlock(&lock);
    notify_shell();
}

bool wm_get_menus(int id, char *out, int size) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) strlcpy(out, w->menus, size);
    mutex_unlock(&lock);
    return w && out[0];
}

void wm_send_menu(int id, int index, int x, int y) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w) post(w, (event_t){ .type = EV_MENU, .key = (uint32_t)index, .x = x, .y = y });
    mutex_unlock(&lock);
}

void wm_zoom(int id) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w && can_max(w)) toggle_max(w);
    mutex_unlock(&lock);
}

void wm_snap_window(int id, int side) {
    mutex_lock(&lock);
    window_t *w = find_by_id(id);
    if (w && (w->flags & WF_RESIZABLE)) {
        if (w->state != WS_NORMAL && w->state != WS_SNAP_LEFT && w->state != WS_SNAP_RIGHT) restore_window(w);
        snap(w, side < 0 ? WS_SNAP_LEFT : WS_SNAP_RIGHT);
    }
    mutex_unlock(&lock);
}

void wm_capture(bool region) {
    mutex_lock(&lock);
    if (!lock_win) {
        if (region) start_snip();
        else capture_full();
    }
    mutex_unlock(&lock);
}

void wm_paste_to_focused(void) {
    mutex_lock(&lock);
    if (focused && is_deco(focused))
        post(focused, (event_t){ .type = EV_KEY_DOWN, .key = KEY_CHAR, .ch = 'v', .mods = MOD_CTRL });
    mutex_unlock(&lock);
}

void wm_broadcast(const event_t *e) {
    mutex_lock(&lock);
    task_t *sent[MAX_WIN];
    int n = 0;
    for (int i = 0; i < nwin; i++) {
        task_t *t = zorder[i]->owner;
        bool dup = false;
        for (int j = 0; j < n; j++) dup |= sent[j] == t;
        if (!dup) {
            sent[n++] = t;
            ev_post(t, e);
        }
    }
    mutex_unlock(&lock);
}

void wm_redraw_all(void) { mark_dirty(R(0, 0, scr_w, scr_h)); }

void wm_apply_theme(bool dark, int accent, int wallpaper) {
    theme_apply(dark, accent, wallpaper);
    event_t e = { .type = EV_THEME };
    wm_broadcast(&e);
    wm_redraw_all();
}

/* Quando uma tarefa termina, as janelas dela são fechadas. */
static void wm_task_exit(task_t *t) {
    for (;;) {
        mutex_lock(&lock);
        window_t *victim = NULL;
        for (int i = 0; i < nwin && !victim; i++)
            if (zorder[i]->owner == t) victim = zorder[i];
        mutex_unlock(&lock);
        if (!victim) break;
        win_destroy(victim);
    }
}

/* Tela de erro fatal: desenhada direto no framebuffer. */
static void panic_screen(const char *title, const char *details) {
    canvas_t c;
    canvas_wrap(&c, fb, scr_w, scr_h, fb_stride);
    gfx_vgradient(&c, 0, 0, scr_w, scr_h, HEX(0x3A1020), HEX(0x14060C));
    int x = 80, y = 90;
    font_draw(&c, x, y, ":(", HEX(0xFFFFFF), FONT_2X | FONT_BOLD);
    y += 60;
    font_draw(&c, x, y, title, HEX(0xFFFFFF), FONT_2X);
    y += 50;
    font_draw(&c, x, y, "Anote as informações abaixo e reinicie o computador.", HEX(0xE8C8D0), 0);
    y += 36;
    const char *line = details;
    while (*line && y < scr_h - 40) {
        const char *end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        font_draw_n(&c, x, y, line, len, HEX(0xF5E6EA), FONT_MONO);
        y += 18;
        if (!end) break;
        line = end + 1;
    }
}

void wm_init(void) {
    if (bootinfo.fb_bpp != 32) panic("Somente framebuffer de 32 bits é suportado");
    fb = phys_to_virt(bootinfo.fb_phys);
    fb_stride = bootinfo.fb_pitch / 4;
    scr_w = bootinfo.fb_width;
    scr_h = bootinfo.fb_height;
    scr = canvas_create(scr_w, scr_h);
    if (!scr) panic("Sem memória para o back buffer");

    font_init();
    build_cursors();
    for (int j = 0; j < RADIUS; j++) {
        float dy = RADIUS - (j + 0.5f);
        float ins = RADIUS - gfx_sqrt(RADIUS * RADIUS - dy * dy) - 0.5f;
        corner_inset[j] = ins <= 0 ? 0 : (int)(ins + 0.999f);
    }
    mouse_x = scr_w / 2;
    mouse_y = scr_h / 2;

    panic_set_screen(panic_screen);
    sched_set_exit_hook(wm_task_exit);
    gui_task = task_create("compositor", gui_main, NULL);
    gui_task->system = true;
    kprintf("gui: compositor %dx%d iniciado\n", scr_w, scr_h);
}
