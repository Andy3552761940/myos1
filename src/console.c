#include "console.h"
#include "lib.h"
#include "serial.h"

#define VGA_TEXT_MODE_BASE ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

#define SCROLLBACK_LINES 512
#define TOTAL_LINES (SCROLLBACK_LINES + VGA_HEIGHT)

static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;
static uint8_t vga_color = 0x0F; /* white on black */

static int serial_enabled = 1;

static console_fb_info_t fb_info;
static int fb_enabled = 0;

static uint16_t scrollback_buf[TOTAL_LINES][VGA_WIDTH];
static size_t scrollback_first_line = 0;    /* global index of the oldest stored line */
static size_t scrollback_first_slot = 0;    /* ring-buffer slot that holds first_line */
static size_t scrollback_line_count = VGA_HEIGHT; /* how many lines are currently valid */
static size_t cursor_line_index = 0;        /* global line index of the active cursor line */
static size_t view_offset = 0;              /* 0 = follow output, >0 = lines above newest */

static uint16_t make_vga_entry(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static size_t line_slot_for_index(size_t line_index) {
    size_t delta = line_index - scrollback_first_line;
    delta %= TOTAL_LINES;
    return (scrollback_first_slot + delta) % TOTAL_LINES;
}

static void clear_line_slot(size_t slot) {
    uint16_t blank = make_vga_entry(' ', vga_color);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        scrollback_buf[slot][x] = blank;
    }
}

static void clamp_view_offset(void) {
    size_t newest_start = 0;
    if (cursor_line_index + 1 > VGA_HEIGHT) {
        newest_start = cursor_line_index + 1 - VGA_HEIGHT;
    }
    if (newest_start < scrollback_first_line) newest_start = scrollback_first_line;

    size_t max_offset = newest_start - scrollback_first_line;
    if (view_offset > max_offset) view_offset = max_offset;
}

static size_t display_start_line(void) {
    size_t newest_start = 0;
    if (cursor_line_index + 1 > VGA_HEIGHT) {
        newest_start = cursor_line_index + 1 - VGA_HEIGHT;
    }
    if (newest_start < scrollback_first_line) newest_start = scrollback_first_line;

    clamp_view_offset();
    return newest_start - view_offset;
}

static void render_view(void) {
    size_t start_line = display_start_line();
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line_index = start_line + y;
        size_t slot = line_slot_for_index(line_index);
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_TEXT_MODE_BASE[y * VGA_WIDTH + x] = scrollback_buf[slot][x];
        }
    }

    if (cursor_line_index >= start_line) {
        size_t rel = cursor_line_index - start_line;
        cursor_y = (rel < VGA_HEIGHT) ? (uint8_t)rel : (VGA_HEIGHT - 1);
    } else {
        cursor_y = 0;
    }
}

static void drop_oldest_line_if_needed(void) {
    if (scrollback_line_count <= TOTAL_LINES) return;

    scrollback_first_slot = (scrollback_first_slot + 1) % TOTAL_LINES;
    scrollback_first_line++;
    scrollback_line_count--;
    clamp_view_offset();
}

static void ensure_line_available(size_t line_index) {
    while (line_index >= scrollback_first_line + scrollback_line_count) {
        size_t new_slot = line_slot_for_index(scrollback_first_line + scrollback_line_count);
        clear_line_slot(new_slot);
        scrollback_line_count++;
        drop_oldest_line_if_needed();
    }
}

static void start_new_line(void) {
    cursor_x = 0;
    cursor_line_index++;
    ensure_line_available(cursor_line_index);
}

static void write_char_at_cursor(char c) {
    ensure_line_available(cursor_line_index);
    size_t slot = line_slot_for_index(cursor_line_index);
    scrollback_buf[slot][cursor_x] = make_vga_entry(c, vga_color);
}

void console_init(void) {
    serial_init();

    for (size_t i = 0; i < TOTAL_LINES; i++) {
        clear_line_slot(i);
    }
    scrollback_first_line = 0;
    scrollback_first_slot = 0;
    scrollback_line_count = VGA_HEIGHT;
    cursor_line_index = 0;
    view_offset = 0;
    cursor_x = 0;
    cursor_y = 0;

    render_view();
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

void console_set_serial_enabled(int enabled) {
    serial_enabled = (enabled != 0);
}

int console_get_serial_enabled(void) {
    return serial_enabled;
}

void console_scroll_view(int lines) {
    if (lines == 0) return;

    size_t newest_start = 0;
    if (cursor_line_index + 1 > VGA_HEIGHT) {
        newest_start = cursor_line_index + 1 - VGA_HEIGHT;
    }
    if (newest_start < scrollback_first_line) newest_start = scrollback_first_line;

    size_t max_offset = newest_start - scrollback_first_line;

    if (lines > 0) {
        size_t delta = (size_t)lines;
        if (view_offset + delta > max_offset) {
            view_offset = max_offset;
        } else {
            view_offset += delta;
        }
    } else {
        size_t delta = (size_t)(-lines);
        if (delta > view_offset) {
            view_offset = 0;
        } else {
            view_offset -= delta;
        }
    }

    render_view();
}

void console_putc_vga(char c) {
    if (c == '\n') {
        start_new_line();
        render_view();
        return;
    } else if (c == '\r') {
        cursor_x = 0;
        render_view();
        return;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3u;
        if (cursor_x >= VGA_WIDTH) start_new_line();
        render_view();
        return;
    }

    write_char_at_cursor(c);
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        start_new_line();
    }
    render_view();
}

void console_putc(char c) {
    if (serial_enabled) serial_putc(c);
    console_putc_vga(c);
}

void console_write_vga(const char* s) {
    for (size_t i = 0; s[i]; i++) console_putc_vga(s[i]);
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
