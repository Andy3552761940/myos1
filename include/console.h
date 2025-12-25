#pragma once
#include <stdint.h>
#include <stddef.h>

void console_init(void);
void console_putc(char c);
void console_putc_vga(char c);
void console_write(const char* s);
void console_write_vga(const char* s);
void console_write_hex64(uint64_t v);
void console_write_dec_u64(uint64_t v);

void console_write_hex32(uint32_t v);
void console_write_dec_u32(uint32_t v);

typedef struct {
    void* base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t type;
} console_fb_info_t;

void console_set_color(uint8_t fg, uint8_t bg);
uint8_t console_get_color(void);
void console_set_framebuffer(const console_fb_info_t* info);
void console_draw_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void console_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);

void console_set_serial_enabled(int enabled);
int console_get_serial_enabled(void);
