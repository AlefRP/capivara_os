/* Lixeira: excluir move o item para uma pasta oculta, de onde dá para restaurar. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define TRASH_DIR  "/home/usuario/.lixeira"
#define TRASH_INFO "/home/usuario/.lixeira-info"

void trash_init(void);
bool trash_move(const char *path);
int  trash_count(void);
bool trash_origin(const char *name, char *out, size_t size);
bool trash_restore(const char *name);
void trash_delete(const char *name);      /* apaga um item de vez */
void trash_empty(void);
