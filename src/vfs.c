#include "vfs.h"
#include "kmalloc.h"
#include "lib.h"

static vfs_node_t* g_root = 0;
static vfs_node_t* g_cwd = 0;

static char* vfs_strdup(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)kmalloc(n + 1);
    if (!out) return 0;
    strncpy(out, s, n);
    out[n] = 0;
    return out;
}

void vfs_init(vfs_node_t* root) {
    g_root = root;
    g_cwd = root;
}

vfs_node_t* vfs_root(void) {
    return g_root;
}

vfs_node_t* vfs_cwd(void) {
    return g_cwd;
}

void vfs_set_cwd(vfs_node_t* node) {
    if (node) g_cwd = node;
}

vfs_node_t* vfs_create_node(const char* name, vfs_node_type_t type, vfs_node_ops_t* ops, void* data) {
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return 0;
    memset(node, 0, sizeof(*node));
    node->name = vfs_strdup(name ? name : "");
    if (!node->name) {
        kfree(node);
        return 0;
    }
    node->type = type;
    node->ops = ops;
    node->data = data;
    node->size = 0;
    return node;
}

vfs_node_t* vfs_find_child(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != VFS_NODE_DIR) return 0;
    vfs_node_t* cur = parent->children;
    while (cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return 0;
}

int vfs_add_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child || parent->type != VFS_NODE_DIR) return -1;
    if (vfs_find_child(parent, child->name)) return -1;
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
    return 0;
}

int vfs_remove_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child || parent->type != VFS_NODE_DIR) return -1;
    vfs_node_t** cur = &parent->children;
    while (*cur) {
        if (*cur == child) {
            *cur = child->next;
            child->next = 0;
            child->parent = 0;
            return 0;
        }
        cur = &(*cur)->next;
    }
    return -1;
}

static const char* vfs_skip_slashes(const char* p) {
    while (*p == '/') p++;
    return p;
}

vfs_node_t* vfs_resolve(const char* path, vfs_node_t* cwd) {
    if (!path || !g_root) return 0;
    vfs_node_t* cur = 0;
    const char* p = path;

    if (p[0] == '/') {
        cur = g_root;
        p = vfs_skip_slashes(p);
    } else {
        cur = cwd ? cwd : g_root;
    }

    if (*p == 0) return cur;

    while (*p) {
        const char* seg = p;
        size_t len = 0;
        while (p[len] && p[len] != '/') len++;

        if (len == 1 && seg[0] == '.') {
            /* no-op */
        } else if (len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (cur->parent) cur = cur->parent;
        } else if (len > 0) {
            char name[VFS_NAME_MAX + 1];
            if (len > VFS_NAME_MAX) return 0;
            strncpy(name, seg, len);
            name[len] = 0;
            cur = vfs_find_child(cur, name);
            if (!cur) return 0;
        }

        p += len;
        p = vfs_skip_slashes(p);
    }

    return cur;
}

vfs_node_t* vfs_resolve_path(const char* path) {
    return vfs_resolve(path, g_cwd);
}

static int vfs_resolve_parent(const char* path, vfs_node_t* cwd, vfs_node_t** out_parent, char* out_name) {
    if (!path || !out_parent || !out_name) return -1;
    size_t len = strlen(path);
    if (len == 0 || len >= VFS_PATH_MAX) return -1;

    char tmp[VFS_PATH_MAX];
    strncpy(tmp, path, len);
    tmp[len] = 0;

    while (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
        len--;
    }

    char* last = 0;
    for (size_t i = 0; i < len; i++) {
        if (tmp[i] == '/') last = &tmp[i];
    }

    if (!last) {
        *out_parent = cwd ? cwd : g_root;
        strncpy(out_name, tmp, VFS_NAME_MAX);
        out_name[VFS_NAME_MAX] = 0;
        return 0;
    }

    if (last == tmp) {
        *out_parent = g_root;
        strncpy(out_name, last + 1, VFS_NAME_MAX);
        out_name[VFS_NAME_MAX] = 0;
        return 0;
    }

    *last = 0;
    *out_parent = vfs_resolve(tmp, cwd);
    if (!*out_parent) return -1;

    strncpy(out_name, last + 1, VFS_NAME_MAX);
    out_name[VFS_NAME_MAX] = 0;
    return 0;
}

int vfs_mkdir(const char* path) {
    vfs_node_t* parent = 0;
    char name[VFS_NAME_MAX + 1];
    if (vfs_resolve_parent(path, g_cwd, &parent, name) != 0) return -1;
    if (!parent || parent->type != VFS_NODE_DIR) return -1;
    if (name[0] == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;
    if (!parent->ops || !parent->ops->create) return -1;
    vfs_node_t* created = 0;
    if (parent->ops->create(parent, name, VFS_NODE_DIR, &created) != 0) return -1;
    return 0;
}

int vfs_create(const char* path) {
    vfs_node_t* parent = 0;
    char name[VFS_NAME_MAX + 1];
    if (vfs_resolve_parent(path, g_cwd, &parent, name) != 0) return -1;
    if (!parent || parent->type != VFS_NODE_DIR) return -1;
    if (name[0] == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;
    if (!parent->ops || !parent->ops->create) return -1;
    vfs_node_t* created = 0;
    if (parent->ops->create(parent, name, VFS_NODE_FILE, &created) != 0) return -1;
    return 0;
}

int vfs_unlink(const char* path) {
    vfs_node_t* parent = 0;
    char name[VFS_NAME_MAX + 1];
    if (vfs_resolve_parent(path, g_cwd, &parent, name) != 0) return -1;
    if (!parent || parent->type != VFS_NODE_DIR) return -1;
    if (!parent->ops || !parent->ops->unlink) return -1;
    return parent->ops->unlink(parent, name);
}

vfs_file_t* vfs_open(const char* path, int flags) {
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node && (flags & VFS_O_CREAT)) {
        if (vfs_create(path) != 0) return 0;
        node = vfs_resolve_path(path);
    }
    if (!node) return 0;
    if (node->type == VFS_NODE_DIR && (flags & VFS_O_WRONLY)) return 0;

    vfs_file_t* file = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!file) return 0;
    file->node = node;
    file->offset = 0;
    file->flags = flags;
    return file;
}

vfs_ssize_t vfs_read(vfs_file_t* file, void* buf, size_t len) {
    if (!file || !file->node || !file->node->ops || !file->node->ops->read) return -1;
    vfs_ssize_t n = file->node->ops->read(file->node, file->offset, buf, len);
    if (n > 0) file->offset += (size_t)n;
    return n;
}

vfs_ssize_t vfs_write(vfs_file_t* file, const void* buf, size_t len) {
    if (!file || !file->node || !file->node->ops || !file->node->ops->write) return -1;
    vfs_ssize_t n = file->node->ops->write(file->node, file->offset, buf, len);
    if (n > 0) file->offset += (size_t)n;
    return n;
}

void vfs_close(vfs_file_t* file) {
    if (!file) return;
    kfree(file);
}
