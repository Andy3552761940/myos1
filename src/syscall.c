#include "syscall.h"
#include "console.h"
#include "scheduler.h"
#include "thread.h"
#include "lib.h"
#include "vmm.h"
#include "pmm.h"
#include "vfs.h"
#include "kmalloc.h"
#include "elf.h"
#include "sysinfo.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/pit.h"
#include "time.h"
#include "net.h"
#include "input.h"

#define USTACK_PAGES  4

static int vfs_fd_allocate(thread_t* t, vfs_file_t* file) {
    if (!t || !file) return -1;
    for (size_t i = 0; i < THREAD_MAX_OPEN_FILES; i++) {
        if (!t->open_files[i]) {
            t->open_files[i] = file;
            t->open_file_count++;
            return (int)(i + 3);
        }
    }
    return -1;
}

static vfs_file_t* vfs_fd_get(thread_t* t, int fd) {
    if (!t || fd < 3) return 0;
    size_t idx = (size_t)(fd - 3);
    if (idx >= THREAD_MAX_OPEN_FILES) return 0;
    return t->open_files[idx];
}

static int vfs_fd_close(thread_t* t, int fd) {
    if (!t || fd < 3) return -1;
    size_t idx = (size_t)(fd - 3);
    if (idx >= THREAD_MAX_OPEN_FILES || !t->open_files[idx]) return -1;
    vfs_close(t->open_files[idx]);
    t->open_files[idx] = 0;
    if (t->open_file_count > 0) t->open_file_count--;
    return 0;
}

static uint64_t mmap_default_base(uint64_t brk_end) {
    uint64_t base = align_up_u64(brk_end, PAGE_SIZE);
    return base + 0x01000000ULL;
}

static uint64_t mmap_map_anonymous(thread_t* t, uint64_t addr, uint64_t len, int prot) {
    if (!t || !t->is_user || len == 0) return (uint64_t)-1;
    uint64_t size = align_up_u64(len, PAGE_SIZE);
    uint64_t base = addr ? align_down_u64(addr, PAGE_SIZE) : align_up_u64(t->mmap_base, PAGE_SIZE);
    uint64_t flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (prot & 0x2) flags |= VMM_FLAG_WRITABLE;
    if ((prot & 0x4) == 0) flags |= VMM_FLAG_NOEXEC;

    uint64_t mapped = 0;
    for (; mapped < size; mapped += PAGE_SIZE) {
        uint64_t pa = pmm_alloc_pages(1);
        if (!pa) break;
        memset((void*)(uintptr_t)pa, 0, PAGE_SIZE);
        if (!vmm_map_page(t->cr3, base + mapped, pa, flags)) {
            pmm_free_pages(pa, 1);
            break;
        }
    }

    if (mapped != size) {
        for (uint64_t off = 0; off < mapped; off += PAGE_SIZE) {
            uint64_t pa = vmm_unmap_page(t->cr3, base + off);
            if (pa) pmm_free_pages(pa, 1);
        }
        return (uint64_t)-1;
    }

    if (!addr) {
        t->mmap_base = base + size;
    }

    return base;
}

static char scancode_to_char(uint8_t scancode, int shift) {
    static const char keymap[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2',
        '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const char keymap_shift[128] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2',
        '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    if (scancode >= 128) return 0;
    return shift ? keymap_shift[scancode] : keymap[scancode];
}

static uint64_t stdin_read_chars(char* buf, uint64_t len) {
    uint64_t count = 0;
    int shift = 0;
    while (count < len) {
        key_event_t ev;
        if (!input_read_key(&ev)) {
            cpu_hlt();
            continue;
        }

        if (!ev.pressed) {
            if (ev.scancode == 0x2A || ev.scancode == 0x36) shift = 0;
            continue;
        }

        if (ev.scancode == 0x49) { /* Page Up */
            console_scroll_view(25);
            continue;
        } else if (ev.scancode == 0x51) { /* Page Down */
            console_scroll_view(-25);
            continue;
        } else if (ev.scancode == 0x48) { /* Arrow Up */
            console_scroll_view(1);
            continue;
        } else if (ev.scancode == 0x50) { /* Arrow Down */
            console_scroll_view(-1);
            continue;
        }

        if (ev.scancode == 0x2A || ev.scancode == 0x36) {
            shift = 1;
            continue;
        }

        char c = scancode_to_char(ev.scancode, shift);
        if (!c) continue;

        if (c == '\b') {
            if (count > 0) count--;
            continue;
        }

        buf[count++] = c;
        console_putc(c);
        if (c == '\n') break;
    }
    return count;
}

intr_frame_t* syscall_handle(intr_frame_t* frame) {
    thread_t* cur = thread_current();
    bool from_user = (frame->cs & 3) == 3;
    if (from_user) {
        if (!cur || !cur->is_user) {
            console_write("[syscall] denied: user-mode syscall without user thread\n");
            frame->rax = (uint64_t)-1;
            return frame;
        }
    } else {
        if (cur && cur->is_user) {
            console_write("[syscall] denied: kernel-mode syscall from user thread\n");
            frame->rax = (uint64_t)-1;
            return frame;
        }
    }

    uint64_t num = frame->rax;

    switch (num) {
        case SYS_write: {
            uint64_t fd  = frame->rdi;
            const char* buf = (const char*)(uintptr_t)frame->rsi;
            uint64_t len = frame->rdx;

            if (fd != 1 && fd != 2) {
                thread_t* t = thread_current();
                vfs_file_t* file = vfs_fd_get(t, (int)fd);
                if (!file || !(file->flags & VFS_O_WRONLY)) {
                    frame->rax = (uint64_t)-1;
                    return frame;
                }
                frame->rax = (uint64_t)vfs_write(file, buf, len);
                return frame;
            }

            for (uint64_t i = 0; i < len; i++) {
                console_putc(buf[i]);
            }

            frame->rax = len;
            return frame;
        }
        case SYS_fork: {
            return scheduler_fork(frame);
        }
        case SYS_execve: {
            thread_t* cur = thread_current();
            if (!cur || !cur->is_user) {
                frame->rax = (uint64_t)-1;
                return frame;
            }

            const char* path = (const char*)(uintptr_t)frame->rdi;
            vfs_file_t* file = vfs_open(path, VFS_O_RDONLY);
            if (!file || !file->node) {
                frame->rax = (uint64_t)-1;
                return frame;
            }

            size_t size = file->node->size;
            uint8_t* data = 0;
            if (size > 0) {
                data = (uint8_t*)kmalloc(size);
                if (!data) {
                    vfs_close(file);
                    frame->rax = (uint64_t)-1;
                    return frame;
                }
                vfs_ssize_t nread = vfs_read(file, data, size);
                if (nread < 0 || (size_t)nread != size) {
                    vfs_close(file);
                    kfree(data);
                    frame->rax = (uint64_t)-1;
                    return frame;
                }
            }
            vfs_close(file);

            uint64_t new_cr3 = vmm_create_user_space();
            uint64_t entry = 0;
            uint64_t brk = 0;
            if (!new_cr3 || !elf64_load_image(data, size, new_cr3, &entry, &brk)) {
                if (data) kfree(data);
                frame->rax = (uint64_t)-1;
                return frame;
            }
            if (data) kfree(data);

            uint64_t stack_phys = pmm_alloc_pages(USTACK_PAGES);
            if (!stack_phys) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            memset((void*)(uintptr_t)stack_phys, 0, USTACK_PAGES * PAGE_SIZE);
            uint64_t user_stack_base = USER_STACK_TOP - USTACK_PAGES * PAGE_SIZE;
            if (!vmm_map_range(new_cr3, user_stack_base, stack_phys, USTACK_PAGES * PAGE_SIZE,
                               VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
                pmm_free_pages(stack_phys, USTACK_PAGES);
                frame->rax = (uint64_t)-1;
                return frame;
            }

            cur->cr3 = new_cr3;
            cur->ustack = (uint8_t*)(uintptr_t)stack_phys;
            cur->ustack_size = USTACK_PAGES * PAGE_SIZE;
            cur->ustack_top = USER_STACK_TOP;
            cur->brk_start = brk;
            cur->brk_end = brk;
            cur->mmap_base = mmap_default_base(brk);

            write_cr3(new_cr3);
            frame->rip = entry;
            frame->rsp = cur->ustack_top;
            frame->rax = 0;
            return frame;
        }
        case SYS_waitpid: {
            int pid = (int)frame->rdi;
            uint64_t status_ptr = frame->rsi;
            return scheduler_waitpid(frame, pid, status_ptr);
        }
        case SYS_exit: {
            int code = (int)frame->rdi;
            console_write("[syscall] exit code=");
            console_write_dec_u64((uint64_t)code);
            console_write("\n");
            return scheduler_on_exit(frame, code);
        }
        case SYS_yield: {
            return scheduler_yield(frame);
        }
        case SYS_brk: {
            thread_t* t = thread_current();
            uint64_t new_end = frame->rdi;
            if (!t || !t->is_user) {
                frame->rax = (uint64_t)-1;
                return frame;
            }

            if (new_end == 0) {
                frame->rax = t->brk_end;
                return frame;
            }

            if (!vmm_user_set_brk(t, new_end)) {
                frame->rax = (uint64_t)-1;
            } else {
                frame->rax = t->brk_end;
            }
            return frame;
        }
        case SYS_gettimeofday: {
            time_val_t* tv = (time_val_t*)(uintptr_t)frame->rdi;
            if (!tv) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            time_gettimeofday(tv);
            frame->rax = 0;
            return frame;
        }
        case SYS_open: {
            const char* path = (const char*)(uintptr_t)frame->rdi;
            int flags = (int)frame->rsi;
            thread_t* t = thread_current();
            if (!t || !t->is_user || !path) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            if (flags == 0) flags = VFS_O_RDONLY;
            vfs_file_t* file = vfs_open(path, flags);
            if (!file) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            int fd = vfs_fd_allocate(t, file);
            if (fd < 0) {
                vfs_close(file);
                frame->rax = (uint64_t)-1;
                return frame;
            }
            frame->rax = (uint64_t)fd;
            return frame;
        }
        case SYS_read: {
            int fd = (int)frame->rdi;
            void* buf = (void*)(uintptr_t)frame->rsi;
            size_t len = (size_t)frame->rdx;
            thread_t* t = thread_current();
            if (fd == 0) {
                if (!buf || len == 0) {
                    frame->rax = 0;
                    return frame;
                }
                frame->rax = stdin_read_chars((char*)buf, len);
                return frame;
            }
            vfs_file_t* file = vfs_fd_get(t, fd);
            if (!file || !(file->flags & VFS_O_RDONLY)) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            frame->rax = (uint64_t)vfs_read(file, buf, len);
            return frame;
        }
        case SYS_lseek: {
            int fd = (int)frame->rdi;
            int64_t offset = (int64_t)frame->rsi;
            int whence = (int)frame->rdx;
            thread_t* t = thread_current();
            vfs_file_t* file = vfs_fd_get(t, fd);
            if (!file) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            int64_t base = 0;
            if (whence == SYS_SEEK_SET) {
                base = 0;
            } else if (whence == SYS_SEEK_CUR) {
                base = (int64_t)file->offset;
            } else if (whence == SYS_SEEK_END) {
                base = file->node ? (int64_t)file->node->size : 0;
            } else {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            int64_t new_pos = base + offset;
            if (new_pos < 0) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            file->offset = (size_t)new_pos;
            frame->rax = (uint64_t)new_pos;
            return frame;
        }
        case SYS_getpid: {
            thread_t* t = thread_current();
            frame->rax = t ? t->id : (uint64_t)-1;
            return frame;
        }
        case SYS_uname: {
            utsname_t* info = (utsname_t*)(uintptr_t)frame->rdi;
            if (!info) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            memset(info, 0, sizeof(*info));
            strncpy(info->sysname, "MyOS", UTSNAME_LEN - 1);
            strncpy(info->nodename, "myos-node", UTSNAME_LEN - 1);
            strncpy(info->release, "0.1", UTSNAME_LEN - 1);
            strncpy(info->version, "dev", UTSNAME_LEN - 1);
            strncpy(info->machine, "x86_64", UTSNAME_LEN - 1);
            frame->rax = 0;
            return frame;
        }
        case SYS_sysinfo: {
            sysinfo_t* info = (sysinfo_t*)(uintptr_t)frame->rdi;
            if (!info) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            memset(info, 0, sizeof(*info));
            info->totalram = pmm_total_memory_bytes();
            info->freeram = pmm_free_memory_bytes();
            info->procs = (uint16_t)scheduler_thread_count();
            uint32_t hz = pit_frequency_hz();
            if (hz) info->uptime = pit_ticks() / hz;
            frame->rax = 0;
            return frame;
        }
        case SYS_mmap: {
            thread_t* t = thread_current();
            uint64_t addr = frame->rdi;
            uint64_t len = frame->rsi;
            int prot = (int)frame->rdx;
            if (!t || !t->is_user) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            uint64_t base = mmap_map_anonymous(t, addr, len, prot);
            frame->rax = base;
            return frame;
        }
        case SYS_kill: {
            int pid = (int)frame->rdi;
            int sig = (int)frame->rsi;
            thread_t* cur = thread_current();
            if (cur && (pid == 0 || pid == (int)cur->id)) {
                return scheduler_on_exit(frame, -sig);
            }
            frame->rax = (uint64_t)scheduler_kill(pid, sig);
            return frame;
        }
        case SYS_sleep: {
            uint64_t ms = frame->rdi;
            uint32_t hz = pit_frequency_hz();
            if (hz == 0) {
                frame->rax = (uint64_t)-1;
                return frame;
            }
            uint64_t ticks = (ms * hz + 999) / 1000;
            scheduler_sleep(ticks);
            frame->rax = 0;
            return frame;
        }
        case SYS_socket: {
            int domain = (int)frame->rdi;
            int type = (int)frame->rsi;
            frame->rax = (uint64_t)net_socket(domain, type);
            return frame;
        }
        case SYS_bind: {
            int fd = (int)frame->rdi;
            const net_sockaddr_in_t* addr = (const net_sockaddr_in_t*)(uintptr_t)frame->rsi;
            frame->rax = (uint64_t)net_bind(fd, addr);
            return frame;
        }
        case SYS_sendto: {
            int fd = (int)frame->rdi;
            const void* buf = (const void*)(uintptr_t)frame->rsi;
            size_t len = (size_t)frame->rdx;
            const net_sockaddr_in_t* addr = (const net_sockaddr_in_t*)(uintptr_t)frame->r10;
            frame->rax = (uint64_t)net_sendto(fd, buf, len, addr);
            return frame;
        }
        case SYS_recvfrom: {
            int fd = (int)frame->rdi;
            void* buf = (void*)(uintptr_t)frame->rsi;
            size_t len = (size_t)frame->rdx;
            net_sockaddr_in_t* addr = (net_sockaddr_in_t*)(uintptr_t)frame->r10;
            frame->rax = (uint64_t)net_recvfrom(fd, buf, len, addr);
            return frame;
        }
        case SYS_connect: {
            int fd = (int)frame->rdi;
            const net_sockaddr_in_t* addr = (const net_sockaddr_in_t*)(uintptr_t)frame->rsi;
            frame->rax = (uint64_t)net_connect(fd, addr);
            return frame;
        }
        case SYS_listen: {
            int fd = (int)frame->rdi;
            frame->rax = (uint64_t)net_listen(fd);
            return frame;
        }
        case SYS_accept: {
            int fd = (int)frame->rdi;
            net_sockaddr_in_t* addr = (net_sockaddr_in_t*)(uintptr_t)frame->rsi;
            frame->rax = (uint64_t)net_accept(fd, addr);
            return frame;
        }
        case SYS_close: {
            int fd = (int)frame->rdi;
            thread_t* t = thread_current();
            if (vfs_fd_get(t, fd)) {
                frame->rax = (uint64_t)vfs_fd_close(t, fd);
            } else {
                frame->rax = (uint64_t)net_close(fd);
            }
            return frame;
        }
        case SYS_readdir: {
            int fd = (int)frame->rdi;
            char* buf = (char*)(uintptr_t)frame->rsi;
            size_t len = (size_t)frame->rdx;
            thread_t* t = thread_current();
            vfs_file_t* file = vfs_fd_get(t, fd);
            if (!file || !file->node || file->node->type != VFS_NODE_DIR || !buf || len == 0) {
                frame->rax = (uint64_t)-1;
                return frame;
            }

            size_t idx = file->offset;
            vfs_node_t* child = file->node->children;
            while (child && idx > 0) {
                child = child->next;
                idx--;
            }

            if (!child) {
                frame->rax = 0;
                return frame;
            }

            size_t name_len = strlen(child->name);
            if (name_len >= len) name_len = len - 1;
            memcpy(buf, child->name, name_len);
            buf[name_len] = 0;
            file->offset++;
            frame->rax = (uint64_t)name_len;
            return frame;
        }
        case SYS_netif_get: {
            size_t index = (size_t)frame->rdi;
            net_ifinfo_t* info = (net_ifinfo_t*)(uintptr_t)frame->rsi;
            frame->rax = (uint64_t)net_if_get(index, info);
            return frame;
        }
        case SYS_netif_set: {
            const net_ifreq_t* req = (const net_ifreq_t*)(uintptr_t)frame->rdi;
            frame->rax = (uint64_t)net_if_set(req);
            return frame;
        }
        case SYS_route_get: {
            size_t index = (size_t)frame->rdi;
            net_route_t* route = (net_route_t*)(uintptr_t)frame->rsi;
            frame->rax = (uint64_t)net_route_get(index, route);
            return frame;
        }
        case SYS_route_add: {
            const net_route_t* route = (const net_route_t*)(uintptr_t)frame->rdi;
            frame->rax = (uint64_t)net_route_add(route);
            return frame;
        }
        default:
            console_write("[syscall] unknown syscall ");
            console_write_dec_u64(num);
            console_write("\n");
            frame->rax = (uint64_t)-1;
            return frame;
    }
}
