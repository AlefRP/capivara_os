#include "gui/ui.h"
#include "dev/keyboard.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "lib/printf.h"
#include "mm/heap.h"

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ */
/* Área de transferência                                                */
/* ------------------------------------------------------------------ */
static char *clip_data;
static int clip_len;

/* histórico: os últimos textos copiados (o mais recente primeiro) */
static char *history[CLIP_HISTORY];
static int history_len[CLIP_HISTORY], history_n;
static mutex_t clip_lock;

void clipboard_set(const char *s, int len) {
    char *n = kmalloc(len + 1), *h = kmalloc(len + 1);
    if (!n || !h) { kfree(n); kfree(h); return; }
    memcpy(n, s, len);
    n[len] = 0;
    memcpy(h, n, len + 1);

    mutex_lock(&clip_lock);
    char *old = clip_data;
    clip_data = n;
    clip_len = len;
    /* remove duplicata e põe no topo do histórico */
    for (int i = 0; i < history_n; i++) {
        if (history_len[i] == len && !memcmp(history[i], s, len)) {
            kfree(history[i]);
            memmove(&history[i], &history[i + 1], (history_n - i - 1) * sizeof(char *));
            memmove(&history_len[i], &history_len[i + 1], (history_n - i - 1) * sizeof(int));
            history_n--;
            break;
        }
    }
    if (history_n == CLIP_HISTORY) kfree(history[--history_n]);
    memmove(&history[1], &history[0], history_n * sizeof(char *));
    memmove(&history_len[1], &history_len[0], history_n * sizeof(int));
    history[0] = h;
    history_len[0] = len;
    history_n++;
    mutex_unlock(&clip_lock);
    kfree(old);
}

int clipboard_history_count(void) { return history_n; }

const char *clipboard_history_get(int i, int *len) {
    if (i < 0 || i >= history_n) return NULL;
    *len = history_len[i];
    return history[i];
}

const char *clipboard_get(int *len) {
    *len = clip_len;
    return clip_data;
}

/* ------------------------------------------------------------------ */
/* Desenho de controles                                                 */
/* ------------------------------------------------------------------ */
void ui_text_center(canvas_t *c, rect_t r, const char *s, uint32_t color, int flags) {
    int w = font_width(s, flags);
    font_draw(c, r.x + (r.w - w) / 2, r.y + (r.h - font_height(flags)) / 2, s, color, flags);
}

void ui_button(canvas_t *c, rect_t r, const char *label, int style, ui_state_t st) {
    uint32_t bg, fg = theme.text, border = theme.control_border;
    if (style == BTN_ACCENT) {
        bg = st == UI_PRESSED ? theme.accent_pressed : st == UI_HOVER ? theme.accent_hover : theme.accent;
        fg = theme.accent_text;
        border = bg;
    } else if (style == BTN_FLAT) {
        bg = st == UI_PRESSED ? theme.pressed : st == UI_HOVER ? theme.hover : 0;
        border = 0;
    } else {
        bg = st == UI_PRESSED ? theme.window_bg : st == UI_HOVER ? theme.control_hover : theme.control;
    }
    if (st == UI_DISABLED) fg = theme.text_disabled;
    if (bg) gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, bg);
    if (border) gfx_round_rect(c, r.x, r.y, r.w, r.h, 4, border);
    ui_text_center(c, r, label, fg, 0);
}

void ui_glyph_button(canvas_t *c, rect_t r, int glyph, ui_state_t st) {
    if (st == UI_HOVER || st == UI_PRESSED)
        gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, st == UI_PRESSED ? theme.pressed : theme.hover);
    int s = 16;
    glyph_draw(c, glyph, r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s,
               st == UI_DISABLED ? theme.text_disabled : theme.text);
}

/* Quebra de linha por palavras. Devolve a altura usada. */
int ui_wrap(canvas_t *c, rect_t r, const char *text, uint32_t color, int flags, bool draw) {
    int y = r.y, lh = font_height(flags) + 4;
    const char *p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int para = nl ? (int)(nl - p) : (int)strlen(p);
        if (para == 0) {
            y += lh;
            p += 1;
            continue;
        }
        const char *q = p, *end = p + para;
        while (q < end) {
            int fit = font_fit(q, r.w, flags);
            if (fit >= end - q) fit = (int)(end - q);
            else {
                int brk = fit;
                while (brk > 0 && q[brk] != ' ') brk--;
                if (brk > 0) fit = brk;
                if (fit == 0) fit = 1;
            }
            if (draw) font_draw_n(c, r.x, y, q, fit, color, flags);
            y += lh;
            q += fit;
            while (q < end && *q == ' ') q++;
        }
        p = nl ? nl + 1 : end;
    }
    return y - r.y;
}

void ui_scrollbar(canvas_t *c, rect_t t, int total, int visible, int offset) {
    if (total <= visible) return;
    int th = imax(24, t.h * visible / total);
    int ty = t.y + (int)((int64_t)(t.h - th) * offset / (total - visible));
    gfx_round_fill(c, t.x + t.w - 6, ty, 4, th, 2, theme.dark ? HEX(0x6A6A6A) : HEX(0xA8A8A8));
}

int ui_scrollbar_offset(rect_t t, int total, int visible, int y) {
    if (total <= visible) return 0;
    int th = imax(24, t.h * visible / total);
    int pos = y - t.y - th / 2;
    int range = t.h - th;
    if (range <= 0) return 0;
    int off = (int)((int64_t)pos * (total - visible) / range);
    return imax(0, imin(total - visible, off));
}

void ui_toggle(canvas_t *c, int x, int y, bool on, ui_state_t st) {
    uint32_t bg = on ? (st == UI_HOVER ? theme.accent_hover : theme.accent) : 0;
    if (on) gfx_round_fill(c, x, y, 40, 20, 10, bg);
    else gfx_round_rect(c, x, y, 40, 20, 10, theme.text_dim);
    float kx = on ? x + 30 : x + 10;
    gfx_circle(c, kx, y + 10, st == UI_HOVER ? 6.5f : 5.5f, on ? HEX(0xFFFFFF) : theme.text_dim);
}

void ui_checkbox(canvas_t *c, int x, int y, bool on) {
    if (on) {
        gfx_round_fill(c, x, y, 18, 18, 4, theme.accent);
        glyph_draw(c, GLYPH_CHECK, x + 1, y + 1, 16, HEX(0xFFFFFF));
    } else {
        gfx_round_rect(c, x, y, 18, 18, 4, theme.text_dim);
    }
}

/* ------------------------------------------------------------------ */
/* Campo de texto                                                       */
/* ------------------------------------------------------------------ */
void ui_edit_set(ui_edit_t *e, const char *s) {
    strlcpy(e->text, s, sizeof(e->text));
    e->len = (int)strlen(e->text);
    e->cur = e->len;
    e->scroll = 0;
}

bool ui_edit_key(ui_edit_t *e, const event_t *ev) {
    if (ev->type != EV_KEY_DOWN) return false;
    switch (ev->key) {
    case KEY_LEFT:  e->cur = (int)utf8_prev(e->text, e->cur); return false;
    case KEY_RIGHT: e->cur = (int)utf8_next(e->text, e->len, e->cur); return false;
    case KEY_HOME:  e->cur = 0; return false;
    case KEY_END:   e->cur = e->len; return false;
    case KEY_BACKSPACE:
        if (e->cur > 0) {
            int p = (int)utf8_prev(e->text, e->cur);
            memmove(e->text + p, e->text + e->cur, e->len - e->cur + 1);
            e->len -= e->cur - p;
            e->cur = p;
            return true;
        }
        return false;
    case KEY_DELETE:
        if (e->cur < e->len) {
            int n = (int)utf8_next(e->text, e->len, e->cur);
            memmove(e->text + e->cur, e->text + n, e->len - n + 1);
            e->len -= n - e->cur;
            return true;
        }
        return false;
    case KEY_CHAR:
        if (ev->ch >= 32 && !(ev->mods & (MOD_CTRL | MOD_ALT))) {
            char buf[4];
            int n = utf8_encode(ev->ch, buf);
            if (e->len + n >= (int)sizeof(e->text)) return false;
            memmove(e->text + e->cur + n, e->text + e->cur, e->len - e->cur + 1);
            memcpy(e->text + e->cur, buf, n);
            e->len += n;
            e->cur += n;
            return true;
        }
        return false;
    }
    return false;
}

void ui_edit_click(ui_edit_t *e, rect_t r, int x) {
    int target = x - (r.x + 8) + e->scroll;
    int pos = 0, w = 0;
    while (pos < e->len) {
        int next = (int)utf8_next(e->text, e->len, pos);
        int cw = font_width_n(e->text + pos, next - pos, 0);
        if (w + cw / 2 > target) break;
        w += cw;
        pos = next;
    }
    e->cur = pos;
}

void ui_edit_draw(canvas_t *c, rect_t r, ui_edit_t *e, bool focused, const char *placeholder) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, theme.dark ? HEX(0x2F2F2F) : HEX(0xFFFFFF));
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 4, theme.control_border);
    if (focused) gfx_fill(c, r.x + 2, r.y + r.h - 2, r.w - 4, 2, theme.accent);
    else gfx_fill(c, r.x + 2, r.y + r.h - 1, r.w - 4, 1, theme.text_dim);

    int inner = r.w - 16;
    int curx = font_width_n(e->text, e->cur, 0);
    if (curx - e->scroll > inner) e->scroll = curx - inner;
    if (curx < e->scroll) e->scroll = curx;
    rect_t saved = c->clip;
    canvas_clip(c, rect_intersect(saved, R(r.x + 4, r.y, r.w - 8, r.h)));
    int ty = r.y + (r.h - 16) / 2;
    if (!e->len && placeholder) font_draw(c, r.x + 8, ty, placeholder, theme.text_dim, 0);
    font_draw(c, r.x + 8 - e->scroll, ty, e->text, theme.text, 0);
    if (focused) gfx_vline(c, r.x + 8 + curx - e->scroll, ty, 16, theme.text);
    c->clip = saved;
}

/* ------------------------------------------------------------------ */
/* Lista rolável                                                        */
/* ------------------------------------------------------------------ */
void ui_list_clamp(ui_list_t *l, rect_t r) {
    int max = imax(0, l->count * l->row_h - r.h);
    l->scroll = imax(0, imin(max, l->scroll));
    if (l->sel >= l->count) l->sel = l->count - 1;
}

int ui_list_hit(ui_list_t *l, rect_t r, int y) {
    if (y < r.y || y >= r.y + r.h) return -1;
    int i = (y - r.y + l->scroll) / l->row_h;
    return i < l->count ? i : -1;
}

void ui_list_wheel(ui_list_t *l, rect_t r, int delta) {
    l->scroll -= delta * l->row_h * 3;
    ui_list_clamp(l, r);
}

void ui_list_ensure(ui_list_t *l, rect_t r) {
    if (l->sel < 0) return;
    int top = l->sel * l->row_h;
    if (top < l->scroll) l->scroll = top;
    if (top + l->row_h > l->scroll + r.h) l->scroll = top + l->row_h - r.h;
    ui_list_clamp(l, r);
}

bool ui_list_key(ui_list_t *l, rect_t r, const event_t *e) {
    if (e->type != EV_KEY_DOWN || !l->count) return false;
    int page = imax(1, r.h / l->row_h - 1);
    int s = l->sel;
    switch (e->key) {
    case KEY_UP:   s = s <= 0 ? 0 : s - 1; break;
    case KEY_DOWN: s = s < 0 ? 0 : imin(l->count - 1, s + 1); break;
    case KEY_HOME: s = 0; break;
    case KEY_END:  s = l->count - 1; break;
    case KEY_PGUP: s = imax(0, s - page); break;
    case KEY_PGDN: s = imin(l->count - 1, s + page); break;
    default: return false;
    }
    l->sel = s;
    ui_list_ensure(l, r);
    return true;
}

/* ------------------------------------------------------------------ */
/* Laços modais                                                         */
/*                                                                      */
/* Enquanto um menu/diálogo está aberto, eventos importantes das outras  */
/* janelas da mesma tarefa (redimensionar, fechar, tema) são guardados e */
/* reenviados no final; cliques e teclas nelas são ignorados.            */
/* ------------------------------------------------------------------ */
typedef struct {
    event_t ev[24];
    int n;
} deferred_t;

static void defer(deferred_t *d, const event_t *e) {
    switch (e->type) {
    case EV_RESIZE: case EV_CLOSE: case EV_THEME: case EV_WINLIST: case EV_SHELL:
        if (d->n < 24) d->ev[d->n++] = *e;
        break;
    }
}

static void repost(deferred_t *d) {
    for (int i = 0; i < d->n; i++) ev_post(task_current(), &d->ev[i]);
}

int ui_menu(int sx, int sy, const ui_menu_item_t *items, int n) {
    int w = 180, h = 8;
    for (int i = 0; i < n; i++) {
        if (items[i].separator) { h += 9; continue; }
        int lw = font_width(items[i].label, 0) + 48 + (items[i].shortcut ? font_width(items[i].shortcut, 0) + 32 : 0);
        w = imax(w, lw + 16);
        h += 32;
    }
    int scr_w = wm_screen_w(), scr_h = wm_screen_h();
    if (sx + w > scr_w) sx = imax(0, sx - w);
    if (sy + h > scr_h) sy = imax(0, sy - h);

    window_t *win = win_create("menu", 0, sx, sy, w, h,
                               WF_POPUP | WF_TOPMOST | WF_SHADOW | WF_ALPHA | WF_NOTASKBAR);
    if (!win) return 0;
    int hover = -1, result = 0;
    bool done = false;
    deferred_t def = { .n = 0 };

    while (!done) {
        canvas_t *c = win_canvas(win);
        gfx_clear(c, 0);
        gfx_round_fill(c, 0, 0, w, h, 10, theme.dark ? ARGB(0xF4, 0x2C2C2E) : ARGB(0xF4, 0xF6F6F6));
        gfx_round_rect(c, 0, 0, w, h, 10, theme.popup_border);
        int y = 4;
        for (int i = 0; i < n; i++) {
            if (items[i].separator) {
                gfx_hline(c, 1, y + 4, w - 2, theme.border);
                y += 9;
                continue;
            }
            bool hot = i == hover && !items[i].disabled;
            if (hot) gfx_round_fill(c, 5, y + 1, w - 10, 30, 6, theme.accent);      /* destaque na cor do sistema */
            uint32_t fg = items[i].disabled ? theme.text_disabled : hot ? HEX(0xFFFFFF) : theme.text;
            if (items[i].glyph) glyph_draw(c, items[i].glyph, 14, y + 8, 16, fg);
            font_draw(c, 44, y + 8, items[i].label, fg, 0);
            if (items[i].shortcut)
                font_draw(c, w - 16 - font_width(items[i].shortcut, 0), y + 8, items[i].shortcut,
                          hot ? ARGB(0xD0, 0xFFFFFF) : theme.text_dim, 0);
            y += 32;
        }
        win_invalidate(win);

        event_t e;
        gui_wait(&e);
        if (e.win != win) {
            if (e.type == EV_MOUSE_DOWN || e.type == EV_KEY_DOWN) done = true;
            else defer(&def, &e);
            continue;
        }
        int at = -1;
        if (e.type == EV_MOUSE_MOVE || e.type == EV_MOUSE_UP || e.type == EV_MOUSE_DOWN) {
            int yy = 4;
            for (int i = 0; i < n; i++) {
                int ih = items[i].separator ? 9 : 32;
                if (!items[i].separator && e.y >= yy && e.y < yy + ih && e.x >= 0 && e.x < w) at = i;
                yy += ih;
            }
        }
        switch (e.type) {
        case EV_MOUSE_MOVE: hover = at; break;
        case EV_MOUSE_LEAVE: hover = -1; break;
        case EV_MOUSE_UP:
            if (at >= 0 && !items[at].disabled) { result = items[at].id; done = true; }
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_ESC) done = true;
            else if (e.key == KEY_DOWN || e.key == KEY_UP) {
                for (int k = 0; k < n; k++) {
                    hover = (hover + (e.key == KEY_DOWN ? 1 : n - 1) + n) % n;
                    if (hover < 0) hover = 0;
                    if (!items[hover].separator && !items[hover].disabled) break;
                }
            } else if (e.key == KEY_ENTER && hover >= 0 && !items[hover].disabled) {
                result = items[hover].id;
                done = true;
            }
            break;
        case EV_POPUP_CLOSE: case EV_BLUR: case EV_CLOSE:
            done = true;
            break;
        }
    }
    win_destroy(win);
    repost(&def);
    return result;
}

static rect_t center_over(window_t *parent, int w, int h) {
    rect_t wa = wm_workarea();
    int x, y;
    if (parent) {
        rect_t p = win_client_rect(parent);
        x = p.x + (p.w - w) / 2;
        y = p.y + (p.h - h) / 2 - TITLE_H / 2;
    } else {
        x = wa.x + (wa.w - w) / 2;
        y = wa.y + (wa.h - h) / 2;
    }
    x = imax(wa.x, imin(x, wa.x + wa.w - w));
    y = imax(wa.y, imin(y, wa.y + wa.h - h - TITLE_H));
    return R(x, y, w, h);
}

int ui_message(window_t *parent, const char *title, const char *msg,
               const char *b1, const char *b2, const char *b3) {
    const char *labels[3] = { b1, b2, b3 };
    int nb = b3 ? 3 : b2 ? 2 : 1;
    int w = 440;
    int text_h = ui_wrap(NULL, R(24, 0, w - 48, 0), msg, 0, 0, false);
    int h = 24 + 30 + text_h + 24 + 72;
    rect_t pos = center_over(parent, w, h);
    window_t *win = win_create(title, ICON_INFO, pos.x, pos.y, w, h, WF_DECORATED | WF_NOTASKBAR);
    if (!win) return 0;
    int hover = -1, press = -1, result = 0;
    bool done = false;
    deferred_t def = { .n = 0 };
    rect_t btn[3];
    for (int i = 0; i < nb; i++) btn[i] = R(w - 24 - (nb - i) * 128 + 8, h - 52, 120, 32);

    while (!done) {
        canvas_t *c = win_canvas(win);
        gfx_clear(c, theme.surface);
        font_draw(c, 24, 20, title, theme.text, FONT_BOLD);
        ui_wrap(c, R(24, 54, w - 48, text_h), msg, theme.text, 0, true);
        gfx_fill(c, 0, h - 72, w, 72, theme.window_bg);
        gfx_hline(c, 0, h - 72, w, theme.border);
        for (int i = 0; i < nb; i++)
            ui_button(c, btn[i], labels[i], i == 0 ? BTN_ACCENT : BTN_STANDARD,
                      press == i && hover == i ? UI_PRESSED : hover == i ? UI_HOVER : UI_NORMAL);
        win_invalidate(win);

        event_t e;
        gui_wait(&e);
        if (e.win != win) {
            if (e.type == EV_MOUSE_DOWN) win_focus(win);
            else defer(&def, &e);
            continue;
        }
        int at = -1;
        for (int i = 0; i < nb; i++) if (ui_hit(btn[i], &e)) at = i;
        switch (e.type) {
        case EV_MOUSE_MOVE: hover = at; break;
        case EV_MOUSE_DOWN: press = at; break;
        case EV_MOUSE_UP:
            if (at >= 0 && at == press) { result = at + 1; done = true; }
            press = -1;
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_ENTER) { result = 1; done = true; }
            else if (e.key == KEY_ESC) { result = 0; done = true; }
            break;
        case EV_CLOSE: done = true; break;
        }
    }
    win_destroy(win);
    if (parent) win_focus(parent);
    repost(&def);
    return result;
}

bool ui_input(window_t *parent, const char *title, const char *prompt, char *buf, int size) {
    return ui_input_ex(parent, title, prompt, buf, size, false);
}

/* campo com bolinhas no lugar das letras */
static void edit_draw_masked(canvas_t *c, rect_t r, ui_edit_t *e) {
    gfx_round_fill(c, r.x, r.y, r.w, r.h, 4, theme.dark ? HEX(0x2F2F2F) : HEX(0xFFFFFF));
    gfx_round_rect(c, r.x, r.y, r.w, r.h, 4, theme.control_border);
    gfx_fill(c, r.x + 2, r.y + r.h - 2, r.w - 4, 2, theme.accent);
    int n = (int)utf8_count(e->text, e->len), cur = (int)utf8_count(e->text, e->cur);
    for (int i = 0; i < n && 12 + i * 12 < r.w - 12; i++) gfx_circle(c, r.x + 14 + i * 12, r.y + r.h / 2, 3.5f, theme.text);
    gfx_vline(c, r.x + 9 + cur * 12, r.y + (r.h - 16) / 2, 16, theme.text);
}

bool ui_input_ex(window_t *parent, const char *title, const char *prompt, char *buf, int size, bool masked) {
    int w = 420, h = 176;
    rect_t pos = center_over(parent, w, h);
    window_t *win = win_create(title, ICON_INFO, pos.x, pos.y, w, h, WF_DECORATED | WF_NOTASKBAR);
    if (!win) return false;
    ui_edit_t ed;
    ui_edit_set(&ed, buf);
    rect_t edit_r = R(24, 48, w - 48, 32);
    rect_t ok = R(w - 24 - 248, h - 52, 120, 32), cancel = R(w - 24 - 120, h - 52, 120, 32);
    int hover = 0, press = 0;
    bool done = false, result = false;
    deferred_t def = { .n = 0 };

    while (!done) {
        canvas_t *c = win_canvas(win);
        gfx_clear(c, theme.surface);
        font_draw(c, 24, 20, prompt, theme.text, 0);
        if (masked) edit_draw_masked(c, edit_r, &ed);
        else ui_edit_draw(c, edit_r, &ed, true, NULL);
        gfx_fill(c, 0, h - 72, w, 72, theme.window_bg);
        gfx_hline(c, 0, h - 72, w, theme.border);
        ui_button(c, ok, "OK", BTN_ACCENT, hover == 1 ? (press == 1 ? UI_PRESSED : UI_HOVER) : UI_NORMAL);
        ui_button(c, cancel, "Cancelar", BTN_STANDARD, hover == 2 ? (press == 2 ? UI_PRESSED : UI_HOVER) : UI_NORMAL);
        win_invalidate(win);

        event_t e;
        gui_wait(&e);
        if (e.win != win) {
            if (e.type == EV_MOUSE_DOWN) win_focus(win);
            else defer(&def, &e);
            continue;
        }
        int at = ui_hit(ok, &e) ? 1 : ui_hit(cancel, &e) ? 2 : 0;
        switch (e.type) {
        case EV_MOUSE_MOVE:
            hover = at;
            win_set_cursor(win, ui_hit(edit_r, &e) ? CURSOR_IBEAM : CURSOR_ARROW);
            break;
        case EV_MOUSE_DOWN:
            press = at;
            if (ui_hit(edit_r, &e)) ui_edit_click(&ed, edit_r, e.x);
            break;
        case EV_MOUSE_UP:
            if (at && at == press) { result = at == 1; done = true; }
            press = 0;
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_ENTER) { result = true; done = true; }
            else if (e.key == KEY_ESC) done = true;
            else ui_edit_key(&ed, &e);
            break;
        case EV_CLOSE: done = true; break;
        }
    }
    if (result) strlcpy(buf, ed.text, size);
    win_destroy(win);
    if (parent) win_focus(parent);
    repost(&def);
    return result;
}

/* ------------------------------------------------------------------ */
/* Diálogo Abrir / Salvar                                               */
/* ------------------------------------------------------------------ */
bool ui_file_dialog(window_t *parent, bool save, const char *title, char *path, int size) {
    int w = 600, h = 440;
    rect_t pos = center_over(parent, w, h);
    window_t *win = win_create(title, save ? ICON_FOLDER : ICON_FOLDER, pos.x, pos.y, w, h,
                               WF_DECORATED | WF_NOTASKBAR);
    if (!win) return false;

    /* diretório inicial = diretório do caminho atual, ou Documentos */
    char dirpath[VFS_PATH_MAX];
    strlcpy(dirpath, path, sizeof(dirpath));
    char *slash = strrchr(dirpath, '/');
    const char *initial_name = "";
    if (slash) {
        initial_name = path + (slash - dirpath) + 1;
        *slash = 0;
        if (!dirpath[0]) strlcpy(dirpath, "/", sizeof(dirpath));
    }
    vnode_t *dir = vfs_lookup(NULL, dirpath);
    if (!dir || dir->type != VFS_DIR) dir = vfs_lookup(NULL, "/home/usuario/Documentos");

    ui_edit_t name;
    ui_edit_set(&name, initial_name);
    ui_list_t list = { .row_h = 28, .sel = -1 };
    rect_t up_r = R(16, 12, 32, 32);
    rect_t list_r = R(16, 56, w - 32, h - 56 - 128);
    rect_t name_r = R(80, h - 112, w - 96, 32);
    rect_t ok = R(w - 16 - 248, h - 52, 120, 32), cancel = R(w - 16 - 120, h - 52, 120, 32);
    int hover = 0, press = 0, hover_row = -1;
    bool done = false, result = false;
    deferred_t def = { .n = 0 };

    while (!done) {
        list.count = vfs_child_count(dir);
        ui_list_clamp(&list, list_r);

        canvas_t *c = win_canvas(win);
        gfx_clear(c, theme.window_bg);
        ui_glyph_button(c, up_r, GLYPH_UP, hover == 3 ? UI_HOVER : UI_NORMAL);
        char p[VFS_PATH_MAX];
        vfs_path(dir, p, sizeof(p));
        gfx_round_fill(c, 56, 12, w - 72, 32, 4, theme.surface);
        gfx_round_rect(c, 56, 12, w - 72, 32, 4, theme.control_border);
        icon_draw(c, ICON_FOLDER, 64, 20, 16);
        font_draw_ellipsis(c, 88, 20, w - 72 - 40, p, theme.text, 0);

        gfx_round_fill(c, list_r.x, list_r.y, list_r.w, list_r.h, 4, theme.surface);
        gfx_round_rect(c, list_r.x, list_r.y, list_r.w, list_r.h, 4, theme.border);
        rect_t saved = c->clip;
        canvas_clip(c, rect_inset(list_r, 1));
        for (int i = list.scroll / list.row_h; i < list.count; i++) {
            int y = list_r.y + i * list.row_h - list.scroll;
            if (y > list_r.y + list_r.h) break;
            vnode_t *n = vfs_child_at(dir, i);
            if (!n) break;
            if (i == list.sel) gfx_fill(c, list_r.x + 2, y, list_r.w - 4, list.row_h, theme.selection);
            else if (i == hover_row) gfx_fill(c, list_r.x + 2, y, list_r.w - 4, list.row_h, theme.hover);
            icon_draw(c, n->type == VFS_DIR ? ICON_FOLDER : ICON_TEXT, list_r.x + 10, y + 6, 16);
            font_draw(c, list_r.x + 36, y + 6, n->name, theme.text, 0);
        }
        c->clip = saved;
        ui_scrollbar(c, list_r, list.count * list.row_h, list_r.h, list.scroll);

        font_draw(c, 16, h - 104, "Nome:", theme.text, 0);
        ui_edit_draw(c, name_r, &name, true, "nome do arquivo");
        ui_button(c, ok, save ? "Salvar" : "Abrir", BTN_ACCENT, hover == 1 ? (press == 1 ? UI_PRESSED : UI_HOVER) : UI_NORMAL);
        ui_button(c, cancel, "Cancelar", BTN_STANDARD, hover == 2 ? (press == 2 ? UI_PRESSED : UI_HOVER) : UI_NORMAL);
        win_invalidate(win);

        event_t e;
        gui_wait(&e);
        if (e.win != win) {
            if (e.type == EV_MOUSE_DOWN) win_focus(win);
            else defer(&def, &e);
            continue;
        }
        int at = ui_hit(ok, &e) ? 1 : ui_hit(cancel, &e) ? 2 : ui_hit(up_r, &e) ? 3 : 0;
        bool accept = false;
        switch (e.type) {
        case EV_MOUSE_MOVE:
            hover = at;
            hover_row = ui_list_hit(&list, list_r, e.y);
            if (!ui_hit(list_r, &e)) hover_row = -1;
            win_set_cursor(win, ui_hit(name_r, &e) ? CURSOR_IBEAM : CURSOR_ARROW);
            break;
        case EV_MOUSE_LEAVE:
            hover = 0;
            hover_row = -1;
            break;
        case EV_MOUSE_WHEEL:
            ui_list_wheel(&list, list_r, e.wheel);
            break;
        case EV_MOUSE_DOWN:
            press = at;
            if (ui_hit(name_r, &e)) ui_edit_click(&name, name_r, e.x);
            if (ui_hit(list_r, &e)) {
                int i = ui_list_hit(&list, list_r, e.y);
                list.sel = i;
                vnode_t *n = i >= 0 ? vfs_child_at(dir, i) : NULL;
                if (n && n->type == VFS_FILE) ui_edit_set(&name, n->name);
                if (n && n->type == VFS_DIR && e.clicks >= 2) {
                    dir = n;
                    list.sel = -1;
                    list.scroll = 0;
                } else if (n && n->type == VFS_FILE && e.clicks >= 2) {
                    accept = true;
                }
            }
            break;
        case EV_MOUSE_UP:
            if (at && at == press) {
                if (at == 1) accept = true;
                else if (at == 2) done = true;
                else if (at == 3 && dir->parent) { dir = dir->parent; list.sel = -1; list.scroll = 0; }
            }
            press = 0;
            break;
        case EV_KEY_DOWN:
            if (e.key == KEY_ENTER) accept = true;
            else if (e.key == KEY_ESC) done = true;
            else if (e.key == KEY_UP || e.key == KEY_DOWN) {
                ui_list_key(&list, list_r, &e);
                vnode_t *n = vfs_child_at(dir, list.sel);
                if (n && n->type == VFS_FILE) ui_edit_set(&name, n->name);
            } else ui_edit_key(&name, &e);
            break;
        case EV_CLOSE:
            done = true;
            break;
        }

        if (accept && name.len) {
            vnode_t *sel = vfs_lookup(dir, name.text);
            if (sel && sel->type == VFS_DIR) {
                dir = sel;
                ui_edit_set(&name, "");
                list.sel = -1;
                list.scroll = 0;
            } else if (!save && !sel) {
                ui_message(win, "Abrir", "O arquivo não foi encontrado.", "OK", NULL, NULL);
            } else {
                char d[VFS_PATH_MAX];
                vfs_path(dir, d, sizeof(d));
                snprintf(path, size, "%s/%s", strcmp(d, "/") ? d : "", name.text);
                if (save && sel) {
                    char q[320];
                    snprintf(q, sizeof(q), "\"%s\" já existe. Deseja substituí-lo?", name.text);
                    if (ui_message(win, "Confirmar", q, "Sim", "Não", NULL) != 1) continue;
                }
                result = true;
                done = true;
            }
        }
    }
    win_destroy(win);
    if (parent) win_focus(parent);
    repost(&def);
    return result;
}
