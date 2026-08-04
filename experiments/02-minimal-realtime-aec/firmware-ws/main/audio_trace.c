#include "audio_trace.h"

#include <string.h>

#include "app_config.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static int16_t *s_samples;
static size_t s_capacity_frames;
static size_t s_write_frame;
static size_t s_frame_count;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

esp_err_t audio_trace_init(void)
{
    if (s_samples != NULL) {
        return ESP_OK;
    }

    s_capacity_frames =
        STACKCHAN_AUDIO_SAMPLE_RATE * STACKCHAN_AUDIO_TRACE_SECONDS;
    const size_t bytes = s_capacity_frames *
                         STACKCHAN_AUDIO_TRACE_CHANNELS * sizeof(int16_t);
    s_samples = heap_caps_calloc(
        1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_samples == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    if (s_mutex == NULL) {
        heap_caps_free(s_samples);
        s_samples = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void audio_trace_record(const int16_t *raw, const int16_t *reference,
                        const int16_t *clean, size_t sample_count)
{
    if (s_samples == NULL || s_mutex == NULL || raw == NULL ||
        reference == NULL || clean == NULL ||
        xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        return;
    }

    for (size_t index = 0; index < sample_count; ++index) {
        const size_t output =
            s_write_frame * STACKCHAN_AUDIO_TRACE_CHANNELS;
        s_samples[output] = raw[index];
        s_samples[output + 1] = reference[index];
        s_samples[output + 2] = clean[index];
        s_write_frame = (s_write_frame + 1) % s_capacity_frames;
        if (s_frame_count < s_capacity_frames) {
            ++s_frame_count;
        }
    }
    xSemaphoreGive(s_mutex);
}

esp_err_t audio_trace_lock(audio_trace_view_t *view)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(view, 0, sizeof(*view));
    if (s_samples == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    view->interleaved = s_samples;
    view->capacity_frames = s_capacity_frames;
    view->frame_count = s_frame_count;
    view->start_frame =
        (s_write_frame + s_capacity_frames - s_frame_count) %
        s_capacity_frames;
    return ESP_OK;
}

void audio_trace_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}
