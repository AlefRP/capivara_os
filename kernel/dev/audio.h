/*
 * Som: a saída de áudio do sistema. Hoje há um driver só, o do AC'97
 * (ac97.c), a placa de som que o VirtualBox e o QEMU emulam.
 *
 * O formato é fixo: 48 000 quadros por segundo, estéreo, 16 bits com sinal,
 * intercalados (esquerdo, direito, esquerdo, direito...). Quem tem som em
 * outro formato converte antes de chamar audio_write().
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_RATE 48000

void   audio_init(void);
bool   audio_present(void);
const char *audio_name(void);          /* "AC'97 (8086:2415)", ou NULL sem placa */

/* Põe quadros estéreo na fila da placa de som. Não bloqueia: devolve quantos
 * quadros couberam, que pode ser menos que n se a fila (~0,3 s) encheu. */
size_t audio_write(const int16_t *frames, size_t n);

/* Quantos quadros ainda faltam tocar. Quem gera som em tempo real (um jogo)
 * usa isto para não se adiantar nem se atrasar em relação à placa. */
size_t audio_queued(void);

void   audio_set_volume(int percent);  /* volume geral, 0 a 100 (0 = mudo) */
int    audio_volume(void);
