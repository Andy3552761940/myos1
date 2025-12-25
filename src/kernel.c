#include "console.h"
#include "lib.h"
#include "multiboot2.h"
#include "pmm.h"
#include "tarfs.h"
#include "vfs.h"
#include "memfs.h"
#include "devfs.h"
#include "elf.h"
#include "pci.h"
#include "virtio_blk.h"
#include "vmm.h"
#include "kmalloc.h"
#include "input.h"
#include "net.h"
#include "time.h"
#include "disk.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/smp.h"
#include "arch/x86_64/common.h"
#include "scheduler.h"
#include "thread.h"
#include "log.h"
#include "gdb.h"

#define MB2_BOOTLOADER_MAGIC 0x36d76289u

extern uint8_t _binary_build_initramfs_tar_start[];
extern uint8_t _binary_build_initramfs_tar_end[];

static void klogger(void* arg) {
    (void)arg;
    for (;;) {
        scheduler_sleep(100); /* ~1 second at 100Hz */
        log_info("ticks=%llu free_mem=%llu KiB\n",
                 (unsigned long long)pit_ticks(),
                 (unsigned long long)(pmm_free_memory_bytes() / 1024));
    }
}

static void pci_cb(const pci_dev_t* dev, void* user) {
    (void)user;
    net_pci_probe(dev);
    if (dev->vendor_id == 0x1AF4) {
        log_info("virtio dev 0x%08x at %llu:%llu.%llu\n",
                 dev->device_id,
                 (unsigned long long)dev->bus,
                 (unsigned long long)dev->slot,
                 (unsigned long long)dev->func);

        virtio_blk_try_init_legacy(dev->bus, dev->slot, dev->func);
    }
}

void kernel_main(uint64_t mb2_magic, const mb2_info_t* mb2) {
    console_init();
    log_init(LOG_LEVEL_INFO, LOG_TARGET_CONSOLE | LOG_TARGET_SERIAL);
    gdb_init();

    log_info("mb2_magic=0x%llx mb2=0x%llx\n",
             (unsigned long long)mb2_magic,
             (unsigned long long)(uintptr_t)mb2);

    if ((uint32_t)mb2_magic != MB2_BOOTLOADER_MAGIC) {
        log_error("bad multiboot2 magic\n");
        for (;;) cpu_hlt();
    }

    /* Framebuffer tag (optional). */
    const mb2_tag_t* tag = (const mb2_tag_t*)((const uint8_t*)mb2 + 8);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            const mb2_tag_framebuffer_t* fb = (const mb2_tag_framebuffer_t*)tag;
            console_fb_info_t info = {
                .base = (void*)(uintptr_t)fb->framebuffer_addr,
                .width = fb->framebuffer_width,
                .height = fb->framebuffer_height,
                .pitch = fb->framebuffer_pitch,
                .bpp = fb->framebuffer_bpp,
                .type = fb->framebuffer_type,
            };
            console_set_framebuffer(&info);
            log_info("framebuffer enabled\n");
            break;
        }
        tag = (const mb2_tag_t*)((const uint8_t*)tag + mb2_align8(tag->size));
    }

    /* Memory manager (identity mapped 0..4GiB). */
    pmm_init(mb2);

    /* Virtual memory + kernel heap. */
    vmm_init();
    kmalloc_init();
    vfs_init(memfs_create_root());

    /* CPU tables */
    gdt_init();
    idt_init();

    /* PIC/PIT */
    pic_init();
    irq_init();
    pit_init(100);
    time_init();

    /* Mask all IRQs except PIT (IRQ0). */
    for (uint8_t i = 0; i < 16; i++) pic_set_mask(i, 1);
    pic_set_mask(0, 0);
    pic_set_mask(1, 0);
    pic_set_mask(2, 0);
    pic_set_mask(12, 0);
    pic_set_mask(14, 0);

    /* Scheduler */
    scheduler_init();

    /* SMP bring-up (APIC + APs) */
    smp_init();

    input_init();
    disk_init();
    net_init();

    /* Init tarfs initramfs embedded in kernel. */
    tarfs_init(_binary_build_initramfs_tar_start, _binary_build_initramfs_tar_end);
    tarfs_populate_vfs(vfs_root());
    vfs_mkdir("/rw");
    devfs_init();

    /* Load and run init.elf from initramfs in user mode (reserve its memory early). */
    vfs_file_t* init_file = vfs_open("/init.elf", VFS_O_RDONLY);
    if (!init_file || !init_file->node) {
        log_error("init.elf not found in initramfs\n");
    } else {
        size_t init_size = init_file->node->size;
        uint8_t* init_data = 0;
        if (init_size > 0) {
            init_data = (uint8_t*)kmalloc(init_size);
        }
        if (!init_data && init_size > 0) {
            log_error("failed to allocate init buffer\n");
        } else if (init_size > 0) {
            vfs_ssize_t nread = vfs_read(init_file, init_data, init_size);
            if (nread < 0 || (size_t)nread != init_size) {
                log_error("failed to read init.elf\n");
                kfree(init_data);
                init_data = 0;
                init_size = 0;
            }
        }
        uint64_t entry = 0;
        uint64_t brk = 0;
        uint64_t init_cr3 = vmm_create_user_space();
        if (init_data && init_cr3 && elf64_load_image(init_data, init_size, init_cr3, &entry, &brk)) {
            thread_create_user("init", entry, brk, init_cr3);
        } else {
            log_error("failed to load init.elf\n");
        }
        if (init_data) kfree(init_data);
        vfs_close(init_file);
    }

    /* Spawn a kernel logger thread. */
    thread_create_kernel("klogger", klogger, 0);

    /* PCI scan for virtio devices (especially virtio-blk legacy). */
    pci_enumerate(pci_cb, 0);

    /* Try reading sector 0 if virtio-blk is present. */
    uint8_t sector0[512];
    memset(sector0, 0, sizeof(sector0));
    if (virtio_blk_read_sector(0, sector0)) {
        log_info("virtio-blk sector0[0..31]: ");
        for (int i = 0; i < 32; i++) {
            static const char* hex = "0123456789ABCDEF";
            uint8_t b = sector0[i];
            console_putc(hex[b >> 4]);
            console_putc(hex[b & 0xF]);
            console_putc(' ');
        }
        console_putc('\n');
    } else {
        log_warn("virtio-blk read sector0 skipped/failed\n");
    }

    scheduler_dump();

    log_info("enabling interrupts\n");
    cpu_sti();

    log_info("idle loop\n");
    for (;;) {
        cpu_hlt();
    }
}
