#pragma once
#include <stdint.h>
#include <stddef.h>

void console_init(void);
void console_putc(char c);
void console_write(const char* s);
void console_write_hex64(uint64_t v);
void console_write_dec_u64(uint64_t v);

void console_write_hex32(uint32_t v);
void console_write_dec_u32(uint32_t v);
