#pragma once

#include "esp_err.h"

typedef enum {
    STATUS_LED_OFF = 0,
    STATUS_LED_LISTENING,
    STATUS_LED_ERROR,
} status_led_state_t;

/*
 * Drives the 12 RGB LEDs in the StackChan body through its PY32 I/O
 * expander. A missing body is valid: initialization reports ESP_ERR_NOT_FOUND
 * and every later state update becomes a no-op.
 */
esp_err_t status_led_init(void);
void status_led_set(status_led_state_t state);
