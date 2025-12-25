#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct vfs_node vfs_node_t;
typedef struct vfs_file vfs_file_t;
typedef struct vfs_node_ops vfs_node_ops_t;

typedef int64_t vfs_ssize_t;

typedef enum {
    VFS_NODE_DIR,
    VFS_NODE_FILE,
    VFS_NODE_DEV,
} vfs_node_type_t;

struct vfs_node_ops {
    vfs_ssize_t (*read)(vfs_node_t* node, size_t offset, void* buf, size_t len);
    vfs_ssize_t (*write)(vfs_node_t* node, size_t offset, const void* buf, size_t len);
    int (*create)(vfs_node_t* dir, const char* name, vfs_node_type_t type, vfs_node_t** out);
    int (*unlink)(vfs_node_t* dir, const char* name);
};

struct vfs_node {
    char* name;
    vfs_node_type_t type;
    size_t size;
    void* data;
    vfs_node_ops_t* ops;
    vfs_node_t* parent;
    vfs_node_t* children;
    vfs_node_t* next;
};

struct vfs_file {
    vfs_node_t* node;
    size_t offset;
    int flags;
};

#define VFS_O_RDONLY 0x1
#define VFS_O_WRONLY 0x2
#define VFS_O_RDWR   (VFS_O_RDONLY | VFS_O_WRONLY)
#define VFS_O_CREAT  0x4

#define VFS_NAME_MAX 255
#define VFS_PATH_MAX 512

void vfs_init(vfs_node_t* root);
vfs_node_t* vfs_root(void);
vfs_node_t* vfs_cwd(void);
void vfs_set_cwd(vfs_node_t* node);

vfs_node_t* vfs_create_node(const char* name, vfs_node_type_t type, vfs_node_ops_t* ops, void* data);
int vfs_add_child(vfs_node_t* parent, vfs_node_t* child);
vfs_node_t* vfs_find_child(vfs_node_t* parent, const char* name);
int vfs_remove_child(vfs_node_t* parent, vfs_node_t* child);

vfs_node_t* vfs_resolve(const char* path, vfs_node_t* cwd);
vfs_node_t* vfs_resolve_path(const char* path);

int vfs_mkdir(const char* path);
int vfs_create(const char* path);
int vfs_unlink(const char* path);

vfs_file_t* vfs_open(const char* path, int flags);
vfs_ssize_t vfs_read(vfs_file_t* file, void* buf, size_t len);
vfs_ssize_t vfs_write(vfs_file_t* file, const void* buf, size_t len);
void vfs_close(vfs_file_t* file);
