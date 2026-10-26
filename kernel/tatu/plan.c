/* Planejador e iteradores: cada operador pede uma linha ao seu filho.
 * Ordenação e agrupamento precisam ler tudo; filtro, projeção e LIMIT não. */
#include "tatu/internal.h"

enum { O_SINGLE, O_SEQ, O_INDEX, O_FILTER, O_JOIN, O_GROUP, O_SORT, O_PROJECT, O_DISTINCT, O_LIMIT };

typedef struct { tatu_value_t *v; int n, cap; } values_t;
typedef struct group {
    tatu_value_t *key;
    values_t *args;
    struct group *next;
} group_t;

static op_t *make_op(int kind, op_t *child) {
    op_t *op = ar_alloc(TMP, sizeof(*op));
    op->kind = kind;
    if (child) { op->child[0] = child; op->nchild = 1; op->scope = child->scope; }
    return op;
}

scope_t table_scope(table_t *t, const char *ref) {
    scope_t s = { .n = t->schema.ncols };
    s.slots = ar_alloc(TMP, (size_t)s.n * sizeof(slot_t));
    for (int i = 0; i < s.n; i++) s.slots[i] = (slot_t){ ref, t->schema.cols[i].name, NULL };
    return s;
}

static op_t *scan(table_t *t, const char *ref) {
    op_t *op = make_op(O_SEQ, NULL);
    op->table = t;
    op->ref_name = ref;
    op->scope = table_scope(t, ref);
    return op;
}

static op_t *filter(op_t *child, const node_t *cond) {
    op_t *op = make_op(O_FILTER, child);
    op->cond = cond;
    op->pred = compile_expr(cond, &child->scope);
    return op;
}

static void split_and(const node_t *n, vec_t *v, int depth) {
    if (!n) return;
    if (depth > 64) tatu_fail(TE_EXECUTION, "WHERE profundo demais; divida a expressão");
    if (n->kind == N_BINARY && n->op == OP_AND) {
        split_and(n->a, v, depth + 1);
        split_and(n->b, v, depth + 1);
    } else vec_push(TMP, v, (void *)n);
}

static node_t *join_and(vec_t *v) {
    node_t *out = v->v[0];
    for (int i = 1; i < v->n; i++) {
        node_t *n = new_node(N_BINARY);
        n->op = OP_AND;
        n->a = out;
        n->b = v->v[i];
        out = n;
    }
    return out;
}

/* Só a igualdade com literal e tipo compatível pode substituir o filtro. */
static void choose_index(op_t *op, vec_t *conditions, const scope_t *all) {
    for (int k = 0; k < conditions->n; k++) {
        node_t *n = conditions->v[k];
        if (n->kind != N_BINARY || n->op != OP_EQ) continue;
        for (int side = 0; side < 2; side++) {
            node_t *col = side ? n->b : n->a, *literal = side ? n->a : n->b;
            if (col->kind != N_COLUMN || literal->kind != N_LITERAL) continue;
            int pos = scope_try_resolve(all, col);
            if (pos < 0 || !all->slots[pos].table || strcmp(all->slots[pos].table, op->ref_name)) continue;
            index_t *ix = table_index_for(op->table, col->name);
            int ci = schema_find(&op->table->schema, col->name);
            if (!ix || ci < 0 || family(literal->value) != type_family(op->table->schema.cols[ci].type)) continue;
            op->kind = O_INDEX;
            op->index = ix;
            op->value = literal->value;
            for (int i = k + 1; i < conditions->n; i++) conditions->v[i - 1] = conditions->v[i];
            conditions->n--;
            return;
        }
    }
}

static bool contains_aggregate(const node_t *n, int depth) {
    if (!n) return false;
    if (depth > 64) tatu_fail(TE_EXECUTION, "Expressão profunda demais");
    if (is_aggregate(n)) return true;
    if (contains_aggregate(n->a, depth + 1) || contains_aggregate(n->b, depth + 1)
        || contains_aggregate(n->c, depth + 1)) return true;
    for (int i = 0; i < n->nitems; i++) if (contains_aggregate(n->items[i], depth + 1)) return true;
    return false;
}

static void collect(const node_t *n, vec_t *calls, int depth) {
    if (!n) return;
    if (depth > 64) tatu_fail(TE_EXECUTION, "Expressão profunda demais");
    if (is_aggregate(n)) {
        if ((n->star && strcmp(n->name, "COUNT")) || (!n->star && n->nitems != 1))
            tatu_fail(TE_EXECUTION, "%s() recebe um argumento; apenas COUNT aceita '*'", n->name);
        for (int i = 0; i < n->nitems; i++) if (contains_aggregate(n->items[i], 0))
            tatu_fail(TE_EXECUTION, "Agregação dentro de agregação não é permitida: %s", node_text(n));
        for (int i = 0; i < calls->n; i++) if (node_eq(n, calls->v[i])) return;
        vec_push(TMP, calls, (void *)n);
        return;
    }
    collect(n->a, calls, depth + 1);
    collect(n->b, calls, depth + 1);
    collect(n->c, calls, depth + 1);
    for (int i = 0; i < n->nitems; i++) collect(n->items[i], calls, depth + 1);
}

static int64_t constant_int(const node_t *n, const char *what) {
    if (!n) return 0;
    if (n->kind == N_LITERAL && n->value.type == TV_INT && n->value.i >= 0) return n->value.i;
    tatu_fail(TE_EXECUTION, "%s precisa ser um número inteiro não negativo", what);
}

op_t *plan_select(const select_t *s, storage_t *st) {
    vec_t conditions = { 0 };
    split_and(s->where, &conditions, 0);
    op_t *op;
    if (!s->from) op = make_op(O_SINGLE, NULL);
    else {
        int nrefs = s->njoins + 1;
        table_t **tables = ar_alloc(TMP, (size_t)nrefs * sizeof(table_t *));
        const char **refs = ar_alloc(TMP, (size_t)nrefs * sizeof(char *));
        scope_t all = { 0 };
        for (int i = 0; i < nrefs; i++) {
            const table_ref_t *r = i ? &s->joins[i - 1].table : s->from;
            refs[i] = r->alias ? r->alias : r->name;
            for (int j = 0; j < i; j++) if (!strcmp(refs[i], refs[j]))
                tatu_fail(TE_CATALOG, "A tabela '%s' aparece duas vezes no FROM; dê um apelido a cada uma", refs[i]);
            tables[i] = storage_get_table(st, r->name);
            all = scope_concat(all, table_scope(tables[i], refs[i]));
        }
        op = scan(tables[0], refs[0]);
        choose_index(op, &conditions, &all);
        for (int i = 1; i < nrefs; i++) {
            op_t *right = scan(tables[i], refs[i]);
            op_t *join = make_op(O_JOIN, op);
            join->child[1] = right;
            join->nchild = 2;
            join->scope = scope_concat(op->scope, right->scope);
            join->join_kind = s->joins[i - 1].kind;
            join->cond = s->joins[i - 1].on;
            if (join->cond) join->pred = compile_expr(join->cond, &join->scope);
            op = join;
        }
    }
    if (conditions.n) op = filter(op, join_and(&conditions));

    order_item_t *orders = ar_alloc(TMP, (size_t)s->norder * sizeof(order_item_t));
    for (int i = 0; i < s->norder; i++) {
        orders[i] = s->order_by[i];
        node_t *e = orders[i].expr;
        if (e->kind == N_LITERAL && e->value.type == TV_INT) {
            int64_t p = e->value.i;
            if (p < 1 || p > s->nitems || s->items[p - 1].expr->kind == N_STAR)
                tatu_fail(TE_EXECUTION, "ORDER BY %ld: não existe essa coluna no SELECT", p);
            orders[i].expr = s->items[p - 1].expr;
        } else if (e->kind == N_COLUMN && !e->table) {
            for (int j = 0; j < s->nitems; j++) if (s->items[j].alias && !strcmp(s->items[j].alias, e->name)) {
                orders[i].expr = s->items[j].expr;
                break;
            }
        }
    }
    vec_t calls = { 0 };
    for (int i = 0; i < s->nitems; i++) collect(s->items[i].expr, &calls, 0);
    collect(s->having, &calls, 0);
    for (int i = 0; i < s->norder; i++) collect(orders[i].expr, &calls, 0);
    if (s->ngroup || calls.n) {
        op_t *g = make_op(O_GROUP, op);
        g->group_by = (const node_t **)s->group_by;
        g->ngroup = s->ngroup;
        g->calls = (const node_t **)calls.v;
        g->ncalls = calls.n;
        g->nfns = s->ngroup + calls.n;
        g->fns = ar_alloc(TMP, (size_t)g->nfns * sizeof(cexpr_t *));
        g->scope = (scope_t){ .n = g->nfns, .aggregated = true };
        g->scope.slots = ar_alloc(TMP, (size_t)g->nfns * sizeof(slot_t));
        for (int i = 0; i < s->ngroup; i++) {
            node_t *e = s->group_by[i];
            g->fns[i] = compile_expr(e, &op->scope);
            slot_t slot = { .expr = e };
            if (e->kind == N_COLUMN) slot = op->scope.slots[scope_resolve(&op->scope, e)];
            slot.expr = e;
            g->scope.slots[i] = slot;
        }
        for (int i = 0; i < calls.n; i++) {
            node_t *e = calls.v[i];
            if (!e->star) g->fns[s->ngroup + i] = compile_expr(e->items[0], &op->scope);
            g->scope.slots[s->ngroup + i].expr = e;
        }
        op = g;
        if (s->having) op = filter(op, s->having);
    } else if (s->having) tatu_fail(TE_EXECUTION, "HAVING só pode ser usado com GROUP BY ou agregações");

    if (s->norder) {
        op_t *sort = make_op(O_SORT, op);
        sort->items = orders;
        sort->nitems = s->norder;
        sort->fns = ar_alloc(TMP, (size_t)s->norder * sizeof(cexpr_t *));
        for (int i = 0; i < s->norder; i++) sort->fns[i] = compile_expr(orders[i].expr, &op->scope);
        op = sort;
    }

    op_t *project = make_op(O_PROJECT, op);
    vec_t fns = { 0 }, names = { 0 };
    for (int i = 0; i < s->nitems; i++) {
        select_item_t item = s->items[i];
        if (item.expr->kind == N_STAR) {
            int count;
            int *positions = scope_star(&op->scope, item.expr->table, &count);
            for (int k = 0; k < count; k++) {
                slot_t slot = op->scope.slots[positions[k]];
                node_t *col = new_node(N_COLUMN);
                col->table = slot.table;
                col->name = slot.name;
                vec_push(TMP, &fns, compile_expr(col, &op->scope));
                vec_push(TMP, &names, (void *)slot.name);
            }
        } else {
            vec_push(TMP, &fns, compile_expr(item.expr, &op->scope));
            vec_push(TMP, &names, (void *)(item.alias ? item.alias : item.expr->kind == N_COLUMN
                         ? item.expr->name : node_text(item.expr)));
        }
    }
    project->nfns = fns.n;
    project->fns = (cexpr_t **)fns.v;
    project->names = (const char **)names.v;
    project->scope = (scope_t){ .n = fns.n };
    project->scope.slots = ar_alloc(TMP, (size_t)fns.n * sizeof(slot_t));
    for (int i = 0; i < fns.n; i++) project->scope.slots[i].name = names.v[i];
    op = project;
    if (s->distinct) op = make_op(O_DISTINCT, op);
    if (s->limit || s->offset) {
        op_t *limit = make_op(O_LIMIT, op);
        limit->has_limit = s->limit != NULL;
        limit->limit = constant_int(s->limit, "LIMIT");
        limit->offset = constant_int(s->offset, "OFFSET");
        op = limit;
    }
    return op;
}

static tatu_value_t **materialize(op_t *op, int *count) {
    vec_t rows = { 0 };
    tatu_value_t *row;
    while ((row = op_next(op))) vec_push(TMP, &rows, row);
    *count = rows.n;
    return (tatu_value_t **)rows.v;
}

static bool row_eq(const tatu_value_t *a, const tatu_value_t *b, int n) {
    for (int i = 0; i < n; i++) if (!value_eq(a[i], b[i])) return false;
    return true;
}

static uint32_t row_hash(const tatu_value_t *row, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ value_hash(row[i])) * 16777619u;
    return h;
}

static void add_value(values_t *b, tatu_value_t v) {
    if (b->n == b->cap) {
        int cap = b->cap ? b->cap * 2 : 16;
        tatu_value_t *next = ar_alloc(TMP, (size_t)cap * sizeof(tatu_value_t));
        memcpy(next, b->v, (size_t)b->n * sizeof(tatu_value_t));
        b->v = next;
        b->cap = cap;
    }
    b->v[b->n++] = v;
}

static group_t *new_group(op_t *op, tatu_value_t *key) {
    group_t *g = ar_alloc(TMP, sizeof(*g));
    g->key = key;
    g->args = ar_alloc(TMP, (size_t)op->ncalls * sizeof(values_t));
    return g;
}

static void group_rows(op_t *op) {
    enum { BUCKETS = 1024 };
    group_t **buckets = ar_alloc(TMP, BUCKETS * sizeof(group_t *));
    vec_t ordered = { 0 };
    tatu_value_t *row;
    while ((row = op_next(op->child[0]))) {
        tatu_value_t *key = ar_alloc(TMP, (size_t)op->ngroup * sizeof(tatu_value_t));
        for (int i = 0; i < op->ngroup; i++) key[i] = eval(op->fns[i], row);
        unsigned b = row_hash(key, op->ngroup) % BUCKETS;
        group_t *g = buckets[b];
        while (g && !row_eq(g->key, key, op->ngroup)) g = g->next;
        if (!g) {
            g = new_group(op, key);
            g->next = buckets[b];
            buckets[b] = g;
            vec_push(TMP, &ordered, g);
        }
        for (int i = 0; i < op->ncalls; i++) {
            tatu_value_t v = op->calls[i]->star ? vint(1) : eval(op->fns[op->ngroup + i], row);
            if (v.type != TV_NULL) add_value(&g->args[i], v);
        }
    }
    if (!ordered.n && !op->ngroup) vec_push(TMP, &ordered, new_group(op, NULL));
    op->nrows = ordered.n;
    op->rows = ar_alloc(TMP, (size_t)op->nrows * sizeof(tatu_value_t *));
    for (int i = 0; i < op->nrows; i++) {
        group_t *g = ordered.v[i];
        tatu_value_t *result = ar_alloc(TMP, (size_t)op->scope.n * sizeof(tatu_value_t));
        memcpy(result, g->key, (size_t)op->ngroup * sizeof(tatu_value_t));
        for (int j = 0; j < op->ncalls; j++) result[op->ngroup + j] = aggregate(op->calls[j], g->args[j].v, g->args[j].n);
        op->rows[i] = result;
    }
}

typedef struct { tatu_value_t *row, *keys; } sort_row_t;

static int sort_cmp(op_t *op, const sort_row_t *a, const sort_row_t *b) {
    for (int i = 0; i < op->nitems; i++) {
        tatu_value_t x = a->keys[i], y = b->keys[i];
        int c;
        if (x.type == TV_NULL || y.type == TV_NULL) c = (x.type != TV_NULL) - (y.type != TV_NULL);
        else {
            if (family(x) != family(y)) tatu_fail(TE_TYPE, "ORDER BY encontrou tipos misturados na mesma coluna");
            c = value_cmp(x, y);
        }
        if (c) return op->items[i].desc ? -c : c;
    }
    return 0;
}

static void sort_rows(op_t *op) {
    op->rows = materialize(op->child[0], &op->nrows);
    int n = op->nrows;
    sort_row_t *a = ar_alloc(TMP, (size_t)n * sizeof(sort_row_t));
    sort_row_t *b = ar_alloc(TMP, (size_t)n * sizeof(sort_row_t));
    for (int i = 0; i < n; i++) {
        a[i].row = op->rows[i];
        a[i].keys = ar_alloc(TMP, (size_t)op->nitems * sizeof(tatu_value_t));
        for (int j = 0; j < op->nitems; j++) a[i].keys[j] = eval(op->fns[j], a[i].row);
    }
    /* Merge sort estável: empates mantêm a ordem de entrada. */
    for (int width = 1; width < n; width *= 2) {
        for (int start = 0; start < n; start += width * 2) {
            int mid = start + width < n ? start + width : n;
            int end = mid + width < n ? mid + width : n;
            int l = start, r = mid;
            for (int k = start; k < end; k++)
                b[k] = r >= end || (l < mid && sort_cmp(op, &a[l], &a[r]) <= 0) ? a[l++] : a[r++];
        }
        sort_row_t *tmp = a; a = b; b = tmp;
    }
    for (int i = 0; i < n; i++) op->rows[i] = a[i].row;
}

void op_open(op_t *op) {
    op->pos = op->rpos = 0;
    op->produced = 0;
    op->done = op->matched = false;
    op->left_row = NULL;
    for (int i = 0; i < op->nchild; i++) op_open(op->child[i]);
    switch (op->kind) {
    case O_SEQ: op->snap = table_scan(op->table, &op->nsnap); break;
    case O_INDEX: {
        int count;
        int64_t *ids = index_lookup(op->index, op->value, &count);
        op->snap = ar_alloc(TMP, (size_t)count * sizeof(heap_slot_t));
        op->nsnap = 0;
        for (int i = 0; i < count; i++) {
            tatu_value_t *row = table_row(op->table, ids[i]);
            if (row) op->snap[op->nsnap++] = (heap_slot_t){ ids[i], row };
        }
        for (int i = 1; i < op->nsnap; i++) {
            heap_slot_t slot = op->snap[i];
            int j = i;
            while (j && op->snap[j - 1].rowid > slot.rowid) { op->snap[j] = op->snap[j - 1]; j--; }
            op->snap[j] = slot;
        }
        break;
    }
    case O_JOIN: op->rows = materialize(op->child[1], &op->nrows); break;
    case O_GROUP: group_rows(op); break;
    case O_SORT: sort_rows(op); break;
    case O_DISTINCT: op->seen = ar_alloc(TMP, sizeof(vec_t)); break;
    default: break;
    }
}

tatu_value_t *op_next(op_t *op) {
    switch (op->kind) {
    case O_SINGLE:
        if (op->done) return NULL;
        op->done = true;
        return ar_alloc(TMP, sizeof(tatu_value_t));
    case O_SEQ: case O_INDEX: return op->pos < op->nsnap ? op->snap[op->pos++].row : NULL;
    case O_SORT: case O_GROUP: return op->pos < op->nrows ? op->rows[op->pos++] : NULL;
    case O_FILTER: {
        tatu_value_t *row;
        while ((row = op_next(op->child[0]))) if (truth(eval(op->pred, row)) == 1) return row;
        return NULL;
    }
    case O_JOIN: {
        int nl = op->child[0]->scope.n, nr = op->child[1]->scope.n;
        for (;;) {
            if (!op->left_row) {
                op->left_row = op_next(op->child[0]);
                if (!op->left_row) return NULL;
                op->rpos = 0;
                op->matched = false;
            }
            while (op->rpos < op->nrows) {
                tatu_value_t *row = ar_alloc(TMP, (size_t)(nl + nr) * sizeof(tatu_value_t));
                memcpy(row, op->left_row, (size_t)nl * sizeof(tatu_value_t));
                memcpy(row + nl, op->rows[op->rpos++], (size_t)nr * sizeof(tatu_value_t));
                if (!op->pred || truth(eval(op->pred, row)) == 1) { op->matched = true; return row; }
            }
            if (op->join_kind == J_LEFT && !op->matched) {
                tatu_value_t *row = ar_alloc(TMP, (size_t)(nl + nr) * sizeof(tatu_value_t));
                memcpy(row, op->left_row, (size_t)nl * sizeof(tatu_value_t));
                op->left_row = NULL;
                return row;              /* a arena zerou a metade direita: TV_NULL */
            }
            op->left_row = NULL;
        }
    }
    case O_PROJECT: {
        tatu_value_t *source = op_next(op->child[0]);
        if (!source) return NULL;
        tatu_value_t *row = ar_alloc(TMP, (size_t)op->nfns * sizeof(tatu_value_t));
        for (int i = 0; i < op->nfns; i++) row[i] = eval(op->fns[i], source);
        return row;
    }
    case O_DISTINCT: {
        vec_t *seen = op->seen;
        tatu_value_t *row;
        while ((row = op_next(op->child[0]))) {
            bool duplicate = false;
            for (int i = 0; i < seen->n; i++) if (row_eq(row, seen->v[i], op->scope.n)) { duplicate = true; break; }
            if (!duplicate) { vec_push(TMP, seen, row); return row; }
        }
        return NULL;
    }
    case O_LIMIT:
        while (op->produced < op->offset) {
            if (!op_next(op->child[0])) return NULL;
            op->produced++;
        }
        if (op->has_limit && op->produced - op->offset >= op->limit) return NULL;
        { tatu_value_t *row = op_next(op->child[0]); if (row) op->produced++; return row; }
    default: return NULL;
    }
}

heap_slot_t *find_rows(table_t *t, const node_t *where, int *count) {
    int n;
    heap_slot_t *rows = table_scan(t, &n);
    if (!where) { *count = n; return rows; }
    scope_t scope = table_scope(t, t->schema.name);
    cexpr_t *pred = compile_expr(where, &scope);
    *count = 0;
    for (int i = 0; i < n; i++) if (truth(eval(pred, rows[i].row)) == 1) rows[(*count)++] = rows[i];
    return rows;
}

static const char *describe(op_t *op) {
    sb_t b;
    sb_init(&b, TMP);
    switch (op->kind) {
    case O_SINGLE: return "SingleRow";
    case O_SEQ: case O_INDEX:
        sb_printf(&b, "%s(%s", op->kind == O_SEQ ? "SeqScan" : "IndexScan", op->table->schema.name);
        if (strcmp(op->ref_name, op->table->schema.name)) sb_printf(&b, " AS %s", op->ref_name);
        if (op->kind == O_INDEX) sb_printf(&b, " usando %s: %s = %s", op->index->name, op->index->column, sql_literal(op->value));
        break;
    case O_FILTER: sb_printf(&b, "Filter(%s", node_text(op->cond)); break;
    case O_JOIN:
        sb_printf(&b, "NestedLoopJoin(%s", op->join_kind == J_LEFT ? "LEFT" : op->join_kind == J_CROSS ? "CROSS" : "INNER");
        if (op->cond) sb_printf(&b, " ON %s", node_text(op->cond));
        break;
    case O_GROUP:
        sb_add(&b, "HashAggregate(");
        for (int i = 0; i < op->ngroup; i++) { sb_add(&b, i ? ", " : "por "); sb_add(&b, node_text(op->group_by[i])); }
        if (op->ngroup && op->ncalls) sb_add(&b, "; ");
        for (int i = 0; i < op->ncalls; i++) { sb_add(&b, i ? ", " : "calcula "); sb_add(&b, node_text(op->calls[i])); }
        break;
    case O_SORT:
        sb_add(&b, "Sort(");
        for (int i = 0; i < op->nitems; i++) { if (i) sb_add(&b, ", "); sb_add(&b, node_text(op->items[i].expr)); if (op->items[i].desc) sb_add(&b, " DESC"); }
        break;
    case O_PROJECT:
        sb_add(&b, "Project(");
        for (int i = 0; i < op->nfns; i++) { if (i) sb_add(&b, ", "); sb_add(&b, op->names[i]); }
        break;
    case O_DISTINCT: return "Distinct";
    case O_LIMIT:
        if (op->has_limit) sb_printf(&b, "Limit(%ld", op->limit);
        else sb_add(&b, "Limit(sem limite");
        if (op->offset) sb_printf(&b, ", pula %ld", op->offset);
        break;
    }
    sb_addc(&b, ')');
    return sb_str(&b);
}

static void explain(op_t *op, vec_t *lines, const char *prefix, const char *branch) {
    vec_push(TMP, lines, ar_printf(TMP, "%s%s%s", prefix, branch, describe(op)));
    const char *child_prefix = ar_printf(TMP, "%s%s", prefix, !*branch ? "" : !strcmp(branch, "└── ") ? "    " : "│   ");
    for (int i = 0; i < op->nchild; i++) explain(op->child[i], lines, child_prefix, i + 1 == op->nchild ? "└── " : "├── ");
}

void op_explain(op_t *op, vec_t *lines) { explain(op, lines, "", ""); }
