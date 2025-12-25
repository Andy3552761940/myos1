#pragma once
#include <stdint.h>
#include <stddef.h>

void serial_init(void);
int serial_is_ready(void);
void serial_putc(char c);
int serial_can_rx(void);
char serial_getc(void);
void serial_write(const char* s);
void serial_write_n(const char* s, size_t n);
