/*
 * CMOS RTC: lido uma vez na inicialização. Depois disso a hora é mantida
 * somando os milissegundos do PIT. Como no Windows, consideramos que o relógio
 * do hardware guarda a hora LOCAL (no QEMU: -rtc base=localtime).
 */
#include "dev/rtc.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/pit.h"
#include "lib/printf.h"

static uint64_t boot_time;      /* hora no instante em que timer_ms() == 0 */

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static bool updating(void) {
    outb(0x70, 0x0A);
    return inb(0x71) & 0x80;
}

static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }

void rtc_init(void) {
    for (int i = 0; i < 100000 && updating(); i++) cpu_pause();
    datetime_t dt;
    dt.second = cmos_read(0x00);
    dt.minute = cmos_read(0x02);
    dt.hour   = cmos_read(0x04);
    dt.day    = cmos_read(0x07);
    dt.month  = cmos_read(0x08);
    dt.year   = cmos_read(0x09);
    uint8_t regb = cmos_read(0x0B);

    if (!(regb & 0x04)) {                       /* valores em BCD */
        dt.second = bcd(dt.second);
        dt.minute = bcd(dt.minute);
        dt.hour   = bcd(dt.hour & 0x7F) | (dt.hour & 0x80);
        dt.day    = bcd(dt.day);
        dt.month  = bcd(dt.month);
        dt.year   = bcd(dt.year);
    }
    if (!(regb & 0x02) && (dt.hour & 0x80))     /* formato 12 h, PM */
        dt.hour = ((dt.hour & 0x7F) + 12) % 24;
    dt.year += 2000;

    time_set(&dt);
    kprintf("rtc: %02d/%02d/%04d %02d:%02d:%02d\n", dt.day, dt.month, dt.year,
            dt.hour, dt.minute, dt.second);
}

/* Dias desde 1970-01-01 (algoritmo de calendário civil). */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint64_t datetime_to_time(const datetime_t *dt) {
    return (uint64_t)days_from_civil(dt->year, dt->month, dt->day) * 86400 +
           dt->hour * 3600 + dt->minute * 60 + dt->second;
}

void time_to_datetime(uint64_t t, datetime_t *dt) {
    int64_t z = (int64_t)(t / 86400);
    uint64_t secs = t % 86400;
    dt->hour = secs / 3600;
    dt->minute = (secs / 60) % 60;
    dt->second = secs % 60;
    dt->weekday = (int)((z + 4) % 7);          /* 1970-01-01 foi quinta-feira */

    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    dt->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    dt->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    dt->year = (int)(y + (dt->month <= 2));
}

uint64_t time_now(void) {
    return boot_time + timer_ms() / 1000;
}

void time_set(const datetime_t *dt) {
    boot_time = datetime_to_time(dt) - timer_ms() / 1000;
}

const char *weekday_name(int wd) {
    static const char *names[] = { "domingo", "segunda-feira", "terça-feira", "quarta-feira",
                                   "quinta-feira", "sexta-feira", "sábado" };
    return names[wd % 7];
}

const char *month_name(int m) {
    static const char *names[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho",
                                   "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };
    return names[(m - 1) % 12];
}
