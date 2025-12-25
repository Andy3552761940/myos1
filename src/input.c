#include "input.h"
#include "arch/x86_64/irq.h"
#include "io.h"
#include "lib.h"
#include "console.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_CMD 0x64

#define KEY_QUEUE_SIZE 64
#define MOUSE_QUEUE_SIZE 64

static key_event_t key_queue[KEY_QUEUE_SIZE];
static size_t key_head = 0;
static size_t key_tail = 0;

static mouse_event_t mouse_queue[MOUSE_QUEUE_SIZE];
static size_t mouse_head = 0;
static size_t mouse_tail = 0;

static int ps2_wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) return 1;
    }
    return 0;
}

static int ps2_wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & 0x02) == 0) return 1;
    }
    return 0;
}

static void ps2_write_cmd(uint8_t cmd) {
    if (!ps2_wait_write()) return;
    outb(PS2_CMD, cmd);
}

static void ps2_write_data(uint8_t data) {
    if (!ps2_wait_write()) return;
    outb(PS2_DATA, data);
}

static uint8_t ps2_read_data(void) {
    if (!ps2_wait_read()) return 0;
    return inb(PS2_DATA);
}

static void queue_key(uint8_t scancode) {
    size_t next = (key_head + 1) % KEY_QUEUE_SIZE;
    if (next == key_tail) return;
    key_queue[key_head].pressed = (scancode & 0x80u) ? 0 : 1;
    key_queue[key_head].scancode = (uint8_t)(scancode & 0x7Fu);
    key_head = next;
}

static void queue_mouse(int8_t dx, int8_t dy, uint8_t buttons) {
    size_t next = (mouse_head + 1) % MOUSE_QUEUE_SIZE;
    if (next == mouse_tail) return;
    mouse_queue[mouse_head].dx = dx;
    mouse_queue[mouse_head].dy = dy;
    mouse_queue[mouse_head].buttons = buttons;
    mouse_head = next;
}

void input_init(void) {
    /* Disable devices */
    ps2_write_cmd(0xAD);
    ps2_write_cmd(0xA7);

    /* Flush buffer */
    (void)inb(PS2_DATA);

    /* Read/modify controller config */
    ps2_write_cmd(0x20);
    uint8_t config = ps2_read_data();
    config |= 0x03; /* enable IRQ1/IRQ12 */
    config &= (uint8_t)~0x10; /* enable clock for 2nd port */
    ps2_write_cmd(0x60);
    ps2_write_data(config);

    /* Enable devices */
    ps2_write_cmd(0xAE);
    ps2_write_cmd(0xA8);

    /* Enable mouse streaming */
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF6); /* defaults */
    (void)ps2_read_data();
    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4); /* enable */
    (void)ps2_read_data();

    irq_register_handler(1, input_handle_irq1, "ps2-keyboard");
    irq_register_handler(12, input_handle_irq12, "ps2-mouse");

    console_write("[input] PS/2 keyboard/mouse initialized\n");
}

void input_handle_irq1(uint8_t irq, intr_frame_t* frame) {
    (void)irq;
    (void)frame;
    uint8_t scancode = inb(PS2_DATA);
    queue_key(scancode);
}

void input_handle_irq12(uint8_t irq, intr_frame_t* frame) {
    (void)irq;
    (void)frame;
    static uint8_t packet[3];
    static uint8_t idx = 0;

    uint8_t status = inb(PS2_STATUS);
    if ((status & 0x01) == 0) return;

    uint8_t data = inb(PS2_DATA);

    if (idx == 0 && (data & 0x08) == 0) {
        return; /* resync */
    }

    packet[idx++] = data;
    if (idx < 3) return;
    idx = 0;

    uint8_t buttons = packet[0] & 0x07;
    int8_t dx = (int8_t)packet[1];
    int8_t dy = (int8_t)packet[2];

    queue_mouse(dx, dy, buttons);
}

int input_read_key(key_event_t* out) {
    if (!out) return 0;
    if (key_tail == key_head) return 0;
    *out = key_queue[key_tail];
    key_tail = (key_tail + 1) % KEY_QUEUE_SIZE;
    return 1;
}

int input_read_mouse(mouse_event_t* out) {
    if (!out) return 0;
    if (mouse_tail == mouse_head) return 0;
    *out = mouse_queue[mouse_tail];
    mouse_tail = (mouse_tail + 1) % MOUSE_QUEUE_SIZE;
    return 1;
}
