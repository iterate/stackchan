#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CAPTURE_CHANNELS 3

static const char *TAG = "diagnostics";

static SemaphoreHandle_t s_lock;
static diagnostics_state_t s_state;
static int16_t *s_signal;
static size_t s_signal_samples;
static int16_t *s_capture;
static size_t s_capture_samples;
static size_t s_target_samples;
static bool s_blocks_realtime;
static bool s_capture_download_active;
static char s_error[96];

static size_t milliseconds_to_samples(size_t milliseconds)
{
    return milliseconds * STACKCHAN_AUDIO_SAMPLE_RATE / 1000;
}

void diagnostics_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
    s_state = DIAGNOSTICS_IDLE;
}

esp_err_t diagnostics_set_signal(const int16_t *signal, size_t samples)
{
    if (signal == NULL || samples == 0 ||
        samples > STACKCHAN_DIAG_MAX_SIGNAL_SECONDS * STACKCHAN_AUDIO_SAMPLE_RATE) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t bytes = samples * sizeof(int16_t);
    int16_t *replacement =
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (replacement == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(replacement, signal, bytes);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == DIAGNOSTICS_RUNNING) {
        xSemaphoreGive(s_lock);
        heap_caps_free(replacement);
        return ESP_ERR_INVALID_STATE;
    }
    heap_caps_free(s_signal);
    s_signal = replacement;
    s_signal_samples = samples;
    s_state = DIAGNOSTICS_IDLE;
    s_error[0] = '\0';
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Loaded %.2f seconds of diagnostic speaker PCM",
             (double)samples / STACKCHAN_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t diagnostics_start(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == DIAGNOSTICS_RUNNING || s_capture_download_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_signal == NULL || s_signal_samples == 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    const size_t target_samples =
        milliseconds_to_samples(STACKCHAN_DIAG_PREROLL_MS) +
        s_signal_samples +
        milliseconds_to_samples(STACKCHAN_DIAG_POSTROLL_MS);
    const size_t capture_bytes =
        target_samples * CAPTURE_CHANNELS * sizeof(int16_t);
    int16_t *replacement =
        heap_caps_malloc(capture_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (replacement == NULL) {
        snprintf(s_error, sizeof(s_error), "Unable to allocate %u capture bytes",
                 (unsigned)capture_bytes);
        s_state = DIAGNOSTICS_ERROR;
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    heap_caps_free(s_capture);
    s_capture = replacement;
    s_capture_samples = 0;
    s_target_samples = target_samples;
    s_error[0] = '\0';
    s_state = DIAGNOSTICS_RUNNING;
    s_blocks_realtime = true;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Synchronized capture started (%u sample frames)",
             (unsigned)target_samples);
    return ESP_OK;
}

bool diagnostics_is_running(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool running = s_state == DIAGNOSTICS_RUNNING;
    xSemaphoreGive(s_lock);
    return running;
}

bool diagnostics_blocks_realtime(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool blocked = s_blocks_realtime;
    xSemaphoreGive(s_lock);
    return blocked;
}

void diagnostics_fill_playback(int16_t *reference, size_t samples)
{
    memset(reference, 0, samples * sizeof(int16_t));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state == DIAGNOSTICS_RUNNING) {
        const size_t preroll =
            milliseconds_to_samples(STACKCHAN_DIAG_PREROLL_MS);
        for (size_t index = 0; index < samples; ++index) {
            const size_t capture_index = s_capture_samples + index;
            if (capture_index >= preroll) {
                const size_t signal_index = capture_index - preroll;
                if (signal_index < s_signal_samples) {
                    reference[index] = s_signal[signal_index];
                }
            }
        }
    }
    xSemaphoreGive(s_lock);
}

void diagnostics_record_frame(const int16_t *raw, const int16_t *reference,
                              const int16_t *clean, size_t samples)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != DIAGNOSTICS_RUNNING) {
        xSemaphoreGive(s_lock);
        return;
    }

    const size_t remaining = s_target_samples - s_capture_samples;
    const size_t to_copy = samples < remaining ? samples : remaining;
    const size_t offset_bytes = s_capture_samples * sizeof(int16_t);
    const size_t copy_bytes = to_copy * sizeof(int16_t);
    uint8_t *capture_raw = (uint8_t *)s_capture;
    uint8_t *capture_reference = (uint8_t *)(
        s_capture + s_target_samples);
    uint8_t *capture_clean = (uint8_t *)(
        s_capture + 2 * s_target_samples);
    memcpy(capture_raw + offset_bytes, raw, copy_bytes);
    memcpy(capture_reference + offset_bytes, reference, copy_bytes);
    memcpy(capture_clean + offset_bytes, clean, copy_bytes);
    s_capture_samples += to_copy;
    if (s_capture_samples == s_target_samples) {
        s_state = DIAGNOSTICS_READY;
        ESP_LOGI(TAG, "Synchronized capture ready (%u sample frames)",
                 (unsigned)s_capture_samples);
    }
    xSemaphoreGive(s_lock);
}

void diagnostics_fail(const char *message)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_error, sizeof(s_error), "%s",
             message ? message : "Audio failure");
    s_state = DIAGNOSTICS_ERROR;
    xSemaphoreGive(s_lock);
}

void diagnostics_snapshot(diagnostics_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snapshot->state = s_state;
    snapshot->signal_samples = s_signal_samples;
    snapshot->captured_samples = s_capture_samples;
    snapshot->target_samples = s_target_samples;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s", s_error);
    xSemaphoreGive(s_lock);
}

const char *diagnostics_state_name(diagnostics_state_t state)
{
    switch (state) {
    case DIAGNOSTICS_IDLE:
        return "idle";
    case DIAGNOSTICS_RUNNING:
        return "running";
    case DIAGNOSTICS_READY:
        return "ready";
    case DIAGNOSTICS_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

esp_err_t diagnostics_capture_lock(diagnostics_capture_view_t *view)
{
    if (view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != DIAGNOSTICS_READY || s_capture == NULL ||
        s_capture_download_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_capture_download_active = true;
    view->raw = s_capture;
    view->reference = s_capture + s_target_samples;
    view->clean = s_capture + 2 * s_target_samples;
    view->sample_frames = s_capture_samples;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void diagnostics_capture_unlock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_capture_download_active = false;
    xSemaphoreGive(s_lock);
}

void diagnostics_release_realtime(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_blocks_realtime = false;
    xSemaphoreGive(s_lock);
}
