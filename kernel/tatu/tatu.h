/*
 * Tatu DB no Capivara OS: um banco de dados relacional educacional.
 *
 * É o port para C do Tatu DB em Python (github.com/AlefRP/tatu_db, do mesmo
 * autor do Capivara), sem biblioteca C. A arquitetura é a mesma, uma etapa
 * por arquivo, e a trilha de aprendizado de lá vale para o código daqui:
 *
 *   texto → lexer.c → parser.c (+ ast.c) → plan.c ────────→ storage.c
 *           tokens     árvore sintática     plano e         heap, índices,
 *                                           operadores      diário e arquivo
 *
 * O formato do arquivo .tatu (JSON legível) também é o mesmo: um banco
 * criado no PC abre aqui, e vice-versa. Detalhes em docs/tatu.md.
 *
 * O motor inteiro é protegido por um mutex: o Terminal e o app Toca podem
 * usá-lo ao mesmo tempo, mas um comando roda por vez.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------------------------------------------- valores */
typedef enum { TV_NULL, TV_INT, TV_REAL, TV_TEXT, TV_BOOL } tatu_vtype_t;

typedef struct {
    uint8_t type;
    union {
        int64_t i;
        double r;
        const char *s;          /* UTF-8, terminado em zero */
        bool b;
    };
} tatu_value_t;

/* ---------------------------------------------------------------- erros */
typedef enum {
    TE_NONE, TE_SYNTAX, TE_CATALOG, TE_CONSTRAINT, TE_TYPE, TE_EXECUTION, TE_TRANSACTION, TE_GENERIC,
} tatu_errkind_t;

typedef struct {
    tatu_errkind_t kind;
    char message[400];          /* só a mensagem */
    int pos;                    /* posição (em bytes) no SQL, ou -1 */
    int line, column;           /* linha e coluna (em caracteres), de 1 em diante */
    char rendered[900];         /* mensagem + "(linha L, coluna C)" + a linha + a setinha */
} tatu_error_t;

const char *tatu_error_label(tatu_errkind_t kind);       /* "Erro de sintaxe", "Restrição violada"... */

/* ---------------------------------------------------------------- resultados */
typedef struct tatu_result {
    int ncols, nrows;
    const char **cols;
    tatu_value_t *cells;        /* nrows × ncols, linha por linha */
    const char *message;        /* comandos sem linhas: "3 linha(s) inserida(s)" */
    int rowcount;               /* linhas afetadas por INSERT, UPDATE e DELETE */
    bool plan;                  /* veio de um EXPLAIN: uma coluna "plano" */
    uint64_t micros;            /* quanto o comando levou */
    struct tatu_result *next;
} tatu_result_t;

typedef struct {
    tatu_result_t *first, *last;
    int count;
    void *arena;                /* a memória de todos os resultados */
} tatu_output_t;

void tatu_output_free(tatu_output_t *out);

/* ---------------------------------------------------------------- banco */
typedef struct tatu_db tatu_db_t;

/* Abre (ou cria, na primeira mudança) um arquivo .tatu. NULL ou ":memory:":
 * o banco fica só na memória. */
tatu_db_t *tatu_open(const char *path, tatu_error_t *err);
void       tatu_close(tatu_db_t *db);          /* uma transação aberta é desfeita */
/* Grava uma cópia e passa a usar esse arquivo; exige transação encerrada. */
bool       tatu_save_as(tatu_db_t *db, const char *path, tatu_error_t *err);

/* Roda um ou mais comandos separados por ';'. O texto é todo analisado antes:
 * com erro de sintaxe, nada roda. Os resultados vão para "out" (zerado antes;
 * libere com tatu_output_free) até o primeiro erro, que preenche "err". */
bool tatu_run(tatu_db_t *db, const char *sql, tatu_output_t *out, tatu_error_t *err);

/* Um comando só, com os valores dos parâmetros '?' (nada de SQL injection). */
bool tatu_execute(tatu_db_t *db, const char *sql, const tatu_value_t *params, int nparams,
                  tatu_output_t *out, tatu_error_t *err);

const char *tatu_path(tatu_db_t *db);          /* NULL: em memória */
bool        tatu_in_transaction(tatu_db_t *db);

/* ---------------------------------------------------------------- catálogo */
typedef struct {
    const char *name, *type;    /* tipo: INTEGER, REAL, TEXT ou BOOLEAN */
    bool primary_key, not_null, unique;
    tatu_value_t def;           /* DEFAULT (TV_NULL se não houver) */
    const char *ref_table, *ref_column;
} tatu_column_info_t;

typedef struct {
    const char *name, *column;
    bool unique, automatic;     /* automático: criado por PRIMARY KEY ou UNIQUE */
} tatu_index_info_t;

/* Os nomes das tabelas, em ordem alfabética. Devolve quantas existem. */
int  tatu_tables(tatu_db_t *db, const char **names, int max);
/* Informações de uma tabela (os ponteiros valem até a próxima mudança). */
bool tatu_table_info(tatu_db_t *db, const char *table, int *ncols, int *nrows, int *nindexes);
bool tatu_column_info(tatu_db_t *db, const char *table, int i, tatu_column_info_t *out);
bool tatu_index_info(tatu_db_t *db, const char *table, int i, tatu_index_info_t *out);
/* O CREATE TABLE (e os CREATE INDEX) da tabela, como texto. */
bool tatu_schema_sql(tatu_db_t *db, const char *table, char *buf, size_t size);

/* ---------------------------------------------------------------- texto */
/* Como o shell mostra um valor: NULL, TRUE, 3.0, 2.5, Bolinha. */
void tatu_format_cell(const tatu_value_t *v, char *buf, size_t size);
/* Como literal SQL: NULL, 'pé-d''água', 3.14. */
void tatu_sql_literal(const tatu_value_t *v, char *buf, size_t size);
/* O comando terminou? (acaba em ';' fora de um texto entre aspas) */
bool tatu_is_complete(const char *sql);
/* Palavras-chave do SQL do Tatu (para colorir o editor). */
bool tatu_is_keyword(const char *word, size_t len);

/* Dados de exemplo (espécies de tatu, tatus e avistamentos). */
extern const char tatu_demo_sql[];
extern const char *const tatu_demo_queries[];
extern const int tatu_demo_query_count;

/* ---------------------------------------------------------------- shell */
/* O shell interativo, como o "python -m tatu": as cores são as do Terminal
 * (0 texto, 1 apagado, 2 verde, 3 azul, 4 amarelo, 5 vermelho, 6 ciano). */
typedef void (*tatu_write_fn)(void *ctx, int color, const char *text);
typedef struct tatu_shell tatu_shell_t;

tatu_shell_t *tatu_shell_new(tatu_db_t *db, tatu_write_fn write, void *ctx);
void          tatu_shell_banner(tatu_shell_t *s);
bool          tatu_shell_feed(tatu_shell_t *s, const char *line);   /* false: hora de sair */
void          tatu_shell_cancel(tatu_shell_t *s);                     /* Ctrl+C: descarta o comando */
const char   *tatu_shell_prompt(tatu_shell_t *s);
void          tatu_shell_free(tatu_shell_t *s);                       /* também fecha o banco */

/* Casos de regressão do port, incluindo comportamentos do Tatu em Python.
 * Devolve quantos falharam. */
int tatu_selftest(tatu_write_fn write, void *ctx);
