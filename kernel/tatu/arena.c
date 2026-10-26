/*
 * Arena, texto que cresce (sb_t), vetores e o tratamento de erros.
 *
 * Arena: em vez de um malloc/free para cada pedacinho, pedimos blocos
 * grandes e vamos "cortando" deles. No fim do comando, tudo é devolvido de
 * uma vez. É o mesmo truque de muitos compiladores e bancos de dados.
 */
#include "tatu/internal.h"

#define CHUNK 16384

void *ar_alloc(arena_t *a, size_t n) {
    n = (n + 15) & ~(size_t)15;
    arena_chunk_t *c = a->head;
    if (!c || c->used + n > c->size) {
        size_t size = n > CHUNK ? n : CHUNK;
        c = kmalloc(sizeof(arena_chunk_t) + size);
        if (!c) tatu_fail(TE_GENERIC, "Memória insuficiente");
        c->size = size;
        c->used = 0;
        c->next = a->head;
        a->head = c;
    }
    void *p = c->data + c->used;
    c->used += n;
    memset(p, 0, n);
    return p;
}

char *ar_strndup(arena_t *a, const char *s, size_t n) {
    char *p = ar_alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *ar_strdup(arena_t *a, const char *s) { return ar_strndup(a, s, strlen(s)); }

char *ar_printf(arena_t *a, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return ar_strdup(a, buf);
}

void ar_free(arena_t *a) {
    for (arena_chunk_t *c = a->head, *next; c; c = next) {
        next = c->next;
        kfree(c);
    }
    a->head = NULL;
}

/* ------------------------------------------------------------------ sb_t */
void sb_init(sb_t *b, arena_t *a) {
    b->a = a;
    b->cap = 64;
    b->len = 0;
    b->buf = ar_alloc(a, b->cap);
}

void sb_addn(sb_t *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap * 2;
        while (cap < b->len + n + 1) cap *= 2;
        char *nb = ar_alloc(b->a, cap);          /* a arena não devolve: o antigo fica até o fim */
        memcpy(nb, b->buf, b->len);
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

void sb_add(sb_t *b, const char *s) { sb_addn(b, s, strlen(s)); }
void sb_addc(sb_t *b, char c) { sb_addn(b, &c, 1); }

void sb_printf(sb_t *b, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sb_addn(b, buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

char *sb_str(sb_t *b) { return b->buf; }

void vec_push(arena_t *a, vec_t *v, void *p) {
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        void **nv = ar_alloc(a, (size_t)cap * sizeof(void *));
        if (v->n) memcpy(nv, v->v, (size_t)v->n * sizeof(void *));
        v->v = nv;
        v->cap = cap;
    }
    v->v[v->n++] = p;
}

/* ------------------------------------------------------------------ erros */
ctx_t *tatu_ctx;

const char *tatu_error_label(tatu_errkind_t kind) {
    switch (kind) {
    case TE_SYNTAX: return "Erro de sintaxe";
    case TE_CATALOG: return "Erro de catálogo";
    case TE_CONSTRAINT: return "Restrição violada";
    case TE_TYPE: return "Erro de tipo";
    case TE_EXECUTION: return "Erro de execução";
    case TE_TRANSACTION: return "Erro de transação";
    default: return "Erro";
    }
}

/* A setinha: "(linha 1, coluna 18)", a linha do SQL e um ^ embaixo do
 * problema. A coluna conta caracteres, não bytes (um "ç" ocupa dois). */
void tatu_render_error(tatu_error_t *e, const char *sql) {
    if (!sql || e->pos < 0) {
        e->line = e->column = 0;
        strlcpy(e->rendered, e->message, sizeof(e->rendered));
        return;
    }
    int start = e->pos, line = 1;
    while (start > 0 && sql[start - 1] != '\n') start--;
    for (int i = 0; i < start; i++) if (sql[i] == '\n') line++;
    int end = e->pos;
    while (sql[end] && sql[end] != '\n') end++;
    int col = utf8_len_n(sql + start, e->pos - start);
    e->line = line;
    e->column = col + 1;
    char text[300];
    int n = end - start < (int)sizeof(text) - 1 ? end - start : (int)sizeof(text) - 1;
    memcpy(text, sql + start, (size_t)n);
    text[n] = 0;
    for (int i = 0; i < n; i++) if (text[i] == '\t') text[i] = ' ';
    char pad[300];
    int k = col < (int)sizeof(pad) - 1 ? col : (int)sizeof(pad) - 1;
    memset(pad, ' ', (size_t)k);
    pad[k] = 0;
    snprintf(e->rendered, sizeof(e->rendered), "%s (linha %d, coluna %d)\n  %s\n  %s^",
             e->message, line, col + 1, text, pad);
}

static __attribute__((noreturn)) void jump(void) {
    __builtin_longjmp(tatu_ctx->jb, 1);
}

static void fill(tatu_errkind_t kind, int pos, const char *fmt, va_list ap) {
    tatu_error_t *e = tatu_ctx->err;
    e->kind = kind;
    e->pos = pos;
    vsnprintf(e->message, sizeof(e->message), fmt, ap);
    tatu_render_error(e, pos >= 0 ? tatu_ctx->sql : NULL);
}

void tatu_fail(tatu_errkind_t kind, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fill(kind, -1, fmt, ap);
    va_end(ap);
    jump();
}

void tatu_fail_at(tatu_errkind_t kind, int pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fill(kind, pos, fmt, ap);
    va_end(ap);
    jump();
}
