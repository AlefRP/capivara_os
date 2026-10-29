/*
 * Terminal: emulador de texto + um interpretador de comandos simples
 * (no estilo dos shells Unix) que opera sobre o sistema de arquivos virtual.
 */
#include "apps/apps.h"
#include "version.h"
#include "gui/ui.h"
#include "fs/vfs.h"
#include "dev/rtc.h"
#include "dev/power.h"
#include "dev/keyboard.h"
#include "dev/audio.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"
#include "boot/bootinfo.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "lib/printf.h"
#include "apps/notify.h"
#include "apps/trash.h"
#include "apps/ostep.h"
#include "gui/wm.h"
#include "tatu/tatu.h"

#define MAX_COLS   160
#define MAX_LINES  600
#define PAD        8
#define CW         8
#define CH         16

enum { C_TEXT, C_DIM, C_GREEN, C_BLUE, C_YELLOW, C_RED, C_CYAN, C_MAGENTA, C_WHITE, C_COUNT };
static const uint32_t palette[C_COUNT] = {
    HEX(0xCCCCCC), HEX(0x767676), HEX(0x16C60C), HEX(0x3B78FF), HEX(0xF9F1A5),
    HEX(0xE74856), HEX(0x61D6D6), HEX(0xB4009E), HEX(0xF2F2F2),
};

typedef struct { uint32_t cp; uint8_t color; } cell_t;
typedef struct { cell_t c[MAX_COLS]; int len; } line_t;

typedef struct {
    window_t *win;
    line_t *lines;              /* buffer circular */
    int first, count;           /* linha mais antiga e total */
    int cols, rows;
    int view;                   /* linhas roladas para cima */
    char input[256];
    int in_len, in_cur;
    char history[32][256];
    int hist_n, hist_pos;
    vnode_t *cwd;
    tatu_shell_t *tatu;
    bool cursor_on;
    bool quit;
    rect_t cursor_rect;         /* onde o cursor foi desenhado (para piscar barato) */
} term_t;

static line_t *L(term_t *t, int i) { return &t->lines[(t->first + i) % MAX_LINES]; }

static void new_line(term_t *t) {
    if (t->count < MAX_LINES) t->count++;
    else t->first = (t->first + 1) % MAX_LINES;
    L(t, t->count - 1)->len = 0;
}

static void put(term_t *t, const char *s, int color) {
    if (!t->count) new_line(t);
    while (*s) {
        uint32_t cp = utf8_decode(&s);
        if (cp == '\n') { new_line(t); continue; }
        if (cp == '\t') { put(t, "    ", color); continue; }
        line_t *ln = L(t, t->count - 1);
        if (ln->len >= t->cols || ln->len >= MAX_COLS) { new_line(t); ln = L(t, t->count - 1); }
        ln->c[ln->len++] = (cell_t){ cp, (uint8_t)color };
    }
}

static void printc(term_t *t, int color, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    put(t, buf, color);
}

#define print(t, ...) printc(t, C_TEXT, __VA_ARGS__)

/* saída dos laboratórios do OSTEP (apps/ostep.c): as cores são as mesmas */
static void lab_write(void *ctx, int color, const char *s) { put(ctx, s, color); }

static void cwd_display(term_t *t, char *out, size_t size) {
    char p[VFS_PATH_MAX];
    vfs_path(t->cwd, p, sizeof(p));
    if (!strncmp(p, "/home/usuario", 13) && (p[13] == 0 || p[13] == '/')) snprintf(out, size, "~%s", p + 13);
    else strlcpy(out, p, size);
}

static void prompt(term_t *t) {
    if (t->tatu) { printc(t, C_YELLOW, "%s", tatu_shell_prompt(t->tatu)); return; }
    char p[VFS_PATH_MAX];
    cwd_display(t, p, sizeof(p));
    printc(t, C_GREEN, "usuario@capivara");
    printc(t, C_TEXT, ":");
    printc(t, C_BLUE, "%s", p);
    printc(t, C_TEXT, "$ ");
}

/* ------------------------------------------------------------------ */
/* Desenho                                                              */
/* ------------------------------------------------------------------ */
static void draw(term_t *t) {
    canvas_t *c = win_canvas(t->win);
    gfx_clear(c, HEX(0x0C0C0C));

    /* a linha do prompt é desenhada junto com o texto digitado */
    int prompt_line = t->count - 1;
    line_t *pl = L(t, prompt_line);
    int prompt_len = pl->len;

    /* quantas linhas visuais a entrada ocupa */
    int in_chars = (int)utf8_count(t->input, t->in_len);
    int total_last = prompt_len + in_chars + 1;
    int extra = (total_last - 1) / t->cols;
    int total_rows = t->count + extra;
    int top = total_rows - t->rows - t->view;
    if (top < 0) top = 0;

    char tmp[8];
    for (int r = 0; r < t->rows; r++) {
        int vis = top + r;
        int y = PAD + r * CH;
        if (vis < prompt_line) {
            line_t *ln = L(t, vis);
            for (int i = 0; i < ln->len; i++) {
                int n = utf8_encode(ln->c[i].cp, tmp);
                tmp[n] = 0;
                font_draw(c, PAD + i * CW, y, tmp, palette[ln->c[i].color], FONT_MONO);
            }
        }
    }
    /* última linha lógica: prompt + entrada (pode quebrar em várias linhas) */
    int base = prompt_line - top;
    int pos = 0;
    for (int i = 0; i < prompt_len; i++, pos++) {
        int row = base + pos / t->cols;
        if (row < 0 || row >= t->rows) continue;
        int n = utf8_encode(pl->c[i].cp, tmp);
        tmp[n] = 0;
        font_draw(c, PAD + (pos % t->cols) * CW, PAD + row * CH, tmp, palette[pl->c[i].color], FONT_MONO);
    }
    const char *s = t->input;
    int cursor_pos = prompt_len + (int)utf8_count(t->input, t->in_cur);
    while (s < t->input + t->in_len) {
        const char *start = s;
        utf8_decode(&s);
        int row = base + pos / t->cols;
        if (row >= 0 && row < t->rows)
            font_draw_n(c, PAD + (pos % t->cols) * CW, PAD + row * CH, start, (int)(s - start), palette[C_WHITE], FONT_MONO);
        pos++;
    }
    int crow = base + cursor_pos / t->cols;
    t->cursor_rect = R(PAD + (cursor_pos % t->cols) * CW, PAD + crow * CH, CW, CH);
    if (t->cursor_on && t->view == 0 && crow >= 0 && crow < t->rows)
        gfx_fill(c, t->cursor_rect.x, t->cursor_rect.y + 13, CW, 3, palette[C_WHITE]);
    if (t->view > 0)
        ui_scrollbar(c, R(0, 4, c->w - 2, c->h - 8), total_rows, t->rows, top);
    win_invalidate(t->win);
}

static void layout(term_t *t) {
    canvas_t *c = win_canvas(t->win);
    t->cols = (c->w - 2 * PAD) / CW;
    t->rows = (c->h - 2 * PAD) / CH;
    if (t->cols > MAX_COLS) t->cols = MAX_COLS;
    if (t->cols < 10) t->cols = 10;
    if (t->rows < 2) t->rows = 2;
}

/* ------------------------------------------------------------------ */
/* Comandos                                                             */
/* ------------------------------------------------------------------ */
static vnode_t *resolve(term_t *t, const char *path) {
    if (path[0] == '~') {
        char buf[VFS_PATH_MAX];
        snprintf(buf, sizeof(buf), "/home/usuario%s", path + 1);
        return vfs_lookup(NULL, buf);
    }
    return vfs_lookup(t->cwd, path);
}

static void abs_path(term_t *t, const char *path, char *out, size_t size) {
    if (path[0] == '/') strlcpy(out, path, size);
    else if (path[0] == '~') snprintf(out, size, "/home/usuario%s", path + 1);
    else {
        char cwd[VFS_PATH_MAX];
        vfs_path(t->cwd, cwd, sizeof(cwd));
        snprintf(out, size, "%s/%s", strcmp(cwd, "/") ? cwd : "", path);
    }
}

static void fmt_size(uint64_t b, char *out, size_t n) {
    if (b < 1024) snprintf(out, n, "%lu B", b);
    else if (b < 1024 * 1024) snprintf(out, n, "%lu,%lu KB", b / 1024, (b % 1024) * 10 / 1024);
    else snprintf(out, n, "%lu,%lu MB", b >> 20, ((b >> 10) % 1024) * 10 / 1024);
}

static void cmd_ls(term_t *t, int argc, char **argv) {
    bool longf = false;
    const char *path = NULL;
    bool all = false;                 /* -a: mostra também os itens ocultos (".nome") */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strchr(argv[i], 'l')) longf = true;
            if (strchr(argv[i], 'a')) all = true;
        } else path = argv[i];
    }
    vnode_t *d = path ? resolve(t, path) : t->cwd;
    if (!d) { printc(t, C_RED, "ls: '%s' não existe\n", path); return; }
    if (d->type == VFS_FILE) { print(t, "%s\n", d->name); return; }
    int n = vfs_child_count(d);
    if (!n) return;
    if (longf) {
        for (int i = 0; i < n; i++) {
            vnode_t *c = vfs_child_at(d, i);
            if (c->name[0] == '.' && !all) continue;
            datetime_t dt;
            time_to_datetime(c->mtime, &dt);
            char sz[24];
            fmt_size(c->size, sz, sizeof(sz));
            printc(t, C_DIM, "%s  %10s  %02d/%02d/%04d %02d:%02d  ", c->type == VFS_DIR ? "d" : "-",
                   c->type == VFS_DIR ? "-" : sz, dt.day, dt.month, dt.year, dt.hour, dt.minute);
            printc(t, c->type == VFS_DIR ? C_BLUE : C_TEXT, "%s\n", c->name);
        }
        return;
    }
    int col = 0;
    for (int i = 0; i < n; i++) {
        vnode_t *c = vfs_child_at(d, i);
        if (c->name[0] == '.' && !all) continue;
        int w = (int)utf8_count(c->name, strlen(c->name)) + 2;
        if (col && col + w > t->cols) { put(t, "\n", 0); col = 0; }
        printc(t, c->type == VFS_DIR ? C_BLUE : C_TEXT, "%s  ", c->name);
        col += w;
    }
    put(t, "\n", 0);
}

static void tree(term_t *t, vnode_t *d, char *prefix, int depth) {
    int n = vfs_child_count(d);
    for (int i = 0; i < n && depth < 8; i++) {
        vnode_t *c = vfs_child_at(d, i);
        bool last = i == n - 1;
        printc(t, C_DIM, "%s%s", prefix, last ? "└── " : "├── ");
        printc(t, c->type == VFS_DIR ? C_BLUE : C_TEXT, "%s\n", c->name);
        if (c->type == VFS_DIR) {
            size_t pl = strlen(prefix);
            strlcat(prefix, last ? "    " : "│   ", 256);
            tree(t, c, prefix, depth + 1);
            prefix[pl] = 0;
        }
    }
}

static void cmd_sysinfo(term_t *t) {
    /* uma capivara (com laranjinha) em ASCII */
    static const char *logo[] = {
        "                   ", "      (o)          ", "  ,_,.-'---------. ", " /  o             \\",
        "(__,               )", "   \\   ,------,   / ", "    \\_/        \\_/ ", "                   ",
        "                   ",
    };
    char info[9][96];
    uint64_t up = timer_ms() / 1000;
    heap_stats_t hs;
    heap_stats(&hs);
    task_info_t tl[64];
    snprintf(info[0], 96, "usuario@capivara");
    snprintf(info[1], 96, "----------------");
    snprintf(info[2], 96, "Sistema: Capivara OS " CAPIVARA_VERSION " (x86_64)");
    snprintf(info[3], 96, "Kernel: capivara (próprio, monolítico)");
    snprintf(info[4], 96, "Tempo ligado: %luh %lum %lus", up / 3600, (up / 60) % 60, up % 60);
    snprintf(info[5], 96, "CPU: %s", cpu_brand());
    snprintf(info[6], 96, "Memória: %lu MiB / %lu MiB", (pmm_total_bytes() - pmm_free_bytes()) >> 20, pmm_total_bytes() >> 20);
    snprintf(info[7], 96, "Tela: %ux%u", bootinfo.fb_width, bootinfo.fb_height);
    snprintf(info[8], 96, "Tarefas: %d   Boot: %s", task_snapshot(tl, 64), bootinfo.bootloader);
    for (int i = 0; i < 9; i++) {
        printc(t, C_YELLOW, "%-21s ", logo[i]);
        printc(t, i < 2 ? C_GREEN : C_TEXT, "%s\n", info[i]);
    }
    put(t, "\n", 0);
    for (int i = 1; i < C_COUNT; i++) printc(t, i, "███");
    put(t, "\n", 0);
}

/* Uma nota senoidal na placa de som. O envelope (sobe em 5 ms e desce até o
 * fim) evita o estalo que um som cortado de repente faz. */
static void play_tone(float freq, int ms) {
    static int16_t chunk[256 * 2];
    int total = AUDIO_RATE * ms / 1000, stuck = 0;
    float phase = 0, step = 2 * GFX_PI * freq / AUDIO_RATE;
    for (int done = 0; done < total && stuck < 200; ) {
        int n = total - done < 256 ? total - done : 256;
        for (int i = 0; i < n; i++) {
            int k = done + i;
            float env = k < 240 ? k / 240.0f : (float)(total - k) / total;
            chunk[2 * i] = chunk[2 * i + 1] = (int16_t)(gfx_sin(phase) * env * 9000);
            phase += step;
            if (phase > 2 * GFX_PI) phase -= 2 * GFX_PI;
        }
        /* fila cheia: espera a placa tocar um pouco (e desiste se ela não andar) */
        for (int off = 0; off < n && stuck < 200; ) {
            size_t w = audio_write(chunk + off * 2, (size_t)(n - off));
            off += (int)w;
            if (off < n) { task_sleep(5); stuck = w ? 0 : stuck + 1; }
        }
        done += n;
    }
}

static void cmd_som(term_t *t, int argc, char **argv) {
    if (!audio_present()) {
        printc(t, C_RED, "Nenhuma placa de som. O sistema conhece a AC'97 (no VirtualBox: Áudio > ICH AC97).\n");
        return;
    }
    if (argc > 2 && !strcmp(argv[1], "volume")) {
        audio_set_volume((int)strtol(argv[2], NULL, 10));
    } else if (argc > 1 && !strcmp(argv[1], "teste")) {
        static const float notas[] = { 523.25f, 659.25f, 783.99f, 1046.5f };   /* dó, mi, sol, dó */
        for (int i = 0; i < 4; i++) play_tone(notas[i], i == 3 ? 450 : 180);
    } else if (argc > 1) {
        print(t, "uso: som [teste | volume 0-100]\n");
        return;
    }
    print(t, "Placa: %s, %d Hz estéreo, volume %d%%\n", audio_name(), AUDIO_RATE, audio_volume());
}

static const char *state_name(task_state_t s) {
    switch (s) {
    case TASK_READY: return "pronta";
    case TASK_RUNNING: return "rodando";
    case TASK_SLEEPING: return "dormindo";
    case TASK_BLOCKED: return "esperando";
    default: return "morta";
    }
}

/* avaliador de expressões inteiras: + - * / % e parênteses */
static const char *ep;
static long expr(void);
static long factor(void) {
    while (*ep == ' ') ep++;
    if (*ep == '(') { ep++; long v = expr(); while (*ep == ' ') ep++; if (*ep == ')') ep++; return v; }
    if (*ep == '-') { ep++; return -factor(); }
    return strtol(ep, &ep, 0);
}
static long term(void) {
    long v = factor();
    for (;;) {
        while (*ep == ' ') ep++;
        if (*ep == '*') { ep++; v *= factor(); }
        else if (*ep == '/') { ep++; long d = factor(); v = d ? v / d : 0; }
        else if (*ep == '%') { ep++; long d = factor(); v = d ? v % d : 0; }
        else return v;
    }
}
static long expr(void) {
    long v = term();
    for (;;) {
        while (*ep == ' ') ep++;
        if (*ep == '+') { ep++; v += term(); }
        else if (*ep == '-') { ep++; v -= term(); }
        else return v;
    }
}

static void help(term_t *t) {
    static const char *cmds[][2] = {
        { "ajuda", "mostra esta lista" }, { "ls [-l] [caminho]", "lista arquivos" },
        { "cd [caminho]", "muda de pasta (~ = pasta pessoal)" }, { "pwd", "mostra a pasta atual" },
        { "cat <arquivo>", "mostra o conteúdo" }, { "echo <texto> [> arq]", "escreve texto (ou grava em arquivo)" },
        { "mkdir / touch", "cria pasta / arquivo" }, { "rm <caminho>", "apaga" },
        { "cp / mv <orig> <dest>", "copia / move" }, { "tree [caminho]", "árvore de pastas" },
        { "abrir <app|caminho>", "abre aplicativo ou arquivo" }, { "apps", "lista aplicativos" },
        { "ps / kill <id>", "tarefas / finaliza tarefa" }, { "mem", "uso de memória" },
        { "calc <expressão>", "calcula (ex.: calc (2+3)*4)" }, { "date / uptime", "data e tempo ligado" },
        { "sysinfo", "informações do sistema" }, { "dmesg", "log do kernel" },
        { "history", "comandos anteriores" }, { "clear", "limpa a tela" },
        { "teclado [abnt2|us]", "mostra/troca o layout do teclado" },
        { "tema [claro|escuro]", "mostra/troca o tema" },
        { "som [teste|volume N]", "placa de som: testa ou muda o volume" },
        { "ostep", "laboratórios do livro OSTEP (escalonar, heap...)" },
        { "prodcons <modo> ...", "produtor/consumidor: polling ou semaforo (OSTEP)" },
        { "tatu [arquivo|teste]", "banco SQL do Tatu DB (.help para ajuda)" },
        { "notificar <texto>", "mostra uma notificação" }, { "captura", "salva a tela em Imagens" },
        { "lixeira [esvaziar]", "lista ou esvazia a Lixeira" }, { "bloquear", "tela de bloqueio" },
        { "reiniciar / desligar", "energia" }, { "exit", "fecha o terminal" },
    };
    printc(t, C_YELLOW, "Comandos disponíveis:\n");
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        printc(t, C_CYAN, "  %-24s", cmds[i][0]);
        print(t, "%s\n", cmds[i][1]);
    }
}

static void run(term_t *t, char *line) {
    /* O SQL precisa chegar inteiro ao parser: aspas e > pertencem ao SQL,
     * portanto o shell do Terminal não deve separar nem redirecionar aqui. */
    if (t->tatu) {
        if (!tatu_shell_feed(t->tatu, line)) { tatu_shell_free(t->tatu); t->tatu = NULL; }
        return;
    }
    char *argv[16];
    int argc = 0;
    char *redirect = NULL;
    bool append = false;
    for (char *p = line; *p && argc < 16; ) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        if (*p == '>') {
            append = p[1] == '>';
            *p = 0;
            p += append ? 2 : 1;
            while (*p == ' ') p++;
            redirect = p;
            break;
        }
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = 0;
            continue;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '>') p++;
        if (*p == '>') continue;
    }
    if (!argc) return;
    char *cmd = argv[0];

    if (!strcmp(cmd, "tatu")) {
        if (argc > 1 && !strcmp(argv[1], "teste")) {
            tatu_selftest(lab_write, t);
        } else {
            char path[VFS_PATH_MAX];
            const char *file = NULL;
            if (argc > 1 && strcmp(argv[1], ":memory:")) { abs_path(t, argv[1], path, sizeof(path)); file = path; }
            tatu_error_t err;
            tatu_db_t *db = tatu_open(file, &err);
            if (!db) printc(t, C_RED, "%s\n", err.rendered);
            else {
                t->tatu = tatu_shell_new(db, lab_write, t);
                if (t->tatu) tatu_shell_banner(t->tatu);
                else { tatu_close(db); printc(t, C_RED, "Memória insuficiente.\n"); }
            }
        }
    }
    else if (!strcmp(cmd, "ajuda") || !strcmp(cmd, "help")) help(t);
    else if (!strcmp(cmd, "clear") || !strcmp(cmd, "cls") || !strcmp(cmd, "limpar")) { t->count = 0; t->first = 0; }
    else if (!strcmp(cmd, "ls") || !strcmp(cmd, "dir")) cmd_ls(t, argc, argv);
    else if (!strcmp(cmd, "pwd")) { char p[VFS_PATH_MAX]; vfs_path(t->cwd, p, sizeof(p)); print(t, "%s\n", p); }
    else if (!strcmp(cmd, "cd")) {
        vnode_t *d = argc > 1 ? resolve(t, argv[1]) : vfs_lookup(NULL, "/home/usuario");
        if (!d || d->type != VFS_DIR) printc(t, C_RED, "cd: '%s' não é uma pasta\n", argc > 1 ? argv[1] : "");
        else t->cwd = d;
    }
    else if (!strcmp(cmd, "cat") || !strcmp(cmd, "type")) {
        for (int i = 1; i < argc; i++) {
            vnode_t *f = resolve(t, argv[i]);
            if (!f || f->type != VFS_FILE) { printc(t, C_RED, "cat: '%s' não é um arquivo\n", argv[i]); continue; }
            char *buf = kmalloc(f->size + 1);
            long n = vfs_read(f, 0, buf, f->size);
            buf[n < 0 ? 0 : n] = 0;
            put(t, buf, C_TEXT);
            if (n > 0 && buf[n - 1] != '\n') put(t, "\n", 0);
            kfree(buf);
        }
    }
    else if (!strcmp(cmd, "echo")) {
        char text[512] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strlcat(text, " ", sizeof(text));
            strlcat(text, argv[i], sizeof(text));
        }
        strlcat(text, "\n", sizeof(text));
        if (redirect && *redirect) {
            char path[VFS_PATH_MAX];
            abs_path(t, redirect, path, sizeof(path));
            vnode_t *f = vfs_create(NULL, path, VFS_FILE);
            if (!f) { printc(t, C_RED, "echo: não foi possível gravar em '%s'\n", redirect); return; }
            if (!append) vfs_truncate(f, 0);
            vfs_write(f, f->size, text, strlen(text));
        } else put(t, text, C_TEXT);
    }
    else if (!strcmp(cmd, "mkdir") || !strcmp(cmd, "touch")) {
        for (int i = 1; i < argc; i++) {
            char path[VFS_PATH_MAX];
            abs_path(t, argv[i], path, sizeof(path));
            if (!vfs_create(NULL, path, cmd[0] == 'm' ? VFS_DIR : VFS_FILE))
                printc(t, C_RED, "%s: não foi possível criar '%s'\n", cmd, argv[i]);
        }
    }
    else if (!strcmp(cmd, "rm") || !strcmp(cmd, "del")) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-rf")) continue;
            vnode_t *n = resolve(t, argv[i]);
            if (!n || n == vfs_root()) { printc(t, C_RED, "rm: '%s' não existe\n", argv[i]); continue; }
            if (vfs_is_ancestor(n, t->cwd)) { printc(t, C_RED, "rm: não dá para apagar a pasta atual\n"); continue; }
            vfs_remove(n);
        }
    }
    else if ((!strcmp(cmd, "cp") || !strcmp(cmd, "mv")) && argc >= 3) {
        vnode_t *src = resolve(t, argv[1]);
        vnode_t *dst = resolve(t, argv[2]);
        if (!src) { printc(t, C_RED, "%s: '%s' não existe\n", cmd, argv[1]); return; }
        if (dst && dst->type == VFS_DIR) {
            bool ok = cmd[0] == 'c' ? vfs_copy(src, dst) != NULL : vfs_move(src, dst) == 0;
            if (!ok) printc(t, C_RED, "%s: falhou\n", cmd);
        } else if (!dst && cmd[0] == 'm' && !strchr(argv[2], '/')) {
            if (vfs_rename(src, argv[2])) printc(t, C_RED, "mv: falhou\n");
        } else if (!dst && cmd[0] == 'c' && src->type == VFS_FILE) {
            char path[VFS_PATH_MAX];
            abs_path(t, argv[2], path, sizeof(path));
            vnode_t *f = vfs_create(NULL, path, VFS_FILE);
            if (f) vfs_write(f, 0, src->data, src->size);
        } else printc(t, C_RED, "%s: destino inválido\n", cmd);
    }
    else if (!strcmp(cmd, "tree")) {
        vnode_t *d = argc > 1 ? resolve(t, argv[1]) : t->cwd;
        if (!d) return;
        printc(t, C_BLUE, "%s\n", d == vfs_root() ? "/" : d->name);
        char prefix[256] = "";
        tree(t, d, prefix, 0);
    }
    else if (!strcmp(cmd, "abrir") || !strcmp(cmd, "open") || !strcmp(cmd, "start")) {
        if (argc < 2) { print(t, "uso: abrir <aplicativo|caminho>\n"); return; }
        if (app_find(argv[1])) {
            char extra[VFS_PATH_MAX] = "";
            if (argc > 2) abs_path(t, argv[2], extra, sizeof(extra));
            app_launch(argv[1], argc > 2 ? extra : NULL);
        } else {
            char path[VFS_PATH_MAX];
            abs_path(t, argv[1], path, sizeof(path));
            if (vfs_lookup(NULL, path)) app_open_path(path);
            else printc(t, C_RED, "abrir: '%s' não encontrado\n", argv[1]);
        }
    }
    else if (!strcmp(cmd, "apps")) {
        for (int i = 0; i < app_count; i++) {
            printc(t, C_CYAN, "  %-10s", app_list[i].id);
            print(t, "%s\n", app_list[i].name);
        }
    }
    else if (!strcmp(cmd, "ps")) {
        task_info_t tl[64];
        int n = task_snapshot(tl, 64);
        printc(t, C_YELLOW, "  ID  ESTADO      CPU  NOME\n");
        for (int i = 0; i < n; i++)
            print(t, "%4d  %-10s %3u%%  %s\n", tl[i].id, state_name(tl[i].state), tl[i].cpu_percent, tl[i].name);
    }
    else if (!strcmp(cmd, "kill") && argc > 1) {
        task_t *victim = task_find((int)strtol(argv[1], NULL, 10));
        if (!victim) printc(t, C_RED, "kill: tarefa não encontrada\n");
        else if (!task_kill(victim)) printc(t, C_RED, "kill: tarefa do sistema não pode ser finalizada\n");
    }
    else if (!strcmp(cmd, "mem") || !strcmp(cmd, "free")) {
        heap_stats_t hs;
        heap_stats(&hs);
        uint64_t total = pmm_total_bytes(), freeb = pmm_free_bytes();
        print(t, "RAM física: %lu MiB no total, %lu MiB em uso, %lu MiB livres\n", total >> 20, (total - freeb) >> 20, freeb >> 20);
        print(t, "Heap do kernel: %lu KiB reservados, %lu KiB em uso, %lu blocos\n", hs.size >> 10, hs.used >> 10, hs.blocks);
    }
    else if (!strcmp(cmd, "calc")) {
        char e[256] = "";
        for (int i = 1; i < argc; i++) strlcat(e, argv[i], sizeof(e));
        ep = e;
        print(t, "%ld\n", expr());
    }
    else if (!strcmp(cmd, "date")) {
        datetime_t dt;
        time_to_datetime(time_now(), &dt);
        print(t, "%s, %d de %s de %d, %02d:%02d:%02d\n", weekday_name(dt.weekday), dt.day, month_name(dt.month),
              dt.year, dt.hour, dt.minute, dt.second);
    }
    else if (!strcmp(cmd, "uptime")) {
        uint64_t s = timer_ms() / 1000;
        print(t, "ligado há %lu h %lu min %lu s\n", s / 3600, (s / 60) % 60, s % 60);
    }
    else if (!strcmp(cmd, "som")) cmd_som(t, argc, argv);
    else if (!strcmp(cmd, "uname")) print(t, argc > 1 ? "Capivara " CAPIVARA_VERSION " x86_64 (kernel próprio, compilado com GCC)\n" : "Capivara\n");
    else if (!strcmp(cmd, "teclado")) {
        if (argc > 1 && !strcasecmp(argv[1], "us")) keyboard_set_layout(KB_LAYOUT_US);
        else if (argc > 1 && !strcasecmp(argv[1], "abnt2")) keyboard_set_layout(KB_LAYOUT_ABNT2);
        print(t, "layout atual: %s  (use: teclado abnt2 | teclado us)\n",
              keyboard_layout() == KB_LAYOUT_US ? "EUA" : "ABNT2");
    }
    else if (!strcmp(cmd, "tema")) {
        if (argc > 1 && (!strcasecmp(argv[1], "claro") || !strcasecmp(argv[1], "escuro")))
            wm_apply_theme(!strcasecmp(argv[1], "escuro"), theme.accent_index, theme.wallpaper);
        print(t, "tema atual: %s  (use: tema claro | tema escuro)\n", theme.dark ? "escuro" : "claro");
    }
    else if (!strcmp(cmd, "notificar")) {
        char text[160] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strlcat(text, " ", sizeof(text));
            strlcat(text, argv[i], sizeof(text));
        }
        notify("Terminal", text[0] ? text : "Olá!", "Enviada pelo comando notificar.", ICON_TERMINAL, NULL);
    }
    else if (!strcmp(cmd, "captura")) {
        wm_capture(false);
        print(t, "captura salva em ~/Imagens/Capturas de tela\n");
    }
    else if (!strcmp(cmd, "bloquear")) wm_shell_command(SHELL_LOCK);
    else if (!strcmp(cmd, "lixeira")) {
        if (argc > 1 && !strcmp(argv[1], "esvaziar")) {
            trash_empty();
            print(t, "Lixeira esvaziada.\n");
        } else {
            vnode_t *d = vfs_lookup(NULL, TRASH_DIR);
            int n = d ? vfs_child_count(d) : 0;
            if (!n) print(t, "A Lixeira está vazia.\n");
            for (int i = 0; i < n; i++) {
                vnode_t *c = vfs_child_at(d, i);
                char origin[VFS_PATH_MAX];
                if (!trash_origin(c->name, origin, sizeof(origin))) strlcpy(origin, "?", sizeof(origin));
                printc(t, c->type == VFS_DIR ? C_BLUE : C_TEXT, "  %s", c->name);
                printc(t, C_DIM, "   (de %s)\n", origin);
            }
        }
    }
    else if (!strcmp(cmd, "whoami")) print(t, "usuario\n");
    else if (!strcmp(cmd, "hostname")) print(t, "capivara\n");
    else if (!strcmp(cmd, "sysinfo") || !strcmp(cmd, "neofetch")) cmd_sysinfo(t);
    else if (!strcmp(cmd, "dmesg")) {
        char *buf = kmalloc(16384);
        klog_read(buf, 16384);
        put(t, buf, C_DIM);
        kfree(buf);
    }
    else if (!strcmp(cmd, "history")) {
        for (int i = 0; i < t->hist_n; i++) print(t, "%4d  %s\n", i + 1, t->history[i]);
    }
    else if (!strcmp(cmd, "reiniciar") || !strcmp(cmd, "reboot")) power_reboot();
    else if (!strcmp(cmd, "desligar") || !strcmp(cmd, "shutdown") || !strcmp(cmd, "poweroff")) power_off();
    else if (!strcmp(cmd, "exit") || !strcmp(cmd, "sair")) t->quit = true;
    else {
        lab_out_t lo = { lab_write, t, t->cwd };
        if (!lab_run(&lo, argc, argv)) printc(t, C_RED, "%s: comando não encontrado (digite 'ajuda')\n", cmd);
    }
}

/* completa o nome de arquivo com Tab */
static void complete(term_t *t) {
    int start = t->in_len;
    while (start > 0 && t->input[start - 1] != ' ') start--;
    char word[256];
    strlcpy(word, t->input + start, sizeof(word));
    char *slash = strrchr(word, '/');
    vnode_t *dir = t->cwd;
    const char *prefix = word;
    if (slash) {
        *slash = 0;
        dir = word[0] ? resolve(t, word) : vfs_root();
        prefix = slash + 1;
        *slash = '/';
    }
    if (!dir || dir->type != VFS_DIR) return;
    size_t pl = strlen(prefix);
    vnode_t *match = NULL;
    int matches = 0;
    for (int i = 0; i < vfs_child_count(dir); i++) {
        vnode_t *c = vfs_child_at(dir, i);
        if (!strncmp(c->name, prefix, pl)) { match = c; matches++; }
    }
    if (matches != 1) return;
    const char *rest = match->name + pl;
    char add[VFS_NAME_MAX + 2];
    snprintf(add, sizeof(add), "%s%s", rest, match->type == VFS_DIR ? "/" : "");
    size_t n = strlen(add);
    if (t->in_len + n < sizeof(t->input) - 1) {
        memcpy(t->input + t->in_len, add, n + 1);
        t->in_len += (int)n;
        t->in_cur = t->in_len;
    }
}

static void submit(term_t *t) {
    char line[256];
    strlcpy(line, t->input, sizeof(line));
    put(t, t->input, C_WHITE);
    put(t, "\n", 0);
    if (t->in_len) {
        if (t->hist_n == 32) {
            memmove(t->history[0], t->history[1], sizeof(t->history[0]) * 31);
            t->hist_n--;
        }
        strlcpy(t->history[t->hist_n++], line, 256);
    }
    t->hist_pos = t->hist_n;
    t->in_len = t->in_cur = 0;
    t->input[0] = 0;
    run(t, line);
    if (L(t, t->count - 1)->len) new_line(t);
    if (!t->quit) prompt(t);
}

static void key(term_t *t, event_t *e) {
    t->view = 0;
    t->cursor_on = true;
    if ((e->mods & MOD_CTRL) && e->key == KEY_CHAR) {
        uint32_t c = tolower((int)e->ch);
        if (c == 'c') { put(t, t->input, C_WHITE); put(t, "^C\n", C_DIM); t->in_len = t->in_cur = 0; t->input[0] = 0; if (t->tatu) tatu_shell_cancel(t->tatu); prompt(t); }
        else if (c == 'l') { t->count = 0; t->first = 0; prompt(t); }
        else if (c == 'v') {
            int n;
            const char *clip = clipboard_get(&n);
            for (int i = 0; clip && i < n && t->in_len < 250; i++) {
                if (clip[i] == '\n') continue;
                memmove(t->input + t->in_cur + 1, t->input + t->in_cur, t->in_len - t->in_cur + 1);
                t->input[t->in_cur++] = clip[i];
                t->in_len++;
            }
        }
        return;
    }
    switch (e->key) {
    case KEY_ENTER: submit(t); return;
    case KEY_TAB: complete(t); return;
    case KEY_UP:
        if (t->hist_pos > 0) {
            t->hist_pos--;
            strlcpy(t->input, t->history[t->hist_pos], sizeof(t->input));
            t->in_len = t->in_cur = (int)strlen(t->input);
        }
        return;
    case KEY_DOWN:
        if (t->hist_pos < t->hist_n) {
            t->hist_pos++;
            strlcpy(t->input, t->hist_pos < t->hist_n ? t->history[t->hist_pos] : "", sizeof(t->input));
            t->in_len = t->in_cur = (int)strlen(t->input);
        }
        return;
    case KEY_PGUP: t->view += t->rows / 2; return;
    case KEY_PGDN: t->view = t->view > t->rows / 2 ? t->view - t->rows / 2 : 0; return;
    }
    ui_edit_t ed;
    memcpy(ed.text, t->input, sizeof(t->input));
    ed.len = t->in_len;
    ed.cur = t->in_cur;
    ed.scroll = 0;
    ui_edit_key(&ed, e);
    memcpy(t->input, ed.text, sizeof(t->input));
    t->in_len = ed.len;
    t->in_cur = ed.cur;
}

void terminal_main(void *arg) {
    term_t *t = kcalloc(1, sizeof(term_t));
    t->lines = kcalloc(MAX_LINES, sizeof(line_t));
    t->cwd = vfs_lookup(NULL, "/home/usuario");
    /* O argumento é a pasta onde começar ou, com "cmd:", um comando para já
     * rodar (o app Estudos abre os laboratórios assim). */
    char *first_cmd = NULL;
    if (arg) {
        if (!strncmp(arg, "cmd:", 4)) {
            first_cmd = kstrdup((char *)arg + 4);
        } else {
            vnode_t *d = vfs_lookup(NULL, arg);
            if (d && d->type == VFS_DIR) t->cwd = d;
        }
        kfree(arg);
    }
    t->win = win_create("Terminal", ICON_TERMINAL, WIN_AUTO, WIN_AUTO, 736, 448, WF_APP);
    win_set_cursor(t->win, CURSOR_IBEAM);
    layout(t);
    printc(t, C_YELLOW, "Capivara OS - terminal\n");
    printc(t, C_DIM, "Digite 'ajuda' para ver os comandos.\n\n");
    prompt(t);
    if (first_cmd) {
        draw(t);                  /* mostra a janela antes: o comando pode demorar */
        strlcpy(t->input, first_cmd, sizeof(t->input));
        t->in_len = t->in_cur = (int)strlen(t->input);
        kfree(first_cmd);
        submit(t);
    }
    t->cursor_on = true;
    draw(t);
    ev_set_timer(530);

    while (!t->quit) {
        event_t e;
        gui_wait(&e);
        switch (e.type) {
        case EV_KEY_DOWN: key(t, &e); break;
        case EV_TIMER: {
            /* só o cursor muda: apaga/desenha a barrinha e avisa só esse retângulo */
            t->cursor_on = !t->cursor_on;
            canvas_t *c = win_canvas(t->win);
            rect_t r = t->cursor_rect;
            gfx_fill(c, r.x, r.y + 13, r.w, 3, t->cursor_on && t->view == 0 ? palette[C_WHITE] : HEX(0x0C0C0C));
            win_invalidate_rect(t->win, r);
            continue;
        }
        case EV_MOUSE_WHEEL:
            t->view += e.wheel * 3;
            if (t->view < 0) t->view = 0;
            if (t->view > t->count) t->view = t->count;
            break;
        case EV_RESIZE: layout(t); break;
        case EV_CLOSE: t->quit = true; break;
        default: continue;
        }
        if (!t->quit) draw(t);
    }
    win_destroy(t->win);
    tatu_shell_free(t->tatu);
    kfree(t->lines);
    kfree(t);
}
