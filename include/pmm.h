#pragma once
#include <stdint.h>
#include <stddef.h>
#include "multiboot2.h"

#define PAGE_SIZE 4096ULL

void pmm_init(const mb2_info_t* mb2);
uint64_t pmm_total_memory_bytes(void);
uint64_t pmm_free_memory_bytes(void);

/* Allocate/free contiguous physical pages. Returns physical address (identity-mapped in this kernel). */
uint64_t pmm_alloc_pages(size_t pages);
void     pmm_free_pages(uint64_t addr, size_t pages);

/* Mark a range used (for ELF load, etc). */
void pmm_reserve_range(uint64_t addr, uint64_t size);

/* Mark a range free (rarely needed after init). */
void pmm_free_range(uint64_t addr, uint64_t size);
