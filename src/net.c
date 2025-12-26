#include "net.h"
#include "console.h"
#include "lib.h"

#define NET_MAX_SOCKETS 32
#define NET_MAX_QUEUE 16
#define NET_MAX_PAYLOAD 1500
#define NET_MAX_IFS 2
#define NET_MAX_ROUTES 8

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
    int owner_pid;
    net_msg_t queue[NET_MAX_QUEUE];
    size_t q_head;
    size_t q_tail;
    int pending[NET_MAX_QUEUE];
    size_t p_head;
    size_t p_tail;
} net_socket_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];
static net_ifinfo_t g_netifs[NET_MAX_IFS];
static net_route_t g_routes[NET_MAX_ROUTES];
static size_t g_route_count;

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
    memset(g_netifs, 0, sizeof(g_netifs));
    memset(g_routes, 0, sizeof(g_routes));
    g_route_count = 0;
    strncpy(g_netifs[0].name, "lo", NET_IF_NAME_MAX);
    g_netifs[0].addr = 0x7F000001u;
    g_netifs[0].netmask = 0xFF000000u;
    g_netifs[0].up = 1;
    g_netifs[0].present = 1;

    strncpy(g_netifs[1].name, "eth0", NET_IF_NAME_MAX);
    g_netifs[1].addr = 0xC0A80002u;
    g_netifs[1].netmask = 0xFFFFFF00u;
    g_netifs[1].mac[0] = 0x52;
    g_netifs[1].mac[1] = 0x54;
    g_netifs[1].mac[2] = 0x00;
    g_netifs[1].mac[3] = 0x12;
    g_netifs[1].mac[4] = 0x34;
    g_netifs[1].mac[5] = 0x56;
    g_netifs[1].up = 1;
    g_netifs[1].present = 1;
    g_routes[g_route_count++] = (net_route_t){
        .dest = 0,
        .netmask = 0,
        .gateway = 0xC0A80001u
    };
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

int net_socket(int domain, int type, int owner_pid) {
    if (domain != NET_AF_INET) return -1;
    if (type != NET_SOCK_STREAM && type != NET_SOCK_DGRAM) return -1;
    int fd = alloc_socket();
    if (fd < 0) return -1;
    net_socket_t* s = get_socket(fd);
    s->type = type;
    s->state = NET_STATE_BOUND;
    s->local.addr = 0x7F000001u;
    s->owner_pid = owner_pid;
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
    server->owner_pid = listener->owner_pid;

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

int net_socket_get(size_t index, net_socket_info_t* out) {
    if (!out) return -1;
    if (index >= NET_MAX_SOCKETS) return -1;
    net_socket_t* s = &g_sockets[index];
    out->in_use = s->in_use;
    out->type = s->type;
    out->state = s->state;
    out->local = s->local;
    out->remote = s->remote;
    out->owner_pid = s->owner_pid;
    return 0;
}

int net_if_get(size_t index, net_ifinfo_t* out) {
    if (!out) return -1;
    if (index >= NET_MAX_IFS) return -1;
    if (!g_netifs[index].present) return -1;
    *out = g_netifs[index];
    return 0;
}

int net_if_set(const net_ifreq_t* req) {
    if (!req) return -1;
    for (size_t i = 0; i < NET_MAX_IFS; i++) {
        if (!g_netifs[i].present) continue;
        if (strncmp(g_netifs[i].name, req->name, NET_IF_NAME_MAX) != 0) continue;
        if (req->flags & NET_IF_SET_ADDR) {
            g_netifs[i].addr = req->addr;
        }
        if (req->flags & NET_IF_SET_NETMASK) {
            g_netifs[i].netmask = req->netmask;
        }
        if (req->flags & NET_IF_SET_MAC) {
            memcpy(g_netifs[i].mac, req->mac, sizeof(g_netifs[i].mac));
        }
        if (req->flags & NET_IF_SET_UP) {
            g_netifs[i].up = req->up ? 1 : 0;
        }
        return 0;
    }
    return -1;
}

int net_route_get(size_t index, net_route_t* out) {
    if (!out) return -1;
    if (index >= g_route_count) return -1;
    *out = g_routes[index];
    return 0;
}

int net_route_add(const net_route_t* route) {
    if (!route) return -1;
    for (size_t i = 0; i < g_route_count; i++) {
        if (g_routes[i].dest == route->dest && g_routes[i].netmask == route->netmask) {
            g_routes[i].gateway = route->gateway;
            return 0;
        }
    }
    if (g_route_count >= NET_MAX_ROUTES) return -1;
    g_routes[g_route_count++] = *route;
    return 0;
}
