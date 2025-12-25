#include "rtc.h"
#include "io.h"
#include "lib.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

static int rtc_is_updating(void) {
    outb(CMOS_ADDR, 0x0A);
    io_wait();
    return inb(CMOS_DATA) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

void rtc_read_time(rtc_time_t* out) {
    if (!out) return;

    while (rtc_is_updating()) { }

    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t reg_b = cmos_read(0x0B);

    if ((reg_b & 0x04) == 0) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7F);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }

    if ((reg_b & 0x02) == 0 && (hour & 0x80)) {
        hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
    }

    uint16_t full_year = (uint16_t)(2000 + year);
    if (year < 70) {
        full_year = (uint16_t)(2000 + year);
    } else {
        full_year = (uint16_t)(1900 + year);
    }

    out->second = sec;
    out->minute = min;
    out->hour = hour;
    out->day = day;
    out->month = month;
    out->year = full_year;
}
