/*
 * Toca: a bancada gráfica do Tatu DB.
 *
 * O editor guarda bytes UTF-8; o cursor e as colunas da tela contam caracteres.
 * O resultado é uma fotografia do comando, independente do catálogo do banco.
 */
#include "apps/apps.h"
#include "gui/ui.h"
#include "dev/keyboard.h"
#include "fs/vfs.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "mm/heap.h"
#include "tatu/tatu.h"

#define TABLE_MAX 64
#define HISTORY_MAX 16
#define SQL_MAX (256 * 1024)
#define ROW_H 26
#define LINE_H 20

enum { TAB_DATA, TAB_SCHEMA, TAB_HISTORY };
enum { FOCUS_EDITOR, FOCUS_GRID };
enum { A_NEW = 1, A_OPEN, A_DEMO, A_SAVE_DB, A_SAVE_QUERY, A_LOAD_QUERY, A_EXPORT, A_EXIT,
       A_RUN, A_COPY, A_CUT, A_PASTE, A_ALL, A_BEGIN, A_COMMIT, A_ROLLBACK, A_ABOUT };

typedef struct {
    char *sql;
    bool ok;
    int rows;
} history_t;

typedef struct {
    window_t *win;
    tatu_db_t *db;
    tatu_output_t output;
    tatu_result_t *result;
    tatu_error_t error;
    char *buf;
    int len, cap, cur, anchor, top, scroll_x, want_col;
    int *lines, nlines, line_cap;
    char *tables[TABLE_MAX];
    int ntables, total_tables, selected, table_top;
    char query_path[VFS_PATH_MAX];
    char message[300];
    history_t history[HISTORY_MAX];
    int nhistory, history_top;
    int tab, focus, row_top, grid_x, cell_row, cell_col, schema_top;
    bool quit, modified, selecting, cursor_on, failed, memory_changed;
    int hover;
} toca_t;

typedef struct {
    rect_t sidebar, editor, results, tabs, run, examples, new_db, open_db;
    rect_t grid, hscroll, prev, next;
    int side_w, main_x, main_w;
} layout_t;

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static uint32_t green(void) { return theme.dark ? HEX(0x80CBB1) : HEX(0x287962); }
static uint32_t red(void) { return theme.dark ? HEX(0xFFA9A1) : HEX(0xB13732); }
static uint32_t panel(void) { return theme.dark ? HEX(0x202A29) : HEX(0xF1F5F0); }
static uint32_t editor_bg(void) { return theme.dark ? HEX(0x182220) : HEX(0xFCFDFB); }

static void result_message(toca_t *t) {
    if (!t->result) return;
    char elapsed[40];
    if (t->result->micros < 1000) strlcpy(elapsed, "<1 ms", sizeof(elapsed));
    else snprintf(elapsed, sizeof(elapsed), "%llu ms", (unsigned long long)(t->result->micros / 1000));
    snprintf(t->message, sizeof(t->message), "%d linha(s)  ·  %d coluna(s)  ·  %s",
             t->result->nrows, t->result->ncols, elapsed);
}

static layout_t layout(toca_t *t) {
    canvas_t *c = win_canvas(t->win);
    int sw = c->w < 820 ? 174 : 198, mx = sw + 20, mw = c->w - mx - 20;
    int eh = imin(182, imax(130, (c->h - 180) * 43 / 100));
    int ry = 122 + eh + 14;
    layout_t l = { .side_w = sw, .main_x = mx, .main_w = mw };
    l.sidebar = R(0, 0, sw, c->h - 26);
    l.editor = R(mx, 122, mw, eh);
    l.tabs = R(mx, ry, mw, 36);
    l.results = R(mx, ry + 36, mw, c->h - 26 - 16 - ry - 36);
    l.grid = R(mx + 1, ry + 73, mw - 2, l.results.h - 38 - 12);
    l.hscroll = R(mx + 1, l.results.y + l.results.h - 13, mw - 14, 12);
    l.run = R(mx + mw - 122, 80, 122, 30);
    l.examples = R(mx + mw - 222, 80, 90, 30);
    l.new_db = R(16, c->h - 26 - 54, (sw - 40) / 2, 30);
    l.open_db = R(l.new_db.x + l.new_db.w + 8, l.new_db.y, l.new_db.w, 30);
    l.prev = R(mx + mw - 58, ry + 2, 24, 28);
    l.next = R(mx + mw - 30, ry + 2, 24, 28);
    return l;
}

static void card(canvas_t *c, rect_t r, uint32_t color) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 8, color);
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 8, theme.border);
}

static int sa(toca_t *t) { return imin(t->cur, t->anchor); }
static int sb(toca_t *t) { return imax(t->cur, t->anchor); }
static bool has_selection(toca_t *t) { return t->cur != t->anchor; }

static void recompute(toca_t *t) {
    t->nlines = 0;
    for (int i = 0; i <= t->len; i++) {
        if (i && t->buf[i - 1] != '\n') continue;
        if (t->nlines == t->line_cap) {
            int cap = t->line_cap ? t->line_cap * 2 : 64;
            int *p = krealloc(t->lines, (size_t)cap * sizeof(int));
            if (!p) return;
            t->lines = p;
            t->line_cap = cap;
        }
        t->lines[t->nlines++] = i;
    }
}

static bool reserve(toca_t *t, int n) {
    if (n > SQL_MAX) {
        strlcpy(t->message, "A consulta atingiu o limite de 256 KiB.", sizeof(t->message));
        return false;
    }
    if (n < t->cap) return true;
    int cap = imax(1024, imin(SQL_MAX + 1, n * 2 + 1));
    char *p = krealloc(t->buf, (size_t)cap);
    if (!p) return false;
    t->buf = p;
    t->cap = cap;
    return true;
}

static void set_text(toca_t *t, const char *s) {
    int n = (int)strlen(s);
    if (!reserve(t, n)) return;
    memcpy(t->buf, s, (size_t)n + 1);
    t->len = n;
    t->cur = t->anchor = t->top = t->scroll_x = t->want_col = 0;
    t->modified = false;
    t->focus = FOCUS_EDITOR;
    recompute(t);
}

static int line_of(toca_t *t, int p) {
    int lo = 0, hi = t->nlines - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (t->lines[mid] <= p) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static int line_end(toca_t *t, int line) {
    return line + 1 < t->nlines ? t->lines[line + 1] - 1 : t->len;
}

static int column(toca_t *t, int p) {
    int start = t->lines[line_of(t, p)];
    return (int)utf8_count(t->buf + start, (size_t)(p - start));
}

static int pos_at(toca_t *t, int line, int col) {
    if (line < 0) return 0;
    if (line >= t->nlines) return t->len;
    int p = t->lines[line], end = line_end(t, line);
    while (col-- > 0 && p < end) p = (int)utf8_next(t->buf, (size_t)end, (size_t)p);
    return p;
}

static void delete_range(toca_t *t, int a, int b) {
    if (b <= a) return;
    memmove(t->buf + a, t->buf + b, (size_t)(t->len - b + 1));
    t->len -= b - a;
    t->cur = t->anchor = a;
    t->modified = true;
    recompute(t);
}

static void insert(toca_t *t, const char *s, int n) {
    if (n < 0 || n > SQL_MAX || t->len - (sb(t) - sa(t)) > SQL_MAX - n) {
        strlcpy(t->message, "A consulta atingiu o limite de 256 KiB.", sizeof(t->message));
        return;
    }
    if (!reserve(t, t->len - (sb(t) - sa(t)) + n)) return;
    if (has_selection(t)) delete_range(t, sa(t), sb(t));
    memmove(t->buf + t->cur + n, t->buf + t->cur, (size_t)(t->len - t->cur + 1));
    memcpy(t->buf + t->cur, s, (size_t)n);
    t->len += n;
    t->cur += n;
    t->anchor = t->cur;
    t->modified = true;
    recompute(t);
}

static void ensure_cursor(toca_t *t) {
    rect_t r = layout(t).editor;
    int rows = imax(1, (r.h - 20) / LINE_H), line = line_of(t, t->cur);
    if (line < t->top) t->top = line;
    if (line >= t->top + rows) t->top = line - rows + 1;
    int x = column(t, t->cur) * 8, width = r.w - 78;
    if (x < t->scroll_x) t->scroll_x = imax(0, x - width / 4);
    if (x > t->scroll_x + width) t->scroll_x = x - width + width / 4;
}

static int point_pos(toca_t *t, int x, int y) {
    rect_t r = layout(t).editor;
    int line = t->top + (y - r.y - 12) / LINE_H;
    return pos_at(t, line, imax(0, (x - r.x - 52 + t->scroll_x + 4) / 8));
}

static void refresh_tables(toca_t *t) {
    char *selected = t->selected >= 0 && t->selected < t->ntables ? kstrdup(t->tables[t->selected]) : NULL;
    for (int i = 0; i < t->ntables; i++) {
        kfree(t->tables[i]);
        t->tables[i] = NULL;
    }
    const char *names[TABLE_MAX];
    t->total_tables = tatu_tables(t->db, names, TABLE_MAX);
    int count = imin(TABLE_MAX, t->total_tables);
    t->ntables = 0;
    t->selected = -1;
    for (int i = 0; i < count; i++) {
        t->tables[i] = kstrdup(names[i]);
        if (!t->tables[i]) break;
        t->ntables++;
        if (selected && !strcmp(selected, t->tables[i])) t->selected = i;
    }
    kfree(selected);
    if (t->selected < 0 && t->ntables) t->selected = 0;
    t->table_top = imin(t->table_top, imax(0, t->ntables - 1));
}

static void title(toca_t *t) {
    const char *path = tatu_path(t->db), *name = path ? strrchr(path, '/') : NULL;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s — Toca", path ? (name ? name + 1 : path) : "Laboratório em memória");
    win_set_title(t->win, buf);
}

static void remember(toca_t *t, const char *sql, bool ok) {
    if (t->nhistory == HISTORY_MAX) {
        kfree(t->history[HISTORY_MAX - 1].sql);
        t->nhistory--;
    }
    memmove(t->history + 1, t->history, (size_t)t->nhistory * sizeof(history_t));
    t->history[0] = (history_t){ kstrdup(sql), ok, t->result ? t->result->nrows : 0 };
    t->nhistory++;
    t->history_top = 0;
}

static void execute(toca_t *t, const char *sql, bool history) {
    tatu_output_free(&t->output);
    memset(&t->error, 0, sizeof(t->error));
    t->result = NULL;
    bool ok = tatu_run(t->db, sql, &t->output, &t->error);
    for (tatu_result_t *r = t->output.first; r; r = r->next) t->result = r;
    t->failed = !ok;
    t->tab = TAB_DATA;
    t->row_top = t->grid_x = 0;
    t->cell_row = t->cell_col = -1;
    if (ok) {
        if (t->result && t->result->ncols) result_message(t);
        else strlcpy(t->message, t->result && t->result->message ? t->result->message : "Consulta concluída.", sizeof(t->message));
    } else strlcpy(t->message, t->error.message, sizeof(t->message));
    if (!tatu_path(t->db)) {
        for (tatu_result_t *r = t->output.first; r; r = r->next)
            if (!r->ncols) t->memory_changed = true;
    }
    refresh_tables(t);
    if (history) remember(t, sql, ok);
}

static void run_editor(toca_t *t) {
    if (!t->len) return;
    if (has_selection(t)) {
        int n = sb(t) - sa(t);
        char *sql = kmalloc((size_t)n + 1);
        if (!sql) return;
        memcpy(sql, t->buf + sa(t), (size_t)n);
        sql[n] = 0;
        execute(t, sql, true);
        kfree(sql);
    } else execute(t, t->buf, true);
    kprintf("toca: consulta %s (%d linhas, %d colunas)\n", t->failed ? "com erro" : "concluída",
            t->result ? t->result->nrows : 0, t->result ? t->result->ncols : 0);
}

/* A cor é decidida por token, sem alterar o texto que o motor vai analisar. */
static void draw_sql(canvas_t *c, int x, int y, const char *s, int n) {
    int i = 0;
    while (i < n) {
        int start = i;
        uint32_t color = theme.text;
        if (s[i] == '-' && i + 1 < n && s[i + 1] == '-') {
            i = n;
            color = theme.text_dim;
        } else if (s[i] == '\'') {
            color = theme.dark ? HEX(0xB9CE90) : HEX(0x597C30);
            i++;
            while (i < n) {
                if (s[i++] != '\'') continue;
                if (i < n && s[i] == '\'') i++;
                else break;
            }
        } else if (isalpha((unsigned char)s[i]) || s[i] == '_') {
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
            if (tatu_is_keyword(s + start, (size_t)(i - start)))
                color = theme.dark ? HEX(0x8DC8E9) : HEX(0x266CA0);
        } else if (isdigit((unsigned char)s[i])) {
            while (i < n && (isdigit((unsigned char)s[i]) || s[i] == '.')) i++;
            color = theme.dark ? HEX(0xE8BD88) : HEX(0xA36B27);
        } else i = (int)utf8_next(s, (size_t)n, (size_t)i);
        x += font_draw_n(c, x, y, s + start, i - start, color, FONT_MONO);
    }
}

static void draw_editor(toca_t *t, canvas_t *c, layout_t l) {
    rect_t r = l.editor;
    card(c, r, editor_bg());
    rect_t saved = c->clip;
    canvas_clip(c, rect_inset(r, 2));
    gfx_fill(c, r.x + 1, r.y + 2, 38, r.h - 4, panel());
    gfx_vline(c, r.x + 39, r.y + 1, r.h - 2, theme.border);
    int rows = (r.h - 18) / LINE_H;
    int a = sa(t), b = sb(t), cl = line_of(t, t->cur);
    for (int i = 0; i < rows; i++) {
        int line = t->top + i;
        if (line >= t->nlines) break;
        int y = r.y + 12 + i * LINE_H;
        char num[16];
        snprintf(num, sizeof(num), "%d", line + 1);
        font_draw(c, r.x + 29 - font_width(num, FONT_MONO), y, num,
                  line == cl ? green() : theme.text_dim, FONT_MONO);
        int start = t->lines[line], end = line_end(t, line);
        rect_t whole = c->clip;
        canvas_clip(c, R(r.x + 42, r.y + 2, r.w - 57, r.h - 4));
        if (t->focus == FOCUS_EDITOR && cl == line)
            gfx_fill(c, r.x + 42, y - 2, r.w - 56, LINE_H, color_with_alpha(green(), 12));
        if (has_selection(t) && b > start && a <= end) {
            int x0 = (int)utf8_count(t->buf + start, (size_t)(imax(a, start) - start)) * 8;
            int x1 = (int)utf8_count(t->buf + start, (size_t)(imin(b, end) - start)) * 8;
            if (b > end) x1 += 8;
            gfx_fill(c, r.x + 52 + x0 - t->scroll_x, y - 2, x1 - x0, LINE_H, theme.selection);
        }
        draw_sql(c, r.x + 52 - t->scroll_x, y, t->buf + start, end - start);
        if (t->focus == FOCUS_EDITOR && t->cursor_on && win_is_focused(t->win) && line == cl)
            gfx_fill(c, r.x + 52 + column(t, t->cur) * 8 - t->scroll_x, y - 1, 1, 18, green());
        c->clip = whole;
    }
    c->clip = saved;
    ui_scrollbar(c, R(r.x, r.y + 3, r.w - 3, r.h - 6), t->nlines, rows, t->top);
}

static int col_width(tatu_result_t *r, int col) {
    int w = font_width(r->cols[col], 0) + 36;
    for (int i = 0; i < imin(80, r->nrows); i++) {
        char s[256];
        tatu_format_cell(&r->cells[(size_t)i * r->ncols + col], s, sizeof(s));
        w = imax(w, font_width(s, FONT_MONO) + 28);
    }
    return imin(240, imax(100, w));
}

static int grid_width(toca_t *t) {
    if (!t->result) return 0;
    int width = 46;
    for (int i = 0; i < t->result->ncols; i++) width += col_width(t->result, i);
    return width;
}

static void horizontal(canvas_t *c, rect_t r, int total, int offset) {
    if (total <= r.w) return;
    gfx_fill(c, r.x, r.y, r.w, r.h, theme.surface_alt);
    int width = imax(24, r.w * r.w / total);
    int x = r.x + (r.w - width) * offset / imax(1, total - r.w);
    gfx_round_fill(c, x, r.y + 3, width, 6, 3, theme.border_strong);
}

static void draw_data(toca_t *t, canvas_t *c, layout_t l) {
    rect_t r = l.results;
    card(c, r, theme.surface);
    if (t->failed) {
        gfx_round_fill(c, r.x + 14, r.y + 14, 4, 28, 2, red());
        font_draw(c, r.x + 30, r.y + 16, tatu_error_label(t->error.kind), red(), FONT_BOLD);
        rect_t saved = c->clip;
        canvas_clip(c, rect_inset(r, 10));
        const char *p = t->error.rendered[0] ? t->error.rendered : t->error.message;
        int y = r.y + 52;
        while (*p && y < r.y + r.h - 8) {
            const char *end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            int n = (int)(end - p), pos = 0;
            do {
                int take = font_fit(p + pos, r.w - 44, FONT_MONO);
                take = imin(take, n - pos);
                if (take <= 0 && pos < n) take = (int)utf8_next(p + pos, (size_t)(n - pos), 0);
                font_draw_n(c, r.x + 22, y, p + pos, take, theme.text, FONT_MONO);
                y += LINE_H;
                pos += take;
            } while (pos < n && y < r.y + r.h - 8);
            p = *end ? end + 1 : end;
        }
        c->clip = saved;
        return;
    }
    tatu_result_t *res = t->result;
    if (!res || !res->ncols) {
        int y = r.y + imax(20, r.h / 2 - 42);
        gfx_circle(c, r.x + r.w / 2, y + 10, 18, color_with_alpha(green(), 20));
        glyph_draw(c, GLYPH_CHECK, r.x + r.w / 2 - 10, y, 20, green());
        ui_text_center(c, R(r.x + 20, y + 38, r.w - 40, 24),
                       res && res->message ? res->message : "Um lugar para suas próximas descobertas.", theme.text, 0);
        ui_text_center(c, R(r.x + 20, y + 66, r.w - 40, 20), "Escreva SQL e pressione Ctrl+Enter para executar.", theme.text_dim, 0);
        return;
    }
    int total = grid_width(t), gw = l.grid.w - 12;
    t->grid_x = imin(t->grid_x, imax(0, total - gw));
    int visible = imax(1, l.grid.h / ROW_H);
    t->row_top = imin(t->row_top, imax(0, res->nrows - visible));
    rect_t saved = c->clip;
    canvas_clip(c, R(r.x + 1, r.y + 1, r.w - 14, r.h - 14));
    gfx_fill(c, r.x + 1, r.y + 1, r.w - 2, 36, panel());
    int x = r.x + 1 - t->grid_x;
    font_draw(c, x + 15, r.y + 11, "#", theme.text_dim, FONT_MONO);
    x += 46;
    for (int col = 0; col < res->ncols; col++) {
        int w = col_width(res, col);
        font_draw_ellipsis(c, x + 12, r.y + 11, w - 20, res->cols[col], theme.text, FONT_BOLD);
        gfx_vline(c, x - 1, r.y + 1, r.h - 13, theme.border);
        x += w;
    }
    gfx_hline(c, r.x, r.y + 36, r.w, theme.border);
    for (int i = 0; i <= visible; i++) {
        int row = t->row_top + i, y = l.grid.y + i * ROW_H;
        if (row >= res->nrows) break;
        if (i & 1) gfx_fill(c, l.grid.x, y, l.grid.w, ROW_H, theme.surface_alt);
        if (row == t->cell_row) gfx_fill(c, l.grid.x, y, l.grid.w, ROW_H, color_with_alpha(green(), 22));
        char text[256];
        snprintf(text, sizeof(text), "%d", row + 1);
        x = l.grid.x - t->grid_x;
        font_draw(c, x + 8, y + 5, text, theme.text_dim, FONT_MONO);
        x += 46;
        for (int col = 0; col < res->ncols; col++) {
            int w = col_width(res, col);
            tatu_value_t *v = &res->cells[(size_t)row * res->ncols + col];
            if (row == t->cell_row && col == t->cell_col)
                gfx_rect(c, x, y, w, ROW_H, green());
            tatu_format_cell(v, text, sizeof(text));
            font_draw_ellipsis(c, x + 12, y + 5, w - 20, text,
                              v->type == TV_NULL ? theme.text_dim : theme.text, FONT_MONO);
            gfx_vline(c, x - 1, y, ROW_H, theme.border);
            x += w;
        }
        gfx_hline(c, l.grid.x, y + ROW_H - 1, l.grid.w, color_with_alpha(theme.border, 100));
    }
    if (!res->nrows)
        ui_text_center(c, R(r.x + 20, l.grid.y + 30, r.w - 40, 25), "Nenhuma linha corresponde à consulta.", theme.text_dim, 0);
    c->clip = saved;
    horizontal(c, l.hscroll, total, t->grid_x);
    ui_scrollbar(c, l.grid, res->nrows, visible, t->row_top);
}

static void draw_schema(toca_t *t, canvas_t *c, layout_t l) {
    rect_t r = l.results;
    card(c, r, theme.surface);
    if (t->selected < 0) {
        ui_text_center(c, r, "Crie ou selecione uma tabela para explorar a estrutura.", theme.text_dim, 0);
        return;
    }
    int nc = 0, nr = 0, ni = 0;
    const char *name = t->tables[t->selected];
    tatu_table_info(t->db, name, &nc, &nr, &ni);
    char text[160];
    snprintf(text, sizeof(text), "%s  ·  %d colunas  ·  %d linhas  ·  %d índices", name, nc, nr, ni);
    font_draw_ellipsis(c, r.x + 16, r.y + 14, r.w - 30, text, theme.text, FONT_BOLD);
    gfx_hline(c, r.x, r.y + 42, r.w, theme.border);
    int visible = imax(1, (r.h - 52) / 32);
    t->schema_top = imin(t->schema_top, imax(0, nc - visible));
    rect_t saved = c->clip;
    canvas_clip(c, R(r.x + 1, r.y + 44, r.w - 14, r.h - 46));
    for (int i = 0; i < visible; i++) {
        int col = t->schema_top + i;
        if (col >= nc) break;
        tatu_column_info_t info;
        if (!tatu_column_info(t->db, name, col, &info)) continue;
        int y = r.y + 52 + i * 32;
        if (i & 1) gfx_fill(c, r.x + 1, y - 6, r.w - 2, 32, theme.surface_alt);
        if (info.primary_key) glyph_draw(c, GLYPH_LOCK, r.x + 13, y, 16, green());
        else gfx_circle(c, r.x + 21, y + 8, 3, theme.text_dim);
        font_draw_ellipsis(c, r.x + 40, y, r.w * 40 / 100 - 45, info.name, theme.text, FONT_MONO);
        font_draw(c, r.x + r.w * 42 / 100, y, info.type, green(), FONT_MONO);
        const char *tag = info.primary_key ? "CHAVE PRIMÁRIA" : info.unique ? "ÚNICO" : info.not_null ? "OBRIGATÓRIO" : "ACEITA NULL";
        font_draw_ellipsis(c, r.x + r.w * 67 / 100, y, r.w * 33 / 100 - 15, tag, theme.text_dim, 0);
    }
    c->clip = saved;
    ui_scrollbar(c, R(r.x, r.y + 44, r.w, r.h - 46), nc, visible, t->schema_top);
}

static void draw_history(toca_t *t, canvas_t *c, layout_t l) {
    rect_t r = l.results;
    card(c, r, theme.surface);
    if (!t->nhistory) {
        ui_text_center(c, r, "As consultas desta sessão aparecerão aqui.", theme.text_dim, 0);
        return;
    }
    font_draw(c, r.x + 16, r.y + 12, "Clique em uma consulta para levá-la ao editor.", theme.text_dim, 0);
    rect_t saved = c->clip;
    canvas_clip(c, R(r.x + 1, r.y + 35, r.w - 14, r.h - 37));
    for (int i = 0, y = r.y + 40; y < r.y + r.h; i++, y += 44) {
        int h = t->history_top + i;
        if (h >= t->nhistory) break;
        history_t *entry = &t->history[h];
        if (i & 1) gfx_fill(c, r.x + 1, y - 4, r.w - 2, 44, theme.surface_alt);
        gfx_circle(c, r.x + 20, y + 12, 3, entry->ok ? green() : red());
        char first[512];
        strlcpy(first, entry->sql ? entry->sql : "", sizeof(first));
        for (char *p = first; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
        font_draw_ellipsis(c, r.x + 34, y + 4, r.w - 53, first, theme.text, FONT_MONO);
        gfx_hline(c, r.x + 14, y + 37, r.w - 28, theme.border);
    }
    c->clip = saved;
    ui_scrollbar(c, R(r.x, r.y + 35, r.w, r.h - 37), t->nhistory, imax(1, (r.h - 35) / 44), t->history_top);
}

static void table_glyph(canvas_t *c, int x, int y, uint32_t color) {
    gfx_round_rect(c, x, y, 16, 15, 2, color);
    gfx_hline(c, x + 1, y + 5, 14, color);
    gfx_hline(c, x + 1, y + 10, 14, color);
    gfx_vline(c, x + 6, y + 5, 10, color);
}

static void draw(toca_t *t) {
    canvas_t *c = win_canvas(t->win);
    layout_t l = layout(t);
    gfx_clear(c, theme.window_bg);
    gfx_fill(c, 0, 0, l.side_w, c->h - 26, panel());
    gfx_vline(c, l.side_w, 0, c->h - 26, theme.border);
    icon_draw(c, ICON_TATU, 16, 15, 46);
    font_draw(c, 70, 18, "Toca", theme.text, FONT_2X | FONT_BOLD);
    font_draw(c, 72, 53, "TATU DB", green(), 0);
    font_draw(c, l.main_x, 20, "Seu banco, por dentro.", theme.text, FONT_BOLD);
    font_draw_ellipsis(c, l.main_x, 45, l.main_w - 8, "Explore os dados. Escreva SQL. Entenda o caminho.", theme.text_dim, 0);
    gfx_hline(c, l.main_x, 66, l.main_w, theme.border);

    font_draw(c, 18, 94, "CONEXÃO LOCAL", theme.text_dim, 0);
    const char *path = tatu_path(t->db), *basename = path ? strrchr(path, '/') : NULL;
    gfx_circle(c, 23, 128, 4, green());
    font_draw_ellipsis(c, 36, 120, l.side_w - 50,
                       path ? (basename ? basename + 1 : path) : "Laboratório", theme.text, FONT_BOLD);
    font_draw(c, 18, 146, path ? "Arquivo na RAM" : "Banco em memória", theme.text_dim, 0);
    gfx_hline(c, 16, 178, l.side_w - 32, theme.border);
    char s[300];
    snprintf(s, sizeof(s), "TABELAS  ·  %d", t->total_tables);
    font_draw(c, 18, 194, s, theme.text_dim, 0);
    int end_y = c->h - 182, visible = imax(1, (end_y - 225) / 36);
    t->table_top = imin(t->table_top, imax(0, t->ntables - visible));
    rect_t saved = c->clip;
    canvas_clip(c, R(8, 224, l.side_w - 16, imax(0, end_y - 224)));
    for (int i = 0; i < visible; i++) {
        int ti = t->table_top + i, y = 224 + i * 36;
        if (ti >= t->ntables) break;
        bool selected = ti == t->selected;
        if (selected) {
            gfx_round_fill(c, 10, y, l.side_w - 20, 31, 6, color_with_alpha(green(), 24));
            gfx_round_fill(c, 10, y + 7, 3, 17, 1, green());
        }
        table_glyph(c, 22, y + 8, selected ? green() : theme.text_dim);
        font_draw_ellipsis(c, 47, y + 8, l.side_w - 62, t->tables[ti], selected ? green() : theme.text, selected ? FONT_BOLD : 0);
    }
    if (!t->ntables) font_draw(c, 18, 233, "Nenhuma tabela ainda", theme.text_dim, 0);
    c->clip = saved;
    card(c, R(14, c->h - 172, l.side_w - 28, 75), theme.surface);
    font_draw(c, 26, c->h - 159, "SÓ NESTA SESSÃO", green(), 0);
    font_draw(c, 26, c->h - 136, "Dados na RAM.", theme.text_dim, 0);
    font_draw(c, 26, c->h - 116, "Somem ao reiniciar.", theme.text_dim, 0);
    ui_button(c, l.new_db, "Novo", BTN_STANDARD, t->hover == A_NEW ? UI_HOVER : UI_NORMAL);
    ui_button(c, l.open_db, "Abrir", BTN_STANDARD, t->hover == A_OPEN ? UI_HOVER : UI_NORMAL);

    font_draw(c, l.main_x + 2, 88, t->modified ? "Consulta SQL  •" : "Consulta SQL", theme.text, FONT_BOLD);
    ui_button(c, l.examples, "Exemplos", BTN_STANDARD, t->hover == A_DEMO ? UI_HOVER : UI_NORMAL);
    gfx_round_fill(c, l.run.x, l.run.y, l.run.w, l.run.h, 6,
                   t->hover == A_RUN ? color_mix(green(), theme.text, 30) : green());
    gfx_triangle(c, l.run.x + 14, l.run.y + 9, l.run.x + 14, l.run.y + 21, l.run.x + 23, l.run.y + 15,
                 theme.dark ? HEX(0x14291E) : HEX(0xFFFFFF));
    font_draw(c, l.run.x + 33, l.run.y + 7, "Executar", theme.dark ? HEX(0x14291E) : HEX(0xFFFFFF), FONT_BOLD);
    draw_editor(t, c, l);

    const char *tabs[] = { "Resultados", "Estrutura", "Histórico" };
    int x = l.tabs.x;
    for (int i = 0; i < 3; i++) {
        int width = i == 0 ? 114 : 100;
        font_draw(c, x + 10, l.tabs.y + 9, tabs[i], t->tab == i ? green() : theme.text_dim, t->tab == i ? FONT_BOLD : 0);
        if (t->tab == i) gfx_round_fill(c, x + 7, l.tabs.y + 32, width - 18, 3, 1, green());
        x += width;
    }
    if (t->tab == TAB_DATA && !t->failed && t->output.count > 1) {
        int number = 1;
        for (tatu_result_t *r = t->output.first; r && r != t->result; r = r->next) number++;
        snprintf(s, sizeof(s), "%d/%d", number, t->output.count);
        font_draw(c, l.prev.x - 48, l.tabs.y + 9, s, theme.text_dim, 0);
        glyph_draw(c, GLYPH_BACK, l.prev.x + 4, l.prev.y + 6, 16, theme.text_dim);
        glyph_draw(c, GLYPH_FORWARD, l.next.x + 4, l.next.y + 6, 16, theme.text_dim);
    }
    if (t->tab == TAB_DATA) draw_data(t, c, l);
    else if (t->tab == TAB_SCHEMA) draw_schema(t, c, l);
    else draw_history(t, c, l);

    int sy = c->h - 26;
    gfx_fill(c, 0, sy, c->w, 26, theme.surface_alt);
    gfx_hline(c, 0, sy, c->w, theme.border);
    gfx_circle(c, 16, sy + 13, 3, t->failed ? red() : green());
    font_draw_ellipsis(c, 28, sy + 5, c->w - 288, t->message, t->failed ? red() : theme.text_dim, 0);
    const char *hint = tatu_in_transaction(t->db) ? "Transação aberta  ·  Ctrl+Enter" : "UTF-8  ·  Ctrl+Enter executa";
    font_draw(c, c->w - font_width(hint, 0) - 14, sy + 5, hint, theme.text_dim, 0);
    win_invalidate(t->win);
}

static void show_error(toca_t *t, const tatu_error_t *err) {
    t->error = *err;
    t->failed = true;
    t->tab = TAB_DATA;
    strlcpy(t->message, err->message, sizeof(t->message));
}

static bool save_query(toca_t *t, bool choose_path) {
    char path[VFS_PATH_MAX];
    strlcpy(path, t->query_path[0] ? t->query_path : "/home/usuario/Documentos/consulta.sql", sizeof(path));
    if ((choose_path || !t->query_path[0]) &&
        !ui_file_dialog(t->win, true, "Salvar consulta SQL", path, sizeof(path))) return false;
    if (!vfs_write_all(path, t->buf, (size_t)t->len)) {
        ui_message(t->win, "Salvar consulta", "Não foi possível gravar a consulta nesse caminho.", "OK", NULL, NULL);
        return false;
    }
    strlcpy(t->query_path, path, sizeof(t->query_path));
    t->modified = false;
    strlcpy(t->message, "Consulta salva no sistema de arquivos em RAM.", sizeof(t->message));
    kprintf("toca: consulta salva\n");
    return true;
}

static bool keep_query(toca_t *t) {
    if (!t->modified) return true;
    int answer = ui_message(t->win, "Consulta alterada", "Deseja salvar a consulta antes de continuar?",
                            "Salvar", "Descartar", "Cancelar");
    if (answer == 1) return save_query(t, false);
    return answer == 2;
}

static bool keep_connection(toca_t *t) {
    if (!keep_query(t)) return false;
    if (tatu_in_transaction(t->db)) {
        int answer = ui_message(t->win, "Transação aberta",
                                "Trocar ou fechar o banco desfaz a transação em andamento.",
                                "Desfazer e continuar", "Cancelar", NULL);
        if (answer != 1) return false;
    } else if (!tatu_path(t->db) && t->memory_changed) {
        int answer = ui_message(t->win, "Banco em memória",
                                "Há alterações no banco em memória. Salve o banco pelo menu Arquivo para guardá-las nesta sessão.",
                                "Descartar banco", "Cancelar", NULL);
        if (answer != 1) return false;
    }
    return true;
}

static void replace_db(toca_t *t, tatu_db_t *db) {
    tatu_output_free(&t->output);
    t->result = NULL;
    tatu_close(t->db);
    t->db = db;
    t->failed = t->memory_changed = false;
    t->selected = -1;
    t->query_path[0] = 0;
    t->row_top = t->grid_x = t->schema_top = t->table_top = 0;
    t->cell_row = t->cell_col = -1;
    set_text(t, "-- Escreva sua consulta aqui.\nSELECT 1 AS inicio;");
    refresh_tables(t);
    title(t);
    strlcpy(t->message, "Banco conectado. As alterações confirmadas são salvas no arquivo em RAM.", sizeof(t->message));
}

static void open_database(toca_t *t, bool create) {
    if (!keep_connection(t)) return;
    char path[VFS_PATH_MAX];
    strlcpy(path, create ? "/home/usuario/Documentos/meu-banco.tatu" : "/home/usuario/Documentos/", sizeof(path));
    if (!ui_file_dialog(t->win, create, create ? "Criar banco Tatu" : "Abrir banco Tatu", path, sizeof(path))) return;
    vnode_t *f = vfs_lookup(NULL, path);
    if (create && f) {
        ui_message(t->win, "Criar banco", "Esse arquivo já existe. Use Abrir para acessar o banco existente.", "OK", NULL, NULL);
        return;
    }
    if (!create && (!f || f->type != VFS_FILE)) return;
    const char *current = tatu_path(t->db);
    if (current && !strcmp(current, path)) {
        strlcpy(t->message, "Esse banco já está conectado.", sizeof(t->message));
        return;
    }
    tatu_error_t err = {0};
    tatu_db_t *db = tatu_open(path, &err);
    if (!db) { show_error(t, &err); return; }
    replace_db(t, db);
    kprintf("toca: banco %s\n", create ? "criado" : "aberto");
    if (create) set_text(t, "-- Seu primeiro banco. Execute e depois explore a tabela.\nCREATE TABLE notas (id INTEGER PRIMARY KEY, texto TEXT);\nINSERT INTO notas VALUES (1, 'Olá, Capivara!');\nSELECT * FROM notas;");
}

static void save_database(toca_t *t) {
    char path[VFS_PATH_MAX];
    strlcpy(path, tatu_path(t->db) ? tatu_path(t->db) : "/home/usuario/Documentos/laboratorio.tatu", sizeof(path));
    if (!ui_file_dialog(t->win, true, "Salvar banco como", path, sizeof(path))) return;
    tatu_error_t err = {0};
    if (!tatu_save_as(t->db, path, &err)) { show_error(t, &err); return; }
    t->memory_changed = false;
    t->failed = false;
    title(t);
    strlcpy(t->message, "Banco salvo em .tatu na RAM. Reiniciar o Capivara apaga os arquivos.", sizeof(t->message));
    kprintf("toca: banco salvo\n");
}

static void load_query(toca_t *t) {
    if (!keep_query(t)) return;
    char path[VFS_PATH_MAX] = "/home/usuario/Documentos/";
    if (!ui_file_dialog(t->win, false, "Abrir consulta SQL", path, sizeof(path))) return;
    vnode_t *f = vfs_lookup(NULL, path);
    const size_t size = f ? (size_t)f->size : 0;
    if (!f || f->type != VFS_FILE || size > SQL_MAX) {
        ui_message(t->win, "Abrir consulta", "Escolha um arquivo de texto de até 256 KiB.", "OK", NULL, NULL);
        return;
    }
    char *text = kmalloc(size + 1);
    if (!text) return;
    long n = vfs_read(f, 0, text, size);
    if (n >= 0) {
        text[n] = 0;
        set_text(t, text);
        strlcpy(t->query_path, path, sizeof(t->query_path));
        kprintf("toca: consulta aberta\n");
    }
    kfree(text);
}

/* O CSV escreve o texto completo, mesmo quando a célula é abreviada na tela. */
typedef struct { char *buf; size_t len, cap; } csv_t;

static bool csv_add(csv_t *b, const char *s, size_t n) {
    if (n > (size_t)32 * 1024 * 1024 || b->len > (size_t)32 * 1024 * 1024 - n) return false;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->len + n + 1) cap *= 2;
        char *p = krealloc(b->buf, cap);
        if (!p) return false;
        b->buf = p;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
    return true;
}

static bool csv_field(csv_t *b, const char *s) {
    if (!csv_add(b, "\"", 1)) return false;
    const char *start = s;
    for (; *s; s++) {
        if (*s != '"') continue;
        if (!csv_add(b, start, (size_t)(s - start)) || !csv_add(b, "\"\"", 2)) return false;
        start = s + 1;
    }
    return csv_add(b, start, (size_t)(s - start)) && csv_add(b, "\"", 1);
}

static void export_csv(toca_t *t) {
    tatu_result_t *r = t->result;
    if (!r || !r->ncols || t->failed) return;
    char path[VFS_PATH_MAX] = "/home/usuario/Documentos/resultado.csv";
    if (!ui_file_dialog(t->win, true, "Exportar resultado CSV", path, sizeof(path))) return;
    csv_t b = {0};
    bool ok = true;
    for (int row = -1; row < r->nrows && ok; row++) {
        for (int col = 0; col < r->ncols && ok; col++) {
            if (col) ok = csv_add(&b, ",", 1);
            if (!ok) break;
            char text[256];
            const char *s;
            if (row < 0) s = r->cols[col];
            else {
                tatu_value_t *v = &r->cells[(size_t)row * r->ncols + col];
                if (v->type == TV_TEXT) s = v->s;
                else { tatu_format_cell(v, text, sizeof(text)); s = text; }
            }
            ok = csv_field(&b, s);
        }
        if (ok) ok = csv_add(&b, "\r\n", 2);
    }
    if (ok) ok = vfs_write_all(path, b.buf, b.len);
    kfree(b.buf);
    if (ok) {
        strlcpy(t->message, "Resultado exportado em CSV para o sistema de arquivos em RAM.", sizeof(t->message));
        kprintf("toca: CSV exportado\n");
    }
    else ui_message(t->win, "Exportar CSV", "Não foi possível exportar. Confira o caminho e o limite de 32 MiB.", "OK", NULL, NULL);
}

static void copy(toca_t *t) {
    if (t->focus == FOCUS_EDITOR) {
        if (has_selection(t)) clipboard_set(t->buf + sa(t), sb(t) - sa(t));
    } else if (t->result && t->cell_row >= 0 && t->cell_row < t->result->nrows &&
               t->cell_col >= 0 && t->cell_col < t->result->ncols) {
        tatu_value_t *v = &t->result->cells[(size_t)t->cell_row * t->result->ncols + t->cell_col];
        if (v->type == TV_TEXT) clipboard_set(v->s, (int)strlen(v->s));
        else {
            char text[256];
            tatu_format_cell(v, text, sizeof(text));
            clipboard_set(text, (int)strlen(text));
        }
        strlcpy(t->message, "Célula copiada.", sizeof(t->message));
    }
}

static void paste(toca_t *t) {
    int n = 0;
    const char *text = clipboard_get(&n);
    if (text && n > 0) { t->focus = FOCUS_EDITOR; insert(t, text, n); }
}

static void sample_queries(toca_t *t) {
    if (!keep_query(t)) return;
    ui_menu_item_t items[6];
    static const char *const labels[] = { "Explorar os tatus", "Cruzar tabelas", "Agrupar e contar", "Entender o plano", "Ordenar resultados", "Outra consulta" };
    int n = imin(6, tatu_demo_query_count);
    for (int i = 0; i < n; i++) items[i] = (ui_menu_item_t){ labels[i], i + 1, 0, NULL, false, false };
    rect_t cr = win_client_rect(t->win), r = layout(t).examples;
    int pick = ui_menu(cr.x + r.x, cr.y + r.y + r.h + 3, items, n);
    if (pick > 0 && pick <= n) {
        set_text(t, tatu_demo_queries[pick - 1]);
        t->query_path[0] = 0;
        strlcpy(t->message, "Exemplo no editor. Ctrl+Enter executa no banco atual.", sizeof(t->message));
    }
}

static void new_demo(toca_t *t) {
    if (!keep_connection(t)) return;
    tatu_error_t err = {0};
    tatu_db_t *db = tatu_open(NULL, &err);
    if (!db) { show_error(t, &err); return; }
    replace_db(t, db);
    execute(t, tatu_demo_sql, false);
    if (!t->failed) {
        set_text(t, tatu_demo_query_count ? tatu_demo_queries[0] : "SELECT 1;");
        execute(t, t->buf, false);
        for (int i = 0; i < t->ntables; i++)
            if (!strcmp(t->tables[i], "especies")) t->selected = i;
    }
    t->memory_changed = false;
}

static void action(toca_t *t, int id) {
    switch (id) {
    case A_NEW: open_database(t, true); break;
    case A_OPEN: open_database(t, false); break;
    case A_DEMO: new_demo(t); break;
    case A_SAVE_DB: save_database(t); break;
    case A_SAVE_QUERY: save_query(t, false); break;
    case A_LOAD_QUERY: load_query(t); break;
    case A_EXPORT: export_csv(t); break;
    case A_EXIT: if (keep_connection(t)) t->quit = true; break;
    case A_RUN: run_editor(t); break;
    case A_COPY: copy(t); break;
    case A_CUT:
        if (t->focus == FOCUS_EDITOR) { copy(t); delete_range(t, sa(t), sb(t)); }
        break;
    case A_PASTE: paste(t); break;
    case A_ALL: t->focus = FOCUS_EDITOR; t->anchor = 0; t->cur = t->len; break;
    case A_BEGIN: execute(t, "BEGIN;", true); break;
    case A_COMMIT: execute(t, "COMMIT;", true); break;
    case A_ROLLBACK: execute(t, "ROLLBACK;", true); break;
    case A_ABOUT:
        ui_message(t->win, "Sobre a Toca",
                   "Toca é o cliente nativo do Tatu DB no Capivara OS. SQL, catálogo, resultados e transações em uma bancada didática.\n\nOs arquivos do Capivara ficam na RAM e somem ao reiniciar.",
                   "Entendi", NULL, NULL);
        break;
    }
}

static void menu(toca_t *t, int which, int sx, int sy) {
    int pick = 0;
    if (which == 0) {
        ui_menu_item_t m[] = {
            { "Novo banco...", A_NEW, GLYPH_NEW_FILE, "Ctrl+N", false, false },
            { "Abrir banco...", A_OPEN, GLYPH_OPEN, "Ctrl+O", false, false },
            { "Salvar banco como...", A_SAVE_DB, GLYPH_SAVE, "Ctrl+Shift+S", false, false },
            { "Novo laboratório de exemplo", A_DEMO, 0, NULL, false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Abrir consulta SQL...", A_LOAD_QUERY, GLYPH_OPEN, NULL, false, false },
            { "Salvar consulta SQL...", A_SAVE_QUERY, GLYPH_SAVE, "Ctrl+S", false, false },
            { "Exportar resultado CSV...", A_EXPORT, 0, NULL, !t->result || !t->result->ncols || t->failed, false },
            { NULL, 0, 0, NULL, false, true },
            { "Fechar", A_EXIT, 0, NULL, false, false },
        };
        pick = ui_menu(sx, sy, m, sizeof(m) / sizeof(m[0]));
    } else if (which == 1) {
        ui_menu_item_t m[] = {
            { "Recortar", A_CUT, GLYPH_CUT, "Ctrl+X", t->focus != FOCUS_EDITOR || !has_selection(t), false },
            { "Copiar", A_COPY, GLYPH_COPY, "Ctrl+C", false, false },
            { "Colar", A_PASTE, GLYPH_PASTE, "Ctrl+V", false, false },
            { "Selecionar todo o SQL", A_ALL, 0, "Ctrl+A", false, false },
        };
        pick = ui_menu(sx, sy, m, sizeof(m) / sizeof(m[0]));
    } else if (which == 2) {
        bool tx = tatu_in_transaction(t->db);
        ui_menu_item_t m[] = {
            { "Executar SQL / seleção", A_RUN, 0, "Ctrl+Enter / F5", false, false },
            { NULL, 0, 0, NULL, false, true },
            { "Iniciar transação", A_BEGIN, 0, "BEGIN", tx, false },
            { "Confirmar transação", A_COMMIT, GLYPH_CHECK, "COMMIT", !tx, false },
            { "Desfazer transação", A_ROLLBACK, GLYPH_BACK, "ROLLBACK", !tx, false },
        };
        pick = ui_menu(sx, sy, m, sizeof(m) / sizeof(m[0]));
    } else {
        ui_menu_item_t m[] = { { "Sobre a Toca", A_ABOUT, GLYPH_SEARCH, NULL, false, false } };
        pick = ui_menu(sx, sy, m, 1);
    }
    if (pick) action(t, pick);
}

static void query_table(toca_t *t, int index) {
    if (index < 0 || index >= t->ntables || !keep_query(t)) return;
    t->selected = index;
    size_t size = strlen(t->tables[index]) * 2 + 64;
    if (size > SQL_MAX) return;
    char *sql = kmalloc(size);
    if (!sql) return;
    int n = 0;
    const char *prefix = "SELECT * FROM \"";
    while (*prefix) sql[n++] = *prefix++;
    for (const char *p = t->tables[index]; *p; p++) {
        sql[n++] = *p;
        if (*p == '"') sql[n++] = '"';
    }
    strlcpy(sql + n, "\" LIMIT 200;", size - (size_t)n);
    set_text(t, sql);
    kfree(sql);
    t->query_path[0] = 0;
    t->schema_top = 0;
    execute(t, t->buf, true);
}

static void result_move(toca_t *t, bool next) {
    tatu_result_t *prior = NULL;
    for (tatu_result_t *r = t->output.first; r; r = r->next) {
        if (r == t->result) {
            if (next && r->next) t->result = r->next;
            if (!next && prior) t->result = prior;
            break;
        }
        prior = r;
    }
    t->row_top = t->grid_x = 0;
    t->cell_row = t->cell_col = -1;
    if (t->result && !t->failed) result_message(t);
}

static void grid_key(toca_t *t, event_t *e) {
    if (!t->result || !t->result->nrows || !t->result->ncols) return;
    int rows = imax(1, layout(t).grid.h / ROW_H);
    int row = imax(0, t->cell_row), col = imax(0, t->cell_col);
    switch (e->key) {
    case KEY_UP: row--; break;
    case KEY_DOWN: row++; break;
    case KEY_LEFT: col--; break;
    case KEY_RIGHT: col++; break;
    case KEY_PGUP: row -= rows; break;
    case KEY_PGDN: row += rows; break;
    case KEY_HOME: row = 0; break;
    case KEY_END: row = t->result->nrows - 1; break;
    case KEY_ESC: t->focus = FOCUS_EDITOR; return;
    default: return;
    }
    t->cell_row = imin(t->result->nrows - 1, imax(0, row));
    t->cell_col = imin(t->result->ncols - 1, imax(0, col));
    if (t->cell_row < t->row_top) t->row_top = t->cell_row;
    if (t->cell_row >= t->row_top + rows) t->row_top = t->cell_row - rows + 1;
    int x = 46;
    for (int i = 0; i < t->cell_col; i++) x += col_width(t->result, i);
    int width = col_width(t->result, t->cell_col), visible = layout(t).grid.w - 12;
    if (x < t->grid_x) t->grid_x = x;
    if (x + width > t->grid_x + visible) t->grid_x = x + width - visible;
}

static void key(toca_t *t, event_t *e) {
    bool ctrl = e->mods & MOD_CTRL, shift = e->mods & MOD_SHIFT;
    if ((ctrl && e->key == KEY_ENTER) || e->key == KEY_F5) { run_editor(t); return; }
    if (ctrl && e->key == KEY_CHAR) {
        switch (tolower((int)e->ch)) {
        case 's': action(t, shift ? A_SAVE_DB : A_SAVE_QUERY); return;
        case 'o': action(t, A_OPEN); return;
        case 'n': action(t, A_NEW); return;
        case 'a': action(t, A_ALL); return;
        case 'c': action(t, A_COPY); return;
        case 'x': action(t, A_CUT); return;
        case 'v': action(t, A_PASTE); return;
        }
        return;
    }
    if (t->focus == FOCUS_GRID) { grid_key(t, e); return; }
    int line = line_of(t, t->cur), col = column(t, t->cur);
    bool move = true, vertical = false;
    switch (e->key) {
    case KEY_LEFT:
        t->cur = has_selection(t) && !shift ? sa(t) : (int)utf8_prev(t->buf, (size_t)t->cur);
        break;
    case KEY_RIGHT:
        t->cur = has_selection(t) && !shift ? sb(t) : (int)utf8_next(t->buf, (size_t)t->len, (size_t)t->cur);
        break;
    case KEY_UP: t->cur = pos_at(t, line - 1, t->want_col); vertical = true; break;
    case KEY_DOWN: t->cur = pos_at(t, line + 1, t->want_col); vertical = true; break;
    case KEY_HOME: t->cur = ctrl ? 0 : t->lines[line]; break;
    case KEY_END: t->cur = ctrl ? t->len : line_end(t, line); break;
    case KEY_PGUP: t->cur = pos_at(t, line - imax(1, layout(t).editor.h / LINE_H - 1), col); vertical = true; break;
    case KEY_PGDN: t->cur = pos_at(t, line + imax(1, layout(t).editor.h / LINE_H - 1), col); vertical = true; break;
    default: move = false; break;
    }
    if (move) {
        if (!shift) t->anchor = t->cur;
        if (!vertical) t->want_col = column(t, t->cur);
        return;
    }
    switch (e->key) {
    case KEY_ENTER: insert(t, "\n", 1); break;
    case KEY_TAB: insert(t, "    ", 4); break;
    case KEY_BACKSPACE:
        if (has_selection(t)) delete_range(t, sa(t), sb(t));
        else if (t->cur) delete_range(t, (int)utf8_prev(t->buf, (size_t)t->cur), t->cur);
        break;
    case KEY_DELETE:
        if (has_selection(t)) delete_range(t, sa(t), sb(t));
        else if (t->cur < t->len) delete_range(t, t->cur, (int)utf8_next(t->buf, (size_t)t->len, (size_t)t->cur));
        break;
    case KEY_CHAR:
        if (e->ch >= 32 && !(e->mods & MOD_ALT)) {
            char text[4];
            int n = utf8_encode(e->ch, text);
            insert(t, text, n);
        }
        break;
    }
    t->want_col = column(t, t->cur);
}

static void mouse(toca_t *t, event_t *e) {
    layout_t l = layout(t);
    if (e->button != 1) return;
    if (ui_hit(l.run, e)) { run_editor(t); return; }
    if (ui_hit(l.examples, e)) { sample_queries(t); return; }
    if (ui_hit(l.new_db, e)) { open_database(t, true); return; }
    if (ui_hit(l.open_db, e)) { open_database(t, false); return; }
    if (ui_hit(l.editor, e)) {
        t->focus = FOCUS_EDITOR;
        if (e->x >= l.editor.x + l.editor.w - 14) {
            t->top = ui_scrollbar_offset(l.editor, t->nlines, imax(1, (l.editor.h - 20) / LINE_H), e->y);
            return;
        }
        t->cur = point_pos(t, e->x, e->y);
        if (!(e->mods & MOD_SHIFT)) t->anchor = t->cur;
        if (e->clicks >= 3) {
            int line = line_of(t, t->cur);
            t->anchor = t->lines[line];
            t->cur = line_end(t, line);
        } else if (e->clicks == 2) {
            int a = t->cur, b = t->cur;
            while (a > 0 && (isalnum((unsigned char)t->buf[a - 1]) || t->buf[a - 1] == '_' || (unsigned char)t->buf[a - 1] >= 128)) a--;
            while (b < t->len && (isalnum((unsigned char)t->buf[b]) || t->buf[b] == '_' || (unsigned char)t->buf[b] >= 128)) b++;
            t->anchor = a;
            t->cur = b;
        } else t->selecting = true;
        t->want_col = column(t, t->cur);
        return;
    }
    if (e->x < l.side_w && e->y >= 224 && e->y < win_canvas(t->win)->h - 182) {
        query_table(t, t->table_top + (e->y - 224) / 36);
        return;
    }
    if (ui_hit(l.tabs, e)) {
        if (t->tab == TAB_DATA && !t->failed && t->output.count > 1) {
            if (ui_hit(l.prev, e)) { result_move(t, false); return; }
            if (ui_hit(l.next, e)) { result_move(t, true); return; }
        }
        int x = e->x - l.tabs.x;
        if (x < 114) t->tab = TAB_DATA;
        else if (x < 214) t->tab = TAB_SCHEMA;
        else if (x < 314) t->tab = TAB_HISTORY;
        kprintf("toca: aba %s\n", t->tab == TAB_DATA ? "resultados" : t->tab == TAB_SCHEMA ? "estrutura" : "histórico");
        return;
    }
    if (t->tab == TAB_HISTORY && ui_hit(l.results, e) && e->y >= l.results.y + 36) {
        int i = t->history_top + (e->y - l.results.y - 36) / 44;
        if (i < t->nhistory && t->history[i].sql && keep_query(t)) {
            set_text(t, t->history[i].sql);
            t->query_path[0] = 0;
        }
        return;
    }
    if (t->tab == TAB_DATA && !t->failed && t->result && t->result->ncols) {
        if (ui_hit(l.hscroll, e)) {
            int total = grid_width(t);
            if (total > l.hscroll.w)
                t->grid_x = imin(total - l.hscroll.w, imax(0, (e->x - l.hscroll.x) * total / l.hscroll.w - l.hscroll.w / 2));
            return;
        }
        if (ui_hit(l.grid, e)) {
            if (e->x >= l.grid.x + l.grid.w - 12) {
                t->row_top = ui_scrollbar_offset(l.grid, t->result->nrows, imax(1, l.grid.h / ROW_H), e->y);
                return;
            }
            int row = t->row_top + (e->y - l.grid.y) / ROW_H;
            if (row >= t->result->nrows) return;
            int x = l.grid.x - t->grid_x + 46;
            for (int col = 0; col < t->result->ncols; col++) {
                int width = col_width(t->result, col);
                if (e->x >= x && e->x < x + width) {
                    t->cell_row = row;
                    t->cell_col = col;
                    t->focus = FOCUS_GRID;
                    if (e->clicks >= 2) copy(t);
                    break;
                }
                x += width;
            }
        }
    }
}

void toca_main(void *arg) {
    toca_t *t = kcalloc(1, sizeof(toca_t));
    if (!t) { kfree(arg); return; }
    t->selected = t->cell_row = t->cell_col = -1;
    t->cursor_on = true;
    t->win = win_create("Toca — Tatu DB", ICON_TATU, WIN_AUTO, WIN_AUTO, 940, 596, WF_APP);
    if (!t->win) { kfree(arg); kfree(t); return; }
    win_set_min_size(t->win, 740, 480);
    win_set_menus(t->win, "Arquivo|Editar|Consulta|Ajuda");
    tatu_error_t err = {0};
    t->db = tatu_open(arg ? (const char *)arg : NULL, &err);
    bool loaded = t->db != NULL;
    tatu_error_t open_error = err;
    if (!t->db) t->db = tatu_open(NULL, &err);
    if (!t->db) {
        ui_message(t->win, "Toca", err.message, "Fechar", NULL, NULL);
        win_destroy(t->win);
        kfree(arg);
        kfree(t);
        return;
    }
    set_text(t, tatu_demo_query_count ? tatu_demo_queries[0] : "SELECT 1;");
    if (!t->buf || !t->nlines) {
        tatu_close(t->db);
        win_destroy(t->win);
        kfree(arg);
        kfree(t->lines);
        kfree(t->buf);
        kfree(t);
        return;
    }
    if (!arg) {
        execute(t, tatu_demo_sql, false);
        if (!t->failed) execute(t, t->buf, false);
        for (int i = 0; i < t->ntables; i++)
            if (!strcmp(t->tables[i], "especies")) t->selected = i;
        t->memory_changed = false;
    } else if (loaded) {
        refresh_tables(t);
        if (t->ntables) query_table(t, 0);
        else {
            set_text(t, "-- Banco conectado. Escreva uma consulta SQL.\nSELECT 1 AS inicio;");
            strlcpy(t->message, "Banco conectado. Escreva SQL para criar suas tabelas.", sizeof(t->message));
        }
    } else show_error(t, &open_error);
    kfree(arg);
    title(t);
    ev_set_timer(550);
    draw(t);
    kprintf("toca: cliente pronto\n");
    while (!t->quit) {
        event_t e;
        if (!gui_wait(&e)) continue;
        layout_t l = layout(t);
        switch (e.type) {
        case EV_CLOSE: action(t, A_EXIT); break;
        case EV_KEY_DOWN:
            key(t, &e);
            ensure_cursor(t);
            t->cursor_on = true;
            break;
        case EV_MOUSE_DOWN: mouse(t, &e); t->cursor_on = true; break;
        case EV_MOUSE_UP: t->selecting = false; break;
        case EV_MOUSE_MOVE: {
            bool edit = ui_hit(l.editor, &e);
            win_set_cursor(t->win, edit ? CURSOR_IBEAM : CURSOR_ARROW);
            int hover = ui_hit(l.run, &e) ? A_RUN : ui_hit(l.examples, &e) ? A_DEMO :
                        ui_hit(l.new_db, &e) ? A_NEW : ui_hit(l.open_db, &e) ? A_OPEN : 0;
            if (t->selecting) { t->cur = point_pos(t, e.x, e.y); ensure_cursor(t); }
            else if (hover == t->hover) continue;
            t->hover = hover;
            break;
        }
        case EV_MOUSE_LEAVE: t->hover = 0; break;
        case EV_MOUSE_WHEEL:
            if (ui_hit(l.editor, &e)) t->top = imax(0, imin(imax(0, t->nlines - (l.editor.h - 20) / LINE_H), t->top - e.wheel * 3));
            else if (e.x < l.side_w) t->table_top = imax(0, t->table_top - e.wheel * 3);
            else if (t->tab == TAB_SCHEMA) t->schema_top = imax(0, t->schema_top - e.wheel * 3);
            else if (t->tab == TAB_HISTORY) t->history_top = imin(imax(0, t->nhistory - 1), imax(0, t->history_top - e.wheel * 2));
            else if (e.mods & MOD_SHIFT) t->grid_x = imax(0, t->grid_x - e.wheel * 60);
            else t->row_top = imax(0, t->row_top - e.wheel * 3);
            break;
        case EV_MENU: menu(t, (int)e.key, e.x, e.y); break;
        case EV_TIMER:
            if (t->focus != FOCUS_EDITOR || !win_is_focused(t->win)) continue;
            t->cursor_on = !t->cursor_on;
            /* Somente o editor muda quando o cursor pisca. */
            draw_editor(t, win_canvas(t->win), l);
            win_invalidate_rect(t->win, l.editor);
            continue;
        case EV_FOCUS: case EV_BLUR: case EV_THEME: case EV_RESIZE: break;
        default: continue;
        }
        if (!t->quit) draw(t);
    }
    ev_set_timer(0);
    tatu_output_free(&t->output);
    tatu_close(t->db);
    for (int i = 0; i < t->nhistory; i++) kfree(t->history[i].sql);
    for (int i = 0; i < t->ntables; i++) kfree(t->tables[i]);
    kfree(t->lines);
    kfree(t->buf);
    win_destroy(t->win);
    kfree(t);
}
