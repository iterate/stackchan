#include "realtime_event_log.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static realtime_event_record_t *s_records;
static size_t s_write_index;
static size_t s_count;
static uint32_t s_next_sequence = 1;
static uint32_t s_overwritten_records;
static uint32_t s_lock_contention_drops;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

static void copy_bounded(char *destination, size_t capacity,
                         const char *source)
{
    if (destination == NULL || capacity == 0) {
        return;
    }
    snprintf(destination, capacity, "%s", source != NULL ? source : "");
}

esp_err_t realtime_event_log_init(void)
{
    if (s_records != NULL) {
        return ESP_OK;
    }

    s_records = heap_caps_calloc(
        STACKCHAN_REALTIME_EVENT_LOG_CAPACITY,
        sizeof(realtime_event_record_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_records == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (s_mutex == NULL) {
        heap_caps_free(s_records);
        s_records = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void realtime_event_log_record(const char *direction, const char *kind,
                               const char *type, const char *text,
                               size_t payload_bytes, size_t audio_samples)
{
    if (s_records == NULL || s_mutex == NULL ||
        xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        __atomic_fetch_add(
            &s_lock_contention_drops, 1, __ATOMIC_RELAXED);
        return;
    }

    realtime_event_record_t *record = &s_records[s_write_index];
    memset(record, 0, sizeof(*record));
    record->sequence = s_next_sequence++;
    record->timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000);
    record->payload_bytes =
        payload_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)payload_bytes;
    record->audio_samples =
        audio_samples > UINT32_MAX ? UINT32_MAX : (uint32_t)audio_samples;
    copy_bounded(record->direction, sizeof(record->direction), direction);
    copy_bounded(record->kind, sizeof(record->kind), kind);
    copy_bounded(record->type, sizeof(record->type), type);
    copy_bounded(record->text, sizeof(record->text), text);

    s_write_index =
        (s_write_index + 1) % STACKCHAN_REALTIME_EVENT_LOG_CAPACITY;
    if (s_count < STACKCHAN_REALTIME_EVENT_LOG_CAPACITY) {
        ++s_count;
    } else {
        ++s_overwritten_records;
    }
    xSemaphoreGive(s_mutex);
}

size_t realtime_event_log_copy(realtime_event_record_t *destination,
                               size_t capacity,
                               realtime_event_log_info_t *info)
{
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->capacity = STACKCHAN_REALTIME_EVENT_LOG_CAPACITY;
    }
    if (s_records == NULL || s_mutex == NULL ||
        xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return 0;
    }

    const size_t count = s_count < capacity ? s_count : capacity;
    const size_t skipped = s_count - count;
    const size_t oldest =
        (s_write_index + STACKCHAN_REALTIME_EVENT_LOG_CAPACITY - s_count) %
        STACKCHAN_REALTIME_EVENT_LOG_CAPACITY;
    const size_t start =
        (oldest + skipped) % STACKCHAN_REALTIME_EVENT_LOG_CAPACITY;
    for (size_t index = 0; index < count; ++index) {
        destination[index] =
            s_records[(start + index) %
                      STACKCHAN_REALTIME_EVENT_LOG_CAPACITY];
    }
    if (info != NULL) {
        info->available = s_count;
        info->newest_sequence =
            s_next_sequence > 1 ? s_next_sequence - 1 : 0;
        info->overwritten_records = s_overwritten_records;
        info->lock_contention_drops =
            __atomic_load_n(
                &s_lock_contention_drops, __ATOMIC_RELAXED);
    }
    xSemaphoreGive(s_mutex);
    return count;
}
