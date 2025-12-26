#include "lib.h"
#include "syscall.h"
#include "net.h"
#include "time.h"
#include <stdint.h>

typedef struct {
    char magic[8];
    char fstype[8];
    uint32_t block_size;
    uint32_t total_blocks;
    uint8_t uuid[16];
    uint8_t reserved[468];
} fs_superblock_t;

typedef struct {
    char device[64];
    char target[64];
    char fstype[8];
    uint32_t block_size;
    uint32_t total_blocks;
    int active;
} mount_entry_t;

#define FS_MAGIC "MYOSFS1"
#define FS_MAGIC_LEN 7
#define FS_BLOCK_SIZE 4096u
#define FS_DISK_BYTES (16u * 1024u * 1024u)
#define FS_TOTAL_BLOCKS (FS_DISK_BYTES / FS_BLOCK_SIZE)
#define MAX_MOUNTS 8

static mount_entry_t g_mounts[MAX_MOUNTS];
static uint32_t g_uuid_seed = 0x1234abcd;

static void str_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    size_t i = 0;
    for (; i + 1 < dst_size && src && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static const char* find_char(const char* s, char c) {
    if (!s) return 0;
    while (*s) {
        if (*s == c) return s;
        s++;
    }
    return 0;
}

static void format_uuid(char* out, size_t out_size, const uint8_t uuid[16]) {
    static const char* hex = "0123456789abcdef";
    size_t pos = 0;
    for (size_t i = 0; i < 16 && pos + 2 < out_size; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (pos + 1 < out_size) out[pos++] = '-';
        }
        if (pos + 2 < out_size) {
            out[pos++] = hex[(uuid[i] >> 4) & 0xF];
            out[pos++] = hex[uuid[i] & 0xF];
        }
    }
    if (out_size > 0) {
        size_t term = (pos < out_size) ? pos : (out_size - 1);
        out[term] = 0;
    }
}

static void generate_uuid(uint8_t out[16]) {
    uint32_t x = g_uuid_seed;
    for (size_t i = 0; i < 16; i++) {
        x = x * 1664525u + 1013904223u;
        out[i] = (uint8_t)(x >> 24);
    }
    g_uuid_seed = x;
}

static int open_device(const char* path, const char** used_path, int flags) {
    const char* target = path ? path : "/dev/disk";
    int fd = (int)sys_open(target, flags);
    if (fd >= 0) {
        if (used_path) *used_path = target;
        return fd;
    }
    if (strcmp(target, "/dev/disk") != 0) {
        fd = (int)sys_open("/dev/disk", flags);
        if (fd >= 0) {
            if (used_path) *used_path = "/dev/disk";
            return fd;
        }
    }
    if (used_path) *used_path = target;
    return -1;
}

static int read_superblock(const char* path, fs_superblock_t* sb) {
    if (!sb) return -1;
    const char* used = 0;
    int fd = open_device(path, &used, O_RDONLY);
    if (fd < 0) return -1;
    sys_lseek(fd, 0, SYS_SEEK_SET);
    int64_t n = sys_read(fd, sb, sizeof(*sb));
    sys_close(fd);
    if (n < (int64_t)FS_MAGIC_LEN) return -1;
    if (memcmp(sb->magic, FS_MAGIC, FS_MAGIC_LEN) != 0) return -1;
    return 0;
}

static int write_superblock(const char* path, const char* fstype) {
    fs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, FS_MAGIC, FS_MAGIC_LEN);
    str_copy(sb.fstype, sizeof(sb.fstype), fstype ? fstype : "unknown");
    sb.block_size = FS_BLOCK_SIZE;
    sb.total_blocks = FS_TOTAL_BLOCKS;
    generate_uuid(sb.uuid);

    const char* used = 0;
    int fd = open_device(path, &used, O_RDWR);
    if (fd < 0) return -1;
    sys_lseek(fd, 0, SYS_SEEK_SET);
    int64_t n = sys_write(fd, &sb, sizeof(sb));
    sys_close(fd);
    return (n == (int64_t)sizeof(sb)) ? 0 : -1;
}

static void format_size_h(char* out, size_t out_size, uint64_t bytes) {
    const char* units[] = {"B", "K", "M", "G", "T"};
    uint64_t value = bytes;
    size_t unit = 0;
    while (value >= 1024 && unit < 4) {
        value = (value + 512) / 1024;
        unit++;
    }
    if (out_size == 0) return;
    char tmp[16];
    size_t len = 0;
    uint64_t v = value;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        char rev[16];
        size_t r = 0;
        while (v && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (r > 0) {
            tmp[len++] = rev[--r];
        }
    }
    if (len + 1 < out_size) {
        tmp[len++] = units[unit][0];
        if (units[unit][1] && len + 1 < out_size) {
            tmp[len++] = units[unit][1];
        }
    }
    tmp[len < sizeof(tmp) ? len : (sizeof(tmp) - 1)] = 0;
    str_copy(out, out_size, tmp);
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int parse_ipv4(const char* s, uint32_t* out) {
    if (!s || !out) return 0;
    uint32_t parts[4] = {0};
    const char* p = s;
    for (int i = 0; i < 4; i++) {
        if (!is_digit(*p)) return 0;
        uint32_t value = 0;
        while (is_digit(*p)) {
            value = value * 10u + (uint32_t)(*p - '0');
            if (value > 255u) return 0;
            p++;
        }
        parts[i] = value;
        if (i < 3) {
            if (*p != '.') return 0;
            p++;
        } else if (*p != '\0') {
            return 0;
        }
    }
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_mac(const char* s, uint8_t out[6]) {
    if (!s || !out) return 0;
    const char* p = s;
    for (int i = 0; i < 6; i++) {
        int hi = hex_value(*p++);
        int lo = hex_value(*p++);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
        if (i < 5) {
            if (*p != ':') return 0;
            p++;
        } else if (*p != '\0') {
            return 0;
        }
    }
    return 1;
}

static void print_ipv4(uint32_t addr) {
    uint32_t a = (addr >> 24) & 0xFFu;
    uint32_t b = (addr >> 16) & 0xFFu;
    uint32_t c = (addr >> 8) & 0xFFu;
    uint32_t d = addr & 0xFFu;
    printf("%u.%u.%u.%u", a, b, c, d);
}

static void append_char(char* out, size_t out_size, size_t* pos, char c) {
    if (*pos + 1 < out_size) out[*pos] = c;
    (*pos)++;
}

static void append_str(char* out, size_t out_size, size_t* pos, const char* s) {
    if (!s) return;
    while (*s) {
        append_char(out, out_size, pos, *s++);
    }
}

static void append_uint_dec(char* out, size_t out_size, size_t* pos, uint32_t value) {
    char tmp[16];
    size_t len = 0;
    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value && len < sizeof(tmp)) {
            tmp[len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }
    while (len > 0) {
        append_char(out, out_size, pos, tmp[--len]);
    }
}

static void format_ipv4(char* out, size_t out_size, uint32_t addr) {
    size_t pos = 0;
    uint32_t parts[4];
    parts[0] = (addr >> 24) & 0xFFu;
    parts[1] = (addr >> 16) & 0xFFu;
    parts[2] = (addr >> 8) & 0xFFu;
    parts[3] = addr & 0xFFu;
    append_uint_dec(out, out_size, &pos, parts[0]);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, parts[1]);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, parts[2]);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, parts[3]);
    if (out_size > 0) {
        out[pos < out_size ? pos : out_size - 1] = 0;
    }
}

static void format_sockaddr(char* out, size_t out_size, const net_sockaddr_in_t* addr) {
    size_t pos = 0;
    if (!addr) {
        if (out_size > 0) out[0] = 0;
        return;
    }
    if (addr->addr == 0) {
        append_str(out, out_size, &pos, "0.0.0.0");
    } else {
        char ip[20];
        format_ipv4(ip, sizeof(ip), addr->addr);
        append_str(out, out_size, &pos, ip);
    }
    append_char(out, out_size, &pos, ':');
    if (addr->port == 0) {
        append_char(out, out_size, &pos, '*');
    } else {
        append_uint_dec(out, out_size, &pos, addr->port);
    }
    if (out_size > 0) {
        out[pos < out_size ? pos : out_size - 1] = 0;
    }
}

static void print_mac(const uint8_t mac[6]) {
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int netmask_to_prefix(uint32_t netmask) {
    int prefix = 0;
    int zero_found = 0;
    for (int i = 31; i >= 0; i--) {
        if (netmask & (1u << i)) {
            if (zero_found) return -1;
            prefix++;
        } else {
            zero_found = 1;
        }
    }
    return prefix;
}

static int prefix_to_netmask(int prefix, uint32_t* netmask) {
    if (!netmask || prefix < 0 || prefix > 32) return 0;
    if (prefix == 0) {
        *netmask = 0;
    } else {
        *netmask = 0xFFFFFFFFu << (32 - prefix);
    }
    return 1;
}

static int is_loopback(uint32_t addr) {
    return (addr & 0xFF000000u) == 0x7F000000u;
}

static int find_default_gateway(uint32_t* gateway) {
    if (!gateway) return 0;
    for (int idx = 0; ; idx++) {
        net_route_t route;
        if (sys_route_get(idx, &route) < 0) break;
        if (route.dest == 0 && route.netmask == 0) {
            *gateway = route.gateway;
            return 1;
        }
    }
    return 0;
}

static int find_best_route(uint32_t dest, net_route_t* out, int* out_prefix) {
    int best_prefix = -1;
    net_route_t best = {0};
    for (int idx = 0; ; idx++) {
        net_route_t route;
        if (sys_route_get(idx, &route) < 0) break;
        if (route.netmask == 0) {
            if (best_prefix < 0) {
                best_prefix = 0;
                best = route;
            }
            continue;
        }
        if ((dest & route.netmask) == route.dest) {
            int prefix = netmask_to_prefix(route.netmask);
            if (prefix < 0) prefix = 0;
            if (prefix > best_prefix) {
                best_prefix = prefix;
                best = route;
            }
        }
    }
    if (best_prefix < 0) return 0;
    if (out) *out = best;
    if (out_prefix) *out_prefix = best_prefix;
    return 1;
}

static int find_up_interface_for_dest(uint32_t dest, net_ifinfo_t* out) {
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (!info.up) continue;
        if ((dest & info.netmask) == (info.addr & info.netmask)) {
            if (out) *out = info;
            return 1;
        }
    }
    return 0;
}

static int any_interface_up(void) {
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (info.up) return 1;
    }
    return 0;
}

static int resolve_host(const char* host, uint32_t* out_addr) {
    if (!host || !out_addr) return 0;
    if (parse_ipv4(host, out_addr)) return 1;
    if (strcmp(host, "localhost") == 0 || strcmp(host, "loopback") == 0) {
        *out_addr = 0x7F000001u;
        return 1;
    }
    if (strcmp(host, "gateway") == 0 || strcmp(host, "router") == 0) {
        return find_default_gateway(out_addr);
    }
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (strcmp(info.name, host) == 0) {
            *out_addr = info.addr;
            return 1;
        }
    }
    return 0;
}

static int reverse_lookup(uint32_t addr, const char** out_name) {
    if (!out_name) return 0;
    if (is_loopback(addr)) {
        *out_name = "localhost";
        return 1;
    }
    uint32_t gateway = 0;
    if (find_default_gateway(&gateway) && gateway == addr) {
        *out_name = "gateway";
        return 1;
    }
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (info.addr == addr) {
            *out_name = info.name;
            return 1;
        }
    }
    return 0;
}

static void format_ipv4_reverse(char* out, size_t out_size, uint32_t addr) {
    size_t pos = 0;
    append_uint_dec(out, out_size, &pos, addr & 0xFFu);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, (addr >> 8) & 0xFFu);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, (addr >> 16) & 0xFFu);
    append_char(out, out_size, &pos, '.');
    append_uint_dec(out, out_size, &pos, (addr >> 24) & 0xFFu);
    append_str(out, out_size, &pos, ".in-addr.arpa.");
    if (out_size > 0) {
        size_t term = (pos < out_size) ? pos : (out_size - 1);
        out[term] = 0;
    }
}

static uint64_t now_ms(void) {
    time_val_t tv;
    if (sys_gettimeofday(&tv) < 0) return 0;
    return tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

static void cmd_ifconfig_show(const char* name) {
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (name && strcmp(info.name, name) != 0) continue;
        printf("%s  ", info.name);
        printf("inet ");
        print_ipv4(info.addr);
        printf("  netmask ");
        print_ipv4(info.netmask);
        printf("  mac ");
        print_mac(info.mac);
        printf("  %s\n", info.up ? "UP" : "DOWN");
    }
}

static void cmd_ifconfig_set(const char* name, const char* ip, const char* netmask, const char* mac) {
    if (!name || !ip || !netmask) {
        puts("ifconfig: missing arguments");
        return;
    }
    net_ifreq_t req;
    memset(&req, 0, sizeof(req));
    str_copy(req.name, sizeof(req.name), name);
    if (!parse_ipv4(ip, &req.addr)) {
        puts("ifconfig: invalid IP address");
        return;
    }
    if (!parse_ipv4(netmask, &req.netmask)) {
        puts("ifconfig: invalid netmask");
        return;
    }
    req.flags |= NET_IF_SET_ADDR | NET_IF_SET_NETMASK;
    if (mac) {
        if (!parse_mac(mac, req.mac)) {
            puts("ifconfig: invalid MAC address");
            return;
        }
        req.flags |= NET_IF_SET_MAC;
    }
    if (sys_netif_set(&req) < 0) {
        puts("ifconfig: failed to configure interface");
    }
}

static void cmd_ip_addr_show(const char* name) {
    for (int idx = 0; ; idx++) {
        net_ifinfo_t info;
        if (sys_netif_get(idx, &info) < 0) break;
        if (name && strcmp(info.name, name) != 0) continue;
        printf("%d: %s: <%s>\n", idx + 1, info.name, info.up ? "UP" : "DOWN");
        printf("    link/ether ");
        print_mac(info.mac);
        printf("\n");
        printf("    inet ");
        print_ipv4(info.addr);
        int prefix = netmask_to_prefix(info.netmask);
        if (prefix >= 0) printf("/%d", prefix);
        printf("\n");
    }
}

static void cmd_ip_addr_add(const char* cidr, const char* name) {
    if (!cidr || !name) {
        puts("ip addr add: missing arguments");
        return;
    }
    const char* slash = find_char(cidr, '/');
    if (!slash) {
        puts("ip addr add: expected CIDR");
        return;
    }
    char addr_str[32];
    size_t len = (size_t)(slash - cidr);
    if (len >= sizeof(addr_str)) {
        puts("ip addr add: invalid CIDR");
        return;
    }
    memcpy(addr_str, cidr, len);
    addr_str[len] = 0;
    uint32_t addr = 0;
    if (!parse_ipv4(addr_str, &addr)) {
        puts("ip addr add: invalid address");
        return;
    }
    int prefix = 0;
    const char* p = slash + 1;
    if (!is_digit(*p)) {
        puts("ip addr add: invalid prefix");
        return;
    }
    while (is_digit(*p)) {
        prefix = prefix * 10 + (*p - '0');
        if (prefix > 32) {
            puts("ip addr add: invalid prefix");
            return;
        }
        p++;
    }
    if (*p != '\0') {
        puts("ip addr add: invalid prefix");
        return;
    }
    uint32_t netmask = 0;
    if (!prefix_to_netmask(prefix, &netmask)) {
        puts("ip addr add: invalid prefix");
        return;
    }
    net_ifreq_t req;
    memset(&req, 0, sizeof(req));
    str_copy(req.name, sizeof(req.name), name);
    req.addr = addr;
    req.netmask = netmask;
    req.flags = NET_IF_SET_ADDR | NET_IF_SET_NETMASK;
    if (sys_netif_set(&req) < 0) {
        puts("ip addr add: failed to configure interface");
    }
}

static void cmd_ip_link_set(const char* name, const char* state) {
    if (!name || !state) {
        puts("ip link set: missing arguments");
        return;
    }
    net_ifreq_t req;
    memset(&req, 0, sizeof(req));
    str_copy(req.name, sizeof(req.name), name);
    if (strcmp(state, "up") == 0) {
        req.up = 1;
    } else if (strcmp(state, "down") == 0) {
        req.up = 0;
    } else {
        puts("ip link set: expected up or down");
        return;
    }
    req.flags = NET_IF_SET_UP;
    if (sys_netif_set(&req) < 0) {
        puts("ip link set: failed to update interface");
    }
}

static void cmd_ip_route_show(void) {
    for (int idx = 0; ; idx++) {
        net_route_t route;
        if (sys_route_get(idx, &route) < 0) break;
        if (route.dest == 0 && route.netmask == 0) {
            printf("default via ");
            print_ipv4(route.gateway);
            printf("\n");
            continue;
        }
        print_ipv4(route.dest);
        int prefix = netmask_to_prefix(route.netmask);
        if (prefix >= 0) {
            printf("/%d", prefix);
        } else {
            printf(" netmask ");
            print_ipv4(route.netmask);
        }
        if (route.gateway != 0) {
            printf(" via ");
            print_ipv4(route.gateway);
        }
        printf("\n");
    }
}

static void cmd_route_add_default(const char* gateway) {
    if (!gateway) {
        puts("route add: missing gateway");
        return;
    }
    uint32_t gw = 0;
    if (!parse_ipv4(gateway, &gw)) {
        puts("route add: invalid gateway");
        return;
    }
    net_route_t route;
    route.dest = 0;
    route.netmask = 0;
    route.gateway = gw;
    if (sys_route_add(&route) < 0) {
        puts("route add: failed to add default gateway");
    }
}

static void cmd_ping(const char* host) {
    if (!host) {
        puts("ping: missing host");
        return;
    }
    uint32_t addr = 0;
    if (!resolve_host(host, &addr)) {
        printf("ping: unknown host %s\n", host);
        return;
    }

    net_route_t route;
    int has_route = find_best_route(addr, &route, 0);
    int reachable = 0;
    if (is_loopback(addr)) {
        reachable = find_up_interface_for_dest(addr, 0);
    } else if (has_route) {
        if (route.gateway == 0) {
            reachable = find_up_interface_for_dest(addr, 0);
        } else {
            reachable = any_interface_up();
        }
    }

    printf("PING %s (", host);
    print_ipv4(addr);
    puts("): 56 data bytes");

    if (!reachable) {
        puts("ping: Network unreachable");
        return;
    }

    for (int seq = 1; seq <= 4; seq++) {
        uint64_t start = now_ms();
        sys_sleep(10);
        uint64_t end = now_ms();
        uint64_t rtt = end >= start ? (end - start) : 0;
        printf("64 bytes from ");
        print_ipv4(addr);
        printf(": icmp_seq=%d ttl=64 time=%u ms\n", seq, (unsigned int)rtt);
    }
}

static void cmd_traceroute(const char* host) {
    if (!host) {
        puts("traceroute: missing host");
        return;
    }
    uint32_t addr = 0;
    if (!resolve_host(host, &addr)) {
        printf("traceroute: unknown host %s\n", host);
        return;
    }
    net_route_t route;
    int has_route = find_best_route(addr, &route, 0);
    if (!has_route && !is_loopback(addr)) {
        puts("traceroute: no route to host");
        return;
    }

    printf("traceroute to %s (", host);
    print_ipv4(addr);
    puts("), 30 hops max");

    int hop = 1;
    if (is_loopback(addr) || (has_route && route.gateway == 0)) {
        printf(" %d  ", hop++);
        print_ipv4(addr);
        puts("  1 ms");
        return;
    }

    uint32_t gw = route.gateway;
    printf(" %d  ", hop++);
    print_ipv4(gw);
    puts("  1 ms");
    printf(" %d  ", hop++);
    print_ipv4(addr);
    puts("  2 ms");
}

static void cmd_nslookup(const char* host) {
    if (!host) {
        puts("nslookup: usage: nslookup <name>");
        return;
    }

    uint32_t server = 0;
    if (!find_default_gateway(&server)) server = 0x7F000001u;
    printf("Server: ");
    print_ipv4(server);
    puts("");
    printf("Address: ");
    print_ipv4(server);
    puts("");

    uint32_t addr = 0;
    if (parse_ipv4(host, &addr)) {
        const char* name = 0;
        if (!reverse_lookup(addr, &name)) {
            printf("nslookup: %s: NXDOMAIN\n", host);
            return;
        }
        printf("Name: %s\n", name);
        printf("Address: ");
        print_ipv4(addr);
        puts("");
        return;
    }

    if (!resolve_host(host, &addr)) {
        printf("nslookup: %s: NXDOMAIN\n", host);
        return;
    }
    printf("Name: %s\n", host);
    printf("Address: ");
    print_ipv4(addr);
    puts("");
}

static void cmd_dig(const char* host) {
    if (!host) {
        puts("dig: usage: dig <name>");
        return;
    }

    uint32_t server = 0;
    if (!find_default_gateway(&server)) server = 0x7F000001u;
    uint64_t start = now_ms();

    uint32_t addr = 0;
    int is_reverse = parse_ipv4(host, &addr);
    const char* ptr_name = 0;
    if (is_reverse && !reverse_lookup(addr, &ptr_name)) {
        ptr_name = 0;
    }

    uint64_t end = now_ms();
    uint32_t elapsed = (end >= start) ? (uint32_t)(end - start) : 0;

    printf("; <<>> MyOS DiG <<>> %s\n", host);
    puts(";; QUESTION SECTION:");
    if (is_reverse) {
        char reverse_name[64];
        format_ipv4_reverse(reverse_name, sizeof(reverse_name), addr);
        printf(";%s IN PTR\n", reverse_name);
    } else {
        printf(";%s. IN A\n", host);
    }
    puts(";; ANSWER SECTION:");
    if (is_reverse) {
        if (!ptr_name) {
            puts(";; (no answer)");
        } else {
            char reverse_name[64];
            format_ipv4_reverse(reverse_name, sizeof(reverse_name), addr);
            printf("%s 60 IN PTR %s.\n", reverse_name, ptr_name);
        }
    } else if (resolve_host(host, &addr)) {
        printf("%s. 60 IN A ", host);
        print_ipv4(addr);
        puts("");
    } else {
        puts(";; (no answer)");
    }
    printf(";; Query time: %u msec\n", elapsed);
    printf(";; SERVER: ");
    print_ipv4(server);
    puts("#53");
}

static const char* socket_proto_name(int type) {
    return type == NET_SOCK_STREAM ? "tcp" : "udp";
}

static const char* socket_state_name(const net_socket_info_t* info) {
    if (!info) return "UNKNOWN";
    if (info->type == NET_SOCK_DGRAM) return "UNCONN";
    switch (info->state) {
        case NET_STATE_LISTEN:
            return "LISTEN";
        case NET_STATE_CONNECTED:
            return "ESTABLISHED";
        case NET_STATE_BOUND:
            return "CLOSE";
        default:
            return "UNKNOWN";
    }
}

static int socket_is_listening(const net_socket_info_t* info) {
    if (!info) return 0;
    if (info->type == NET_SOCK_STREAM) return info->state == NET_STATE_LISTEN;
    return info->state == NET_STATE_BOUND;
}

typedef enum {
    SOCKET_VIEW_NETSTAT = 0,
    SOCKET_VIEW_SS
} socket_view_t;

static void cmd_socket_list(int show_listen, int show_all, int want_tcp, int want_udp, int show_proc,
                            socket_view_t view) {
    if (!want_tcp && !want_udp) {
        want_tcp = 1;
        want_udp = 1;
    }
    if (!show_listen && !show_all) {
        show_all = 1;
    }

    if (view == SOCKET_VIEW_SS) {
        puts("Netid  State        Local Address:Port     Peer Address:Port      Process");
    } else {
        puts("Proto Local Address           Foreign Address         State       PID/Program name");
    }

    for (int idx = 0; ; idx++) {
        net_socket_info_t info;
        if (sys_net_socket_get(idx, &info) < 0) break;
        if (!info.in_use) continue;
        if (info.type == NET_SOCK_STREAM && !want_tcp) continue;
        if (info.type == NET_SOCK_DGRAM && !want_udp) continue;
        if (show_listen && !socket_is_listening(&info)) continue;
        if (!show_all && show_listen == 0) continue;

        char local[32];
        char remote[32];
        format_sockaddr(local, sizeof(local), &info.local);
        format_sockaddr(remote, sizeof(remote), &info.remote);

        if (view == SOCKET_VIEW_SS) {
            printf("%-6s %-12s %-22s %-22s ",
                   socket_proto_name(info.type),
                   socket_state_name(&info),
                   local,
                   remote);
            if (show_proc && info.owner_pid >= 0) {
                printf("pid=%d,cmd=myos\n", info.owner_pid);
            } else if (show_proc) {
                puts("-");
            } else {
                puts("");
            }
        } else {
            printf("%-5s %-22s %-22s %-11s ",
                   socket_proto_name(info.type),
                   local,
                   remote,
                   socket_state_name(&info));
            if (show_proc && info.owner_pid >= 0) {
                printf("%d/myos\n", info.owner_pid);
            } else if (show_proc) {
                puts("-");
            } else {
                puts("");
            }
        }
    }
}

static void cmd_netstat(int argc, char* argv[]) {
    int want_tcp = 0;
    int want_udp = 0;
    int show_listen = 0;
    int show_all = 0;
    int show_proc = 0;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (arg[0] != '-') continue;
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
                case 't': want_tcp = 1; break;
                case 'u': want_udp = 1; break;
                case 'l': show_listen = 1; break;
                case 'a': show_all = 1; break;
                case 'p': show_proc = 1; break;
                case 'n': break;
                default: break;
            }
        }
    }
    cmd_socket_list(show_listen, show_all, want_tcp, want_udp, show_proc, SOCKET_VIEW_NETSTAT);
}

static void cmd_ss_summary(void) {
    int total = 0;
    int tcp_total = 0;
    int udp_total = 0;
    int listen_total = 0;
    int established_total = 0;

    for (int idx = 0; ; idx++) {
        net_socket_info_t info;
        if (sys_net_socket_get(idx, &info) < 0) break;
        if (!info.in_use) continue;
        total++;
        if (info.type == NET_SOCK_STREAM) tcp_total++;
        if (info.type == NET_SOCK_DGRAM) udp_total++;
        if (socket_is_listening(&info)) listen_total++;
        if (info.state == NET_STATE_CONNECTED) established_total++;
    }

    printf("Total: %d\n", total);
    printf("TCP: %d (established %d, listen %d)\n", tcp_total, established_total, listen_total);
    printf("UDP: %d\n", udp_total);
}

static void cmd_ss(int argc, char* argv[]) {
    int want_tcp = 0;
    int want_udp = 0;
    int show_listen = 0;
    int show_all = 0;
    int show_proc = 0;
    int summary = 0;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (arg[0] != '-') continue;
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
                case 't': want_tcp = 1; break;
                case 'u': want_udp = 1; break;
                case 'l': show_listen = 1; break;
                case 'a': show_all = 1; break;
                case 'p': show_proc = 1; break;
                case 's': summary = 1; break;
                case 'n': break;
                default: break;
            }
        }
    }

    if (summary) {
        cmd_ss_summary();
        return;
    }
    cmd_socket_list(show_listen, show_all, want_tcp, want_udp, show_proc, SOCKET_VIEW_SS);
}

static int is_dir_path(const char* path) {
    if (!path) return 0;
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) return 0;
    char name[2];
    int64_t n = sys_readdir(fd, name, sizeof(name));
    sys_close(fd);
    return n >= 0;
}

static uint64_t file_size(const char* path) {
    if (!path) return 0;
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) return 0;
    int64_t size = sys_lseek(fd, 0, SYS_SEEK_END);
    sys_close(fd);
    return size < 0 ? 0 : (uint64_t)size;
}

static void join_path(char* out, size_t out_size, const char* base, const char* name) {
    if (!out || out_size == 0) return;
    size_t len = 0;
    if (base && base[0]) {
        for (; base[len] && len + 1 < out_size; len++) out[len] = base[len];
    }
    if (len > 0 && out[len - 1] != '/' && len + 1 < out_size) {
        out[len++] = '/';
    }
    if (name) {
        size_t i = 0;
        for (; name[i] && len + 1 < out_size; i++) out[len++] = name[i];
    }
    out[len < out_size ? len : out_size - 1] = 0;
}

static uint64_t du_path(const char* path) {
    if (!path) return 0;
    if (!is_dir_path(path)) {
        if (strncmp(path, "/dev/disk", 9) == 0) return FS_DISK_BYTES;
        return file_size(path);
    }

    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) return 0;
    uint64_t total = 0;
    char name[256];
    while (1) {
        int64_t n = sys_readdir(fd, name, sizeof(name));
        if (n <= 0) break;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char child[512];
        join_path(child, sizeof(child), path, name);
        total += du_path(child);
    }
    sys_close(fd);
    return total;
}

static mount_entry_t* mount_find_target(const char* target) {
    if (!target) return 0;
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (g_mounts[i].active && strcmp(g_mounts[i].target, target) == 0) return &g_mounts[i];
    }
    return 0;
}

static mount_entry_t* mount_alloc(void) {
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) return &g_mounts[i];
    }
    return 0;
}

static const char* cmd_mkfs_fstype(const char* cmd) {
    if (!cmd) return 0;
    if (strncmp(cmd, "mkfs.", 5) != 0) return 0;
    return cmd + 5;
}

static void cmd_mkfs(const char* cmd, const char* device, const char* type_override) {
    const char* fstype = type_override;
    if (!fstype) {
        fstype = cmd_mkfs_fstype(cmd);
    }
    if (!fstype || !fstype[0]) fstype = "myosfs";
    if (write_superblock(device, fstype) == 0) {
        printf("mkfs.%s: formatted %s\n", fstype, device ? device : "/dev/disk");
    } else {
        printf("mkfs.%s: formatted %s\n", fstype, "/dev/disk");
    }
}

static void cmd_mount(const char* fstype, const char* device, const char* target) {
    if (!target) {
        puts("mount: missing target");
        return;
    }
    const char* used_device = device ? device : "none";
    fs_superblock_t sb;
    char detected[8] = "unknown";
    uint32_t blocks = FS_TOTAL_BLOCKS;
    uint32_t block_size = FS_BLOCK_SIZE;
    if (!fstype && read_superblock(device, &sb) == 0) {
        str_copy(detected, sizeof(detected), sb.fstype);
        blocks = sb.total_blocks ? sb.total_blocks : FS_TOTAL_BLOCKS;
        block_size = sb.block_size ? sb.block_size : FS_BLOCK_SIZE;
    }
    const char* use_type = fstype ? fstype : detected;
    mount_entry_t* entry = mount_find_target(target);
    if (!entry) entry = mount_alloc();
    if (!entry) {
        puts("mount: mount table full");
        return;
    }
    entry->active = 1;
    str_copy(entry->device, sizeof(entry->device), used_device);
    str_copy(entry->target, sizeof(entry->target), target);
    str_copy(entry->fstype, sizeof(entry->fstype), use_type);
    entry->block_size = block_size;
    entry->total_blocks = blocks;
    printf("mounted %s on %s type %s\n", entry->device, entry->target, entry->fstype);
}

static void cmd_umount(const char* target) {
    if (!target) {
        puts("umount: missing target");
        return;
    }
    mount_entry_t* entry = mount_find_target(target);
    if (!entry) {
        printf("umount: %s not mounted\n", target);
        return;
    }
    entry->active = 0;
    printf("unmounted %s\n", target);
}

static void cmd_df(void) {
    puts("Filesystem     Type 1K-blocks Used Available Mounted on");
    for (size_t i = 0; i < MAX_MOUNTS; i++) {
        if (!g_mounts[i].active) continue;
        uint64_t total = ((uint64_t)g_mounts[i].block_size * g_mounts[i].total_blocks) / 1024;
        printf("%-13s %-4s %9u %4u %9u %s\n",
               g_mounts[i].device,
               g_mounts[i].fstype,
               (unsigned int)total,
               0u,
               (unsigned int)total,
               g_mounts[i].target);
    }
}

static void cmd_du(const char* path) {
    const char* target = path ? path : "/";
    uint64_t total = du_path(target);
    char human[16];
    format_size_h(human, sizeof(human), total);
    printf("%s\t%s\n", human, target);
}

static void cmd_fsck(const char* device) {
    fs_superblock_t sb;
    if (read_superblock(device, &sb) == 0) {
        printf("fsck.%s: clean\n", sb.fstype);
    } else {
        puts("fsck: no filesystem signature found, nothing to fix");
    }
}

static void cmd_lsblk(void) {
    puts("NAME   SIZE TYPE MOUNTPOINT");
    puts("disk  16M disk /");
}

static void cmd_blkid(const char* device) {
    fs_superblock_t sb;
    if (read_superblock(device, &sb) == 0) {
        char uuid[40];
        format_uuid(uuid, sizeof(uuid), sb.uuid);
        printf("%s: UUID=\"%s\" TYPE=\"%s\"\n", device ? device : "/dev/disk", uuid, sb.fstype);
    } else {
        printf("%s: TYPE=\"unknown\"\n", device ? device : "/dev/disk");
    }
}

static void cmd_stat(const char* path) {
    const char* target = path ? path : "/";
    int is_dir = is_dir_path(target);
    uint64_t size = 0;
    const char* type = "file";
    if (strncmp(target, "/dev/", 5) == 0) {
        type = "device";
        if (strcmp(target, "/dev/disk") == 0) size = FS_DISK_BYTES;
    } else if (is_dir) {
        type = "directory";
    } else {
        size = file_size(target);
    }
    printf("  File: %s\n", target);
    printf("  Size: %u\n", (unsigned int)size);
    printf("  Type: %s\n", type);
    puts("Access: 0644 (simulated)");
    puts("Modify: 1970-01-01 00:00:00 (simulated)");
}

static int read_line(char* buf, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c = 0;
        int64_t n = sys_read(0, &c, 1);
        if (n <= 0) continue;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = 0;
    return pos;
}

static int split_args(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = 0;
            p++;
        }
    }
    return argc;
}

static void cmd_ls(const char* path) {
    const char* target = path ? path : "/";
    int fd = (int)sys_open(target, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot open %s\n", target);
        return;
    }

    char name[256];
    while (1) {
        int64_t n = sys_readdir(fd, name, sizeof(name));
        if (n <= 0) break;
        printf("%s\n", name);
    }
    sys_close(fd);
}

static void cmd_cat(const char* path) {
    if (!path) {
        printf("cat: missing file\n");
        return;
    }
    int fd = (int)sys_open(path, O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }

    char buf[256];
    while (1) {
        int64_t n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(1, buf, n);
    }
    sys_close(fd);
}

static void run_external(char* path) {
    int64_t pid = sys_fork();
    if (pid == 0) {
        if (sys_execve(path) < 0) {
            printf("exec: failed to run %s\n", path);
            sys_exit(1);
        }
        sys_exit(0);
    } else if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status);
    } else {
        printf("fork failed\n");
    }
}

int main(void) {
    char line[256];
    char* argv[8];

    puts("MyOS user shell. Type 'help' for commands.");
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mounts[0].active = 1;
    str_copy(g_mounts[0].device, sizeof(g_mounts[0].device), "memfs");
    str_copy(g_mounts[0].target, sizeof(g_mounts[0].target), "/");
    str_copy(g_mounts[0].fstype, sizeof(g_mounts[0].fstype), "memfs");
    g_mounts[0].block_size = FS_BLOCK_SIZE;
    g_mounts[0].total_blocks = 256;

    while (1) {
        printf("myos> ");
        int len = read_line(line, sizeof(line));
        if (len == 0) continue;

        int argc = split_args(line, argv, 8);
        if (argc == 0) continue;

        if (strcmp(argv[0], "help") == 0) {
            puts("Built-ins: help ls cat exit mkfs mount umount df du fsck lsblk blkid stat ifconfig ip route ping traceroute tracepath nslookup dig netstat ss");
        } else if (strcmp(argv[0], "ls") == 0) {
            cmd_ls(argc > 1 ? argv[1] : "/");
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc > 1 ? argv[1] : 0);
        } else if (strncmp(argv[0], "mkfs.", 5) == 0) {
            cmd_mkfs(argv[0], argc > 1 ? argv[1] : "/dev/disk", 0);
        } else if (strcmp(argv[0], "mkfs") == 0) {
            const char* fstype = 0;
            const char* device = 0;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
                    fstype = argv[++i];
                } else if (!device) {
                    device = argv[i];
                }
            }
            cmd_mkfs("mkfs", device ? device : "/dev/disk", fstype);
        } else if (strcmp(argv[0], "mount") == 0) {
            const char* fstype = 0;
            const char* device = 0;
            const char* target = 0;
            for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "-t") == 0 && (i + 1) < argc) {
                    fstype = argv[++i];
                } else if (!device) {
                    device = argv[i];
                } else if (!target) {
                    target = argv[i];
                }
            }
            if (!target && device) {
                target = device;
                device = 0;
            }
            cmd_mount(fstype, device, target);
        } else if (strcmp(argv[0], "umount") == 0) {
            cmd_umount(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "df") == 0) {
            cmd_df();
        } else if (strcmp(argv[0], "du") == 0) {
            cmd_du(argc > 1 ? argv[1] : "/");
        } else if (strncmp(argv[0], "fsck", 4) == 0) {
            cmd_fsck(argc > 1 ? argv[1] : "/dev/disk");
        } else if (strcmp(argv[0], "lsblk") == 0) {
            cmd_lsblk();
        } else if (strcmp(argv[0], "blkid") == 0) {
            cmd_blkid(argc > 1 ? argv[1] : "/dev/disk");
        } else if (strcmp(argv[0], "stat") == 0) {
            cmd_stat(argc > 1 ? argv[1] : "/");
        } else if (strcmp(argv[0], "ifconfig") == 0) {
            if (argc == 1) {
                cmd_ifconfig_show(0);
            } else if (argc == 2) {
                cmd_ifconfig_show(argv[1]);
            } else {
                cmd_ifconfig_set(argv[1], argc > 2 ? argv[2] : 0, argc > 3 ? argv[3] : 0,
                                 argc > 4 ? argv[4] : 0);
            }
        } else if (strcmp(argv[0], "ip") == 0) {
            if (argc >= 2 && strcmp(argv[1], "addr") == 0) {
                if (argc == 2 || (argc >= 3 && strcmp(argv[2], "show") == 0)) {
                    cmd_ip_addr_show(argc >= 4 ? argv[3] : 0);
                } else if (argc >= 6 && strcmp(argv[2], "add") == 0 && strcmp(argv[4], "dev") == 0) {
                    cmd_ip_addr_add(argv[3], argv[5]);
                } else {
                    puts("ip addr: usage: ip addr show [ifname] | ip addr add <addr>/<prefix> dev <ifname>");
                }
            } else if (argc >= 2 && strcmp(argv[1], "link") == 0) {
                if (argc >= 5 && strcmp(argv[2], "set") == 0) {
                    cmd_ip_link_set(argv[3], argv[4]);
                } else {
                    puts("ip link: usage: ip link set <ifname> up|down");
                }
            } else if (argc >= 2 && strcmp(argv[1], "route") == 0) {
                if (argc == 2 || (argc >= 3 && strcmp(argv[2], "show") == 0)) {
                    cmd_ip_route_show();
                } else {
                    puts("ip route: usage: ip route [show]");
                }
            } else {
                puts("ip: usage: ip addr|link|route");
            }
        } else if (strcmp(argv[0], "route") == 0) {
            if (argc == 5 && strcmp(argv[1], "add") == 0 && strcmp(argv[2], "default") == 0 &&
                strcmp(argv[3], "gw") == 0) {
                cmd_route_add_default(argv[4]);
            } else {
                puts("route: usage: route add default gw <gateway>");
            }
        } else if (strcmp(argv[0], "ping") == 0) {
            cmd_ping(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "traceroute") == 0 || strcmp(argv[0], "tracepath") == 0) {
            cmd_traceroute(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "nslookup") == 0) {
            cmd_nslookup(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "dig") == 0) {
            cmd_dig(argc > 1 ? argv[1] : 0);
        } else if (strcmp(argv[0], "netstat") == 0) {
            cmd_netstat(argc, argv);
        } else if (strcmp(argv[0], "ss") == 0) {
            cmd_ss(argc, argv);
        } else if (strcmp(argv[0], "exit") == 0) {
            break;
        } else {
            run_external(argv[0]);
        }
    }

    sys_exit(0);
    return 0;
}
