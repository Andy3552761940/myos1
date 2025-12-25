#include "virtio_blk.h"
#include "pci.h"
#include "io.h"
#include "pmm.h"
#include "console.h"
#include "lib.h"
#include "arch/x86_64/common.h"

/* Legacy virtio PCI I/O register offsets (OSDev virtio legacy layout) */
#define VIRTIO_PCI_HOST_FEATURES   0x00 /* 32-bit */
#define VIRTIO_PCI_GUEST_FEATURES  0x04 /* 32-bit */
#define VIRTIO_PCI_QUEUE_ADDRESS   0x08 /* 32-bit (PFN) */
#define VIRTIO_PCI_QUEUE_SIZE      0x0C /* 16-bit */
#define VIRTIO_PCI_QUEUE_SELECT    0x0E /* 16-bit */
#define VIRTIO_PCI_QUEUE_NOTIFY    0x10 /* 16-bit */
#define VIRTIO_PCI_STATUS          0x12 /* 8-bit */
#define VIRTIO_PCI_ISR             0x13 /* 8-bit */
#define VIRTIO_PCI_DEVICE_SPECIFIC 0x14

/* Device status bits */
#define VIRTIO_STATUS_ACK      0x01
#define VIRTIO_STATUS_DRIVER   0x02
#define VIRTIO_STATUS_DRIVEROK 0x04
#define VIRTIO_STATUS_FAILED   0x80

/* Virtqueue structures */
#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    /* ring[num] follows */
} __attribute__((packed)) virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    /* ring[num] follows */
} __attribute__((packed)) virtq_used_t;

typedef struct {
    uint16_t io_base;
    uint16_t queue_num;

    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;

    uint8_t* queue_mem;
    size_t queue_mem_pages;

    uint16_t last_used_idx;

    /* Request buffers (single in-flight request) */
    struct {
        uint32_t type;
        uint32_t reserved;
        uint64_t sector;
    } __attribute__((packed)) req;

    uint8_t status;
} virtio_blk_t;

static virtio_blk_t g_dev;
static int g_inited = 0;

static inline void mb(void) { __asm__ volatile("" ::: "memory"); }

static uint8_t in8(uint16_t base, uint16_t off)  { return inb((uint16_t)(base + off)); }
static uint16_t in16(uint16_t base, uint16_t off){ return inw((uint16_t)(base + off)); }
static uint32_t in32(uint16_t base, uint16_t off){ return inl((uint16_t)(base + off)); }

static void out8(uint16_t base, uint16_t off, uint8_t v)   { outb((uint16_t)(base + off), v); }
static void out16(uint16_t base, uint16_t off, uint16_t v) { outw((uint16_t)(base + off), v); }
static void out32(uint16_t base, uint16_t off, uint32_t v) { outl((uint16_t)(base + off), v); }

static bool setup_queue(virtio_blk_t* d) {
    /* Select queue 0 */
    out16(d->io_base, VIRTIO_PCI_QUEUE_SELECT, 0);
    uint16_t qsz = in16(d->io_base, VIRTIO_PCI_QUEUE_SIZE);
    if (qsz == 0) return false;
    d->queue_num = qsz;

    /* Allocate virtqueue memory. Layout:
       desc[num] (16 bytes each)
       avail (6 + 2*num bytes) then pad to 4096
       used (6 + 8*num bytes)
    */
    size_t desc_size = (size_t)qsz * sizeof(virtq_desc_t);
    size_t avail_size = 6 + (size_t)qsz * 2;
    size_t used_offset = align_up_u64(desc_size + avail_size, 4096);
    size_t used_size = 6 + (size_t)qsz * sizeof(virtq_used_elem_t);
    size_t total = used_offset + used_size;
    total = align_up_u64(total, 4096);

    size_t pages = total / PAGE_SIZE;
    uint64_t mem = pmm_alloc_pages(pages);
    if (!mem) return false;

    memset((void*)(uintptr_t)mem, 0, pages * PAGE_SIZE);

    d->queue_mem = (uint8_t*)(uintptr_t)mem;
    d->queue_mem_pages = pages;

    d->desc  = (virtq_desc_t*)(void*)d->queue_mem;
    d->avail = (virtq_avail_t*)(void*)(d->queue_mem + desc_size);
    d->used  = (virtq_used_t*)(void*)(d->queue_mem + used_offset);

    d->last_used_idx = 0;

    /* Tell device about the queue (PFN) */
    uint32_t pfn = (uint32_t)(mem >> 12);
    out32(d->io_base, VIRTIO_PCI_QUEUE_ADDRESS, pfn);

    return true;
}

bool virtio_blk_try_init_legacy(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor = pci_read16(bus, slot, func, 0x00);
    uint16_t device = pci_read16(bus, slot, func, 0x02);

    /* QEMU legacy virtio-blk device id is typically 0x1001 */
    if (vendor != 0x1AF4 || device != 0x1001) return false;

    /* Enable I/O space and bus mastering */
    uint16_t cmd = pci_read16(bus, slot, func, 0x04);
    cmd |= 0x0005; /* IO space + bus master */
    pci_write16(bus, slot, func, 0x04, cmd);

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if ((bar0 & 1u) == 0) {
        console_write("[virtio-blk] BAR0 is not I/O; legacy driver needs I/O\n");
        return false;
    }
    uint16_t iobase = (uint16_t)(bar0 & ~0x3u);

    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.io_base = iobase;

    /* Reset */
    out8(iobase, VIRTIO_PCI_STATUS, 0);
    io_wait();

    /* Ack + Driver */
    out8(iobase, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
    out8(iobase, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Negotiate features (accept none for now) */
    uint32_t features = in32(iobase, VIRTIO_PCI_HOST_FEATURES);
    (void)features;
    out32(iobase, VIRTIO_PCI_GUEST_FEATURES, 0);

    if (!setup_queue(&g_dev)) {
        console_write("[virtio-blk] failed to setup queue\n");
        out8(iobase, VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return false;
    }

    /* DRIVER OK */
    uint8_t st = in8(iobase, VIRTIO_PCI_STATUS);
    out8(iobase, VIRTIO_PCI_STATUS, (uint8_t)(st | VIRTIO_STATUS_DRIVEROK));

    g_inited = 1;

    console_write("[virtio-blk] legacy device initialized at io=");
    console_write_hex64(iobase);
    console_write(" qsz=");
    console_write_dec_u64(g_dev.queue_num);
    console_write("\n");

    /* Capacity in sectors (64-bit) in device-specific config offset 0 */
    uint32_t cap_lo = in32(iobase, VIRTIO_PCI_DEVICE_SPECIFIC + 0);
    uint32_t cap_hi = in32(iobase, VIRTIO_PCI_DEVICE_SPECIFIC + 4);
    uint64_t cap = ((uint64_t)cap_hi << 32) | cap_lo;
    console_write("[virtio-blk] capacity(sectors)=");
    console_write_dec_u64(cap);
    console_write("\n");

    return true;
}

bool virtio_blk_read_sector(uint64_t sector, void* out512) {
    if (!g_inited) return false;

    virtio_blk_t* d = &g_dev;
    const uint16_t qsz = d->queue_num;
    if (qsz < 3) return false;

    d->req.type = 0; /* VIRTIO_BLK_T_IN */
    d->req.reserved = 0;
    d->req.sector = sector;
    d->status = 0xFF;

    /* Descriptors 0..2 */
    d->desc[0].addr = (uint64_t)(uintptr_t)&d->req;
    d->desc[0].len = sizeof(d->req);
    d->desc[0].flags = VIRTQ_DESC_F_NEXT;
    d->desc[0].next = 1;

    d->desc[1].addr = (uint64_t)(uintptr_t)out512;
    d->desc[1].len = 512;
    d->desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE; /* device writes data */
    d->desc[1].next = 2;

    d->desc[2].addr = (uint64_t)(uintptr_t)&d->status;
    d->desc[2].len = 1;
    d->desc[2].flags = VIRTQ_DESC_F_WRITE;
    d->desc[2].next = 0;

    /* Put head in avail ring */
    uint16_t* avail_ring = (uint16_t*)((uint8_t*)d->avail + 4);
    uint16_t idx = d->avail->idx;
    avail_ring[idx % qsz] = 0;
    mb();
    d->avail->idx = (uint16_t)(idx + 1);
    mb();

    /* Notify device */
    out16(d->io_base, VIRTIO_PCI_QUEUE_NOTIFY, 0);

    /* Wait for used->idx to advance */
    while (d->used->idx == d->last_used_idx) {
        cpu_pause();
    }

    mb();
    /* used ring starts after 4 bytes (flags, idx) */
    virtq_used_elem_t* used_ring = (virtq_used_elem_t*)((uint8_t*)d->used + 4);
    (void)used_ring; /* we don't need id/len for single request */

    d->last_used_idx++;

    return d->status == 0;
}

bool virtio_blk_write_sector(uint64_t sector, const void* in512) {
    if (!g_inited) return false;

    virtio_blk_t* d = &g_dev;
    const uint16_t qsz = d->queue_num;
    if (qsz < 3) return false;

    d->req.type = 1; /* VIRTIO_BLK_T_OUT */
    d->req.reserved = 0;
    d->req.sector = sector;
    d->status = 0xFF;

    d->desc[0].addr = (uint64_t)(uintptr_t)&d->req;
    d->desc[0].len = sizeof(d->req);
    d->desc[0].flags = VIRTQ_DESC_F_NEXT;
    d->desc[0].next = 1;

    d->desc[1].addr = (uint64_t)(uintptr_t)in512;
    d->desc[1].len = 512;
    d->desc[1].flags = VIRTQ_DESC_F_NEXT;
    d->desc[1].next = 2;

    d->desc[2].addr = (uint64_t)(uintptr_t)&d->status;
    d->desc[2].len = 1;
    d->desc[2].flags = VIRTQ_DESC_F_WRITE;
    d->desc[2].next = 0;

    uint16_t* avail_ring = (uint16_t*)((uint8_t*)d->avail + 4);
    uint16_t idx = d->avail->idx;
    avail_ring[idx % qsz] = 0;
    mb();
    d->avail->idx = (uint16_t)(idx + 1);
    mb();

    out16(d->io_base, VIRTIO_PCI_QUEUE_NOTIFY, 0);

    while (d->used->idx == d->last_used_idx) {
        cpu_pause();
    }

    mb();
    d->last_used_idx++;

    return d->status == 0;
}

bool virtio_blk_is_ready(void) {
    return g_inited != 0;
}
