#pragma once
#include <stdbool.h>
#include "arch/x86_64/interrupts.h"

void gdb_init(void);
bool gdb_handle_exception(intr_frame_t* frame, intr_frame_t** out_frame);
