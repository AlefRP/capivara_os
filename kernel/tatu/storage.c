/* Heap, índices hash e diário de desfazer; arquivo JSON compatível com o Tatu. */
#include "tatu/internal.h"
#include "fs/vfs.h"
#include "dev/rtc.h"

static void *alloc(size_t n) {
    void *p = kcalloc(1, n ? n : 1);
    if (!p) tatu_fail(TE_GENERIC, "Memória insuficiente no armazenamento");
    return p;
}

static void *resize(void *old, size_t n) {
    void *p = krealloc(old, n);
    if (!p) tatu_fail(TE_GENERIC, "Memória insuficiente no armazenamento");
    return p;
}

static char *dup(const char *s) {
    char *p = alloc(strlen(s) + 1); strcpy(p, s); return p;
}

static tatu_value_t own(tatu_value_t v) { if (v.type == TV_TEXT) v.s = dup(v.s); return v; }
static void value_free(tatu_value_t v) { if (v.type == TV_TEXT) kfree((void *)v.s); }

static tatu_value_t *row_copy(const table_t *t, const tatu_value_t *row) {
    tatu_value_t *copy = alloc((size_t)t->schema.ncols * sizeof(*copy));
    ctx_t *saved = tatu_ctx, ctx = { .err = saved->err, .sql = saved->sql, .arena = saved->arena };
    tatu_ctx = &ctx;
    if (__builtin_setjmp(ctx.jb)) {
        for (int i = 0; i < t->schema.ncols; i++) value_free(copy[i]);
        kfree(copy); tatu_ctx = saved; __builtin_longjmp(saved->jb, 1);
    }
    for (int i = 0; i < t->schema.ncols; i++) copy[i] = own(row[i]);
    tatu_ctx = saved;
    return copy;
}

static void row_free(const table_t *t, tatu_value_t *row) {
    if (!row) return;
    for (int i = 0; i < t->schema.ncols; i++) value_free(row[i]);
    kfree(row);
}

int schema_find(const schema_t *s, const char *column) {
    for (int i = 0; i < s->ncols; i++) if (!strcmp(s->cols[i].name, column)) return i;
    return -1;
}

int schema_index_of(const schema_t *s, const char *column) {
    int i = schema_find(s, column);
    if (i < 0) tatu_fail(TE_CATALOG, "A tabela '%s' não tem a coluna '%s'", s->name, column);
    return i;
}

column_t *schema_primary_key(const schema_t *s) {
    for (int i = 0; i < s->ncols; i++) if (s->cols[i].primary_key) return &s->cols[i];
    return NULL;
}

static void identifier(sb_t *b, const char *s) {
    /* Aspas também permitem reabrir esquemas com espaços ou palavras-chave. */
    sb_addc(b, '"');
    for (; *s; s++) { if (*s == '"') sb_addc(b, '"'); sb_addc(b, *s); }
    sb_addc(b, '"');
}

void schema_sql(const schema_t *s, sb_t *b) {
    sb_add(b, "CREATE TABLE "); identifier(b, s->name); sb_add(b, " (\n");
    for (int i = 0; i < s->ncols; i++) {
        column_t *c = &s->cols[i];
        sb_add(b, "  "); identifier(b, c->name); sb_addc(b, ' '); sb_add(b, c->type);
        if (c->primary_key) sb_add(b, " PRIMARY KEY");
        else if (c->not_null) sb_add(b, " NOT NULL");
        if (c->unique) sb_add(b, " UNIQUE");
        if (c->def.type != TV_NULL) { sb_add(b, " DEFAULT "); sb_add(b, sql_literal(c->def)); }
        if (c->ref_table) {
            sb_add(b, " REFERENCES "); identifier(b, c->ref_table); sb_addc(b, '(');
            identifier(b, c->ref_column); sb_addc(b, ')');
        }
        sb_add(b, i + 1 < s->ncols ? ",\n" : "\n");
    }
    sb_add(b, ");");
}

static ientry_t *entry(index_t *ix, tatu_value_t v, bool create) {
    if (v.type == TV_NULL) return NULL;
    unsigned bucket = value_hash(v) % (unsigned)ix->nbuckets;
    for (ientry_t *e = ix->buckets[bucket]; e; e = e->next) if (value_eq(v, e->key)) return e;
    if (!create) return NULL;
    ientry_t *e = alloc(sizeof(*e));
    e->next = ix->buckets[bucket]; ix->buckets[bucket] = e; ix->count++;
    e->key = own(v);
    return e;
}

int64_t *index_lookup(index_t *ix, tatu_value_t v, int *count) {
    ientry_t *e = ix ? entry(ix, v, false) : NULL;
    *count = e ? e->n : 0;
    return e ? e->rowids : NULL;
}

static void index_add(index_t *ix, tatu_value_t value, int64_t rowid) {
    ientry_t *e = entry(ix, value, true);
    if (!e) return;
    for (int i = 0; i < e->n; i++) if (e->rowids[i] == rowid) return;
    if (e->n == e->cap) {
        int cap = e->cap ? e->cap * 2 : 4;
        e->rowids = resize(e->rowids, (size_t)cap * sizeof(*e->rowids)); e->cap = cap;
    }
    e->rowids[e->n++] = rowid;
}

static void index_remove(index_t *ix, tatu_value_t value, int64_t rowid) {
    ientry_t *e = entry(ix, value, false);
    if (!e) return;
    for (int i = 0; i < e->n; i++) if (e->rowids[i] == rowid) {
        memmove(e->rowids + i, e->rowids + i + 1, (size_t)(e->n - i - 1) * sizeof(*e->rowids));
        e->n--; return;
    }
    /* Entradas vazias continuam alocadas: desfazer nunca precisa pedir memória. */
}

static void index_free(index_t *ix) {
    for (int i = 0; i < ix->nbuckets; i++) for (ientry_t *e = ix->buckets[i], *next; e; e = next) {
        next = e->next; value_free(e->key); kfree(e->rowids); kfree(e);
    }
    kfree(ix->buckets); kfree(ix->name); kfree(ix->column); kfree(ix);
}

index_t *table_index_for(table_t *t, const char *column) {
    index_t *best = NULL;
    for (int i = 0; i < t->nindexes; i++) if (!strcmp(t->indexes[i]->column, column)) {
        if (!best || t->indexes[i]->unique) best = t->indexes[i];
    }
    return best;
}

static unsigned rowhash(int64_t id) {
    uint64_t n = (uint64_t)id; n ^= n >> 33; n *= 0xff51afd7ed558ccdULL;
    n ^= n >> 33; return (unsigned)n;
}

static int row_index(table_t *t, int64_t id) {
    if (!t->map_cap) return -1;
    unsigned at = rowhash(id) & (unsigned)(t->map_cap - 1);
    while (t->map[at]) {
        int i = t->map[at] - 1;
        if (t->heap[i].rowid == id) return i;
        at = (at + 1) & (unsigned)(t->map_cap - 1);
    }
    return -1;
}

static void map_add(table_t *t, int i) {
    unsigned at = rowhash(t->heap[i].rowid) & (unsigned)(t->map_cap - 1);
    while (t->map[at]) at = (at + 1) & (unsigned)(t->map_cap - 1);
    t->map[at] = i + 1;
}

static void reserve_row(table_t *t) {
    if (t->heap_n == t->heap_cap) {
        int cap = t->heap_cap ? t->heap_cap * 2 : 16;
        t->heap = resize(t->heap, (size_t)cap * sizeof(*t->heap)); t->heap_cap = cap;
    }
    if (t->heap_n * 2 + 2 >= t->map_cap) {
        int cap = t->map_cap ? t->map_cap * 2 : 32;
        int *map = alloc((size_t)cap * sizeof(*map));
        kfree(t->map); t->map = map; t->map_cap = cap;
        for (int i = 0; i < t->heap_n; i++) map_add(t, i);
    }
}

tatu_value_t *table_row(table_t *t, int64_t rowid) {
    int i = row_index(t, rowid); return i < 0 ? NULL : t->heap[i].row;
}

heap_slot_t *table_scan(table_t *t, int *count) {
    heap_slot_t *out = ar_alloc(TMP, (size_t)(t->live ? t->live : 1) * sizeof(*out));
    int n = 0;
    for (int i = 0; i < t->heap_n; i++) if (t->heap[i].row) out[n++] = t->heap[i];
    *count = n; return out;
}

int64_t table_next_key(table_t *t) {
    column_t *pk = schema_primary_key(&t->schema);
    int pos = pk ? schema_index_of(&t->schema, pk->name) : -1;
    int64_t max = 0;
    if (pos >= 0) for (int i = 0; i < t->heap_n; i++) if (t->heap[i].row) {
        tatu_value_t v = t->heap[i].row[pos]; if (v.type == TV_INT && v.i > max) max = v.i;
    }
    if (max == INT64_MAX) tatu_fail(TE_CONSTRAINT, "A chave automática de '%s' esgotou os inteiros de 64 bits", t->schema.name);
    return max + 1;
}

static void journal_reserve(journal_t *j) {
    if (!j) return;
    if (j->n == j->cap) {
        int cap = j->cap ? j->cap * 2 : 16;
        j->undo = resize(j->undo, (size_t)cap * sizeof(*j->undo)); j->cap = cap;
    }
}

static void record(journal_t *j, undo_t u) {
    if (!j) return;
    journal_reserve(j);
    j->undo[j->n++] = u;
}

static void check_unique(table_t *t, const tatu_value_t *row, int64_t ignore) {
    for (int i = 0; i < t->nindexes; i++) {
        index_t *ix = t->indexes[i]; if (!ix->unique) continue;
        int n; int64_t *ids = index_lookup(ix, row[ix->position], &n);
        for (int j = 0; j < n; j++) if (ids[j] != ignore) tatu_fail(TE_CONSTRAINT,
            "%s violada: já existe %s.%s = %s", t->schema.cols[ix->position].primary_key ? "PRIMARY KEY" : "UNIQUE",
            t->schema.name, ix->column, sql_literal(row[ix->position]));
    }
}

static void detach(table_t *t, int i) {
    tatu_value_t *row = t->heap[i].row;
    if (!row) return;
    for (int j = 0; j < t->nindexes; j++) index_remove(t->indexes[j], row[t->indexes[j]->position], t->heap[i].rowid);
    t->heap[i].row = NULL; t->live--;
}

static void attach(table_t *t, int i, tatu_value_t *row) {
    t->heap[i].row = row; t->live++;
    for (int j = 0; j < t->nindexes; j++) index_add(t->indexes[j], row[t->indexes[j]->position], t->heap[i].rowid);
}

int64_t table_insert(table_t *t, journal_t *j, const tatu_value_t *row) {
    check_unique(t, row, 0);
    if (t->next_rowid <= 0 || t->next_rowid == INT64_MAX) tatu_fail(TE_CONSTRAINT, "Identificadores internos esgotados");
    reserve_row(t);
    journal_reserve(j);
    tatu_value_t *copy = row_copy(t, row);
    int64_t id = t->next_rowid++;
    record(j, (undo_t){ .kind = U_INSERT, .table = t, .rowid = id });
    int i = t->heap_n++; t->heap[i] = (heap_slot_t){ .rowid = id }; map_add(t, i);
    attach(t, i, copy);
    return id;
}

void table_update(table_t *t, journal_t *j, int64_t rowid, const tatu_value_t *row) {
    int i = row_index(t, rowid);
    if (i < 0 || !t->heap[i].row) tatu_fail(TE_EXECUTION, "Linha interna não encontrada");
    check_unique(t, row, rowid);
    journal_reserve(j);
    tatu_value_t *copy = row_copy(t, row), *old = t->heap[i].row;
    record(j, (undo_t){ .kind = U_UPDATE, .table = t, .rowid = rowid, .old_row = old });
    detach(t, i); attach(t, i, copy);
    if (!j) row_free(t, old);
}

void table_delete(table_t *t, journal_t *j, int64_t rowid) {
    int i = row_index(t, rowid);
    if (i < 0 || !t->heap[i].row) tatu_fail(TE_EXECUTION, "Linha interna não encontrada");
    tatu_value_t *old = t->heap[i].row;
    record(j, (undo_t){ .kind = U_DELETE, .table = t, .rowid = rowid, .old_row = old });
    detach(t, i);
    if (!j) row_free(t, old);
}

void table_create_index(table_t *t, journal_t *j, const char *name, const char *column, bool unique) {
    int position = schema_index_of(&t->schema, column);
    for (int i = 0; i < t->nindexes; i++) if (!strcmp(t->indexes[i]->name, name))
        tatu_fail(TE_CATALOG, "Já existe o índice '%s'", name);
    t->indexes = resize(t->indexes, (size_t)(t->nindexes + 1) * sizeof(*t->indexes));
    journal_reserve(j);
    index_t *ix = alloc(sizeof(*ix));
    record(j, (undo_t){ .kind = U_CREATE_INDEX, .table = t, .index = ix });
    t->indexes[t->nindexes++] = ix;
    ix->name = dup(name); ix->column = dup(column); ix->position = position; ix->unique = unique;
    ix->buckets = alloc(257 * sizeof(*ix->buckets)); ix->nbuckets = 257;
    for (int i = 0; i < t->heap_n; i++) if (t->heap[i].row) {
        tatu_value_t v = t->heap[i].row[position]; int n;
        index_lookup(ix, v, &n);
        if (unique && n) tatu_fail(TE_CONSTRAINT, "Não dá para criar '%s': %s.%s tem valores repetidos (%s)",
                                 name, t->schema.name, column, sql_literal(v));
        index_add(ix, v, t->heap[i].rowid);
    }
}

static void table_free(table_t *t) {
    for (int i = 0; i < t->heap_n; i++) row_free(t, t->heap[i].row);
    for (int i = 0; i < t->nindexes; i++) index_free(t->indexes[i]);
    for (int i = 0; i < t->schema.ncols; i++) {
        column_t *c = &t->schema.cols[i];
        kfree(c->name); value_free(c->def); kfree(c->ref_table); kfree(c->ref_column);
    }
    kfree(t->schema.cols); kfree(t->schema.name); kfree(t->indexes); kfree(t->heap); kfree(t->map); kfree(t);
}

table_t *storage_find_table(storage_t *s, const char *name) {
    for (int i = 0; i < s->ntables; i++) if (!strcmp(s->tables[i]->schema.name, name)) return s->tables[i];
    return NULL;
}

table_t *storage_get_table(storage_t *s, const char *name) {
    table_t *t = storage_find_table(s, name);
    if (!t) tatu_fail(TE_CATALOG, "A tabela '%s' não existe", name);
    return t;
}

index_t *storage_find_index(storage_t *s, const char *name) {
    for (int i = 0; i < s->ntables; i++) for (int j = 0; j < s->tables[i]->nindexes; j++)
        if (!strcmp(s->tables[i]->indexes[j]->name, name)) return s->tables[i]->indexes[j];
    return NULL;
}

static void add_table(storage_t *s, table_t *t) {
    if (s->ntables == s->cap) {
        int cap = s->cap ? s->cap * 2 : 8;
        s->tables = resize(s->tables, (size_t)cap * sizeof(*s->tables)); s->cap = cap;
    }
    s->tables[s->ntables++] = t;
}

static void remove_table(storage_t *s, table_t *t) {
    for (int i = 0; i < s->ntables; i++) if (s->tables[i] == t) {
        memmove(s->tables + i, s->tables + i + 1, (size_t)(s->ntables - i - 1) * sizeof(*s->tables));
        s->ntables--; return;
    }
}

table_t *storage_create_table(storage_t *s, schema_t *schema) {
    if (storage_find_table(s, schema->name)) tatu_fail(TE_CATALOG, "A tabela '%s' já existe", schema->name);
    if (s->ntables == s->cap) {
        int cap = s->cap ? s->cap * 2 : 8;
        s->tables = resize(s->tables, (size_t)cap * sizeof(*s->tables)); s->cap = cap;
    }
    journal_reserve(&s->journal);
    table_t *t = alloc(sizeof(*t));
    record(&s->journal, (undo_t){ .kind = U_CREATE_TABLE, .table = t }); add_table(s, t);
    t->schema.name = dup(schema->name);
    t->schema.cols = alloc((size_t)schema->ncols * sizeof(*t->schema.cols));
    t->schema.ncols = schema->ncols;
    for (int i = 0; i < schema->ncols; i++) {
        column_t *c = &t->schema.cols[i], *src = &schema->cols[i];
        c->type = src->type; c->primary_key = src->primary_key; c->not_null = src->not_null; c->unique = src->unique;
        c->name = dup(src->name); c->def = own(src->def);
        if (src->ref_table) { c->ref_table = dup(src->ref_table); c->ref_column = dup(src->ref_column); }
    }
    t->next_rowid = 1;
    for (int i = 0; i < t->schema.ncols; i++) {
        column_t *c = &t->schema.cols[i];
        if (!c->primary_key && !c->unique) continue;
        const char *name = ar_printf(TMP, "%s_%s_%s", c->primary_key ? "pk" : "uq", schema->name, c->name);
        table_create_index(t, NULL, name, c->name, true);
        t->indexes[t->nindexes - 1]->automatic = true;
    }
    return t;
}

void storage_drop_table(storage_t *s, const char *name) {
    table_t *t = storage_get_table(s, name);
    record(&s->journal, (undo_t){ .kind = U_DROP_TABLE, .table = t }); remove_table(s, t);
}

int journal_mark(journal_t *j) { return j->n; }

void journal_rollback_to(storage_t *s, int mark) {
    journal_t *j = &s->journal;
    while (j->n > mark) {
        undo_t u = j->undo[--j->n]; table_t *t = u.table;
        int i;
        switch (u.kind) {
        case U_INSERT:
            i = row_index(t, u.rowid);
            if (i >= 0) { tatu_value_t *row = t->heap[i].row; detach(t, i); row_free(t, row); }
            break;
        case U_UPDATE:
            i = row_index(t, u.rowid);
            if (i >= 0) { tatu_value_t *row = t->heap[i].row; detach(t, i); row_free(t, row); attach(t, i, u.old_row); }
            break;
        case U_DELETE: i = row_index(t, u.rowid); if (i >= 0) attach(t, i, u.old_row); break;
        case U_CREATE_INDEX:
            for (i = 0; i < t->nindexes; i++) if (t->indexes[i] == u.index) {
                memmove(t->indexes + i, t->indexes + i + 1, (size_t)(t->nindexes - i - 1) * sizeof(*t->indexes));
                t->nindexes--; break;
            }
            index_free(u.index); break;
        case U_CREATE_TABLE: remove_table(s, t); table_free(t); break;
        case U_DROP_TABLE: add_table(s, t); break;
        }
    }
}

void journal_clear(storage_t *s) {
    journal_t *j = &s->journal;
    for (int i = 0; i < j->n; i++) {
        undo_t *u = &j->undo[i];
        if (u->kind == U_UPDATE || u->kind == U_DELETE) row_free(u->table, u->old_row);
        if (u->kind == U_DROP_TABLE) table_free(u->table);
    }
    j->n = 0;
}

void storage_init(storage_t *s, const char *path) {
    memset(s, 0, sizeof(*s)); if (path) s->path = dup(path);
}

void storage_free(storage_t *s) {
    journal_rollback_to(s, 0);
    for (int i = 0; i < s->ntables; i++) table_free(s->tables[i]);
    kfree(s->tables); kfree(s->journal.undo); kfree(s->path); memset(s, 0, sizeof(*s));
}

static jv_t *js(const char *s) { jv_t *v = jv_new(JV_STRING); v->s = s; return v; }
static jv_t *ji(int64_t i) { jv_t *v = jv_new(JV_INT); v->i = i; return v; }
static jv_t *jb(bool b) { jv_t *v = jv_new(JV_BOOL); v->b = b; return v; }
static jv_t *jvalue(tatu_value_t v) {
    switch (v.type) {
    case TV_INT: return ji(v.i);
    case TV_BOOL: return jb(v.b);
    case TV_TEXT: return js(v.s);
    case TV_REAL: { jv_t *r = jv_new(JV_REAL); r->r = v.r; return r; }
    default: return jv_new(JV_NULL);
    }
}

void storage_save(storage_t *s) {
    if (!s->path) return;
    jv_t *root = jv_new(JV_OBJECT), *tables = jv_new(JV_ARRAY);
    jv_set(root, "formato", js("tatu-db")); jv_set(root, "versao", ji(1)); jv_set(root, "tabelas", tables);
    for (int ti = 0; ti < s->ntables; ti++) {
        table_t *t = s->tables[ti]; jv_t *item = jv_new(JV_OBJECT), *schema = jv_new(JV_OBJECT);
        jv_t *cols = jv_new(JV_ARRAY), *indexes = jv_new(JV_ARRAY), *rows = jv_new(JV_ARRAY);
        jv_set(item, "esquema", schema); jv_set(schema, "nome", js(t->schema.name)); jv_set(schema, "colunas", cols);
        jv_set(item, "proximo_rowid", ji(t->next_rowid)); jv_set(item, "indices", indexes); jv_set(item, "linhas", rows);
        for (int ci = 0; ci < t->schema.ncols; ci++) {
            column_t *c = &t->schema.cols[ci]; jv_t *col = jv_new(JV_OBJECT);
            jv_set(col, "nome", js(c->name)); jv_set(col, "tipo", js(c->type));
            if (c->primary_key) jv_set(col, "chave_primaria", jb(true));
            else if (c->not_null) jv_set(col, "nao_nulo", jb(true));
            if (c->unique) jv_set(col, "unico", jb(true));
            if (c->def.type != TV_NULL) jv_set(col, "padrao", jvalue(c->def));
            if (c->ref_table) {
                jv_t *ref = jv_new(JV_ARRAY); jv_push(ref, js(c->ref_table)); jv_push(ref, js(c->ref_column));
                jv_set(col, "referencia", ref);
            }
            jv_push(cols, col);
        }
        for (int i = 0; i < t->nindexes; i++) {
            index_t *ix = t->indexes[i]; if (ix->automatic) continue;
            jv_t *v = jv_new(JV_OBJECT); jv_set(v, "nome", js(ix->name)); jv_set(v, "coluna", js(ix->column));
            jv_set(v, "unico", jb(ix->unique)); jv_push(indexes, v);
        }
        for (int i = 0; i < t->heap_n; i++) if (t->heap[i].row) {
            jv_t *row = jv_new(JV_ARRAY); jv_push(row, ji(t->heap[i].rowid));
            for (int c = 0; c < t->schema.ncols; c++) jv_push(row, jvalue(t->heap[i].row[c]));
            jv_push(rows, row);
        }
        jv_push(tables, item);
    }
    sb_t text; sb_init(&text, TMP); json_write(&text, root, 0); sb_addc(&text, '\n');
    /* A RAMFS não possui rename que substitua. Preparamos todos os bytes antes,
     * e trocamos o buffer sob o mesmo mutex do VFS: o leitor vê uma versão inteira. */
    uint8_t *bytes = alloc(text.len + 1); memcpy(bytes, text.buf, text.len + 1);
    vnode_t *file = vfs_create(NULL, s->path, VFS_FILE);
    if (!file || file->type != VFS_FILE) { kfree(bytes); tatu_fail(TE_GENERIC, "Não consegui gravar '%s'; confira a pasta", s->path); }
    vfs_lock(); uint8_t *old = file->data; file->data = bytes; file->size = text.len; file->capacity = text.len + 1;
    file->mtime = time_now();
    vfs_unlock(); kfree(old);
}

static jv_t *need(const jv_t *obj, const char *key, int type) {
    jv_t *v = jv_get(obj, key);
    if (!v || v->type != type) tatu_fail(TE_GENERIC, "Arquivo .tatu inválido: campo '%s' ausente ou com tipo errado", key);
    return v;
}

static bool optional_bool(const jv_t *obj, const char *key) {
    jv_t *v = jv_get(obj, key); if (!v) return false;
    if (v->type != JV_BOOL) tatu_fail(TE_GENERIC, "Arquivo .tatu inválido: '%s' precisa ser booleano", key);
    return v->b;
}

static tatu_value_t from_json(const jv_t *v) {
    if (!v || v->type == JV_NULL) return vnull();
    switch (v->type) {
    case JV_BOOL: return vbool(v->b);
    case JV_INT: return vint(v->i);
    case JV_REAL: return vreal(v->r);
    case JV_STRING: return vtext(v->s);
    default: tatu_fail(TE_GENERIC, "Arquivo .tatu inválido: uma célula precisa ser um valor simples");
    }
}

void storage_load(storage_t *s) {
    if (!s->path) return;
    vnode_t *file = vfs_lookup(NULL, s->path);
    if (!file) return;
    if (file->type != VFS_FILE || file->size > 16 * 1024 * 1024)
        tatu_fail(TE_GENERIC, "'%s' não é um arquivo .tatu válido (limite: 16 MiB)", s->path);
    size_t size = (size_t)file->size; char *text = ar_alloc(TMP, size + 1);
    if (vfs_read(file, 0, text, size) != (long)size) tatu_fail(TE_GENERIC, "Não consegui ler '%s'", s->path);
    text[size] = 0; if (strlen(text) != size) tatu_fail(TE_GENERIC, "Arquivo .tatu com byte zero");
    char why[200]; jv_t *root = json_parse(text, why, sizeof(why));
    if (!root) tatu_fail(TE_GENERIC, "JSON inválido em '%s': %s", s->path, why);
    if (strcmp(need(root, "formato", JV_STRING)->s, "tatu-db") || need(root, "versao", JV_INT)->i != 1)
        tatu_fail(TE_GENERIC, "'%s' não usa o formato Tatu DB versão 1", s->path);
    jv_t *tables = need(root, "tabelas", JV_ARRAY);
    for (int ti = 0; ti < tables->n; ti++) {
        jv_t *item = tables->items[ti], *schema = need(item, "esquema", JV_OBJECT), *cols = need(schema, "colunas", JV_ARRAY);
        if (cols->n < 1 || cols->n > 256) tatu_fail(TE_GENERIC, "Arquivo .tatu com quantidade de colunas inválida");
        schema_t def = { .name = (char *)need(schema, "nome", JV_STRING)->s, .ncols = cols->n };
        def.cols = ar_alloc(TMP, (size_t)cols->n * sizeof(*def.cols)); int npk = 0;
        for (int ci = 0; ci < cols->n; ci++) {
            jv_t *col = cols->items[ci]; column_t *c = &def.cols[ci];
            c->name = (char *)need(col, "nome", JV_STRING)->s; c->type = normalize_type(need(col, "tipo", JV_STRING)->s);
            if (!c->type || !*c->name) tatu_fail(TE_GENERIC, "Arquivo .tatu com coluna ou tipo inválido");
            for (int p = 0; p < ci; p++) if (!strcmp(def.cols[p].name, c->name)) tatu_fail(TE_GENERIC, "Coluna repetida no arquivo .tatu");
            c->primary_key = optional_bool(col, "chave_primaria"); c->not_null = c->primary_key || optional_bool(col, "nao_nulo");
            c->unique = optional_bool(col, "unico"); c->def = coerce(from_json(jv_get(col, "padrao")), c->type, c->name);
            if (c->primary_key) npk++;
            jv_t *ref = jv_get(col, "referencia");
            if (ref) {
                if (ref->type != JV_ARRAY || ref->n != 2 || ref->items[0]->type != JV_STRING || ref->items[1]->type != JV_STRING)
                    tatu_fail(TE_GENERIC, "Referência inválida no arquivo .tatu");
                c->ref_table = (char *)ref->items[0]->s; c->ref_column = (char *)ref->items[1]->s;
            }
        }
        if (npk > 1) tatu_fail(TE_GENERIC, "Mais de uma chave primária no arquivo .tatu");
        table_t *t = storage_create_table(s, &def);
        jv_t *rows = need(item, "linhas", JV_ARRAY);
        int64_t highest = 0;
        for (int ri = 0; ri < rows->n; ri++) {
            jv_t *r = rows->items[ri];
            if (r->type != JV_ARRAY || r->n != cols->n + 1 || r->items[0]->type != JV_INT)
                tatu_fail(TE_GENERIC, "Linha inválida no arquivo .tatu");
            int64_t id = r->items[0]->i;
            if (id <= 0 || row_index(t, id) >= 0) tatu_fail(TE_GENERIC, "Identificador repetido ou inválido no arquivo .tatu");
            tatu_value_t *row = ar_alloc(TMP, (size_t)cols->n * sizeof(*row));
            for (int ci = 0; ci < cols->n; ci++) {
                row[ci] = coerce(from_json(r->items[ci + 1]), def.cols[ci].type, def.cols[ci].name);
                if (row[ci].type == TV_NULL && def.cols[ci].not_null) tatu_fail(TE_CONSTRAINT, "NULL em coluna obrigatória no arquivo .tatu");
            }
            t->next_rowid = id; table_insert(t, &s->journal, row);
            if (id > highest) highest = id;
        }
        int64_t next = need(item, "proximo_rowid", JV_INT)->i;
        if (next <= highest || next <= 0) tatu_fail(TE_GENERIC, "Próximo identificador inválido no arquivo .tatu");
        t->next_rowid = next;
        jv_t *indexes = need(item, "indices", JV_ARRAY);
        for (int ii = 0; ii < indexes->n; ii++) {
            jv_t *ix = indexes->items[ii]; const char *name = need(ix, "nome", JV_STRING)->s;
            if (storage_find_index(s, name)) tatu_fail(TE_GENERIC, "Índice repetido no arquivo .tatu: '%s'", name);
            table_create_index(t, &s->journal, name, need(ix, "coluna", JV_STRING)->s, optional_bool(ix, "unico"));
        }
    }
    /* Referências podem apontar para uma tabela que só apareceu depois no JSON. */
    for (int ti = 0; ti < s->ntables; ti++) {
        table_t *t = s->tables[ti];
        for (int ci = 0; ci < t->schema.ncols; ci++) {
            column_t *c = &t->schema.cols[ci]; if (!c->ref_table) continue;
            table_t *target = storage_get_table(s, c->ref_table); int p = schema_index_of(&target->schema, c->ref_column);
            column_t *tc = &target->schema.cols[p];
            if ((!tc->primary_key && !tc->unique) || strcmp(tc->type, c->type))
                tatu_fail(TE_CONSTRAINT, "Destino de REFERENCES inválido no arquivo .tatu");
            index_t *ix = table_index_for(target, c->ref_column);
            for (int ri = 0; ri < t->heap_n; ri++) if (t->heap[ri].row && t->heap[ri].row[ci].type != TV_NULL) {
                int n; index_lookup(ix, t->heap[ri].row[ci], &n);
                if (!n) tatu_fail(TE_CONSTRAINT, "REFERENCES violada no arquivo .tatu: %s.%s", t->schema.name, c->name);
            }
        }
    }
    journal_clear(s);
}
