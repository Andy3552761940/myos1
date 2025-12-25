#include "arch/x86_64/pic.h"
#include "io.h"

/* PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

uint16_t pic_get_mask(void) {
    uint8_t master = inb(PIC1_DATA);
    uint8_t slave = inb(PIC2_DATA);
    return (uint16_t)(master | ((uint16_t)slave << 8));
}

void pic_set_mask_all(uint16_t mask) {
    outb(PIC1_DATA, (uint8_t)(mask & 0xFF));
    outb(PIC2_DATA, (uint8_t)((mask >> 8) & 0xFF));
}

void pic_set_mask(uint8_t irq_line, int masked) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq_line -= 8;
    }

    value = inb(port);
    if (masked) value |= (uint8_t)(1u << irq_line);
    else        value &= (uint8_t)~(1u << irq_line);
    outb(port, value);
}

void pic_init(void) {
    /* Save masks */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    /* Start init sequence */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* Remap offsets */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* Tell master about slave at IRQ2, tell slave its cascade identity */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Restore masks */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}
