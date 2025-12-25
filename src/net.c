#include "net.h"
#include "console.h"
#include "lib.h"

#define NET_MAX_SOCKETS 32
#define NET_MAX_QUEUE 16
#define NET_MAX_PAYLOAD 1500

typedef enum {
    NET_STATE_FREE = 0,
    NET_STATE_BOUND,
    NET_STATE_LISTEN,
    NET_STATE_CONNECTED
} net_state_t;

typedef struct {
    size_t len;
    uint8_t data[NET_MAX_PAYLOAD];
    net_sockaddr_in_t src;
} net_msg_t;

typedef struct {
    int in_use;
    int type;
    net_state_t state;
    net_sockaddr_in_t local;
    net_sockaddr_in_t remote;
    int peer;
    net_msg_t queue[NET_MAX_QUEUE];
    size_t q_head;
    size_t q_tail;
    int pending[NET_MAX_QUEUE];
    size_t p_head;
    size_t p_tail;
} net_socket_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];

static int index_from_fd(int fd) {
    return fd - 1;
}

static int fd_from_index(int idx) {
    return idx + 1;
}

static net_socket_t* get_socket(int fd) {
    int idx = index_from_fd(fd);
    if (idx < 0 || idx >= NET_MAX_SOCKETS) return 0;
    if (!g_sockets[idx].in_use) return 0;
    return &g_sockets[idx];
}

static int alloc_socket(void) {
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(g_sockets[i]));
            g_sockets[i].in_use = 1;
            g_sockets[i].peer = -1;
            return fd_from_index(i);
        }
    }
    return -1;
}

static void enqueue_msg(net_socket_t* s, const net_msg_t* msg) {
    size_t next = (s->q_head + 1) % NET_MAX_QUEUE;
    if (next == s->q_tail) return;
    s->queue[s->q_head] = *msg;
    s->q_head = next;
}

static int dequeue_msg(net_socket_t* s, net_msg_t* out) {
    if (s->q_tail == s->q_head) return 0;
    *out = s->queue[s->q_tail];
    s->q_tail = (s->q_tail + 1) % NET_MAX_QUEUE;
    return 1;
}

static void enqueue_pending(net_socket_t* s, int fd) {
    size_t next = (s->p_head + 1) % NET_MAX_QUEUE;
    if (next == s->p_tail) return;
    s->pending[s->p_head] = fd;
    s->p_head = next;
}

static int dequeue_pending(net_socket_t* s) {
    if (s->p_tail == s->p_head) return -1;
    int fd = s->pending[s->p_tail];
    s->p_tail = (s->p_tail + 1) % NET_MAX_QUEUE;
    return fd;
}

void net_init(void) {
    memset(g_sockets, 0, sizeof(g_sockets));
    console_write("[net] loopback stack initialized\n");
}

void net_pci_probe(const pci_dev_t* dev) {
    if (dev->class_code != 0x02) return;
    console_write("[net] PCI network dev ");
    console_write_hex32(dev->vendor_id);
    console_write(":");
    console_write_hex32(dev->device_id);
    console_write(" at ");
    console_write_dec_u64(dev->bus);
    console_write(":");
    console_write_dec_u64(dev->slot);
    console_write(".");
    console_write_dec_u64(dev->func);
    console_write("\n");

    if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x10D3)) {
        console_write("[net] e1000 detected (driver stub active)\n");
    }
}

int net_socket(int domain, int type) {
    if (domain != NET_AF_INET) return -1;
    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM) return -1;
    int fd = alloc_socket();
    if (fd < 0) return -1;
    net_socket_t* s = get_socket(fd);
    s->type = type;
    s->state = NET_STATE_BOUND;
    s->local.addr = 0x7F000001u;
    return fd;
}

int net_bind(int fd, const net_sockaddr_in_t* addr) {
    net_socket_t* s = get_socket(fd);
    if (!s || !addr) return -1;
    s->local = *addr;
    s->state = NET_STATE_BOUND;
    return 0;
}

int net_listen(int fd) {
    net_socket_t* s = get_socket(fd);
    if (!s || s->type != NET_SOCK_STREAM) return -1;
    s->state = NET_STATE_LISTEN;
    return 0;
}

int net_accept(int fd, net_sockaddr_in_t* addr) {
    net_socket_t* s = get_socket(fd);
    if (!s || s->state != NET_STATE_LISTEN) return -1;
    int child_fd = dequeue_pending(s);
    if (child_fd < 0) return -1;
    net_socket_t* child = get_socket(child_fd);
    if (child && addr) {
        *addr = child->remote;
    }
    return child_fd;
}

int net_connect(int fd, const net_sockaddr_in_t* addr) {
    net_socket_t* client = get_socket(fd);
    if (!client || client->type != NET_SOCK_STREAM || !addr) return -1;

    net_socket_t* listener = 0;
    int listener_fd = -1;
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) continue;
        if (g_sockets[i].state != NET_STATE_LISTEN) continue;
        if (g_sockets[i].local.port == addr->port) {
            listener = &g_sockets[i];
            listener_fd = fd_from_index(i);
            break;
        }
    }
    if (!listener) return -1;

    int server_fd = alloc_socket();
    if (server_fd < 0) return -1;
    net_socket_t* server = get_socket(server_fd);
    server->type = NET_SOCK_STREAM;
    server->state = NET_STATE_CONNECTED;
    server->local = listener->local;
    server->remote = client->local;
    server->peer = index_from_fd(fd);

    client->remote = *addr;
    client->state = NET_STATE_CONNECTED;
    client->peer = index_from_fd(server_fd);

    enqueue_pending(listener, server_fd);
    (void)listener_fd;
    return 0;
}

int net_sendto(int fd, const void* buf, size_t len, const net_sockaddr_in_t* addr) {
    net_socket_t* s = get_socket(fd);
    if (!s || !buf) return -1;
    if (len > NET_MAX_PAYLOAD) len = NET_MAX_PAYLOAD;

    net_msg_t msg;
    msg.len = len;
    msg.src = s->local;
    memcpy(msg.data, buf, len);

    if (s->type == NET_SOCK_DGRAM) {
        if (!addr) return -1;
        for (int i = 0; i < NET_MAX_SOCKETS; i++) {
            if (!g_sockets[i].in_use) continue;
            if (g_sockets[i].type != NET_SOCK_DGRAM) continue;
            if (g_sockets[i].local.port == addr->port) {
                enqueue_msg(&g_sockets[i], &msg);
                return (int)len;
            }
        }
        return (int)len;
    }

    if (s->type == NET_SOCK_STREAM && s->state == NET_STATE_CONNECTED && s->peer >= 0) {
        net_socket_t* peer = &g_sockets[s->peer];
        if (!peer->in_use) return -1;
        enqueue_msg(peer, &msg);
        return (int)len;
    }

    return -1;
}

int net_recvfrom(int fd, void* buf, size_t len, net_sockaddr_in_t* addr) {
    net_socket_t* s = get_socket(fd);
    if (!s || !buf) return -1;
    net_msg_t msg;
    if (!dequeue_msg(s, &msg)) return 0;
    if (len > msg.len) len = msg.len;
    memcpy(buf, msg.data, len);
    if (addr) *addr = msg.src;
    return (int)len;
}

int net_close(int fd) {
    net_socket_t* s = get_socket(fd);
    if (!s) return -1;
    if (s->peer >= 0 && s->peer < NET_MAX_SOCKETS) {
        g_sockets[s->peer].peer = -1;
    }
    memset(s, 0, sizeof(*s));
    return 0;
}
