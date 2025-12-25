#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Initialize tarfs with a ustar tar archive in memory. */
void tarfs_init(const uint8_t* start, const uint8_t* end);

/* Find a file by path. Path may be "init.elf" or "./init.elf". */
bool tarfs_find(const char* path, const uint8_t** out_data, size_t* out_size);
