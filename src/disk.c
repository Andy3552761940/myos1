#include "disk.h"
#include "arch/x86_64/irq.h"
#include "console.h"

static void disk_irq_handler(uint8_t irq, intr_frame_t* frame) {
    (void)frame;
    console_write("[disk] IRQ ");
    console_write_dec_u64(irq);
    console_write(" signaled\n");
}

void disk_init(void) {
    irq_register_handler(14, disk_irq_handler, "ata-primary");
}
