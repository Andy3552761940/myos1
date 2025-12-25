#include "syscall.h"
#include "malloc.h"
#include <stdint.h>

int main(void) {
    const char msg[] = "Hello from user mode! (int 0x80 sys_write)\n";
    sys_write(1, msg, (int64_t)(sizeof(msg) - 1));

    const char msg2[] = "Now exiting...\n";
    sys_write(1, msg2, (int64_t)(sizeof(msg2) - 1));

    char* buf = (char*)malloc(64);
    if (buf) {
        for (int i = 0; i < 63; i++) buf[i] = 'A' + (i % 26);
        buf[63] = '\0';
        sys_write(1, buf, 63);
        sys_write(1, "\n", 1);
        free(buf);
    }

    sys_exit(0);
    return 0;
}
