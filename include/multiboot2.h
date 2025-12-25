#pragma once
#include <stdint.h>

/* Multiboot2 info structure basics */

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

enum {
    MB2_TAG_END        = 0,
    MB2_TAG_CMDLINE    = 1,
    MB2_TAG_BOOTLOADER = 2,
    MB2_TAG_MMAP       = 6,
    MB2_TAG_FRAMEBUFFER = 8,
};

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} mb2_tag_mmap_t;

typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} mb2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} mb2_tag_framebuffer_t;

static inline uint32_t mb2_align8(uint32_t x) {
    return (x + 7u) & ~7u;
}
