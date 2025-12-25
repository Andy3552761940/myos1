#include "memfs.h"
#include "kmalloc.h"
#include "lib.h"

typedef struct {
    uint8_t* data;
    size_t size;
    size_t cap;
} memfs_file_t;

static vfs_ssize_t memfs_read(vfs_node_t* node, size_t offset, void* buf, size_t len);
static vfs_ssize_t memfs_write(vfs_node_t* node, size_t offset, const void* buf, size_t len);
static int memfs_create(vfs_node_t* dir, const char* name, vfs_node_type_t type, vfs_node_t** out);
static int memfs_unlink(vfs_node_t* dir, const char* name);

static vfs_node_ops_t memfs_dir_ops = {
    .read = 0,
    .write = 0,
    .create = memfs_create,
    .unlink = memfs_unlink,
};

static vfs_node_ops_t memfs_file_ops = {
    .read = memfs_read,
    .write = memfs_write,
    .create = 0,
    .unlink = 0,
};

static void memfs_free_node(vfs_node_t* node) {
    if (!node) return;
    if (node->type == VFS_NODE_FILE && node->data) {
        memfs_file_t* file = (memfs_file_t*)node->data;
        if (file->data) kfree(file->data);
        kfree(file);
    }
    if (node->name) kfree(node->name);
    kfree(node);
}

static vfs_ssize_t memfs_read(vfs_node_t* node, size_t offset, void* buf, size_t len) {
    if (!node || node->type != VFS_NODE_FILE) return -1;
    memfs_file_t* file = (memfs_file_t*)node->data;
    if (!file || !buf) return -1;
    if (offset >= file->size) return 0;
    size_t avail = file->size - offset;
    if (len > avail) len = avail;
    memcpy(buf, file->data + offset, len);
    return (vfs_ssize_t)len;
}

static vfs_ssize_t memfs_write(vfs_node_t* node, size_t offset, const void* buf, size_t len) {
    if (!node || node->type != VFS_NODE_FILE) return -1;
    memfs_file_t* file = (memfs_file_t*)node->data;
    if (!file || !buf) return -1;

    size_t need = offset + len;
    if (need > file->cap) {
        size_t new_cap = file->cap ? file->cap : 64;
        while (new_cap < need) new_cap *= 2;
        uint8_t* new_data = (uint8_t*)kmalloc(new_cap);
        if (!new_data) return -1;
        if (file->data && file->size) memcpy(new_data, file->data, file->size);
        if (file->data) kfree(file->data);
        file->data = new_data;
        file->cap = new_cap;
    }

    memcpy(file->data + offset, buf, len);
    if (need > file->size) file->size = need;
    node->size = file->size;
    return (vfs_ssize_t)len;
}

static int memfs_create(vfs_node_t* dir, const char* name, vfs_node_type_t type, vfs_node_t** out) {
    if (!dir || dir->type != VFS_NODE_DIR || !name) return -1;
    if (vfs_find_child(dir, name)) return -1;

    vfs_node_ops_t* ops = 0;
    void* data = 0;
    if (type == VFS_NODE_DIR) {
        ops = &memfs_dir_ops;
    } else {
        memfs_file_t* file = (memfs_file_t*)kmalloc(sizeof(memfs_file_t));
        if (!file) return -1;
        memset(file, 0, sizeof(*file));
        data = file;
        ops = &memfs_file_ops;
    }

    vfs_node_t* node = vfs_create_node(name, type, ops, data);
    if (!node) {
        if (data) kfree(data);
        return -1;
    }

    if (vfs_add_child(dir, node) != 0) {
        memfs_free_node(node);
        return -1;
    }

    if (out) *out = node;
    return 0;
}

static int memfs_unlink(vfs_node_t* dir, const char* name) {
    if (!dir || dir->type != VFS_NODE_DIR) return -1;
    vfs_node_t* child = vfs_find_child(dir, name);
    if (!child) return -1;
    if (child->type == VFS_NODE_DIR && child->children) return -1;

    if (vfs_remove_child(dir, child) != 0) return -1;
    memfs_free_node(child);
    return 0;
}

vfs_node_t* memfs_create_root(void) {
    vfs_node_t* root = vfs_create_node("", VFS_NODE_DIR, &memfs_dir_ops, 0);
    return root;
}
