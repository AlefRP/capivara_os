/* Casos observáveis do motor: execução, restrições, desfazer e arquivo. */
#include "tatu/tatu.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "arch/x86_64/pit.h"

typedef struct {
    tatu_db_t *db;
    tatu_write_fn write;
    void *ctx;
    int total, failed;
} test_t;

static void check(test_t *t, bool ok, const char *name, const char *detail) {
    t->total++;
    if (!ok) t->failed++;
    char buf[1100];
    snprintf(buf, sizeof(buf), "%s %s%s%s\n", ok ? "OK" : "FALHOU", name,
        !ok && detail ? ": " : "", !ok && detail ? detail : "");
    if (t->write) t->write(t->ctx, ok ? 2 : 5, buf);
    if (!ok) kprintf("tatu: %s", buf);
}

static void query(test_t *t, const char *name, const char *sql, int rows, int cols, const char *first) {
    tatu_output_t out = {0};
    tatu_error_t e;
    bool ok = tatu_run(t->db, sql, &out, &e);
    char value[256] = "", detail[1000];
    tatu_result_t *r = out.last;
    if (r && r->nrows && r->ncols) tatu_format_cell(&r->cells[0], value, sizeof(value));
    bool pass = ok && r && (rows < 0 || r->nrows == rows) && (cols < 0 || r->ncols == cols)
        && (!first || !strcmp(first, value));
    snprintf(detail, sizeof(detail), "linhas=%d colunas=%d valor=%s erro=%s",
        r ? r->nrows : -1, r ? r->ncols : -1, value, ok ? "nenhum" : e.message);
    check(t, pass, name, detail);
    tatu_output_free(&out);
}

static void reject(test_t *t, const char *name, const char *sql, tatu_errkind_t kind) {
    tatu_output_t out = {0};
    tatu_error_t e;
    bool ok = tatu_run(t->db, sql, &out, &e);
    check(t, !ok && e.kind == kind, name, ok ? "comando aceito indevidamente" : e.message);
    tatu_output_free(&out);
}

static void discard_output(void *ctx, int color, const char *s) { (void)ctx; (void)color; (void)s; }

int tatu_selftest(tatu_write_fn write, void *ctx) {
    test_t t = {.write = write, .ctx = ctx};
    tatu_error_t e;
    t.db = tatu_open(NULL, &e);
    check(&t, t.db != NULL, "abrir banco em memória", t.db ? NULL : e.message);
    if (!t.db) return t.failed;
    query(&t, "aritmética", "SELECT 2+3*4;", 1, 1, "14");
    query(&t, "texto e aspas", "SELECT 'pé-d''água';", 1, 1, "pé-d'água");
    query(&t, "lógica ternária", "SELECT NULL AND FALSE;", 1, 1, "FALSE");
    query(&t, "IS NULL", "SELECT NULL IS NULL;", 1, 1, "TRUE");
    query(&t, "função escalar", "SELECT LENGTH('capivara');", 1, 1, "8");
    query(&t, "COALESCE", "SELECT COALESCE(NULL, 'tatu');", 1, 1, "tatu");
    query(&t, "criar esquema", "CREATE TABLE teste (id INTEGER PRIMARY KEY, nome TEXT NOT NULL UNIQUE, n INTEGER DEFAULT 7);", 0, 0, NULL);
    query(&t, "inserir e chave automática", "INSERT INTO teste(nome) VALUES('A'),('B'); SELECT id FROM teste ORDER BY id;", 2, 1, "1");
    query(&t, "valor padrão", "SELECT n FROM teste WHERE id=1;", 1, 1, "7");
    reject(&t, "restrição UNIQUE", "INSERT INTO teste(nome) VALUES('A');", TE_CONSTRAINT);
    reject(&t, "restrição NOT NULL", "INSERT INTO teste(nome) VALUES(NULL);", TE_CONSTRAINT);
    reject(&t, "tipo incompatível", "INSERT INTO teste(nome,n) VALUES('C','texto');", TE_TYPE);
    reject(&t, "INSERT inteiro desfeito ao falhar", "INSERT INTO teste(nome) VALUES('C'),('A');", TE_CONSTRAINT);
    query(&t, "sem inserção parcial", "SELECT COUNT(*) FROM teste;", 1, 1, "2");
    query(&t, "UPDATE", "UPDATE teste SET n=n+1 WHERE id=1; SELECT n FROM teste WHERE id=1;", 1, 1, "8");
    query(&t, "ROLLBACK de linhas", "BEGIN; UPDATE teste SET n=99; DELETE FROM teste WHERE id=2; INSERT INTO teste(nome) VALUES('Z'); ROLLBACK; SELECT SUM(n) FROM teste;", 1, 1, "15");
    query(&t, "ROLLBACK de esquema", "BEGIN; CREATE TABLE temporaria(x TEXT); DROP TABLE teste; ROLLBACK; SELECT COUNT(*) FROM teste;", 1, 1, "2");
    reject(&t, "tabela revertida não existe", "SELECT * FROM temporaria;", TE_CATALOG);
    query(&t, "COMMIT", "BEGIN; INSERT INTO teste(nome,n) VALUES('D',10); COMMIT; SELECT COUNT(*) FROM teste;", 1, 1, "3");
    reject(&t, "COMMIT sem transação", "COMMIT;", TE_TRANSACTION);
    query(&t, "criar índice", "CREATE INDEX idx_teste_nome ON teste(nome); SELECT id FROM teste WHERE nome='B';", 1, 1, "2");
    query(&t, "DISTINCT", "SELECT DISTINCT n FROM teste ORDER BY n;", 3, 1, "7");
    query(&t, "LIMIT e OFFSET", "SELECT nome FROM teste ORDER BY nome LIMIT 1 OFFSET 1;", 1, 1, "B");
    query(&t, "GROUP BY e HAVING", "SELECT n,COUNT(*) AS qtd FROM teste GROUP BY n HAVING COUNT(*) > 0 ORDER BY n;", 3, 2, "7");
    query(&t, "LEFT JOIN", "CREATE TABLE filho (id INTEGER PRIMARY KEY, pai INTEGER REFERENCES teste(id)); INSERT INTO filho VALUES(1,1),(2,NULL); SELECT f.id,t.nome FROM filho f LEFT JOIN teste t ON f.pai=t.id ORDER BY f.id;", 2, 2, "1");
    reject(&t, "chave estrangeira", "INSERT INTO filho VALUES(3,999);", TE_CONSTRAINT);
    reject(&t, "pai referenciado", "DELETE FROM teste WHERE id=1;", TE_CONSTRAINT);
    query(&t, "EXPLAIN", "EXPLAIN SELECT nome FROM teste WHERE id=1;", -1, 1, NULL);
    query(&t, "comparação com NULL", "SELECT COUNT(*) FROM filho WHERE pai = NULL;", 1, 1, "0");
    reject(&t, "erro antes de executar script", "INSERT INTO teste(nome) VALUES('nunca'); SELECT FROM;", TE_SYNTAX);
    query(&t, "script inválido não alterou dados", "SELECT COUNT(*) FROM teste WHERE nome='nunca';", 1, 1, "0");
    query(&t, "iniciar transação", "BEGIN;", 0, 0, NULL);
    reject(&t, "transação aninhada", "BEGIN;", TE_TRANSACTION);
    reject(&t, "falha preserva transação", "INSERT INTO teste(nome) VALUES('A');", TE_CONSTRAINT);
    check(&t, tatu_in_transaction(t.db), "transação continua aberta", NULL);
    query(&t, "reverter após erro", "ROLLBACK; SELECT COUNT(*) FROM teste;", 1, 1, "3");
    tatu_value_t param = {.type = TV_TEXT, .s = "A' OR 1=1 --"};
    tatu_output_t out = {0};
    bool ok = tatu_execute(t.db, "SELECT id FROM teste WHERE nome=?;", &param, 1, &out, &e);
    check(&t, ok && out.last && out.last->nrows == 0, "parâmetro é dado, não SQL", ok ? NULL : e.message);
    tatu_output_free(&out);
    tatu_shell_t *shell = tatu_shell_new(t.db, discard_output, NULL);
    int before = tatu_tables(t.db, NULL, 0);
    if (shell) tatu_shell_feed(shell, ".demo");
    check(&t, shell && tatu_tables(t.db, NULL, 0) == before,
          "demo preserva banco existente", NULL);
    if (shell) tatu_shell_free(shell); else tatu_close(t.db);

    /* Caminho exclusivo: rodar o autoteste não pode apagar um banco do usuário. */
    vfs_create(NULL, "/tmp", VFS_DIR);
    char path[VFS_PATH_MAX];
    unsigned suffix = 0;
    do { snprintf(path, sizeof(path), "/tmp/tatu-teste-%llu-%u.tatu", (unsigned long long)timer_ms(), suffix++); }
    while (vfs_lookup(NULL, path));
    t.db = tatu_open(path, &e);
    check(&t, t.db != NULL, "abrir arquivo novo", t.db ? NULL : e.message);
    if (t.db) {
        query(&t, "salvar dados UTF-8 e reais", "CREATE TABLE arquivo(id INTEGER PRIMARY KEY,nome TEXT,x REAL); INSERT INTO arquivo VALUES(1,'Tatá',1.25); CREATE INDEX ix_arquivo ON arquivo(nome);", 0, 0, NULL);
        query(&t, "transação não confirmada", "BEGIN; INSERT INTO arquivo VALUES(2,'descartar',2.5);", 0, 0, NULL);
        tatu_close(t.db);
        t.db = tatu_open(path, &e);
        check(&t, t.db != NULL, "reabrir arquivo JSON", t.db ? NULL : e.message);
        if (t.db) {
            query(&t, "fechar reverte transação", "SELECT COUNT(*) FROM arquivo;", 1, 1, "1");
            query(&t, "texto UTF-8 preservado", "SELECT nome FROM arquivo;", 1, 1, "Tatá");
            query(&t, "real preservado", "SELECT x FROM arquivo;", 1, 1, "1.25");
            int nc = 0, nr = 0, ni = 0;
            check(&t, tatu_table_info(t.db, "arquivo", &nc, &nr, &ni) && ni == 2, "índice persistido", NULL);
            tatu_close(t.db);
        }
        vnode_t *file = vfs_lookup(NULL, path);
        if (file) vfs_remove(file);
    }
    char summary[160];
    snprintf(summary, sizeof(summary), "Tatu DB: %d testes, %d falha(s).\n", t.total, t.failed);
    if (write) write(ctx, t.failed ? 5 : 2, summary);
    kprintf("tatu: testes %s (%d casos, %d falhas)\n", t.failed ? "FALHARAM" : "OK", t.total, t.failed);
    return t.failed;
}
