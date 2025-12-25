#include "serial.h"
#include "io.h"

#define SERIAL_PORT 0x3F8

static int serial_inited = 0;

void serial_init(void) {
    /* COM1 base 0x3F8 */
    outb(SERIAL_PORT + 1, 0x00);    /* Disable all interrupts */
    outb(SERIAL_PORT + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(SERIAL_PORT + 0, 0x03);    /* Set divisor to 3 (lo byte) 38400 baud */
    outb(SERIAL_PORT + 1, 0x00);    /*                  (hi byte) */
    outb(SERIAL_PORT + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(SERIAL_PORT + 2, 0xC7);    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(SERIAL_PORT + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
    serial_inited = 1;
}

int serial_is_ready(void) {
    return serial_inited;
}

static int serial_can_tx(void) {
    return inb(SERIAL_PORT + 5) & 0x20;
}

int serial_can_rx(void) {
    return inb(SERIAL_PORT + 5) & 0x01;
}

void serial_putc(char c) {
    if (!serial_inited) return;
    while (!serial_can_tx()) { }
    outb(SERIAL_PORT, (uint8_t)c);
}

char serial_getc(void) {
    if (!serial_inited) return 0;
    while (!serial_can_rx()) { }
    return (char)inb(SERIAL_PORT);
}

void serial_write(const char* s) {
    if (!s) return;
    for (size_t i = 0; s[i]; i++) serial_putc(s[i]);
}

void serial_write_n(const char* s, size_t n) {
    if (!s) return;
    for (size_t i = 0; i < n; i++) serial_putc(s[i]);
}
