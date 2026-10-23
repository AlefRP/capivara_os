#pragma once
#include <stdint.h>

#define TIMER_HZ 1000

typedef void (*pit_hook_t)(uint64_t now_ms);

void pit_init(void);
void pit_add_hook(pit_hook_t fn);
uint64_t timer_ms(void);   /* milissegundos desde o boot */
