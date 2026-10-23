/*
 * ramfs: árvore de nós (vnode) em memória. Cada diretório tem uma lista
 * ligada de filhos, mantida em ordem (diretórios primeiro, depois por nome).
 *
 * Um mutex global protege a árvore: vários aplicativos (tarefas) podem usar
 * arquivos ao mesmo tempo.
 */
#include "fs/vfs.h"
#include "mm/heap.h"
#include "dev/rtc.h"
#include "sched/task.h"
#include "lib/string.h"
#include "lib/printf.h"

static vnode_t root;
static mutex_t vfs_mutex;

void vfs_lock(void)   { mutex_lock(&vfs_mutex); }
void vfs_unlock(void) { mutex_unlock(&vfs_mutex); }

vnode_t *vfs_root(void) { return &root; }

static int compare(vnode_t *a, vnode_t *b) {
    if (a->type != b->type) return a->type == VFS_DIR ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

static void insert_child(vnode_t *dir, vnode_t *n) {
    n->parent = dir;
    vnode_t **pp = &dir->children;
    while (*pp && compare(*pp, n) < 0) pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
}

static void unlink_child(vnode_t *n) {
    if (!n->parent) return;
    for (vnode_t **pp = &n->parent->children; *pp; pp = &(*pp)->next) {
        if (*pp == n) { *pp = n->next; break; }
    }
    n->parent = NULL;
    n->next = NULL;
}

static vnode_t *find_child(vnode_t *dir, const char *name, size_t len) {
    for (vnode_t *c = dir->children; c; c = c->next)
        if (strlen(c->name) == len && !strncmp(c->name, name, len)) return c;
    return NULL;
}

/* Percorre o caminho. Se "want_parent", para no penúltimo componente e
 * devolve em *last o nome final. */
static vnode_t *walk(vnode_t *cwd, const char *path, bool want_parent, const char **last, size_t *last_len) {
    vnode_t *n = (path[0] == '/' || !cwd) ? &root : cwd;
    const char *p = path;
    for (;;) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *end = p;
        while (*end && *end != '/') end++;
        size_t len = end - p;
        const char *after = end;
        while (*after == '/') after++;
        if (want_parent && !*after) {
            *last = p;
            *last_len = len;
            return n;
        }
        if (len == 1 && p[0] == '.') { /* nada */ }
        else if (len == 2 && p[0] == '.' && p[1] == '.') { if (n->parent) n = n->parent; }
        else {
            if (n->type != VFS_DIR) return NULL;
            n = find_child(n, p, len);
            if (!n) return NULL;
        }
        p = end;
    }
    if (want_parent) { *last = NULL; *last_len = 0; }
    return n;
}

vnode_t *vfs_lookup(vnode_t *cwd, const char *path) {
    vfs_lock();
    vnode_t *n = walk(cwd, path, false, NULL, NULL);
    vfs_unlock();
    return n;
}

static vnode_t *new_node(const char *name, size_t len, vfs_type_t type) {
    vnode_t *n = kcalloc(1, sizeof(vnode_t));
    if (!n) return NULL;
    if (len >= VFS_NAME_MAX) len = VFS_NAME_MAX - 1;
    memcpy(n->name, name, len);
    n->name[len] = 0;
    n->type = type;
    n->mtime = time_now();
    return n;
}

vnode_t *vfs_create(vnode_t *cwd, const char *path, vfs_type_t type) {
    vfs_lock();
    const char *name;
    size_t len;
    vnode_t *dir = walk(cwd, path, true, &name, &len);
    vnode_t *n = NULL;
    if (dir && dir->type == VFS_DIR && name && len) {
        n = find_child(dir, name, len);
        if (!n) {
            n = new_node(name, len, type);
            if (n) {
                insert_child(dir, n);
                dir->mtime = n->mtime;
            }
        } else if (n->type != type) {
            n = NULL;
        }
    }
    vfs_unlock();
    return n;
}

static void free_tree(vnode_t *n) {
    vnode_t *c = n->children;
    while (c) {
        vnode_t *next = c->next;
        free_tree(c);
        c = next;
    }
    kfree(n->data);
    kfree(n);
}

int vfs_remove(vnode_t *node) {
    if (node == &root) return -1;
    vfs_lock();
    unlink_child(node);
    free_tree(node);
    vfs_unlock();
    return 0;
}

int vfs_rename(vnode_t *node, const char *new_name) {
    if (node == &root || !*new_name || strchr(new_name, '/')) return -1;
    vfs_lock();
    vnode_t *parent = node->parent;
    if (find_child(parent, new_name, strlen(new_name))) {
        vfs_unlock();
        return -1;
    }
    unlink_child(node);
    strlcpy(node->name, new_name, VFS_NAME_MAX);
    insert_child(parent, node);
    vfs_unlock();
    return 0;
}

bool vfs_is_ancestor(vnode_t *a, vnode_t *b) {
    for (vnode_t *p = b; p; p = p->parent)
        if (p == a) return true;
    return false;
}

int vfs_move(vnode_t *node, vnode_t *new_parent) {
    if (node == &root || new_parent->type != VFS_DIR || vfs_is_ancestor(node, new_parent)) return -1;
    vfs_lock();
    if (find_child(new_parent, node->name, strlen(node->name))) {
        vfs_unlock();
        return -1;
    }
    unlink_child(node);
    insert_child(new_parent, node);
    vfs_unlock();
    return 0;
}

static vnode_t *copy_tree(vnode_t *src, const char *name) {
    vnode_t *n = new_node(name, strlen(name), src->type);
    if (!n) return NULL;
    if (src->type == VFS_FILE && src->size) {
        n->data = kmalloc(src->size);
        if (n->data) {
            memcpy(n->data, src->data, src->size);
            n->size = n->capacity = src->size;
        }
    }
    for (vnode_t *c = src->children; c; c = c->next) {
        vnode_t *cc = copy_tree(c, c->name);
        if (cc) insert_child(n, cc);
    }
    return n;
}

vnode_t *vfs_copy(vnode_t *node, vnode_t *dest_dir) {
    if (dest_dir->type != VFS_DIR || vfs_is_ancestor(node, dest_dir)) return NULL;
    vfs_lock();
    /* se já existe um com o mesmo nome, gera "nome (cópia)" */
    char name[VFS_NAME_MAX];
    strlcpy(name, node->name, sizeof(name));
    for (int i = 1; find_child(dest_dir, name, strlen(name)) && i < 100; i++) {
        if (i == 1) snprintf(name, sizeof(name), "%s (cópia)", node->name);
        else snprintf(name, sizeof(name), "%s (cópia %d)", node->name, i);
    }
    vnode_t *n = copy_tree(node, name);
    if (n) insert_child(dest_dir, n);
    vfs_unlock();
    return n;
}

long vfs_read(vnode_t *f, uint64_t off, void *buf, size_t n) {
    if (f->type != VFS_FILE) return -1;
    vfs_lock();
    long r = 0;
    if (off < f->size) {
        r = (long)((f->size - off) < n ? (f->size - off) : n);
        memcpy(buf, f->data + off, r);
    }
    vfs_unlock();
    return r;
}

static bool reserve(vnode_t *f, uint64_t size) {
    if (size <= f->capacity) return true;
    uint64_t cap = f->capacity ? f->capacity : 256;
    while (cap < size) cap *= 2;
    uint8_t *d = krealloc(f->data, cap);
    if (!d) return false;
    f->data = d;
    f->capacity = cap;
    return true;
}

long vfs_write(vnode_t *f, uint64_t off, const void *buf, size_t n) {
    if (f->type != VFS_FILE) return -1;
    vfs_lock();
    if (!reserve(f, off + n)) {
        vfs_unlock();
        return -1;
    }
    if (off > f->size) memset(f->data + f->size, 0, off - f->size);
    memcpy(f->data + off, buf, n);
    if (off + n > f->size) f->size = off + n;
    f->mtime = time_now();
    vfs_unlock();
    return (long)n;
}

int vfs_truncate(vnode_t *f, uint64_t size) {
    vfs_lock();
    int r = 0;
    if (size > f->size) {
        if (reserve(f, size)) memset(f->data + f->size, 0, size - f->size);
        else r = -1;
    }
    if (!r) f->size = size;
    f->mtime = time_now();
    vfs_unlock();
    return r;
}

bool vfs_write_all(const char *path, const void *buf, size_t n) {
    vnode_t *f = vfs_create(NULL, path, VFS_FILE);
    if (!f) return false;
    vfs_truncate(f, 0);
    return vfs_write(f, 0, buf, n) == (long)n;
}

int vfs_child_count(vnode_t *dir) {
    int n = 0;
    vfs_lock();
    for (vnode_t *c = dir->children; c; c = c->next) n++;
    vfs_unlock();
    return n;
}

vnode_t *vfs_child_at(vnode_t *dir, int index) {
    vfs_lock();
    vnode_t *c = dir->children;
    while (c && index--) c = c->next;
    vfs_unlock();
    return c;
}

void vfs_path(vnode_t *node, char *out, size_t size) {
    if (node == &root) { strlcpy(out, "/", size); return; }
    const char *parts[32];
    int n = 0;
    for (vnode_t *p = node; p && p != &root && n < 32; p = p->parent) parts[n++] = p->name;
    out[0] = 0;
    while (n--) {
        strlcat(out, "/", size);
        strlcat(out, parts[n], size);
    }
}

/* ---- initrd: arquivo tar no formato USTAR ----
 * Cada arquivo = cabeçalho de 512 bytes (nome, tamanho em octal, tipo)
 * seguido dos dados arredondados para múltiplos de 512. */
static uint64_t octal(const char *s, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (s[i] - '0');
    return v;
}

void vfs_load_initrd(void *tar, uint64_t size) {
    uint8_t *p = tar, *end = p + size;
    int files = 0;
    while (p + 512 <= end && p[0]) {
        char name[256];
        const char *prefix = (const char *)p + 345;
        if (*prefix) snprintf(name, sizeof(name), "%.155s/%.100s", prefix, (const char *)p);
        else snprintf(name, sizeof(name), "%.100s", (const char *)p);
        uint64_t fsize = octal((const char *)p + 124, 12);
        char type = p[156];

        const char *path = name;
        if (path[0] == '.' && path[1] == '/') path += 1;      /* "./x" → "/x" */
        size_t plen = strlen(name);
        if (plen && name[plen - 1] == '/') name[plen - 1] = 0;

        if (*path && strcmp(path, ".") && strcmp(path, "/")) {
            if (type == '5') {
                vfs_create(NULL, path, VFS_DIR);
            } else if (type == '0' || type == 0) {
                vnode_t *f = vfs_create(NULL, path, VFS_FILE);
                if (f) {
                    vfs_write(f, 0, p + 512, fsize);
                    files++;
                }
            }
        }
        p += 512 + ((fsize + 511) & ~511ULL);
    }
    kprintf("initrd: %d arquivos carregados\n", files);
}

void vfs_init(void) {
    root.type = VFS_DIR;
    root.mtime = time_now();
    static const char *dirs[] = {
        "/home", "/home/usuario", "/home/usuario/Área de Trabalho", "/home/usuario/Documentos",
        "/home/usuario/Downloads", "/home/usuario/Imagens", "/home/usuario/Músicas",
        "/sistema", "/tmp",
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        vfs_create(NULL, dirs[i], VFS_DIR);
}
