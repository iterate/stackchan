#pragma once

#include "esp_err.h"

typedef esp_err_t (*web_control_command_t)(void *context);

typedef struct {
    web_control_command_t start_conversation;
    web_control_command_t stop_conversation;
    void *context;
} web_control_callbacks_t;

esp_err_t web_control_start(const web_control_callbacks_t *callbacks);
