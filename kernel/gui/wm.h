/*
 * Gerenciador de janelas e compositor.
 *
 * Funções "win_*": usadas por um aplicativo nas PRÓPRIAS janelas.
 * Funções "wm_*": operações globais (usadas pelo shell / barra de tarefas),
 * que identificam janelas pelo número (id), nunca por ponteiro.
 */
#pragma once
#include "gui/gfx.h"
#include "sched/event.h"
#include "sched/task.h"

#define TITLE_H    28
#define BORDER     1
#define SHADOW     18
#define RADIUS     10
#define LIGHT_D    12      /* diâmetro dos botões "semáforo" (fechar/minimizar/zoom) */
#define MENUBAR_H  26      /* barra de menus no topo da tela */
#define DOCK_H     74      /* faixa reservada para o Dock embaixo */
#define DOCK_WIN_H 156     /* janela do Dock (inclui espaço para a ampliação dos ícones) */

/* flags de janela */
#define WF_DECORATED  0x0001   /* barra de título + borda + sombra */
#define WF_RESIZABLE  0x0002
#define WF_TOPMOST    0x0004   /* sempre no topo (barra de tarefas, menus) */
#define WF_DESKTOP    0x0008   /* sempre no fundo */
#define WF_NOFOCUS    0x0010   /* clicar não rouba o foco */
#define WF_NOTASKBAR  0x0020   /* não aparece na barra de tarefas */
#define WF_POPUP      0x0040   /* recebe EV_POPUP_CLOSE ao clicar fora */
#define WF_ALPHA      0x0080   /* conteúdo com transparência */
#define WF_SHADOW     0x0100   /* sombra mesmo sem decoração */
#define WF_HIDDEN     0x0200   /* criar invisível */
#define WF_CENTER     0x0400   /* centralizar na tela */
#define WF_NOMAXIMIZE 0x0800
#define WF_CLICKTHRU  0x1000   /* pixels transparentes não recebem cliques (Dock) */

#define WF_APP (WF_DECORATED | WF_RESIZABLE)

#define WIN_AUTO (-100000)     /* posição automática (cascata) */

enum {
    CURSOR_ARROW, CURSOR_IBEAM, CURSOR_SIZE_H, CURSOR_SIZE_V,
    CURSOR_SIZE_D1, CURSOR_SIZE_D2, CURSOR_HAND, CURSOR_CROSS, CURSOR_COUNT,
};

/* comandos enviados ao shell (EV_SHELL, campo key) */
enum {
    SHELL_START = 1, SHELL_EXPLORER, SHELL_TASKMGR, SHELL_RUN_TERMINAL, SHELL_LOCK,
    SHELL_NOTIFY,          /* chegou notificação nova */
    SHELL_NOTIFICATIONS,   /* abrir a central (Super+N) */
    SHELL_CLIPBOARD,       /* histórico da área de transferência (Super+V) */
    SHELL_SEARCH,          /* pesquisa rápida (Super+Espaço / Super+S) */
};

#define MAX_DESKTOPS 8

typedef enum { WS_NORMAL, WS_MAXIMIZED, WS_SNAP_LEFT, WS_SNAP_RIGHT, WS_SNAP_CUSTOM } win_state_t;

struct window {
    int id;
    task_t *owner;
    char title[96];
    int icon;
    uint32_t flags;
    rect_t frame;              /* retângulo externo (com barra de título e borda) */
    canvas_t *canvas;          /* conteúdo da área cliente (o app desenha aqui) */
    bool visible, minimized;
    win_state_t state;
    rect_t restore;            /* geometria antes de maximizar/encaixar */
    int min_w, min_h;
    int cursor;
    int hover_btn, press_btn;
    bool resize_pending;
    int desk;                  /* área de trabalho virtual */
    char menus[96];            /* títulos dos menus na barra superior, separados por '|' */
};

/* ---- para aplicativos ---- */
window_t *win_create(const char *title, int icon, int x, int y, int w, int h, uint32_t flags);
void      win_destroy(window_t *w);
canvas_t *win_canvas(window_t *w);
void      win_invalidate(window_t *w);
void      win_invalidate_rect(window_t *w, rect_t r);     /* coordenadas do cliente */
void      win_set_title(window_t *w, const char *title);
void      win_show(window_t *w, bool show);
void      win_move(window_t *w, int x, int y);            /* posição do quadro */
void      win_resize(window_t *w, int cw, int ch);        /* tamanho do cliente */
void      win_set_min_size(window_t *w, int cw, int ch);
void      win_set_cursor(window_t *w, int cursor);
/* menus que aparecem na barra superior quando a janela tem foco, ex.: "Arquivo|Editar".
 * Ao clicar, a janela recebe EV_MENU com key = índice e x/y = onde abrir o menu. */
void      win_set_menus(window_t *w, const char *titles);
void      win_focus(window_t *w);
bool      win_is_focused(window_t *w);
rect_t    win_client_rect(window_t *w);                   /* em coordenadas da tela */

/* Espera o próximo evento da tarefa. Trata internamente o realocamento do
 * canvas em EV_RESIZE (o app só precisa redesenhar). */
bool gui_wait(event_t *e);
bool gui_poll(event_t *e);

/* ---- para o shell ---- */
typedef struct {
    int id;
    char title[96];
    int icon;
    bool minimized, focused;
} win_info_t;

int    wm_list(win_info_t *out, int max);
void   wm_activate(int id);
void   wm_minimize(int id);
void   wm_toggle(int id);                 /* ativa, ou minimiza se já ativa */
void   wm_close(int id);
void   wm_show_desktop(void);
void   wm_register_shell(task_t *t);
rect_t wm_workarea(void);
int    wm_screen_w(void);
int    wm_screen_h(void);
void   wm_broadcast(const event_t *e);    /* para todas as tarefas com janelas */
void   wm_redraw_all(void);
void   wm_apply_theme(bool dark, int accent, int wallpaper);   /* troca o tema e avisa todos os aplicativos */
void   wm_init(void);
void   wm_shell_command(int cmd);              /* envia EV_SHELL ao shell */

/* tela de bloqueio: enquanto ativa, só essa janela aparece e recebe entrada */
void   wm_lock(window_t *lock_window);         /* NULL = desbloquear */
bool   wm_is_locked(void);

/* áreas de trabalho virtuais e Visão de Tarefas */
void   wm_task_view(void);                     /* abre/fecha (Super+Tab) */
int    wm_desktop_count(void);
int    wm_current_desktop(void);

/* miniatura do conteúdo de uma janela (prévia na barra de tarefas) */
bool   wm_thumbnail(int id, canvas_t *dst, rect_t area);
/* manda Ctrl+V para a janela com foco (histórico da área de transferência) */
void   wm_paste_to_focused(void);
/* captura de tela: região (modo recorte) ou tela inteira */
void   wm_capture(bool region);
/* barra de menus global */
bool   wm_get_menus(int id, char *out, int size);
void   wm_send_menu(int id, int index, int x, int y);
void   wm_zoom(int id);                        /* maximiza / restaura */
void   wm_snap_window(int id, int side);       /* -1 metade esquerda, +1 direita */
