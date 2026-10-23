/* Registro de aplicativos: nome, ícone e função de entrada de cada um. */
#pragma once
#include <stdbool.h>

typedef struct {
    const char *id;
    const char *name;
    const char *description;
    int icon;
    void (*entry)(void *arg);     /* recebe uma string (kmalloc) ou NULL */
    bool pinned;                  /* aparece fixado na barra de tarefas */
} app_info_t;

extern const app_info_t app_list[];
extern const int app_count;

const app_info_t *app_find(const char *id);
bool app_launch(const char *id, const char *arg);
void app_open_path(const char *path);        /* abre com o aplicativo adequado */
int  app_icon_for(const char *name, bool dir); /* ícone pelo tipo de arquivo */

void shell_main(void *arg);
void terminal_main(void *arg);
void notepad_main(void *arg);
void explorer_main(void *arg);
void calc_main(void *arg);
void settings_main(void *arg);
void taskmgr_main(void *arg);
void paint_main(void *arg);
void photos_main(void *arg);
void snip_main(void *arg);
