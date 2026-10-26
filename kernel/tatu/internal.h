/*
 * Peças internas do Tatu DB (o que os arquivos do motor dividem entre si).
 * Quem usa o banco vê só tatu.h.
 */
#pragma once
#include "tatu/tatu.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "mm/heap.h"

/* ==================================================================== */
/* Memória: arena                                                        */
/* ==================================================================== */
/* Tudo o que um comando cria e joga fora (tokens, árvore, plano, linhas
 * intermediárias, textos calculados) sai de uma arena: pedaços grandes de
 * memória, liberados de uma vez no fim. Assim um erro no meio do comando
 * (que "pula" para fora com longjmp) não deixa nada vazando. */
typedef struct arena_chunk {
    struct arena_chunk *next;
    size_t used, size;
    uint8_t data[];
} arena_chunk_t;

typedef struct arena { arena_chunk_t *head; } arena_t;

void *ar_alloc(arena_t *a, size_t n);                  /* zerado, alinhado em 16 */
char *ar_strdup(arena_t *a, const char *s);
char *ar_strndup(arena_t *a, const char *s, size_t n);
char *ar_printf(arena_t *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void  ar_free(arena_t *a);

/* texto que cresce, dentro de uma arena */
typedef struct { arena_t *a; char *buf; size_t len, cap; } sb_t;
void  sb_init(sb_t *b, arena_t *a);
void  sb_addn(sb_t *b, const char *s, size_t n);
void  sb_add(sb_t *b, const char *s);
void  sb_addc(sb_t *b, char c);
void  sb_printf(sb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *sb_str(sb_t *b);

/* vetor de ponteiros que cresce, dentro de uma arena */
typedef struct { void **v; int n, cap; } vec_t;
void vec_push(arena_t *a, vec_t *v, void *p);

/* ==================================================================== */
/* Erros: o "raise" do C                                                 */
/* ==================================================================== */
/* O Python levanta exceções; aqui, tatu_fail preenche o erro e pula de volta
 * para quem chamou tatu_run (com __builtin_setjmp/__builtin_longjmp do GCC),
 * atravessando quantas funções for preciso. */
typedef struct {
    void *jb[5];                /* o "ponto de retorno" do __builtin_setjmp */
    tatu_error_t *err;
    const char *sql;            /* o texto em análise (para a setinha) */
    arena_t *arena;             /* temporários do comando atual */
    int depth;                  /* profundidade do parser (contra recursão infinita) */
} ctx_t;

extern ctx_t *tatu_ctx;

/* Um comando por vez no sistema todo (o tatu_ctx é um só). */
void tatu_lock(void);
void tatu_unlock(void);

__attribute__((noreturn, format(printf, 2, 3))) void tatu_fail(tatu_errkind_t kind, const char *fmt, ...);
__attribute__((noreturn, format(printf, 3, 4))) void tatu_fail_at(tatu_errkind_t kind, int pos, const char *fmt, ...);
void tatu_render_error(tatu_error_t *e, const char *sql);
#define TMP (tatu_ctx->arena)    /* a arena do comando em andamento */

/* ==================================================================== */
/* Valores (value.c)                                                     */
/* ==================================================================== */
enum { FAM_NULL, FAM_NUMBER, FAM_TEXT, FAM_BOOL };

static inline tatu_value_t vnull(void) { return (tatu_value_t){ .type = TV_NULL }; }
static inline tatu_value_t vint(int64_t i) { return (tatu_value_t){ .type = TV_INT, .i = i }; }
static inline tatu_value_t vreal(double r) { return (tatu_value_t){ .type = TV_REAL, .r = r }; }
static inline tatu_value_t vbool(bool b) { return (tatu_value_t){ .type = TV_BOOL, .b = b }; }
static inline tatu_value_t vtext(const char *s) { return (tatu_value_t){ .type = TV_TEXT, .s = s }; }

const char *type_of(tatu_value_t v);                    /* "INTEGER", "TEXT", "NULL"... */
int         family(tatu_value_t v);
int         type_family(const char *type);
const char *normalize_type(const char *name);           /* "varchar" → "TEXT"; NULL se não existe */
double      as_double(tatu_value_t v);
bool        value_eq(tatu_value_t a, tatu_value_t b);   /* o == do Python: 1 == 1.0 */
uint32_t    value_hash(tatu_value_t v);                 /* igual para valores iguais */
int         value_cmp(tatu_value_t a, tatu_value_t b);  /* mesma família: -1, 0 ou 1 */
tatu_value_t coerce(tatu_value_t v, const char *type, const char *column);
const char *sql_literal(tatu_value_t v);                /* na arena do comando */
void        real_repr(double x, char *out);             /* como o repr() do Python: 0.1, 3.0, 1e+16 */
bool        parse_real(const char *s, size_t n, double *out);
int         utf8_len(const char *s);                    /* quantos caracteres */
int         utf8_len_n(const char *s, int bytes);

/* ==================================================================== */
/* Tokens (lexer.c)                                                      */
/* ==================================================================== */
enum { TK_KEYWORD, TK_IDENT, TK_NUMBER, TK_STRING, TK_SYMBOL, TK_PARAM, TK_EOF };

typedef struct {
    uint8_t kind;
    const char *text;           /* palavra normalizada, texto sem aspas, símbolo */
    tatu_value_t num;           /* TK_NUMBER */
    int pos, len;               /* onde está no SQL e quantos bytes ocupa */
    bool quoted;                /* identificador entre aspas duplas */
} token_t;

/* Quebra o SQL em tokens (o último é TK_EOF). Com "lenient", aspas sem
 * fechamento não são erro: o tokenizer para ali (usado por tatu_is_complete). */
token_t *tokenize(const char *sql, int *count, bool lenient, bool *unterminated);

/* ==================================================================== */
/* Árvore sintática (ast.c)                                              */
/* ==================================================================== */
enum {
    N_LITERAL, N_PARAM, N_COLUMN, N_STAR, N_UNARY, N_BINARY, N_ISNULL, N_INLIST, N_BETWEEN, N_LIKE, N_CALL,
};

/* operadores binários e unários */
enum { OP_OR, OP_AND, OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_ADD, OP_SUB, OP_CONCAT,
       OP_MUL, OP_DIV, OP_MOD, OP_NOT, OP_NEG };
extern const char *const op_text[];

typedef struct node node_t;
struct node {
    uint8_t kind, op;
    bool negated, distinct, star;
    tatu_value_t value;         /* N_LITERAL */
    int index;                  /* N_PARAM */
    const char *name;           /* N_COLUMN, N_CALL (em maiúsculas) */
    const char *table;          /* N_COLUMN, N_STAR (tabela.*) */
    node_t *a, *b, *c;          /* operando, esquerda/direita, low/high, padrão do LIKE */
    node_t **items;             /* N_INLIST: itens; N_CALL: argumentos */
    int nitems;
};

typedef struct { node_t *expr; const char *alias; } select_item_t;
typedef struct { node_t *expr; bool desc; } order_item_t;
typedef struct { const char *name, *alias; } table_ref_t;
enum { J_INNER, J_LEFT, J_CROSS };
typedef struct { int kind; table_ref_t table; node_t *on; } join_t;

typedef struct {
    select_item_t *items; int nitems;
    table_ref_t *from;                      /* NULL: SELECT sem FROM */
    join_t *joins; int njoins;
    node_t *where;
    node_t **group_by; int ngroup;
    node_t *having;
    order_item_t *order_by; int norder;
    node_t *limit, *offset;
    bool distinct;
} select_t;

typedef struct {
    const char *name, *type;
    bool primary_key, not_null, unique;
    node_t *def;
    const char *ref_table, *ref_column;
} coldef_t;

enum { S_SELECT, S_INSERT, S_UPDATE, S_DELETE, S_CREATE_TABLE, S_DROP_TABLE, S_CREATE_INDEX,
       S_BEGIN, S_COMMIT, S_ROLLBACK, S_EXPLAIN };

typedef struct {
    int kind;
    int pos;                                /* onde o comando começa */
    const char *name;                       /* tabela (ou índice, no CREATE INDEX) */
    const char *table, *column;             /* CREATE INDEX */
    bool flag;                              /* IF NOT EXISTS, IF EXISTS, UNIQUE */
    coldef_t *cols; int ncols;              /* CREATE TABLE */
    const char **names; int nnames;         /* INSERT (colunas; nnames < 0: todas) e UPDATE (SET) */
    node_t ***rows; int *rowlen; int nrows; /* INSERT: VALUES */
    node_t **exprs;                         /* UPDATE: um valor por coluna do SET */
    node_t *where;                          /* UPDATE e DELETE */
    select_t *select;                       /* SELECT e EXPLAIN */
} stmt_t;

node_t *new_node(int kind);
bool    node_eq(const node_t *a, const node_t *b);
void    node_str(sb_t *b, const node_t *n);
const char *node_text(const node_t *n);     /* na arena do comando */
/* chama fn(n, ctx) em cada nó da expressão (pré-ordem) */
void    node_walk(node_t *n, void (*fn)(node_t *n, void *ctx), void *ctx);
void    stmt_walk(stmt_t *s, void (*fn)(node_t *n, void *ctx), void *ctx);

/* ==================================================================== */
/* Parser (parser.c)                                                     */
/* ==================================================================== */
stmt_t **parse_script(const char *sql, int *count, int *nparams);

/* ==================================================================== */
/* Expressões (expr.c)                                                   */
/* ==================================================================== */
typedef struct { const char *table, *name; const node_t *expr; } slot_t;
typedef struct { slot_t *slots; int n; bool aggregated; } scope_t;

scope_t scope_concat(scope_t a, scope_t b);
int     scope_find_expr(const scope_t *s, const node_t *e);
int     scope_resolve(const scope_t *s, const node_t *column);          /* reclama se não achar */
int     scope_try_resolve(const scope_t *s, const node_t *column);      /* -1 se não achar */
int    *scope_star(const scope_t *s, const char *table, int *count);

typedef struct cexpr cexpr_t;               /* expressão "compilada": nomes viraram posições */
cexpr_t *compile_expr(const node_t *e, const scope_t *s);
tatu_value_t eval(const cexpr_t *e, const tatu_value_t *row);
tatu_value_t evaluate_constant(const node_t *e);
int     truth(tatu_value_t v);              /* 1 verdadeiro, 0 falso, -1 NULL ("não sei") */
bool    is_aggregate(const node_t *n);
tatu_value_t aggregate(const node_t *call, tatu_value_t *values, int n);

/* ==================================================================== */
/* Armazenamento (storage.c)                                             */
/* ==================================================================== */
typedef struct {
    char *name;
    const char *type;
    bool primary_key, not_null, unique;
    tatu_value_t def;                       /* texto do DEFAULT é dono da memória */
    char *ref_table, *ref_column;
} column_t;

typedef struct {
    char *name;
    column_t *cols;
    int ncols;
} schema_t;

typedef struct ientry {
    tatu_value_t key;
    int64_t *rowids;
    int n, cap;
    struct ientry *next;
} ientry_t;

typedef struct {
    char *name, *column;
    int position;
    bool unique, automatic;
    ientry_t **buckets;
    int nbuckets, count;
} index_t;

typedef struct { int64_t rowid; tatu_value_t *row; } heap_slot_t;

typedef struct table {
    schema_t schema;
    heap_slot_t *heap;                      /* ordem de inserção (como o dict do Python) */
    int heap_n, heap_cap, live;
    int *map;                               /* rowid → posição no heap (hash aberto) */
    int map_cap;
    int64_t next_rowid;
    index_t **indexes;
    int nindexes;
} table_t;

enum { U_INSERT, U_UPDATE, U_DELETE, U_CREATE_INDEX, U_CREATE_TABLE, U_DROP_TABLE };
typedef struct {
    int kind;
    table_t *table;
    int64_t rowid;
    tatu_value_t *old_row;                  /* UPDATE e DELETE: a linha antiga (dona) */
    index_t *index;
} undo_t;

typedef struct {
    undo_t *undo;
    int n, cap;
} journal_t;

typedef struct {
    char *path;
    table_t **tables;
    int ntables, cap;
    journal_t journal;
} storage_t;

void     storage_init(storage_t *s, const char *path);
void     storage_load(storage_t *s);
void     storage_save(storage_t *s);
void     storage_free(storage_t *s);
table_t *storage_get_table(storage_t *s, const char *name);              /* reclama se não existir */
table_t *storage_find_table(storage_t *s, const char *name);             /* NULL se não existir */
table_t *storage_create_table(storage_t *s, schema_t *schema);           /* copia o esquema */
void     storage_drop_table(storage_t *s, const char *name);
index_t *storage_find_index(storage_t *s, const char *name);

int      schema_index_of(const schema_t *s, const char *column);        /* reclama se não existir */
int      schema_find(const schema_t *s, const char *column);             /* -1 */
column_t *schema_primary_key(const schema_t *s);
void     schema_sql(const schema_t *s, sb_t *b);

index_t *table_index_for(table_t *t, const char *column);
tatu_value_t *table_row(table_t *t, int64_t rowid);                       /* NULL se não existir */
int64_t  table_next_key(table_t *t);
/* foto das linhas: (rowid, linha) em ordem, numa arena */
heap_slot_t *table_scan(table_t *t, int *count);
int64_t *index_lookup(index_t *ix, tatu_value_t v, int *count);          /* rowids (do índice) */
int64_t  table_insert(table_t *t, journal_t *j, const tatu_value_t *row);
void     table_update(table_t *t, journal_t *j, int64_t rowid, const tatu_value_t *row);
void     table_delete(table_t *t, journal_t *j, int64_t rowid);
void     table_create_index(table_t *t, journal_t *j, const char *name, const char *column, bool unique);

int      journal_mark(journal_t *j);
void     journal_rollback_to(storage_t *s, int mark);
void     journal_clear(storage_t *s);

/* ==================================================================== */
/* Planejador e operadores (plan.c)                                      */
/* ==================================================================== */
typedef struct op op_t;
struct op {
    int kind;
    scope_t scope;
    op_t *child[2];
    int nchild;
    /* o que cada operador precisa (veja plan.c) */
    table_t *table;
    const char *ref_name;
    index_t *index;
    tatu_value_t value;
    const node_t *cond;
    cexpr_t *pred;
    int join_kind;
    const node_t **group_by; int ngroup;
    const node_t **calls; int ncalls;
    cexpr_t **fns; int nfns;
    order_item_t *items; int nitems;
    const char **names;
    int64_t limit, offset;
    bool has_limit;
    /* estado da iteração */
    tatu_value_t **rows; int nrows, pos;
    heap_slot_t *snap; int nsnap;
    tatu_value_t *left_row; bool matched; int rpos;
    void *seen;
    int64_t produced;
    bool done;
};

op_t *plan_select(const select_t *sel, storage_t *st);
void  op_open(op_t *op);
tatu_value_t *op_next(op_t *op);                                           /* NULL: acabou */
void  op_explain(op_t *op, vec_t *lines);                                  /* as linhas do EXPLAIN */
scope_t table_scope(table_t *t, const char *ref_name);
/* linhas (rowid, linha) que passam no WHERE: para UPDATE e DELETE */
heap_slot_t *find_rows(table_t *t, const node_t *where, int *count);

/* ==================================================================== */
/* Banco (exec.c)                                                        */
/* ==================================================================== */
struct tatu_db {
    storage_t storage;
    bool in_transaction, closed;
};

/* JSON (json.c): o arquivo .tatu */
enum { JV_NULL, JV_BOOL, JV_INT, JV_REAL, JV_STRING, JV_ARRAY, JV_OBJECT };
typedef struct jv {
    int type;
    bool b;
    int64_t i;
    double r;
    const char *s;
    struct jv **items;          /* JV_ARRAY e JV_OBJECT (valores) */
    const char **keys;          /* JV_OBJECT */
    int n;
} jv_t;

jv_t *json_parse(const char *text, char *errbuf, size_t errsize);
void  json_write(sb_t *b, const jv_t *v, int level);
jv_t *jv_new(int type);
void  jv_push(jv_t *arr, jv_t *item);
void  jv_set(jv_t *obj, const char *key, jv_t *value);
jv_t *jv_get(const jv_t *obj, const char *key);
