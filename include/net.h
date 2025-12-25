#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pci.h"

#define NET_AF_INET 2
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM 2

typedef struct {
    uint32_t addr;
    uint16_t port;
} net_sockaddr_in_t;

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
