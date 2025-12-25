#include "devfs.h"
#include "vfs.h"
#include "virtio_blk.h"
#include "kmalloc.h"
#include "lib.h"

#define DEV_SECTOR_SIZE 512

static vfs_ssize_t dev_disk_read(vfs_node_t* node, size_t offset, void* buf, size_t len) {
    (void)node;
    if (!buf || !virtio_blk_is_ready()) return -1;

    uint8_t* out = (uint8_t*)buf;
    size_t done = 0;
    uint8_t sector[DEV_SECTOR_SIZE];

    while (done < len) {
        size_t off = offset + done;
        uint64_t sector_idx = off / DEV_SECTOR_SIZE;
        size_t sector_off = off % DEV_SECTOR_SIZE;
        size_t chunk = DEV_SECTOR_SIZE - sector_off;
        if (chunk > (len - done)) chunk = len - done;

        if (!virtio_blk_read_sector(sector_idx, sector)) return -1;
        memcpy(out + done, sector + sector_off, chunk);
        done += chunk;
    }

    return (vfs_ssize_t)done;
}

static vfs_ssize_t dev_disk_write(vfs_node_t* node, size_t offset, const void* buf, size_t len) {
    (void)node;
    if (!buf || !virtio_blk_is_ready()) return -1;

    const uint8_t* in = (const uint8_t*)buf;
    size_t done = 0;
    uint8_t sector[DEV_SECTOR_SIZE];

    while (done < len) {
        size_t off = offset + done;
        uint64_t sector_idx = off / DEV_SECTOR_SIZE;
        size_t sector_off = off % DEV_SECTOR_SIZE;
        size_t chunk = DEV_SECTOR_SIZE - sector_off;
        if (chunk > (len - done)) chunk = len - done;

        if (sector_off != 0 || chunk != DEV_SECTOR_SIZE) {
            if (!virtio_blk_read_sector(sector_idx, sector)) return -1;
        }

        memcpy(sector + sector_off, in + done, chunk);
        if (!virtio_blk_write_sector(sector_idx, sector)) return -1;
        done += chunk;
    }

    return (vfs_ssize_t)done;
}

static vfs_node_ops_t dev_disk_ops = {
    .read = dev_disk_read,
    .write = dev_disk_write,
    .create = 0,
    .unlink = 0,
};

void devfs_init(void) {
    vfs_node_t* root = vfs_root();
    if (!root) return;

    vfs_node_t* dev_dir = vfs_find_child(root, "dev");
    if (!dev_dir) {
        vfs_mkdir("/dev");
        dev_dir = vfs_find_child(root, "dev");
    }
    if (!dev_dir || dev_dir->type != VFS_NODE_DIR) return;

    if (!vfs_find_child(dev_dir, "disk")) {
        vfs_node_t* node = vfs_create_node("disk", VFS_NODE_DEV, &dev_disk_ops, 0);
        if (!node) return;
        vfs_add_child(dev_dir, node);
    }
}
