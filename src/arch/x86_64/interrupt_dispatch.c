#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/apic.h"
#include "console.h"
#include "scheduler.h"
#include "thread.h"
#include "syscall.h"

static const char* exc_name(uint64_t n) {
    switch (n) {
        case 0: return "#DE Divide Error";
        case 1: return "#DB Debug";
        case 2: return "NMI";
        case 3: return "#BP Breakpoint";
        case 4: return "#OF Overflow";
        case 5: return "#BR Bound Range Exceeded";
        case 6: return "#UD Invalid Opcode";
        case 7: return "#NM Device Not Available";
        case 8: return "#DF Double Fault";
        case 9: return "Coprocessor Segment Overrun";
        case 10: return "#TS Invalid TSS";
        case 11: return "#NP Segment Not Present";
        case 12: return "#SS Stack-Segment Fault";
        case 13: return "#GP General Protection";
        case 14: return "#PF Page Fault";
        case 15: return "Reserved";
        case 16: return "#MF x87 Floating-Point";
        case 17: return "#AC Alignment Check";
        case 18: return "#MC Machine Check";
        case 19: return "#XM SIMD Floating-Point";
        case 20: return "#VE Virtualization";
        case 21: return "#CP Control Protection";
        case 22: return "Reserved";
        case 23: return "Reserved";
        case 24: return "Reserved";
        case 25: return "Reserved";
        case 26: return "Reserved";
        case 27: return "Reserved";
        case 28: return "#HV Hypervisor Injection";
        case 29: return "#VC VMM Communication";
        case 30: return "#SX Security";
        case 31: return "Reserved";
        default: return "Unknown";
    }
}

static void dump_frame(const intr_frame_t* f) {
    console_write(" int=");
    console_write_dec_u64(f->int_no);
    console_write(" err=");
    console_write_hex64(f->err_code);
    console_write("\n");

    console_write(" RIP="); console_write_hex64(f->rip);
    console_write(" CS=");  console_write_hex64(f->cs);
    console_write(" RFLAGS="); console_write_hex64(f->rflags);
    console_write("\n");

    console_write(" RAX="); console_write_hex64(f->rax);
    console_write(" RBX="); console_write_hex64(f->rbx);
    console_write(" RCX="); console_write_hex64(f->rcx);
    console_write(" RDX="); console_write_hex64(f->rdx);
    console_write("\n");

    console_write(" RSI="); console_write_hex64(f->rsi);
    console_write(" RDI="); console_write_hex64(f->rdi);
    console_write(" RBP="); console_write_hex64(f->rbp);
    console_write("\n");

    if ((f->cs & 3) == 3) {
        console_write(" RSP="); console_write_hex64(f->rsp);
        console_write(" SS=");  console_write_hex64(f->ss);
        console_write("\n");
    }
}

static int exc_exit_code(uint64_t n) {
    if (n > 31) return -1;
    return (int)(128 + n);
}

static void log_current_thread(void) {
    thread_t* t = thread_current();
    if (!t) return;
    console_write(" thread=");
    console_write_dec_u64(t->id);
    console_write(" (");
    console_write(t->name);
    console_write(")");
}

intr_frame_t* interrupt_dispatch(intr_frame_t* frame) {
    uint64_t n = frame->int_no;

    /* IRQs (PIC remapped to 32-47) */
    if (n >= 32 && n <= 47) {
        uint8_t irq = (uint8_t)(n - 32);

        irq_enter(irq);
        if (irq == 0) {
            pit_handle_irq0();
        } else {
            irq_dispatch(irq, frame);
        }

        cpu_cli();
        pic_send_eoi(irq);

        if (irq == 0) {
            intr_frame_t* next = scheduler_on_tick(frame);
            irq_exit();
            return next;
        }

        irq_exit();
        return frame;
    }

    if (n == APIC_RESCHED_VECTOR) {
        apic_eoi();
        return scheduler_on_tick(frame);
    }

    if (n == APIC_SPURIOUS_VECTOR) {
        return frame;
    }

    /* Syscall */
    if (n == 0x80) {
        return syscall_handle(frame);
    }

    /* CPU exception */
    console_write("\n[EXCEPTION] ");
    console_write(exc_name(n));
    log_current_thread();
    console_write("\n");

    if (n == 14) {
        uint64_t cr2 = read_cr2();
        console_write(" CR2=");
        console_write_hex64(cr2);
        console_write(" err=");
        console_write_hex64(frame->err_code);
        console_write(" [P=");
        console_write_dec_u64(frame->err_code & 1);
        console_write(" W=");
        console_write_dec_u64((frame->err_code >> 1) & 1);
        console_write(" U=");
        console_write_dec_u64((frame->err_code >> 2) & 1);
        console_write(" RSVD=");
        console_write_dec_u64((frame->err_code >> 3) & 1);
        console_write(" I=");
        console_write_dec_u64((frame->err_code >> 4) & 1);
        console_write("]");
        console_write("\n");
    } else if (n == 0) {
        console_write(" divide-by-zero\n");
    } else if (n == 6) {
        console_write(" invalid opcode\n");
    }

    dump_frame(frame);

    /* If from user mode, kill current thread instead of panicking. */
    if ((frame->cs & 3) == 3) {
        console_write("[EXCEPTION] killing user thread");
        log_current_thread();
        console_write("\n");
        return scheduler_on_exit(frame, exc_exit_code(n));
    }

    console_write("[PANIC] kernel exception, halting.\n");
    for (;;) { cpu_hlt(); }
}
