/*
 * Bloco de Notas: editor de texto UTF-8.
 *
 * O texto fica num único buffer contíguo; inserir/apagar move o restante com
 * memmove (simples e suficiente para arquivos pequenos). Uma tabela com o
 * início de cada linha é recalculada depois de cada alteração.
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

#define MENU_H   0
#define STATUS_H 26
#define TPAD     8

typedef struct {
    window_t *win;
    char *buf;
    int len, cap;
    int cur, anchor;            /* seleção = [min(cur,anchor), max(cur,anchor)) */
    int *lines;
    int nlines, lines_cap;
    int top;                    /* primeira linha visível */
    int scroll_x;               /* em pixels */
    int want_col;               /* coluna desejada ao subir/descer */
    char path[VFS_PATH_MAX];
    bool modified, selecting, cursor_on, zoom, quit;
    int menu_hover;
    rect_t cursor_rect;
} np_t;

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

static int cw(np_t *n) { return n->zoom ? 16 : 8; }
static int lh(np_t *n) { return n->zoom ? 34 : 18; }
static int fflags(np_t *n) { return FONT_MONO | (n->zoom ? FONT_2X : 0); }

static rect_t text_rect(np_t *n) {
    canvas_t *c = win_canvas(n->win);
    return R(0, MENU_H, c->w, c->h - MENU_H - STATUS_H);
}

static void recompute_lines(np_t *n) {
    n->nlines = 0;
    for (int i = 0; i <= n->len; i++) {
        if (i == 0 || n->buf[i - 1] == '\n') {
            if (n->nlines == n->lines_cap) {
                n->lines_cap = n->lines_cap ? n->lines_cap * 2 : 256;
                n->lines = krealloc(n->lines, n->lines_cap * sizeof(int));
            }
            n->lines[n->nlines++] = i;
        }
    }
}

static int line_of(np_t *n, int pos) {
    int lo = 0, hi = n->nlines - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (n->lines[mid] <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static int line_end(np_t *n, int line) {
    return line + 1 < n->nlines ? n->lines[line + 1] - 1 : n->len;
}

static int col_of(np_t *n, int pos) {
    int l = line_of(n, pos);
    return (int)utf8_count(n->buf + n->lines[l], pos - n->lines[l]);
}

static int pos_at(np_t *n, int line, int col) {
    if (line < 0) return 0;
    if (line >= n->nlines) return n->len;
    int p = n->lines[line], end = line_end(n, line);
    while (col-- > 0 && p < end) p = (int)utf8_next(n->buf, end, p);
    return p;
}

static void update_title(np_t *n) {
    const char *name = n->path[0] ? strrchr(n->path, '/') + 1 : "Sem título";
    char t[96];
    snprintf(t, sizeof(t), "%s%s - Bloco de Notas", n->modified ? "*" : "", name);
    win_set_title(n->win, t);
}

static void set_text(np_t *n, const char *s, int len) {
    if (len + 1 > n->cap) {
        n->cap = len + 1024;
        n->buf = krealloc(n->buf, n->cap);
    }
    memcpy(n->buf, s, len);
    n->len = len;
    n->buf[len] = 0;
    n->cur = n->anchor = 0;
    n->top = 0;
    n->scroll_x = 0;
    recompute_lines(n);
}

static bool has_sel(np_t *n) { return n->cur != n->anchor; }
static int sel_a(np_t *n) { return imin(n->cur, n->anchor); }
static int sel_b(np_t *n) { return imax(n->cur, n->anchor); }

static void delete_range(np_t *n, int a, int b) {
    if (b <= a) return;
    memmove(n->buf + a, n->buf + b, n->len - b + 1);
    n->len -= b - a;
    n->cur = n->anchor = a;
    n->modified = true;
    recompute_lines(n);
}

static void insert(np_t *n, const char *s, int len) {
    if (has_sel(n)) delete_range(n, sel_a(n), sel_b(n));
    if (n->len + len + 1 > n->cap) {
        n->cap = (n->len + len + 1) * 2;
        n->buf = krealloc(n->buf, n->cap);
    }
    memmove(n->buf + n->cur + len, n->buf + n->cur, n->len - n->cur + 1);
    memcpy(n->buf + n->cur, s, len);
    n->len += len;
    n->cur += len;
    n->anchor = n->cur;
    n->modified = true;
    recompute_lines(n);
}

static void ensure_visible(np_t *n) {
    rect_t tr = text_rect(n);
    int rows = imax(1, (tr.h - 2 * TPAD) / lh(n));
    int line = line_of(n, n->cur);
    if (line < n->top) n->top = line;
    if (line >= n->top + rows) n->top = line - rows + 1;
    int x = col_of(n, n->cur) * cw(n);
    int vis = tr.w - 2 * TPAD - 12;
    if (x < n->scroll_x) n->scroll_x = imax(0, x - vis / 3);
    if (x > n->scroll_x + vis) n->scroll_x = x - vis + vis / 3;
}

static int pos_from_point(np_t *n, int x, int y) {
    rect_t tr = text_rect(n);
    int line = n->top + (y - tr.y - TPAD) / lh(n);
    if (y < tr.y + TPAD) line = n->top - 1;
    if (line < 0) return 0;
    if (line >= n->nlines) return n->len;
    int col = (x - TPAD + n->scroll_x + cw(n) / 2) / cw(n);
    return pos_at(n, line, imax(0, col));
}

/* ------------------------------------------------------------------ */
static void draw(np_t *n) {
    canvas_t *c = win_canvas(n->win);
    rect_t tr = text_rect(n);
    gfx_clear(c, theme.surface);

    int rows = (tr.h - 2 * TPAD) / lh(n) + 1;
    int a = sel_a(n), b = sel_b(n);
    rect_t saved = c->clip;
    canvas_clip(c, R(tr.x, tr.y, tr.w, tr.h));
    for (int r = 0; r < rows; r++) {
        int line = n->top + r;
        if (line >= n->nlines) break;
        int y = tr.y + TPAD + r * lh(n);
        int start = n->lines[line], end = line_end(n, line);
        /* seleção */
        if (has_sel(n) && b > start && a <= end) {
            int sa = imax(a, start), sb = imin(b, end);
            int x0 = (int)utf8_count(n->buf + start, sa - start) * cw(n);
            int x1 = (int)utf8_count(n->buf + start, sb - start) * cw(n);
            if (b > end) x1 += cw(n) / 2;
            gfx_fill(c, TPAD + x0 - n->scroll_x, y - 1, x1 - x0, lh(n), theme.selection);
        }
        font_draw_n(c, TPAD - n->scroll_x, y, n->buf + start, end - start, theme.text, fflags(n));
    }
    n->cursor_rect = R(0, 0, 0, 0);
    int cline = line_of(n, n->cur);
    if (cline >= n->top && cline < n->top + rows) {
        int x = TPAD + col_of(n, n->cur) * cw(n) - n->scroll_x;
        n->cursor_rect = rect_intersect(R(x, tr.y + TPAD + (cline - n->top) * lh(n) - 1, n->zoom ? 2 : 1, lh(n)), tr);
        if (n->cursor_on && win_is_focused(n->win))
            gfx_fill(c, n->cursor_rect.x, n->cursor_rect.y, n->cursor_rect.w, n->cursor_rect.h, theme.text);
    }
    c->clip = saved;
    int total = n->nlines, visible = (tr.h - 2 * TPAD) / lh(n);
    ui_scrollbar(c, R(tr.x, tr.y + 2, tr.w - 1, tr.h - 4), total, visible, n->top);

    /* barra de status (os menus ficam na barra de menus do topo da tela) */
    int sy = c->h - STATUS_H;
    gfx_fill(c, 0, sy, c->w, STATUS_H, theme.window_bg);
    gfx_hline(c, 0, sy, c->w, theme.border);
    char st[96];
    snprintf(st, sizeof(st), "Ln %d, Col %d", line_of(n, n->cur) + 1, col_of(n, n->cur) + 1);
    font_draw(c, 12, sy + 5, st, theme.text_dim, 0);
    snprintf(st, sizeof(st), "%d caracteres", (int)utf8_count(n->buf, n->len));
    font_draw(c, 150, sy + 5, st, theme.text_dim, 0);
    const char *right = n->zoom ? "200%   |   UTF-8" : "100%   |   UTF-8";
    font_draw(c, c->w - 12 - font_width(right, 0), sy + 5, right, theme.text_dim, 0);
    win_invalidate(n->win);
}

/* ------------------------------------------------------------------ */
static bool save_as(np_t *n);

static bool save(np_t *n) {
    if (!n->path[0]) return save_as(n);
    if (!vfs_write_all(n->path, n->buf, n->len)) {
        ui_message(n->win, "Bloco de Notas", "Não foi possível salvar o arquivo.", "OK", NULL, NULL);
        return false;
    }
    n->modified = false;
    update_title(n);
    return true;
}

static bool save_as(np_t *n) {
    char p[VFS_PATH_MAX];
    if (n->path[0]) strlcpy(p, n->path, sizeof(p));
    else strlcpy(p, "/home/usuario/Documentos/Sem título.txt", sizeof(p));
    if (!ui_file_dialog(n->win, true, "Salvar como", p, sizeof(p))) return false;
    strlcpy(n->path, p, sizeof(n->path));
    return save(n);
}

/* true = pode continuar (salvou ou descartou) */
static bool confirm_discard(np_t *n) {
    if (!n->modified) return true;
    char msg[160];
    snprintf(msg, sizeof(msg), "Deseja salvar as alterações em %s?",
             n->path[0] ? strrchr(n->path, '/') + 1 : "Sem título");
    int r = ui_message(n->win, "Bloco de Notas", msg, "Salvar", "Não salvar", "Cancelar");
    if (r == 1) return save(n);
    return r == 2;
}

static void load(np_t *n, const char *path) {
    vnode_t *f = vfs_lookup(NULL, path);
    if (!f || f->type != VFS_FILE) {
        ui_message(n->win, "Bloco de Notas", "Arquivo não encontrado.", "OK", NULL, NULL);
        return;
    }
    char *tmp = kmalloc(f->size + 1);
    long len = vfs_read(f, 0, tmp, f->size);
    set_text(n, tmp, len < 0 ? 0 : (int)len);
    kfree(tmp);
    strlcpy(n->path, path, sizeof(n->path));
    n->modified = false;
    update_title(n);
}

static void copy_sel(np_t *n) {
    if (has_sel(n)) clipboard_set(n->buf + sel_a(n), sel_b(n) - sel_a(n));
}

static void paste(np_t *n) {
    int len;
    const char *s = clipboard_get(&len);
    if (s && len) insert(n, s, len);
}

static void insert_datetime(np_t *n) {
    datetime_t dt;
    time_to_datetime(time_now(), &dt);
    char s[32];
    snprintf(s, sizeof(s), "%02d:%02d %02d/%02d/%04d", dt.hour, dt.minute, dt.day, dt.month, dt.year);
    insert(n, s, (int)strlen(s));
}

static void open_dialog(np_t *n) {
    if (!confirm_discard(n)) return;
    char p[VFS_PATH_MAX];
    strlcpy(p, n->path[0] ? n->path : "/home/usuario/Documentos/", sizeof(p));
    if (ui_file_dialog(n->win, false, "Abrir", p, sizeof(p))) load(n, p);
}

enum { M_NEW = 1, M_NEWWIN, M_OPEN, M_SAVE, M_SAVEAS, M_EXIT, M_CUT, M_COPY, M_PASTE, M_DEL,
       M_SELALL, M_DATE, M_ZOOM };

/* chamado quando o usuário clica num menu do app na barra do topo (EV_MENU) */
static void run_menu(np_t *n, int which, int sx, int sy) {
    int r = 0;
    if (which == 0) {
        ui_menu_item_t m[] = {
            { "Novo", M_NEW, GLYPH_NEW_FILE, "Ctrl+N", false, false },
            { "Nova janela", M_NEWWIN, 0, NULL, false, false },
            { "Abrir...", M_OPEN, GLYPH_OPEN, "Ctrl+O", false, false },
            { "Salvar", M_SAVE, GLYPH_SAVE, "Ctrl+S", false, false },
            { "Salvar como...", M_SAVEAS, 0, NULL, false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Sair", M_EXIT, 0, NULL, false, false },
        };
        r = ui_menu(sx, sy, m, 7);
    } else if (which == 1) {
        ui_menu_item_t m[] = {
            { "Recortar", M_CUT, GLYPH_CUT, "Ctrl+X", !has_sel(n), false },
            { "Copiar", M_COPY, GLYPH_COPY, "Ctrl+C", !has_sel(n), false },
            { "Colar", M_PASTE, GLYPH_PASTE, "Ctrl+V", false, false },
            { "Excluir", M_DEL, GLYPH_DELETE, "Del", !has_sel(n), false },
            { NULL, 0, 0, NULL, false, true },
            { "Selecionar tudo", M_SELALL, 0, "Ctrl+A", false, false },
            { "Data e hora", M_DATE, 0, "F5", false, false },
        };
        r = ui_menu(sx, sy, m, 7);
    } else {
        ui_menu_item_t m[] = {
            { n->zoom ? "Tamanho normal (100%)" : "Ampliar (200%)", M_ZOOM, GLYPH_SEARCH, "Ctrl++", false, false },
        };
        r = ui_menu(sx, sy, m, 1);
    }
    switch (r) {
    case M_NEW:
        if (confirm_discard(n)) { set_text(n, "", 0); n->path[0] = 0; n->modified = false; update_title(n); }
        break;
    case M_NEWWIN: app_launch("notepad", NULL); break;
    case M_OPEN: open_dialog(n); break;
    case M_SAVE: save(n); break;
    case M_SAVEAS: save_as(n); break;
    case M_EXIT: if (confirm_discard(n)) n->quit = true; break;
    case M_CUT: copy_sel(n); delete_range(n, sel_a(n), sel_b(n)); break;
    case M_COPY: copy_sel(n); break;
    case M_PASTE: paste(n); break;
    case M_DEL: delete_range(n, sel_a(n), sel_b(n)); break;
    case M_SELALL: n->anchor = 0; n->cur = n->len; break;
    case M_DATE: insert_datetime(n); break;
    case M_ZOOM: n->zoom = !n->zoom; break;
    }
    update_title(n);
}

static bool is_word(char c) { return isalnum(c) || (uint8_t)c >= 0x80 || c == '_'; }

static void key(np_t *n, event_t *e) {
    bool shift = e->mods & MOD_SHIFT, ctrl = e->mods & MOD_CTRL;
    int line = line_of(n, n->cur);
    int rows = imax(1, (text_rect(n).h - 2 * TPAD) / lh(n));
    int old = n->cur;
    bool move = true, vertical = false;

    if (ctrl && e->key == KEY_CHAR) {
        switch (tolower((int)e->ch)) {
        case 'a': n->anchor = 0; n->cur = n->len; return;
        case 'c': copy_sel(n); return;
        case 'x': copy_sel(n); delete_range(n, sel_a(n), sel_b(n)); return;
        case 'v': paste(n); return;
        case 's': if (shift) save_as(n); else save(n); return;
        case 'o': open_dialog(n); return;
        case 'n':
            if (confirm_discard(n)) {
                set_text(n, "", 0);
                n->path[0] = 0;
                n->modified = false;
                update_title(n);
            }
            return;
        case '+': case '=': n->zoom = true; return;
        case '-': n->zoom = false; return;
        }
        return;
    }

    switch (e->key) {
    case KEY_LEFT:
        if (has_sel(n) && !shift) { n->cur = n->anchor = sel_a(n); return; }
        n->cur = (int)utf8_prev(n->buf, n->cur);
        if (ctrl) {
            while (n->cur > 0 && !is_word(n->buf[n->cur - 1])) n->cur--;
            while (n->cur > 0 && is_word(n->buf[n->cur - 1])) n->cur--;
        }
        break;
    case KEY_RIGHT:
        if (has_sel(n) && !shift) { n->cur = n->anchor = sel_b(n); return; }
        n->cur = (int)utf8_next(n->buf, n->len, n->cur);
        if (ctrl) {
            while (n->cur < n->len && is_word(n->buf[n->cur])) n->cur++;
            while (n->cur < n->len && !is_word(n->buf[n->cur])) n->cur++;
        }
        break;
    case KEY_UP:   n->cur = pos_at(n, line - 1, n->want_col); vertical = true; if (line == 0) n->cur = 0; break;
    case KEY_DOWN: n->cur = line + 1 < n->nlines ? pos_at(n, line + 1, n->want_col) : n->len; vertical = true; break;
    case KEY_PGUP: n->cur = pos_at(n, imax(0, line - rows), n->want_col); vertical = true; break;
    case KEY_PGDN: n->cur = pos_at(n, imin(n->nlines - 1, line + rows), n->want_col); vertical = true; break;
    case KEY_HOME: n->cur = ctrl ? 0 : n->lines[line]; break;
    case KEY_END:  n->cur = ctrl ? n->len : line_end(n, line); break;
    default: move = false; break;
    }
    if (move) {
        if (!shift) n->anchor = n->cur;
        if (!vertical) n->want_col = col_of(n, n->cur);
        (void)old;
        return;
    }

    switch (e->key) {
    case KEY_ENTER: insert(n, "\n", 1); break;
    case KEY_TAB: insert(n, "    ", 4); break;
    case KEY_BACKSPACE:
        if (has_sel(n)) delete_range(n, sel_a(n), sel_b(n));
        else if (n->cur > 0) delete_range(n, (int)utf8_prev(n->buf, n->cur), n->cur);
        break;
    case KEY_DELETE:
        if (has_sel(n)) delete_range(n, sel_a(n), sel_b(n));
        else if (n->cur < n->len) delete_range(n, n->cur, (int)utf8_next(n->buf, n->len, n->cur));
        break;
    case KEY_F5: insert_datetime(n); break;
    case KEY_CHAR:
        if (e->ch >= 32 && !(e->mods & MOD_ALT)) {
            char tmp[4];
            insert(n, tmp, utf8_encode(e->ch, tmp));
        }
        break;
    default:
        return;
    }
    n->want_col = col_of(n, n->cur);
}

void notepad_main(void *arg) {
    np_t *n = kcalloc(1, sizeof(np_t));
    n->menu_hover = -1;
    n->cursor_on = true;
    set_text(n, "", 0);
    n->win = win_create("Sem título - Bloco de Notas", ICON_NOTEPAD, WIN_AUTO, WIN_AUTO, 700, 460, WF_APP);
    win_set_min_size(n->win, 300, 160);
    win_set_menus(n->win, "Arquivo|Editar|Exibir");
    if (arg) {
        load(n, arg);
        kfree(arg);
    }
    update_title(n);
    ev_set_timer(530);
    draw(n);

    while (!n->quit) {
        event_t e;
        gui_wait(&e);
        rect_t tr = text_rect(n);
        bool was_modified = n->modified;
        switch (e.type) {
        case EV_TIMER: {
            /* piscar: redesenha só a linha do cursor e avisa só esse pedaço */
            n->cursor_on = !n->cursor_on;
            rect_t r = n->cursor_rect;
            if (rect_empty(r)) continue;
            int line = line_of(n, n->cur);
            canvas_t *c = win_canvas(n->win);
            rect_t row = R(tr.x, r.y, tr.w, r.h);
            rect_t saved = c->clip;
            canvas_clip(c, row);
            gfx_fill(c, row.x, row.y, row.w, row.h, theme.surface);
            if (has_sel(n)) { c->clip = saved; draw(n); continue; }
            font_draw_n(c, TPAD - n->scroll_x, r.y + 1, n->buf + n->lines[line], line_end(n, line) - n->lines[line],
                        theme.text, fflags(n));
            if (n->cursor_on && win_is_focused(n->win)) gfx_fill(c, r.x, r.y, r.w, r.h, theme.text);
            c->clip = saved;
            win_invalidate_rect(n->win, row);
            continue;
        }
        case EV_KEY_DOWN:
            key(n, &e);
            n->cursor_on = true;
            ensure_visible(n);
            break;
        case EV_MOUSE_MOVE:
            win_set_cursor(n->win, ui_hit(tr, &e) ? CURSOR_IBEAM : CURSOR_ARROW);
            if (!n->selecting) continue;          /* nada mudou: não redesenha */
            n->cur = pos_from_point(n, e.x, e.y);
            ensure_visible(n);
            break;
        case EV_MENU:
            run_menu(n, (int)e.key, e.x, e.y);
            break;
        case EV_MOUSE_DOWN:
            if (ui_hit(tr, &e)) {
                if (e.button == 2) {
                    rect_t cr = win_client_rect(n->win);
                    ui_menu_item_t m[] = {
                        { "Recortar", M_CUT, GLYPH_CUT, "Ctrl+X", !has_sel(n), false },
                        { "Copiar", M_COPY, GLYPH_COPY, "Ctrl+C", !has_sel(n), false },
                        { "Colar", M_PASTE, GLYPH_PASTE, "Ctrl+V", false, false },
                        { NULL, 0, 0, NULL, false, true },
                        { "Selecionar tudo", M_SELALL, 0, "Ctrl+A", false, false },
                    };
                    int r = ui_menu(cr.x + e.x, cr.y + e.y, m, 5);
                    if (r == M_CUT) { copy_sel(n); delete_range(n, sel_a(n), sel_b(n)); }
                    else if (r == M_COPY) copy_sel(n);
                    else if (r == M_PASTE) paste(n);
                    else if (r == M_SELALL) { n->anchor = 0; n->cur = n->len; }
                    break;
                }
                n->cur = pos_from_point(n, e.x, e.y);
                if (e.clicks == 2) {
                    int a = n->cur, b = n->cur;
                    while (a > 0 && is_word(n->buf[a - 1])) a--;
                    while (b < n->len && is_word(n->buf[b])) b++;
                    n->anchor = a;
                    n->cur = b;
                } else if (e.clicks >= 3) {
                    int l = line_of(n, n->cur);
                    n->anchor = n->lines[l];
                    n->cur = line_end(n, l);
                } else {
                    n->anchor = n->cur;
                    n->selecting = true;
                }
                n->want_col = col_of(n, n->cur);
                n->cursor_on = true;
            }
            break;
        case EV_MOUSE_UP:
            n->selecting = false;
            break;
        case EV_MOUSE_WHEEL: {
            int rows = (tr.h - 2 * TPAD) / lh(n);
            n->top = imax(0, imin(imax(0, n->nlines - rows), n->top - e.wheel * 3));
            break;
        }
        case EV_RESIZE: case EV_FOCUS: case EV_BLUR: case EV_THEME:
            break;
        case EV_CLOSE:
            if (confirm_discard(n)) n->quit = true;
            break;
        default:
            continue;
        }
        if (n->modified != was_modified) update_title(n);
        if (!n->quit) draw(n);
    }
    win_destroy(n->win);
    kfree(n->buf);
    kfree(n->lines);
    kfree(n);
}
