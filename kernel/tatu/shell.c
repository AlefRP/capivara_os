/* Shell SQL nativo. O Terminal cuida das teclas; aqui ficam SQL e resultados. */
#include "tatu/tatu.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "lib/utf8.h"
#include "mm/heap.h"

struct tatu_shell {
    tatu_db_t *db;
    tatu_write_fn write;
    void *ctx;
    char *buf;
    int len, cap;
};

static void say(tatu_shell_t *s, int color, const char *fmt, ...) {
    char buf[1600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s->write(s->ctx, color, buf);
}

static void border(tatu_shell_t *s, const int *width, int n) {
    say(s, 1, "+");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < width[i] + 2; j++) say(s, 1, "-");
        say(s, 1, "+");
    }
    say(s, 1, "\n");
}

static void cell(tatu_shell_t *s, const char *text, int width, int color) {
    const char *p = text;
    int chars = 0;
    while (*p && chars < width) { utf8_decode(&p); chars++; }
    char buf[256];
    size_t bytes = (size_t)(p - text);
    if (bytes >= sizeof(buf)) bytes = sizeof(buf) - 1;
    memcpy(buf, text, bytes);
    buf[bytes] = 0;
    /* Uma quebra dentro de uma célula não pode desmontar a tabela. */
    for (size_t i = 0; i < bytes; i++) if ((unsigned char)buf[i] < 32) buf[i] = ' ';
    say(s, color, " %s", buf);
    for (int j = chars; j < width; j++) say(s, color, " ");
    say(s, 1, " |");
}

static void show(tatu_shell_t *s, tatu_result_t *r) {
    if (!r->ncols) { say(s, 2, "%s\n", r->message ? r->message : "OK"); return; }
    /* Largura limitada mantém a saída legível no Terminal de 90 colunas. */
    int n = r->ncols > 8 ? 8 : r->ncols, width[8];
    int cap = 80 / n - 3;
    if (cap < 4) cap = 4;
    if (cap > 26) cap = 26;
    for (int i = 0; i < n; i++) {
        width[i] = (int)utf8_count(r->cols[i], strlen(r->cols[i]));
        for (int j = 0; j < r->nrows; j++) {
            char buf[256];
            tatu_format_cell(&r->cells[j * r->ncols + i], buf, sizeof(buf));
            int w = (int)utf8_count(buf, strlen(buf));
            if (w > width[i]) width[i] = w;
        }
        if (width[i] > cap) width[i] = cap;
        if (width[i] < 4) width[i] = 4;
    }
    border(s, width, n);
    say(s, 1, "|");
    for (int i = 0; i < n; i++) cell(s, r->cols[i], width[i], 6);
    say(s, 0, "\n");
    border(s, width, n);
    int rows = r->nrows > 200 ? 200 : r->nrows;
    for (int j = 0; j < rows; j++) {
        say(s, 1, "|");
        for (int i = 0; i < n; i++) {
            char buf[256];
            tatu_value_t *v = &r->cells[j * r->ncols + i];
            tatu_format_cell(v, buf, sizeof(buf));
            cell(s, buf, width[i], v->type == TV_NULL ? 1 : 0);
        }
        say(s, 0, "\n");
    }
    border(s, width, n);
    say(s, 1, "%d linha(s) · %llu ms%s\n", r->nrows,
        (unsigned long long)(r->micros / 1000), rows < r->nrows ? " (mostrando 200)" : "");
    if (n < r->ncols) say(s, 4, "Mostrando 8 de %d colunas; veja todas no Toca.\n", r->ncols);
}

static void run_sql(tatu_shell_t *s, const char *sql) {
    tatu_output_t out = {0};
    tatu_error_t err;
    bool ok = tatu_run(s->db, sql, &out, &err);
    for (tatu_result_t *r = out.first; r; r = r->next) show(s, r);
    if (!ok) say(s, 5, "%s: %s\n", tatu_error_label(err.kind), err.rendered);
    tatu_output_free(&out);
}

tatu_shell_t *tatu_shell_new(tatu_db_t *db, tatu_write_fn write, void *ctx) {
    tatu_shell_t *s = kcalloc(1, sizeof(*s));
    if (!s) return NULL;
    s->db = db;
    s->write = write;
    s->ctx = ctx;
    return s;
}

void tatu_shell_banner(tatu_shell_t *s) {
    say(s, 4, "\nTatu DB · banco de dados relacional educacional\n");
    say(s, 1, "Banco: %s\n", tatu_path(s->db) ? tatu_path(s->db) : "em memória");
    say(s, 1, "Arquivos do Capivara ficam na RAM e somem ao reiniciar.\n");
    say(s, 6, ".help ajuda · .demo exemplos · .quit sair · termine SQL com ;\n\n");
}

static bool meta(tatu_shell_t *s, const char *line) {
    char cmd[32];
    int n = 0;
    while (line[n] && !isspace(line[n]) && n < (int)sizeof(cmd) - 1) { cmd[n] = line[n]; n++; }
    cmd[n] = 0;
    const char *arg = line + n;
    while (isspace(*arg)) arg++;
    if (!strcmp(cmd, ".quit") || !strcmp(cmd, ".q") || !strcmp(cmd, ".exit")) {
        if (tatu_in_transaction(s->db)) say(s, 4, "Transação aberta: ROLLBACK ao sair.\n");
        return false;
    }
    if (!strcmp(cmd, ".help")) {
        say(s, 6, ".tables          lista tabelas\n.schema [tabela] mostra esquema e índices\n"
            ".demo            carrega os tatus de exemplo\n.read /caminho   executa arquivo SQL\n.quit            volta ao Terminal\n");
        say(s, 1, "SELECT, INSERT, UPDATE, DELETE, CREATE TABLE/INDEX, DROP TABLE,\n"
            "JOIN, GROUP BY, EXPLAIN, BEGIN, COMMIT e ROLLBACK.\n");
    } else if (!strcmp(cmd, ".demo")) {
        if (tatu_tables(s->db, NULL, 0)) say(s, 4, "Carregue .demo em um banco vazio para preservar suas tabelas.\n");
        else run_sql(s, tatu_demo_sql);
    }
    else if (!strcmp(cmd, ".tables") || !strcmp(cmd, ".schema")) {
        const char *names[128];
        int total = tatu_tables(s->db, names, 128);
        if (!total) say(s, 1, "Nenhuma tabela. Experimente .demo\n");
        if (total > 128) total = 128;
        bool found = false;
        for (int i = 0; i < total; i++) {
            if (*arg && strcmp(arg, names[i])) continue;
            found = true;
            if (!strcmp(cmd, ".schema")) {
                char buf[8192];
                if (tatu_schema_sql(s->db, names[i], buf, sizeof(buf))) {
                    /* O esquema pode ser maior que o buffer de say(). */
                    s->write(s->ctx, 0, buf);
                    say(s, 0, "\n");
                }
            } else {
                int cols = 0, rows = 0, indexes = 0;
                tatu_table_info(s->db, names[i], &cols, &rows, &indexes);
                say(s, 0, "  %-24s %d coluna(s), %d linha(s), %d índice(s)\n", names[i], cols, rows, indexes);
            }
        }
        if (*arg && !found) say(s, 5, "A tabela '%s' não existe.\n", arg);
    } else if (!strcmp(cmd, ".read")) {
        vnode_t *f = vfs_lookup(NULL, arg);
        size_t size = f ? (size_t)f->size : 0;
        if (!f || f->type != VFS_FILE || size > 1024 * 1024) {
            say(s, 5, "Use .read /caminho/arquivo.sql (até 1 MiB).\n");
        } else {
            char *sql = kmalloc(size + 1);
            if (!sql) say(s, 5, "Memória insuficiente.\n");
            else {
                long got = vfs_read(f, 0, sql, size);
                sql[got > 0 ? got : 0] = 0;
                run_sql(s, sql);
                kfree(sql);
            }
        }
    } else say(s, 4, "Comando desconhecido: %s. Digite .help\n", cmd);
    return true;
}

bool tatu_shell_feed(tatu_shell_t *s, const char *line) {
    while (!s->len && isspace(*line)) line++;
    if (!s->len && *line == '.') return meta(s, line);
    if (!s->len && !*line) return true;
    int n = (int)strlen(line);
    if (s->len + n + 2 > 65536) {
        say(s, 5, "Comando muito grande (limite: 64 KiB). Entrada descartada.\n");
        tatu_shell_cancel(s);
        return true;
    }
    if (s->len + n + 2 > s->cap) {
        int cap = (s->len + n + 2) * 2;
        char *buf = krealloc(s->buf, (size_t)cap);
        if (!buf) { say(s, 5, "Memória insuficiente.\n"); return true; }
        s->buf = buf;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, line, (size_t)n);
    s->len += n;
    s->buf[s->len++] = '\n';
    s->buf[s->len] = 0;
    if (tatu_is_complete(s->buf)) {
        run_sql(s, s->buf);
        s->len = 0;
        s->buf[0] = 0;
    }
    return true;
}

void tatu_shell_cancel(tatu_shell_t *s) { s->len = 0; if (s->buf) s->buf[0] = 0; }
const char *tatu_shell_prompt(tatu_shell_t *s) { return s->len ? "  ..> " : "tatu> "; }
void tatu_shell_free(tatu_shell_t *s) {
    if (!s) return;
    tatu_close(s->db);
    kfree(s->buf);
    kfree(s);
}
