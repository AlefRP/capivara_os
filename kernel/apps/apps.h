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

/* Os apps do sistema e, no fim, os extras. Só leitura: quem monta é apps_init(). */
extern app_info_t app_list[];
extern int app_count;

/* Programas "extras" vêm de fora do repositório e só entram num build local
 * (docs/extras.md). Cada um se registra assim, num arquivo dele:
 *
 *     EXTRA_APP(meu_jogo,
 *               .app = { "jogo", "Meu Jogo", "Descrição", ICON_GAME, jogo_main, true },
 *               .image = &icone_do_jogo,     (opcional: ícone de imagem, gui/icons.h)
 *               .desktop = true);            (opcional: atalho na área de trabalho)
 *
 * A entrada vai para a seção .extra_apps do kernel (kernel/linker.ld), e
 * apps_init() a acrescenta à lista no boot. Assim o código do sistema não
 * precisa conhecer os extras. */
struct icon_image;
typedef struct {
    app_info_t app;
    const struct icon_image *image;   /* se houver, substitui app.icon */
    bool desktop;
} extra_app_t;

#define EXTRA_APP(var, ...) \
    __attribute__((used, section(".extra_apps"), aligned(8))) \
    static const extra_app_t var = { __VA_ARGS__ }

void apps_init(void);
const app_info_t *app_find(const char *id);
bool app_launch(const char *id, const char *arg);
void app_open_path(const char *path);        /* abre com o aplicativo adequado */
int  app_icon_for(const char *name, bool dir); /* ícone pelo tipo de arquivo */

/* Atalho: um arquivo "Nome.app" que guarda o id de um aplicativo. O
 * Explorador e a área de trabalho mostram o ícone do app, e abrir o arquivo
 * abre o app. */
#define DESKTOP_DIR "/home/usuario/Área de Trabalho"
bool app_make_shortcut(const app_info_t *a, const char *dir);

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
void estudos_main(void *arg);
void toca_main(void *arg);
