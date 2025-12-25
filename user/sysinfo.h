#pragma once
#include <stdint.h>

#define UTSNAME_LEN 32

typedef struct utsname {
    char sysname[UTSNAME_LEN];
    char nodename[UTSNAME_LEN];
    char release[UTSNAME_LEN];
    char version[UTSNAME_LEN];
    char machine[UTSNAME_LEN];
} utsname_t;

typedef struct sysinfo {
    uint64_t uptime;
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
} sysinfo_t;
