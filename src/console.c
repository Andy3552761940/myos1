#include "console.h"
#include "io.h"
#include "lib.h"

#define VGA_TEXT_MODE_BASE ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;
static uint8_t vga_color = 0x0F; /* white on black */

static int serial_inited = 0;

static console_fb_info_t fb_info;
static int fb_enabled = 0;

static void serial_init(void) {
    /* COM1 base 0x3F8 */
    outb(0x3F8 + 1, 0x00);    // Disable all interrupts
    outb(0x3F8 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(0x3F8 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(0x3F8 + 1, 0x00);    //                  (hi byte)
    outb(0x3F8 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(0x3F8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(0x3F8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    serial_inited = 1;
}

static int serial_can_tx(void) {
    return inb(0x3F8 + 5) & 0x20;
}

static void serial_putc(char c) {
    if (!serial_inited) return;
    while (!serial_can_tx()) { }
    outb(0x3F8, (uint8_t)c);
}

static void vga_put_entry_at(char c, uint8_t color, uint8_t x, uint8_t y) {
    const size_t index = (size_t)y * VGA_WIDTH + x;
    VGA_TEXT_MODE_BASE[index] = (uint16_t)c | (uint16_t)color << 8;
}

static void vga_scroll_if_needed(void) {
    if (cursor_y < VGA_HEIGHT) return;

    /* scroll up by one row */
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_TEXT_MODE_BASE[(y - 1) * VGA_WIDTH + x] = VGA_TEXT_MODE_BASE[y * VGA_WIDTH + x];
        }
    }

    /* clear last row */
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_put_entry_at(' ', vga_color, (uint8_t)x, VGA_HEIGHT - 1);
    }

    cursor_y = VGA_HEIGHT - 1;
}

static void vga_newline(void) {
    cursor_x = 0;
    cursor_y++;
    vga_scroll_if_needed();
}

void console_init(void) {
    serial_init();

    /* clear screen */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_put_entry_at(' ', vga_color, (uint8_t)x, (uint8_t)y);
        }
    }
    cursor_x = 0;
    cursor_y = 0;

    console_write("TinyOS64 booting...\n");
}

void console_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

uint8_t console_get_color(void) {
    return vga_color;
}

void console_set_framebuffer(const console_fb_info_t* info) {
    if (!info || !info->base || info->width == 0 || info->height == 0) {
        fb_enabled = 0;
        return;
    }
    fb_info = *info;
    fb_enabled = 1;
}

void console_putc(char c) {
    serial_putc(c);

    if (c == '\n') {
        vga_newline();
        return;
    } else if (c == '\r') {
        cursor_x = 0;
        return;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3u;
        if (cursor_x >= VGA_WIDTH) vga_newline();
        return;
    }

    vga_put_entry_at(c, vga_color, cursor_x, cursor_y);
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        vga_newline();
    }
}

void console_write(const char* s) {
    for (size_t i = 0; s[i]; i++) console_putc(s[i]);
}

static void console_write_hex_n(uint64_t v, int nibbles) {
    static const char* hex = "0123456789ABCDEF";
    for (int i = (nibbles - 1); i >= 0; i--) {
        uint8_t nib = (v >> (i * 4)) & 0xF;
        console_putc(hex[nib]);
    }
}

void console_write_hex64(uint64_t v) {
    console_write("0x");
    console_write_hex_n(v, 16);
}

void console_write_hex32(uint32_t v) {
    console_write("0x");
    console_write_hex_n(v, 8);
}

void console_write_dec_u64(uint64_t v) {
    char buf[32];
    size_t i = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0 && i < sizeof(buf)) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i > 0) console_putc(buf[--i]);
}

void console_write_dec_u32(uint32_t v) {
    console_write_dec_u64((uint64_t)v);
}

void console_draw_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!fb_enabled) return;
    if (x >= fb_info.width || y >= fb_info.height) return;

    uint8_t* base = (uint8_t*)fb_info.base + fb_info.pitch * y;
    if (fb_info.bpp == 32) {
        uint32_t* p = (uint32_t*)(base + x * 4u);
        *p = rgb;
    } else if (fb_info.bpp == 24) {
        uint8_t* p = base + x * 3u;
        p[0] = (uint8_t)(rgb & 0xFF);
        p[1] = (uint8_t)((rgb >> 8) & 0xFF);
        p[2] = (uint8_t)((rgb >> 16) & 0xFF);
    } else if (fb_info.bpp == 16) {
        uint16_t r = (uint16_t)((rgb >> 19) & 0x1F);
        uint16_t g = (uint16_t)((rgb >> 10) & 0x3F);
        uint16_t b = (uint16_t)((rgb >> 3) & 0x1F);
        uint16_t color = (uint16_t)((r << 11) | (g << 5) | b);
        uint16_t* p = (uint16_t*)(base + x * 2u);
        *p = color;
    }
}

void console_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (!fb_enabled) return;
    for (uint32_t yy = 0; yy < h; yy++) {
        for (uint32_t xx = 0; xx < w; xx++) {
            console_draw_pixel(x + xx, y + yy, rgb);
        }
    }
}
