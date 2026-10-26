/* Conexão, execução, transações e catálogo público do Tatu DB. */
#include "tatu/internal.h"
#include "sched/task.h"
#include "arch/x86_64/pit.h"
#include "fs/vfs.h"

static mutex_t engine_mutex;
typedef struct connection { tatu_db_t db; struct connection *next; } connection_t;
static connection_t *connections;

void tatu_lock(void) { mutex_lock(&engine_mutex); }
void tatu_unlock(void) { mutex_unlock(&engine_mutex); }

static void error_set(tatu_error_t *err, const char *msg) {
    memset(err, 0, sizeof(*err)); err->kind = TE_GENERIC; err->pos = -1;
    strlcpy(err->message, msg, sizeof(err->message)); strlcpy(err->rendered, msg, sizeof(err->rendered));
}

static const char *path_normalize(const char *path) {
    if (!path || !strcmp(path, ":memory:")) return NULL;
    if (!*path || strlen(path) >= VFS_PATH_MAX) tatu_fail(TE_GENERIC, "Caminho do banco vazio ou longo demais");
    char out[VFS_PATH_MAX] = "/"; size_t n = 1;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        const char *start = p; while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (!len || (len == 1 && *start == '.')) continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (n > 1) { while (n > 1 && out[n - 1] != '/') n--; if (n > 1) n--; out[n] = 0; }
            continue;
        }
        if (len >= VFS_NAME_MAX || n + len + 2 > sizeof(out)) tatu_fail(TE_GENERIC, "Nome de arquivo ou caminho longo demais");
        if (n > 1) out[n++] = '/';
        memcpy(out + n, start, len); n += len; out[n] = 0;
    }
    if (n == 1) tatu_fail(TE_GENERIC, "Escolha um arquivo .tatu, não a pasta raiz");
    return ar_strdup(TMP, out);
}

static void path_available(const char *path, tatu_db_t *self) {
    if (!path) return;
    for (connection_t *c = connections; c; c = c->next)
        if (&c->db != self && c->db.storage.path && !strcmp(c->db.storage.path, path))
            tatu_fail(TE_GENERIC, "'%s' já está aberto; feche a outra conexão antes", path);
}

tatu_db_t *tatu_open(const char *path, tatu_error_t *err) {
    tatu_error_t fallback; if (!err) err = &fallback;
    memset(err, 0, sizeof(*err)); err->pos = -1;
    connection_t *c = kcalloc(1, sizeof(*c));
    if (!c) { error_set(err, "Memória insuficiente para abrir o banco"); return NULL; }
    tatu_lock(); arena_t arena = { 0 }; ctx_t ctx = { .err = err, .arena = &arena }, *saved = tatu_ctx;
    tatu_ctx = &ctx;
    if (__builtin_setjmp(ctx.jb)) {
        storage_free(&c->db.storage); ar_free(&arena); tatu_ctx = saved; tatu_unlock(); kfree(c); return NULL;
    }
    const char *normalized = path_normalize(path); path_available(normalized, NULL);
    storage_init(&c->db.storage, normalized); storage_load(&c->db.storage);
    c->next = connections; connections = c;
    ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return &c->db;
}

void tatu_close(tatu_db_t *db) {
    if (!db) return;
    tatu_lock();
    connection_t **link = &connections;
    while (*link && &(*link)->db != db) link = &(*link)->next;
    if (*link) {
        connection_t *c = *link; *link = c->next;
        storage_free(&db->storage); db->closed = true; kfree(c);
    }
    tatu_unlock();
}

const char *tatu_path(tatu_db_t *db) { return db ? db->storage.path : NULL; }
bool tatu_in_transaction(tatu_db_t *db) { return db && db->in_transaction; }

static bool registered(tatu_db_t *db) {
    for (connection_t *c = connections; c; c = c->next) if (&c->db == db) return true;
    return false;
}

bool tatu_save_as(tatu_db_t *db, const char *path, tatu_error_t *err) {
    tatu_error_t fallback; if (!err) err = &fallback;
    memset(err, 0, sizeof(*err)); err->pos = -1;
    tatu_lock(); arena_t arena = { 0 }; ctx_t ctx = { .err = err, .arena = &arena }, *saved = tatu_ctx;
    tatu_ctx = &ctx;
    if (__builtin_setjmp(ctx.jb)) { ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return false; }
    if (!registered(db)) tatu_fail(TE_GENERIC, "O banco está fechado");
    if (db->in_transaction) tatu_fail(TE_TRANSACTION, "Confirme ou desfaça a transação antes de salvar como");
    const char *normalized = path_normalize(path);
    if (!normalized) tatu_fail(TE_GENERIC, "Escolha um caminho de arquivo para salvar");
    path_available(normalized, db);
    char *new_path = kstrdup(normalized);
    if (!new_path) tatu_fail(TE_GENERIC, "Memória insuficiente para o caminho do banco");
    /* A cópia temporária aponta para as mesmas tabelas, mas só muda seu caminho.
     * Uma falha na gravação deixa o banco original e seu nome intactos. */
    storage_t copy = db->storage; copy.path = new_path;
    ctx_t inner = { .err = err, .arena = &arena }; tatu_ctx = &inner;
    if (__builtin_setjmp(inner.jb)) {
        kfree(new_path); ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return false;
    }
    storage_save(&copy);
    kfree(db->storage.path); db->storage.path = new_path;
    ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return true;
}

void tatu_output_free(tatu_output_t *out) {
    if (!out) return;
    if (out->arena) { ar_free(out->arena); kfree(out->arena); }
    memset(out, 0, sizeof(*out));
}

static tatu_value_t result_value(arena_t *a, tatu_value_t value) {
    if (value.type == TV_TEXT) value.s = ar_strdup(a, value.s);
    return value;
}

static tatu_result_t *message(tatu_output_t *out, const char *text, int count) {
    tatu_result_t *r = ar_alloc(out->arena, sizeof(*r));
    r->message = ar_strdup(out->arena, text); r->rowcount = count; return r;
}

static tatu_result_t *select_result(storage_t *storage, stmt_t *s, tatu_output_t *out) {
    op_t *op = plan_select(s->select, storage);
    tatu_result_t *r = ar_alloc(out->arena, sizeof(*r));
    if (s->kind == S_EXPLAIN) {
        vec_t lines = { 0 }; op_explain(op, &lines);
        r->plan = true; r->ncols = 1; r->nrows = lines.n;
        r->cols = ar_alloc(out->arena, sizeof(*r->cols)); r->cols[0] = ar_strdup(out->arena, "plano");
        r->cells = ar_alloc(out->arena, (size_t)lines.n * sizeof(*r->cells));
        for (int i = 0; i < lines.n; i++) r->cells[i] = vtext(ar_strdup(out->arena, lines.v[i]));
        return r;
    }
    r->ncols = op->scope.n;
    r->cols = ar_alloc(out->arena, (size_t)r->ncols * sizeof(*r->cols));
    for (int i = 0; i < r->ncols; i++) r->cols[i] = ar_strdup(out->arena, op->scope.slots[i].name);
    op_open(op); vec_t rows = { 0 }; tatu_value_t *row;
    while ((row = op_next(op))) {
        if (rows.n >= 100000) tatu_fail(TE_EXECUTION, "Resultado grande demais; use LIMIT (máximo: 100000 linhas)");
        tatu_value_t *copy = ar_alloc(TMP, (size_t)r->ncols * sizeof(*copy));
        memcpy(copy, row, (size_t)r->ncols * sizeof(*copy)); vec_push(TMP, &rows, copy);
    }
    r->nrows = rows.n; r->rowcount = rows.n;
    r->cells = ar_alloc(out->arena, (size_t)r->nrows * (size_t)r->ncols * sizeof(*r->cells));
    for (int i = 0; i < rows.n; i++) for (int j = 0; j < r->ncols; j++)
        r->cells[(size_t)i * (size_t)r->ncols + (size_t)j] = result_value(out->arena, ((tatu_value_t *)rows.v[i])[j]);
    return r;
}

static void reference_target(storage_t *st, schema_t *schema, column_t *c) {
    schema_t *target = !strcmp(c->ref_table, schema->name) ? schema : &storage_get_table(st, c->ref_table)->schema;
    column_t *tc = &target->cols[schema_index_of(target, c->ref_column)];
    if (!tc->primary_key && !tc->unique) tatu_fail(TE_CATALOG,
        "REFERENCES %s(%s): a coluna precisa ser PRIMARY KEY ou UNIQUE", c->ref_table, c->ref_column);
    if (strcmp(tc->type, c->type)) tatu_fail(TE_CATALOG,
        "REFERENCES %s(%s): os tipos não batem (%s e %s)", c->ref_table, c->ref_column, c->type, tc->type);
}

static tatu_result_t *create_table(storage_t *st, stmt_t *s, tatu_output_t *out) {
    if (storage_find_table(st, s->name)) {
        if (s->flag) return message(out, ar_printf(TMP, "A tabela '%s' já existe; nada a fazer", s->name), 0);
        tatu_fail(TE_CATALOG, "A tabela '%s' já existe", s->name);
    }
    if (s->ncols < 1 || s->ncols > 256) tatu_fail(TE_CATALOG, "Uma tabela precisa ter de 1 a 256 colunas");
    schema_t schema = { .name = (char *)s->name, .ncols = s->ncols };
    schema.cols = ar_alloc(TMP, (size_t)s->ncols * sizeof(*schema.cols)); int pks = 0;
    for (int i = 0; i < s->ncols; i++) {
        coldef_t *d = &s->cols[i]; column_t *c = &schema.cols[i];
        for (int j = 0; j < i; j++) if (!strcmp(schema.cols[j].name, d->name))
            tatu_fail(TE_CATALOG, "A coluna '%s' foi declarada duas vezes", d->name);
        c->name = (char *)d->name; c->type = d->type; c->primary_key = d->primary_key;
        c->not_null = d->not_null || d->primary_key; c->unique = d->unique && !d->primary_key;
        c->def = d->def ? coerce(evaluate_constant(d->def), d->type, d->name) : vnull();
        c->ref_table = (char *)d->ref_table; c->ref_column = (char *)d->ref_column;
        if (c->primary_key) pks++;
    }
    if (pks > 1) tatu_fail(TE_CATALOG, "Só uma coluna pode ser PRIMARY KEY; chaves compostas ficam de exercício");
    for (int i = 0; i < schema.ncols; i++) if (schema.cols[i].ref_table) reference_target(st, &schema, &schema.cols[i]);
    storage_create_table(st, &schema);
    return message(out, ar_printf(TMP, "Tabela '%s' criada", s->name), 0);
}

static void validate_row(storage_t *st, table_t *t, tatu_value_t *row) {
    for (int i = 0; i < t->schema.ncols; i++) {
        column_t *c = &t->schema.cols[i]; row[i] = coerce(row[i], c->type, c->name);
        if (row[i].type == TV_NULL && c->not_null) tatu_fail(TE_CONSTRAINT,
            "NOT NULL violada: %s.%s não aceita NULL", t->schema.name, c->name);
    }
    for (int i = 0; i < t->schema.ncols; i++) {
        column_t *c = &t->schema.cols[i]; if (!c->ref_table || row[i].type == TV_NULL) continue;
        table_t *target = storage_get_table(st, c->ref_table); index_t *ix = table_index_for(target, c->ref_column);
        int n; index_lookup(ix, row[i], &n);
        if (!n) tatu_fail(TE_CONSTRAINT, "REFERENCES violada: não existe %s.%s = %s",
                          c->ref_table, c->ref_column, sql_literal(row[i]));
    }
}

static void not_referenced(storage_t *st, table_t *t, const tatu_value_t *old, const tatu_value_t *new_row) {
    for (int ti = 0; ti < st->ntables; ti++) {
        table_t *other = st->tables[ti];
        for (int ci = 0; ci < other->schema.ncols; ci++) {
            column_t *c = &other->schema.cols[ci];
            if (!c->ref_table || strcmp(c->ref_table, t->schema.name)) continue;
            int p = schema_index_of(&t->schema, c->ref_column); tatu_value_t value = old[p];
            if (value.type == TV_NULL || (new_row && value_eq(value, new_row[p]))) continue;
            for (int ri = 0; ri < other->heap_n; ri++) if (other->heap[ri].row && value_eq(other->heap[ri].row[ci], value))
                tatu_fail(TE_CONSTRAINT, "REFERENCES violada: %s.%s ainda aponta para %s.%s = %s",
                          other->schema.name, c->name, t->schema.name, c->ref_column, sql_literal(value));
        }
    }
}

static tatu_result_t *insert_rows(storage_t *st, stmt_t *s, tatu_output_t *out) {
    table_t *t = storage_get_table(st, s->name); int count = s->nnames < 0 ? t->schema.ncols : s->nnames;
    int *positions = ar_alloc(TMP, (size_t)count * sizeof(*positions));
    for (int i = 0; i < count; i++) {
        positions[i] = s->nnames < 0 ? i : schema_index_of(&t->schema, s->names[i]);
        for (int j = 0; j < i; j++) if (positions[j] == positions[i]) tatu_fail(TE_EXECUTION, "Há colunas repetidas na lista do INSERT");
    }
    for (int ri = 0; ri < s->nrows; ri++) {
        if (s->rowlen[ri] != count) tatu_fail(TE_EXECUTION, "O INSERT tem %d coluna(s), mas %d valor(es) nesta linha", count, s->rowlen[ri]);
        tatu_value_t *row = ar_alloc(TMP, (size_t)t->schema.ncols * sizeof(*row));
        for (int i = 0; i < t->schema.ncols; i++) row[i] = t->schema.cols[i].def;
        for (int i = 0; i < count; i++) row[positions[i]] = evaluate_constant(s->rows[ri][i]);
        column_t *pk = schema_primary_key(&t->schema);
        if (pk && !strcmp(pk->type, "INTEGER")) {
            int i = schema_index_of(&t->schema, pk->name); if (row[i].type == TV_NULL) row[i] = vint(table_next_key(t));
        }
        validate_row(st, t, row); table_insert(t, &st->journal, row);
    }
    return message(out, ar_printf(TMP, "%d linha(s) inserida(s)", s->nrows), s->nrows);
}

static tatu_result_t *change_rows(storage_t *st, stmt_t *s, tatu_output_t *out) {
    table_t *t = storage_get_table(st, s->name); bool update = s->kind == S_UPDATE;
    int *positions = NULL; cexpr_t **exprs = NULL;
    if (update) {
        scope_t scope = table_scope(t, t->schema.name);
        positions = ar_alloc(TMP, (size_t)s->nnames * sizeof(*positions));
        exprs = ar_alloc(TMP, (size_t)s->nnames * sizeof(*exprs));
        for (int i = 0; i < s->nnames; i++) {
            positions[i] = schema_index_of(&t->schema, s->names[i]); exprs[i] = compile_expr(s->exprs[i], &scope);
        }
    }
    int count; heap_slot_t *matches = find_rows(t, s->where, &count);
    for (int ri = 0; ri < count; ri++) {
        if (!update) { not_referenced(st, t, matches[ri].row, NULL); table_delete(t, &st->journal, matches[ri].rowid); continue; }
        tatu_value_t *row = ar_alloc(TMP, (size_t)t->schema.ncols * sizeof(*row));
        memcpy(row, matches[ri].row, (size_t)t->schema.ncols * sizeof(*row));
        for (int i = 0; i < s->nnames; i++) row[positions[i]] = eval(exprs[i], matches[ri].row);
        validate_row(st, t, row); not_referenced(st, t, matches[ri].row, row); table_update(t, &st->journal, matches[ri].rowid, row);
    }
    return message(out, ar_printf(TMP, "%d linha(s) %s", count, update ? "atualizada(s)" : "apagada(s)"), count);
}

static tatu_result_t *execute_stmt(tatu_db_t *db, stmt_t *s, tatu_output_t *out) {
    storage_t *st = &db->storage;
    switch (s->kind) {
    case S_SELECT: case S_EXPLAIN: return select_result(st, s, out);
    case S_CREATE_TABLE: return create_table(st, s, out);
    case S_INSERT: return insert_rows(st, s, out);
    case S_UPDATE: case S_DELETE: return change_rows(st, s, out);
    case S_CREATE_INDEX: {
        if (storage_find_index(st, s->name)) tatu_fail(TE_CATALOG, "Já existe um índice chamado '%s'", s->name);
        table_t *t = storage_get_table(st, s->table); table_create_index(t, &st->journal, s->name, s->column, s->flag);
        return message(out, ar_printf(TMP, "Índice '%s' criado em %s(%s)", s->name, s->table, s->column), 0);
    }
    case S_DROP_TABLE:
        if (!storage_find_table(st, s->name)) {
            if (s->flag) return message(out, ar_printf(TMP, "A tabela '%s' não existe; nada a fazer", s->name), 0);
            tatu_fail(TE_CATALOG, "A tabela '%s' não existe", s->name);
        }
        for (int i = 0; i < st->ntables; i++) {
            table_t *t = st->tables[i]; if (!strcmp(t->schema.name, s->name)) continue;
            for (int j = 0; j < t->schema.ncols; j++) if (t->schema.cols[j].ref_table && !strcmp(t->schema.cols[j].ref_table, s->name))
                tatu_fail(TE_CONSTRAINT, "Não dá para apagar '%s': %s.%s aponta para ela", s->name, t->schema.name, t->schema.cols[j].name);
        }
        storage_drop_table(st, s->name); return message(out, ar_printf(TMP, "Tabela '%s' apagada", s->name), 0);
    case S_BEGIN:
        if (db->in_transaction) tatu_fail(TE_TRANSACTION, "Já existe uma transação em andamento; use COMMIT ou ROLLBACK");
        { tatu_result_t *r = message(out, "Transação iniciada; mudanças aguardam COMMIT", 0);
          db->in_transaction = true; return r; }
    case S_COMMIT: {
        if (!db->in_transaction) tatu_fail(TE_TRANSACTION, "Não há transação para confirmar; use BEGIN antes");
        int changes = st->journal.n;
        tatu_result_t *r = message(out, ar_printf(TMP, "Transação confirmada (%d mudança(s) gravada(s))", changes), 0);
        storage_save(st); journal_clear(st); db->in_transaction = false; return r;
    }
    case S_ROLLBACK: {
        if (!db->in_transaction) tatu_fail(TE_TRANSACTION, "Não há transação para desfazer; use BEGIN antes");
        int changes = st->journal.n;
        tatu_result_t *r = message(out, ar_printf(TMP, "Transação desfeita (%d mudança(s) descartada(s))", changes), 0);
        journal_rollback_to(st, 0); db->in_transaction = false; return r;
    }
    default: tatu_fail(TE_EXECUTION, "Comando SQL não suportado");
    }
}

typedef struct { const tatu_value_t *values; int n; } binding_t;
static void bind_node(node_t *n, void *arg) {
    if (n->kind != N_PARAM) return;
    binding_t *b = arg;
    if (n->index < 0 || n->index >= b->n) tatu_fail(TE_EXECUTION, "Faltou o valor do parâmetro %d", n->index + 1);
    n->kind = N_LITERAL; n->value = b->values[n->index];
    if (n->value.type > TV_BOOL || (n->value.type == TV_TEXT && !n->value.s)) tatu_fail(TE_TYPE, "Valor de parâmetro inválido");
    if (n->value.type == TV_REAL &&
        (n->value.r != n->value.r || n->value.r > 1.7976931348623157e308 || n->value.r < -1.7976931348623157e308))
        tatu_fail(TE_TYPE, "Parâmetro REAL precisa ser finito; NaN e infinito não são suportados");
}

static bool run(tatu_db_t *db, const char *sql, const tatu_value_t *params, int nparams,
                bool single, tatu_output_t *out, tatu_error_t *err) {
    tatu_error_t fallback; if (!err) err = &fallback;
    memset(err, 0, sizeof(*err)); err->pos = -1;
    if (!out) { error_set(err, "O resultado precisa de uma saída válida"); return false; }
    memset(out, 0, sizeof(*out)); out->arena = kcalloc(1, sizeof(arena_t));
    if (!out->arena) { error_set(err, "Memória insuficiente para o resultado"); return false; }
    tatu_lock(); arena_t arena = { 0 }; ctx_t ctx = { .err = err, .sql = sql, .arena = &arena }, *saved = tatu_ctx;
    tatu_ctx = &ctx; volatile int mark = 0; volatile bool executing = false, persisting = false;
    if (__builtin_setjmp(ctx.jb)) {
        if (executing && registered(db)) {
            journal_rollback_to(&db->storage, persisting ? 0 : mark);
            if (persisting) db->in_transaction = false;
        }
        ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return false;
    }
    if (!registered(db)) tatu_fail(TE_GENERIC, "O banco está fechado");
    if (!sql) tatu_fail(TE_GENERIC, "O comando SQL está vazio");
    int count, needed; stmt_t **stmts = parse_script(sql, &count, &needed);
    if (single && count != 1) tatu_fail(TE_EXECUTION, "execute() roda um comando por vez; para vários, use tatu_run()");
    if (needed != nparams || nparams < 0 || (nparams && !params))
        tatu_fail(TE_EXECUTION, "O SQL tem %d parâmetro(s), mas recebeu %d valor(es)", needed, nparams);
    binding_t binding = { .values = params, .n = nparams };
    for (int i = 0; i < count; i++) stmt_walk(stmts[i], bind_node, &binding);
    for (int i = 0; i < count; i++) {
        mark = journal_mark(&db->storage.journal); executing = true;
        persisting = stmts[i]->kind == S_COMMIT;
        uint64_t start = timer_ms(); tatu_result_t *r = execute_stmt(db, stmts[i], out);
        if (!db->in_transaction && db->storage.journal.n) {
            persisting = true; storage_save(&db->storage); journal_clear(&db->storage);
        }
        r->micros = (timer_ms() - start) * 1000;
        if (out->last) out->last->next = r; else out->first = r;
        out->last = r; out->count++; executing = false; persisting = false;
    }
    ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return true;
}

bool tatu_run(tatu_db_t *db, const char *sql, tatu_output_t *out, tatu_error_t *err) {
    return run(db, sql, NULL, 0, false, out, err);
}

bool tatu_execute(tatu_db_t *db, const char *sql, const tatu_value_t *params, int nparams,
                  tatu_output_t *out, tatu_error_t *err) {
    return run(db, sql, params, nparams, true, out, err);
}

int tatu_tables(tatu_db_t *db, const char **names, int max) {
    tatu_lock();
    if (!registered(db)) { tatu_unlock(); return 0; }
    int count = db->storage.ntables, n = count < max ? count : max;
    if (n < 0 || !names) n = 0;
    int filled = 0;
    for (int i = 0; i < count && n; i++) {
        const char *name = db->storage.tables[i]->schema.name;
        int at = 0;
        while (at < filled && strcmp(names[at], name) < 0) at++;
        if (at >= n) continue;
        int end = filled < n ? filled++ : n - 1;
        while (end > at) { names[end] = names[end - 1]; end--; }
        names[at] = name;
    }
    tatu_unlock(); return count;
}

bool tatu_table_info(tatu_db_t *db, const char *name, int *ncols, int *nrows, int *nindexes) {
    tatu_lock(); table_t *t = registered(db) ? storage_find_table(&db->storage, name) : NULL;
    if (t) { if (ncols) *ncols = t->schema.ncols; if (nrows) *nrows = t->live; if (nindexes) *nindexes = t->nindexes; }
    tatu_unlock(); return t != NULL;
}

bool tatu_column_info(tatu_db_t *db, const char *name, int i, tatu_column_info_t *out) {
    tatu_lock(); table_t *t = registered(db) ? storage_find_table(&db->storage, name) : NULL;
    bool ok = t && out && i >= 0 && i < t->schema.ncols;
    if (ok) {
        column_t *c = &t->schema.cols[i];
        *out = (tatu_column_info_t){ c->name, c->type, c->primary_key, c->not_null, c->unique, c->def, c->ref_table, c->ref_column };
    }
    tatu_unlock(); return ok;
}

bool tatu_index_info(tatu_db_t *db, const char *name, int i, tatu_index_info_t *out) {
    tatu_lock(); table_t *t = registered(db) ? storage_find_table(&db->storage, name) : NULL;
    bool ok = t && out && i >= 0 && i < t->nindexes;
    if (ok) { index_t *ix = t->indexes[i]; *out = (tatu_index_info_t){ ix->name, ix->column, ix->unique, ix->automatic }; }
    tatu_unlock(); return ok;
}

bool tatu_schema_sql(tatu_db_t *db, const char *name, char *buf, size_t size) {
    if (!buf || !size) return false;
    buf[0] = 0;
    tatu_lock(); arena_t arena = { 0 }; tatu_error_t err; ctx_t ctx = { .err = &err, .arena = &arena }, *saved = tatu_ctx;
    tatu_ctx = &ctx;
    if (__builtin_setjmp(ctx.jb)) { ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return false; }
    table_t *t = registered(db) ? storage_find_table(&db->storage, name) : NULL;
    if (!t) { tatu_ctx = saved; tatu_unlock(); return false; }
    sb_t b; sb_init(&b, &arena); schema_sql(&t->schema, &b);
    for (int i = 0; i < t->nindexes; i++) {
        index_t *ix = t->indexes[i]; if (ix->automatic) continue;
        sb_add(&b, ix->unique ? "\nCREATE UNIQUE INDEX \"" : "\nCREATE INDEX \"");
        for (const char *p = ix->name; *p; p++) { if (*p == '"') sb_addc(&b, '"'); sb_addc(&b, *p); }
        sb_add(&b, "\" ON \"");
        for (const char *p = t->schema.name; *p; p++) { if (*p == '"') sb_addc(&b, '"'); sb_addc(&b, *p); }
        sb_add(&b, "\"(\"");
        for (const char *p = ix->column; *p; p++) { if (*p == '"') sb_addc(&b, '"'); sb_addc(&b, *p); }
        sb_add(&b, "\");");
    }
    bool fits = b.len < size; strlcpy(buf, b.buf, size); ar_free(&arena); tatu_ctx = saved; tatu_unlock(); return fits;
}
