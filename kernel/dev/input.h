/*
 * Fila de entrada: os drivers de teclado e mouse (em interrupção) colocam
 * eventos brutos aqui; o compositor da interface gráfica os consome.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct task;

enum { INPUT_KEY = 1, INPUT_MOUSE = 2 };

typedef struct {
    uint8_t type;
    /* teclado */
    bool pressed;
    uint32_t key, ch, mods;
    /* mouse */
    int32_t dx, dy, wheel;
    uint8_t buttons;          /* bit0 esquerdo, bit1 direito, bit2 meio */
    bool absolute;
    int32_t ax, ay;           /* 0..65535 quando absolute */
} input_event_t;

void input_push(const input_event_t *e);
bool input_pop(input_event_t *e);
bool input_pending(void);
void input_set_consumer(struct task *t);
