/* Laboratórios do OSTEP: comandos do Terminal que mostram, no sistema de
 * verdade ou em simuladores, os assuntos do livro "Operating Systems: Three
 * Easy Pieces" (guia em /home/usuario/Documentos/OSTEP e docs/ostep.md). */
#pragma once
#include <stdbool.h>

/* As cores são as do Terminal (C_TEXT, C_DIM...), na mesma ordem. */
enum { LAB_TEXT, LAB_DIM, LAB_GREEN, LAB_BLUE, LAB_YELLOW, LAB_RED, LAB_CYAN, LAB_MAGENTA, LAB_WHITE };

struct vnode;

/* Por onde os laboratórios escrevem: o Terminal passa uma função dele. */
typedef struct {
    void (*write)(void *ctx, int color, const char *text);
    void *ctx;
    struct vnode *cwd;         /* pasta atual, para caminhos relativos */
} lab_out_t;

/* Roda o laboratório argv[0]; devolve false se não for um deles. */
bool lab_run(lab_out_t *o, int argc, char **argv);
/* Regressões determinísticas das primitivas reais de semáforo e join. */
void lab_sync_test(lab_out_t *o);
