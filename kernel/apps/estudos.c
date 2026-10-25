/*
 * Estudos: o guia para estudar o livro OSTEP ("Operating Systems: Three Easy
 * Pieces") com o Capivara. Lê os arquivos Markdown de Documentos/OSTEP e os
 * mostra com um pouco de formatação. Um clique duplo numa linha de comando
 * abre o Terminal já rodando ela (os laboratórios de apps/ostep.c).
 *
 * Só entende o pedaço do Markdown que o guia usa: títulos "#", "##" e "###",
 * listas com "-" ou "1.", blocos de código com ``` (os ```sh são comandos) e
 * um parágrafo que começa com um rótulo em negrito ("**No livro.** ..."). Os
 * marcadores * e ` no meio do texto são tirados.
 *
 * O texto vira uma lista de blocos (parse) e os blocos viram linhas da tela,
 * já quebradas na largura da janela (layout). Desenhar é só percorrer as
 * linhas visíveis.
 */
#include "apps/apps.h"
#include "gui/ui.h"
#include "fs/vfs.h"
#include "dev/keyboard.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

#define DOC_DIR   "/home/usuario/Documentos/OSTEP"
#define SIDE_W    250
#define PAD       28
#define ITEM_H    36
#define LIST_Y    64
#define MAXDOCS   32

enum { B_H1, B_H2, B_H3, B_LABEL, B_PARA, B_ITEM, B_CODE };

typedef struct {
    uint8_t kind;
    bool run;                   /* B_CODE: linha de um bloco ```sh, dá para rodar */
    char marker[8];             /* B_ITEM: "•" ou "1." */
    char *text;
} block_t;

typedef struct {
    int block, start, len;      /* pedaço do texto do bloco nesta linha */
    int x, y, h, flags;
    uint32_t color;
    bool first;                 /* primeira linha do bloco: desenha o marcador */
} row_t;

typedef struct {
    window_t *win;
    bool quit;
    int ndocs, cur;
    char names[MAXDOCS][VFS_NAME_MAX];
    char titles[MAXDOCS][96];
    block_t *blocks;
    int nblocks, cap_blocks;
    row_t *rows;
    int nrows, cap_rows;
    int content_h, scroll, hover;
} est_t;

/* ------------------------------------------------------------------ texto */

/* Tira os marcadores de ênfase (* e `) e troca [texto](endereço) por texto. */
static char *clean(const char *s, int n) {
    char *out = kmalloc((size_t)n + 1);
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '*' || s[i] == '`') continue;
        if (s[i] == ']' && i + 1 < n && s[i + 1] == '(') {
            while (i < n && s[i] != ')') i++;
            continue;
        }
        if (s[i] == '[') continue;
        out[k++] = s[i];
    }
    out[k] = 0;
    return out;
}

static void add_block(est_t *s, int kind, bool run, const char *marker, char *text) {
    if (s->nblocks == s->cap_blocks) {
        s->cap_blocks = s->cap_blocks ? s->cap_blocks * 2 : 64;
        s->blocks = krealloc(s->blocks, (size_t)s->cap_blocks * sizeof(block_t));
    }
    block_t *b = &s->blocks[s->nblocks++];
    b->kind = (uint8_t)kind;
    b->run = run;
    strlcpy(b->marker, marker ? marker : "", sizeof(b->marker));
    b->text = text;
}

static void free_doc(est_t *s) {
    for (int i = 0; i < s->nblocks; i++) kfree(s->blocks[i].text);
    s->nblocks = 0;
    s->nrows = 0;
}

/* Fecha o parágrafo (ou item de lista) que está sendo juntado. Um parágrafo
 * que começa com "**Rótulo.**" vira duas coisas: o rótulo e o resto. */
static void flush(est_t *s, char *para, int *plen, int *pkind, const char *marker) {
    if (!*plen) { *pkind = -1; return; }
    para[*plen] = 0;
    if (*pkind == B_PARA && !strncmp(para, "**", 2)) {
        char *end = strstr(para + 2, "**");
        if (end) {
            add_block(s, B_LABEL, false, NULL, clean(para + 2, (int)(end - para - 2)));
            char *rest = end + 2;
            while (*rest == ' ') rest++;
            if (*rest) add_block(s, B_PARA, false, NULL, clean(rest, (int)strlen(rest)));
            *plen = 0;
            *pkind = -1;
            return;
        }
    }
    add_block(s, *pkind, false, marker, clean(para, *plen));
    *plen = 0;
    *pkind = -1;
}

#define PARA_MAX 4096

static void parse(est_t *s, char *src) {
    char *para = kmalloc(PARA_MAX);
    char marker[8] = "";
    int plen = 0, pkind = -1;
    bool code = false, run = false;
    for (char *line = src; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        int n = (int)strlen(line);
        if (n && line[n - 1] == '\r') line[--n] = 0;
        char *next = nl ? nl + 1 : NULL;

        if (code) {
            if (!strncmp(line, "```", 3)) code = false;
            else add_block(s, B_CODE, run, NULL, kstrdup(line));
        } else if (!strncmp(line, "```", 3)) {
            flush(s, para, &plen, &pkind, marker);
            code = true;
            run = !line[3] || !strcmp(line + 3, "sh");
        } else {
            char *t = line;
            while (*t == ' ') t++;
            int digits = 0;
            while (isdigit(t[digits])) digits++;
            if (!*t) {
                flush(s, para, &plen, &pkind, marker);
            } else if (t == line && line[0] == '#') {
                flush(s, para, &plen, &pkind, marker);
                int lvl = 0;
                while (line[lvl] == '#') lvl++;
                const char *h = line + lvl;
                while (*h == ' ') h++;
                add_block(s, lvl == 1 ? B_H1 : lvl == 2 ? B_H2 : B_H3, false, NULL, clean(h, (int)strlen(h)));
            } else if (t == line && (!strncmp(t, "- ", 2) || (digits && t[digits] == '.' && t[digits + 1] == ' '))) {
                flush(s, para, &plen, &pkind, marker);
                pkind = B_ITEM;
                if (t[0] == '-') {
                    strlcpy(marker, "•", sizeof(marker));
                    t += 2;
                } else {
                    int k = digits < 5 ? digits : 5;          /* "12." */
                    memcpy(marker, t, (size_t)k);
                    marker[k] = '.';
                    marker[k + 1] = 0;
                    t += digits + 2;
                }
                plen = (int)strlcpy(para, t, PARA_MAX);
                if (plen >= PARA_MAX) plen = PARA_MAX - 1;
            } else {
                /* texto corrido: linhas seguidas se juntam com um espaço */
                if (pkind < 0) pkind = B_PARA;
                if (plen && plen < PARA_MAX - 1) para[plen++] = ' ';
                int k = (int)strlen(t);
                if (plen + k >= PARA_MAX) k = PARA_MAX - 1 - plen;
                memcpy(para + plen, t, (size_t)k);
                plen += k;
            }
        }
        line = next;
    }
    flush(s, para, &plen, &pkind, marker);
    kfree(para);
}

/* ------------------------------------------------------------------ layout */
static void add_row(est_t *s, row_t r) {
    if (s->nrows == s->cap_rows) {
        s->cap_rows = s->cap_rows ? s->cap_rows * 2 : 256;
        s->rows = krealloc(s->rows, (size_t)s->cap_rows * sizeof(row_t));
    }
    s->rows[s->nrows++] = r;
}

/* Quebra o texto em linhas que cabem em w pixels, sempre num espaço. */
static void wrap(est_t *s, int bi, int x, int *y, int w, int lh, int flags, uint32_t color) {
    const char *t = s->blocks[bi].text;
    int len = (int)strlen(t), start = 0;
    bool first = true;
    do {
        int n = font_fit(t + start, w, flags);
        if (start + n < len) {
            int k = n;
            while (k > 0 && t[start + k] != ' ') k--;
            if (k > 0) n = k;
        }
        if (n <= 0 && start < len) n = len - start;    /* palavra maior que a linha */
        add_row(s, (row_t){ bi, start, n, x, *y, lh, flags, color, first });
        *y += lh;
        first = false;
        start += n;
        while (t[start] == ' ') start++;
    } while (start < len);
}

static rect_t text_rect(est_t *s) {
    canvas_t *c = win_canvas(s->win);
    return R(SIDE_W + 1, 0, c->w - SIDE_W - 1, c->h);
}

static void layout(est_t *s) {
    rect_t tr = text_rect(s);
    int w = tr.w - 2 * PAD - 12, y = 22;
    s->nrows = 0;
    for (int i = 0; i < s->nblocks; i++) {
        block_t *b = &s->blocks[i];
        int prev = i ? s->blocks[i - 1].kind : -1;
        switch (b->kind) {
        case B_H1:
            y += i ? 24 : 0;
            wrap(s, i, PAD, &y, w, 38, FONT_2X | FONT_BOLD, theme.text);
            y += 4;
            break;
        case B_H2:
            y += 22;
            wrap(s, i, PAD, &y, w, 24, FONT_BOLD, theme.accent);
            y += 2;
            break;
        case B_H3:
            y += 14;
            wrap(s, i, PAD, &y, w, 22, FONT_BOLD, theme.text);
            break;
        case B_LABEL:
            y += 12;
            wrap(s, i, PAD, &y, w, 22, FONT_BOLD, theme.text);
            break;
        case B_PARA:
            y += prev == B_LABEL ? 2 : 8;
            wrap(s, i, PAD, &y, w, 20, 0, theme.text);
            break;
        case B_ITEM:
            y += prev == B_ITEM ? 3 : 8;
            wrap(s, i, PAD + 22, &y, w - 22, 20, 0, theme.text);
            break;
        case B_CODE:
            y += prev == B_CODE ? 0 : 8;
            wrap(s, i, PAD + 14, &y, w - 20, 22, FONT_MONO, b->run ? theme.text : theme.text_dim);
            break;
        }
    }
    s->content_h = y + 28;
}

static void clamp_scroll(est_t *s) {
    int max = s->content_h - text_rect(s).h;
    if (s->scroll > max) s->scroll = max;
    if (s->scroll < 0) s->scroll = 0;
}

/* ------------------------------------------------------------------ arquivos */
static void list_docs(est_t *s) {
    s->ndocs = 0;
    vnode_t *dir = vfs_lookup(NULL, DOC_DIR);
    if (!dir) return;
    int n = vfs_child_count(dir);
    for (int i = 0; i < n && s->ndocs < MAXDOCS; i++) {
        vnode_t *f = vfs_child_at(dir, i);
        const char *dot = strrchr(f->name, '.');
        if (f->type != VFS_FILE || !dot || strcmp(dot, ".md")) continue;
        int k = s->ndocs++;
        strlcpy(s->names[k], f->name, sizeof(s->names[k]));
        /* o título é a primeira linha "# ..." do arquivo */
        char head[160] = "";
        long got = vfs_read(f, 0, head, sizeof(head) - 1);
        head[got > 0 ? got : 0] = 0;
        char *nl = strchr(head, '\n');
        if (nl) *nl = 0;
        const char *t = head[0] == '#' ? head + 2 : f->name;
        strlcpy(s->titles[k], t, sizeof(s->titles[k]));
    }
}

static void open_doc(est_t *s, int k) {
    free_doc(s);
    s->cur = k;
    s->scroll = 0;
    s->hover = -1;
    char path[VFS_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", DOC_DIR, s->names[k]);
    vnode_t *f = vfs_lookup(NULL, path);
    if (!f) return;
    char *buf = kmalloc(f->size + 1);
    long got = vfs_read(f, 0, buf, f->size);
    buf[got > 0 ? got : 0] = 0;
    parse(s, buf);
    kfree(buf);
    layout(s);
}

/* ------------------------------------------------------------------ desenho */
static void draw(est_t *s) {
    canvas_t *c = win_canvas(s->win);
    rect_t tr = text_rect(s);
    gfx_clear(c, theme.surface);

    /* barra lateral: os capítulos */
    gfx_fill(c, 0, 0, SIDE_W, c->h, theme.surface_alt);
    gfx_fill(c, SIDE_W, 0, 1, c->h, theme.border);
    font_draw(c, 18, 16, "OSTEP", theme.text, FONT_BOLD);
    font_draw(c, 18, 36, "Three Easy Pieces", theme.text_dim, 0);
    for (int i = 0; i < s->ndocs; i++) {
        rect_t r = R(8, LIST_Y + i * ITEM_H, SIDE_W - 16, ITEM_H - 4);
        bool sel = i == s->cur;
        if (sel) gfx_round_fill(c, r.x, r.y, r.w, r.h, 7, theme.selection);
        char num[4] = { s->names[i][0], s->names[i][1], 0, 0 };
        font_draw(c, r.x + 10, r.y + 8, num, sel ? theme.selection_text : theme.text_dim, FONT_MONO);
        font_draw_ellipsis(c, r.x + 34, r.y + 8, r.w - 42, s->titles[i], sel ? theme.selection_text : theme.text, 0);
    }
    if (!s->ndocs) font_draw(c, 18, LIST_Y, "Nenhum arquivo em Documentos/OSTEP", theme.text_dim, 0);

    /* o texto */
    rect_t old = c->clip;
    c->clip = rect_intersect(old, tr);
    for (int i = 0; i < s->nrows; i++) {
        row_t *r = &s->rows[i];
        int y = tr.y + r->y - s->scroll;
        if (y + r->h < tr.y || y > tr.y + tr.h) continue;
        block_t *b = &s->blocks[r->block];
        if (b->kind == B_CODE) {
            int bx = tr.x + PAD, bw = tr.w - 2 * PAD - 12;
            gfx_fill(c, bx, y, bw, r->h, i == s->hover && b->run ? theme.hover : theme.surface_alt);
            if (b->run) gfx_fill(c, bx, y, 3, r->h, theme.accent);
            if (i == s->hover && b->run) {
                const char *hint = "clique duplo para rodar";
                font_draw(c, bx + bw - font_width(hint, 0) - 10, y + 3, hint, theme.text_dim, 0);
            }
        }
        if (b->kind == B_ITEM && r->first)
            font_draw(c, tr.x + r->x - 20, y + 2, b->marker, theme.accent, b->marker[0] == '\xe2' ? FONT_BOLD : 0);
        int dy = b->kind == B_CODE ? 3 : 2;
        font_draw_n(c, tr.x + r->x, y + dy, b->text + r->start, r->len, r->color, r->flags);
    }
    c->clip = old;

    int vis = tr.h;
    if (s->content_h > vis) ui_scrollbar(c, R(c->w - 10, 6, 6, c->h - 12), s->content_h, vis, s->scroll);
    win_invalidate(s->win);
}

/* ------------------------------------------------------------------ eventos */
static int row_at(est_t *s, int x, int y) {
    rect_t tr = text_rect(s);
    if (x < tr.x) return -1;
    int cy = y - tr.y + s->scroll;
    for (int i = 0; i < s->nrows; i++)
        if (cy >= s->rows[i].y && cy < s->rows[i].y + s->rows[i].h) return i;
    return -1;
}

static void run_command(est_t *s, int row) {
    block_t *b = &s->blocks[s->rows[row].block];
    if (b->kind != B_CODE || !b->run || !b->text[0]) return;
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "cmd:%s", b->text);
    app_launch("terminal", cmd);
}

static void scroll_by(est_t *s, int dy) {
    s->scroll += dy;
    clamp_scroll(s);
}

void estudos_main(void *arg) {
    if (arg) kfree(arg);
    est_t *s = kcalloc(1, sizeof(est_t));
    s->win = win_create("Estudos", ICON_BOOK, WIN_AUTO, WIN_AUTO, 920, 600, WF_APP);
    win_set_min_size(s->win, 640, 360);
    list_docs(s);
    s->hover = -1;
    if (s->ndocs) open_doc(s, 0);
    draw(s);
    while (!s->quit) {
        event_t e;
        gui_wait(&e);
        rect_t tr = text_rect(s);
        switch (e.type) {
        case EV_CLOSE:
            s->quit = true;
            continue;
        case EV_RESIZE:
        case EV_THEME:
            layout(s);
            clamp_scroll(s);
            break;
        case EV_MOUSE_MOVE: {
            int h = row_at(s, e.x, e.y);
            if (h >= 0 && !(s->blocks[s->rows[h].block].kind == B_CODE && s->blocks[s->rows[h].block].run)) h = -1;
            if (h == s->hover) continue;
            s->hover = h;
            break;
        }
        case EV_MOUSE_LEAVE:
            if (s->hover < 0) continue;
            s->hover = -1;
            break;
        case EV_MOUSE_DOWN:
            if (e.button != 1) continue;
            if (e.x < SIDE_W) {
                int k = (e.y - LIST_Y) / ITEM_H;
                if (e.y < LIST_Y || k >= s->ndocs || k == s->cur) continue;
                open_doc(s, k);
            } else if (e.clicks == 2) {
                int r = row_at(s, e.x, e.y);
                if (r >= 0) run_command(s, r);
                continue;
            } else {
                continue;
            }
            break;
        case EV_MOUSE_WHEEL:
            scroll_by(s, -e.wheel * 60);
            break;
        case EV_KEY_DOWN:
            switch (e.key) {
            case KEY_UP: scroll_by(s, -40); break;
            case KEY_DOWN: scroll_by(s, 40); break;
            case KEY_PGUP: scroll_by(s, -(tr.h - 60)); break;
            case KEY_PGDN: case KEY_CHAR:
                if (e.key == KEY_CHAR && e.ch != ' ') continue;
                scroll_by(s, tr.h - 60);
                break;
            case KEY_HOME: s->scroll = 0; break;
            case KEY_END: s->scroll = s->content_h; clamp_scroll(s); break;
            case KEY_LEFT: if (s->cur > 0) open_doc(s, s->cur - 1); else continue; break;
            case KEY_RIGHT: if (s->cur < s->ndocs - 1) open_doc(s, s->cur + 1); else continue; break;
            default: continue;
            }
            break;
        default:
            continue;
        }
        draw(s);
    }
    free_doc(s);
    kfree(s->blocks);
    kfree(s->rows);
    win_destroy(s->win);
    kfree(s);
}
