/*
 * Explorador de Arquivos.
 *
 * Guarda a pasta atual como TEXTO (caminho) e a procura de novo a cada
 * redesenho: assim, se outro aplicativo apagar a pasta, o explorador não fica
 * com um ponteiro inválido.
 */
#include "apps/apps.h"
#include "gui/ui.h"
#include "fs/vfs.h"
#include "dev/rtc.h"
#include "dev/keyboard.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "lib/printf.h"
#include "apps/trash.h"

#define NAV_H    48
#define CMD_H    44
#define SIDE_W   190
#define STATUS_H 26
#define HEAD_H   30
#define ROW_H    28
#define CELL_W   104
#define CELL_H   98
#define HIST     16

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

/* área de transferência de arquivos, compartilhada entre janelas do explorador */
static char file_clip[VFS_PATH_MAX];
static bool file_clip_cut;

typedef struct { const char *label; int icon; const char *path; } place_t;
static const place_t places[] = {
    { "Início", ICON_HOME, "/home/usuario" },
    { "Área de Trabalho", ICON_DESKTOP, "/home/usuario/Área de Trabalho" },
    { "Documentos", ICON_DOCUMENTS, "/home/usuario/Documentos" },
    { "Downloads", ICON_DOWNLOADS, "/home/usuario/Downloads" },
    { "Imagens", ICON_PICTURES, "/home/usuario/Imagens" },
    { "Músicas", ICON_MUSIC, "/home/usuario/Músicas" },
    { "Lixeira", ICON_TRASH, TRASH_DIR },
    { NULL, 0, NULL },
    { "Computador", ICON_COMPUTER, "/" },
};
#define NPLACES ((int)(sizeof(places) / sizeof(places[0])))

enum { F_LIST, F_ADDRESS, F_SEARCH };
enum { B_BACK = 1, B_FWD, B_UP, B_REFRESH, B_NEWDIR, B_NEWFILE, B_CUT, B_COPY, B_PASTE, B_RENAME,
       B_DELETE, B_VIEW, B_RESTORE, B_EMPTY, B_COUNT };

typedef struct {
    window_t *win;
    char cwd[VFS_PATH_MAX];
    char back[HIST][VFS_PATH_MAX], fwd[HIST][VFS_PATH_MAX];
    int nback, nfwd;
    vnode_t *entries[512];
    ui_list_t list;
    bool icons_view, show_hidden;
    int focus;
    ui_edit_t address, search;
    int hover_btn, press_btn, hover_place, hover_row;
    rect_t btn[B_COUNT];
    rect_t crumbs[16];
    char crumb_path[16][VFS_PATH_MAX];
    int ncrumbs;
    bool quit;
    uint64_t signature;
} ex_t;

static int folder_icon(const char *path) {
    if (!strcmp(path, TRASH_DIR)) return trash_count() ? ICON_TRASH_FULL : ICON_TRASH;
    for (int i = 0; i < NPLACES; i++)
        if (places[i].path && !strcmp(places[i].path, path)) return places[i].icon;
    return ICON_FOLDER;
}

static void node_path(ex_t *x, vnode_t *n, char *out, size_t size) {
    snprintf(out, size, "%s/%s", strcmp(x->cwd, "/") ? x->cwd : "", n->name);
}

static vnode_t *cwd_node(ex_t *x) {
    vnode_t *d = vfs_lookup(NULL, x->cwd);
    if (!d || d->type != VFS_DIR) {
        strlcpy(x->cwd, "/home/usuario", sizeof(x->cwd));
        d = vfs_lookup(NULL, x->cwd);
    }
    return d;
}

static void refresh_entries(ex_t *x) {
    vnode_t *d = cwd_node(x);
    int n = vfs_child_count(d), k = 0;
    for (int i = 0; i < n && k < 512; i++) {
        vnode_t *c = vfs_child_at(d, i);
        if (!c) break;
        if (x->search.len && !strcasestr_utf8(c->name, x->search.text)) continue;
        if (c->name[0] == '.' && !x->show_hidden) continue;           /* itens ocultos */
        x->entries[k++] = c;
    }
    x->list.count = k;
    if (x->list.sel >= k) x->list.sel = k - 1;
}

/* "assinatura" do conteúdo da pasta: muda se algo for criado, apagado ou alterado */
static uint64_t folder_signature(ex_t *x) {
    vnode_t *d = cwd_node(x);
    uint64_t sig = (uint64_t)d * 31;
    vfs_lock();
    for (vnode_t *c = d->children; c; c = c->next)
        sig = sig * 131 + c->mtime + c->size + (uint64_t)c + (uint8_t)c->name[0];
    vfs_unlock();
    return sig;
}

static void navigate(ex_t *x, const char *path, bool record) {
    vnode_t *d = vfs_lookup(NULL, path);
    if (!d || d->type != VFS_DIR) {
        ui_message(x->win, "Explorador de Arquivos", "Esse caminho não existe ou não é uma pasta.", "OK", NULL, NULL);
        return;
    }
    char canon[VFS_PATH_MAX];
    vfs_path(d, canon, sizeof(canon));
    if (record && strcmp(canon, x->cwd)) {
        if (x->nback == HIST) { memmove(x->back[0], x->back[1], sizeof(x->back[0]) * (HIST - 1)); x->nback--; }
        strlcpy(x->back[x->nback++], x->cwd, VFS_PATH_MAX);
        x->nfwd = 0;
    }
    strlcpy(x->cwd, canon, sizeof(x->cwd));
    x->list.sel = -1;
    x->list.scroll = 0;
    ui_edit_set(&x->search, "");
    const char *title = !strcmp(canon, "/") ? "Computador" : strrchr(canon, '/') + 1;
    char t[96];
    snprintf(t, sizeof(t), "%s - Explorador de Arquivos", title);
    win_set_title(x->win, t);
}

static void go_back(ex_t *x) {
    if (!x->nback) return;
    strlcpy(x->fwd[x->nfwd++], x->cwd, VFS_PATH_MAX);
    char p[VFS_PATH_MAX];
    strlcpy(p, x->back[--x->nback], sizeof(p));
    navigate(x, p, false);
}

static void go_forward(ex_t *x) {
    if (!x->nfwd) return;
    strlcpy(x->back[x->nback++], x->cwd, VFS_PATH_MAX);
    char p[VFS_PATH_MAX];
    strlcpy(p, x->fwd[--x->nfwd], sizeof(p));
    navigate(x, p, false);
}

static void go_up(ex_t *x) {
    if (!strcmp(x->cwd, "/")) return;
    char p[VFS_PATH_MAX];
    strlcpy(p, x->cwd, sizeof(p));
    char *s = strrchr(p, '/');
    if (s == p) p[1] = 0; else *s = 0;
    navigate(x, p, true);
}

/* ------------------------------------------------------------------ */
/* Geometria                                                            */
/* ------------------------------------------------------------------ */
static rect_t main_rect(ex_t *x) {
    canvas_t *c = win_canvas(x->win);
    return R(SIDE_W, NAV_H + CMD_H, c->w - SIDE_W, c->h - NAV_H - CMD_H - STATUS_H);
}
static rect_t list_rect(ex_t *x) {
    rect_t m = main_rect(x);
    return x->icons_view ? R(m.x + 8, m.y + 8, m.w - 16, m.h - 16) : R(m.x, m.y + HEAD_H, m.w, m.h - HEAD_H);
}
static int grid_cols(ex_t *x) { return imax(1, list_rect(x).w / CELL_W); }
static rect_t place_rect(int i) { return R(8, NAV_H + CMD_H + 8 + i * 34 - (i > 7 ? 24 : 0), SIDE_W - 16, 32); }
static bool in_trash(ex_t *x) { return !strcmp(x->cwd, TRASH_DIR); }
static rect_t address_rect(ex_t *x) { canvas_t *c = win_canvas(x->win); return R(160, 8, c->w - 160 - 232, 32); }
static rect_t search_rect(ex_t *x) { canvas_t *c = win_canvas(x->win); return R(c->w - 222, 8, 210, 32); }

static rect_t item_rect(ex_t *x, int i) {
    rect_t lr = list_rect(x);
    if (x->icons_view) {
        int cols = grid_cols(x);
        return R(lr.x + (i % cols) * CELL_W, lr.y + (i / cols) * CELL_H - x->list.scroll, CELL_W - 4, CELL_H - 4);
    }
    return R(lr.x, lr.y + i * ROW_H - x->list.scroll, lr.w, ROW_H);
}

static int item_at(ex_t *x, int px, int py) {
    rect_t lr = list_rect(x);
    if (!rect_has(lr, px, py)) return -1;
    for (int i = 0; i < x->list.count; i++)
        if (rect_has(item_rect(x, i), px, py)) return i;
    return -1;
}

static int content_height(ex_t *x) {
    if (x->icons_view) return ((x->list.count + grid_cols(x) - 1) / grid_cols(x)) * CELL_H;
    return x->list.count * ROW_H;
}

static void clamp_scroll(ex_t *x) {
    int max = imax(0, content_height(x) - list_rect(x).h);
    x->list.scroll = imax(0, imin(max, x->list.scroll));
}

static void ensure_sel_visible(ex_t *x) {
    if (x->list.sel < 0) return;
    rect_t lr = list_rect(x), r = item_rect(x, x->list.sel);
    if (r.y < lr.y) x->list.scroll -= lr.y - r.y;
    if (r.y + r.h > lr.y + lr.h) x->list.scroll += r.y + r.h - (lr.y + lr.h);
    clamp_scroll(x);
}

/* ------------------------------------------------------------------ */
/* Desenho                                                              */
/* ------------------------------------------------------------------ */
static const char *type_name(vnode_t *n, char *buf, size_t size) {
    if (n->type == VFS_DIR) return "Pasta de arquivos";
    const char *dot = strrchr(n->name, '.');
    if (dot && !strcasecmp(dot, ".txt")) return "Documento de texto";
    if (dot && dot[1]) {
        snprintf(buf, size, "Arquivo %s", dot + 1);
        for (char *p = buf + 8; *p; p++) *p = (char)toupper(*p);
        return buf;
    }
    return "Arquivo";
}

static void fmt_size(uint64_t b, char *out, size_t n) {
    if (b < 1024) snprintf(out, n, "%lu bytes", b);
    else if (b < 1024 * 1024) snprintf(out, n, "%lu KB", (b + 1023) / 1024);
    else snprintf(out, n, "%lu,%lu MB", b >> 20, ((b >> 10) % 1024) * 10 / 1024);
}

static int node_icon(ex_t *x, vnode_t *n) {
    if (n->type != VFS_DIR) return app_icon_for(n->name, false);
    char p[VFS_PATH_MAX];
    node_path(x, n, p, sizeof(p));
    return folder_icon(p);
}

static ui_state_t bstate(ex_t *x, int b, bool enabled) {
    if (!enabled) return UI_DISABLED;
    if (x->hover_btn == b) return x->press_btn == b ? UI_PRESSED : UI_HOVER;
    return UI_NORMAL;
}

static void cmd_button(ex_t *x, canvas_t *c, int b, int glyph, const char *label, bool enabled) {
    rect_t r = x->btn[b];
    ui_state_t st = bstate(x, b, enabled);
    if (st == UI_HOVER || st == UI_PRESSED)
        gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, st == UI_PRESSED ? theme.pressed : theme.hover);
    uint32_t fg = enabled ? theme.text : theme.text_disabled;
    glyph_draw(c, glyph, r.x + 8, r.y + (r.h - 16) / 2, 16, fg);
    if (label) font_draw(c, r.x + 32, r.y + (r.h - 16) / 2, label, fg, 0);
}

static void layout_buttons(ex_t *x) {
    canvas_t *c = win_canvas(x->win);
    x->btn[B_BACK] = R(8, 8, 32, 32);
    x->btn[B_FWD] = R(42, 8, 32, 32);
    x->btn[B_UP] = R(76, 8, 32, 32);
    x->btn[B_REFRESH] = R(110, 8, 32, 32);
    int y = NAV_H + 4, bx = 8;
    x->btn[B_NEWDIR] = R(bx, y, 32 + font_width("Nova pasta", 0) + 10, 34); bx += x->btn[B_NEWDIR].w + 4;
    x->btn[B_NEWFILE] = R(bx, y, 32 + font_width("Novo arquivo", 0) + 10, 34); bx += x->btn[B_NEWFILE].w + 12;
    for (int b = B_CUT; b <= B_DELETE; b++) { x->btn[b] = R(bx, y, 34, 34); bx += 36; }
    int vw = 32 + font_width("Detalhes", 0) + 24;
    x->btn[B_VIEW] = R(c->w - vw - 8, y, vw, 34);
    bx += 8;
    if (in_trash(x)) {
        x->btn[B_RESTORE] = R(bx, y, 32 + font_width("Restaurar", 0) + 10, 34); bx += x->btn[B_RESTORE].w + 4;
        x->btn[B_EMPTY] = R(bx, y, 32 + font_width("Esvaziar Lixeira", 0) + 10, 34);
    } else {
        x->btn[B_RESTORE] = x->btn[B_EMPTY] = R(0, 0, 0, 0);
    }
}

static void draw(ex_t *x) {
    canvas_t *c = win_canvas(x->win);
    layout_buttons(x);
    refresh_entries(x);
    clamp_scroll(x);
    gfx_clear(c, theme.window_bg);
    bool has_sel = x->list.sel >= 0 && x->list.sel < x->list.count;

    /* barra de navegação */
    ui_glyph_button(c, x->btn[B_BACK], GLYPH_BACK, bstate(x, B_BACK, x->nback > 0));
    ui_glyph_button(c, x->btn[B_FWD], GLYPH_FORWARD, bstate(x, B_FWD, x->nfwd > 0));
    ui_glyph_button(c, x->btn[B_UP], GLYPH_UP, bstate(x, B_UP, strcmp(x->cwd, "/") != 0));
    ui_glyph_button(c, x->btn[B_REFRESH], GLYPH_REFRESH, bstate(x, B_REFRESH, true));

    rect_t ar = address_rect(x);
    if (x->focus == F_ADDRESS) {
        ui_edit_draw(c, ar, &x->address, true, NULL);
    } else {
        gfx_round_fill(c, ar.x, ar.y, ar.w, ar.h, 4, theme.control);
        gfx_round_rect(c, ar.x, ar.y, ar.w, ar.h, 4, theme.control_border);
        icon_draw(c, folder_icon(x->cwd), ar.x + 8, ar.y + 8, 16);
        /* trilha clicável: Computador > home > usuario > ... */
        x->ncrumbs = 0;
        int cx = ar.x + 32;
        rect_t saved = c->clip;
        canvas_clip(c, rect_intersect(saved, R(ar.x + 1, ar.y, ar.w - 2, ar.h)));
        char acc[VFS_PATH_MAX] = "";
        const char *p = x->cwd;
        for (int seg = 0; seg < 16; seg++) {
            char name[VFS_NAME_MAX];
            if (seg == 0) {
                strlcpy(name, "Computador", sizeof(name));
                strlcpy(acc, "/", sizeof(acc));
            } else {
                while (*p == '/') p++;
                if (!*p) break;
                const char *e = strchr(p, '/');
                int len = e ? (int)(e - p) : (int)strlen(p);
                memcpy(name, p, len);
                name[len] = 0;
                if (strcmp(acc, "/")) strlcat(acc, "/", sizeof(acc));
                strlcat(acc, name, sizeof(acc));
                p += len;
                glyph_draw(c, GLYPH_CHEVRON_RIGHT, cx, ar.y + 9, 14, theme.text_dim);
                cx += 18;
            }
            int w = font_width(name, 0) + 12;
            x->crumbs[x->ncrumbs] = R(cx, ar.y + 3, w, ar.h - 6);
            strlcpy(x->crumb_path[x->ncrumbs], acc, VFS_PATH_MAX);
            if (x->hover_btn == 100 + x->ncrumbs) gfx_round_fill(c, cx, ar.y + 3, w, ar.h - 6, 4, theme.hover);
            font_draw(c, cx + 6, ar.y + 8, name, theme.text, 0);
            x->ncrumbs++;
            cx += w;
        }
        c->clip = saved;
    }
    rect_t sr = search_rect(x);
    ui_edit_draw(c, sr, &x->search, x->focus == F_SEARCH, "Pesquisar nesta pasta");
    if (!x->search.len && x->focus != F_SEARCH) glyph_draw(c, GLYPH_SEARCH, sr.x + sr.w - 26, sr.y + 8, 16, theme.text_dim);
    gfx_hline(c, 0, NAV_H - 1, c->w, theme.border);

    /* barra de comandos */
    cmd_button(x, c, B_NEWDIR, GLYPH_NEW_FOLDER, "Nova pasta", !in_trash(x));
    cmd_button(x, c, B_NEWFILE, GLYPH_NEW_FILE, "Novo arquivo", !in_trash(x));
    gfx_vline(c, x->btn[B_CUT].x - 7, NAV_H + 10, 22, theme.border);
    cmd_button(x, c, B_CUT, GLYPH_CUT, NULL, has_sel);
    cmd_button(x, c, B_COPY, GLYPH_COPY, NULL, has_sel);
    cmd_button(x, c, B_PASTE, GLYPH_PASTE, NULL, file_clip[0] != 0);
    cmd_button(x, c, B_RENAME, GLYPH_RENAME, NULL, has_sel);
    cmd_button(x, c, B_DELETE, GLYPH_DELETE, NULL, has_sel);
    cmd_button(x, c, B_VIEW, GLYPH_TASKVIEW, x->icons_view ? "Ícones" : "Detalhes", true);
    glyph_draw(c, GLYPH_CHEVRON_DOWN, x->btn[B_VIEW].x + x->btn[B_VIEW].w - 20, x->btn[B_VIEW].y + 11, 12, theme.text);
    if (in_trash(x)) {
        cmd_button(x, c, B_RESTORE, GLYPH_RESTORE, "Restaurar", has_sel);
        cmd_button(x, c, B_EMPTY, GLYPH_DELETE, "Esvaziar Lixeira", x->list.count > 0);
    }
    gfx_hline(c, 0, NAV_H + CMD_H - 1, c->w, theme.border);

    /* painel lateral */
    for (int i = 0; i < NPLACES; i++) {
        if (!places[i].label) {
            gfx_hline(c, 16, place_rect(i).y + 4, SIDE_W - 32, theme.border);
            continue;
        }
        rect_t r = place_rect(i);
        bool cur = !strcmp(places[i].path, x->cwd);
        if (cur || x->hover_place == i) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, cur ? theme.pressed : theme.hover);
        if (cur) gfx_round_fill(c, r.x, r.y + 8, 3, r.h - 16, 1.5f, theme.accent);
        icon_draw(c, places[i].icon, r.x + 10, r.y + 8, 16);
        font_draw(c, r.x + 36, r.y + 8, places[i].label, theme.text, 0);
    }

    /* conteúdo */
    rect_t m = main_rect(x), lr = list_rect(x);
    gfx_fill(c, m.x, m.y, m.w, m.h, theme.surface);
    gfx_vline(c, m.x, m.y, m.h, theme.border);
    if (!x->icons_view) {
        const char *heads[] = { "Nome", "Data de modificação", in_trash(x) ? "Local original" : "Tipo", "Tamanho" };
        int colx[4] = { m.x + 16, m.x + m.w * 46 / 100, m.x + m.w * 68 / 100, m.x + m.w * 87 / 100 };
        for (int i = 0; i < 4; i++) {
            font_draw(c, colx[i], m.y + 7, heads[i], theme.text_dim, 0);
            if (i) gfx_vline(c, colx[i] - 8, m.y + 6, 18, theme.border);
        }
        gfx_hline(c, m.x, m.y + HEAD_H - 1, m.w, theme.border);
        rect_t saved = c->clip;
        canvas_clip(c, lr);
        for (int i = 0; i < x->list.count; i++) {
            rect_t r = item_rect(x, i);
            if (r.y + r.h < lr.y) continue;
            if (r.y > lr.y + lr.h) break;
            vnode_t *n = x->entries[i];
            if (i == x->list.sel) gfx_round_fill(c, r.x + 4, r.y + 1, r.w - 8, r.h - 2, 4, theme.selection);
            else if (i == x->hover_row) gfx_round_fill(c, r.x + 4, r.y + 1, r.w - 8, r.h - 2, 4, theme.hover);
            icon_draw(c, node_icon(x, n), colx[0], r.y + 6, 16);
            font_draw_ellipsis(c, colx[0] + 24, r.y + 6, colx[1] - colx[0] - 36, n->name, theme.text, 0);
            datetime_t dt;
            time_to_datetime(n->mtime, &dt);
            char buf[48];
            snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", dt.day, dt.month, dt.year, dt.hour, dt.minute);
            font_draw(c, colx[1], r.y + 6, buf, theme.text_dim, 0);
            if (in_trash(x)) {
                char origin[VFS_PATH_MAX];
                if (trash_origin(n->name, origin, sizeof(origin))) {
                    char *sl = strrchr(origin, '/');
                    if (sl && sl != origin) *sl = 0;
                } else strlcpy(origin, "?", sizeof(origin));
                font_draw_ellipsis(c, colx[2], r.y + 6, colx[3] - colx[2] - 12, origin, theme.text_dim, 0);
            } else
                font_draw_ellipsis(c, colx[2], r.y + 6, colx[3] - colx[2] - 12, type_name(n, buf, sizeof(buf)), theme.text_dim, 0);
            if (n->type == VFS_FILE) {
                fmt_size(n->size, buf, sizeof(buf));
                font_draw(c, colx[3], r.y + 6, buf, theme.text_dim, 0);
            }
        }
        c->clip = saved;
    } else {
        rect_t saved = c->clip;
        canvas_clip(c, R(m.x + 1, m.y, m.w - 1, m.h));
        for (int i = 0; i < x->list.count; i++) {
            rect_t r = item_rect(x, i);
            if (r.y + r.h < m.y || r.y > m.y + m.h) continue;
            vnode_t *n = x->entries[i];
            if (i == x->list.sel) gfx_round_fill(c, r.x, r.y, r.w, r.h, 6, theme.selection);
            else if (i == x->hover_row) gfx_round_fill(c, r.x, r.y, r.w, r.h, 6, theme.hover);
            icon_draw(c, node_icon(x, n), r.x + (r.w - 48) / 2, r.y + 8, 48);
            int tw = imin(font_width(n->name, 0), r.w - 8);
            font_draw_ellipsis(c, r.x + (r.w - tw) / 2, r.y + 64, r.w - 8, n->name, theme.text, 0);
        }
        c->clip = saved;
    }
    if (!x->list.count)
        ui_text_center(c, R(m.x, m.y + 40, m.w, 40), x->search.len ? "Nenhum item corresponde à pesquisa." : "Esta pasta está vazia.",
                       theme.text_dim, 0);
    ui_scrollbar(c, R(lr.x, lr.y + 2, lr.w - 1, lr.h - 4), content_height(x), lr.h, x->list.scroll);

    /* barra de status */
    int sy = c->h - STATUS_H;
    gfx_hline(c, 0, sy, c->w, theme.border);
    char st[128];
    int n = snprintf(st, sizeof(st), "%d %s", x->list.count, x->list.count == 1 ? "item" : "itens");
    if (has_sel) {
        vnode_t *s = x->entries[x->list.sel];
        n += snprintf(st + n, sizeof(st) - n, "    |    1 item selecionado");
        if (s->type == VFS_FILE) {
            char sz[32];
            fmt_size(s->size, sz, sizeof(sz));
            snprintf(st + n, sizeof(st) - n, "  %s", sz);
        }
    }
    font_draw(c, 12, sy + 5, st, theme.text_dim, 0);
    win_invalidate(x->win);
}

/* ------------------------------------------------------------------ */
/* Ações                                                                */
/* ------------------------------------------------------------------ */
static vnode_t *selected(ex_t *x) {
    return (x->list.sel >= 0 && x->list.sel < x->list.count) ? x->entries[x->list.sel] : NULL;
}

static void select_name(ex_t *x, const char *name) {
    refresh_entries(x);
    for (int i = 0; i < x->list.count; i++)
        if (!strcmp(x->entries[i]->name, name)) { x->list.sel = i; ensure_sel_visible(x); return; }
}

static void open_item(ex_t *x, vnode_t *n) {
    char p[VFS_PATH_MAX];
    node_path(x, n, p, sizeof(p));
    if (n->type == VFS_DIR) navigate(x, p, true);
    else app_open_path(p);
}

static void new_item(ex_t *x, bool folder) {
    char name[VFS_NAME_MAX];
    strlcpy(name, folder ? "Nova pasta" : "Novo documento de texto.txt", sizeof(name));
    if (!ui_input(x->win, folder ? "Nova pasta" : "Novo arquivo", "Nome:", name, sizeof(name)) || !name[0]) return;
    if (strchr(name, '/')) { ui_message(x->win, "Nome inválido", "O nome não pode conter '/'.", "OK", NULL, NULL); return; }
    vnode_t *d = cwd_node(x);
    if (vfs_lookup(d, name)) { ui_message(x->win, "Explorador de Arquivos", "Já existe um item com esse nome.", "OK", NULL, NULL); return; }
    vfs_create(d, name, folder ? VFS_DIR : VFS_FILE);
    select_name(x, name);
}

static void rename_item(ex_t *x) {
    vnode_t *n = selected(x);
    if (!n) return;
    char name[VFS_NAME_MAX];
    strlcpy(name, n->name, sizeof(name));
    if (!ui_input(x->win, "Renomear", "Novo nome:", name, sizeof(name)) || !name[0] || !strcmp(name, n->name)) return;
    if (vfs_rename(n, name)) ui_message(x->win, "Renomear", "Não foi possível renomear (nome inválido ou já existe).", "OK", NULL, NULL);
    else select_name(x, name);
}

static void delete_item(ex_t *x, bool permanent) {
    vnode_t *n = selected(x);
    if (!n) return;
    char path[VFS_PATH_MAX];
    node_path(x, n, path, sizeof(path));
    if (!permanent && !in_trash(x)) {
        trash_move(path);                     /* vai para a Lixeira: dá para restaurar */
        x->list.sel = -1;
        return;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "Excluir permanentemente \"%s\"%s? Isso não pode ser desfeito.", n->name,
             n->type == VFS_DIR ? " e tudo o que há dentro" : "");
    if (ui_message(x->win, "Excluir", msg, "Excluir", "Cancelar", NULL) != 1) return;
    if (in_trash(x)) trash_delete(n->name);
    else vfs_remove(n);
    x->list.sel = -1;
}

static void restore_item(ex_t *x) {
    vnode_t *n = selected(x);
    if (!n || !in_trash(x)) return;
    char name[VFS_NAME_MAX];
    strlcpy(name, n->name, sizeof(name));
    if (!trash_restore(name)) ui_message(x->win, "Restaurar", "Não foi possível restaurar o item.", "OK", NULL, NULL);
    x->list.sel = -1;
}

static void empty_trash(ex_t *x) {
    if (!trash_count()) return;
    if (ui_message(x->win, "Esvaziar Lixeira", "Excluir permanentemente todos os itens da Lixeira?",
                   "Esvaziar", "Cancelar", NULL) == 1)
        trash_empty();
    x->list.sel = -1;
}

static void view_menu(ex_t *x) {
    rect_t cr = win_client_rect(x->win), b = x->btn[B_VIEW];
    ui_menu_item_t m[] = {
        { "Detalhes", 1, x->icons_view ? 0 : GLYPH_CHECK, NULL, false, false },
        { "Ícones grandes", 2, x->icons_view ? GLYPH_CHECK : 0, NULL, false, false },
        { NULL, 0, 0, NULL, false, true },
        { "Itens ocultos", 3, x->show_hidden ? GLYPH_CHECK : 0, NULL, false, false },
    };
    switch (ui_menu(cr.x + b.x, cr.y + b.y + b.h + 4, m, 4)) {
    case 1: x->icons_view = false; x->list.scroll = 0; break;
    case 2: x->icons_view = true; x->list.scroll = 0; break;
    case 3: x->show_hidden = !x->show_hidden; x->list.sel = -1; break;
    }
}

static void clip_item(ex_t *x, bool cut) {
    vnode_t *n = selected(x);
    if (!n) return;
    node_path(x, n, file_clip, sizeof(file_clip));
    file_clip_cut = cut;
}

static void paste_item(ex_t *x) {
    if (!file_clip[0] || in_trash(x)) return;
    vnode_t *src = vfs_lookup(NULL, file_clip);
    vnode_t *d = cwd_node(x);
    if (!src) { file_clip[0] = 0; return; }
    if (vfs_is_ancestor(src, d)) {
        ui_message(x->win, "Colar", "Não é possível colar uma pasta dentro dela mesma.", "OK", NULL, NULL);
        return;
    }
    if (file_clip_cut) {
        if (src->parent != d && vfs_move(src, d))
            ui_message(x->win, "Mover", "Já existe um item com esse nome aqui.", "OK", NULL, NULL);
        else select_name(x, src->name);
        file_clip[0] = 0;
    } else {
        vnode_t *copy = vfs_copy(src, d);
        if (copy) select_name(x, copy->name);
    }
}

static void properties(ex_t *x) {
    vnode_t *n = selected(x);
    char msg[512], p[VFS_PATH_MAX], sz[32], tb[48];
    if (!n) {
        vnode_t *d = cwd_node(x);
        snprintf(msg, sizeof(msg), "Pasta: %s\nItens: %d", x->cwd, vfs_child_count(d));
    } else {
        node_path(x, n, p, sizeof(p));
        datetime_t dt;
        time_to_datetime(n->mtime, &dt);
        fmt_size(n->size, sz, sizeof(sz));
        snprintf(msg, sizeof(msg), "Nome: %s\nTipo: %s\nLocal: %s\n%s%s%sModificado em: %02d/%02d/%04d às %02d:%02d",
                 n->name, type_name(n, tb, sizeof(tb)), p, n->type == VFS_FILE ? "Tamanho: " : "",
                 n->type == VFS_FILE ? sz : "", n->type == VFS_FILE ? "\n" : "",
                 dt.day, dt.month, dt.year, dt.hour, dt.minute);
    }
    ui_message(x->win, "Propriedades", msg, "OK", NULL, NULL);
}

static void button_action(ex_t *x, int b) {
    switch (b) {
    case B_BACK: go_back(x); break;
    case B_FWD: go_forward(x); break;
    case B_UP: go_up(x); break;
    case B_REFRESH: break;
    case B_NEWDIR: new_item(x, true); break;
    case B_NEWFILE: new_item(x, false); break;
    case B_CUT: clip_item(x, true); break;
    case B_COPY: clip_item(x, false); break;
    case B_PASTE: paste_item(x); break;
    case B_RENAME: rename_item(x); break;
    case B_DELETE: delete_item(x, false); break;
    case B_VIEW: view_menu(x); break;
    case B_RESTORE: restore_item(x); break;
    case B_EMPTY: empty_trash(x); break;
    }
}

static void context_menu(ex_t *x, int sx, int sy) {
    enum { C_OPEN = 1, C_CUT, C_COPY, C_PASTE, C_RENAME, C_DELETE, C_PROPS, C_NEWDIR, C_NEWFILE, C_TERM, C_REFRESH,
           C_RESTORE, C_PURGE, C_EMPTY };
    int r;
    if (in_trash(x)) {
        if (selected(x)) {
            ui_menu_item_t m[] = {
                { "Restaurar", C_RESTORE, GLYPH_RESTORE, NULL, false, false },
                { "Excluir permanentemente", C_PURGE, GLYPH_DELETE, "Del", false, false },
                { NULL, 0, 0, NULL, false, true },
                { "Propriedades", C_PROPS, 0, NULL, false, false },
            };
            r = ui_menu(sx, sy, m, 4);
        } else {
            ui_menu_item_t m[] = { { "Esvaziar Lixeira", C_EMPTY, GLYPH_DELETE, NULL, x->list.count == 0, false } };
            r = ui_menu(sx, sy, m, 1);
        }
        if (r == C_RESTORE) restore_item(x);
        else if (r == C_PURGE) delete_item(x, true);
        else if (r == C_EMPTY) empty_trash(x);
        else if (r == C_PROPS) properties(x);
        return;
    }
    if (selected(x)) {
        ui_menu_item_t m[] = {
            { "Abrir", C_OPEN, GLYPH_OPEN, "Enter", false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Recortar", C_CUT, GLYPH_CUT, "Ctrl+X", false, false },
            { "Copiar", C_COPY, GLYPH_COPY, "Ctrl+C", false, false },
            { "Renomear", C_RENAME, GLYPH_RENAME, "F2", false, false },
            { "Excluir", C_DELETE, GLYPH_DELETE, "Del", false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Propriedades", C_PROPS, 0, "Alt+Enter", false, false },
        };
        r = ui_menu(sx, sy, m, 8);
    } else {
        ui_menu_item_t m[] = {
            { "Nova pasta", C_NEWDIR, GLYPH_NEW_FOLDER, NULL, false, false },
            { "Novo arquivo de texto", C_NEWFILE, GLYPH_NEW_FILE, NULL, false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Colar", C_PASTE, GLYPH_PASTE, "Ctrl+V", file_clip[0] == 0, false },
            { "Abrir no Terminal", C_TERM, 0, NULL, false, false },
            { "Atualizar", C_REFRESH, GLYPH_REFRESH, "F5", false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Propriedades", C_PROPS, 0, NULL, false, false },
        };
        r = ui_menu(sx, sy, m, 8);
    }
    switch (r) {
    case C_OPEN: open_item(x, selected(x)); break;
    case C_CUT: clip_item(x, true); break;
    case C_COPY: clip_item(x, false); break;
    case C_PASTE: paste_item(x); break;
    case C_RENAME: rename_item(x); break;
    case C_DELETE: delete_item(x, false); break;
    case C_PROPS: properties(x); break;
    case C_NEWDIR: new_item(x, true); break;
    case C_NEWFILE: new_item(x, false); break;
    case C_TERM: app_launch("terminal", x->cwd); break;
    }
}

/* menus da barra do topo: Arquivo | Editar | Exibir | Ir */
static void menubar_menu(ex_t *x, int which, int sx, int sy) {
    bool sel = selected(x) != NULL, trash = in_trash(x);
    int r = 0;
    if (which == 0) {
        ui_menu_item_t m[] = {
            { "Nova janela", 1, 0, "Super+E", false, false },
            { "Nova pasta", 2, GLYPH_NEW_FOLDER, NULL, trash, false },
            { "Novo arquivo de texto", 3, GLYPH_NEW_FILE, NULL, trash, false },
            { NULL, 0, 0, NULL, false, true },
            { "Abrir no Terminal", 4, 0, NULL, false, false },
            { "Propriedades", 5, 0, "Alt+Enter", false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Fechar janela", 6, GLYPH_CLOSE, "Alt+F4", false, false },
        };
        r = ui_menu(sx, sy, m, 8);
        switch (r) {
        case 1: app_launch("explorer", x->cwd); break;
        case 2: new_item(x, true); break;
        case 3: new_item(x, false); break;
        case 4: app_launch("terminal", x->cwd); break;
        case 5: properties(x); break;
        case 6: x->quit = true; break;
        }
    } else if (which == 1) {
        ui_menu_item_t m[] = {
            { "Recortar", 1, GLYPH_CUT, "Ctrl+X", !sel || trash, false },
            { "Copiar", 2, GLYPH_COPY, "Ctrl+C", !sel || trash, false },
            { "Colar", 3, GLYPH_PASTE, "Ctrl+V", !file_clip[0] || trash, false },
            { "Renomear", 4, GLYPH_RENAME, "F2", !sel || trash, false },
            { NULL, 0, 0, NULL, false, true },
            { trash ? "Excluir permanentemente" : "Mover para a Lixeira", 5, GLYPH_DELETE, "Del", !sel, false },
        };
        r = ui_menu(sx, sy, m, 6);
        switch (r) {
        case 1: clip_item(x, true); break;
        case 2: clip_item(x, false); break;
        case 3: paste_item(x); break;
        case 4: rename_item(x); break;
        case 5: delete_item(x, false); break;
        }
    } else if (which == 2) {
        ui_menu_item_t m[] = {
            { "Ícones grandes", 1, x->icons_view ? GLYPH_CHECK : 0, NULL, false, false },
            { "Detalhes", 2, x->icons_view ? 0 : GLYPH_CHECK, NULL, false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Mostrar itens ocultos", 3, x->show_hidden ? GLYPH_CHECK : 0, NULL, false, false },
        };
        r = ui_menu(sx, sy, m, 4);
        if (r == 1) { x->icons_view = true; x->list.scroll = 0; }
        if (r == 2) { x->icons_view = false; x->list.scroll = 0; }
        if (r == 3) { x->show_hidden = !x->show_hidden; x->list.sel = -1; }
    } else if (which == 3) {
        ui_menu_item_t m[] = {
            { "Voltar", 1, GLYPH_BACK, "Alt+←", x->nback == 0, false },
            { "Avançar", 2, GLYPH_FORWARD, "Alt+→", x->nfwd == 0, false },
            { "Pasta acima", 3, GLYPH_UP, "Alt+↑", !strcmp(x->cwd, "/"), false },
            { NULL, 0, 0, NULL, false, true },
            { "Início", 4, 0, NULL, false, false },
            { "Documentos", 5, 0, NULL, false, false },
            { "Downloads", 6, 0, NULL, false, false },
            { "Imagens", 7, 0, NULL, false, false },
            { "Computador", 8, 0, NULL, false, false },
            { "Lixeira", 9, 0, NULL, false, false },
        };
        static const char *paths[] = { NULL, NULL, NULL, NULL, "/home/usuario", "/home/usuario/Documentos",
                                       "/home/usuario/Downloads", "/home/usuario/Imagens", "/", TRASH_DIR };
        r = ui_menu(sx, sy, m, 10);
        if (r == 1) go_back(x);
        else if (r == 2) go_forward(x);
        else if (r == 3) go_up(x);
        else if (r >= 4) navigate(x, paths[r], true);
    }
}

static void move_sel(ex_t *x, int delta) {
    if (!x->list.count) return;
    int s = x->list.sel < 0 ? 0 : x->list.sel + delta;
    x->list.sel = imax(0, imin(x->list.count - 1, s));
    ensure_sel_visible(x);
}

static void key(ex_t *x, event_t *e) {
    bool ctrl = e->mods & MOD_CTRL, alt = e->mods & MOD_ALT;
    if (x->focus == F_ADDRESS) {
        if (e->key == KEY_ENTER) { x->focus = F_LIST; navigate(x, x->address.text, true); }
        else if (e->key == KEY_ESC) x->focus = F_LIST;
        else ui_edit_key(&x->address, e);
        return;
    }
    if (x->focus == F_SEARCH) {
        if (e->key == KEY_ESC) { ui_edit_set(&x->search, ""); x->focus = F_LIST; }
        else if (e->key == KEY_ENTER || e->key == KEY_DOWN) { x->focus = F_LIST; x->list.sel = 0; }
        else if (ui_edit_key(&x->search, e)) x->list.sel = -1;
        return;
    }
    if (alt && e->key == KEY_LEFT) { go_back(x); return; }
    if (alt && e->key == KEY_RIGHT) { go_forward(x); return; }
    if (alt && e->key == KEY_UP) { go_up(x); return; }
    if (alt && e->key == KEY_ENTER) { properties(x); return; }
    if (ctrl && e->key == KEY_CHAR) {
        switch (tolower((int)e->ch)) {
        case 'c': clip_item(x, false); break;
        case 'x': clip_item(x, true); break;
        case 'v': paste_item(x); break;
        case 'f': x->focus = F_SEARCH; break;
        case 'l': x->focus = F_ADDRESS; ui_edit_set(&x->address, x->cwd); break;
        }
        return;
    }
    int cols = x->icons_view ? grid_cols(x) : 1;
    switch (e->key) {
    case KEY_UP: move_sel(x, -cols); break;
    case KEY_DOWN: move_sel(x, cols); break;
    case KEY_LEFT: if (x->icons_view) move_sel(x, -1); break;
    case KEY_RIGHT: if (x->icons_view) move_sel(x, 1); break;
    case KEY_HOME: x->list.sel = 0; ensure_sel_visible(x); break;
    case KEY_END: x->list.sel = x->list.count - 1; ensure_sel_visible(x); break;
    case KEY_ENTER: if (selected(x)) open_item(x, selected(x)); break;
    case KEY_BACKSPACE: go_up(x); break;
    case KEY_DELETE: delete_item(x, (e->mods & MOD_SHIFT) != 0); break;
    case KEY_F2: rename_item(x); break;
    case KEY_CHAR:
        /* digitar uma letra pula para o primeiro item que começa com ela */
        if (e->ch > 32) {
            for (int k = 1; k <= x->list.count; k++) {
                int i = (x->list.sel + k + x->list.count) % x->list.count;
                const char *nm = x->entries[i]->name;
                uint32_t c0 = utf8_decode(&nm);
                if (tolower((int)c0) == tolower((int)e->ch)) { x->list.sel = i; ensure_sel_visible(x); break; }
            }
        }
        break;
    }
}

static int button_at(ex_t *x, event_t *e) {
    for (int b = 1; b < B_COUNT; b++)
        if (ui_hit(x->btn[b], e)) return b;
    if (x->focus != F_ADDRESS)
        for (int i = 0; i < x->ncrumbs; i++)
            if (ui_hit(x->crumbs[i], e)) return 100 + i;
    return 0;
}

void explorer_main(void *arg) {
    ex_t *x = kcalloc(1, sizeof(ex_t));
    x->list.sel = -1;
    x->list.row_h = ROW_H;
    x->hover_place = x->hover_row = -1;
    strlcpy(x->cwd, "/home/usuario", sizeof(x->cwd));
    x->win = win_create("Explorador de Arquivos", ICON_EXPLORER, WIN_AUTO, WIN_AUTO, 880, 540, WF_APP);
    win_set_min_size(x->win, 560, 300);
    win_set_menus(x->win, "Arquivo|Editar|Exibir|Ir");
    navigate(x, arg ? (const char *)arg : "/home/usuario", false);
    if (arg) kfree(arg);
    ev_set_timer(2000);   /* atualiza se outro programa mexer na pasta */
    draw(x);

    while (!x->quit) {
        event_t e;
        gui_wait(&e);
        switch (e.type) {
        case EV_MOUSE_MOVE: {
            int hb = button_at(x, &e), hp = -1, hr = item_at(x, e.x, e.y);
            for (int i = 0; i < NPLACES; i++)
                if (places[i].label && ui_hit(place_rect(i), &e)) hp = i;
            bool text = ui_hit(search_rect(x), &e) || (x->focus == F_ADDRESS && ui_hit(address_rect(x), &e));
            win_set_cursor(x->win, text ? CURSOR_IBEAM : CURSOR_ARROW);
            if (hb == x->hover_btn && hp == x->hover_place && hr == x->hover_row) continue;
            x->hover_btn = hb;
            x->hover_place = hp;
            x->hover_row = hr;
            break;
        }
        case EV_MOUSE_LEAVE:
            x->hover_btn = 0;
            x->hover_place = x->hover_row = -1;
            break;
        case EV_MOUSE_DOWN: {
            x->press_btn = button_at(x, &e);
            rect_t ar = address_rect(x), sr = search_rect(x);
            if (ui_hit(sr, &e)) { x->focus = F_SEARCH; ui_edit_click(&x->search, sr, e.x); break; }
            if (ui_hit(ar, &e)) {
                if (x->press_btn >= 100) break;
                if (x->focus != F_ADDRESS) { x->focus = F_ADDRESS; ui_edit_set(&x->address, x->cwd); }
                else ui_edit_click(&x->address, ar, e.x);
                break;
            }
            x->focus = F_LIST;
            for (int i = 0; i < NPLACES; i++)
                if (places[i].label && ui_hit(place_rect(i), &e)) navigate(x, places[i].path, true);
            if (ui_hit(main_rect(x), &e)) {
                int i = item_at(x, e.x, e.y);
                x->list.sel = i;
                if (e.button == 2) {
                    rect_t cr = win_client_rect(x->win);
                    draw(x);
                    context_menu(x, cr.x + e.x, cr.y + e.y);
                } else if (i >= 0 && e.clicks == 2) {
                    open_item(x, x->entries[i]);
                }
            }
            break;
        }
        case EV_MOUSE_UP: {
            int b = button_at(x, &e);
            if (b && b == x->press_btn) {
                if (b >= 100) navigate(x, x->crumb_path[b - 100], true);
                else button_action(x, b);
            }
            x->press_btn = 0;
            break;
        }
        case EV_MENU:
            menubar_menu(x, (int)e.key, e.x, e.y);
            if (x->quit) continue;
            break;
        case EV_MOUSE_WHEEL:
            x->list.scroll -= e.wheel * ROW_H * 3;
            clamp_scroll(x);
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_F5) break;
            key(x, &e);
            break;
        case EV_TIMER: {
            uint64_t sig = folder_signature(x);
            if (sig == x->signature) continue;
            x->signature = sig;
            break;
        }
        case EV_RESIZE: case EV_THEME: case EV_FOCUS: case EV_BLUR:
            break;
        case EV_CLOSE:
            x->quit = true;
            continue;
        default:
            continue;
        }
        draw(x);
    }
    win_destroy(x->win);
    kfree(x);
}
