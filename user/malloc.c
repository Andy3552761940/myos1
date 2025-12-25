#include "malloc.h"
#include "syscall.h"
#include <stdint.h>

typedef struct ublock {
    struct ublock* next;
    size_t size;
    int free;
} ublock_t;

extern uint8_t _end[];
static ublock_t* g_head = 0;
static void* g_program_break = 0;

static void* sbrk(size_t inc) {
    if (!g_program_break) {
        g_program_break = _end;
        sys_brk(g_program_break);
    }
    void* old = g_program_break;
    void* new_brk = (uint8_t*)g_program_break + inc;
    if (sys_brk(new_brk) == -1) return (void*)-1;
    g_program_break = new_brk;
    return old;
}

static ublock_t* request_space(size_t size) {
    size_t total = sizeof(ublock_t) + size;
    total = (total + 15) & ~((size_t)15);
    ublock_t* block = (ublock_t*)sbrk(total);
    if (block == (void*)-1) return 0;
    block->next = 0;
    block->free = 0;
    block->size = total - sizeof(ublock_t);

    if (g_head) {
        ublock_t* cur = g_head;
        while (cur->next) cur = cur->next;
        cur->next = block;
    } else {
        g_head = block;
    }
    return block;
}

static ublock_t* find_block(size_t size) {
    ublock_t* cur = g_head;
    while (cur) {
        if (cur->free && cur->size >= size) return cur;
        cur = cur->next;
    }
    return 0;
}

void* malloc(size_t size) {
    if (size == 0) return 0;
    size = (size + 15) & ~((size_t)15);

    ublock_t* block = find_block(size);
    if (!block) {
        block = request_space(size);
        if (!block) return 0;
    } else {
        block->free = 0;
    }
    return (void*)(block + 1);
}

static void merge(void) {
    ublock_t* cur = g_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(ublock_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void free(void* ptr) {
    if (!ptr) return;
    ublock_t* block = ((ublock_t*)ptr) - 1;
    block->free = 1;
    merge();
}
