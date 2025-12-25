#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VMM_FLAG_PRESENT   (1ULL << 0)
#define VMM_FLAG_WRITABLE  (1ULL << 1)
#define VMM_FLAG_USER      (1ULL << 2)
#define VMM_FLAG_WRITE_THR (1ULL << 3)
#define VMM_FLAG_CACHE_DIS (1ULL << 4)
#define VMM_FLAG_ACCESSED  (1ULL << 5)
#define VMM_FLAG_DIRTY     (1ULL << 6)
#define VMM_FLAG_HUGE      (1ULL << 7)
#define VMM_FLAG_GLOBAL    (1ULL << 8)
#define VMM_FLAG_NOEXEC    (1ULL << 63)

#define USER_REGION_BASE 0x0000008000000000ULL
#define USER_STACK_TOP   (USER_REGION_BASE + 0x0000007FFFFFF000ULL)

struct thread;

void     vmm_init(void);
uint64_t vmm_kernel_cr3(void);
uint64_t vmm_create_user_space(void);

bool vmm_map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags);
bool vmm_map_range(uint64_t cr3, uint64_t virt, uint64_t phys, size_t size, uint64_t flags);
uint64_t vmm_unmap_page(uint64_t cr3, uint64_t virt);
bool vmm_resolve(uint64_t cr3, uint64_t virt, uint64_t* out_phys, uint64_t* out_flags);

/* Simple heap grow/shrink for user brk handling. */
bool vmm_user_set_brk(struct thread* t, uint64_t new_end);
