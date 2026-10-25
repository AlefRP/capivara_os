#include "apps/apps.h"
#include "gui/icons.h"
#include "sched/task.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "gui/wm.h"
#include "mm/heap.h"
#include "lib/printf.h"

static const app_info_t system_apps[] = {
    { "explorer", "Explorador de Arquivos", "Navegue pelas suas pastas", ICON_EXPLORER, explorer_main, true },
    { "terminal", "Terminal", "Linha de comando", ICON_TERMINAL, terminal_main, true },
    { "notepad", "Bloco de Notas", "Editor de texto", ICON_NOTEPAD, notepad_main, true },
    { "calc", "Calculadora", "Contas do dia a dia", ICON_CALC, calc_main, true },
    { "paint", "Paint", "Desenho livre", ICON_PAINT, paint_main, true },
    { "fotos", "Fotos", "Visualizador de imagens", ICON_PHOTOS, photos_main, true },
    { "captura", "Ferramenta de Captura", "Capturar a tela (Super+Shift+S)", ICON_SNIP, snip_main, false },
    { "taskmgr", "Gerenciador de Tarefas", "Processos e desempenho", ICON_TASKMGR, taskmgr_main, false },
    { "estudos", "Estudos", "Guia do livro OSTEP, com laboratórios", ICON_BOOK, estudos_main, true },
    { "settings", "Configurações", "Tema, papel de parede, sistema", ICON_SETTINGS, settings_main, true },
};

#define APP_MAX 32
app_info_t app_list[APP_MAX];
int app_count;

/* Começo e fim da seção .extra_apps: o linker define os dois símbolos. */
extern const app_info_t __extra_apps_start[], __extra_apps_end[];

void apps_init(void) {
    app_count = 0;
    for (unsigned i = 0; i < sizeof(system_apps) / sizeof(system_apps[0]); i++)
        app_list[app_count++] = system_apps[i];
    for (const app_info_t *a = __extra_apps_start; a < __extra_apps_end && app_count < APP_MAX; a++) {
        app_list[app_count++] = *a;
        kprintf("apps: extra '%s'\n", a->id);
    }
}

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
