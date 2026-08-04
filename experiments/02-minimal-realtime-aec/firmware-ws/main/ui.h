#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ui_tap_callback_t)(void);

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    size_t bytes;
} ui_snapshot_t;

esp_err_t ui_init(ui_tap_callback_t tap_callback);
void ui_set_status(const char *title, const char *detail);
void ui_set_button_label(const char *label);
esp_err_t ui_snapshot_rgb565(ui_snapshot_t *snapshot);
void ui_snapshot_release(ui_snapshot_t *snapshot);
