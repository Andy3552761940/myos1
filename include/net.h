#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pci.h"

#define NET_AF_INET 2
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM 2

#define NET_IF_NAME_MAX 8
#define NET_IF_SET_ADDR 0x1
#define NET_IF_SET_NETMASK 0x2
#define NET_IF_SET_MAC 0x4
#define NET_IF_SET_UP 0x8

typedef struct {
    uint32_t addr;
    uint16_t port;
} net_sockaddr_in_t;

typedef struct {
    char name[NET_IF_NAME_MAX];
    uint8_t mac[6];
    uint32_t addr;
    uint32_t netmask;
    uint8_t up;
    uint8_t present;
    uint8_t reserved[2];
} net_ifinfo_t;

typedef struct {
    char name[NET_IF_NAME_MAX];
    uint32_t addr;
    uint32_t netmask;
    uint8_t mac[6];
    uint8_t up;
    uint32_t flags;
} net_ifreq_t;

typedef struct {
    uint32_t dest;
    uint32_t netmask;
    uint32_t gateway;
} net_route_t;

void net_init(void);
void net_pci_probe(const pci_dev_t* dev);

int net_socket(int domain, int type);
int net_bind(int fd, const net_sockaddr_in_t* addr);
int net_listen(int fd);
int net_accept(int fd, net_sockaddr_in_t* addr);
int net_connect(int fd, const net_sockaddr_in_t* addr);
int net_sendto(int fd, const void* buf, size_t len, const net_sockaddr_in_t* addr);
int net_recvfrom(int fd, void* buf, size_t len, net_sockaddr_in_t* addr);
int net_close(int fd);

int net_if_get(size_t index, net_ifinfo_t* out);
int net_if_set(const net_ifreq_t* req);

int net_route_get(size_t index, net_route_t* out);
int net_route_add(const net_route_t* route);
