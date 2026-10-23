---
name: novo-aplicativo
description: Passo a passo para criar um aplicativo novo no Capivara OS (janela, laço de eventos, registro no Launchpad/Dock/pesquisa, ícone, docs e teste). Use quando o pedido for "cria um app de ...", "adiciona um programa", ou para entender como os apps existentes são montados.
---

# Criar um aplicativo

Todo app é uma **tarefa do kernel** com uma janela e um laço
*espera evento → trata → redesenha*. A Calculadora (`kernel/apps/calc.c`) é
um bom exemplo pequeno; o Bloco de Notas (`notepad.c`) mostra menus e arquivos.

## 1. O arquivo do app

Crie `kernel/apps/<nome>.c`. O Makefile compila qualquer `.c` dessa pasta,
com SSE liberado, então `float` pode ser usado aqui.

```c
/* Relógio: mostra a hora em letras grandes e atualiza a cada segundo. */
#include "apps/apps.h"
#include "gui/ui.h"          /* traz wm.h, gfx.h, font.h, theme.h e icons.h */
#include "dev/rtc.h"
#include "mm/heap.h"
#include "lib/printf.h"

#define W 320
#define H 160

typedef struct {
    window_t *win;
    bool quit;
} relogio_t;

static void draw(relogio_t *r) {
    canvas_t *cv = win_canvas(r->win);
    gfx_clear(cv, theme.window_bg);            /* sempre cores do tema: claro e escuro */
    /* ... desenhe com gfx_* e font_draw ... */
    win_invalidate(r->win);                    /* avisa o compositor que mudou */
}

void relogio_main(void *arg) {
    if (arg) kfree(arg);                       /* argumento opcional (ex.: um caminho) */
    relogio_t *r = kcalloc(1, sizeof(relogio_t));
    r->win = win_create("Relógio", ICON_CLOCK, WIN_AUTO, WIN_AUTO, W, H, WF_DECORATED);
    ev_set_timer(1000);                        /* EV_TIMER a cada segundo */
    draw(r);
    while (!r->quit) {
        event_t e;
        gui_wait(&e);                          /* dorme até chegar um evento */
        switch (e.type) {
        case EV_TIMER: case EV_THEME: case EV_RESIZE: break;
        case EV_CLOSE: r->quit = true; continue;
        default: continue;                     /* evento sem efeito: não redesenha */
        }
        draw(r);
    }
    win_destroy(r->win);
    kfree(r);
}
```

Eventos mais usados (`kernel/sched/event.h`): `EV_MOUSE_MOVE/DOWN/UP/WHEEL/LEAVE`
(coordenadas `e.x`, `e.y` do cliente; `e.button`, `e.clicks`), `EV_KEY_DOWN`
(`e.key`, e `e.ch` quando `e.key == KEY_CHAR`), `EV_CLOSE`, `EV_RESIZE`,
`EV_THEME`, `EV_TIMER` e `EV_MENU`.

Para mais recursos:

- **Janela redimensionável:** use `WF_APP` no lugar de `WF_DECORATED`, mais
  `win_set_min_size()`. No `EV_RESIZE`, `win_canvas()` já tem o tamanho novo.
- **Menus na barra do topo:** chame `win_set_menus(win, "Arquivo|Editar")`.
  Trate `EV_MENU`: `e.key` é o índice do menu e `e.x`/`e.y` a posição na tela.
  Para abrir a lista de itens, use `ui_menu` (veja `run_menu` no Bloco de Notas).
- **Diálogos prontos** (`gui/ui.h`): `ui_message`, `ui_input`,
  `ui_file_dialog`, `ui_menu`.

## 2. Registrar

1. `kernel/apps/apps.h`: declare `void relogio_main(void *arg);`.
2. `kernel/apps/apps.c`: acrescente uma linha em `app_list[]`:
   `{ "relogio", "Relógio", "Hora certa", ICON_CLOCK, relogio_main, false },`.
   Os campos são o id (usado em `abrir relogio` no Terminal), o nome, a
   descrição (aparece na pesquisa), o ícone, a função e se fica fixado no Dock.

O Launchpad, a pesquisa (Super+Espaço) e o comando `abrir` leem a `app_list`
sozinhos.

## 3. Ícone

Se nenhum `ICON_*` existente servir, crie um seguindo a skill
`desenhar-icone`: um valor novo no enum de `kernel/gui/icons.h`, antes de
`ICON_COUNT`, e um `case` em `icon_draw()`.

## 4. Documentar e testar

- Acrescente o app nas tabelas do `README.md` e do `docs/guia-de-uso.md`. Se
  ele tiver ícone novo, inclua-o também na lista de `main()` em
  `tools/arte/arte.c` e rode `make arte`.
- Teste com a skill `testar-no-qemu`: abra o app pela pesquisa
  (`key meta_l-spc`, `type relogio`, `key ret`), tire uma captura e confira
  nos temas claro e escuro (`tema escuro` no Terminal).
- Build sem avisos.
