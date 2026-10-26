/*
 * Etapa 2: parser (analisador sintático).
 *
 * Lê a lista de tokens e monta a árvore sintática de ast.c. É um parser
 * descendente recursivo: cada regra da gramática vira uma função, e as
 * funções chamam umas às outras seguindo a gramática.
 *
 * As expressões são montadas por níveis de precedência, do mais fraco para o
 * mais forte:
 *
 *     OR → AND → NOT → = < > IS IN LIKE BETWEEN → + - || → * / % → -x → átomo
 *
 * Cada nível só chama o nível seguinte. Por isso 1 + 2 * 3 vira 1 + (2 * 3):
 * o * é resolvido "mais embaixo", antes do +.
 */
#include "tatu/internal.h"

#define MAX_DEPTH 32            /* parênteses dentro de parênteses (a pilha de uma tarefa é pequena) */

typedef struct {
    const char *sql;
    token_t *t;
    int n, i, params;
} parser_t;

static node_t *expr(parser_t *p);

/* ------------------------------------------------------------ utilidades */
static token_t *tok(parser_t *p) { return &p->t[p->i]; }

static token_t *advance(parser_t *p) {
    token_t *t = &p->t[p->i];
    if (t->kind != TK_EOF) p->i++;
    return t;
}

static bool check(parser_t *p, int kind, const char *text) {
    token_t *t = tok(p);
    return t->kind == kind && (!text || !strcmp(t->text, text));
}

static token_t *accept(parser_t *p, int kind, const char *text) {
    return check(p, kind, text) ? advance(p) : NULL;
}

static bool accept_kw(parser_t *p, const char *w) { return accept(p, TK_KEYWORD, w) != NULL; }
static bool accept_sym(parser_t *p, const char *s) { return accept(p, TK_SYMBOL, s) != NULL; }

/* "Esperava X, mas encontrei Y", com a setinha no token da vez */
static __attribute__((noreturn)) void error(parser_t *p, const char *message) {
    token_t *t = tok(p);
    const char *found;
    char num[40];
    if (t->kind == TK_EOF) found = "o fim do comando";
    else if (t->kind == TK_STRING) found = ar_printf(TMP, "o texto '%s'", t->text);
    else if ((t->kind == TK_IDENT || t->kind == TK_KEYWORD) && !t->quoted)
        found = ar_printf(TMP, "'%s'", ar_strndup(TMP, p->sql + t->pos, (size_t)t->len));   /* como foi digitado */
    else if (t->kind == TK_NUMBER) {
        if (t->num.type == TV_REAL) real_repr(t->num.r, num);
        else snprintf(num, sizeof(num), "%ld", t->num.i);
        found = ar_printf(TMP, "'%s'", num);
    } else found = ar_printf(TMP, "'%s'", t->text);
    tatu_fail_at(TE_SYNTAX, t->pos, "%s, mas encontrei %s", message, found);
}

static void expect_kw(parser_t *p, const char *w) {
    if (!accept_kw(p, w)) error(p, ar_printf(TMP, "Esperava %s", w));
}

static void expect_sym(parser_t *p, const char *s) {
    if (!accept_sym(p, s)) error(p, ar_printf(TMP, "Esperava '%s'", s));
}

static const char *identifier(parser_t *p, const char *what) {
    if (check(p, TK_IDENT, NULL)) return advance(p)->text;
    error(p, ar_printf(TMP, "Esperava %s", what));
}

static void deeper(parser_t *p) {
    if (++tatu_ctx->depth > MAX_DEPTH)
        tatu_fail_at(TE_SYNTAX, tok(p)->pos, "Expressão aninhada demais (o limite é %d níveis)", MAX_DEPTH);
}

/* vetor de ponteiros na arena, copiado de um vec_t */
static void **freeze(vec_t *v) {
    void **out = ar_alloc(TMP, sizeof(void *) * (size_t)(v->n ? v->n : 1));
    for (int i = 0; i < v->n; i++) out[i] = v->v[i];
    return out;
}

static node_t **expr_list(parser_t *p, int *n) {
    vec_t v = { 0 };
    vec_push(TMP, &v, expr(p));
    while (accept_sym(p, ",")) vec_push(TMP, &v, expr(p));
    *n = v.n;
    return (node_t **)freeze(&v);
}

/* ------------------------------------------------------------ expressões */
static node_t *binary(int op, node_t *a, node_t *b) {
    node_t *n = new_node(N_BINARY);
    n->op = (uint8_t)op;
    n->a = a;
    n->b = b;
    return n;
}

static node_t *unary(parser_t *p);

static node_t *call(parser_t *p, const char *name) {
    node_t *n = new_node(N_CALL);
    n->name = name;
    if (accept_sym(p, "*")) {
        expect_sym(p, ")");
        n->star = true;
        return n;
    }
    n->distinct = accept_kw(p, "DISTINCT");
    if (!check(p, TK_SYMBOL, ")")) n->items = expr_list(p, &n->nitems);
    expect_sym(p, ")");
    return n;
}

static char *upper(const char *s) {
    char *u = ar_strdup(TMP, s);
    for (char *c = u; *c; c++) *c = (char)toupper(*c);
    return u;
}

static node_t *primary(parser_t *p) {
    token_t *t = tok(p);
    if (t->kind == TK_NUMBER || t->kind == TK_STRING) {
        if (t->kind == TK_NUMBER && t->num.type == TV_INT && t->num.i == INT64_MIN)
            tatu_fail_at(TE_SYNTAX, t->pos, "Número positivo grande demais para INTEGER de 64 bits");
        advance(p);
        node_t *n = new_node(N_LITERAL);
        n->value = t->kind == TK_NUMBER ? t->num : vtext(t->text);
        return n;
    }
    if (t->kind == TK_PARAM) {
        advance(p);
        node_t *n = new_node(N_PARAM);
        n->index = p->params++;
        return n;
    }
    if (accept_kw(p, "NULL")) return new_node(N_LITERAL);
    if (check(p, TK_KEYWORD, "TRUE") || check(p, TK_KEYWORD, "FALSE")) {
        advance(p);
        node_t *n = new_node(N_LITERAL);
        n->value = vbool(t->text[0] == 'T');
        return n;
    }
    if (accept_sym(p, "(")) {
        node_t *e = expr(p);
        expect_sym(p, ")");
        return e;
    }
    if (t->kind == TK_IDENT) {
        advance(p);
        if (accept_sym(p, "(")) return call(p, upper(t->text));      /* nome( → chamada de função */
        if (accept_sym(p, ".")) {                                     /* tabela.coluna ou tabela.* */
            if (accept_sym(p, "*")) {
                node_t *n = new_node(N_STAR);
                n->table = t->text;
                return n;
            }
            node_t *n = new_node(N_COLUMN);
            n->name = identifier(p, "o nome da coluna");
            n->table = t->text;
            return n;
        }
        node_t *n = new_node(N_COLUMN);
        n->name = t->text;
        return n;
    }
    error(p, "Esperava um valor, uma coluna ou uma expressão");
}

static node_t *unary(parser_t *p) {
    if (accept_sym(p, "-")) {
        if (tok(p)->kind == TK_NUMBER && tok(p)->num.type == TV_INT && tok(p)->num.i == INT64_MIN) {
            node_t *n = new_node(N_LITERAL);
            n->value = advance(p)->num;
            return n;
        }
        deeper(p);
        node_t *operand = unary(p);
        tatu_ctx->depth--;
        /* -5 já vira o literal -5 (em vez de "negativo de 5") */
        if (operand->kind == N_LITERAL && operand->value.type == TV_INT) {
            if (operand->value.i == INT64_MIN) tatu_fail_at(TE_SYNTAX, tok(p)->pos, "Negação excedeu o intervalo INTEGER de 64 bits");
            operand->value.i = -operand->value.i;
            return operand;
        }
        if (operand->kind == N_LITERAL && operand->value.type == TV_REAL) { operand->value.r = -operand->value.r; return operand; }
        node_t *n = new_node(N_UNARY);
        n->op = OP_NEG;
        n->a = operand;
        return n;
    }
    if (accept_sym(p, "+")) {
        deeper(p);
        node_t *n = unary(p);
        tatu_ctx->depth--;
        return n;
    }
    return primary(p);
}

static node_t *multiplicative(parser_t *p) {
    node_t *left = unary(p);
    for (;;) {
        int op = check(p, TK_SYMBOL, "*") ? OP_MUL : check(p, TK_SYMBOL, "/") ? OP_DIV
               : check(p, TK_SYMBOL, "%") ? OP_MOD : -1;
        if (op < 0) return left;
        advance(p);
        left = binary(op, left, unary(p));
    }
}

static node_t *additive(parser_t *p) {
    node_t *left = multiplicative(p);
    for (;;) {
        int op = check(p, TK_SYMBOL, "+") ? OP_ADD : check(p, TK_SYMBOL, "-") ? OP_SUB
               : check(p, TK_SYMBOL, "||") ? OP_CONCAT : -1;
        if (op < 0) return left;
        advance(p);
        left = binary(op, left, multiplicative(p));
    }
}

static node_t *comparison(parser_t *p) {
    node_t *left = additive(p);
    static const struct { const char *sym; int op; } cmp[] = {
        { "=", OP_EQ }, { "!=", OP_NE }, { "<>", OP_NE }, { "<", OP_LT }, { "<=", OP_LE }, { ">", OP_GT }, { ">=", OP_GE },
    };
    for (unsigned k = 0; k < sizeof(cmp) / sizeof(cmp[0]); k++)
        if (accept_sym(p, cmp[k].sym)) return binary(cmp[k].op, left, additive(p));

    if (accept_kw(p, "IS")) {
        node_t *n = new_node(N_ISNULL);
        n->negated = accept_kw(p, "NOT");
        expect_kw(p, "NULL");
        n->a = left;
        return n;
    }
    bool negated = accept_kw(p, "NOT");
    if (accept_kw(p, "IN")) {
        expect_sym(p, "(");
        node_t *n = new_node(N_INLIST);
        n->items = expr_list(p, &n->nitems);
        expect_sym(p, ")");
        n->a = left;
        n->negated = negated;
        return n;
    }
    if (accept_kw(p, "BETWEEN")) {
        node_t *n = new_node(N_BETWEEN);
        n->a = left;
        n->b = additive(p);                    /* additive, e não expr: o AND aqui é do BETWEEN */
        expect_kw(p, "AND");
        n->c = additive(p);
        n->negated = negated;
        return n;
    }
    if (accept_kw(p, "LIKE")) {
        node_t *n = new_node(N_LIKE);
        n->a = left;
        n->b = additive(p);
        n->negated = negated;
        return n;
    }
    if (negated) error(p, "Esperava IN, BETWEEN ou LIKE depois de NOT");
    return left;
}

static node_t *not_expr(parser_t *p) {
    if (accept_kw(p, "NOT")) {
        deeper(p);
        node_t *n = new_node(N_UNARY);
        n->op = OP_NOT;
        n->a = not_expr(p);
        tatu_ctx->depth--;
        return n;
    }
    return comparison(p);
}

static node_t *and_expr(parser_t *p) {
    node_t *left = not_expr(p);
    while (accept_kw(p, "AND")) left = binary(OP_AND, left, not_expr(p));
    return left;
}

static node_t *expr(parser_t *p) {
    deeper(p);
    node_t *left = and_expr(p);
    while (accept_kw(p, "OR")) left = binary(OP_OR, left, and_expr(p));
    tatu_ctx->depth--;
    return left;
}

/* ------------------------------------------------------------ comandos */
static stmt_t *new_stmt(int kind, int pos) {
    stmt_t *s = ar_alloc(TMP, sizeof(stmt_t));
    s->kind = kind;
    s->pos = pos;
    return s;
}

static coldef_t column_def(parser_t *p) {
    coldef_t c = { 0 };
    c.name = identifier(p, "o nome da coluna");
    if (!check(p, TK_IDENT, NULL))
        error(p, ar_printf(TMP, "Esperava o tipo da coluna '%s' (INTEGER, REAL, TEXT ou BOOLEAN)", c.name));
    token_t *type = advance(p);
    c.type = normalize_type(type->text);
    if (!c.type)
        tatu_fail_at(TE_SYNTAX, type->pos, "Tipo desconhecido '%s' — use INTEGER, REAL, TEXT ou BOOLEAN", type->text);
    if (accept_sym(p, "(")) {                       /* VARCHAR(80): o tamanho é aceito e ignorado */
        if (!accept(p, TK_NUMBER, NULL)) error(p, "Esperava o tamanho do tipo");
        expect_sym(p, ")");
    }
    for (;;) {
        if (accept_kw(p, "PRIMARY")) { expect_kw(p, "KEY"); c.primary_key = true; }
        else if (accept_kw(p, "NOT")) { expect_kw(p, "NULL"); c.not_null = true; }
        else if (accept_kw(p, "NULL")) { }          /* "pode ser nulo": já é o padrão */
        else if (accept_kw(p, "UNIQUE")) c.unique = true;
        else if (accept_kw(p, "DEFAULT")) c.def = unary(p);
        else if (accept_kw(p, "REFERENCES")) {
            c.ref_table = identifier(p, "o nome da tabela referenciada");
            expect_sym(p, "(");
            c.ref_column = identifier(p, "o nome da coluna referenciada");
            expect_sym(p, ")");
        } else break;
    }
    return c;
}

static stmt_t *create(parser_t *p, int pos) {
    expect_kw(p, "CREATE");
    if (accept_kw(p, "TABLE")) {
        stmt_t *s = new_stmt(S_CREATE_TABLE, pos);
        if (accept_kw(p, "IF")) {
            expect_kw(p, "NOT");
            expect_kw(p, "EXISTS");
            s->flag = true;
        }
        s->name = identifier(p, "o nome da tabela");
        expect_sym(p, "(");
        vec_t cols = { 0 };
        do {
            coldef_t *c = ar_alloc(TMP, sizeof(coldef_t));
            *c = column_def(p);
            vec_push(TMP, &cols, c);
        } while (accept_sym(p, ","));
        expect_sym(p, ")");
        s->ncols = cols.n;
        s->cols = ar_alloc(TMP, sizeof(coldef_t) * (size_t)cols.n);
        for (int i = 0; i < cols.n; i++) s->cols[i] = *(coldef_t *)cols.v[i];
        return s;
    }
    bool unique = accept_kw(p, "UNIQUE");
    if (accept_kw(p, "INDEX")) {
        stmt_t *s = new_stmt(S_CREATE_INDEX, pos);
        s->flag = unique;
        s->name = identifier(p, "o nome do índice");
        expect_kw(p, "ON");
        s->table = identifier(p, "o nome da tabela");
        expect_sym(p, "(");
        s->column = identifier(p, "o nome da coluna");
        expect_sym(p, ")");
        return s;
    }
    error(p, "Esperava TABLE ou INDEX depois de CREATE");
}

static stmt_t *drop(parser_t *p, int pos) {
    expect_kw(p, "DROP");
    expect_kw(p, "TABLE");
    stmt_t *s = new_stmt(S_DROP_TABLE, pos);
    if (accept_kw(p, "IF")) {
        expect_kw(p, "EXISTS");
        s->flag = true;
    }
    s->name = identifier(p, "o nome da tabela");
    return s;
}

static stmt_t *insert(parser_t *p, int pos) {
    expect_kw(p, "INSERT");
    expect_kw(p, "INTO");
    stmt_t *s = new_stmt(S_INSERT, pos);
    s->name = identifier(p, "o nome da tabela");
    s->nnames = -1;                                 /* sem lista: todas as colunas, na ordem da tabela */
    if (accept_sym(p, "(")) {
        vec_t v = { 0 };
        do vec_push(TMP, &v, (void *)identifier(p, "o nome da coluna")); while (accept_sym(p, ","));
        expect_sym(p, ")");
        s->names = (const char **)freeze(&v);
        s->nnames = v.n;
    }
    expect_kw(p, "VALUES");
    vec_t rows = { 0 }, lens = { 0 };
    do {
        expect_sym(p, "(");
        int n;
        node_t **row = expr_list(p, &n);
        expect_sym(p, ")");
        vec_push(TMP, &rows, row);
        vec_push(TMP, &lens, (void *)(intptr_t)n);
    } while (accept_sym(p, ","));
    s->nrows = rows.n;
    s->rows = (node_t ***)freeze(&rows);
    s->rowlen = ar_alloc(TMP, sizeof(int) * (size_t)rows.n);
    for (int i = 0; i < rows.n; i++) s->rowlen[i] = (int)(intptr_t)lens.v[i];
    return s;
}

static table_ref_t table_ref(parser_t *p) {
    table_ref_t r = { 0 };
    r.name = identifier(p, "o nome da tabela");
    if (accept_kw(p, "AS")) r.alias = identifier(p, "um apelido (alias)");
    else if (check(p, TK_IDENT, NULL)) r.alias = advance(p)->text;
    return r;
}

static select_t *select_stmt(parser_t *p) {
    expect_kw(p, "SELECT");
    select_t *q = ar_alloc(TMP, sizeof(select_t));
    q->distinct = accept_kw(p, "DISTINCT");

    vec_t items = { 0 };
    do {
        select_item_t *it = ar_alloc(TMP, sizeof(select_item_t));
        if (accept_sym(p, "*")) it->expr = new_node(N_STAR);
        else {
            it->expr = expr(p);
            if (accept_kw(p, "AS")) it->alias = identifier(p, "um apelido (alias)");
            else if (check(p, TK_IDENT, NULL)) it->alias = advance(p)->text;   /* o AS é opcional */
        }
        vec_push(TMP, &items, it);
    } while (accept_sym(p, ","));
    q->nitems = items.n;
    q->items = ar_alloc(TMP, sizeof(select_item_t) * (size_t)items.n);
    for (int i = 0; i < items.n; i++) q->items[i] = *(select_item_t *)items.v[i];

    if (accept_kw(p, "FROM")) {
        q->from = ar_alloc(TMP, sizeof(table_ref_t));
        *q->from = table_ref(p);
        vec_t joins = { 0 };
        for (;;) {
            join_t *j = ar_alloc(TMP, sizeof(join_t));
            bool cross = accept_sym(p, ",");        /* FROM a, b  ==  FROM a CROSS JOIN b */
            if (!cross && accept_kw(p, "CROSS")) {
                expect_kw(p, "JOIN");
                cross = true;
            }
            if (cross) {
                j->kind = J_CROSS;
                j->table = table_ref(p);
                vec_push(TMP, &joins, j);
                continue;
            }
            if (accept_kw(p, "LEFT")) {
                accept_kw(p, "OUTER");
                expect_kw(p, "JOIN");
                j->kind = J_LEFT;
            } else if (accept_kw(p, "INNER")) {
                expect_kw(p, "JOIN");
                j->kind = J_INNER;
            } else if (accept_kw(p, "JOIN")) {
                j->kind = J_INNER;
            } else break;
            j->table = table_ref(p);
            expect_kw(p, "ON");
            j->on = expr(p);
            vec_push(TMP, &joins, j);
        }
        q->njoins = joins.n;
        q->joins = ar_alloc(TMP, sizeof(join_t) * (size_t)(joins.n ? joins.n : 1));
        for (int i = 0; i < joins.n; i++) q->joins[i] = *(join_t *)joins.v[i];
    }
    if (accept_kw(p, "WHERE")) q->where = expr(p);
    if (accept_kw(p, "GROUP")) {
        expect_kw(p, "BY");
        q->group_by = expr_list(p, &q->ngroup);
    }
    if (accept_kw(p, "HAVING")) q->having = expr(p);
    if (accept_kw(p, "ORDER")) {
        expect_kw(p, "BY");
        vec_t v = { 0 };
        do {
            order_item_t *o = ar_alloc(TMP, sizeof(order_item_t));
            o->expr = expr(p);
            if (accept_kw(p, "DESC")) o->desc = true;
            else accept_kw(p, "ASC");
            vec_push(TMP, &v, o);
        } while (accept_sym(p, ","));
        q->norder = v.n;
        q->order_by = ar_alloc(TMP, sizeof(order_item_t) * (size_t)v.n);
        for (int i = 0; i < v.n; i++) q->order_by[i] = *(order_item_t *)v.v[i];
    }
    if (accept_kw(p, "LIMIT")) {
        q->limit = expr(p);
        if (accept_kw(p, "OFFSET")) q->offset = expr(p);
    }
    return q;
}

static stmt_t *update(parser_t *p, int pos) {
    expect_kw(p, "UPDATE");
    stmt_t *s = new_stmt(S_UPDATE, pos);
    s->name = identifier(p, "o nome da tabela");
    expect_kw(p, "SET");
    vec_t names = { 0 }, exprs = { 0 };
    do {
        vec_push(TMP, &names, (void *)identifier(p, "o nome da coluna"));
        expect_sym(p, "=");
        vec_push(TMP, &exprs, expr(p));
    } while (accept_sym(p, ","));
    s->names = (const char **)freeze(&names);
    s->nnames = names.n;
    s->exprs = (node_t **)freeze(&exprs);
    if (accept_kw(p, "WHERE")) s->where = expr(p);
    return s;
}

static stmt_t *delete_stmt(parser_t *p, int pos) {
    expect_kw(p, "DELETE");
    expect_kw(p, "FROM");
    stmt_t *s = new_stmt(S_DELETE, pos);
    s->name = identifier(p, "o nome da tabela");
    if (accept_kw(p, "WHERE")) s->where = expr(p);
    return s;
}

static stmt_t *statement(parser_t *p) {
    token_t *t = tok(p);
    int pos = t->pos;
    if (t->kind == TK_KEYWORD) {
        const char *w = t->text;
        if (!strcmp(w, "SELECT")) { stmt_t *s = new_stmt(S_SELECT, pos); s->select = select_stmt(p); return s; }
        if (!strcmp(w, "INSERT")) return insert(p, pos);
        if (!strcmp(w, "UPDATE")) return update(p, pos);
        if (!strcmp(w, "DELETE")) return delete_stmt(p, pos);
        if (!strcmp(w, "CREATE")) return create(p, pos);
        if (!strcmp(w, "DROP")) return drop(p, pos);
        if (!strcmp(w, "BEGIN")) { advance(p); accept_kw(p, "TRANSACTION"); return new_stmt(S_BEGIN, pos); }
        if (!strcmp(w, "COMMIT")) { advance(p); return new_stmt(S_COMMIT, pos); }
        if (!strcmp(w, "ROLLBACK")) { advance(p); return new_stmt(S_ROLLBACK, pos); }
        if (!strcmp(w, "EXPLAIN")) {
            advance(p);
            stmt_t *s = new_stmt(S_EXPLAIN, pos);
            s->select = select_stmt(p);
            return s;
        }
    }
    error(p, "Esperava um comando (SELECT, INSERT, UPDATE, DELETE, CREATE...)");
}

/* Analisa um ou mais comandos separados por ';'. */
stmt_t **parse_script(const char *sql, int *count, int *nparams) {
    parser_t p = { .sql = sql };
    tatu_ctx->depth = 0;
    p.t = tokenize(sql, &p.n, false, NULL);
    vec_t stmts = { 0 };
    for (;;) {
        while (accept_sym(&p, ";")) { }
        if (check(&p, TK_EOF, NULL)) break;
        vec_push(TMP, &stmts, statement(&p));
        if (!check(&p, TK_EOF, NULL)) expect_sym(&p, ";");
    }
    *count = stmts.n;
    if (nparams) *nparams = p.params;
    return (stmt_t **)freeze(&stmts);
}
