#pragma once
#include <stdint.h>
#include <stdbool.h>

bool virtio_blk_try_init_legacy(uint8_t bus, uint8_t slot, uint8_t func);
bool virtio_blk_read_sector(uint64_t sector, void* out512);
bool virtio_blk_write_sector(uint64_t sector, const void* in512);
bool virtio_blk_is_ready(void);
