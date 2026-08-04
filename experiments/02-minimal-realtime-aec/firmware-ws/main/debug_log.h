#pragma once

#include <stddef.h>

#include "esp_err.h"

#define STACKCHAN_DEBUG_LOG_BYTES (32 * 1024)

esp_err_t debug_log_init(void);
size_t debug_log_copy(char *destination, size_t capacity);
