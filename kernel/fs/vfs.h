/*
 * Sistema de arquivos virtual (fase 1: tudo em memória — "ramfs").
 * Caminhos no estilo Unix: "/home/usuario/Documentos/nota.txt".
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VFS_NAME_MAX 64
#define VFS_PATH_MAX 256

typedef enum { VFS_FILE = 1, VFS_DIR = 2 } vfs_type_t;

typedef struct vnode {
    char name[VFS_NAME_MAX];
    vfs_type_t type;
    uint64_t size, capacity;
    uint8_t *data;
    uint64_t mtime;                    /* segundos desde 1970 */
    struct vnode *parent, *children, *next;
} vnode_t;

void     vfs_init(void);
void     vfs_load_initrd(void *tar, uint64_t size);
vnode_t *vfs_root(void);

/* path pode ser absoluto ou relativo a "cwd" (NULL = raiz) */
vnode_t *vfs_lookup(vnode_t *cwd, const char *path);
vnode_t *vfs_create(vnode_t *cwd, const char *path, vfs_type_t type);   /* cria (ou devolve existente) */
int      vfs_remove(vnode_t *node);                                      /* recursivo */
int      vfs_rename(vnode_t *node, const char *new_name);
int      vfs_move(vnode_t *node, vnode_t *new_parent);
vnode_t *vfs_copy(vnode_t *node, vnode_t *dest_dir);

long     vfs_read(vnode_t *f, uint64_t off, void *buf, size_t n);
long     vfs_write(vnode_t *f, uint64_t off, const void *buf, size_t n);
int      vfs_truncate(vnode_t *f, uint64_t size);
bool     vfs_write_all(const char *path, const void *buf, size_t n);  /* cria/sobrescreve */

int      vfs_child_count(vnode_t *dir);
vnode_t *vfs_child_at(vnode_t *dir, int index);   /* diretórios primeiro, ordem alfabética */
void     vfs_path(vnode_t *node, char *out, size_t size);
bool     vfs_is_ancestor(vnode_t *a, vnode_t *b);  /* a é ancestral de b? */
void     vfs_lock(void);
void     vfs_unlock(void);
