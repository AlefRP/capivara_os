/*
 * Shell (interface no estilo das áreas de trabalho do macOS, com recursos do
 * Windows):
 *
 *   - barra de menus no topo: menu do sistema (capivara), nome do app com foco,
 *     menus do app (publicados por ele com win_set_menus), ícones de status,
 *     Central de Controle e relógio;
 *   - Dock embaixo, com ampliação dos ícones sob o mouse e rótulos;
 *   - Launchpad (todos os apps em tela cheia) e Pesquisa (Super+Espaço);
 *   - banners de notificação e Central de Notificações com calendário;
 *   - tela de bloqueio; área de trabalho com ícones à direita.
 *
 * É um aplicativo comum (uma tarefa) que cria janelas especiais. O compositor
 * avisa quando a lista de janelas muda (EV_WINLIST) e repassa atalhos globais
 * (EV_SHELL).
 */
#include "apps/apps.h"
#include "version.h"
#include "apps/session.h"
#include "apps/notify.h"
#include "apps/trash.h"
#include "gui/ui.h"
#include "gui/wallpaper.h"
#include "fs/vfs.h"
#include "dev/rtc.h"
#include "dev/power.h"
#include "dev/keyboard.h"
#include "arch/x86_64/pit.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "lib/printf.h"

#define CENTER_W  344
#define CAL_H     330
#define CARD_H    74
#define TOAST_W   356
#define TOAST_H   84
#define CC_W      300
#define CC_H      316
#define SPOT_W    620

static window_t *desktop, *menubar, *dock, *launchpad, *spot, *center, *ccenter, *toast, *lockwin;
static canvas_t *wallpaper, *wall_blur;
static int scr_w, scr_h;
static bool dnd;                       /* "Não perturbe": sem banners */

static win_info_t wins[48];
static int nwins;

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

static void draw_menubar(void);
static void draw_dock(void);
static void lock_screen(void);

/* ================================================================== */
/* Papel de parede (gerado por código)                                 */
/* ================================================================== */
static void blob(canvas_t *c, float cx, float cy, float r, uint32_t color, float strength) {
    int x0 = imax(0, (int)(cx - r)), x1 = imin(c->w, (int)(cx + r));
    int y0 = imax(0, (int)(cy - r)), y1 = imin(c->h, (int)(cy + r));
    float r2 = r * r;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            float t = 1.0f - d2 / r2;
            int a = (int)(255 * strength * t * t);
            uint32_t *p = &c->px[y * c->stride + x];
            *p = color_blend(*p, ((uint32_t)a << 24) | (color & 0xFFFFFF));
        }
}

/* "Ondas": faixas onduladas e translúcidas sobre um degradê azul-violeta */
static void waves(canvas_t *c) {
    int w = c->w, h = c->h;
    gfx_vgradient(c, 0, 0, w, h, HEX(0x0E2A6B), HEX(0x3B1F6E));
    blob(c, w * 0.80f, h * 0.18f, 380, HEX(0x5AA9FF), 0.35f);
    static const uint32_t colors[4] = { 0x2D7FF0, 0x7B5CF0, 0xD05AC8, 0xF7A26B };
    float *curve = kmalloc(w * sizeof(float));
    for (int k = 0; k < 4; k++) {
        float base = h * (0.38f + 0.13f * k);
        for (int x = 0; x < w; x++)
            curve[x] = base + (46 - k * 6) * gfx_sin(x / (190.0f - k * 22) + k * 1.7f)
                            + 18 * gfx_sin(x / (63.0f + k * 9) + k);
        for (int x = 0; x < w; x++) {
            int y0 = (int)curve[x];
            if (y0 < 0) y0 = 0;
            for (int y = y0; y < h; y++) {
                float d = y - curve[x];
                float t = d < 1.5f ? d / 1.5f : 1.0f;            /* borda suave */
                float fade = 1.0f - (float)(y - y0) / (h - y0 + 1) * 0.55f;
                int a = (int)(150 * t * fade);
                uint32_t *p = &c->px[y * c->stride + x];
                *p = color_blend(*p, ((uint32_t)a << 24) | colors[k]);
            }
            /* brilho na crista da onda */
            if (y0 > 0 && y0 < h) {
                uint32_t *p = &c->px[y0 * c->stride + x];
                *p = color_blend(*p, ARGB(0x60, 0xFFFFFF));
            }
        }
    }
    kfree(curve);
}

static void generate_wallpaper(void) {
    canvas_t *c = wallpaper;
    int w = c->w, h = c->h;
    switch (theme.wallpaper) {
    case 0:
        wallpaper_capivara(c, true);
        break;
    case 1:
        waves(c);
        break;
    case 2: /* Ipê: céu azul com flores desfocadas amarelas e rosas */
        gfx_vgradient(c, 0, 0, w, h, HEX(0x0B2E59), HEX(0x177E89));
        blob(c, w * 0.78f, h * 0.30f, 420, HEX(0xF7C948), 0.55f);
        blob(c, w * 0.18f, h * 0.85f, 380, HEX(0xE86A92), 0.45f);
        blob(c, w * 0.55f, h * 0.70f, 260, HEX(0xFFD166), 0.30f);
        blob(c, w * 0.10f, h * 0.15f, 220, HEX(0x7FDBFF), 0.25f);
        for (int i = 0; i < 26; i++) {
            float fx = (float)((i * 397) % w), fy = (float)((i * 211 + 90) % h);
            float fr = 18 + (i * 7) % 36;
            blob(c, fx, fy, fr, i % 3 ? HEX(0xFFE08A) : HEX(0xFFB3C8), 0.35f);
        }
        break;
    case 3: { /* Aurora */
        gfx_vgradient(c, 0, 0, w, h, HEX(0x050B18), HEX(0x0E2A3B));
        float *ctr = kmalloc(w * sizeof(float)), *amp = kmalloc(w * sizeof(float));
        for (int x = 0; x < w; x++) {
            ctr[x] = h * 0.38f + 70 * gfx_sin(x / 170.0f) + 28 * gfx_sin(x / 61.0f + 1.3f);
            amp[x] = 0.55f + 0.45f * gfx_sin(x / 9.0f) * gfx_sin(x / 37.0f);
        }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float d = y - ctr[x];
                float t = d < 0 ? 1.0f + d / 60.0f : 1.0f - d / 220.0f;
                if (t <= 0) continue;
                int a = (int)(170 * t * t * amp[x]);
                uint32_t col = d < 0 ? HEX(0x7CF5C2) : color_mix(HEX(0x2FD3A0), HEX(0x6A5ACD), imin(255, (int)(d * 1.1f)));
                c->px[y * c->stride + x] = color_blend(c->px[y * c->stride + x], ((uint32_t)a << 24) | (col & 0xFFFFFF));
            }
        for (int i = 0; i < 140; i++) {
            int sx = (i * 7919) % w, sy = (i * 104729) % (h / 2);
            gfx_pixel(c, sx, sy, ARGB(0x90 + (i % 5) * 20, 0xFFFFFF));
        }
        kfree(ctr);
        kfree(amp);
        break;
    }
    case 4: { /* Entardecer */
        gfx_vgradient(c, 0, 0, w, h * 6 / 10, HEX(0x2E1A47), HEX(0xE0607E));
        gfx_vgradient(c, 0, h * 6 / 10, w, h - h * 6 / 10, HEX(0xF79A5B), HEX(0xFFD39B));
        blob(c, w * 0.62f, h * 0.60f, 170, HEX(0xFFF1C1), 0.95f);
        for (int layer = 0; layer < 3; layer++) {
            uint32_t col = layer == 0 ? HEX(0x6B3A6B) : layer == 1 ? HEX(0x47264F) : HEX(0x2A1633);
            float base = h * (0.68f + layer * 0.09f);
            for (int x = 0; x < w; x++) {
                int top = (int)(base - 40 * gfx_sin(x / (140.0f + layer * 50) + layer * 2.1f)
                                - 18 * gfx_sin(x / 47.0f + layer));
                gfx_set(c, x, top, 1, h - top, col);
            }
        }
        break;
    }
    default: /* cor sólida (derivada da cor de destaque) */
        gfx_vgradient(c, 0, 0, w, h, color_mix(theme.accent, HEX(0x000000), 60),
                      color_mix(theme.accent, HEX(0x000000), 150));
        break;
    }
    gfx_blur(wall_blur, wallpaper, 16);        /* versão desfocada (Launchpad) */
}

/* ================================================================== */
/* Área de trabalho (ícones à direita, de cima para baixo)             */
/* ================================================================== */
#define DESK_FOLDER DESKTOP_DIR
#define CELL_W 100
#define CELL_H 92

typedef struct {
    char label[64];
    int icon;
    char app[16];
    char path[VFS_PATH_MAX];
    bool file;
} desk_icon_t;

static desk_icon_t dicons[40];
static int ndicons, dsel = -1, dfolder_count = -1;

static void scan_desktop(void) {
    ndicons = 0;
    desk_icon_t *hd = &dicons[ndicons++];
    memset(hd, 0, sizeof(*hd));
    strlcpy(hd->label, "Capivara HD", sizeof(hd->label));
    hd->icon = ICON_DISK;
    strlcpy(hd->app, "explorer", sizeof(hd->app));
    strlcpy(hd->path, "/", sizeof(hd->path));

    vnode_t *dir = vfs_lookup(NULL, DESK_FOLDER);
    dfolder_count = dir ? vfs_child_count(dir) : 0;
    for (int i = 0; dir && i < dfolder_count && ndicons < 40; i++) {
        vnode_t *n = vfs_child_at(dir, i);
        if (!n) break;
        if (n->name[0] == '.') continue;
        desk_icon_t *d = &dicons[ndicons++];
        memset(d, 0, sizeof(*d));
        strlcpy(d->label, n->name, sizeof(d->label));
        char *ext = strrchr(d->label, '.');
        if (ext && !strcmp(ext, ".app")) *ext = 0;      /* atalho: só o nome do app */
        d->icon = app_icon_for(n->name, n->type == VFS_DIR);
        d->file = true;
        snprintf(d->path, sizeof(d->path), "%s/%s", DESK_FOLDER, n->name);
    }
    if (dsel >= ndicons) dsel = -1;
}

static rect_t dicon_rect(int i) {
    int rows = (scr_h - MENUBAR_H - DOCK_H - 16) / CELL_H;
    return R(scr_w - 10 - (i / rows + 1) * CELL_W, MENUBAR_H + 12 + (i % rows) * CELL_H, CELL_W - 4, CELL_H - 4);
}

/* rótulo de até 2 linhas, centralizado, com sombra para ler em qualquer fundo */
static void draw_label(canvas_t *c, int cx, int y, int maxw, const char *s, bool selected) {
    int len = (int)strlen(s);
    int fit = font_fit(s, maxw, 0);
    int line1 = fit;
    if (fit < len) {
        int brk = fit;
        while (brk > 0 && s[brk] != ' ') brk--;
        if (brk > 0) line1 = brk;
    }
    int w1 = font_width_n(s, line1, 0);
    if (selected) gfx_round_fill(c, cx - w1 / 2 - 5, y - 1, w1 + 10, line1 < len ? 36 : 18, 5, theme.accent);
    if (!selected) font_draw_n(c, cx - w1 / 2 + 1, y + 1, s, line1, ARGB(0xB0, 0x000000), 0);
    font_draw_n(c, cx - w1 / 2, y, s, line1, HEX(0xFFFFFF), 0);
    if (line1 < len) {
        const char *rest = s + line1;
        while (*rest == ' ') rest++;
        int w2 = imin(font_width(rest, 0), maxw);
        if (!selected) font_draw_ellipsis(c, cx - w2 / 2 + 1, y + 19, maxw, rest, ARGB(0xB0, 0x000000), 0);
        font_draw_ellipsis(c, cx - w2 / 2, y + 18, maxw, rest, HEX(0xFFFFFF), 0);
    }
}

static void draw_desktop(void) {
    canvas_t *c = win_canvas(desktop);
    gfx_blit(c, 0, 0, wallpaper, 0, 0, c->w, c->h);
    for (int i = 0; i < ndicons; i++) {
        rect_t r = dicon_rect(i);
        if (i == dsel) gfx_round_fill(c, r.x + (r.w - 52) / 2, r.y + 2, 52, 52, 8, ARGB(0x40, 0x000000));
        icon_draw(c, dicons[i].icon, r.x + (r.w - 48) / 2, r.y + 4, 48);
        draw_label(c, r.x + r.w / 2, r.y + 56, r.w - 6, dicons[i].label, i == dsel);
    }
    win_invalidate(desktop);
}

static void open_dicon(int i) {
    desk_icon_t *d = &dicons[i];
    if (d->app[0]) app_launch(d->app, d->path[0] ? d->path : NULL);
    else app_open_path(d->path);
}

static void desktop_new_item(bool folder) {
    char name[64];
    strlcpy(name, folder ? "Nova pasta" : "Novo documento.txt", sizeof(name));
    if (!ui_input(NULL, folder ? "Nova pasta" : "Novo arquivo de texto", "Nome:", name, sizeof(name))) return;
    char path[VFS_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", DESK_FOLDER, name);
    if (vfs_lookup(NULL, path)) {
        ui_message(NULL, "Área de Trabalho", "Já existe um item com esse nome.", "OK", NULL, NULL);
        return;
    }
    vfs_create(NULL, path, folder ? VFS_DIR : VFS_FILE);
    scan_desktop();
    draw_desktop();
}

static void desktop_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_DOWN: {
        int hit = -1;
        for (int i = 0; i < ndicons; i++)
            if (ui_hit(dicon_rect(i), e)) hit = i;
        if (e->button == 1) {
            dsel = hit;
            draw_desktop();
            if (hit >= 0 && e->clicks == 2) open_dicon(hit);
        } else if (e->button == 2) {
            int sx = e->x, sy = e->y;
            if (hit >= 0) {
                dsel = hit;
                draw_desktop();
                desk_icon_t *d = &dicons[hit];
                ui_menu_item_t m[] = {
                    { "Abrir", 1, GLYPH_OPEN, NULL, false, false },
                    { "Mover para a Lixeira", 2, GLYPH_DELETE, "Del", !d->file, false },
                };
                int r = ui_menu(sx, sy, m, 2);
                if (r == 1) open_dicon(hit);
                if (r == 2) { trash_move(d->path); scan_desktop(); draw_desktop(); }
            } else {
                static const ui_menu_item_t m[] = {
                    { "Nova pasta", 1, GLYPH_NEW_FOLDER, NULL, false, false },
                    { "Novo arquivo de texto", 2, GLYPH_NEW_FILE, NULL, false, false },
                    { NULL, 0, 0, NULL, false, true },
                    { "Abrir no Terminal", 3, 0, NULL, false, false },
                    { "Atualizar", 4, GLYPH_REFRESH, NULL, false, false },
                    { NULL, 0, 0, NULL, false, true },
                    { "Alterar papel de parede…", 5, GLYPH_RENAME, NULL, false, false },
                };
                switch (ui_menu(sx, sy, m, 7)) {
                case 1: desktop_new_item(true); break;
                case 2: desktop_new_item(false); break;
                case 3: app_launch("terminal", DESK_FOLDER); break;
                case 4: scan_desktop(); draw_desktop(); break;
                case 5: app_launch("settings", "personalizacao"); break;
                }
            }
        }
        break;
    }
    case EV_KEY_DOWN:
        if (e->key == KEY_ENTER && dsel >= 0) open_dicon(dsel);
        if (e->key == KEY_DELETE && dsel >= 0 && dicons[dsel].file) {
            trash_move(dicons[dsel].path);
            scan_desktop();
            draw_desktop();
        }
        break;
    }
}

/* ================================================================== */
/* Banners e Central de Notificações                                   */
/* ================================================================== */
static notification_t toast_n;
static uint64_t toast_until;
static bool toast_hover;
static rect_t toast_close_r;

static void fmt_time(uint64_t t, char *out, size_t size) {
    datetime_t dt;
    time_to_datetime(t, &dt);
    snprintf(out, size, "%02d:%02d", dt.hour, dt.minute);
}

/* cartão de notificação (banner e central) */
static void draw_card(canvas_t *c, rect_t r, const notification_t *n, bool hover, rect_t *close_out) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 14, theme.dark ? ARGB(0xF0, 0x3A3A3C) : ARGB(0xF4, 0xFFFFFF));
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 14, theme.dark ? ARGB(0x40, 0xFFFFFF) : ARGB(0x22, 0x000000));
    icon_draw(c, n->icon ? n->icon : ICON_LOGO, r.x + 12, r.y + (r.h - 32) / 2, 32);
    char t[16];
    fmt_time(n->time, t, sizeof(t));
    font_draw_ellipsis(c, r.x + 54, r.y + 10, r.w - 120, n->title, theme.text, FONT_BOLD);
    font_draw(c, r.x + r.w - 12 - font_width(t, 0), r.y + 10, t, theme.text_dim, 0);
    font_draw_ellipsis(c, r.x + 54, r.y + 30, r.w - 66, n->body, theme.text, 0);
    font_draw_ellipsis(c, r.x + 54, r.y + 50, r.w - 66, n->app, theme.text_dim, 0);
    rect_t cr = R(r.x - 6, r.y - 6, 20, 20);
    if (hover) {
        gfx_circle(c, cr.x + 10, cr.y + 10, 10, theme.dark ? HEX(0x5A5A5E) : HEX(0xF2F2F2));
        gfx_ring(c, cr.x + 10, cr.y + 10, 9.5f, 1, ARGB(0x40, 0x000000));
        glyph_draw(c, GLYPH_CLOSE, cr.x + 4, cr.y + 4, 12, theme.text);
    }
    if (close_out) *close_out = cr;
}

static void draw_toast(void) {
    canvas_t *c = win_canvas(toast);
    gfx_clear(c, 0);
    draw_card(c, R(8, 8, TOAST_W - 16, TOAST_H - 16), &toast_n, toast_hover, &toast_close_r);
    win_invalidate(toast);
}

static void show_next_toast(void) {
    if (toast->visible || center->visible || wm_is_locked()) return;
    notification_t n;
    if (!notify_next_toast(&n)) return;
    if (dnd) return;
    toast_n = n;
    toast_hover = false;
    toast_until = timer_ms() + 5000;
    draw_toast();
    win_show(toast, true);
}

static void hide_toast(void) {
    if (toast->visible) win_show(toast, false);
}

static void toast_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_MOVE:
        if (!toast_hover) { toast_hover = true; draw_toast(); }
        break;
    case EV_MOUSE_LEAVE:
        toast_hover = false;
        toast_until = timer_ms() + 2000;
        draw_toast();
        break;
    case EV_MOUSE_UP:
        hide_toast();
        if (!ui_hit(toast_close_r, e)) {
            notify_dismiss(toast_n.id);
            if (toast_n.path[0]) app_open_path(toast_n.path);
        }
        show_next_toast();
        break;
    }
}

/* ---- central: notificações e calendário ---- */
static int cal_month, cal_year;
static rect_t cal_prev, cal_next, clear_all_r;
static notification_t cnotes[NOTIFY_MAX];
static rect_t cnote_r[3], cnote_close[3];
static int ncnotes, center_hover = -1, center_h;

static int days_in_month(int m, int y) {
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static int notes_height(int n) {
    return 44 + (n ? imin(n, 3) * (CARD_H + 8) + (n > 3 ? 22 : 0) : 40) + 8;
}

static void draw_center(void) {
    canvas_t *c = win_canvas(center);
    int w = CENTER_W;
    gfx_clear(c, 0);

    ncnotes = notify_list(cnotes, NOTIFY_MAX);
    /* a central "flutua" sobre o papel de parede: texto branco com sombra */
    font_draw(c, 15, 15, "Notificações", ARGB(0x90, 0x000000), FONT_BOLD);
    font_draw(c, 14, 14, "Notificações", HEX(0xFFFFFF), FONT_BOLD);
    clear_all_r = R(0, 0, 0, 0);
    if (ncnotes) {
        const char *cl = "Limpar tudo";
        clear_all_r = R(w - 22 - font_width(cl, 0), 8, font_width(cl, 0) + 12, 26);
        gfx_round_fill(c, clear_all_r.x, clear_all_r.y, clear_all_r.w, clear_all_r.h, 13,
                       center_hover == 50 ? theme.popup_bg : ARGB(0xC0, theme.dark ? 0x3A3A3C : 0xF4F4F5));
        font_draw(c, clear_all_r.x + 6, 13, cl, theme.text, 0);
    }
    int y = 44;
    if (!ncnotes) {
        rect_t e = R(8, y, w - 16, 36);
        gfx_round_fill(c, e.x, e.y, e.w, e.h, 12, ARGB(0xC0, theme.dark ? 0x3A3A3C : 0xF4F4F5));
        ui_text_center(c, e, "Nenhuma notificação", theme.text_dim, 0);
    }
    for (int i = 0; i < ncnotes && i < 3; i++) {
        cnote_r[i] = R(8, y, w - 16, CARD_H);
        draw_card(c, cnote_r[i], &cnotes[i], center_hover == i, &cnote_close[i]);
        y += CARD_H + 8;
    }
    if (ncnotes > 3) {
        char more[48];
        snprintf(more, sizeof(more), "e mais %d", ncnotes - 3);
        font_draw(c, 16, y, more, HEX(0xFFFFFF), 0);
    }

    /* widget de calendário */
    int cy = notes_height(ncnotes);
    rect_t card = R(8, cy, w - 16, CAL_H - 16);
    gfx_round_fill(c, card.x, card.y, card.w, card.h, 16, theme.dark ? ARGB(0xF0, 0x2C2C2E) : ARGB(0xF4, 0xFFFFFF));
    gfx_round_rect(c, card.x, card.y, card.w, card.h, 16, theme.dark ? ARGB(0x40, 0xFFFFFF) : ARGB(0x22, 0x000000));
    datetime_t now;
    time_to_datetime(time_now(), &now);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s de %d", month_name(cal_month), cal_year);
    buf[0] = (char)toupper(buf[0]);
    font_draw(c, card.x + 16, card.y + 14, buf, HEX(0xFF3B30), FONT_BOLD);
    cal_prev = R(card.x + card.w - 72, card.y + 8, 28, 28);
    cal_next = R(card.x + card.w - 40, card.y + 8, 28, 28);
    glyph_draw(c, GLYPH_CHEVRON_UP, cal_prev.x + 6, cal_prev.y + 6, 16, theme.text_dim);
    glyph_draw(c, GLYPH_CHEVRON_DOWN, cal_next.x + 6, cal_next.y + 6, 16, theme.text_dim);
    static const char *wd[] = { "D", "S", "T", "Q", "Q", "S", "S" };
    int cell = (card.w - 24) / 7, gx = card.x + 12;
    for (int i = 0; i < 7; i++) {
        int tw = font_width(wd[i], 0);
        font_draw(c, gx + i * cell + (cell - tw) / 2, card.y + 46, wd[i], theme.text_dim, 0);
    }
    datetime_t first = { .year = cal_year, .month = cal_month, .day = 1 };
    time_to_datetime(datetime_to_time(&first), &first);
    int days = days_in_month(cal_month, cal_year);
    for (int d = 1; d <= days; d++) {
        int idx = first.weekday + d - 1;
        int x = gx + (idx % 7) * cell, yy = card.y + 72 + (idx / 7) * 36;
        bool today = d == now.day && cal_month == now.month && cal_year == now.year;
        snprintf(buf, sizeof(buf), "%d", d);
        if (today) gfx_circle(c, x + cell / 2.0f, yy + 8, 14, HEX(0xFF3B30));
        int tw = font_width(buf, 0);
        font_draw(c, x + (cell - tw) / 2, yy, buf, today ? HEX(0xFFFFFF) : theme.text, today ? FONT_BOLD : 0);
    }
    win_invalidate(center);
}

static void close_popups(window_t *except);

static void toggle_center(void) {
    if (center->visible) {
        win_show(center, false);
    } else {
        close_popups(center);
        hide_toast();
        datetime_t now;
        time_to_datetime(time_now(), &now);
        cal_month = now.month;
        cal_year = now.year;
        center_hover = -1;
        center_h = notes_height(notify_count()) + CAL_H;
        win_resize(center, CENTER_W, center_h);
        win_move(center, scr_w - CENTER_W - 6, MENUBAR_H + 6);
        draw_center();
        win_show(center, true);
    }
    draw_menubar();
}

static void center_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_MOVE: {
        int h = -1;
        for (int i = 0; i < ncnotes && i < 3; i++) if (ui_hit(cnote_r[i], e)) h = i;
        if (ui_hit(clear_all_r, e)) h = 50;
        if (h != center_hover) { center_hover = h; draw_center(); }
        break;
    }
    case EV_MOUSE_LEAVE:
        center_hover = -1;
        draw_center();
        break;
    case EV_MOUSE_UP:
        if (ui_hit(cal_prev, e)) { if (--cal_month < 1) { cal_month = 12; cal_year--; } }
        else if (ui_hit(cal_next, e)) { if (++cal_month > 12) { cal_month = 1; cal_year++; } }
        else if (ui_hit(clear_all_r, e)) notify_clear();
        else {
            for (int i = 0; i < ncnotes && i < 3; i++) {
                bool on_close = center_hover == i && ui_hit(cnote_close[i], e);
                if (!ui_hit(cnote_r[i], e) && !on_close) continue;
                notify_dismiss(cnotes[i].id);
                if (!on_close && cnotes[i].path[0]) {
                    win_show(center, false);
                    app_open_path(cnotes[i].path);
                    draw_menubar();
                    return;
                }
                break;
            }
        }
        if (notes_height(notify_count()) + CAL_H != center_h) {
            win_show(center, false);
            toggle_center();
        } else {
            draw_center();
        }
        draw_menubar();
        break;
    case EV_KEY_DOWN:
        if (e->key == KEY_ESC) toggle_center();
        break;
    case EV_BLUR:
    case EV_POPUP_CLOSE:
        if (center->visible) toggle_center();
        break;
    }
}

/* ================================================================== */
/* Central de Controle                                                 */
/* ================================================================== */
static rect_t cc_tiles[4], cc_swatch[ACCENT_COUNT], cc_wall;
static int cc_hover = -1;

static void cc_module(canvas_t *c, rect_t r) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 14, theme.dark ? ARGB(0xE8, 0x3A3A3C) : ARGB(0xE8, 0xFFFFFF));
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 14, theme.dark ? ARGB(0x40, 0xFFFFFF) : ARGB(0x18, 0x000000));
}

static void draw_ccenter(void) {
    canvas_t *c = win_canvas(ccenter);
    gfx_clear(c, 0);
    gfx_round_fill(c, 0, 0, CC_W, CC_H, 18, theme.dark ? ARGB(0xE6, 0x232325) : ARGB(0xE6, 0xE9E9EC));
    gfx_round_rect(c, 0, 0, CC_W, CC_H, 18, theme.popup_border);
    static const char *names[4] = { "Escuro", "Silêncio", "Captura", "Bloquear" };
    static const int glyphs[4] = { GLYPH_LOCK, GLYPH_BELL, GLYPH_ZOOM_IN, GLYPH_LOCK };
    bool on[4] = { theme.dark, dnd, false, false };
    for (int i = 0; i < 4; i++) {
        rect_t r = R(12 + (i % 2) * 142, 12 + (i / 2) * 70, 134, 60);
        cc_tiles[i] = r;
        cc_module(c, r);
        if (cc_hover == i) gfx_round_fill(c, r.x, r.y, r.w, r.h, 14, theme.hover);
        uint32_t circle_col = on[i] ? theme.accent : (theme.dark ? HEX(0x5A5A5E) : HEX(0xDCDCE0));
        gfx_circle(c, r.x + 24, r.y + 30, 15, circle_col);
        if (i == 0) {
            gfx_circle(c, r.x + 24, r.y + 30, 7, on[i] ? HEX(0xFFFFFF) : theme.text);
            gfx_circle(c, r.x + 27, r.y + 27, 6, circle_col);       /* lua */
        } else {
            glyph_draw(c, glyphs[i], r.x + 16, r.y + 22, 16, on[i] ? HEX(0xFFFFFF) : theme.text);
        }
        font_draw_ellipsis(c, r.x + 46, r.y + 14, r.w - 50, names[i], theme.text, FONT_BOLD);
        const char *st = i < 2 ? (on[i] ? "Ativado" : "Desativado") : (i == 2 ? "da tela" : "a tela");
        font_draw_ellipsis(c, r.x + 46, r.y + 32, r.w - 50, st, theme.text_dim, 0);
    }
    rect_t m = R(12, 152, CC_W - 24, 72);
    cc_module(c, m);
    font_draw(c, m.x + 14, m.y + 10, "Cor de destaque", theme.text, FONT_BOLD);
    for (int i = 0; i < ACCENT_COUNT; i++) {
        int cx = m.x + 26 + i * 40, cy = m.y + 50;
        cc_swatch[i] = R(cx - 14, cy - 14, 28, 28);
        if (i == theme.accent_index) gfx_ring(c, cx, cy, 14, 2, theme.text);
        gfx_circle(c, cx, cy, 10, accent_colors[i]);
    }
    rect_t wbox = R(12, 234, CC_W - 24, 70);
    cc_module(c, wbox);
    cc_wall = wbox;
    if (cc_hover == 10) gfx_round_fill(c, wbox.x, wbox.y, wbox.w, wbox.h, 14, theme.hover);
    gfx_blit_scaled(c, R(wbox.x + 12, wbox.y + 12, 72, 46), wallpaper, R(0, 0, wallpaper->w, wallpaper->h), true);
    font_draw(c, wbox.x + 98, wbox.y + 16, "Papel de parede", theme.text, FONT_BOLD);
    font_draw(c, wbox.x + 98, wbox.y + 36, wallpaper_names[theme.wallpaper], theme.text_dim, 0);
    glyph_draw(c, GLYPH_CHEVRON_RIGHT, wbox.x + wbox.w - 28, wbox.y + 27, 16, theme.text_dim);
    win_invalidate(ccenter);
}

static void toggle_ccenter(void) {
    if (ccenter->visible) {
        win_show(ccenter, false);
    } else {
        close_popups(ccenter);
        cc_hover = -1;
        draw_ccenter();
        win_show(ccenter, true);
    }
    draw_menubar();
}

static void ccenter_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_MOVE: {
        int h = -1;
        for (int i = 0; i < 4; i++) if (ui_hit(cc_tiles[i], e)) h = i;
        if (ui_hit(cc_wall, e)) h = 10;
        if (h != cc_hover) { cc_hover = h; draw_ccenter(); }
        break;
    }
    case EV_MOUSE_UP:
        if (ui_hit(cc_tiles[0], e)) wm_apply_theme(!theme.dark, theme.accent_index, theme.wallpaper);
        else if (ui_hit(cc_tiles[1], e)) { dnd = !dnd; if (dnd) hide_toast(); }
        else if (ui_hit(cc_tiles[2], e)) { toggle_ccenter(); wm_capture(true); return; }
        else if (ui_hit(cc_tiles[3], e)) { toggle_ccenter(); lock_screen(); return; }
        else if (ui_hit(cc_wall, e)) wm_apply_theme(theme.dark, theme.accent_index, (theme.wallpaper + 1) % WALLPAPER_COUNT);
        for (int i = 0; i < ACCENT_COUNT; i++)
            if (ui_hit(cc_swatch[i], e)) wm_apply_theme(theme.dark, i, theme.wallpaper);
        draw_ccenter();
        break;
    case EV_KEY_DOWN:
        if (e->key == KEY_ESC) toggle_ccenter();
        break;
    case EV_BLUR:
    case EV_POPUP_CLOSE:
        if (ccenter->visible) toggle_ccenter();
        break;
    }
}

/* ================================================================== */
/* Pesquisa rápida (Super+Espaço) — também usada pelo Launchpad         */
/* ================================================================== */
typedef struct {
    int kind;                 /* 0 = app, 1 = arquivo */
    int app;
    char name[64];
    char path[VFS_PATH_MAX];
    rect_t r;
} result_t;

static result_t results[24];
static int nresults, r_sel, r_hover = -1;
static ui_edit_t query;

static void find_files(vnode_t *dir, const char *q, int depth) {
    for (vnode_t *n = dir->children; n && nresults < 10 && depth < 6; n = n->next) {
        if (n->name[0] == '.') continue;
        if (strcasestr_utf8(n->name, q)) {
            result_t *e = &results[nresults++];
            e->kind = 1;
            strlcpy(e->name, n->name, sizeof(e->name));
            vfs_path(n, e->path, sizeof(e->path));
        }
        if (n->type == VFS_DIR) find_files(n, q, depth + 1);
    }
}

static void search(const char *q) {
    nresults = 0;
    if (!*q) return;
    for (int i = 0; i < app_count; i++)
        if (strcasestr_utf8(app_list[i].name, q) || strcasestr_utf8(app_list[i].description, q)) {
            result_t *e = &results[nresults++];
            e->kind = 0;
            e->app = i;
            strlcpy(e->name, app_list[i].name, sizeof(e->name));
        }
    vfs_lock();
    find_files(vfs_root(), q, 0);
    vfs_unlock();
    if (r_sel >= nresults) r_sel = 0;
}

static void open_result(result_t *e) {
    if (e->kind == 0) app_launch(app_list[e->app].id, NULL);
    else app_open_path(e->path);
}

static int spot_height(void) { return 64 + (nresults ? imin(nresults, 8) * 44 + 16 : 0); }

static void draw_spot(void) {
    canvas_t *c = win_canvas(spot);
    int h = c->h;
    gfx_clear(c, 0);
    gfx_round_fill(c, 0, 0, SPOT_W, h, 16, theme.dark ? ARGB(0xF0, 0x2C2C2E) : ARGB(0xF2, 0xF4F4F6));
    gfx_round_rect(c, 0, 0, SPOT_W, h, 16, theme.popup_border);
    glyph_draw(c, GLYPH_SEARCH, 20, 18, 28, theme.text_dim);
    rect_t saved = c->clip;
    canvas_clip(c, rect_intersect(saved, R(60, 0, SPOT_W - 76, 64)));
    if (!query.len) font_draw(c, 64, 16, "Pesquisar", theme.text_disabled, FONT_2X);
    font_draw(c, 64, 16, query.text, theme.text, FONT_2X);
    gfx_fill(c, 64 + font_width_n(query.text, query.cur, FONT_2X), 16, 2, 32, theme.accent);
    c->clip = saved;
    if (nresults) gfx_hline(c, 1, 63, SPOT_W - 2, theme.border);
    for (int i = 0; i < nresults && i < 8; i++) {
        result_t *e = &results[i];
        e->r = R(8, 72 + i * 44, SPOT_W - 16, 42);
        bool sel = i == r_sel;
        if (sel) gfx_round_fill(c, e->r.x, e->r.y, e->r.w, e->r.h, 8, theme.accent);
        else if (i == r_hover) gfx_round_fill(c, e->r.x, e->r.y, e->r.w, e->r.h, 8, theme.hover);
        int icon = e->kind == 0 ? app_list[e->app].icon : ICON_TEXT;
        if (e->kind == 1) {
            vnode_t *n = vfs_lookup(NULL, e->path);
            icon = app_icon_for(e->name, n && n->type == VFS_DIR);
        }
        icon_draw(c, icon, e->r.x + 8, e->r.y + 5, 32);
        font_draw(c, e->r.x + 50, e->r.y + 4, e->name, sel ? HEX(0xFFFFFF) : theme.text, 0);
        font_draw_ellipsis(c, e->r.x + 50, e->r.y + 22, e->r.w - 60, e->kind == 0 ? "Aplicativo" : e->path,
                           sel ? ARGB(0xD0, 0xFFFFFF) : theme.text_dim, 0);
    }
    win_invalidate(spot);
}

static void update_spot(void) {
    search(query.text);
    int h = spot_height();
    if (win_canvas(spot)->h != h) win_resize(spot, SPOT_W, h);
    draw_spot();
}

static void toggle_spot(void) {
    if (spot->visible) {
        win_show(spot, false);
        return;
    }
    close_popups(spot);
    ui_edit_set(&query, "");
    r_sel = 0;
    r_hover = -1;
    update_spot();
    win_show(spot, true);
}

static void spot_event(event_t *e) {
    switch (e->type) {
    case EV_KEY_DOWN:
        if (e->key == KEY_ESC) { toggle_spot(); break; }
        if (e->key == KEY_ENTER) {
            if (nresults) { win_show(spot, false); open_result(&results[r_sel]); }
            break;
        }
        if (e->key == KEY_DOWN || e->key == KEY_UP) {
            if (nresults) r_sel = (r_sel + (e->key == KEY_DOWN ? 1 : imin(nresults, 8) - 1)) % imin(nresults, 8);
            draw_spot();
            break;
        }
        if (ui_edit_key(&query, e)) { r_sel = 0; update_spot(); }
        else draw_spot();
        break;
    case EV_MOUSE_MOVE: {
        int h = -1;
        for (int i = 0; i < nresults && i < 8; i++) if (ui_hit(results[i].r, e)) h = i;
        if (h != r_hover) { r_hover = h; draw_spot(); }
        break;
    }
    case EV_MOUSE_UP:
        for (int i = 0; i < nresults && i < 8; i++)
            if (ui_hit(results[i].r, e)) { win_show(spot, false); open_result(&results[i]); return; }
        break;
    case EV_BLUR:
    case EV_POPUP_CLOSE:
        if (spot->visible) win_show(spot, false);
        break;
    }
}

/* ================================================================== */
/* Launchpad: todos os apps em tela cheia                               */
/* ================================================================== */
#define LP_COLS 7
#define LP_CELL 124
static rect_t lp_cells[32];
static int lp_ids[32], lp_n, lp_hover = -1;
static ui_edit_t lp_query;

static void draw_launchpad(void) {
    canvas_t *c = win_canvas(launchpad);
    gfx_blit(c, 0, 0, wall_blur, 0, 0, c->w, c->h);
    gfx_fill(c, 0, 0, c->w, c->h, ARGB(0x50, 0x000000));
    /* campo de pesquisa */
    rect_t sr = R((c->w - 260) / 2, 40, 260, 30);
    gfx_round_fill(c, sr.x, sr.y, sr.w, sr.h, 8, ARGB(0x50, 0xFFFFFF));
    gfx_round_rect(c, sr.x, sr.y, sr.w, sr.h, 8, ARGB(0x60, 0xFFFFFF));
    glyph_draw(c, GLYPH_SEARCH, sr.x + 10, sr.y + 7, 16, HEX(0xFFFFFF));
    if (!lp_query.len) font_draw(c, sr.x + 34, sr.y + 7, "Pesquisar", ARGB(0xC0, 0xFFFFFF), 0);
    font_draw(c, sr.x + 34, sr.y + 7, lp_query.text, HEX(0xFFFFFF), 0);
    gfx_vline(c, sr.x + 34 + font_width_n(lp_query.text, lp_query.cur, 0), sr.y + 7, 16, HEX(0xFFFFFF));

    lp_n = 0;
    for (int i = 0; i < app_count && lp_n < 32; i++)
        if (!lp_query.len || strcasestr_utf8(app_list[i].name, lp_query.text)) lp_ids[lp_n++] = i;
    int cols = imin(LP_COLS, imax(1, lp_n));
    int x0 = (c->w - cols * LP_CELL) / 2, y0 = 120;
    for (int k = 0; k < lp_n; k++) {
        rect_t r = R(x0 + (k % LP_COLS) * LP_CELL, y0 + (k / LP_COLS) * (LP_CELL + 16), LP_CELL, LP_CELL + 10);
        lp_cells[k] = r;
        int isz = k == lp_hover ? 80 : 76;
        icon_draw(c, app_list[lp_ids[k]].icon, r.x + (r.w - isz) / 2, r.y + 10 - (isz - 76) / 2, isz);
        const char *nm = app_list[lp_ids[k]].name;
        int tw = imin(font_width(nm, 0), r.w - 8);
        font_draw_ellipsis(c, r.x + (r.w - tw) / 2 + 1, r.y + 95, r.w - 8, nm, ARGB(0x90, 0x000000), 0);
        font_draw_ellipsis(c, r.x + (r.w - tw) / 2, r.y + 94, r.w - 8, nm, HEX(0xFFFFFF), 0);
    }
    if (!lp_n) ui_text_center(c, R(0, 200, c->w, 30), "Nenhum aplicativo encontrado", HEX(0xFFFFFF), 0);
    win_invalidate(launchpad);
}

static void toggle_launchpad(void) {
    if (launchpad->visible) {
        win_show(launchpad, false);
        return;
    }
    close_popups(launchpad);
    ui_edit_set(&lp_query, "");
    lp_hover = -1;
    draw_launchpad();
    win_show(launchpad, true);
}

static void launchpad_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_MOVE: {
        int h = -1;
        for (int k = 0; k < lp_n; k++) if (ui_hit(lp_cells[k], e)) h = k;
        if (h != lp_hover) { lp_hover = h; draw_launchpad(); }
        break;
    }
    case EV_MOUSE_UP:
        for (int k = 0; k < lp_n; k++)
            if (ui_hit(lp_cells[k], e)) {
                win_show(launchpad, false);
                app_launch(app_list[lp_ids[k]].id, NULL);
                return;
            }
        if (e->y > 80) win_show(launchpad, false);
        break;
    case EV_KEY_DOWN:
        if (e->key == KEY_ESC) {
            if (lp_query.len) { ui_edit_set(&lp_query, ""); draw_launchpad(); }
            else win_show(launchpad, false);
            break;
        }
        if (e->key == KEY_ENTER && lp_n) {
            win_show(launchpad, false);
            app_launch(app_list[lp_ids[0]].id, NULL);
            break;
        }
        if (ui_edit_key(&lp_query, e)) { lp_hover = -1; draw_launchpad(); }
        break;
    case EV_BLUR:
    case EV_POPUP_CLOSE:
        if (launchpad->visible) win_show(launchpad, false);
        break;
    }
}

/* ================================================================== */
/* Dock                                                                */
/* ================================================================== */
#define DOCK_ICON 48
#define DOCK_MAX  78
#define DOCK_GAP  6
#define PLATE_H   64
#define SEP_W     14

enum { DK_APP, DK_LAUNCHPAD, DK_WINDOW, DK_SEP, DK_TRASH };

typedef struct {
    int kind, app, win_id, icon;
    bool running, active;
    float size, cx;           /* tamanho atual e centro (ampliação) */
} dock_item_t;

static dock_item_t ditems[48];
static int ndock, dock_hover = -1, dock_press = -1;
static int dock_mouse = -1;          /* x do mouse sobre o Dock, -1 = fora */
static int plate_x, plate_w;

static bool pinned_icon(int icon) {
    for (int i = 0; i < app_count; i++)
        if (app_list[i].pinned && app_list[i].icon == icon) return true;
    return false;
}

static void build_dock(void) {
    ndock = 0;
    bool launchpad_added = false;
    for (int i = 0; i < app_count; i++) {
        if (!app_list[i].pinned) continue;
        dock_item_t it = { .kind = DK_APP, .app = i, .icon = app_list[i].icon };
        for (int j = 0; j < nwins; j++)
            if (wins[j].icon == it.icon) {
                it.running = true;
                if (wins[j].focused) it.active = true;
            }
        ditems[ndock++] = it;
        if (!launchpad_added) {
            ditems[ndock++] = (dock_item_t){ .kind = DK_LAUNCHPAD, .icon = ICON_LAUNCHPAD };
            launchpad_added = true;
        }
    }
    /* apps abertos que não estão fixados (um ícone por app) */
    for (int j = 0; j < nwins && ndock < 44; j++) {
        if (pinned_icon(wins[j].icon)) continue;
        bool dup = false;
        for (int k = 0; k < ndock; k++) dup |= ditems[k].kind == DK_WINDOW && ditems[k].icon == wins[j].icon;
        if (dup) continue;
        ditems[ndock++] = (dock_item_t){ .kind = DK_WINDOW, .win_id = wins[j].id, .icon = wins[j].icon, .running = true };
    }
    ditems[ndock++] = (dock_item_t){ .kind = DK_SEP };
    ditems[ndock++] = (dock_item_t){ .kind = DK_TRASH, .icon = trash_count() ? ICON_TRASH_FULL : ICON_TRASH };
}

/* tamanho de cada ícone conforme a distância até o mouse (efeito de lupa) */
static void layout_dock(void) {
    float base_total = 0;
    for (int i = 0; i < ndock; i++) base_total += (ditems[i].kind == DK_SEP ? SEP_W : DOCK_ICON) + DOCK_GAP;
    base_total -= DOCK_GAP;
    float x = (scr_w - base_total) / 2;
    float base_cx[48];
    for (int i = 0; i < ndock; i++) {
        float bw = ditems[i].kind == DK_SEP ? SEP_W : DOCK_ICON;
        base_cx[i] = x + bw / 2;
        x += bw + DOCK_GAP;
    }
    float total = 0;
    for (int i = 0; i < ndock; i++) {
        float s = DOCK_ICON;
        if (ditems[i].kind == DK_SEP) s = SEP_W;
        else if (dock_mouse >= 0) {
            float d = (dock_mouse - base_cx[i]) / 150.0f;
            if (d < 0) d = -d;
            if (d < 1) {
                float f = 1 - d * d;
                s = DOCK_ICON + (DOCK_MAX - DOCK_ICON) * f * f;
            }
        }
        ditems[i].size = s;
        total += s + DOCK_GAP;
    }
    total -= DOCK_GAP;
    x = (scr_w - total) / 2;
    for (int i = 0; i < ndock; i++) {
        ditems[i].cx = x + ditems[i].size / 2;
        x += ditems[i].size + DOCK_GAP;
    }
    plate_x = (int)((scr_w - total) / 2) - 10;
    plate_w = (int)total + 20;
}

static rect_t dock_item_rect(int i) {
    int plate_y = DOCK_WIN_H - 6 - PLATE_H;
    int s = (int)ditems[i].size;
    return R((int)(ditems[i].cx - s / 2.0f), plate_y + PLATE_H - 8 - s, s, s);
}

static const char *dock_label(int i) {
    dock_item_t *it = &ditems[i];
    switch (it->kind) {
    case DK_APP: return app_list[it->app].name;
    case DK_LAUNCHPAD: return "Launchpad";
    case DK_TRASH: return "Lixeira";
    case DK_WINDOW:
        for (int j = 0; j < nwins; j++) if (wins[j].id == it->win_id) return wins[j].title;
        for (int k = 0; k < app_count; k++) if (app_list[k].icon == it->icon) return app_list[k].name;
    }
    return "";
}

static void draw_dock(void) {
    canvas_t *c = win_canvas(dock);
    build_dock();
    layout_dock();
    gfx_clear(c, 0);
    int plate_y = DOCK_WIN_H - 6 - PLATE_H;
    /* com o mouse em cima, uma área quase invisível mantém o Dock "ativo" */
    if (dock_mouse >= 0) gfx_set(c, plate_x, plate_y - (DOCK_MAX - DOCK_ICON) - 6, plate_w, DOCK_MAX - DOCK_ICON + 6, ARGB(1, 0));
    gfx_shadow(c, R(plate_x, plate_y, plate_w, PLATE_H), 18, 12, 50);
    gfx_round_fill(c, plate_x, plate_y, plate_w, PLATE_H, 18, theme.dock_bg);
    gfx_round_rect(c, plate_x, plate_y, plate_w, PLATE_H, 18, theme.dock_border);
    for (int i = 0; i < ndock; i++) {
        dock_item_t *it = &ditems[i];
        if (it->kind == DK_SEP) {
            gfx_vline(c, (int)it->cx, plate_y + 12, PLATE_H - 24, theme.dark ? ARGB(0x50, 0xFFFFFF) : ARGB(0x40, 0x000000));
            continue;
        }
        rect_t r = dock_item_rect(i);
        if (i == dock_press) r = R(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
        icon_draw(c, it->icon, r.x, r.y, r.w);
        if (it->running)
            gfx_circle(c, it->cx, plate_y + PLATE_H - 4, 2, theme.dark ? HEX(0xE0E0E0) : HEX(0x3A3A3C));
    }
    /* rótulo do item sob o mouse */
    if (dock_hover >= 0 && dock_hover < ndock && ditems[dock_hover].kind != DK_SEP) {
        const char *name = dock_label(dock_hover);
        rect_t r = dock_item_rect(dock_hover);
        int tw = imin(font_width(name, 0), 260) + 20;
        rect_t lr = R((int)ditems[dock_hover].cx - tw / 2, r.y - 34, tw, 24);
        gfx_round_fill(c, lr.x, lr.y, lr.w, lr.h, 6, theme.dark ? ARGB(0xF0, 0x3A3A3C) : ARGB(0xF0, 0xF4F4F6));
        gfx_round_rect(c, lr.x, lr.y, lr.w, lr.h, 6, theme.popup_border);
        font_draw_ellipsis(c, lr.x + 10, lr.y + 4, 260, name, theme.text, 0);
    }
    win_invalidate(dock);
}

static int dock_hit(int x, int y) {
    for (int i = 0; i < ndock; i++) {
        if (ditems[i].kind == DK_SEP) continue;
        rect_t r = dock_item_rect(i);
        r = R(r.x - DOCK_GAP / 2, r.y, r.w + DOCK_GAP, DOCK_WIN_H - r.y);
        if (rect_has(r, x, y)) return i;
    }
    return -1;
}

static int app_windows(int icon, int *ids, int max) {
    int n = 0;
    for (int j = 0; j < nwins && n < max; j++)
        if (wins[j].icon == icon) ids[n++] = wins[j].id;
    return n;
}

static void dock_click(int i) {
    dock_item_t *it = &ditems[i];
    close_popups(NULL);
    if (it->kind == DK_LAUNCHPAD) { toggle_launchpad(); return; }
    if (it->kind == DK_TRASH) { app_launch("explorer", TRASH_DIR); return; }
    int ids[16], focused_idx = -1;
    int n = app_windows(it->icon, ids, 16);
    for (int k = 0; k < n; k++)
        for (int j = 0; j < nwins; j++)
            if (wins[j].id == ids[k] && wins[j].focused && !wins[j].minimized) focused_idx = k;
    if (!n && it->kind == DK_APP) app_launch(app_list[it->app].id, NULL);
    else if (n == 1) wm_toggle(ids[0]);
    else if (n > 1) wm_activate(ids[focused_idx < 0 ? n - 1 : (focused_idx + 1) % n]);
}

static void dock_menu(int i, int x) {
    dock_item_t *it = &ditems[i];
    if (it->kind == DK_TRASH) {
        ui_menu_item_t m[] = {
            { "Abrir", 1, GLYPH_OPEN, NULL, false, false },
            { "Esvaziar Lixeira", 2, GLYPH_DELETE, NULL, trash_count() == 0, false },
        };
        int r = ui_menu(x, scr_h - DOCK_H - 80, m, 2);
        if (r == 1) app_launch("explorer", TRASH_DIR);
        if (r == 2 && ui_message(NULL, "Esvaziar Lixeira", "Excluir permanentemente todos os itens da Lixeira?",
                                 "Esvaziar", "Cancelar", NULL) == 1)
            trash_empty();
        return;
    }
    if (it->kind == DK_LAUNCHPAD) return;
    ui_menu_item_t m[16];
    static char titles[10][96];
    int ids[10], n = app_windows(it->icon, ids, 10), k = 0;
    for (int j = 0; j < n; j++) {
        for (int w = 0; w < nwins; w++) if (wins[w].id == ids[j]) strlcpy(titles[j], wins[w].title, 96);
        m[k++] = (ui_menu_item_t){ titles[j], 10 + j, 0, NULL, false, false };
    }
    if (n) m[k++] = (ui_menu_item_t){ NULL, 0, 0, NULL, false, true };
    if (it->kind == DK_APP) m[k++] = (ui_menu_item_t){ "Nova janela", 1, 0, NULL, false, false };
    if (n) m[k++] = (ui_menu_item_t){ "Encerrar", 2, GLYPH_CLOSE, NULL, false, false };
    int r = ui_menu(x, scr_h - DOCK_H - 40 - k * 32, m, k);
    if (r == 1) app_launch(app_list[it->app].id, NULL);
    if (r == 2) for (int j = 0; j < n; j++) wm_close(ids[j]);
    if (r >= 10) wm_activate(ids[r - 10]);
}

static void dock_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_MOVE: {
        dock_mouse = e->x;
        dock_hover = dock_hit(e->x, e->y);
        draw_dock();
        break;
    }
    case EV_MOUSE_LEAVE:
        dock_mouse = -1;
        dock_hover = dock_press = -1;
        draw_dock();
        break;
    case EV_MOUSE_DOWN:
        dock_press = dock_hit(e->x, e->y);
        draw_dock();
        break;
    case EV_MOUSE_UP: {
        int h = dock_hit(e->x, e->y), p = dock_press;
        dock_press = -1;
        if (h >= 0 && h == p) {
            if (e->button == 1) dock_click(h);
            else if (e->button == 2) dock_menu(h, (int)ditems[h].cx - 20);
        }
        draw_dock();
        break;
    }
    }
}

/* ================================================================== */
/* Barra de menus                                                      */
/* ================================================================== */
#define MB_MAX 12
static rect_t mb_rects[MB_MAX];
static char mb_titles[MB_MAX][32];
static int mb_count, mb_open = -1, mb_custom_from = -1;
static int mb_focus_id = -1, mb_focus_icon = -1;
static char mb_app[64];
static rect_t mb_logo, mb_cc, mb_search, mb_clock;
static int last_minute = -1;

static const char *app_name_for_icon(int icon) {
    for (int i = 0; i < app_count; i++) if (app_list[i].icon == icon) return app_list[i].name;
    return NULL;
}

static void collect_menus(void) {
    mb_focus_id = -1;
    mb_focus_icon = -1;
    for (int j = 0; j < nwins; j++)
        if (wins[j].focused && !wins[j].minimized) { mb_focus_id = wins[j].id; mb_focus_icon = wins[j].icon; }
    mb_count = 0;
    mb_custom_from = -1;
    if (mb_focus_id < 0) {
        strlcpy(mb_app, "Área de Trabalho", sizeof(mb_app));
        static const char *desk_menus[] = { "Arquivo", "Ir", "Janela", "Ajuda" };
        for (int i = 0; i < 4; i++) strlcpy(mb_titles[mb_count++], desk_menus[i], 32);
        return;
    }
    const char *nm = app_name_for_icon(mb_focus_icon);
    if (!nm) for (int j = 0; j < nwins; j++) if (wins[j].id == mb_focus_id) nm = wins[j].title;
    strlcpy(mb_app, nm ? nm : "", sizeof(mb_app));
    char menus[96];
    if (wm_get_menus(mb_focus_id, menus, sizeof(menus))) {
        mb_custom_from = 0;
        char *p = menus;
        while (*p && mb_count < MB_MAX - 2) {
            char *bar = strchr(p, '|');
            int len = bar ? (int)(bar - p) : (int)strlen(p);
            if (len > 31) len = 31;
            memcpy(mb_titles[mb_count], p, len);
            mb_titles[mb_count++][len] = 0;
            if (!bar) break;
            p = bar + 1;
        }
    } else {
        strlcpy(mb_titles[mb_count++], "Arquivo", 32);
    }
    strlcpy(mb_titles[mb_count++], "Janela", 32);
    strlcpy(mb_titles[mb_count++], "Ajuda", 32);
}

static void draw_menubar(void) {
    canvas_t *c = win_canvas(menubar);
    gfx_clear(c, theme.taskbar_bg);
    uint32_t fg = theme.text;
    collect_menus();

    mb_logo = R(8, 2, 32, MENUBAR_H - 4);
    if (mb_open == 100) gfx_round_fill(c, mb_logo.x, mb_logo.y, mb_logo.w, mb_logo.h, 5, theme.pressed);
    glyph_draw(c, GLYPH_LOGO, mb_logo.x + 7, 4, 18, color_with_alpha(fg, 0xD0));   /* capivara fosca */
    int x = mb_logo.x + mb_logo.w + 6;
    /* nome do app (negrito) e menus */
    int aw = font_width(mb_app, FONT_BOLD) + 20;
    mb_rects[0] = R(x, 2, aw, MENUBAR_H - 4);
    if (mb_open == 0) gfx_round_fill(c, x, 2, aw, MENUBAR_H - 4, 5, theme.pressed);
    font_draw(c, x + 10, 5, mb_app, fg, FONT_BOLD);
    x += aw;
    for (int i = 0; i < mb_count; i++) {
        int w = font_width(mb_titles[i], 0) + 20;
        mb_rects[i + 1] = R(x, 2, w, MENUBAR_H - 4);
        if (mb_open == i + 1) gfx_round_fill(c, x, 2, w, MENUBAR_H - 4, 5, theme.pressed);
        font_draw(c, x + 10, 5, mb_titles[i], fg, 0);
        x += w;
    }

    /* lado direito: rede, som, pesquisa, Central de Controle, relógio */
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    last_minute = dt.minute;
    static const char *wd[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    static const char *mo[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };
    char clock[40];
    snprintf(clock, sizeof(clock), "%s %d %s  %02d:%02d", wd[dt.weekday], dt.day, mo[dt.month - 1], dt.hour, dt.minute);
    int cw = font_width(clock, 0) + 20;
    mb_clock = R(scr_w - 8 - cw, 2, cw, MENUBAR_H - 4);
    if (center->visible) gfx_round_fill(c, mb_clock.x, 2, mb_clock.w, mb_clock.h, 5, theme.pressed);
    font_draw(c, mb_clock.x + 10, 5, clock, fg, 0);
    int nn = notify_count();
    if (nn) gfx_circle(c, mb_clock.x + 3, 13, 3, theme.accent);
    mb_cc = R(mb_clock.x - 34, 2, 30, MENUBAR_H - 4);
    if (ccenter->visible) gfx_round_fill(c, mb_cc.x, 2, mb_cc.w, mb_cc.h, 5, theme.pressed);
    /* ícone da Central de Controle: dois "interruptores" */
    gfx_round_rect(c, mb_cc.x + 7, 6, 16, 6, 3, fg);
    gfx_circle(c, mb_cc.x + 20, 9, 2, fg);
    gfx_round_rect(c, mb_cc.x + 7, 14, 16, 6, 3, fg);
    gfx_circle(c, mb_cc.x + 10, 17, 2, fg);
    mb_search = R(mb_cc.x - 30, 2, 28, MENUBAR_H - 4);
    if (spot->visible) gfx_round_fill(c, mb_search.x, 2, mb_search.w, mb_search.h, 5, theme.pressed);
    glyph_draw(c, GLYPH_SEARCH, mb_search.x + 6, 5, 16, fg);
    glyph_draw(c, GLYPH_SPEAKER, mb_search.x - 26, 5, 16, fg);
    glyph_draw(c, GLYPH_NETWORK, mb_search.x - 52, 5, 16, fg);
    if (dnd) glyph_draw(c, GLYPH_BELL, mb_search.x - 78, 5, 16, theme.text_dim);
    win_invalidate(menubar);
}

static void system_menu(void) {
    mb_open = 100;
    draw_menubar();
    static const ui_menu_item_t m[] = {
        { "Sobre o Capivara OS", 1, 0, NULL, false, false },
        { NULL, 0, 0, NULL, false, true },
        { "Configurações do Sistema…", 2, 0, NULL, false, false },
        { "Gerenciador de Tarefas…", 3, 0, "Ctrl+Shift+Esc", false, false },
        { NULL, 0, 0, NULL, false, true },
        { "Bloquear Tela", 4, GLYPH_LOCK, "Super+L", false, false },
        { NULL, 0, 0, NULL, false, true },
        { "Reiniciar…", 5, GLYPH_RESTART, NULL, false, false },
        { "Desligar…", 6, GLYPH_POWER, NULL, false, false },
    };
    int r = ui_menu(mb_logo.x, MENUBAR_H + 2, m, 9);
    mb_open = -1;
    draw_menubar();
    switch (r) {
    case 1: app_launch("settings", "sobre"); break;
    case 2: app_launch("settings", NULL); break;
    case 3: app_launch("taskmgr", NULL); break;
    case 4: lock_screen(); break;
    case 5: case 6:
        if (ui_message(NULL, r == 5 ? "Reiniciar" : "Desligar",
                       r == 5 ? "Reiniciar o computador agora? Os arquivos que estão só na memória serão perdidos."
                              : "Desligar o computador agora? Os arquivos que estão só na memória serão perdidos.",
                       r == 5 ? "Reiniciar" : "Desligar", "Cancelar", NULL) == 1) {
            if (r == 5) power_reboot();
            power_off();
        }
        break;
    }
}

static void help_menu(int x) {
    static const ui_menu_item_t m[] = {
        { "Bem-vindo ao Capivara OS", 1, 0, NULL, false, false },
        { "Atalhos de teclado", 2, 0, NULL, false, false },
    };
    int r = ui_menu(x, MENUBAR_H + 2, m, 2);
    if (r == 1) app_launch("notepad", "/home/usuario/Documentos/Bem-vindo.txt");
    if (r == 2)
        ui_message(NULL, "Atalhos de teclado",
                   "Super: Launchpad    Super+Espaço: Pesquisa\n"
                   "Alt+Tab: alternar apps    Alt+F4: fechar\n"
                   "Super+Tab: Mission Control    Super+Ctrl+setas: áreas de trabalho\n"
                   "Super+setas: encaixar janela    Super+Z: layouts\n"
                   "Super+D: área de trabalho    Super+E: Explorador\n"
                   "Super+L: bloquear    Super+V: área de transferência\n"
                   "Super+Shift+S ou Print Screen: captura de tela\n"
                   "Ctrl+Shift+Esc: Gerenciador de Tarefas",
                   "OK", NULL, NULL);
}

static void window_menu(int x) {
    int id = mb_focus_id;
    ui_menu_item_t m[] = {
        { "Minimizar", 1, 0, NULL, id < 0, false },
        { "Zoom", 2, 0, NULL, id < 0, false },
        { NULL, 0, 0, NULL, false, true },
        { "Mover para a esquerda da tela", 3, 0, "Super+←", id < 0, false },
        { "Mover para a direita da tela", 4, 0, "Super+→", id < 0, false },
        { NULL, 0, 0, NULL, false, true },
        { "Mission Control", 5, GLYPH_TASKVIEW, "Super+Tab", false, false },
    };
    switch (ui_menu(x, MENUBAR_H + 2, m, 7)) {
    case 1: wm_minimize(id); break;
    case 2: wm_zoom(id); break;
    case 3: wm_snap_window(id, -1); break;
    case 4: wm_snap_window(id, 1); break;
    case 5: wm_task_view(); break;
    }
}

static const app_info_t *focused_app(void) {
    for (int i = 0; i < app_count; i++) if (app_list[i].icon == mb_focus_icon) return &app_list[i];
    return NULL;
}

static void app_menu(int x) {
    const app_info_t *a = focused_app();
    char about[96], quit[96];
    snprintf(about, sizeof(about), "Sobre %s", mb_app);
    snprintf(quit, sizeof(quit), "Encerrar %s", mb_app);
    ui_menu_item_t m[] = {
        { about, 1, 0, NULL, mb_focus_id < 0, false },
        { NULL, 0, 0, NULL, false, true },
        { "Ocultar", 2, 0, NULL, mb_focus_id < 0, false },
        { quit, 3, GLYPH_CLOSE, "Alt+F4", mb_focus_id < 0, false },
    };
    int r = ui_menu(x, MENUBAR_H + 2, m, 4);
    int ids[16], n = app_windows(mb_focus_icon, ids, 16);
    if (r == 1) {
        char msg[200];
        snprintf(msg, sizeof(msg), "%s\n\n%s.\nParte do Capivara OS " CAPIVARA_VERSION ".", mb_app, a ? a->description : "Aplicativo");
        ui_message(NULL, about, msg, "OK", NULL, NULL);
    } else if (r == 2) {
        for (int i = 0; i < n; i++) wm_minimize(ids[i]);
    } else if (r == 3) {
        for (int i = 0; i < n; i++) wm_close(ids[i]);
        if (!n && mb_focus_id >= 0) wm_close(mb_focus_id);
    }
}

static void default_file_menu(int x) {
    const app_info_t *a = focused_app();
    if (mb_focus_id < 0) {
        static const ui_menu_item_t m[] = {
            { "Nova janela do Explorador", 1, 0, "Super+E", false, false },
            { "Nova pasta na Área de Trabalho", 2, GLYPH_NEW_FOLDER, NULL, false, false },
        };
        int r = ui_menu(x, MENUBAR_H + 2, m, 2);
        if (r == 1) app_launch("explorer", NULL);
        if (r == 2) desktop_new_item(true);
        return;
    }
    ui_menu_item_t m[] = {
        { "Nova janela", 1, 0, NULL, a == NULL, false },
        { "Fechar janela", 2, GLYPH_CLOSE, "Alt+F4", false, false },
    };
    int r = ui_menu(x, MENUBAR_H + 2, m, 2);
    if (r == 1 && a) app_launch(a->id, NULL);
    if (r == 2) wm_close(mb_focus_id);
}

static void go_menu(int x) {
    static const ui_menu_item_t m[] = {
        { "Início", 1, 0, NULL, false, false },
        { "Documentos", 2, 0, NULL, false, false },
        { "Downloads", 3, 0, NULL, false, false },
        { "Imagens", 4, 0, NULL, false, false },
        { NULL, 0, 0, NULL, false, true },
        { "Computador", 5, 0, NULL, false, false },
        { "Lixeira", 6, 0, NULL, false, false },
    };
    static const char *paths[] = { NULL, "/home/usuario", "/home/usuario/Documentos", "/home/usuario/Downloads",
                                   "/home/usuario/Imagens", "/", TRASH_DIR };
    int r = ui_menu(x, MENUBAR_H + 2, m, 7);
    if (r > 0) app_launch("explorer", paths[r]);
}

static void open_menubar_item(int i) {
    rect_t r = mb_rects[i];
    if (i == 0) {
        mb_open = 0; draw_menubar();
        app_menu(r.x);
    } else {
        int idx = i - 1;
        const char *t = mb_titles[idx];
        bool custom = mb_custom_from >= 0 && idx < mb_count - 2;
        if (custom) {
            /* menu do próprio app: ele abre o menu quando receber EV_MENU */
            wm_send_menu(mb_focus_id, idx, r.x, MENUBAR_H + 2);
            return;
        }
        mb_open = i; draw_menubar();
        if (!strcmp(t, "Janela")) window_menu(r.x);
        else if (!strcmp(t, "Ajuda")) help_menu(r.x);
        else if (!strcmp(t, "Ir")) go_menu(r.x);
        else default_file_menu(r.x);
    }
    mb_open = -1;
    draw_menubar();
}

static void menubar_event(event_t *e) {
    if (e->type != EV_MOUSE_DOWN || e->button != 1) return;
    if (ui_hit(mb_logo, e)) { close_popups(NULL); system_menu(); return; }
    if (ui_hit(mb_clock, e)) { toggle_center(); return; }
    if (ui_hit(mb_cc, e)) { toggle_ccenter(); return; }
    if (ui_hit(mb_search, e)) { toggle_spot(); return; }
    for (int i = 0; i <= mb_count; i++)
        if (ui_hit(mb_rects[i], e)) { close_popups(NULL); open_menubar_item(i); return; }
}

/* ================================================================== */
/* Tela de bloqueio                                                    */
/* ================================================================== */
static ui_edit_t lock_pw;
static char lock_error[64];
static rect_t lock_btn;
static bool first_login = true;
static int lock_minute = -1;

static void text_center(canvas_t *c, int y, const char *s, uint32_t color, int flags) {
    int w = font_width(s, flags);
    font_draw(c, (c->w - w) / 2 + 1, y + 1, s, ARGB(0x70, 0x000000), flags);
    font_draw(c, (c->w - w) / 2, y, s, color, flags);
}

static void draw_lock(void) {
    canvas_t *c = win_canvas(lockwin);
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    lock_minute = dt.minute;
    char buf[64];
    gfx_blit(c, 0, 0, wallpaper, 0, 0, c->w, c->h);
    gfx_vgradient(c, 0, 0, c->w, 260, ARGB(0x50, 0x000000), ARGB(0x00, 0x000000));
    gfx_vgradient(c, 0, c->h - 260, c->w, 260, ARGB(0x00, 0x000000), ARGB(0x60, 0x000000));
    snprintf(buf, sizeof(buf), "%s, %d de %s", weekday_name(dt.weekday), dt.day, month_name(dt.month));
    /* abaixo da faixa do placar do papel de parede "Capivara" */
    text_center(c, 104, buf, ARGB(0xF0, 0xFFFFFF), FONT_2X);
    snprintf(buf, sizeof(buf), "%02d:%02d", dt.hour, dt.minute);
    text_center(c, 144, buf, HEX(0xFFFFFF), FONT_4X | FONT_BOLD);

    int cx = c->w / 2, y = c->h - 220;
    gfx_circle(c, cx, y + 32, 36, ARGB(0x60, 0xFFFFFF));
    icon_draw(c, ICON_USER, cx - 32, y, 64);
    text_center(c, y + 76, session_user(), HEX(0xFFFFFF), FONT_BOLD);
    if (session_has_password()) {
        rect_t f = R(cx - 110, y + 104, 220, 30);
        gfx_round_fill(c, f.x, f.y, f.w, f.h, 15, ARGB(0x60, 0xFFFFFF));
        gfx_round_rect(c, f.x, f.y, f.w, f.h, 15, ARGB(0x80, 0xFFFFFF));
        int n = (int)utf8_count(lock_pw.text, lock_pw.len);
        if (!n) font_draw(c, f.x + 14, f.y + 7, "Digite a senha", ARGB(0xD0, 0xFFFFFF), 0);
        for (int i = 0; i < n && i < 18; i++) gfx_circle(c, f.x + 18 + i * 10, f.y + 15, 3, HEX(0xFFFFFF));
        lock_btn = R(f.x + f.w + 6, f.y + 2, 26, 26);
        gfx_circle(c, lock_btn.x + 13, lock_btn.y + 13, 13, ARGB(0x60, 0xFFFFFF));
        glyph_draw(c, GLYPH_FORWARD, lock_btn.x + 5, lock_btn.y + 5, 16, HEX(0xFFFFFF));
    } else {
        lock_btn = R(cx - 70, y + 104, 140, 30);
        gfx_round_fill(c, lock_btn.x, lock_btn.y, lock_btn.w, lock_btn.h, 15, ARGB(0x60, 0xFFFFFF));
        gfx_round_rect(c, lock_btn.x, lock_btn.y, lock_btn.w, lock_btn.h, 15, ARGB(0x80, 0xFFFFFF));
        ui_text_center(c, lock_btn, "Entrar", HEX(0xFFFFFF), FONT_BOLD);
    }
    if (lock_error[0]) text_center(c, y + 144, lock_error, HEX(0xFFC4C4), 0);
    else text_center(c, y + 144, session_has_password() ? "Digite a senha e pressione Enter" : "Pressione Enter ou clique em Entrar",
                     ARGB(0xC0, 0xFFFFFF), 0);
    win_invalidate(lockwin);
}

static void lock_screen(void) {
    if (lockwin->visible) return;
    close_popups(NULL);
    hide_toast();
    lock_error[0] = 0;
    ui_edit_set(&lock_pw, "");
    draw_lock();
    win_show(lockwin, true);
    wm_lock(lockwin);
}

static void unlock(void) {
    wm_lock(NULL);
    win_show(lockwin, false);
    ui_edit_set(&lock_pw, "");
    draw_menubar();
    draw_dock();
    if (first_login) {
        first_login = false;
        char body[128];
        snprintf(body, sizeof(body), "Olá, %s! Pressione a tecla Windows para abrir o Launchpad.", session_user());
        notify("Capivara OS", "Bem-vindo ao Capivara OS", body, ICON_LOGO, NULL);
        if (vfs_lookup(NULL, "/home/usuario/Documentos/Bem-vindo.txt"))
            app_launch("notepad", "/home/usuario/Documentos/Bem-vindo.txt");
    }
    show_next_toast();
}

static void try_login(void) {
    if (session_check_password(lock_pw.text)) {
        unlock();
    } else {
        strlcpy(lock_error, "Senha incorreta. Tente de novo.", sizeof(lock_error));
        ui_edit_set(&lock_pw, "");
        draw_lock();
    }
}

static void lock_event(event_t *e) {
    switch (e->type) {
    case EV_MOUSE_DOWN:
        if (ui_hit(lock_btn, e)) try_login();
        break;
    case EV_KEY_DOWN:
        if (e->key == KEY_ENTER) { try_login(); break; }
        if (e->key == KEY_ESC) ui_edit_set(&lock_pw, "");
        else if (session_has_password()) ui_edit_key(&lock_pw, e);
        lock_error[0] = 0;
        draw_lock();
        break;
    }
}

/* ================================================================== */
/* Histórico da área de transferência (Super+V)                        */
/* ================================================================== */
static void clipboard_menu(void) {
    int n = clipboard_history_count();
    ui_menu_item_t m[CLIP_HISTORY + 3];
    static char labels[CLIP_HISTORY][64];
    int k = 0;
    m[k++] = (ui_menu_item_t){ "Área de transferência", 0, GLYPH_PASTE, "Super+V", true, false };
    m[k++] = (ui_menu_item_t){ NULL, 0, 0, NULL, false, true };
    if (!n) m[k++] = (ui_menu_item_t){ "Vazia: copie algo com Ctrl+C", 0, 0, NULL, true, false };
    for (int i = 0; i < n; i++) {
        int len;
        const char *s = clipboard_history_get(i, &len);
        int j = 0;
        for (int p = 0; p < len && j < 44; p++) labels[i][j++] = (s[p] == '\n' || s[p] == '\t') ? ' ' : s[p];
        while (j > 0 && ((uint8_t)labels[i][j - 1] & 0xC0) == 0x80) j--;   /* não corta um acento ao meio */
        if (j > 0 && (uint8_t)labels[i][j - 1] >= 0xC0) j--;
        labels[i][j] = 0;
        if (len > 44) strlcat(labels[i], "…", sizeof(labels[i]));
        m[k++] = (ui_menu_item_t){ labels[i], i + 1, 0, NULL, false, false };
    }
    int r = ui_menu(scr_w / 2 - 170, scr_h / 2 - 180, m, k);
    if (r <= 0) return;
    int len;
    const char *s = clipboard_history_get(r - 1, &len);
    if (!s) return;
    char *copy = kmalloc(len + 1);
    memcpy(copy, s, len);
    clipboard_set(copy, len);
    kfree(copy);
    wm_paste_to_focused();
}

/* ================================================================== */
/* Laço principal                                                      */
/* ================================================================== */
static void close_popups(window_t *except) {
    window_t *pops[] = { launchpad, spot, center, ccenter };
    for (unsigned i = 0; i < sizeof(pops) / sizeof(pops[0]); i++)
        if (pops[i] != except && pops[i]->visible) win_show(pops[i], false);
}

static void refresh_windows(void) {
    nwins = wm_list(wins, 48);
    draw_menubar();
    draw_dock();
}

static void redraw_everything(void) {
    generate_wallpaper();
    draw_desktop();
    draw_menubar();
    draw_dock();
    if (launchpad->visible) draw_launchpad();
    if (spot->visible) draw_spot();
    if (center->visible) draw_center();
    if (ccenter->visible) draw_ccenter();
    if (lockwin->visible) draw_lock();
}

static int last_trash = -1;

static void tick(void) {
    uint64_t now = timer_ms();
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    if (dt.minute != last_minute) draw_menubar();
    if (lockwin->visible && dt.minute != lock_minute) draw_lock();
    if (center->visible && now % 1000 < 250) draw_center();

    vnode_t *dir = vfs_lookup(NULL, DESK_FOLDER);
    if (dir && vfs_child_count(dir) != dfolder_count) {
        scan_desktop();
        draw_desktop();
    }
    if (trash_count() != last_trash) {
        last_trash = trash_count();
        draw_dock();
    }
    if (toast->visible && !toast_hover && now >= toast_until) {
        hide_toast();
        show_next_toast();
    }
}

static void shell_command(int cmd) {
    switch (cmd) {
    case SHELL_START: toggle_launchpad(); break;
    case SHELL_SEARCH: toggle_spot(); break;
    case SHELL_EXPLORER: app_launch("explorer", NULL); break;
    case SHELL_TASKMGR: app_launch("taskmgr", NULL); break;
    case SHELL_RUN_TERMINAL: app_launch("terminal", NULL); break;
    case SHELL_LOCK: lock_screen(); break;
    case SHELL_NOTIFY:
        draw_menubar();
        if (center->visible) { win_show(center, false); toggle_center(); }
        else show_next_toast();
        break;
    case SHELL_NOTIFICATIONS: toggle_center(); break;
    case SHELL_CLIPBOARD: clipboard_menu(); break;
    }
}

void shell_main(void *arg) {
    wm_register_shell(task_current());
    scr_w = wm_screen_w();
    scr_h = wm_screen_h();
    trash_init();

    wallpaper = canvas_create(scr_w, scr_h);
    wall_blur = canvas_create(scr_w, scr_h);
    generate_wallpaper();
    desktop = win_create("Área de Trabalho", ICON_DESKTOP, 0, 0, scr_w, scr_h, WF_DESKTOP | WF_NOTASKBAR);
    scan_desktop();
    draw_desktop();

    uint32_t popup = WF_TOPMOST | WF_POPUP | WF_SHADOW | WF_ALPHA | WF_NOTASKBAR | WF_HIDDEN;
    uint32_t bar = WF_TOPMOST | WF_NOFOCUS | WF_ALPHA | WF_NOTASKBAR;
    menubar = win_create("Barra de menus", 0, 0, 0, scr_w, MENUBAR_H, bar);
    dock = win_create("Dock", 0, 0, scr_h - DOCK_WIN_H, scr_w, DOCK_WIN_H, bar | WF_CLICKTHRU);
    launchpad = win_create("Launchpad", ICON_LAUNCHPAD, 0, 0, scr_w, scr_h,
                           WF_TOPMOST | WF_POPUP | WF_NOTASKBAR | WF_HIDDEN);
    spot = win_create("Pesquisa", ICON_LOGO, (scr_w - SPOT_W) / 2, 150, SPOT_W, 64, popup);
    center = win_create("Central de Notificações", ICON_CLOCK, scr_w - CENTER_W - 6, MENUBAR_H + 6,
                        CENTER_W, CAL_H, WF_TOPMOST | WF_POPUP | WF_ALPHA | WF_NOTASKBAR | WF_HIDDEN);
    ccenter = win_create("Central de Controle", ICON_SETTINGS, scr_w - CC_W - 6, MENUBAR_H + 6, CC_W, CC_H, popup);
    toast = win_create("Notificação", ICON_LOGO, scr_w - TOAST_W - 4, MENUBAR_H + 4, TOAST_W, TOAST_H,
                       WF_TOPMOST | WF_NOFOCUS | WF_ALPHA | WF_NOTASKBAR | WF_HIDDEN);
    lockwin = win_create("Tela de bloqueio", ICON_LOGO, 0, 0, scr_w, scr_h,
                         WF_TOPMOST | WF_NOTASKBAR | WF_HIDDEN);
    refresh_windows();
    ev_set_timer(250);

    lock_screen();                    /* o sistema começa na tela de bloqueio */

    for (;;) {
        event_t e;
        gui_wait(&e);
        switch (e.type) {
        case EV_TIMER: tick(); continue;
        case EV_WINLIST: refresh_windows(); continue;
        case EV_THEME: redraw_everything(); continue;
        case EV_SHELL: shell_command((int)e.key); continue;
        }
        if (e.win == lockwin) lock_event(&e);
        else if (e.win == desktop) desktop_event(&e);
        else if (e.win == menubar) menubar_event(&e);
        else if (e.win == dock) dock_event(&e);
        else if (e.win == launchpad) launchpad_event(&e);
        else if (e.win == spot) spot_event(&e);
        else if (e.win == center) center_event(&e);
        else if (e.win == ccenter) ccenter_event(&e);
        else if (e.win == toast) toast_event(&e);
    }
}
