#include "syscall.h"
#include "console.h"
#include "scheduler.h"
#include "thread.h"
#include "lib.h"
#include "vmm.h"

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
        default:
            console_write("[syscall] unknown syscall ");
            console_write_dec_u64(num);
            console_write("\n");
            frame->rax = (uint64_t)-1;
            return frame;
    }
}
