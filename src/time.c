#include "time.h"
#include "rtc.h"
#include "arch/x86_64/pit.h"
#include "hpet.h"

static uint64_t g_epoch_base = 0;
static int g_time_ready = 0;

static int is_leap(uint64_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint64_t days_before_month(uint64_t y, uint64_t m) {
    static const uint8_t days[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    uint64_t total = 0;
    for (uint64_t i = 1; i < m; i++) {
        total += days[i - 1];
        if (i == 2 && is_leap(y)) total += 1;
    }
    return total;
}

static uint64_t rtc_to_epoch(const rtc_time_t* t) {
    uint64_t y = t->year;
    uint64_t days = 0;
    for (uint64_t year = 1970; year < y; year++) {
        days += is_leap(year) ? 366 : 365;
    }
    days += days_before_month(y, t->month);
    days += (uint64_t)(t->day - 1);
    uint64_t seconds = days * 86400ull;
    seconds += (uint64_t)t->hour * 3600ull;
    seconds += (uint64_t)t->minute * 60ull;
    seconds += t->second;
    return seconds;
}

void time_init(void) {
    rtc_time_t now;
    rtc_read_time(&now);
    g_epoch_base = rtc_to_epoch(&now);
    g_time_ready = 1;
    (void)hpet_init();
}

void time_gettimeofday(time_val_t* out) {
    if (!out) return;
    uint64_t ticks = pit_ticks();
    uint32_t hz = pit_frequency_hz();
    if (hz == 0 || !g_time_ready) {
        out->tv_sec = 0;
        out->tv_usec = 0;
        return;
    }
    uint64_t sec = ticks / hz;
    uint64_t usec = (ticks % hz) * 1000000ull / hz;
    out->tv_sec = g_epoch_base + sec;
    out->tv_usec = usec;
}

uint64_t time_now_ms(void) {
    uint64_t ticks = pit_ticks();
    uint32_t hz = pit_frequency_hz();
    if (hz == 0) return 0;
    return (ticks * 1000ull) / hz;
}

uint64_t time_now_ns(void) {
    return hpet_now_ns();
}
