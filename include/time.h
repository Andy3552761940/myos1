#pragma once
#include <stdint.h>

typedef struct {
    uint64_t tv_sec;
    uint64_t tv_usec;
} time_val_t;

void time_init(void);
void time_gettimeofday(time_val_t* out);
uint64_t time_now_ms(void);
uint64_t time_now_ns(void);
