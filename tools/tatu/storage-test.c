/* Casos de falha e de propriedade de arquivos, além dos autotestes do kernel. */
#define main tatu_base_main
#include "host.c"
#undef main
#include <string.h>

static int total, failures;
static void check(bool ok, const char *label) {
    total++; if (!ok) failures++;
    printf("%s %s\n", ok ? "OK" : "FALHOU", label);
}

static bool sql(tatu_db_t *db, const char *text) {
    tatu_error_t err; tatu_output_t out = {0};
    bool ok = tatu_run(db, text, &out, &err);
    if (!ok) fprintf(stderr, "%s: %s\n", text, err.message);
    tatu_output_free(&out); return ok;
}

static bool reject(tatu_db_t *db, const char *text, int expected_count) {
    tatu_error_t err; tatu_output_t out = {0};
    bool ok = tatu_run(db, text, &out, &err);
    bool matched = !ok && out.count == expected_count;
    tatu_output_free(&out); return matched;
}

static int64_t scalar(tatu_db_t *db, const char *text) {
    tatu_error_t err; tatu_output_t out = {0};
    bool ok = tatu_run(db, text, &out, &err);
    int64_t result = ok && out.last && out.last->nrows == 1 && out.last->ncols == 1 && out.last->cells[0].type == TV_INT
                   ? out.last->cells[0].i : -999;
    tatu_output_free(&out); return result;
}

static void bad_file(const char *label, const char *contents) {
    tatu_error_t err;
    check(vfs_write_all("/bad.tatu", contents, strlen(contents)), "preparar arquivo inválido");
    tatu_db_t *db = tatu_open("/bad.tatu", &err);
    check(!db && err.kind != TE_NONE && err.message[0], label);
    if (db) tatu_close(db);
}

int main(void) {
    vfs_init(); tatu_error_t err;
    tatu_db_t *db = tatu_open(NULL, &err);
    check(db && sql(db, "CREATE TABLE z(id INTEGER PRIMARY KEY, nome TEXT UNIQUE); INSERT INTO z VALUES(1,'café'),(2,'tatu'); CREATE TABLE a(x TEXT); CREATE TABLE m(x INTEGER);"), "banco inicial");
    const char *names[2];
    check(tatu_tables(db, names, 2) == 3 && !strcmp(names[0], "a") && !strcmp(names[1], "m"), "catálogo truncado continua em ordem");
    check(tatu_save_as(db, "/a.tatu", &err) && !strcmp(tatu_path(db), "/a.tatu"), "salvar como muda o caminho");
    tatu_db_t *duplicate = tatu_open("//./a.tatu", &err);
    check(!duplicate, "conexão duplicada por alias é rejeitada"); if (duplicate) tatu_close(duplicate);
    check(!tatu_save_as(db, "/pasta-inexistente/sem.tatu", &err) && !strcmp(tatu_path(db), "/a.tatu"), "erro ao salvar como preserva o caminho");
    check(sql(db, "BEGIN; INSERT INTO z VALUES(3,'nova');"), "abrir transação antes de salvar como");
    check(!tatu_save_as(db, "/b.tatu", &err) && tatu_in_transaction(db), "salvar como recusa transação sem alterá-la");
    check(sql(db, "ROLLBACK;"), "reverter transação de salvar como");
    check(reject(db, "INSERT INTO z VALUES(3,'terceira'); INSERT INTO z VALUES(4,'quarta'),(2,'repetida');", 1), "script mantém só comandos completos anteriores ao erro");
    check(scalar(db, "SELECT COUNT(*) FROM z;") == 3, "falha de INSERT múltiplo é atômica");
    check(sql(db, "CREATE TABLE reais(x REAL);"), "preparar parâmetros reais");
    const uint64_t invalid_real_bits[] = { 0x7ff8000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL };
    static const char *const invalid_real_labels[] = { "parâmetro NaN rejeitado", "parâmetro infinito rejeitado", "parâmetro infinito negativo rejeitado" };
    for (unsigned i = 0; i < sizeof(invalid_real_bits) / sizeof(invalid_real_bits[0]); i++) {
        tatu_value_t param = { .type = TV_REAL };
        memcpy(&param.r, &invalid_real_bits[i], sizeof(param.r));
        tatu_output_t out = {0};
        bool ok = tatu_execute(db, "INSERT INTO reais VALUES(?);", &param, 1, &out, &err);
        check(!ok && err.kind == TE_TYPE && !out.count, invalid_real_labels[i]);
        tatu_output_free(&out);
    }
    check(scalar(db, "SELECT COUNT(*) FROM reais;") == 0, "valores não finitos não alteram o banco");
    check(sql(db, "BEGIN; UPDATE z SET nome='mudou' WHERE id=1; DROP TABLE z; CREATE TABLE z(x INTEGER); INSERT INTO z VALUES(7); ROLLBACK;"), "rollback de recriação com o mesmo nome");
    check(scalar(db, "SELECT COUNT(*) FROM z;") == 3, "rollback restaurou tabela original");
    check(sql(db, "CREATE TABLE filho(id INTEGER REFERENCES z(id)); INSERT INTO filho VALUES(1); BEGIN; DROP TABLE filho; DROP TABLE z; ROLLBACK;"), "rollback de DROP entre tabelas relacionadas");
    check(reject(db, "DELETE FROM z WHERE id=1;", 0), "referência restaurada bloqueia exclusão");
    tatu_close(db);
    db = tatu_open("/a.tatu", &err);
    check(db && scalar(db, "SELECT COUNT(*) FROM z;") == 3 && scalar(db, "SELECT COUNT(*) FROM reais;") == 0,
          "reabrir estado confirmado após rejeitar valores não finitos");
    if (db) tatu_close(db);
    db = tatu_open("/inexistente/teste.tatu", &err);
    check(db && reject(db, "CREATE TABLE t(x INTEGER);", 0), "autocommit em pasta ausente falha");
    check(db && tatu_tables(db, NULL, 0) == 0, "falha de gravação desfaz o esquema");
    if (db) tatu_close(db);
    bad_file("formato desconhecido", "{\"formato\":\"outro\",\"versao\":1,\"tabelas\":[]}");
    bad_file("versão futura rejeitada", "{\"formato\":\"tatu-db\",\"versao\":2,\"tabelas\":[]}");
    bad_file("chave JSON repetida", "{\"formato\":\"tatu-db\",\"formato\":\"outro\"}");
    bad_file("texto Unicode incompleto", "{\"formato\":\"\\ud800\"}");
    bad_file("texto com zero rejeitado", "{\"formato\":\"\\u0000\"}");
    bad_file("linha com quantidade errada de células", "{\"formato\":\"tatu-db\",\"versao\":1,\"tabelas\":[{\"esquema\":{\"nome\":\"t\",\"colunas\":[{\"nome\":\"id\",\"tipo\":\"INTEGER\"}]},\"linhas\":[[1]],\"proximo_rowid\":2,\"indices\":[]}]}");
    bad_file("falha tardia de chave única libera a carga parcial", "{\"formato\":\"tatu-db\",\"versao\":1,\"tabelas\":[{\"esquema\":{\"nome\":\"t\",\"colunas\":[{\"nome\":\"id\",\"tipo\":\"INTEGER\",\"unico\":true}]},\"linhas\":[[1,1],[2,1]],\"proximo_rowid\":3,\"indices\":[]}]}");
    bad_file("referência ausente na carga libera tabelas", "{\"formato\":\"tatu-db\",\"versao\":1,\"tabelas\":[{\"esquema\":{\"nome\":\"t\",\"colunas\":[{\"nome\":\"id\",\"tipo\":\"INTEGER\",\"referencia\":[\"falta\",\"id\"]}]},\"linhas\":[[1,1]],\"proximo_rowid\":2,\"indices\":[]}]}");
    while (vfs_root()->children) vfs_remove(vfs_root()->children);
    printf("Armazenamento Tatu: %d casos, %d falhas.\n", total, failures);
    return failures ? 1 : 0;
}
