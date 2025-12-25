#include "lib.h"
#include "syscall.h"
#include <stdarg.h>

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int v, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)v;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)ac - (int)bc;
        if (ac == 0) return 0;
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    char* out = dst;
    while (*src) *dst++ = *src++;
    *dst = 0;
    return out;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

static void buf_putc(char* buf, size_t size, size_t* idx, char c) {
    if (*idx + 1 < size) {
        buf[*idx] = c;
    }
    (*idx)++;
}

static void format_uint(char* buf, size_t size, size_t* idx, uint64_t v, uint32_t base, int upper) {
    char tmp[32];
    size_t i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v && i < sizeof(tmp)) {
            uint32_t digit = (uint32_t)(v % base);
            if (digit < 10) tmp[i++] = (char)('0' + digit);
            else tmp[i++] = (char)((upper ? 'A' : 'a') + (digit - 10));
            v /= base;
        }
    }
    while (i > 0) {
        buf_putc(buf, size, idx, tmp[--i]);
    }
}

static int vsnprintf_simple(char* buf, size_t size, const char* fmt, va_list ap) {
    size_t idx = 0;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            buf_putc(buf, size, &idx, fmt[i]);
            continue;
        }
        i++;
        char spec = fmt[i];
        if (!spec) break;

        switch (spec) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                for (; *s; s++) buf_putc(buf, size, &idx, *s);
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                buf_putc(buf, size, &idx, (char)c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = va_arg(ap, int64_t);
                if (v < 0) {
                    buf_putc(buf, size, &idx, '-');
                    v = -v;
                }
                format_uint(buf, size, &idx, (uint64_t)v, 10, 0);
                break;
            }
            case 'u': {
                uint64_t v = va_arg(ap, uint64_t);
                format_uint(buf, size, &idx, v, 10, 0);
                break;
            }
            case 'x': {
                uint64_t v = va_arg(ap, uint64_t);
                format_uint(buf, size, &idx, v, 16, 0);
                break;
            }
            case 'X': {
                uint64_t v = va_arg(ap, uint64_t);
                format_uint(buf, size, &idx, v, 16, 1);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void*);
                buf_putc(buf, size, &idx, '0');
                buf_putc(buf, size, &idx, 'x');
                format_uint(buf, size, &idx, v, 16, 0);
                break;
            }
            case '%':
                buf_putc(buf, size, &idx, '%');
                break;
            default:
                buf_putc(buf, size, &idx, '%');
                buf_putc(buf, size, &idx, spec);
                break;
        }
    }

    if (size > 0) {
        size_t term = (idx < size) ? idx : (size - 1);
        buf[term] = 0;
    }
    return (int)idx;
}

int printf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf_simple(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) return len;
    if (len > (int)(sizeof(buf) - 1)) len = (int)(sizeof(buf) - 1);
    return (int)sys_write(1, buf, len);
}

int puts(const char* s) {
    int len = (int)strlen(s);
    if (len) sys_write(1, s, len);
    sys_write(1, "\n", 1);
    return len + 1;
}
