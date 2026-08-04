#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_station_connect(uint32_t timeout_ms, char *ip_address, size_t ip_address_size);
