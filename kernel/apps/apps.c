#include "apps/apps.h"
#include "gui/icons.h"
#include "sched/task.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "gui/wm.h"
#include "mm/heap.h"

const app_info_t app_list[] = {
    { "explorer", "Explorador de Arquivos", "Navegue pelas suas pastas", ICON_EXPLORER, explorer_main, true },
    { "terminal", "Terminal", "Linha de comando", ICON_TERMINAL, terminal_main, true },
    { "notepad", "Bloco de Notas", "Editor de texto", ICON_NOTEPAD, notepad_main, true },
    { "calc", "Calculadora", "Contas do dia a dia", ICON_CALC, calc_main, true },
    { "paint", "Paint", "Desenho livre", ICON_PAINT, paint_main, true },
    { "fotos", "Fotos", "Visualizador de imagens", ICON_PHOTOS, photos_main, true },
    { "captura", "Ferramenta de Captura", "Capturar a tela (Super+Shift+S)", ICON_SNIP, snip_main, false },
    { "taskmgr", "Gerenciador de Tarefas", "Processos e desempenho", ICON_TASKMGR, taskmgr_main, false },
    { "settings", "Configurações", "Tema, papel de parede, sistema", ICON_SETTINGS, settings_main, true },
};
const int app_count = sizeof(app_list) / sizeof(app_list[0]);

const app_info_t *app_find(const char *id) {
    for (int i = 0; i < app_count; i++)
        if (!strcmp(app_list[i].id, id)) return &app_list[i];
    return NULL;
}

bool app_launch(const char *id, const char *arg) {
    const app_info_t *a = app_find(id);
    if (!a) return false;
    return task_create(a->name, a->entry, arg ? kstrdup(arg) : NULL) != NULL;
}

static bool has_ext(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    return dot && !strcasecmp(dot, ext);
}

void app_open_path(const char *path) {
    vnode_t *n = vfs_lookup(NULL, path);
    if (!n) return;
    if (n->type == VFS_DIR) app_launch("explorer", path);
    else if (has_ext(path, ".bmp")) app_launch("fotos", path);
    else app_launch("notepad", path);
}

int app_icon_for(const char *name, bool dir) {
    if (dir) return ICON_FOLDER;
    if (has_ext(name, ".bmp")) return ICON_IMAGE;
    return ICON_TEXT;
}

/* "Ferramenta de Captura": só pede ao compositor o modo de recorte */
void snip_main(void *arg) {
    if (arg) kfree(arg);
    task_sleep(250);              /* deixa o menu Iniciar sumir antes */
    wm_capture(true);
}
