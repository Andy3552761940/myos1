#include "log.h"
#include "console.h"
#include "serial.h"
#include "lib.h"
#include <stdarg.h>

static log_level_t current_level = LOG_LEVEL_INFO;
static uint32_t current_targets = LOG_TARGET_CONSOLE;

void log_init(log_level_t level, uint32_t targets) {
    current_level = level;
    current_targets = targets;
}

void log_set_level(log_level_t level) {
    current_level = level;
}

void log_set_targets(uint32_t targets) {
    current_targets = targets;
}

log_level_t log_get_level(void) {
    return current_level;
}

uint32_t log_get_targets(void) {
    return current_targets;
}

static void log_putc(char c) {
    if (current_targets & LOG_TARGET_CONSOLE) {
        console_putc_vga(c);
    }
    if (current_targets & LOG_TARGET_SERIAL) {
        serial_putc(c);
    }
}

static void log_write(const char* s) {
    if (!s) return;
    for (size_t i = 0; s[i]; i++) log_putc(s[i]);
}

static void log_write_u64(uint64_t v, int base, int uppercase) {
    char buf[32];
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t i = 0;
    if (v == 0) {
        log_putc('0');
        return;
    }
    while (v > 0 && i < sizeof(buf)) {
        buf[i++] = digits[v % (uint64_t)base];
        v /= (uint64_t)base;
    }
    while (i > 0) {
        log_putc(buf[--i]);
    }
}

static void log_write_i64(int64_t v) {
    if (v < 0) {
        log_putc('-');
        log_write_u64((uint64_t)(-v), 10, 0);
    } else {
        log_write_u64((uint64_t)v, 10, 0);
    }
}

static void log_prefix(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_INFO:
            log_write("[info] ");
            break;
        case LOG_LEVEL_WARN:
            log_write("[warn] ");
            break;
        case LOG_LEVEL_ERROR:
            log_write("[error] ");
            break;
        default:
            log_write("[log] ");
            break;
    }
}

static void log_vprintf(log_level_t level, const char* fmt, va_list args) {
    if (level < current_level) return;

    log_prefix(level);

    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            log_putc(*p);
            continue;
        }

        p++;
        if (*p == '%') {
            log_putc('%');
            continue;
        }

        int long_count = 0;
        while (*p == 'l') {
            long_count++;
            p++;
        }

        switch (*p) {
            case 's': {
                const char* s = va_arg(args, const char*);
                log_write(s ? s : "(null)");
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                log_putc((char)c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = (long_count >= 1) ? va_arg(args, long long) : va_arg(args, int);
                log_write_i64(v);
                break;
            }
            case 'u': {
                uint64_t v = (long_count >= 1) ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                log_write_u64(v, 10, 0);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = (long_count >= 1) ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                log_write_u64(v, 16, (*p == 'X'));
                break;
            }
            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(args, void*);
                log_write("0x");
                log_write_u64((uint64_t)v, 16, 0);
                break;
            }
            default:
                log_putc('%');
                log_putc(*p);
                break;
        }
    }
}

void log_printf(log_level_t level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vprintf(level, fmt, args);
    va_end(args);
}

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vprintf(LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vprintf(LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vprintf(LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}
