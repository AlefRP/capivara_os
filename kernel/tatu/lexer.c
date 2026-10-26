/*
 * Etapa 1: tokenizer (analisador léxico).
 *
 * Transforma o texto SQL numa lista de tokens, as "palavras" da linguagem:
 *
 *     "SELECT nome FROM tatus WHERE idade > 3"
 *
 * vira
 *
 *     KEYWORD(SELECT) IDENT(nome) KEYWORD(FROM) IDENT(tatus)
 *     KEYWORD(WHERE) IDENT(idade) SYMBOL(>) NUMBER(3) EOF
 *
 * O tokenizer não sabe nada de gramática: só reconhece palavras, números,
 * textos e símbolos. Quem dá sentido à sequência é o parser.
 *
 * Diferença do Python: lá, letras e dígitos seguem o Unicode inteiro; aqui,
 * qualquer byte acima de 127 (os caracteres acentuados em UTF-8) conta como
 * letra num nome, e só A-Z e as letras acentuadas do Latin-1 mudam de
 * maiúscula para minúscula.
 */
#include "tatu/internal.h"

static const char *const keywords[] = {
    "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "AS", "DISTINCT",
    "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE",
    "CREATE", "TABLE", "DROP", "INDEX", "ON", "IF", "EXISTS",
    "PRIMARY", "KEY", "UNIQUE", "REFERENCES", "DEFAULT", "NULL",
    "TRUE", "FALSE", "IS", "IN", "BETWEEN", "LIKE",
    "JOIN", "INNER", "LEFT", "OUTER", "CROSS",
    "GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET",
    "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION", "EXPLAIN",
};

/* Símbolos de dois caracteres vêm primeiro: "<=" não pode virar "<" e "=". */
static const char *const symbols[] = {
    "<=", ">=", "<>", "!=", "||", "=", "<", ">", "+", "-", "*", "/", "%", "(", ")", ",", ";", ".",
};

/* A palavra em maiúsculas, se for palavra-chave (o texto vem da tabela). */
static const char *keyword(const char *s, size_t n) {
    if (n > 11) return NULL;
    for (unsigned i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        const char *k = keywords[i];
        if (strlen(k) != n) continue;
        size_t j = 0;
        while (j < n && toupper(s[j]) == k[j]) j++;
        if (j == n) return k;
    }
    return NULL;
}

bool tatu_is_keyword(const char *word, size_t len) { return keyword(word, len) != NULL; }

static bool word_start(unsigned char c) { return isalpha(c) || c == '_' || c >= 0x80; }
static bool word_char(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

/* nomes não diferenciam maiúsculas: Tatus == tatus (ASCII e os acentos do
 * Latin-1, que em UTF-8 são C3 80 a C3 9E) */
static char *lower_word(const char *s, size_t n) {
    char *out = ar_strndup(TMP, s, n);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)out[i];
        if (c >= 'A' && c <= 'Z') out[i] = (char)(c + 32);
        else if (c == 0xC3 && i + 1 < n) {
            unsigned char d = (unsigned char)out[i + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) out[i + 1] = (char)(d + 32);
            i++;
        }
    }
    return out;
}

token_t *tokenize(const char *sql, int *count, bool lenient, bool *unterminated) {
    vec_t toks = { 0 };
    int i = 0, n = (int)strlen(sql);
    if (unterminated) *unterminated = false;

    while (i < n) {
        unsigned char ch = (unsigned char)sql[i];

        /* 1) espaços em branco não significam nada */
        if (isspace(ch)) { i++; continue; }

        /* 2) comentários: de "--" até o fim da linha */
        if (ch == '-' && sql[i + 1] == '-') {
            while (i < n && sql[i] != '\n') i++;
            continue;
        }

        token_t *t = ar_alloc(TMP, sizeof(token_t));
        t->pos = i;

        /* 3) números: 42, 3.14, .5 */
        if (isdigit(ch) || (ch == '.' && isdigit(sql[i + 1]))) {
            int start = i;
            while (i < n && isdigit(sql[i])) i++;
            t->kind = TK_NUMBER;
            if (i < n && sql[i] == '.') {
                i++;
                while (i < n && isdigit(sql[i])) i++;
                double r;
                parse_real(sql + start, (size_t)(i - start), &r);
                if (r != r || r > 1.7976931348623157e308 || r < -1.7976931348623157e308)
                    tatu_fail_at(TE_SYNTAX, start, "Número grande demais para REAL");
                t->num = vreal(r);
            } else {
                uint64_t v = 0;
                const uint64_t limit = (uint64_t)INT64_MAX + 1;
                for (int k = start; k < i; k++) {
                    unsigned digit = (unsigned)(sql[k] - '0');
                    if (v > (limit - digit) / 10) tatu_fail_at(TE_SYNTAX, start, "Número grande demais para INTEGER de 64 bits");
                    v = v * 10 + digit;
                }
                /* A magnitude de INT64_MIN só é aceita se houver '-' antes. */
                t->num = vint(v == limit ? INT64_MIN : (int64_t)v);
            }
            t->len = i - start;
            t->text = ar_strndup(TMP, sql + start, (size_t)t->len);
            vec_push(TMP, &toks, t);
            continue;
        }

        /* 4) textos: 'olá' — duas aspas seguidas ('') representam uma aspa */
        if (ch == '\'') {
            int start = i++;
            sb_t b;
            sb_init(&b, TMP);
            for (;;) {
                if (i >= n) {
                    if (lenient) { if (unterminated) *unterminated = true; goto done; }
                    tatu_fail_at(TE_SYNTAX, start, "Texto sem aspa de fechamento");
                }
                if (sql[i] == '\'') {
                    if (sql[i + 1] == '\'') { sb_addc(&b, '\''); i += 2; continue; }
                    i++;
                    break;
                }
                sb_addc(&b, sql[i++]);
            }
            t->kind = TK_STRING;
            t->text = sb_str(&b);
            t->len = i - start;
            vec_push(TMP, &toks, t);
            continue;
        }

        /* 5) identificadores entre aspas duplas: "Nome Com Espaço" */
        if (ch == '"') {
            int end = i + 1;
            while (end < n && sql[end] != '"') end++;
            if (end >= n) {
                if (lenient) { if (unterminated) *unterminated = true; goto done; }
                tatu_fail_at(TE_SYNTAX, i, "Identificador sem aspa de fechamento");
            }
            t->kind = TK_IDENT;
            t->quoted = true;
            t->text = ar_strndup(TMP, sql + i + 1, (size_t)(end - i - 1));
            t->len = end + 1 - i;
            i = end + 1;
            vec_push(TMP, &toks, t);
            continue;
        }

        /* 6) palavras: ou são palavras-chave (SELECT) ou nomes (tatus) */
        if (word_start(ch)) {
            int start = i;
            while (i < n && word_char((unsigned char)sql[i])) i++;
            const char *kw = keyword(sql + start, (size_t)(i - start));
            t->kind = kw ? TK_KEYWORD : TK_IDENT;
            t->text = kw ? kw : lower_word(sql + start, (size_t)(i - start));
            t->len = i - start;
            vec_push(TMP, &toks, t);
            continue;
        }

        /* 7) parâmetros: o "?" de tatu_execute("... WHERE id = ?", ...) */
        if (ch == '?') {
            t->kind = TK_PARAM;
            t->text = "?";
            t->len = 1;
            i++;
            vec_push(TMP, &toks, t);
            continue;
        }

        /* 8) símbolos e operadores */
        bool found = false;
        for (unsigned k = 0; k < sizeof(symbols) / sizeof(symbols[0]); k++) {
            size_t len = strlen(symbols[k]);
            if (!strncmp(sql + i, symbols[k], len)) {
                t->kind = TK_SYMBOL;
                t->text = symbols[k];
                t->len = (int)len;
                i += (int)len;
                found = true;
                break;
            }
        }
        if (!found) {
            /* mostra o caractere inteiro, mesmo que ocupe mais de um byte */
            int len = 1;
            while (((unsigned char)sql[i + len] & 0xC0) == 0x80) len++;
            tatu_fail_at(TE_SYNTAX, i, "Caractere inesperado '%s'", ar_strndup(TMP, sql + i, (size_t)len));
        }
        vec_push(TMP, &toks, t);
    }
done:;
    token_t *eof = ar_alloc(TMP, sizeof(token_t));
    eof->kind = TK_EOF;
    eof->pos = n;
    eof->text = "";
    vec_push(TMP, &toks, eof);

    token_t *out = ar_alloc(TMP, sizeof(token_t) * (size_t)toks.n);
    for (int k = 0; k < toks.n; k++) out[k] = *(token_t *)toks.v[k];
    *count = toks.n;
    return out;
}

/* O comando acabou? Termina com ';' fora de um texto entre aspas. Um texto
 * com aspa aberta quer dizer que ainda estão digitando. */
bool tatu_is_complete(const char *sql) {
    tatu_lock();
    arena_t a = { 0 };
    ctx_t ctx = { 0 }, *saved = tatu_ctx;
    tatu_error_t err;
    ctx.err = &err;
    ctx.sql = sql;
    ctx.arena = &a;
    tatu_ctx = &ctx;
    bool complete;
    if (__builtin_setjmp(ctx.jb)) {
        complete = true;                     /* erro de verdade: deixa o parser mostrar */
    } else {
        int n;
        bool open;
        token_t *t = tokenize(sql, &n, true, &open);
        complete = !open && n >= 2 && t[n - 2].kind == TK_SYMBOL && !strcmp(t[n - 2].text, ";");
    }
    tatu_ctx = saved;
    ar_free(&a);
    tatu_unlock();
    return complete;
}
