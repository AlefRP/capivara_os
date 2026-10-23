/* Componentes de interface reutilizáveis pelos aplicativos. */
#pragma once
#include "gui/gfx.h"
#include "gui/wm.h"
#include "gui/font.h"
#include "gui/theme.h"
#include "gui/icons.h"

typedef enum { UI_NORMAL, UI_HOVER, UI_PRESSED, UI_DISABLED } ui_state_t;
enum { BTN_STANDARD, BTN_ACCENT, BTN_FLAT };

void ui_button(canvas_t *c, rect_t r, const char *label, int style, ui_state_t st);
void ui_glyph_button(canvas_t *c, rect_t r, int glyph, ui_state_t st);
void ui_text_center(canvas_t *c, rect_t r, const char *s, uint32_t color, int flags);
int  ui_wrap(canvas_t *c, rect_t r, const char *text, uint32_t color, int flags, bool draw);
void ui_scrollbar(canvas_t *c, rect_t track, int total, int visible, int offset);
int  ui_scrollbar_offset(rect_t track, int total, int visible, int y);
void ui_toggle(canvas_t *c, int x, int y, bool on, ui_state_t st);
void ui_checkbox(canvas_t *c, int x, int y, bool on);

/* campo de texto de uma linha */
typedef struct {
    char text[256];
    int len, cur, scroll;
} ui_edit_t;

void ui_edit_set(ui_edit_t *e, const char *s);
bool ui_edit_key(ui_edit_t *e, const event_t *ev);      /* true se o texto mudou */
void ui_edit_click(ui_edit_t *e, rect_t r, int x);
void ui_edit_draw(canvas_t *c, rect_t r, ui_edit_t *e, bool focused, const char *placeholder);

/* lista rolável (o app desenha as linhas; aqui fica a matemática) */
typedef struct {
    int count, sel, scroll, row_h;
} ui_list_t;

int  ui_list_hit(ui_list_t *l, rect_t r, int y);
void ui_list_wheel(ui_list_t *l, rect_t r, int delta);
void ui_list_clamp(ui_list_t *l, rect_t r);
void ui_list_ensure(ui_list_t *l, rect_t r);
bool ui_list_key(ui_list_t *l, rect_t r, const event_t *e);

/* menus e diálogos modais (rodam um laço de eventos próprio) */
typedef struct {
    const char *label;
    int id;
    int glyph;
    const char *shortcut;
    bool disabled;
    bool separator;
} ui_menu_item_t;

int  ui_menu(int screen_x, int screen_y, const ui_menu_item_t *items, int n);
int  ui_message(window_t *parent, const char *title, const char *msg,
                const char *b1, const char *b2, const char *b3);
bool ui_input(window_t *parent, const char *title, const char *prompt, char *buf, int size);
bool ui_input_ex(window_t *parent, const char *title, const char *prompt, char *buf, int size, bool masked);
bool ui_file_dialog(window_t *parent, bool save, const char *title, char *path, int size);

/* área de transferência global (texto UTF-8) */
void        clipboard_set(const char *s, int len);
const char *clipboard_get(int *len);
#define CLIP_HISTORY 10
int         clipboard_history_count(void);
const char *clipboard_history_get(int i, int *len);   /* 0 = mais recente */

static inline bool ui_hit(rect_t r, const event_t *e) { return rect_has(r, e->x, e->y); }
