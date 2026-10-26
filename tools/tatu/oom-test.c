/* Falha uma alocação por vez para exercitar o desfazer em memória insuficiente. */
#include <stdlib.h>
static long fail_after = -1;
static int fail_now(void) {
    if (fail_after < 0) return 0;
    if (!fail_after--) { fail_after = -1; return 1; }
    return 0;
}
static void *fault_malloc(size_t n) { return fail_now() ? NULL : malloc(n); }
static void *fault_calloc(size_t n, size_t s) { return fail_now() ? NULL : calloc(n, s); }
static void *fault_realloc(void *p, size_t n) { return fail_now() ? NULL : realloc(p, n); }
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define main tatu_base_main
#include "host.c"
#undef main
#undef malloc
#undef calloc
#undef realloc

static bool query(tatu_db_t *db, const char *sql) {
    tatu_output_t out = {0}; tatu_error_t err;
    bool ok = tatu_run(db, sql, &out, &err); tatu_output_free(&out); return ok;
}

int main(void) {
    static const char *const statements[] = {
        "CREATE TABLE nova(id INTEGER PRIMARY KEY,nome TEXT DEFAULT 'padrão',pai INTEGER REFERENCES base(id));",
        "INSERT INTO base VALUES(3,'terceira'),(4,'quarta');",
        "UPDATE base SET nome='outro' WHERE id=1;",
        "CREATE UNIQUE INDEX novo ON base(nome);",
        "BEGIN; INSERT INTO base VALUES(3,'terceira'); COMMIT;",
    };
    vfs_init(); unsigned cases = 0;
    for (unsigned which = 0; which < sizeof(statements) / sizeof(statements[0]); which++) {
        for (long allocation = 0; allocation < 180; allocation++) {
            tatu_error_t err; tatu_db_t *db = tatu_open(NULL, &err);
            if (!db || !query(db, "CREATE TABLE base(id INTEGER PRIMARY KEY,nome TEXT UNIQUE); INSERT INTO base VALUES(1,'café'),(2,'tatu');")) return 2;
            fail_after = allocation;
            query(db, statements[which]);
            fail_after = -1;
            if (!query(db, "SELECT * FROM base;")) return 3;
            tatu_close(db); cases++;
        }
    }
    /* Carregar e salvar também atravessam alocações de longa duração. */
    tatu_error_t err; tatu_db_t *seed = tatu_open("/origem.tatu", &err);
    if (!seed || !query(seed, "CREATE TABLE t(id INTEGER PRIMARY KEY,nome TEXT); INSERT INTO t VALUES(1,'Tatá');")) return 4;
    tatu_close(seed);
    for (long allocation = 0; allocation < 180; allocation++) {
        fail_after = allocation; tatu_db_t *db = tatu_open("/origem.tatu", &err); fail_after = -1;
        if (db) tatu_close(db);
        cases++;
    }
    for (long allocation = 0; allocation < 180; allocation++) {
        tatu_db_t *db = tatu_open("/origem.tatu", &err); if (!db) return 5;
        fail_after = allocation; tatu_save_as(db, "/copia.tatu", &err); fail_after = -1;
        if (!query(db, "SELECT * FROM t;")) return 6;
        tatu_close(db); cases++;
    }
    while (vfs_root()->children) vfs_remove(vfs_root()->children);
    printf("Tatu: %u injeções de falha de alocação sem erro de memória.\n", cases);
    return 0;
}
