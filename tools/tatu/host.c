/* Executa o motor real com ASan/UBSan no PC. Só heap, relógio e mutex são
 * adaptados; parser, planejador, armazenamento e VFS são os do kernel. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "tatu/tatu.h"
#include "fs/vfs.h"
#include "sched/task.h"

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kcalloc(size_t n, size_t s) { return calloc(n ? n : 1, s ? s : 1); }
void *krealloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void kfree(void *p) { free(p); }
static task_t host_task;
task_t *task_current(void) { return &host_task; }
void mutex_lock(mutex_t *m) { if (m->locked) abort(); m->locked = 1; }
void mutex_unlock(mutex_t *m) { if (!m->locked) abort(); m->locked = 0; }
uint64_t timer_ms(void) { static uint64_t tick; return ++tick; }
uint64_t time_now(void) { return 0; }
int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    return n;
}
static void output(void *ctx, int color, const char *s) { (void)ctx; (void)color; fputs(s, stdout); }

static bool import_file(const char *host_path, const char *guest_path) {
    FILE *f = fopen(host_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len < 0 || len > 16 * 1024 * 1024) { fclose(f); return false; }
    char *buf = malloc((size_t)len + 1);
    bool ok = buf && fread(buf, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    if (ok) ok = vfs_write_all(guest_path, buf, (size_t)len);
    free(buf);
    return ok;
}

int main(int argc, char **argv) {
    vfs_init();
    int failed = 0;
    if (argc == 1) failed = tatu_selftest(output, NULL);
    else {
        /* Uso: tatu-host banco.tatu 'SQL' [saida.tatu]. '-' cria vazio. */
        if (argc < 3) return 2;
        if (!(argv[1][0] == '-' && !argv[1][1]) && !import_file(argv[1], "/fixture.tatu")) return 2;
        tatu_error_t e;
        tatu_db_t *db = tatu_open("/fixture.tatu", &e);
        if (!db) { fprintf(stderr, "%s\n", e.rendered); failed = 1; }
        else {
            tatu_output_t out = {0};
            bool ok = tatu_run(db, argv[2], &out, &e);
            for (tatu_result_t *r = out.first; r; r = r->next) {
                printf("RESULT %d %d\n", r->nrows, r->ncols);
                for (int row = 0; row < r->nrows; row++) {
                    for (int col = 0; col < r->ncols; col++) {
                        char buf[8192];
                        tatu_sql_literal(&r->cells[row * r->ncols + col], buf, sizeof(buf));
                        printf("%s%s", col ? "\t" : "", buf);
                    }
                    puts("");
                }
            }
            if (!ok) { fprintf(stderr, "%s\n", e.rendered); failed = 1; }
            tatu_output_free(&out);
            tatu_close(db);
            if (argc > 3) {
                vnode_t *f = vfs_lookup(NULL, "/fixture.tatu");
                FILE *dest = f ? fopen(argv[3], "wb") : NULL;
                if (!dest || fwrite(f->data, 1, (size_t)f->size, dest) != (size_t)f->size) failed = 1;
                if (dest) fclose(dest);
            }
        }
    }
    while (vfs_root()->children) vfs_remove(vfs_root()->children);
    return failed ? 1 : 0;
}
