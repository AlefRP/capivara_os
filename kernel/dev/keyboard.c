/*
 * Teclado PS/2 com layout ABNT2 (português do Brasil).
 *
 * O controlador traduz tudo para o "scancode set 1": cada tecla gera um código
 * ao ser pressionada e o mesmo código + 0x80 ao ser solta. Teclas estendidas
 * (setas, Home, Super...) vêm precedidas de 0xE0.
 *
 * Teclas mortas: ´ ` ~ ^ ¨ não produzem nada sozinhas; ficam guardadas e se
 * combinam com a próxima letra (´ + a = á). Seguidas de espaço, produzem o
 * próprio acento.
 */
#include "dev/keyboard.h"
#include "dev/ps2.h"
#include "dev/input.h"
#include "arch/x86_64/idt.h"
#include "sched/event.h"
#include "lib/printf.h"

#define DEAD_ACUTE  0x110000
#define DEAD_GRAVE  0x110001
#define DEAD_TILDE  0x110002
#define DEAD_CIRC   0x110003
#define DEAD_DIAER  0x110004

/* scancode → {normal, shift, altgr} — layout ABNT2 */
static const uint32_t keymap_abnt2[128][3] = {
    [0x02] = {'1', '!', 0xB9}, [0x03] = {'2', '@', 0xB2}, [0x04] = {'3', '#', 0xB3},
    [0x05] = {'4', '$', 0xA3}, [0x06] = {'5', '%', 0xA2}, [0x07] = {'6', DEAD_DIAER, 0xAC},
    [0x08] = {'7', '&'}, [0x09] = {'8', '*'}, [0x0A] = {'9', '('}, [0x0B] = {'0', ')'},
    [0x0C] = {'-', '_'}, [0x0D] = {'=', '+', 0xA7},
    [0x10] = {'q', 'Q', '/'}, [0x11] = {'w', 'W', '?'}, [0x12] = {'e', 'E', 0xB0},
    [0x13] = {'r', 'R'}, [0x14] = {'t', 'T'}, [0x15] = {'y', 'Y'}, [0x16] = {'u', 'U'},
    [0x17] = {'i', 'I'}, [0x18] = {'o', 'O'}, [0x19] = {'p', 'P'},
    [0x1A] = {DEAD_ACUTE, DEAD_GRAVE}, [0x1B] = {'[', '{', 0xAA},
    [0x1E] = {'a', 'A'}, [0x1F] = {'s', 'S'}, [0x20] = {'d', 'D'}, [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'}, [0x23] = {'h', 'H'}, [0x24] = {'j', 'J'}, [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'}, [0x27] = {0xE7, 0xC7}, [0x28] = {DEAD_TILDE, DEAD_CIRC},
    [0x29] = {'\'', '"'}, [0x2B] = {']', '}', 0xBA},
    [0x2C] = {'z', 'Z'}, [0x2D] = {'x', 'X'}, [0x2E] = {'c', 'C'}, [0x2F] = {'v', 'V'},
    [0x30] = {'b', 'B'}, [0x31] = {'n', 'N'}, [0x32] = {'m', 'M'},
    [0x33] = {',', '<'}, [0x34] = {'.', '>'}, [0x35] = {';', ':'},
    [0x39] = {' ', ' '}, [0x56] = {'\\', '|'}, [0x73] = {'/', '?'},
    [0x37] = {'*', '*'}, [0x4A] = {'-', '-'}, [0x4E] = {'+', '+'}, [0x7E] = {'.', '.'},
};

/* layout americano (EUA): mesmas posições, sem teclas mortas */
static const uint32_t keymap_us[128][3] = {
    [0x02] = {'1', '!'}, [0x03] = {'2', '@'}, [0x04] = {'3', '#'}, [0x05] = {'4', '$'},
    [0x06] = {'5', '%'}, [0x07] = {'6', '^'}, [0x08] = {'7', '&'}, [0x09] = {'8', '*'},
    [0x0A] = {'9', '('}, [0x0B] = {'0', ')'}, [0x0C] = {'-', '_'}, [0x0D] = {'=', '+'},
    [0x10] = {'q', 'Q'}, [0x11] = {'w', 'W'}, [0x12] = {'e', 'E'}, [0x13] = {'r', 'R'},
    [0x14] = {'t', 'T'}, [0x15] = {'y', 'Y'}, [0x16] = {'u', 'U'}, [0x17] = {'i', 'I'},
    [0x18] = {'o', 'O'}, [0x19] = {'p', 'P'}, [0x1A] = {'[', '{'}, [0x1B] = {']', '}'},
    [0x1E] = {'a', 'A'}, [0x1F] = {'s', 'S'}, [0x20] = {'d', 'D'}, [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'}, [0x23] = {'h', 'H'}, [0x24] = {'j', 'J'}, [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'}, [0x27] = {';', ':'}, [0x28] = {'\'', '"'}, [0x29] = {'`', '~'},
    [0x2B] = {'\\', '|'}, [0x2C] = {'z', 'Z'}, [0x2D] = {'x', 'X'}, [0x2E] = {'c', 'C'},
    [0x2F] = {'v', 'V'}, [0x30] = {'b', 'B'}, [0x31] = {'n', 'N'}, [0x32] = {'m', 'M'},
    [0x33] = {',', '<'}, [0x34] = {'.', '>'}, [0x35] = {'/', '?'}, [0x39] = {' ', ' '},
    [0x56] = {'\\', '|'}, [0x37] = {'*', '*'}, [0x4A] = {'-', '-'}, [0x4E] = {'+', '+'},
};

static const uint32_t (*keymap)[3] = keymap_abnt2;
static int layout = KB_LAYOUT_ABNT2;

void keyboard_set_layout(int l) {
    layout = l;
    keymap = l == KB_LAYOUT_US ? keymap_us : keymap_abnt2;
}

int keyboard_layout(void) { return layout; }

/* teclado numérico com NumLock ligado */
static const char keypad[0x54] = {
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0', [0x53] = ',',
};


static uint32_t mods;
static bool extended, numlock = true;
static uint32_t pending_dead;

static uint32_t compose(uint32_t dead, uint32_t c) {
    static const char *bases = "aeiouAEIOU";
    static const uint16_t acute[] = {0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA};
    static const uint16_t grave[] = {0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xCC, 0xD2, 0xD9};
    static const uint16_t circ[]  = {0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0xC2, 0xCA, 0xCE, 0xD4, 0xDB};
    static const uint16_t diaer[] = {0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xC4, 0xCB, 0xCF, 0xD6, 0xDC};
    if (dead == DEAD_TILDE) {
        switch (c) {
        case 'a': return 0xE3; case 'o': return 0xF5; case 'n': return 0xF1;
        case 'A': return 0xC3; case 'O': return 0xD5; case 'N': return 0xD1;
        }
        return 0;
    }
    for (int i = 0; bases[i]; i++) {
        if ((uint32_t)bases[i] != c) continue;
        switch (dead) {
        case DEAD_ACUTE: return acute[i];
        case DEAD_GRAVE: return grave[i];
        case DEAD_CIRC:  return circ[i];
        case DEAD_DIAER: return diaer[i];
        }
    }
    return 0;
}

static uint32_t dead_char(uint32_t dead) {
    switch (dead) {
    case DEAD_ACUTE: return 0xB4;
    case DEAD_GRAVE: return '`';
    case DEAD_TILDE: return '~';
    case DEAD_CIRC:  return '^';
    default:         return 0xA8;
    }
}

static void emit(uint32_t key, uint32_t ch, bool pressed) {
    input_event_t e = { .type = INPUT_KEY, .pressed = pressed, .key = key, .ch = ch, .mods = mods };
    input_push(&e);
}

static void set_mod(uint32_t bit, bool on) {
    if (on) mods |= bit; else mods &= ~bit;
}

static void handle_scancode(uint8_t sc) {
    if (sc == 0xE0) { extended = true; return; }
    bool pressed = !(sc & 0x80);
    uint8_t code = sc & 0x7F;
    bool ext = extended;
    extended = false;

    uint32_t key = KEY_NONE;
    if (ext) {
        switch (code) {
        case 0x2A: case 0x36: return;          /* "shift falso" do Print Screen */
        case 0x1D: key = KEY_RCTRL; set_mod(MOD_CTRL, pressed); break;
        case 0x38: key = KEY_ALTGR; set_mod(MOD_ALTGR, pressed); break;
        case 0x47: key = KEY_HOME; break;
        case 0x48: key = KEY_UP; break;
        case 0x49: key = KEY_PGUP; break;
        case 0x4B: key = KEY_LEFT; break;
        case 0x4D: key = KEY_RIGHT; break;
        case 0x4F: key = KEY_END; break;
        case 0x50: key = KEY_DOWN; break;
        case 0x51: key = KEY_PGDN; break;
        case 0x52: key = KEY_INSERT; break;
        case 0x53: key = KEY_DELETE; break;
        case 0x5B: case 0x5C: key = KEY_SUPER; set_mod(MOD_SUPER, pressed); break;
        case 0x5D: key = KEY_MENU; break;
        case 0x37: key = KEY_PRINT; break;            /* Print Screen */
        case 0x1C: key = KEY_ENTER; break;
        case 0x35: emit(KEY_CHAR, '/', pressed); return;
        default: return;
        }
        emit(key, 0, pressed);
        return;
    }

    switch (code) {
    case 0x01: key = KEY_ESC; break;
    case 0x0E: key = KEY_BACKSPACE; break;
    case 0x0F: key = KEY_TAB; break;
    case 0x1C: key = KEY_ENTER; break;
    case 0x1D: key = KEY_LCTRL; set_mod(MOD_CTRL, pressed); break;
    case 0x2A: key = KEY_LSHIFT; set_mod(MOD_SHIFT, pressed); break;
    case 0x36: key = KEY_RSHIFT; set_mod(MOD_SHIFT, pressed); break;
    case 0x38: key = KEY_LALT; set_mod(MOD_ALT, pressed); break;
    case 0x3A: key = KEY_CAPS; if (pressed) mods ^= MOD_CAPS; break;
    case 0x45: key = KEY_NUMLOCK; if (pressed) numlock = !numlock; break;
    case 0x46: key = KEY_SCROLLLOCK; break;
    case 0x57: key = KEY_F11; break;
    case 0x58: key = KEY_F12; break;
    default:
        if (code >= 0x3B && code <= 0x44) key = KEY_F1 + (code - 0x3B);
        break;
    }
    if (key != KEY_NONE) {
        emit(key, 0, pressed);
        return;
    }

    /* teclado numérico */
    if (code >= 0x47 && code <= 0x53 && keypad[code]) {
        if (numlock) {
            char k = keypad[code];
            if (k == ',' && layout == KB_LAYOUT_US) k = '.';
            emit(KEY_CHAR, (uint8_t)k, pressed);
            return;
        }
        static const uint32_t nav[] = {
            [0x47 - 0x47] = KEY_HOME, [0x48 - 0x47] = KEY_UP, [0x49 - 0x47] = KEY_PGUP,
            [0x4B - 0x47] = KEY_LEFT, [0x4D - 0x47] = KEY_RIGHT, [0x4F - 0x47] = KEY_END,
            [0x50 - 0x47] = KEY_DOWN, [0x51 - 0x47] = KEY_PGDN, [0x52 - 0x47] = KEY_INSERT,
            [0x53 - 0x47] = KEY_DELETE,
        };
        if (nav[code - 0x47]) emit(nav[code - 0x47], 0, pressed);
        return;
    }

    if (code >= 128 || !keymap[code][0]) return;

    if (!pressed) {
        emit(KEY_CHAR, keymap[code][0], false);
        return;
    }

    uint32_t ch;
    bool shift = mods & MOD_SHIFT;
    if ((mods & MOD_ALTGR) && keymap[code][2]) ch = keymap[code][2];
    else ch = keymap[code][shift ? 1 : 0];

    /* Caps Lock inverte maiúsculas/minúsculas das letras (inclusive ç) */
    if (mods & MOD_CAPS) {
        uint32_t lower = keymap[code][0], upper = keymap[code][1];
        bool is_letter = (lower >= 'a' && lower <= 'z') || lower == 0xE7;
        if (is_letter) ch = shift ? lower : upper;
    }

    /* Com Ctrl/Alt, o atalho usa a letra base (Ctrl+S = 's') */
    if (mods & (MOD_CTRL | MOD_ALT)) {
        emit(KEY_CHAR, keymap[code][0] >= 0x110000 ? 0 : keymap[code][0], true);
        return;
    }

    if (ch >= 0x110000) {                    /* tecla morta */
        if (pending_dead) {                  /* duas mortas seguidas: solta a primeira */
            emit(KEY_CHAR, dead_char(pending_dead), true);
            pending_dead = 0;
            emit(KEY_CHAR, dead_char(ch), true);
        } else {
            pending_dead = ch;
        }
        return;
    }
    if (pending_dead) {
        uint32_t composed = ch == ' ' ? dead_char(pending_dead) : compose(pending_dead, ch);
        if (!composed) emit(KEY_CHAR, dead_char(pending_dead), true);
        else ch = composed;
        pending_dead = 0;
    }
    emit(KEY_CHAR, ch, true);
}

static void keyboard_irq(regs_t *r) {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    uint8_t sc = inb(0x60);
    if (status & 0x20) {                     /* na verdade é um byte do mouse */
        mouse_feed_byte(sc);
        return;
    }
    handle_scancode(sc);
}

void keyboard_init(void) {
    irq_register(1, keyboard_irq);
    kprintf("teclado: layout ABNT2\n");
}
