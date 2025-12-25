#pragma once
#include <stdint.h>

typedef enum {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_ERROR = 2,
} log_level_t;

typedef enum {
    LOG_TARGET_CONSOLE = 1u << 0,
    LOG_TARGET_SERIAL  = 1u << 1,
} log_target_t;

void log_init(log_level_t level, uint32_t targets);
void log_set_level(log_level_t level);
void log_set_targets(uint32_t targets);
log_level_t log_get_level(void);
uint32_t log_get_targets(void);

void log_printf(log_level_t level, const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
