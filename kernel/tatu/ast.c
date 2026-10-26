/*
 * A árvore sintática (o nodes.py do Tatu em Python).
 *
 * O parser transforma tokens nestes nós. Cada tipo de nó representa uma
 * construção do SQL:
 *
 *     SELECT nome FROM tatus WHERE idade > 3
 *
 * vira um select_t com items = [Column(nome)], from = tatus e
 * where = Binary('>', Column(idade), Literal(3)).
 *
 * Dois nós podem ser comparados (node_eq): o planejador usa isso para
 * perceber que o COUNT(*) do SELECT é o mesmo do HAVING. E todo nó sabe se
 * escrever de volta como SQL (node_str): é daí que vêm os nomes de colunas
 * como "idade + 1" e as linhas do EXPLAIN.
 */
#include "tatu/internal.h"

const char *const op_text[] = { "OR", "AND", "=", "!=", "<", "<=", ">", ">=", "+", "-", "||",
                                "*", "/", "%", "NOT", "-" };

/* força de cada operador binário: só para imprimir com os parênteses certos */
static int precedence(int op) {
    switch (op) {
    case OP_OR: return 1;
    case OP_AND: return 2;
    case OP_ADD: case OP_SUB: case OP_CONCAT: return 5;
    case OP_MUL: case OP_DIV: case OP_MOD: return 6;
    default: return 4;                                     /* comparações */
    }
}

node_t *new_node(int kind) {
    node_t *n = ar_alloc(TMP, sizeof(node_t));
    n->kind = (uint8_t)kind;
    return n;
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return !strcmp(a, b);
}

bool node_eq(const node_t *a, const node_t *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind || a->op != b->op || a->negated != b->negated || a->distinct != b->distinct ||
        a->star != b->star || a->index != b->index || a->nitems != b->nitems)
        return false;
    if (a->kind == N_LITERAL && !value_eq(a->value, b->value)) return false;
    if (!str_eq(a->name, b->name) || !str_eq(a->table, b->table)) return false;
    if (!node_eq(a->a, b->a) || !node_eq(a->b, b->b) || !node_eq(a->c, b->c)) return false;
    for (int i = 0; i < a->nitems; i++)
        if (!node_eq(a->items[i], b->items[i])) return false;
    return true;
}

static void list_str(sb_t *b, node_t **items, int n) {
    for (int i = 0; i < n; i++) {
        if (i) sb_add(b, ", ");
        node_str(b, items[i]);
    }
}

void node_str(sb_t *b, const node_t *n) {
    switch (n->kind) {
    case N_LITERAL: sb_add(b, sql_literal(n->value)); break;
    case N_PARAM: sb_addc(b, '?'); break;
    case N_COLUMN:
        if (n->table) { sb_add(b, n->table); sb_addc(b, '.'); }
        sb_add(b, n->name);
        break;
    case N_STAR:
        if (n->table) { sb_add(b, n->table); sb_addc(b, '.'); }
        sb_addc(b, '*');
        break;
    case N_UNARY:
        sb_add(b, n->op == OP_NOT ? "NOT " : "-");
        node_str(b, n->a);
        break;
    case N_BINARY: {
        int me = precedence(n->op);
        for (int side = 0; side < 2; side++) {
            const node_t *child = side ? n->b : n->a;
            bool wrap = child->kind == N_BINARY &&
                        (precedence(child->op) < me || (side && precedence(child->op) == me));
            if (side) { sb_addc(b, ' '); sb_add(b, op_text[n->op]); sb_addc(b, ' '); }
            if (wrap) sb_addc(b, '(');
            node_str(b, child);
            if (wrap) sb_addc(b, ')');
        }
        break;
    }
    case N_ISNULL:
        node_str(b, n->a);
        sb_add(b, n->negated ? " IS NOT NULL" : " IS NULL");
        break;
    case N_INLIST:
        node_str(b, n->a);
        sb_add(b, n->negated ? " NOT IN (" : " IN (");
        list_str(b, n->items, n->nitems);
        sb_addc(b, ')');
        break;
    case N_BETWEEN:
        node_str(b, n->a);
        sb_add(b, n->negated ? " NOT BETWEEN " : " BETWEEN ");
        node_str(b, n->b);
        sb_add(b, " AND ");
        node_str(b, n->c);
        break;
    case N_LIKE:
        node_str(b, n->a);
        sb_add(b, n->negated ? " NOT LIKE " : " LIKE ");
        node_str(b, n->b);
        break;
    case N_CALL:
        sb_add(b, n->name);
        sb_addc(b, '(');
        if (n->star) sb_addc(b, '*');
        else {
            if (n->distinct) sb_add(b, "DISTINCT ");
            list_str(b, n->items, n->nitems);
        }
        sb_addc(b, ')');
        break;
    }
}

const char *node_text(const node_t *n) {
    sb_t b;
    sb_init(&b, TMP);
    node_str(&b, n);
    return sb_str(&b);
}

void node_walk(node_t *n, void (*fn)(node_t *n, void *ctx), void *ctx) {
    if (!n) return;
    /* A pilha explícita também permite rejeitar SQL muito profundo antes que
     * node_str/compile_expr precisem percorrê-lo recursivamente. */
    typedef struct { node_t *node; int depth; } visit_t;
    vec_t pending = { 0 };
    visit_t *first = ar_alloc(TMP, sizeof(*first));
    *first = (visit_t){ n, 0 };
    vec_push(TMP, &pending, first);
    while (pending.n) {
        visit_t *v = pending.v[--pending.n];
        if (v->depth > 64) tatu_fail(TE_EXECUTION, "Expressão profunda demais; divida o cálculo em etapas");
        node_t *cur = v->node;
        fn(cur, ctx);
        for (int i = cur->nitems - 1; i >= -3; i--) {
            node_t *child = i >= 0 ? cur->items[i] : i == -1 ? cur->c : i == -2 ? cur->b : cur->a;
            if (!child) continue;
            visit_t *next = ar_alloc(TMP, sizeof(*next));
            *next = (visit_t){ child, v->depth + 1 };
            vec_push(TMP, &pending, next);
        }
    }
}

/* todas as expressões de um comando (para achar e trocar os '?') */
void stmt_walk(stmt_t *s, void (*fn)(node_t *n, void *ctx), void *ctx) {
    select_t *q = s->select;
    if (q) {
        for (int i = 0; i < q->nitems; i++) node_walk(q->items[i].expr, fn, ctx);
        for (int i = 0; i < q->njoins; i++) node_walk(q->joins[i].on, fn, ctx);
        node_walk(q->where, fn, ctx);
        for (int i = 0; i < q->ngroup; i++) node_walk(q->group_by[i], fn, ctx);
        node_walk(q->having, fn, ctx);
        for (int i = 0; i < q->norder; i++) node_walk(q->order_by[i].expr, fn, ctx);
        node_walk(q->limit, fn, ctx);
        node_walk(q->offset, fn, ctx);
    }
    for (int i = 0; i < s->ncols; i++) node_walk(s->cols[i].def, fn, ctx);
    for (int r = 0; r < s->nrows; r++)
        for (int i = 0; i < s->rowlen[r]; i++) node_walk(s->rows[r][i], fn, ctx);
    if (s->kind == S_UPDATE)
        for (int i = 0; i < s->nnames; i++) node_walk(s->exprs[i], fn, ctx);
    node_walk(s->where, fn, ctx);
}
