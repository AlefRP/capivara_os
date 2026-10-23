/*
 * Eventos (mensagens) entregues às tarefas. Cada tarefa tem uma fila; quem
 * desenha janelas fica num laço "espera evento → trata → redesenha".
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum {
    EV_NONE,
    EV_MOUSE_MOVE,      /* x, y relativos à área cliente da janela */
    EV_MOUSE_DOWN,      /* button: 1 = esquerdo, 2 = direito, 3 = meio; clicks = 2 em duplo clique */
    EV_MOUSE_UP,
    EV_MOUSE_WHEEL,     /* wheel: +1 = para cima, -1 = para baixo */
    EV_MOUSE_LEAVE,
    EV_KEY_DOWN,        /* key, ch (Unicode), mods */
    EV_KEY_UP,
    EV_FOCUS,
    EV_BLUR,
    EV_CLOSE,           /* usuário pediu para fechar a janela */
    EV_RESIZE,          /* x, y = novo tamanho da área cliente */
    EV_TIMER,
    EV_WINLIST,         /* (shell) a lista de janelas mudou */
    EV_SHELL,           /* (shell) comando global: key = SHELL_* */
    EV_THEME,           /* tema/papel de parede mudou: redesenhe tudo */
    EV_POPUP_CLOSE,     /* clique fora de uma janela popup */
    EV_MENU,            /* menu da barra superior: key = índice do menu, x/y = posição na tela */
};

/* modificadores */
#define MOD_SHIFT 0x01
#define MOD_CTRL  0x02
#define MOD_ALT   0x04
#define MOD_ALTGR 0x08
#define MOD_SUPER 0x10
#define MOD_CAPS  0x20

typedef struct window window_t;

typedef struct event {
    uint16_t type;
    uint16_t button;
    int32_t  x, y;
    int32_t  wheel;
    uint32_t key;       /* código KEY_* */
    uint32_t ch;        /* caractere Unicode (0 se não imprimível) */
    uint32_t mods;
    uint32_t clicks;
    window_t *win;      /* janela de destino */
} event_t;

struct task;
bool ev_post(struct task *t, const event_t *e);   /* pode ser chamado de interrupção */
bool ev_wait(event_t *e);                          /* bloqueia a tarefa atual */
bool ev_poll(event_t *e);                          /* não bloqueia */
void ev_set_timer(uint32_t interval_ms);           /* EV_TIMER periódico (0 = desliga) */
