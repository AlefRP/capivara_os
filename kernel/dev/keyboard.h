/* Códigos de tecla lógicos (independentes do layout). */
#pragma once

enum {
    KEY_NONE = 0,
    KEY_CHAR,           /* tecla que produz caractere: veja event.ch */
    KEY_ESC, KEY_BACKSPACE, KEY_TAB, KEY_ENTER,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_INSERT, KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_ALTGR,
    KEY_SUPER, KEY_MENU, KEY_CAPS, KEY_NUMLOCK, KEY_SCROLLLOCK, KEY_PRINT,
};

enum { KB_LAYOUT_ABNT2, KB_LAYOUT_US };
void keyboard_set_layout(int layout);
int  keyboard_layout(void);
