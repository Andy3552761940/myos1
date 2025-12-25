#include "tarfs.h"
#include "console.h"
#include "lib.h"
#include "kmalloc.h"

typedef struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed)) tar_header_t;

static const uint8_t* g_start = 0;
static const uint8_t* g_end = 0;

typedef struct {
    const uint8_t* data;
    size_t size;
} tarfs_file_t;

static vfs_ssize_t tarfs_read(vfs_node_t* node, size_t offset, void* buf, size_t len) {
    if (!node || node->type != VFS_NODE_FILE || !buf) return -1;
    tarfs_file_t* file = (tarfs_file_t*)node->data;
    if (!file) return -1;
    if (offset >= file->size) return 0;
    size_t avail = file->size - offset;
    if (len > avail) len = avail;
    memcpy(buf, file->data + offset, len);
    return (vfs_ssize_t)len;
}

static vfs_ssize_t tarfs_write(vfs_node_t* node, size_t offset, const void* buf, size_t len) {
    (void)node;
    (void)offset;
    (void)buf;
    (void)len;
    return -1;
}

static vfs_node_ops_t tarfs_dir_ops = {
    .read = 0,
    .write = 0,
    .create = 0,
    .unlink = 0,
};

static vfs_node_ops_t tarfs_file_ops = {
    .read = tarfs_read,
    .write = tarfs_write,
    .create = 0,
    .unlink = 0,
};

static uint64_t parse_octal(const char* s, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == 0) break;
        if (c < '0' || c > '7') continue;
        v = (v << 3) + (uint64_t)(c - '0');
    }
    return v;
}

static const char* normalize(const char* path, char* tmp, size_t tmpn) {
    /* strip leading "./" */
    if (path[0] == '.' && path[1] == '/') path += 2;
    size_t n = strlen(path);
    if (n >= tmpn) n = tmpn - 1;
    for (size_t i = 0; i < n; i++) tmp[i] = path[i];
    tmp[n] = 0;
    return tmp;
}

void tarfs_init(const uint8_t* start, const uint8_t* end) {
    g_start = start;
    g_end = end;

    console_write("[tarfs] initramfs at ");
    console_write_hex64((uint64_t)(uintptr_t)start);
    console_write(" - ");
    console_write_hex64((uint64_t)(uintptr_t)end);
    console_write("\n");
}

static void tarfs_add_entry(vfs_node_t* root, const char* path, const uint8_t* data, size_t size, bool is_dir) {
    if (!root || !path || path[0] == 0) return;

    const char* p = path;
    vfs_node_t* cur = root;
    char name[VFS_NAME_MAX + 1];

    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '/') len++;
        if (len > VFS_NAME_MAX) return;
        strncpy(name, p, len);
        name[len] = 0;

        if (p[len] == '/') {
            vfs_node_t* next = vfs_find_child(cur, name);
            if (!next) {
                next = vfs_create_node(name, VFS_NODE_DIR, &tarfs_dir_ops, 0);
                if (!next) return;
                if (vfs_add_child(cur, next) != 0) return;
            }
            cur = next;
            p += len + 1;
            continue;
        }

        if (is_dir) {
            if (!vfs_find_child(cur, name)) {
                vfs_node_t* node = vfs_create_node(name, VFS_NODE_DIR, &tarfs_dir_ops, 0);
                if (!node) return;
                vfs_add_child(cur, node);
            }
        } else {
            if (!vfs_find_child(cur, name)) {
                tarfs_file_t* file = (tarfs_file_t*)kmalloc(sizeof(tarfs_file_t));
                if (!file) return;
                file->data = data;
                file->size = size;
                vfs_node_t* node = vfs_create_node(name, VFS_NODE_FILE, &tarfs_file_ops, file);
                if (!node) {
                    kfree(file);
                    return;
                }
                node->size = size;
                vfs_add_child(cur, node);
            }
        }
        return;
    }
}

bool tarfs_find(const char* path, const uint8_t** out_data, size_t* out_size) {
    if (!g_start || !g_end) return false;

    char want[128];
    normalize(path, want, sizeof(want));

    const uint8_t* p = g_start;
    while (p + 512 <= g_end) {
        const tar_header_t* h = (const tar_header_t*)p;

        /* End of archive: two consecutive zero blocks, but checking name[0]==0 works */
        if (h->name[0] == 0) return false;

        uint64_t fsize = parse_octal(h->size, sizeof(h->size));
        const uint8_t* fdata = p + 512;

        char name[256];
        /* handle prefix/name */
        size_t prefix_len = 0;
        while (prefix_len < sizeof(h->prefix) && h->prefix[prefix_len]) prefix_len++;
        size_t name_len = 0;
        while (name_len < sizeof(h->name) && h->name[name_len]) name_len++;

        size_t idx = 0;
        if (prefix_len > 0) {
            for (size_t i = 0; i < prefix_len && idx + 1 < sizeof(name); i++) name[idx++] = h->prefix[i];
            if (idx + 1 < sizeof(name)) name[idx++] = '/';
        }
        for (size_t i = 0; i < name_len && idx + 1 < sizeof(name); i++) name[idx++] = h->name[i];
        name[idx] = 0;

        /* strip leading "./" if present in tar name */
        const char* tname = name;
        if (tname[0] == '.' && tname[1] == '/') tname += 2;

        if (strcmp(tname, want) == 0) {
            *out_data = fdata;
            *out_size = (size_t)fsize;
            return true;
        }

        uint64_t advance = 512 + align_up_u64(fsize, 512);
        p += advance;
    }

    return false;
}

void tarfs_populate_vfs(vfs_node_t* root) {
    if (!g_start || !g_end || !root) return;

    const uint8_t* p = g_start;
    while (p + 512 <= g_end) {
        const tar_header_t* h = (const tar_header_t*)p;
        if (h->name[0] == 0) return;

        uint64_t fsize = parse_octal(h->size, sizeof(h->size));
        const uint8_t* fdata = p + 512;

        char name[256];
        size_t prefix_len = 0;
        while (prefix_len < sizeof(h->prefix) && h->prefix[prefix_len]) prefix_len++;
        size_t name_len = 0;
        while (name_len < sizeof(h->name) && h->name[name_len]) name_len++;

        size_t idx = 0;
        if (prefix_len > 0) {
            for (size_t i = 0; i < prefix_len && idx + 1 < sizeof(name); i++) name[idx++] = h->prefix[i];
            if (idx + 1 < sizeof(name)) name[idx++] = '/';
        }
        for (size_t i = 0; i < name_len && idx + 1 < sizeof(name); i++) name[idx++] = h->name[i];
        name[idx] = 0;

        const char* tname = name;
        if (tname[0] == '.' && tname[1] == '/') tname += 2;

        bool is_dir = (h->typeflag == '5');
        size_t tlen = strlen(tname);
        if (tlen > 0 && tname[tlen - 1] == '/') is_dir = true;

        char clean[256];
        size_t clen = tlen;
        if (clen >= sizeof(clean)) clen = sizeof(clean) - 1;
        strncpy(clean, tname, clen);
        clean[clen] = 0;
        if (clen > 0 && clean[clen - 1] == '/') clean[clen - 1] = 0;

        tarfs_add_entry(root, clean, fdata, (size_t)fsize, is_dir);

        uint64_t advance = 512 + align_up_u64(fsize, 512);
        p += advance;
    }
}
