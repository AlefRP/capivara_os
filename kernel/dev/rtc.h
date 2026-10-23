/* Relógio de tempo real (CMOS) e funções de data/hora. */
#pragma once
#include <stdint.h>

typedef struct {
    int year, month, day;        /* month 1-12 */
    int hour, minute, second;
    int weekday;                 /* 0 = domingo */
} datetime_t;

void     rtc_init(void);
uint64_t time_now(void);                         /* segundos desde 1970 (hora local) */
void     time_to_datetime(uint64_t t, datetime_t *dt);
uint64_t datetime_to_time(const datetime_t *dt);
void     time_set(const datetime_t *dt);         /* ajusta a hora do sistema */

const char *weekday_name(int wd);                /* "domingo", "segunda-feira"... */
const char *month_name(int m);                   /* "janeiro"... */
