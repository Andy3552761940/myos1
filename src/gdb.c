#include "gdb.h"
#include "serial.h"
#include "console.h"
#include "log.h"
#include "lib.h"
#include "arch/x86_64/gdt.h"

#define GDB_MAX_PACKET 512
#define GDB_MAX_BREAKPOINTS 32

typedef struct {
    uint64_t addr;
    uint8_t saved;
    int used;
} gdb_breakpoint_t;

static int gdb_enabled = 0;
static int gdb_active = 0;
static uint32_t saved_targets = 0;
static int saved_serial_enabled = 0;
static gdb_breakpoint_t breakpoints[GDB_MAX_BREAKPOINTS];
static int pending_reinsert = 0;
static uint64_t pending_addr = 0;
static int auto_continue = 0;

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char int_to_hex(int v) {
    static const char* hex = "0123456789abcdef";
    return hex[v & 0xF];
}

static void gdb_send_packet(const char* data, size_t len) {
    uint8_t csum = 0;
    serial_putc('$');
    for (size_t i = 0; i < len; i++) {
        csum += (uint8_t)data[i];
        serial_putc(data[i]);
    }
    serial_putc('#');
    serial_putc(int_to_hex((csum >> 4) & 0xF));
    serial_putc(int_to_hex(csum & 0xF));
}

static size_t gdb_read_packet(char* buf, size_t max) {
    char c = 0;
    do {
        c = serial_getc();
        if ((uint8_t)c == 0x03) {
            buf[0] = 0;
            return 0;
        }
    } while (c != '$');

    uint8_t csum = 0;
    size_t i = 0;
    for (;;) {
        c = serial_getc();
        if (c == '#') break;
        if (i + 1 < max) buf[i++] = c;
        csum += (uint8_t)c;
    }

    buf[i] = 0;
    int hi = hex_to_int(serial_getc());
    int lo = hex_to_int(serial_getc());
    uint8_t rxsum = (uint8_t)((hi << 4) | lo);
    if (rxsum != csum) {
        serial_putc('-');
        return 0;
    }

    serial_putc('+');
    return i;
}

static void gdb_send_string(const char* s) {
    gdb_send_packet(s, strlen(s));
}

static void gdb_append_hex(char* out, size_t* idx, uint8_t val) {
    out[(*idx)++] = int_to_hex((val >> 4) & 0xF);
    out[(*idx)++] = int_to_hex(val & 0xF);
}

static void gdb_append_reg(char* out, size_t* idx, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b = (uint8_t)(value >> (i * 8));
        gdb_append_hex(out, idx, b);
    }
}

static void gdb_encode_registers(const intr_frame_t* frame, char* out, size_t* len) {
    size_t idx = 0;
    uint64_t rsp = 0;
    if ((frame->cs & 3) == 3) {
        rsp = frame->rsp;
    } else {
        rsp = (uint64_t)(uintptr_t)&frame->rflags;
    }

    gdb_append_reg(out, &idx, frame->rax, 8);
    gdb_append_reg(out, &idx, frame->rbx, 8);
    gdb_append_reg(out, &idx, frame->rcx, 8);
    gdb_append_reg(out, &idx, frame->rdx, 8);
    gdb_append_reg(out, &idx, frame->rsi, 8);
    gdb_append_reg(out, &idx, frame->rdi, 8);
    gdb_append_reg(out, &idx, frame->rbp, 8);
    gdb_append_reg(out, &idx, rsp, 8);
    gdb_append_reg(out, &idx, frame->r8, 8);
    gdb_append_reg(out, &idx, frame->r9, 8);
    gdb_append_reg(out, &idx, frame->r10, 8);
    gdb_append_reg(out, &idx, frame->r11, 8);
    gdb_append_reg(out, &idx, frame->r12, 8);
    gdb_append_reg(out, &idx, frame->r13, 8);
    gdb_append_reg(out, &idx, frame->r14, 8);
    gdb_append_reg(out, &idx, frame->r15, 8);
    gdb_append_reg(out, &idx, frame->rip, 8);
    gdb_append_reg(out, &idx, frame->rflags & 0xFFFFFFFFu, 4);
    gdb_append_reg(out, &idx, frame->cs & 0xFFFFu, 4);
    gdb_append_reg(out, &idx, (frame->cs & 3) == 3 ? (frame->ss & 0xFFFFu) : GDT_SEL_KDATA, 4);
    gdb_append_reg(out, &idx, 0, 4); /* ds */
    gdb_append_reg(out, &idx, 0, 4); /* es */
    gdb_append_reg(out, &idx, 0, 4); /* fs */
    gdb_append_reg(out, &idx, 0, 4); /* gs */

    out[idx] = 0;
    *len = idx;
}

static int gdb_parse_hex(const char** p, uint64_t* out) {
    uint64_t value = 0;
    int digits = 0;
    while (**p) {
        int v = hex_to_int(**p);
        if (v < 0) break;
        value = (value << 4) | (uint64_t)v;
        (*p)++;
        digits++;
    }
    if (digits == 0) return 0;
    *out = value;
    return 1;
}

static void gdb_write_memory(uint64_t addr, const char* data, size_t len) {
    uint8_t* p = (uint8_t*)(uintptr_t)addr;
    for (size_t i = 0; i < len; i++) {
        int hi = hex_to_int(data[i * 2]);
        int lo = hex_to_int(data[i * 2 + 1]);
        if (hi < 0 || lo < 0) break;
        p[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void gdb_read_memory(uint64_t addr, char* out, size_t len) {
    uint8_t* p = (uint8_t*)(uintptr_t)addr;
    size_t idx = 0;
    for (size_t i = 0; i < len; i++) {
        gdb_append_hex(out, &idx, p[i]);
    }
    out[idx] = 0;
}

static gdb_breakpoint_t* gdb_find_breakpoint(uint64_t addr) {
    for (size_t i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (breakpoints[i].used && breakpoints[i].addr == addr) return &breakpoints[i];
    }
    return 0;
}

static int gdb_add_breakpoint(uint64_t addr) {
    gdb_breakpoint_t* bp = gdb_find_breakpoint(addr);
    if (bp) return 1;
    for (size_t i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (!breakpoints[i].used) {
            uint8_t* p = (uint8_t*)(uintptr_t)addr;
            breakpoints[i].addr = addr;
            breakpoints[i].saved = *p;
            breakpoints[i].used = 1;
            *p = 0xCC;
            return 1;
        }
    }
    return 0;
}

static int gdb_remove_breakpoint(uint64_t addr) {
    gdb_breakpoint_t* bp = gdb_find_breakpoint(addr);
    if (!bp) return 0;
    uint8_t* p = (uint8_t*)(uintptr_t)addr;
    *p = bp->saved;
    bp->used = 0;
    return 1;
}

static void gdb_restore_pending_breakpoint(void) {
    if (!pending_reinsert) return;
    gdb_breakpoint_t* bp = gdb_find_breakpoint(pending_addr);
    if (bp) {
        uint8_t* p = (uint8_t*)(uintptr_t)bp->addr;
        *p = 0xCC;
    }
    pending_reinsert = 0;
    pending_addr = 0;
}

static void gdb_stop_reply(void) {
    gdb_send_string("S05");
}

void gdb_init(void) {
    serial_init();
    gdb_enabled = 1;
}

static void gdb_enter_session(void) {
    if (gdb_active) return;
    gdb_active = 1;
    saved_targets = log_get_targets();
    saved_serial_enabled = console_get_serial_enabled();
    log_set_targets(LOG_TARGET_CONSOLE);
    console_set_serial_enabled(0);
}

static void gdb_leave_session(void) {
    if (!gdb_active) return;
    log_set_targets(saved_targets);
    console_set_serial_enabled(saved_serial_enabled);
    gdb_active = 0;
}

static void gdb_handle_command(const char* cmd, intr_frame_t* frame, int* resume, int* single_step) {
    if (cmd[0] == '?') {
        gdb_stop_reply();
        return;
    }

    if (cmd[0] == 'g') {
        char out[GDB_MAX_PACKET];
        size_t len = 0;
        gdb_encode_registers(frame, out, &len);
        gdb_send_packet(out, len);
        return;
    }

    if (cmd[0] == 'm') {
        const char* p = cmd + 1;
        uint64_t addr = 0;
        uint64_t len = 0;
        if (!gdb_parse_hex(&p, &addr) || *p != ',') {
            gdb_send_string("E01");
            return;
        }
        p++;
        if (!gdb_parse_hex(&p, &len)) {
            gdb_send_string("E01");
            return;
        }
        if (len > (GDB_MAX_PACKET / 2)) {
            gdb_send_string("E02");
            return;
        }
        char out[GDB_MAX_PACKET];
        gdb_read_memory(addr, out, (size_t)len);
        gdb_send_packet(out, strlen(out));
        return;
    }

    if (cmd[0] == 'M') {
        const char* p = cmd + 1;
        uint64_t addr = 0;
        uint64_t len = 0;
        if (!gdb_parse_hex(&p, &addr) || *p != ',') {
            gdb_send_string("E01");
            return;
        }
        p++;
        if (!gdb_parse_hex(&p, &len) || *p != ':') {
            gdb_send_string("E01");
            return;
        }
        p++;
        if (strlen(p) < len * 2) {
            gdb_send_string("E02");
            return;
        }
        gdb_write_memory(addr, p, (size_t)len);
        gdb_send_string("OK");
        return;
    }

    if (cmd[0] == 'Z' || cmd[0] == 'z') {
        const char* p = cmd + 1;
        uint64_t type = 0;
        uint64_t addr = 0;
        if (!gdb_parse_hex(&p, &type) || *p != ',') {
            gdb_send_string("E01");
            return;
        }
        p++;
        if (!gdb_parse_hex(&p, &addr)) {
            gdb_send_string("E01");
            return;
        }
        if (type == 0) {
            int ok = (cmd[0] == 'Z') ? gdb_add_breakpoint(addr) : gdb_remove_breakpoint(addr);
            gdb_send_string(ok ? "OK" : "E03");
            return;
        }
        gdb_send_string("");
        return;
    }

    if (cmd[0] == 'H') {
        gdb_send_string("OK");
        return;
    }

    if (cmd[0] == 'q') {
        if (strncmp(cmd, "qSupported", 10) == 0) {
            gdb_send_string("PacketSize=400");
            return;
        }
        gdb_send_string("");
        return;
    }

    if (cmd[0] == 'v') {
        if (strcmp(cmd, "vCont?") == 0) {
            gdb_send_string("vCont;c;s");
            return;
        }
        gdb_send_string("");
        return;
    }

    if (cmd[0] == 'D') {
        gdb_send_string("OK");
        *resume = 1;
        *single_step = 0;
        return;
    }

    if (cmd[0] == 'c' || cmd[0] == 's') {
        const char* p = cmd + 1;
        uint64_t addr = 0;
        if (*p) {
            if (gdb_parse_hex(&p, &addr)) {
                frame->rip = addr;
            }
        }
        *resume = 1;
        *single_step = (cmd[0] == 's');
        return;
    }

    gdb_send_string("");
}

bool gdb_handle_exception(intr_frame_t* frame, intr_frame_t** out_frame) {
    if (!gdb_enabled) return false;

    if (frame->int_no == 1 && auto_continue) {
        frame->rflags &= ~(1u << 8);
        gdb_restore_pending_breakpoint();
        auto_continue = 0;
        *out_frame = frame;
        return true;
    }

    if (frame->int_no == 3) {
        uint64_t addr = frame->rip - 1;
        gdb_breakpoint_t* bp = gdb_find_breakpoint(addr);
        if (bp) {
            uint8_t* p = (uint8_t*)(uintptr_t)addr;
            *p = bp->saved;
            pending_reinsert = 1;
            pending_addr = addr;
            frame->rip = addr;
        }
    }

    gdb_enter_session();
    gdb_stop_reply();

    char inbuf[GDB_MAX_PACKET];
    for (;;) {
        size_t len = gdb_read_packet(inbuf, sizeof(inbuf));
        if (len == 0) {
            gdb_stop_reply();
            continue;
        }

        int resume = 0;
        int single_step = 0;
        gdb_handle_command(inbuf, frame, &resume, &single_step);
        if (resume) {
            if (pending_reinsert) {
                frame->rflags |= (1u << 8);
                auto_continue = single_step ? 0 : 1;
            } else if (single_step) {
                frame->rflags |= (1u << 8);
            } else {
                frame->rflags &= ~(1u << 8);
            }
            gdb_leave_session();
            *out_frame = frame;
            return true;
        }
    }
}
