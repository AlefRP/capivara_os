/* Calculadora padrão (números em ponto flutuante, vírgula decimal). */
#include "apps/apps.h"
#include "gui/ui.h"
#include "dev/keyboard.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

#define W 340
#define H 520
#define DISPLAY_H 136

static inline int imax(int a, int b) { return a > b ? a : b; }

typedef struct {
    window_t *win;
    char entry[40];            /* número sendo digitado / exibido */
    char expr[96];             /* linha de cima: "12 + 3 =" */
    double acc, last_operand;
    char op, last_op;
    bool new_entry, error, quit;
    int hover, press;
} calc_t;

static const char *labels[24] = {
    "%", "CE", "C", "⌫",
    "1/x", "x²", "√x", "÷",
    "7", "8", "9", "×",
    "4", "5", "6", "−",
    "1", "2", "3", "+",
    "±", "0", ",", "=",
};

static rect_t key_rect(int i) {
    int cols = 4, gap = 3, kw = (W - 8 - gap * (cols - 1)) / cols, kh = 58;
    return R(4 + (i % cols) * (kw + gap), DISPLAY_H + (i / cols) * (kh + gap), kw, kh);
}

/* ---- conversão número <-> texto (sem libc) ---- */
static double parse(const char *s) {
    double v = 0, scale = 0;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    for (; *s; s++) {
        if (*s == ',') { scale = 1; continue; }
        if (*s == '.') continue;                 /* separador de milhar */
        if (!isdigit(*s)) break;
        if (scale) { scale /= 10; v += (*s - '0') * scale; }
        else v = v * 10 + (*s - '0');
    }
    return neg ? -v : v;
}

static void format(double v, char *out, size_t size) {
    if (v != v) { strlcpy(out, "Resultado indefinido", size); return; }
    bool neg = v < 0;
    if (neg) v = -v;
    if (v >= 1e15 || (v > 0 && v < 1e-9)) {
        int e = 0;
        while (v >= 10) { v /= 10; e++; }
        while (v < 1) { v *= 10; e--; }
        char m[32];
        format(v, m, sizeof(m));
        snprintf(out, size, "%s%se%s%d", neg ? "-" : "", m, e >= 0 ? "+" : "", e);
        return;
    }
    unsigned long long ip = (unsigned long long)v;
    int idig = 1;
    for (unsigned long long t = ip; t >= 10; t /= 10) idig++;
    int fdig = 15 - idig;
    if (fdig < 0) fdig = 0;
    unsigned long long scale = 1;
    for (int i = 0; i < fdig; i++) scale *= 10;
    unsigned long long frac = (unsigned long long)((v - (double)ip) * (double)scale + 0.5);
    if (frac >= scale) { ip++; frac -= scale; }

    char ibuf[32], tmp[32];
    int n = 0;
    snprintf(tmp, sizeof(tmp), "%llu", ip);
    int len = (int)strlen(tmp);
    for (int i = 0; i < len; i++) {
        if (i && (len - i) % 3 == 0) ibuf[n++] = '.';
        ibuf[n++] = tmp[i];
    }
    ibuf[n] = 0;
    char fbuf[32] = "";
    if (fdig && frac) {
        snprintf(fbuf, sizeof(fbuf), "%0*llu", fdig, frac);
        int k = (int)strlen(fbuf);
        while (k > 0 && fbuf[k - 1] == '0') fbuf[--k] = 0;
    }
    snprintf(out, size, "%s%s%s%s", neg && (ip || frac) ? "-" : "", ibuf, fbuf[0] ? "," : "", fbuf);
}

static const char *op_str(char op) {
    switch (op) { case '+': return "+"; case '-': return "−"; case '*': return "×"; case '/': return "÷"; }
    return "";
}

static double apply(calc_t *c, double a, char op, double b) {
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/':
        if (b == 0) { c->error = true; return 0; }
        return a / b;
    }
    return b;
}

static void set_entry(calc_t *c, double v) { format(v, c->entry, sizeof(c->entry)); }

static void reset(calc_t *c) {
    strlcpy(c->entry, "0", sizeof(c->entry));
    c->expr[0] = 0;
    c->acc = 0;
    c->op = c->last_op = 0;
    c->new_entry = true;
    c->error = false;
}

static void press_key(calc_t *c, const char *k) {
    if (c->error) {
        reset(c);
        if (!strcmp(k, "C") || !strcmp(k, "CE")) return;
    }
    char a[40], b[40];
    if (isdigit(k[0]) && !k[1]) {
        if (c->new_entry || !strcmp(c->entry, "0")) { strlcpy(c->entry, k, sizeof(c->entry)); c->new_entry = false; }
        else if (strlen(c->entry) < 20) {
            /* reformata com separadores de milhar */
            char raw[40];
            int n = 0;
            for (const char *p = c->entry; *p; p++) if (*p != '.') raw[n++] = *p;
            raw[n++] = k[0];
            raw[n] = 0;
            if (strchr(raw, ',')) strlcpy(c->entry, raw, sizeof(c->entry));
            else set_entry(c, parse(raw));
        }
        if (!strcmp(c->expr + (strlen(c->expr) ? strlen(c->expr) - 1 : 0), "=")) c->expr[0] = 0;
    } else if (!strcmp(k, ",")) {
        if (c->new_entry) { strlcpy(c->entry, "0", sizeof(c->entry)); c->new_entry = false; }
        if (!strchr(c->entry, ',')) strlcat(c->entry, ",", sizeof(c->entry));
    } else if (!strcmp(k, "C")) {
        reset(c);
    } else if (!strcmp(k, "CE")) {
        strlcpy(c->entry, "0", sizeof(c->entry));
        c->new_entry = true;
    } else if (!strcmp(k, "⌫")) {
        if (!c->new_entry) {
            int n = (int)strlen(c->entry);
            c->entry[--n] = 0;
            if (!n || !strcmp(c->entry, "-")) strlcpy(c->entry, "0", sizeof(c->entry));
            else if (!strchr(c->entry, ',')) set_entry(c, parse(c->entry));
        }
    } else if (!strcmp(k, "±")) {
        set_entry(c, -parse(c->entry));
    } else if (!strcmp(k, "%")) {
        set_entry(c, c->op ? c->acc * parse(c->entry) / 100 : parse(c->entry) / 100);
        c->new_entry = true;
    } else if (!strcmp(k, "1/x") || !strcmp(k, "x²") || !strcmp(k, "√x")) {
        double v = parse(c->entry);
        format(v, a, sizeof(a));
        if (k[0] == '1') {
            if (v == 0) { c->error = true; strlcpy(c->entry, "Não é possível dividir por zero", sizeof(c->entry)); return; }
            snprintf(c->expr, sizeof(c->expr), "1/(%s)", a);
            v = 1 / v;
        } else if (k[0] == 'x') {
            snprintf(c->expr, sizeof(c->expr), "sqr(%s)", a);
            v = v * v;
        } else {
            if (v < 0) { c->error = true; strlcpy(c->entry, "Entrada inválida", sizeof(c->entry)); return; }
            snprintf(c->expr, sizeof(c->expr), "√(%s)", a);
            v = __builtin_sqrt(v);
        }
        set_entry(c, v);
        c->new_entry = true;
    } else if (strchr("+-*/", k[0]) && !k[1]) {
        double v = parse(c->entry);
        if (c->op && !c->new_entry) c->acc = apply(c, c->acc, c->op, v);
        else if (!c->op || c->expr[0] == 0 || strchr(c->expr, '=')) c->acc = v;
        if (c->error) { strlcpy(c->entry, "Não é possível dividir por zero", sizeof(c->entry)); return; }
        c->op = k[0];
        format(c->acc, a, sizeof(a));
        snprintf(c->expr, sizeof(c->expr), "%s %s", a, op_str(c->op));
        set_entry(c, c->acc);
        c->new_entry = true;
    } else if (!strcmp(k, "=")) {
        double v = parse(c->entry);
        char op = c->op;
        if (!op) {                               /* "=" repetido: refaz a última conta */
            op = c->last_op;
            if (!op) return;
            c->acc = v;
            v = c->last_operand;
        }
        format(c->acc, a, sizeof(a));
        format(v, b, sizeof(b));
        double r = apply(c, c->acc, op, v);
        if (c->error) { strlcpy(c->entry, "Não é possível dividir por zero", sizeof(c->entry)); return; }
        snprintf(c->expr, sizeof(c->expr), "%s %s %s =", a, op_str(op), b);
        c->last_op = op;
        c->last_operand = v;
        c->op = 0;
        c->acc = r;
        set_entry(c, r);
        c->new_entry = true;
    }
}

static void draw(calc_t *c) {
    canvas_t *cv = win_canvas(c->win);
    gfx_clear(cv, theme.window_bg);
    font_draw(cv, 16, 10, "Padrão", theme.text, FONT_BOLD);
    int ew = font_width(c->expr, 0);
    font_draw(cv, W - 16 - ew, 48, c->expr, theme.text_dim, 0);
    int flags = FONT_2X | FONT_BOLD;
    if (font_width(c->entry, flags) > W - 24) flags = FONT_BOLD;
    int vw = font_width(c->entry, flags);
    font_draw(cv, W - 16 - vw, flags & FONT_2X ? 80 : 92, c->entry, theme.text, flags);

    for (int i = 0; i < 24; i++) {
        rect_t r = key_rect(i);
        bool digit = isdigit(labels[i][0]) || !strcmp(labels[i], ",") || !strcmp(labels[i], "±");
        bool eq = i == 23;
        uint32_t bg = eq ? theme.accent : digit ? theme.control : (theme.dark ? HEX(0x2A2A2A) : HEX(0xF6F6F6));
        if (i == c->hover) bg = eq ? theme.accent_hover : color_mix(bg, theme.dark ? HEX(0xFFFFFF) : HEX(0x000000), 12);
        if (i == c->press && i == c->hover) bg = eq ? theme.accent_pressed : color_mix(bg, theme.dark ? HEX(0xFFFFFF) : HEX(0x000000), 24);
        gfx_round_fill(cv, r.x, r.y, r.w, r.h, 5, bg);
        if (!eq) gfx_round_rect(cv, r.x, r.y, r.w, r.h, 5, theme.dark ? HEX(0x353535) : HEX(0xE5E5E5));
        ui_text_center(cv, r, labels[i], eq ? theme.accent_text : theme.text, digit ? FONT_BOLD : 0);
    }
    win_invalidate(c->win);
}

void calc_main(void *arg) {
    if (arg) kfree(arg);
    calc_t *c = kcalloc(1, sizeof(calc_t));
    c->hover = c->press = -1;
    reset(c);
    c->win = win_create("Calculadora", ICON_CALC, WIN_AUTO, WIN_AUTO, W, H, WF_DECORATED);
    draw(c);
    while (!c->quit) {
        event_t e;
        gui_wait(&e);
        int at = -1;
        for (int i = 0; i < 24; i++) if (ui_hit(key_rect(i), &e)) at = i;
        switch (e.type) {
        case EV_MOUSE_MOVE: if (at == c->hover) continue; c->hover = at; break;
        case EV_MOUSE_LEAVE: c->hover = -1; break;
        case EV_MOUSE_DOWN: c->press = at; break;
        case EV_MOUSE_UP:
            if (at >= 0 && at == c->press) {
                const char *k = labels[at];
                if (!strcmp(k, "÷")) k = "/";
                else if (!strcmp(k, "×")) k = "*";
                else if (!strcmp(k, "−")) k = "-";
                press_key(c, k);
            }
            c->press = -1;
            break;
        case EV_KEY_DOWN: {
            char k[4] = { 0 };
            if (e.key == KEY_ENTER) strlcpy(k, "=", 4);
            else if (e.key == KEY_ESC) strlcpy(k, "C", 4);
            else if (e.key == KEY_BACKSPACE) { press_key(c, "⌫"); break; }
            else if (e.key == KEY_DELETE) strlcpy(k, "CE", 4);
            else if (e.key == KEY_CHAR && e.ch < 128) {
                k[0] = (char)e.ch;
                if (k[0] == '.') k[0] = ',';
                if (k[0] == 'x' || k[0] == 'X') k[0] = '*';
            }
            if (k[0] && (isdigit(k[0]) || strchr("+-*/=,%", k[0]) || k[1])) press_key(c, k);
            break;
        }
        case EV_THEME: case EV_FOCUS: case EV_BLUR: break;
        case EV_CLOSE: c->quit = true; continue;
        default: continue;
        }
        draw(c);
    }
    win_destroy(c->win);
    kfree(c);
}
