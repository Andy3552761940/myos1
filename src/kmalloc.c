#include "kmalloc.h"
#include "pmm.h"
#include "lib.h"

typedef struct km_block {
    struct km_block* next;
    size_t size;
    int free;
} km_block_t;

static km_block_t* g_heap = 0;
static km_block_t* g_last = 0;

static km_block_t* request_space(size_t size) {
    size_t total = sizeof(km_block_t) + size;
    size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t pa = pmm_alloc_pages(pages);
    if (!pa) return 0;
    memset((void*)(uintptr_t)pa, 0, pages * PAGE_SIZE);

    km_block_t* block = (km_block_t*)(uintptr_t)pa;
    block->size = pages * PAGE_SIZE - sizeof(km_block_t);
    block->free = 0;
    block->next = 0;

    if (g_last) g_last->next = block;
    g_last = block;
    if (!g_heap) g_heap = block;
    return block;
}

void kmalloc_init(void) {
    g_heap = 0;
    g_last = 0;
}

static km_block_t* find_block(size_t size) {
    km_block_t* cur = g_heap;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;
    size = align_up_u64(size, 16);

    km_block_t* block = find_block(size);
    if (!block) {
        block = request_space(size);
        if (!block) return 0;
    } else {
        block->free = 0;
    }

    uint8_t* data = (uint8_t*)(block + 1);
    return data;
}

static void merge_free_blocks(void) {
    km_block_t* cur = g_heap;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(km_block_t) + cur->next->size;
            cur->next = cur->next->next;
            if (cur->next == 0) g_last = cur;
        } else {
            cur = cur->next;
        }
    }
}

void kfree(void* ptr) {
    if (!ptr) return;
    km_block_t* block = ((km_block_t*)ptr) - 1;
    block->free = 1;
    merge_free_blocks();
}
