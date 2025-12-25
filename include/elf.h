#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool elf64_load_image(const uint8_t* image, size_t size, uint64_t target_cr3, uint64_t* out_entry, uint64_t* out_brk);
