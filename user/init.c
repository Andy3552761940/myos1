#include "syscall.h"
#include <stdint.h>

int main(void) {
    const char msg[] = "Hello from user mode! (int 0x80 sys_write)\n";
    sys_write(1, msg, (int64_t)(sizeof(msg) - 1));

    const char msg2[] = "Now exiting...\n";
    sys_write(1, msg2, (int64_t)(sizeof(msg2) - 1));

    sys_exit(0);
    return 0;
}
