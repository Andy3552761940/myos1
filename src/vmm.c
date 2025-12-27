#include "vmm.h"
#include "pmm.h"
#include "console.h"
#include "lib.h"
#include "thread.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/spinlock.h"

#define ENTRIES_PER_TABLE 512

typedef struct {
    uint64_t cr3;
    uint32_t refs;
} vmm_space_t;

#define VMM_MAX_USER_SPACES 64
#define VMM_ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t g_kernel_cr3 = 0;
static uint64_t* pml4_from_phys(uint64_t phys) { return (uint64_t*)(uintptr_t)phys; }
static vmm_space_t g_user_spaces[VMM_MAX_USER_SPACES];
static spinlock_t g_space_lock;

static inline void tlb_invalidate(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint64_t alloc_zero_page(void) {
    uint64_t pa = pmm_alloc_pages(1);
    if (!pa) return 0;
    memset((void*)(uintptr_t)pa, 0, PAGE_SIZE);
    return pa;
}

static int find_space_slot(uint64_t cr3) {
    for (size_t i = 0; i < VMM_MAX_USER_SPACES; i++) {
        if (g_user_spaces[i].cr3 == cr3 && g_user_spaces[i].refs) return (int)i;
    }
    return -1;
}

static int find_free_space_slot(void) {
    for (size_t i = 0; i < VMM_MAX_USER_SPACES; i++) {
        if (g_user_spaces[i].refs == 0) return (int)i;
    }
    return -1;
}

static bool ensure_table(uint64_t* parent, size_t idx, uint64_t flags, uint64_t* out_phys) {
    uint64_t entry = parent[idx];
    if (entry & VMM_FLAG_PRESENT) {
        *out_phys = entry & VMM_ADDR_MASK;
        return true;
    }

    uint64_t pa = alloc_zero_page();
    if (!pa) return false;
    parent[idx] = pa | flags;
    *out_phys = pa;
    return true;
}

static bool map_page_inner(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pml4 = pml4_from_phys(cr3 & ~0xFFFULL);
    size_t l4 = (virt >> 39) & 0x1FF;
    size_t l3 = (virt >> 30) & 0x1FF;
    size_t l2 = (virt >> 21) & 0x1FF;
    size_t l1 = (virt >> 12) & 0x1FF;

    uint64_t pdpt_phys = 0;
    if (!ensure_table(pml4, l4, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | ((flags & VMM_FLAG_USER) ? VMM_FLAG_USER : 0), &pdpt_phys)) return false;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;

    uint64_t pd_phys = 0;
    if (!ensure_table(pdpt, l3, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | ((flags & VMM_FLAG_USER) ? VMM_FLAG_USER : 0), &pd_phys)) return false;
    uint64_t* pd = (uint64_t*)(uintptr_t)pd_phys;

    uint64_t pt_phys = 0;
    if (!ensure_table(pd, l2, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | ((flags & VMM_FLAG_USER) ? VMM_FLAG_USER : 0), &pt_phys)) return false;
    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;

    if (pt[l1] & VMM_FLAG_PRESENT) return false; /* already mapped */

    uint64_t entry_flags = flags | VMM_FLAG_PRESENT;
    pt[l1] = (phys & VMM_ADDR_MASK) | entry_flags;
    tlb_invalidate(virt);
    return true;
}

bool vmm_map_page(uint64_t cr3, uint64_t virt, uint64_t phys, uint64_t flags) {
    return map_page_inner(cr3, virt, phys, flags);
}

bool vmm_map_range(uint64_t cr3, uint64_t virt, uint64_t phys, size_t size, uint64_t flags) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        if (!map_page_inner(cr3, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags)) {
            return false;
        }
    }
    return true;
}

uint64_t vmm_unmap_page(uint64_t cr3, uint64_t virt) {
    uint64_t* pml4 = pml4_from_phys(cr3 & ~0xFFFULL);
    size_t l4 = (virt >> 39) & 0x1FF;
    size_t l3 = (virt >> 30) & 0x1FF;
    size_t l2 = (virt >> 21) & 0x1FF;
    size_t l1 = (virt >> 12) & 0x1FF;

    uint64_t pdpt_phys = pml4[l4] & VMM_ADDR_MASK;
    if (!(pml4[l4] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;

    uint64_t pd_phys = pdpt[l3] & VMM_ADDR_MASK;
    if (!(pdpt[l3] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)(uintptr_t)pd_phys;

    uint64_t pt_phys = pd[l2] & VMM_ADDR_MASK;
    if (!(pd[l2] & VMM_FLAG_PRESENT)) return 0;
    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;

    uint64_t entry = pt[l1];
    if (!(entry & VMM_FLAG_PRESENT)) return 0;

    pt[l1] = 0;
    tlb_invalidate(virt);
    return entry & VMM_ADDR_MASK;
}

bool vmm_resolve(uint64_t cr3, uint64_t virt, uint64_t* out_phys, uint64_t* out_flags) {
    uint64_t* pml4 = pml4_from_phys(cr3 & ~0xFFFULL);
    size_t l4 = (virt >> 39) & 0x1FF;
    size_t l3 = (virt >> 30) & 0x1FF;
    size_t l2 = (virt >> 21) & 0x1FF;
    size_t l1 = (virt >> 12) & 0x1FF;

    uint64_t e4 = pml4[l4];
    if (!(e4 & VMM_FLAG_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)(e4 & VMM_ADDR_MASK);

    uint64_t e3 = pdpt[l3];
    if (!(e3 & VMM_FLAG_PRESENT)) return false;
    if (e3 & VMM_FLAG_HUGE) {
        uint64_t pa = (e3 & VMM_ADDR_MASK) + (virt & 0x3FFFFFFFULL);
        if (out_phys) *out_phys = pa;
        if (out_flags) *out_flags = e3;
        return true;
    }
    uint64_t* pd = (uint64_t*)(uintptr_t)(e3 & VMM_ADDR_MASK);

    uint64_t e2 = pd[l2];
    if (!(e2 & VMM_FLAG_PRESENT)) return false;
    if (e2 & VMM_FLAG_HUGE) {
        uint64_t pa = (e2 & VMM_ADDR_MASK) + (virt & 0x1FFFFFULL);
        if (out_phys) *out_phys = pa;
        if (out_flags) *out_flags = e2;
        return true;
    }
    uint64_t* pt = (uint64_t*)(uintptr_t)(e2 & VMM_ADDR_MASK);

    uint64_t e1 = pt[l1];
    if (!(e1 & VMM_FLAG_PRESENT)) return false;
    if (out_phys) *out_phys = (e1 & VMM_ADDR_MASK) | (virt & 0xFFFULL);
    if (out_flags) *out_flags = e1;
    return true;
}

static void vmm_free_pt(uint64_t pt_phys) {
    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;
    for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
        uint64_t entry = pt[i];
        if (!(entry & VMM_FLAG_PRESENT)) continue;
        uint64_t pa = entry & VMM_ADDR_MASK;
        if (pa) pmm_free_pages(pa, 1);
        pt[i] = 0;
    }
    pmm_free_pages(pt_phys, 1);
}

static void vmm_free_pd(uint64_t pd_phys) {
    uint64_t* pd = (uint64_t*)(uintptr_t)pd_phys;
    for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
        uint64_t entry = pd[i];
        if (!(entry & VMM_FLAG_PRESENT)) continue;
        uint64_t pa = entry & VMM_ADDR_MASK;
        if (entry & VMM_FLAG_HUGE) {
            if (pa) pmm_free_pages(pa, 512);
        } else {
            vmm_free_pt(pa);
        }
        pd[i] = 0;
    }
    pmm_free_pages(pd_phys, 1);
}

static void vmm_free_pdpt(uint64_t pdpt_phys) {
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;
    for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
        uint64_t entry = pdpt[i];
        if (!(entry & VMM_FLAG_PRESENT)) continue;
        uint64_t pa = entry & VMM_ADDR_MASK;
        if (entry & VMM_FLAG_HUGE) {
            if (pa) pmm_free_pages(pa, 512 * 512);
        } else {
            vmm_free_pd(pa);
        }
        pdpt[i] = 0;
    }
    pmm_free_pages(pdpt_phys, 1);
}

static void vmm_destroy_user_space(uint64_t cr3) {
    if (!cr3 || cr3 == g_kernel_cr3) return;
    uint64_t pml4_phys = cr3 & VMM_ADDR_MASK;
    uint64_t* pml4 = pml4_from_phys(pml4_phys);
    for (size_t i = 1; i < ENTRIES_PER_TABLE; i++) {
        uint64_t entry = pml4[i];
        if (!(entry & VMM_FLAG_PRESENT)) continue;
        uint64_t pdpt_phys = entry & VMM_ADDR_MASK;
        vmm_free_pdpt(pdpt_phys);
        pml4[i] = 0;
    }
    pmm_free_pages(pml4_phys, 1);
}

static void map_identity_kernel(uint64_t pml4_phys) {
    uint64_t* pml4 = pml4_from_phys(pml4_phys);

    uint64_t pdpt_phys = alloc_zero_page();
    if (!pdpt_phys) return;
    pml4[0] = pdpt_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_GLOBAL;
    uint64_t* pdpt = (uint64_t*)(uintptr_t)pdpt_phys;

    for (size_t gi = 0; gi < 4; gi++) {
        uint64_t pd_phys = alloc_zero_page();
        if (!pd_phys) return;
        pdpt[gi] = pd_phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_GLOBAL;
        uint64_t* pd = (uint64_t*)(uintptr_t)pd_phys;
        for (size_t i = 0; i < ENTRIES_PER_TABLE; i++) {
            uint64_t phys = ((uint64_t)gi * 0x40000000ULL) + (uint64_t)i * 0x200000ULL;
            pd[i] = phys | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_HUGE | VMM_FLAG_GLOBAL;
        }
    }
}

void vmm_init(void) {
    spinlock_init(&g_space_lock);
    memset(g_user_spaces, 0, sizeof(g_user_spaces));

    uint64_t pml4_phys = alloc_zero_page();
    if (!pml4_phys) {
        console_write("[vmm] ERROR: cannot allocate kernel PML4\n");
        return;
    }

    map_identity_kernel(pml4_phys);
    g_kernel_cr3 = pml4_phys;

    write_cr3(g_kernel_cr3);

    console_write("[vmm] kernel CR3=");
    console_write_hex64(g_kernel_cr3);
    console_write("\n");
}

uint64_t vmm_kernel_cr3(void) { return g_kernel_cr3; }

uint64_t vmm_create_user_space(void) {
    uint64_t pml4_phys = alloc_zero_page();
    if (!pml4_phys) return 0;

    uint64_t* new_pml4 = pml4_from_phys(pml4_phys);
    uint64_t* kernel_pml4 = pml4_from_phys(g_kernel_cr3);
    /* Copy kernel entry 0 (identity map, supervisor-only). */
    new_pml4[0] = kernel_pml4[0];

    spinlock_lock(&g_space_lock);
    int slot = find_free_space_slot();
    if (slot >= 0) {
        g_user_spaces[slot].cr3 = pml4_phys;
        g_user_spaces[slot].refs = 1;
    }
    spinlock_unlock(&g_space_lock);
    if (slot < 0) {
        pmm_free_pages(pml4_phys, 1);
        return 0;
    }
    return pml4_phys;
}

void vmm_retain_user_space(uint64_t cr3) {
    if (!cr3 || cr3 == g_kernel_cr3) return;
    spinlock_lock(&g_space_lock);
    int slot = find_space_slot(cr3);
    if (slot >= 0) {
        g_user_spaces[slot].refs++;
        spinlock_unlock(&g_space_lock);
        return;
    }
    slot = find_free_space_slot();
    if (slot >= 0) {
        g_user_spaces[slot].cr3 = cr3;
        g_user_spaces[slot].refs = 2;
    }
    spinlock_unlock(&g_space_lock);
}

void vmm_release_user_space(uint64_t cr3) {
    if (!cr3 || cr3 == g_kernel_cr3) return;
    bool destroy = false;

    spinlock_lock(&g_space_lock);
    int slot = find_space_slot(cr3);
    if (slot >= 0 && g_user_spaces[slot].refs) {
        g_user_spaces[slot].refs--;
        if (g_user_spaces[slot].refs == 0) {
            g_user_spaces[slot].cr3 = 0;
            destroy = true;
        }
    }
    spinlock_unlock(&g_space_lock);

    if (destroy) {
        vmm_destroy_user_space(cr3);
    }
}

bool vmm_user_set_brk(thread_t* t, uint64_t new_end) {
    if (!t || !t->is_user) return false;
    uint64_t start = align_up_u64(t->brk_start, PAGE_SIZE);
    uint64_t new_aligned = align_up_u64(new_end, PAGE_SIZE);
    if (new_aligned < start) new_aligned = start;

    uint64_t cur = align_up_u64(t->brk_end, PAGE_SIZE);
    uint64_t cr3 = t->cr3;

    if (new_aligned > cur) {
        for (uint64_t va = cur; va < new_aligned; va += PAGE_SIZE) {
            uint64_t pa = pmm_alloc_pages(1);
            if (!pa) return false;
            memset((void*)(uintptr_t)pa, 0, PAGE_SIZE);
            if (!vmm_map_page(cr3, va, pa, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
                pmm_free_pages(pa, 1);
                return false;
            }
        }
    } else if (new_aligned < cur) {
        for (uint64_t va = new_aligned; va < cur; va += PAGE_SIZE) {
            uint64_t pa = vmm_unmap_page(cr3, va);
            if (pa) pmm_free_pages(pa, 1);
        }
    }

    t->brk_end = new_end;
    return true;
}
