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
#include "arch/x86_64/common.h"
#include "arch/x86_64/pit.h"
#include "time.h"
#include "net.h"

#define USTACK_PAGES  4

intr_frame_t* syscall_handle(intr_frame_t* frame) {
    uint64_t num = frame->rax;

    switch (num) {
        case SYS_write: {
            uint64_t fd  = frame->rdi;
            const char* buf = (const char*)(uintptr_t)frame->rsi;
            uint64_t len = frame->rdx;

            if (fd != 1 && fd != 2) {
                frame->rax = (uint64_t)-1;
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
            frame->rax = (uint64_t)net_close(fd);
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
