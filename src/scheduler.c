#include "scheduler.h"
#include "pmm.h"
#include "console.h"
#include "lib.h"
#include "vmm.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/pit.h"

#define MAX_THREADS   64
#define KSTACK_PAGES  4   /* 16 KiB */
#define USTACK_PAGES  4   /* 16 KiB */

static thread_t g_threads[MAX_THREADS];
static thread_t* g_current = 0;
static uint64_t g_next_id = 1;

static uint64_t kspace_cr3 = 0;

thread_t* thread_current(void) {
    return g_current;
}

static void* alloc_pages(size_t pages) {
    uint64_t p = pmm_alloc_pages(pages);
    if (!p) return 0;
    return (void*)(uintptr_t)p; /* identity mapped */
}

static void build_kernel_thread_frame(thread_t* t, void (*fn)(void*), void* arg) {
    uint8_t* top = t->kstack + t->kstack_size;
    /* Make entry RSP = top-8 so a C function entered via iret sees RSP%16==8. */
    uint64_t* sp = (uint64_t*)(uintptr_t)((uint64_t)(uintptr_t)top - 8);

    /* iret frame (ring0): RIP, CS, RFLAGS */
    const uint64_t rflags = 0x202ULL; /* IF=1 */
    extern void thread_trampoline(void (*fn)(void*), void* arg);
    *(--sp) = rflags;
    *(--sp) = (uint64_t)GDT_SEL_KCODE;
    *(--sp) = (uint64_t)(uintptr_t)thread_trampoline;

    /* err_code + int_no (discarded by common stub) */
    *(--sp) = 0; /* err_code */
    *(--sp) = 0; /* int_no */

    /* General registers saved by common stub (will be popped before iret).
       Push in the exact order used by the assembly stub: rax..r15 (last). */
    *(--sp) = 0; /* rax */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rcx */
    *(--sp) = 0; /* rdx */
    *(--sp) = 0; /* rbp */
    *(--sp) = (uint64_t)(uintptr_t)fn;  /* rdi = fn */
    *(--sp) = (uint64_t)(uintptr_t)arg; /* rsi = arg */
    *(--sp) = 0; /* r8  */
    *(--sp) = 0; /* r9  */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)(uintptr_t)sp;
}

static void build_user_thread_frame(thread_t* t, uint64_t user_rip) {
    uint8_t* ktop = t->kstack + t->kstack_size;
    uint64_t* sp = (uint64_t*)(uintptr_t)((uint64_t)(uintptr_t)ktop);

    /* User stack: choose 16-byte aligned top */
    uint64_t u_top = (uint64_t)(uintptr_t)t->ustack_top;
    u_top &= ~0xFULL;

    const uint64_t rflags = 0x202ULL;

    /* iret frame (ring3): RIP, CS, RFLAGS, RSP, SS */
    *(--sp) = (uint64_t)GDT_SEL_UDATA; /* SS */
    *(--sp) = u_top;                  /* RSP */
    *(--sp) = rflags;                 /* RFLAGS */
    *(--sp) = (uint64_t)GDT_SEL_UCODE;/* CS */
    *(--sp) = user_rip;               /* RIP */

    /* err_code + int_no */
    *(--sp) = 0;
    *(--sp) = 0;

    /* Registers */
    *(--sp) = 0; /* rax */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rcx */
    *(--sp) = 0; /* rdx */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rdi */
    *(--sp) = 0; /* rsi */
    *(--sp) = 0; /* r8  */
    *(--sp) = 0; /* r9  */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)(uintptr_t)sp;
}

void thread_trampoline(void (*fn)(void*), void* arg) {
    fn(arg);
    console_write("[thread] kernel thread returned; halting it.\n");
    for (;;) { cpu_hlt(); }
}

static thread_t* thread_alloc_slot(void) {
    for (size_t i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_UNUSED) {
            memset(&g_threads[i], 0, sizeof(thread_t));
            g_threads[i].state = THREAD_READY;
            g_threads[i].priority = 1;
            g_threads[i].id = g_next_id++;
            return &g_threads[i];
        }
    }
    return 0;
}

void scheduler_init(void) {
    memset(g_threads, 0, sizeof(g_threads));
    kspace_cr3 = vmm_kernel_cr3();

    /* Bootstrap thread = current execution context (kernel_main). */
    thread_t* t0 = &g_threads[0];
    memset(t0, 0, sizeof(thread_t));
    t0->id = 0;
    t0->state = THREAD_RUNNING;
    t0->is_user = false;
    t0->priority = 1;
    t0->cr3 = kspace_cr3;
    strncpy(t0->name, "bootstrap", sizeof(t0->name)-1);

    g_current = t0;

    /* RSP0 for privilege switches while still on bootstrap thread. */
    extern uint8_t stack_top[];
    tss_set_rsp0((uint64_t)(uintptr_t)stack_top);

    console_write("[sched] init, CR3=");
    console_write_hex64(kspace_cr3);
    console_write("\n");
}

void scheduler_add(thread_t* t) {
    (void)t;
    /* Threads are stored in a fixed array; nothing else needed. */
}

static thread_t* pick_next(void) {
    /* Priority-based pick: choose READY thread with highest priority. */
    thread_t* best = 0;
    int best_prio = -1;

    for (size_t i = 0; i < MAX_THREADS; i++) {
        thread_t* t = &g_threads[i];
        if (t->state != THREAD_READY) continue;
        if (!best || t->priority > best_prio) {
            best = t;
            best_prio = t->priority;
        }
    }

    if (best) return best;
    if (g_current && (g_current->state == THREAD_RUNNING || g_current->state == THREAD_READY)) {
        return g_current;
    }
    return &g_threads[0];
}

static void wake_sleepers(void) {
    uint64_t now = pit_ticks();
    for (size_t i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_SLEEPING && now >= g_threads[i].wakeup_tick) {
            g_threads[i].state = THREAD_READY;
        }
    }
}

static void thread_release_resources(thread_t* t) {
    if (!t) return;
    if (t->kstack) {
        pmm_free_pages((uint64_t)(uintptr_t)t->kstack, t->kstack_size / PAGE_SIZE);
        t->kstack = 0;
    }

    if (t->is_user && t->ustack) {
        uint64_t base = USER_STACK_TOP - t->ustack_size;
        for (uint64_t off = 0; off < t->ustack_size; off += PAGE_SIZE) {
            uint64_t pa = vmm_unmap_page(t->cr3, base + off);
            if (pa) pmm_free_pages(pa, 1);
        }
        t->ustack = 0;
    }

    if (t->is_user && t->cr3 && t->cr3 != kspace_cr3) {
        /* Leave CR3 for now; freeing full address space is not implemented. */
    }
}

static intr_frame_t* do_switch(intr_frame_t* frame, thread_t* next) {
    if (next == g_current) return frame;

    thread_t* prev = g_current;

    /* Save current context */
    prev->rsp = (uint64_t)(uintptr_t)frame;
    if (prev->state == THREAD_RUNNING) prev->state = THREAD_READY;

    /* Activate next */
    next->state = THREAD_RUNNING;
    g_current = next;

    /* Update RSP0 for privilege switches (user threads need it). */
    if (next->kstack) {
        uint64_t rsp0 = (uint64_t)(uintptr_t)(next->kstack + next->kstack_size);
        tss_set_rsp0(rsp0);
    }

    /* Switch address space if needed */
    if (next->cr3 && next->cr3 != prev->cr3) {
        write_cr3(next->cr3);
    }

    return (intr_frame_t*)(uintptr_t)next->rsp;
}

intr_frame_t* scheduler_on_tick(intr_frame_t* frame) {
    wake_sleepers();
    thread_t* next = pick_next();
    return do_switch(frame, next);
}

intr_frame_t* scheduler_fork(intr_frame_t* frame) {
    thread_t* parent = g_current;
    if (!parent || !parent->is_user) {
        frame->rax = (uint64_t)-1;
        return frame;
    }

    thread_t* child = thread_alloc_slot();
    if (!child) {
        frame->rax = (uint64_t)-1;
        return frame;
    }

    child->is_user = true;
    child->cr3 = parent->cr3;
    child->priority = parent->priority;
    child->parent = parent;
    parent->children++;
    child->ustack = parent->ustack;
    child->ustack_size = parent->ustack_size;
    child->ustack_top = parent->ustack_top;
    child->brk_start = parent->brk_start;
    child->brk_end = parent->brk_end;

    child->kstack_size = parent->kstack_size;
    child->kstack = (uint8_t*)pmm_alloc_pages(child->kstack_size / PAGE_SIZE);
    if (!child->kstack) {
        child->state = THREAD_UNUSED;
        frame->rax = (uint64_t)-1;
        return frame;
    }

    uint8_t* top = child->kstack + child->kstack_size;
    intr_frame_t* child_frame = (intr_frame_t*)(top - sizeof(intr_frame_t));
    *child_frame = *frame;
    child_frame->rax = 0;
    child->rsp = (uint64_t)(uintptr_t)child_frame;
    child->state = THREAD_READY;

    frame->rax = child->id;
    return frame;
}

intr_frame_t* scheduler_yield(intr_frame_t* frame) {
    wake_sleepers();
    thread_t* next = pick_next();
    return do_switch(frame, next);
}

intr_frame_t* scheduler_on_exit(intr_frame_t* frame, int exit_code) {
    thread_t* cur = g_current;
    console_write("[sched] thread ");
    console_write_dec_u64(cur->id);
    console_write(" exited\n");

    cur->rsp = (uint64_t)(uintptr_t)frame;
    cur->exit_code = exit_code;
    cur->state = THREAD_ZOMBIE;

    if (cur->parent) {
        if (cur->parent->children > 0) cur->parent->children--;
        /* Wake a waiting parent if it waits for us or for any child (pid<=0). */
        if (cur->parent->state == THREAD_BLOCKED &&
            (cur->parent->wait_target <= 0 || cur->parent->wait_target == (int)cur->id)) {
            if (cur->parent->wait_status_ptr) {
                int* sp = (int*)(uintptr_t)cur->parent->wait_status_ptr;
                *sp = cur->exit_code;
            }
            cur->parent->wait_target = 0;
            cur->parent->wait_status_ptr = 0;
            cur->parent->state = THREAD_READY;
        }
    }

    /* Pick next READY thread; if none, fall back to bootstrap (should be READY/RUNNING). */
    thread_t* next = pick_next();
    if (next == cur) {
        /* No other READY threads: run bootstrap. */
        next = &g_threads[0];
        if (next->state == THREAD_ZOMBIE) {
            console_write("[sched] no runnable threads; halting.\n");
            for(;;) cpu_hlt();
        }
    }

    return do_switch(frame, next);
}

void scheduler_sleep(uint64_t ticks) {
    /* Only usable from thread context if you have a way to reschedule; keep simple. */
    thread_t* cur = g_current;
    cur->wakeup_tick = pit_ticks() + ticks;
    cur->state = THREAD_SLEEPING;
    /* Force a yield via int 0x80 SYS_yield (works in ring0 too). */
    __asm__ volatile ("movq $3, %%rax; int $0x80" : : : "rax", "memory");
}

static thread_t* find_child(thread_t* parent, int pid, bool require_zombie) {
    for (size_t i = 0; i < MAX_THREADS; i++) {
        thread_t* t = &g_threads[i];
        if (t->state == THREAD_UNUSED) continue;
        if (t->parent != parent) continue;
        if (pid > 0 && (int)t->id != pid) continue;
        if (require_zombie && t->state != THREAD_ZOMBIE) continue;
        return t;
    }
    return 0;
}

intr_frame_t* scheduler_waitpid(intr_frame_t* frame, int pid, uint64_t status_ptr) {
    thread_t* cur = g_current;
    if (!cur) return frame;

    thread_t* zombie = find_child(cur, pid, true);
    if (zombie) {
        if (status_ptr) {
            int* sp = (int*)(uintptr_t)status_ptr;
            *sp = zombie->exit_code;
        }
        thread_release_resources(zombie);
        zombie->state = THREAD_UNUSED;
        frame->rax = zombie->id;
        return frame;
    }

    /* If there is no matching child at all, return -1. */
    thread_t* any_child = find_child(cur, pid, false);
    if (!any_child) {
        frame->rax = (uint64_t)-1;
        return frame;
    }

    /* Block current thread until a matching child exits. */
    cur->wait_target = pid;
    cur->wait_status_ptr = status_ptr;
    cur->state = THREAD_BLOCKED;
    return scheduler_yield(frame);
}

void scheduler_dump(void) {
    console_write("[sched] threads:\n");
    for (size_t i = 0; i < MAX_THREADS; i++) {
        thread_t* t = &g_threads[i];
        if (t->state == THREAD_UNUSED) continue;
        console_write("  id=");
        console_write_dec_u64(t->id);
        console_write(" name=");
        console_write(t->name);
        console_write(" state=");
        switch (t->state) {
            case THREAD_READY: console_write("READY"); break;
            case THREAD_RUNNING: console_write("RUNNING"); break;
            case THREAD_SLEEPING: console_write("SLEEP"); break;
            case THREAD_BLOCKED: console_write("BLOCK"); break;
            case THREAD_ZOMBIE: console_write("ZOMBIE"); break;
            default: console_write("UNUSED"); break;
        }
        console_write(" user=");
        console_write_dec_u64(t->is_user ? 1 : 0);
        console_write(" prio=");
        console_write_dec_u64((uint64_t)t->priority);
        console_write("\n");
    }
}

thread_t* thread_create_kernel(const char* name, void (*fn)(void*), void* arg) {
    thread_t* t = thread_alloc_slot();
    if (!t) return 0;

    t->is_user = false;
    t->cr3 = kspace_cr3;
    t->parent = g_current;
    if (t->parent) t->parent->children++;
    t->kstack_size = KSTACK_PAGES * PAGE_SIZE;
    t->kstack = (uint8_t*)alloc_pages(KSTACK_PAGES);
    if (!t->kstack) {
        t->state = THREAD_UNUSED;
        return 0;
    }

    /* Copy name */
    memset(t->name, 0, sizeof(t->name));
    for (size_t i = 0; i < sizeof(t->name)-1 && name[i]; i++) t->name[i] = name[i];

    build_kernel_thread_frame(t, fn, arg);
    t->state = THREAD_READY;
    return t;
}

thread_t* thread_create_user(const char* name, uint64_t user_rip, uint64_t brk_start, uint64_t cr3) {
    thread_t* t = thread_alloc_slot();
    if (!t) return 0;

    t->is_user = true;
    t->cr3 = cr3;
    t->parent = g_current;
    if (t->parent) t->parent->children++;
    if (!t->cr3) {
        t->state = THREAD_UNUSED;
        return 0;
    }
    t->kstack_size = KSTACK_PAGES * PAGE_SIZE;
    t->kstack = (uint8_t*)alloc_pages(KSTACK_PAGES);
    if (!t->kstack) {
        t->state = THREAD_UNUSED;
        return 0;
    }

    t->ustack_size = USTACK_PAGES * PAGE_SIZE;
    uint64_t stack_phys = pmm_alloc_pages(USTACK_PAGES);
    if (!stack_phys) {
        t->state = THREAD_UNUSED;
        return 0;
    }
    memset((void*)(uintptr_t)stack_phys, 0, t->ustack_size);

    uint64_t user_stack_base = USER_STACK_TOP - t->ustack_size;
    if (!vmm_map_range(t->cr3, user_stack_base, stack_phys, t->ustack_size,
                       VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
        pmm_free_pages(stack_phys, USTACK_PAGES);
        t->state = THREAD_UNUSED;
        return 0;
    }
    t->ustack = (uint8_t*)(uintptr_t)stack_phys;
    t->ustack_top = USER_STACK_TOP;

    memset(t->name, 0, sizeof(t->name));
    for (size_t i = 0; i < sizeof(t->name)-1 && name[i]; i++) t->name[i] = name[i];

    t->brk_start = brk_start;
    t->brk_end = brk_start;

    build_user_thread_frame(t, user_rip);

    t->state = THREAD_READY;
    return t;
}

