#include "debug_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FORMAT_BUFFER_BYTES 512

static char *s_ring;
static char *s_format_buffer;
static size_t s_write_offset;
static size_t s_used;
static vprintf_like_t s_previous_vprintf;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

static void append_bytes_locked(const char *bytes, size_t count)
{
    if (s_ring == NULL || bytes == NULL || count == 0) {
        return;
    }
    if (count > STACKCHAN_DEBUG_LOG_BYTES) {
        bytes += count - STACKCHAN_DEBUG_LOG_BYTES;
        count = STACKCHAN_DEBUG_LOG_BYTES;
    }

    const size_t first =
        count < STACKCHAN_DEBUG_LOG_BYTES - s_write_offset
            ? count
            : STACKCHAN_DEBUG_LOG_BYTES - s_write_offset;
    memcpy(s_ring + s_write_offset, bytes, first);
    if (count > first) {
        memcpy(s_ring, bytes + first, count - first);
    }
    s_write_offset = (s_write_offset + count) % STACKCHAN_DEBUG_LOG_BYTES;
    s_used += count;
    if (s_used > STACKCHAN_DEBUG_LOG_BYTES) {
        s_used = STACKCHAN_DEBUG_LOG_BYTES;
    }
}

static int capture_vprintf(const char *format, va_list arguments)
{
    va_list console_arguments;
    va_copy(console_arguments, arguments);
    const int console_result =
        s_previous_vprintf != NULL
            ? s_previous_vprintf(format, console_arguments)
            : vprintf(format, console_arguments);
    va_end(console_arguments);

    if (s_mutex == NULL ||
        xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        return console_result;
    }

    va_list capture_arguments;
    va_copy(capture_arguments, arguments);
    const int required =
        vsnprintf(s_format_buffer, FORMAT_BUFFER_BYTES, format,
                  capture_arguments);
    va_end(capture_arguments);
    if (required > 0) {
        size_t available = (size_t)required;
        if (available >= FORMAT_BUFFER_BYTES) {
            available = FORMAT_BUFFER_BYTES - 1;
        }
        append_bytes_locked(s_format_buffer, available);
    }
    xSemaphoreGive(s_mutex);
    return console_result;
}

esp_err_t debug_log_init(void)
{
    if (s_ring != NULL) {
        return ESP_OK;
    }
    s_ring = heap_caps_calloc(
        STACKCHAN_DEBUG_LOG_BYTES, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ring == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_format_buffer = heap_caps_malloc(
        FORMAT_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_format_buffer == NULL) {
        heap_caps_free(s_ring);
        s_ring = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (s_mutex == NULL) {
        heap_caps_free(s_format_buffer);
        heap_caps_free(s_ring);
        s_format_buffer = NULL;
        s_ring = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_previous_vprintf = esp_log_set_vprintf(capture_vprintf);
    ESP_LOGI("debug_log", "In-memory log capture ready (%u bytes)",
             STACKCHAN_DEBUG_LOG_BYTES);
    return ESP_OK;
}

size_t debug_log_copy(char *destination, size_t capacity)
{
    if (destination == NULL || capacity == 0 || s_ring == NULL) {
        return 0;
    }

    if (s_mutex == NULL ||
        xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    size_t count = s_used < capacity ? s_used : capacity;
    const size_t oldest =
        (s_write_offset + STACKCHAN_DEBUG_LOG_BYTES - s_used) %
        STACKCHAN_DEBUG_LOG_BYTES;
    const size_t skipped = s_used - count;
    const size_t start =
        (oldest + skipped) % STACKCHAN_DEBUG_LOG_BYTES;
    const size_t first =
        count < STACKCHAN_DEBUG_LOG_BYTES - start
            ? count
            : STACKCHAN_DEBUG_LOG_BYTES - start;
    memcpy(destination, s_ring + start, first);
    if (count > first) {
        memcpy(destination + first, s_ring, count - first);
    }
    xSemaphoreGive(s_mutex);
    return count;
}
