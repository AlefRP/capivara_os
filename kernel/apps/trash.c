/*
 * Lixeira.
 *
 *   /home/usuario/.lixeira/<nome>        o item excluído
 *   /home/usuario/.lixeira-info/<nome>   texto com o caminho original
 *
 * Pastas que começam com "." ficam ocultas no Explorador (convenção Unix).
 */
#include "apps/trash.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/printf.h"

void trash_init(void) {
    vfs_create(NULL, TRASH_DIR, VFS_DIR);
    vfs_create(NULL, TRASH_INFO, VFS_DIR);
}

/* gera um nome livre em "dir": "nome", "nome (2)", "nome (3)"... */
static void unique_name(vnode_t *dir, const char *name, char *out, size_t size) {
    strlcpy(out, name, size);
    for (int i = 2; vfs_lookup(dir, out) && i < 1000; i++) {
        const char *dot = strrchr(name, '.');
        if (dot && dot != name) snprintf(out, size, "%.*s (%d)%s", (int)(dot - name), name, i, dot);
        else snprintf(out, size, "%s (%d)", name, i);
    }
}

bool trash_move(const char *path) {
    trash_init();
    vnode_t *n = vfs_lookup(NULL, path);
    vnode_t *trash = vfs_lookup(NULL, TRASH_DIR);
    if (!n || !trash || n == vfs_root() || vfs_is_ancestor(n, trash)) return false;

    char name[VFS_NAME_MAX];
    unique_name(trash, n->name, name, sizeof(name));
    char original[VFS_PATH_MAX];
    vfs_path(n, original, sizeof(original));
    if (strcmp(name, n->name) && vfs_rename(n, name)) return false;
    if (vfs_move(n, trash)) return false;

    char info[VFS_PATH_MAX];
    snprintf(info, sizeof(info), "%s/%s", TRASH_INFO, name);
    vfs_write_all(info, original, strlen(original));
    return true;
}

int trash_count(void) {
    vnode_t *t = vfs_lookup(NULL, TRASH_DIR);
    return t ? vfs_child_count(t) : 0;
}

bool trash_origin(const char *name, char *out, size_t size) {
    char info[VFS_PATH_MAX];
    snprintf(info, sizeof(info), "%s/%s", TRASH_INFO, name);
    vnode_t *f = vfs_lookup(NULL, info);
    if (!f) return false;
    long n = vfs_read(f, 0, out, size - 1);
    out[n < 0 ? 0 : n] = 0;
    return n > 0;
}

static void remove_info(const char *name) {
    char info[VFS_PATH_MAX];
    snprintf(info, sizeof(info), "%s/%s", TRASH_INFO, name);
    vnode_t *f = vfs_lookup(NULL, info);
    if (f) vfs_remove(f);
}

/* cria todas as pastas do caminho, como "mkdir -p" */
static vnode_t *make_dirs(const char *path) {
    char buf[VFS_PATH_MAX];
    strlcpy(buf, path, sizeof(buf));
    for (char *p = buf + 1; ; p++) {
        if (*p == '/' || !*p) {
            char saved = *p;
            *p = 0;
            vfs_create(NULL, buf, VFS_DIR);
            *p = saved;
            if (!saved) break;
        }
    }
    return vfs_lookup(NULL, path);
}

bool trash_restore(const char *name) {
    char item_path[VFS_PATH_MAX], origin[VFS_PATH_MAX];
    snprintf(item_path, sizeof(item_path), "%s/%s", TRASH_DIR, name);
    vnode_t *item = vfs_lookup(NULL, item_path);
    if (!item) return false;
    if (!trash_origin(name, origin, sizeof(origin))) snprintf(origin, sizeof(origin), "/home/usuario/%s", name);

    char *slash = strrchr(origin, '/');
    const char *base = slash + 1;
    char parent_path[VFS_PATH_MAX];
    strlcpy(parent_path, origin, sizeof(parent_path));
    parent_path[slash - origin] = 0;
    if (!parent_path[0]) strlcpy(parent_path, "/", sizeof(parent_path));
    vnode_t *parent = make_dirs(parent_path);
    if (!parent) return false;

    char final[VFS_NAME_MAX];
    unique_name(parent, base, final, sizeof(final));
    if (strcmp(final, item->name) && vfs_rename(item, final)) return false;
    if (vfs_move(item, parent)) return false;
    remove_info(name);
    return true;
}

void trash_delete(const char *name) {
    char item_path[VFS_PATH_MAX];
    snprintf(item_path, sizeof(item_path), "%s/%s", TRASH_DIR, name);
    vnode_t *item = vfs_lookup(NULL, item_path);
    if (item) vfs_remove(item);
    remove_info(name);
}

void trash_empty(void) {
    const char *dirs[] = { TRASH_DIR, TRASH_INFO };
    for (int k = 0; k < 2; k++) {
        vnode_t *d = vfs_lookup(NULL, dirs[k]);
        while (d && vfs_child_count(d)) vfs_remove(vfs_child_at(d, 0));
    }
}
