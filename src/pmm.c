#include "pmm.h"
#include "console.h"
#include "lib.h"

#define MAX_PHYS_BYTES (4ULL * 1024 * 1024 * 1024) /* 4 GiB identity mapped */
#define MAX_PAGES      (MAX_PHYS_BYTES / PAGE_SIZE)
#define BITMAP_BYTES   (MAX_PAGES / 8)

static uint8_t pmm_bitmap[BITMAP_BYTES];
static uint64_t pmm_free_pages_cnt = 0;
static uint64_t pmm_total_pages_cnt = 0;
static int pmm_ready = 0;

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static inline void bit_set(uint64_t idx) {
    pmm_bitmap[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}
static inline void bit_clear(uint64_t idx) {
    pmm_bitmap[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}
static inline int bit_test(uint64_t idx) {
    return (pmm_bitmap[idx >> 3] >> (idx & 7)) & 1u;
}

static void pmm_recount_free(void) {
    uint64_t free_pages = 0;
    for (uint64_t i = 0; i < pmm_total_pages_cnt; i++) {
        if (!bit_test(i)) free_pages++;
    }
    pmm_free_pages_cnt = free_pages;
}

void pmm_free_range(uint64_t addr, uint64_t size) {
    uint64_t start = align_up_u64(addr, PAGE_SIZE);
    uint64_t end   = align_down_u64(addr + size, PAGE_SIZE);
    if (end <= start) return;
    if (start >= MAX_PHYS_BYTES) return;
    if (end > MAX_PHYS_BYTES) end = MAX_PHYS_BYTES;

    for (uint64_t a = start; a < end; a += PAGE_SIZE) {
        uint64_t page = a / PAGE_SIZE;
        if (page >= pmm_total_pages_cnt) break;
        if (bit_test(page)) {
            bit_clear(page);
            if (pmm_ready) pmm_free_pages_cnt++;
        }
    }
}

void pmm_reserve_range(uint64_t addr, uint64_t size) {
    uint64_t start = align_down_u64(addr, PAGE_SIZE);
    uint64_t end   = align_up_u64(addr + size, PAGE_SIZE);
    if (end <= start) return;
    if (start >= MAX_PHYS_BYTES) return;
    if (end > MAX_PHYS_BYTES) end = MAX_PHYS_BYTES;

    for (uint64_t a = start; a < end; a += PAGE_SIZE) {
        uint64_t page = a / PAGE_SIZE;
        if (page >= pmm_total_pages_cnt) break;
        if (!bit_test(page)) {
            bit_set(page);
            if (pmm_ready && pmm_free_pages_cnt) pmm_free_pages_cnt--;
        }
    }
}

void pmm_init(const mb2_info_t* mb2) {
    /* Mark everything used initially. */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));

    /* Default: assume 4GiB max tracked. */
    pmm_total_pages_cnt = MAX_PAGES;

    /* Find memory map tag. */
    const mb2_tag_t* tag = (const mb2_tag_t*)((const uint8_t*)mb2 + 8);
    const mb2_tag_mmap_t* mmap = 0;

    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MMAP) {
            mmap = (const mb2_tag_mmap_t*)tag;
            break;
        }
        tag = (const mb2_tag_t*)((const uint8_t*)tag + mb2_align8(tag->size));
    }

    if (!mmap) {
        console_write("[pmm] ERROR: no multiboot2 memory map; keeping all pages reserved.\n");
        pmm_recount_free();
        return;
    }

    /* Free all "available" pages from the multiboot map. */
    const uint8_t* end = (const uint8_t*)mmap + mmap->size;
    for (const uint8_t* p = (const uint8_t*)mmap + sizeof(mb2_tag_mmap_t);
         p < end;
         p += mmap->entry_size) {
        const mb2_mmap_entry_t* e = (const mb2_mmap_entry_t*)p;
        if (e->type == 1) { /* available */
            pmm_free_range(e->addr, e->len);
        }
    }

    /* Reserve low 1MiB (BIOS, real-mode stuff, VGA, etc.). */
    pmm_reserve_range(0, 0x100000);

    /* Reserve kernel image. */
    uint64_t kstart = (uint64_t)(uintptr_t)_kernel_start;
    uint64_t kend   = (uint64_t)(uintptr_t)_kernel_end;
    pmm_reserve_range(kstart, kend - kstart);

    /* Reserve multiboot info. */
    pmm_reserve_range((uint64_t)(uintptr_t)mb2, mb2->total_size);

    pmm_recount_free();
    pmm_ready = 1;

    /* Compute total pages that are actually usable under MAX_PHYS by scanning bitmap */
    uint64_t used = pmm_total_pages_cnt - pmm_free_pages_cnt;

    console_write("[pmm] kernel: ");
    console_write_hex64(kstart);
    console_write(" - ");
    console_write_hex64(kend);
    console_write("\n");

    console_write("[pmm] total tracked pages: ");
    console_write_dec_u64(pmm_total_pages_cnt);
    console_write(", used: ");
    console_write_dec_u64(used);
    console_write(", free: ");
    console_write_dec_u64(pmm_free_pages_cnt);
    console_write("\n");
}

uint64_t pmm_total_memory_bytes(void) {
    return pmm_total_pages_cnt * PAGE_SIZE;
}

uint64_t pmm_free_memory_bytes(void) {
    return pmm_free_pages_cnt * PAGE_SIZE;
}

uint64_t pmm_alloc_pages(size_t pages) {
    if (pages == 0) return 0;
    if (pages > pmm_free_pages_cnt) return 0;

    uint64_t run = 0;
    uint64_t start = 0;

    for (uint64_t i = 0; i < pmm_total_pages_cnt; i++) {
        if (!bit_test(i)) {
            if (run == 0) start = i;
            run++;
            if (run == pages) {
                for (uint64_t j = 0; j < pages; j++) bit_set(start + j);
                pmm_free_pages_cnt -= pages;
                return start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }

    return 0;
}

void pmm_free_pages(uint64_t addr, size_t pages) {
    if (pages == 0) return;
    if (addr == 0) return;

    uint64_t start = addr / PAGE_SIZE;
    for (uint64_t j = 0; j < pages; j++) {
        uint64_t idx = start + j;
        if (idx >= pmm_total_pages_cnt) break;
        if (bit_test(idx)) {
            bit_clear(idx);
            pmm_free_pages_cnt++;
        }
    }
}
