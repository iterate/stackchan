#include "realtime_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_status.h"
#include "audio_pipeline.h"
#include "cJSON.h"
#include "diagnostics.h"
#include "esp_asrc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "ui.h"

#define CONTROL_CONFIGURE (1U << 0)
#define CONTROL_CANCEL (1U << 1)
#define CONTROL_OUTPUT_PROBE (1U << 2)

#define INPUT_CHUNK_SAMPLES \
    (STACKCHAN_AUDIO_SAMPLE_RATE * STACKCHAN_REALTIME_INPUT_CHUNK_MS / 1000)
#define PLAYOUT_INPUT_SAMPLES \
    (STACKCHAN_REALTIME_SAMPLE_RATE * \
     STACKCHAN_REALTIME_PLAYOUT_CHUNK_MS / 1000)
#define OUTPUT_STREAM_BYTES \
    (STACKCHAN_REALTIME_SAMPLE_RATE * sizeof(int16_t) * \
     STACKCHAN_REALTIME_OUTPUT_BUFFER_SECONDS)
#define OUTPUT_STREAM_STORAGE_BYTES (OUTPUT_STREAM_BYTES + 1)

static const char *TAG = "realtime";

#if STACKCHAN_REALTIME_BINARY_TRANSPORT
static const char SESSION_UPDATE[] =
    "{"
    "\"type\":\"session.update\","
    "\"session\":{"
    "\"instructions\":\"You are StackChan, a concise friendly desktop voice "
    "assistant. Reply naturally in one short sentence unless the user asks "
    "for detail.\","
    "\"voice\":\"" STACKCHAN_REALTIME_VOICE "\","
    "\"reasoning\":{\"effort\":\"none\"},"
    "\"turn_detection\":{\"type\":\"server_vad\"},"
    "\"audio\":{"
    "\"input\":{"
    "\"format\":{\"type\":\"audio/pcm\",\"rate\":16000},"
    "\"transport\":\"binary\","
    "\"transcription\":{"
    "\"model\":\"grok-transcribe\","
    "\"language_hint\":\"en\""
    "}"
    "},"
    "\"output\":{"
    "\"format\":{\"type\":\"audio/pcm\",\"rate\":16000},"
    "\"transport\":\"binary\""
    "}"
    "}"
    "}"
    "}";
#else
static const char SESSION_UPDATE[] =
    "{"
    "\"type\":\"session.update\","
    "\"session\":{"
    "\"type\":\"realtime\","
    "\"model\":\"" STACKCHAN_REALTIME_MODEL "\","
    "\"output_modalities\":[\"audio\"],"
    "\"instructions\":\"You are StackChan, a concise friendly desktop voice "
    "assistant. Reply naturally in one short sentence unless the user asks "
    "for detail.\","
    "\"audio\":{"
    "\"input\":{"
    "\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},"
    "\"turn_detection\":{\"type\":\"server_vad\"},"
    "\"noise_reduction\":null,"
    "\"transcription\":{"
    "\"model\":\"gpt-4o-mini-transcribe\","
    "\"language\":\"en\""
    "}"
    "},"
    "\"output\":{"
    "\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},"
    "\"voice\":\"" STACKCHAN_REALTIME_VOICE "\""
    "}"
    "},"
    "\"max_output_tokens\":256"
    "}"
    "}";
#endif

static const char RESPONSE_CREATE[] = "{\"type\":\"response.create\"}";
static const char RESPONSE_CANCEL[] = "{\"type\":\"response.cancel\"}";
static const char OUTPUT_PROBE_ITEM[] =
    "{"
    "\"type\":\"conversation.item.create\","
    "\"item\":{"
    "\"type\":\"message\","
    "\"role\":\"user\","
    "\"content\":[{"
    "\"type\":\"input_text\","
    "\"text\":\"Say exactly: StackChan direct Realtime audio is working.\""
    "}]"
    "}"
    "}";

static esp_websocket_client_handle_t s_client;
static esp_asrc_handle_t s_input_asrc;
static esp_asrc_handle_t s_output_asrc;
static TaskHandle_t s_control_task;
static SemaphoreHandle_t s_state_lock;

static StreamBufferHandle_t s_output_stream;
static StaticStreamBuffer_t s_output_stream_control;
static uint8_t *s_output_stream_storage;

static int16_t *s_input_device;
static int16_t *s_input_provider;
static int16_t *s_playout_provider;
static int16_t *s_playout_device;
static uint32_t s_input_provider_capacity;
static uint32_t s_playout_device_capacity;

static char s_auth_headers[512];
static uint8_t *s_message_buffer;
static size_t s_message_capacity;
static size_t s_message_expected;
static uint8_t s_message_opcode;

static bool s_initialized;
static bool s_connected;
static bool s_session_ready;
static bool s_streaming;
static bool s_vad_speech_active;
static bool s_response_active;
static bool s_generation_active;
static bool s_accept_output;
static bool s_response_done;
static bool s_output_flush_requested;
static bool s_first_playback_pending;
static int64_t s_speech_end_us;

static uint32_t s_turns;
static uint32_t s_captured_input_samples;
static uint32_t s_uploaded_input_samples;
static uint32_t s_received_output_samples;
static uint32_t s_played_output_samples;
static uint32_t s_dropped_output_samples;
static uint32_t s_binary_audio_frames_received;
static uint32_t s_events_received;
static uint32_t s_websocket_errors;
static uint32_t s_resampler_errors;
static int32_t s_speech_end_to_first_playback_ms = -1;

static char s_input_transcript[256];
static char s_output_transcript[256];
static char s_last_event[64];
static char s_last_error[192];

static bool atomic_bool(const bool *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void atomic_set_bool(bool *value, bool next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static void copy_state_string(char *destination, size_t capacity,
                              const char *source)
{
    if (destination == NULL || capacity == 0) {
        return;
    }
    if (s_state_lock != NULL) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
    }
    snprintf(destination, capacity, "%s", source ? source : "");
    if (s_state_lock != NULL) {
        xSemaphoreGive(s_state_lock);
    }
}

static void append_state_string(char *destination, size_t capacity,
                                const char *suffix)
{
    if (destination == NULL || capacity == 0 || suffix == NULL) {
        return;
    }
    if (s_state_lock != NULL) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
    }
    const size_t used = strnlen(destination, capacity);
    if (used < capacity - 1) {
        snprintf(destination + used, capacity - used, "%s", suffix);
    }
    if (s_state_lock != NULL) {
        xSemaphoreGive(s_state_lock);
    }
}

static void note_error(const char *message)
{
    copy_state_string(s_last_error, sizeof(s_last_error), message);
    ESP_LOGE(TAG, "%s", message ? message : "Unknown Realtime error");
}

static void log_heap(const char *stage)
{
    ESP_LOGI(
        TAG,
        "%s: internal free=%u largest=%u, PSRAM free=%u largest=%u",
        stage,
        (unsigned)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void show_listening(void)
{
    if (diagnostics_blocks_realtime()) {
        return;
    }
    app_status_set_phase(
        APP_PHASE_LISTENING,
        "Continuously streaming AEC-clean microphone audio");
    ui_set_status("Listening", "Hands-free - just speak naturally");
    ui_set_button_label("Always listening");
}

static int send_text(const char *text)
{
    if (text == NULL || s_client == NULL || !atomic_bool(&s_connected)) {
        return -1;
    }
    const int length = (int)strlen(text);
    const int sent = esp_websocket_client_send_text(
        s_client, text, length, pdMS_TO_TICKS(5000));
    if (sent != length) {
        note_error("Realtime WebSocket send failed");
        return -1;
    }
    return sent;
}

static esp_err_t open_asrc(uint32_t source_rate, uint32_t destination_rate,
                           esp_asrc_handle_t *handle)
{
    esp_asrc_cfg_t config = {
        .src_info = {
            .sample_rate = source_rate,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = destination_rate,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 3,
        .timeout_ms = 1000,
    };
    const esp_asrc_err_t result = esp_asrc_open(&config, handle);
    if (result != ESP_ASRC_ERR_OK || *handle == NULL) {
        ESP_LOGE(
            TAG, "ASRC %u -> %u initialization failed: %d",
            (unsigned)source_rate, (unsigned)destination_rate, result);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int16_t *psram_audio_buffer(size_t samples)
{
    return heap_caps_aligned_alloc(
        16, samples * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static esp_err_t query_asrc_capacity(esp_asrc_handle_t handle,
                                     uint32_t input_samples,
                                     const char *direction,
                                     uint32_t *capacity)
{
    uint32_t required = 0;
    const esp_asrc_err_t result =
        esp_asrc_get_out_sample_num(handle, input_samples, &required);
    if (result != ESP_ASRC_ERR_OK || required == 0) {
        ESP_LOGE(
            TAG, "%s ASRC capacity query failed: code=%d input=%u output=%u",
            direction, result, (unsigned)input_samples, (unsigned)required);
        return ESP_FAIL;
    }
    *capacity = required;
    ESP_LOGI(
        TAG, "%s ASRC capacity: %u input samples -> %u output samples",
        direction, (unsigned)input_samples, (unsigned)required);
    return ESP_OK;
}

static bool prepare_asrc_output(esp_asrc_handle_t handle,
                                uint32_t input_samples,
                                uint32_t allocated_capacity,
                                const char *direction,
                                uint32_t *output_capacity)
{
    uint32_t required = 0;
    const esp_asrc_err_t result =
        esp_asrc_get_out_sample_num(handle, input_samples, &required);
    if (result == ESP_ASRC_ERR_OK && required > 0 &&
        required <= allocated_capacity) {
        *output_capacity = allocated_capacity;
        return true;
    }

    char error[192];
    snprintf(
        error, sizeof(error),
        "%s ASRC sizing failed: code=%d input=%u required=%u allocated=%u",
        direction, result, (unsigned)input_samples, (unsigned)required,
        (unsigned)allocated_capacity);
    __atomic_fetch_add(&s_resampler_errors, 1, __ATOMIC_RELAXED);
    note_error(error);
    return false;
}

static void request_output_flush(void)
{
    atomic_set_bool(&s_output_flush_requested, true);
    audio_pipeline_flush_playback();
}

static bool send_input_audio(const int16_t *samples, size_t sample_count)
{
    if (sample_count == 0 || samples == NULL) {
        return true;
    }

    const int16_t *transport_samples = samples;
    uint32_t transport_sample_count = (uint32_t)sample_count;
    if (STACKCHAN_REALTIME_NEEDS_RESAMPLING) {
        if (!prepare_asrc_output(
                s_input_asrc, (uint32_t)sample_count,
                s_input_provider_capacity, "Microphone",
                &transport_sample_count)) {
            return false;
        }
        const esp_asrc_err_t converted = esp_asrc_process(
            s_input_asrc, (uint8_t *)samples, (uint32_t)sample_count,
            (uint8_t *)s_input_provider, &transport_sample_count);
        if (converted != ESP_ASRC_ERR_OK ||
            transport_sample_count == 0) {
            char error[192];
            snprintf(
                error, sizeof(error),
                "Microphone ASRC failed: code=%d input=%u capacity=%u "
                "output=%u",
                converted, (unsigned)sample_count,
                (unsigned)s_input_provider_capacity,
                (unsigned)transport_sample_count);
            __atomic_fetch_add(
                &s_resampler_errors, 1, __ATOMIC_RELAXED);
            note_error(error);
            return false;
        }
        transport_samples = s_input_provider;
    }

    const size_t pcm_bytes =
        transport_sample_count * sizeof(int16_t);
    if (STACKCHAN_REALTIME_BINARY_TRANSPORT) {
        const int sent = esp_websocket_client_send_bin(
            s_client, (const char *)transport_samples, (int)pcm_bytes,
            pdMS_TO_TICKS(5000));
        if (sent != (int)pcm_bytes) {
            note_error("Microphone binary audio upload failed");
            return false;
        }
        __atomic_fetch_add(
            &s_captured_input_samples, (uint32_t)sample_count,
            __ATOMIC_RELAXED);
        __atomic_fetch_add(
            &s_uploaded_input_samples, transport_sample_count,
            __ATOMIC_RELAXED);
        return true;
    }

    const size_t base64_bytes = 4 * ((pcm_bytes + 2) / 3);
    static const char prefix[] =
        "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
    static const char suffix[] = "\"}";
    const size_t message_capacity =
        sizeof(prefix) - 1 + base64_bytes + sizeof(suffix);
    char *message = heap_caps_malloc(
        message_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (message == NULL) {
        note_error("Not enough PSRAM to encode microphone audio");
        return false;
    }

    memcpy(message, prefix, sizeof(prefix) - 1);
    size_t encoded_bytes = 0;
    const int encoded = mbedtls_base64_encode(
        (unsigned char *)message + sizeof(prefix) - 1,
        base64_bytes + 1, &encoded_bytes,
        (const unsigned char *)transport_samples, pcm_bytes);
    if (encoded != 0) {
        heap_caps_free(message);
        note_error("Microphone base64 encoding failed");
        return false;
    }
    memcpy(
        message + sizeof(prefix) - 1 + encoded_bytes,
        suffix, sizeof(suffix));

    const size_t message_bytes =
        sizeof(prefix) - 1 + encoded_bytes + sizeof(suffix) - 1;
    const int sent = esp_websocket_client_send_text(
        s_client, message, (int)message_bytes, pdMS_TO_TICKS(5000));
    heap_caps_free(message);
    if (sent != (int)message_bytes) {
        note_error("Microphone audio upload failed");
        return false;
    }

    __atomic_fetch_add(
        &s_captured_input_samples, (uint32_t)sample_count,
        __ATOMIC_RELAXED);
    __atomic_fetch_add(
        &s_uploaded_input_samples, transport_sample_count,
        __ATOMIC_RELAXED);
    return true;
}

static size_t read_clean_chunk(TickType_t first_wait, TickType_t fill_wait)
{
    size_t received = audio_pipeline_read_clean(
        s_input_device, INPUT_CHUNK_SAMPLES, first_wait);
    while (received < INPUT_CHUNK_SAMPLES) {
        const size_t extra = audio_pipeline_read_clean(
            s_input_device + received, INPUT_CHUNK_SAMPLES - received,
            fill_wait);
        if (extra == 0) {
            break;
        }
        received += extra;
    }
    return received;
}

static void cancel_active_response(void)
{
    if (atomic_bool(&s_generation_active)) {
        (void)send_text(RESPONSE_CANCEL);
    }
    atomic_set_bool(&s_generation_active, false);
    atomic_set_bool(&s_accept_output, false);
    atomic_set_bool(&s_response_active, false);
    atomic_set_bool(&s_response_done, false);
    request_output_flush();
}

static void handle_cancel(void)
{
    cancel_active_response();
    if (diagnostics_blocks_realtime()) {
        return;
    }
    if (atomic_bool(&s_session_ready)) {
        show_listening();
    } else {
        ui_set_status(
            "Connecting",
            "Reconnecting to " STACKCHAN_REALTIME_PROVIDER_DISPLAY "...");
        ui_set_button_label("Please wait");
    }
    ESP_LOGI(
        TAG, "Response cancelled; continuous microphone stream remains on");
}

static void handle_output_probe(void)
{
    if (diagnostics_blocks_realtime() || !atomic_bool(&s_connected) ||
        !atomic_bool(&s_session_ready)) {
        return;
    }

    cancel_active_response();
    copy_state_string(
        s_input_transcript, sizeof(s_input_transcript), "[output probe]");
    copy_state_string(
        s_output_transcript, sizeof(s_output_transcript), "");
    s_speech_end_us = esp_timer_get_time();
    __atomic_store_n(
        &s_speech_end_to_first_playback_ms, -1, __ATOMIC_RELAXED);
    atomic_set_bool(&s_first_playback_pending, true);
    atomic_set_bool(&s_response_active, true);
    atomic_set_bool(&s_generation_active, true);
    atomic_set_bool(&s_accept_output, false);
    atomic_set_bool(&s_response_done, false);
    __atomic_fetch_add(&s_turns, 1, __ATOMIC_RELAXED);

    if (send_text(OUTPUT_PROBE_ITEM) < 0 ||
        send_text(RESPONSE_CREATE) < 0) {
        atomic_set_bool(&s_generation_active, false);
        atomic_set_bool(&s_response_active, false);
        show_listening();
        return;
    }
    app_status_set_phase(
        APP_PHASE_CONNECTING, "Running deterministic output probe");
    ui_set_status("Output probe", "Waiting for fixed Realtime phrase");
    ui_set_button_label("Stop response");
    ESP_LOGI(TAG, "Deterministic Realtime output probe requested");
}

static void control_task(void *argument)
{
    (void)argument;
    while (true) {
        uint32_t commands = 0;
        (void)xTaskNotifyWait(
            0, UINT32_MAX, &commands, pdMS_TO_TICKS(20));

        if ((commands & CONTROL_CANCEL) != 0) {
            handle_cancel();
        }
        if ((commands & CONTROL_CONFIGURE) != 0 &&
            atomic_bool(&s_connected)) {
            atomic_set_bool(&s_session_ready, false);
            app_status_set_realtime(true, false);
            app_status_set_phase(
                APP_PHASE_CONNECTING,
                "Configuring " STACKCHAN_REALTIME_PROVIDER_DISPLAY
                " Realtime");
            ui_set_status("Connecting", "Configuring Realtime audio...");
            ui_set_button_label("Please wait");
            if (send_text(SESSION_UPDATE) < 0) {
                note_error(
                    "Unable to configure "
                    STACKCHAN_REALTIME_PROVIDER_DISPLAY
                    " Realtime session");
            }
        }
        if ((commands & CONTROL_OUTPUT_PROBE) != 0) {
            handle_output_probe();
        }

        if (atomic_bool(&s_streaming) &&
            atomic_bool(&s_session_ready) &&
            !diagnostics_blocks_realtime()) {
            const size_t samples =
                read_clean_chunk(
                    pdMS_TO_TICKS(20), pdMS_TO_TICKS(20));
            if (samples > 0 &&
                !send_input_audio(s_input_device, samples)) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            (void)read_clean_chunk(0, 0);
        }
    }
}

static void finish_playout_if_drained(void)
{
    if (!atomic_bool(&s_response_done) ||
        xStreamBufferBytesAvailable(s_output_stream) > 0 ||
        audio_pipeline_playback_pending_samples() > 0) {
        return;
    }
    atomic_set_bool(&s_response_done, false);
    atomic_set_bool(&s_response_active, false);
    if (!atomic_bool(&s_vad_speech_active) &&
        atomic_bool(&s_session_ready)) {
        show_listening();
    }
}

static void playout_task(void *argument)
{
    (void)argument;
    while (true) {
        if (atomic_bool(&s_output_flush_requested)) {
            (void)xStreamBufferReset(s_output_stream);
            atomic_set_bool(&s_output_flush_requested, false);
        }

        const size_t bytes = xStreamBufferReceive(
            s_output_stream, s_playout_provider,
            PLAYOUT_INPUT_SAMPLES * sizeof(int16_t),
            pdMS_TO_TICKS(20));
        if (bytes == 0) {
            finish_playout_if_drained();
            continue;
        }
        if (atomic_bool(&s_output_flush_requested)) {
            continue;
        }
        if (bytes % sizeof(int16_t) != 0) {
            note_error("Response PCM stream was not sample-aligned");
            request_output_flush();
            continue;
        }

        const uint32_t input_samples = bytes / sizeof(int16_t);
        const int16_t *output = s_playout_provider;
        uint32_t output_samples = input_samples;
        if (STACKCHAN_REALTIME_NEEDS_RESAMPLING) {
            if (!prepare_asrc_output(
                    s_output_asrc, input_samples,
                    s_playout_device_capacity, "Response",
                    &output_samples)) {
                continue;
            }
            const esp_asrc_err_t converted = esp_asrc_process(
                s_output_asrc, (uint8_t *)s_playout_provider,
                input_samples, (uint8_t *)s_playout_device,
                &output_samples);
            if (converted != ESP_ASRC_ERR_OK || output_samples == 0) {
                char error[192];
                snprintf(
                    error, sizeof(error),
                    "Response ASRC failed: code=%d input=%u capacity=%u "
                    "output=%u",
                    converted, (unsigned)input_samples,
                    (unsigned)s_playout_device_capacity,
                    (unsigned)output_samples);
                __atomic_fetch_add(
                    &s_resampler_errors, 1, __ATOMIC_RELAXED);
                note_error(error);
                continue;
            }
            output = s_playout_device;
        }

        size_t written = 0;
        while (written < output_samples &&
               !atomic_bool(&s_output_flush_requested)) {
            const size_t count = audio_pipeline_write_playback(
                output + written, output_samples - written,
                pdMS_TO_TICKS(100));
            if (count == 0) {
                break;
            }
            if (atomic_bool(&s_first_playback_pending)) {
                const int64_t latency_us =
                    esp_timer_get_time() - s_speech_end_us;
                __atomic_store_n(
                    &s_speech_end_to_first_playback_ms,
                    (int32_t)(latency_us / 1000), __ATOMIC_RELAXED);
                atomic_set_bool(&s_first_playback_pending, false);
                ESP_LOGI(
                    TAG, "Speech end to first playout: %lld ms",
                    (long long)(latency_us / 1000));
            }
            written += count;
            __atomic_fetch_add(
                &s_played_output_samples, (uint32_t)count,
                __ATOMIC_RELAXED);
        }
        if (written > 0) {
            app_status_set_phase(
                APP_PHASE_SPEAKING,
                "Playing response while microphone remains live");
            ui_set_status("Speaking", "Interrupt anytime by speaking");
            ui_set_button_label("Stop response");
        }
    }
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static void accept_output_pcm(const uint8_t *pcm, size_t pcm_bytes)
{
    if (pcm == NULL || pcm_bytes == 0 || s_output_stream == NULL ||
        !atomic_bool(&s_accept_output) ||
        diagnostics_blocks_realtime()) {
        return;
    }
    if (pcm_bytes % sizeof(int16_t) != 0) {
        note_error("Invalid response PCM payload");
        return;
    }

    const size_t available = xStreamBufferSpacesAvailable(s_output_stream);
    if (available < pcm_bytes) {
        __atomic_fetch_add(
            &s_dropped_output_samples,
            (uint32_t)(pcm_bytes / sizeof(int16_t)), __ATOMIC_RELAXED);
        note_error("Response audio buffer overflow");
        return;
    }
    const size_t queued =
        xStreamBufferSend(s_output_stream, pcm, pcm_bytes, 0);
    if (queued != pcm_bytes) {
        __atomic_fetch_add(
            &s_dropped_output_samples,
            (uint32_t)((pcm_bytes - queued) / sizeof(int16_t)),
            __ATOMIC_RELAXED);
        note_error("Response audio was only partially queued");
        return;
    }
    __atomic_fetch_add(
        &s_received_output_samples,
        (uint32_t)(pcm_bytes / sizeof(int16_t)), __ATOMIC_RELAXED);
}

static void accept_output_audio(const char *base64)
{
    if (base64 == NULL || !atomic_bool(&s_accept_output)) {
        return;
    }
    const size_t encoded_bytes = strlen(base64);
    const size_t decoded_capacity = encoded_bytes / 4 * 3 + 3;
    uint8_t *decoded = heap_caps_malloc(
        decoded_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (decoded == NULL) {
        note_error("Not enough PSRAM to decode response audio");
        return;
    }

    size_t decoded_bytes = 0;
    const int result = mbedtls_base64_decode(
        decoded, decoded_capacity, &decoded_bytes,
        (const unsigned char *)base64, encoded_bytes);
    if (result != 0 || decoded_bytes % sizeof(int16_t) != 0) {
        heap_caps_free(decoded);
        note_error("Invalid response PCM payload");
        return;
    }
    accept_output_pcm(decoded, decoded_bytes);
    heap_caps_free(decoded);
}

static void process_server_event(const char *payload, size_t length)
{
    __atomic_fetch_add(&s_events_received, 1, __ATOMIC_RELAXED);
    cJSON *root = cJSON_ParseWithLength(payload, length);
    if (root == NULL) {
        note_error("Could not parse a Realtime server event");
        return;
    }

    const char *type = json_string(root, "type");
    if (type == NULL) {
        cJSON_Delete(root);
        return;
    }
    copy_state_string(s_last_event, sizeof(s_last_event), type);

    if (strcmp(type, "session.updated") == 0) {
        atomic_set_bool(&s_session_ready, true);
        atomic_set_bool(&s_streaming, true);
        app_status_set_realtime(true, true);
        audio_pipeline_flush_clean();
        copy_state_string(s_last_error, sizeof(s_last_error), "");
        if (!atomic_bool(&s_response_active)) {
            show_listening();
        }
        ESP_LOGI(
            TAG,
            "%s Realtime session ready: model=%s voice=%s rate=%u "
            "transport=%s",
            STACKCHAN_REALTIME_PROVIDER_DISPLAY,
            STACKCHAN_REALTIME_MODEL, STACKCHAN_REALTIME_VOICE,
            (unsigned)STACKCHAN_REALTIME_SAMPLE_RATE,
            STACKCHAN_REALTIME_BINARY_TRANSPORT ? "binary" :
                                                  "json_base64");
    } else if (
        strcmp(type, "input_audio_buffer.speech_started") == 0) {
        atomic_set_bool(&s_vad_speech_active, true);
        copy_state_string(
            s_input_transcript, sizeof(s_input_transcript), "");
        copy_state_string(
            s_output_transcript, sizeof(s_output_transcript), "");
        if (atomic_bool(&s_response_active) ||
            atomic_bool(&s_generation_active)) {
            atomic_set_bool(&s_accept_output, false);
            request_output_flush();
            ESP_LOGI(
                TAG, "Server VAD detected barge-in; local playout stopped");
        } else {
            ESP_LOGI(TAG, "Server VAD detected speech start");
        }
        if (!diagnostics_blocks_realtime()) {
            app_status_set_phase(
                APP_PHASE_LISTENING, "Server VAD detected live speech");
            ui_set_status("Listening", "I hear you - keep speaking");
            ui_set_button_label("Always listening");
        }
    } else if (
        strcmp(type, "input_audio_buffer.speech_stopped") == 0) {
        atomic_set_bool(&s_vad_speech_active, false);
        s_speech_end_us = esp_timer_get_time();
        __atomic_store_n(
            &s_speech_end_to_first_playback_ms, -1,
            __ATOMIC_RELAXED);
        atomic_set_bool(&s_first_playback_pending, true);
        __atomic_fetch_add(&s_turns, 1, __ATOMIC_RELAXED);
        if (!diagnostics_blocks_realtime()) {
            app_status_set_phase(
                APP_PHASE_CONNECTING,
                "Server VAD ended the turn; waiting for response");
            ui_set_status("Thinking", "Speech ended automatically");
            ui_set_button_label("Stop response");
        }
        ESP_LOGI(TAG, "Server VAD detected speech end");
    } else if (strcmp(type, "response.created") == 0) {
        atomic_set_bool(&s_response_active, true);
        atomic_set_bool(&s_generation_active, true);
        atomic_set_bool(&s_accept_output, true);
        atomic_set_bool(&s_response_done, false);
    } else if (strcmp(type, "response.output_audio.delta") == 0) {
        accept_output_audio(json_string(root, "delta"));
    } else if (
        strcmp(type, "response.output_audio_transcript.delta") == 0) {
        append_state_string(
            s_output_transcript, sizeof(s_output_transcript),
            json_string(root, "delta"));
    } else if (
        strcmp(
            type,
            "conversation.item.input_audio_transcription.updated") == 0) {
        copy_state_string(
            s_input_transcript, sizeof(s_input_transcript),
            json_string(root, "transcript"));
    } else if (
        strcmp(
            type,
            "conversation.item.input_audio_transcription.completed") ==
        0) {
        copy_state_string(
            s_input_transcript, sizeof(s_input_transcript),
            json_string(root, "transcript"));
        ESP_LOGI(TAG, "Input transcript received");
    } else if (strcmp(type, "response.done") == 0) {
        atomic_set_bool(&s_generation_active, false);
        atomic_set_bool(&s_accept_output, false);
        atomic_set_bool(&s_response_done, true);
    } else if (strcmp(type, "error") == 0) {
        const cJSON *error =
            cJSON_GetObjectItemCaseSensitive(root, "error");
        const char *message =
            cJSON_IsObject(error) ? json_string(error, "message") : NULL;
        note_error(
            message ? message :
                      STACKCHAN_REALTIME_PROVIDER_DISPLAY
                      " Realtime returned an error");
    }
    cJSON_Delete(root);
}

static void accept_websocket_data(const esp_websocket_event_data_t *data)
{
    if (data == NULL || data->data_ptr == NULL || data->data_len <= 0) {
        return;
    }
    if (data->op_code != 0x1 && data->op_code != 0x2 &&
        data->op_code != 0x0) {
        return;
    }

    const size_t total = data->payload_len > 0
                             ? (size_t)data->payload_len
                             : (size_t)data->data_len;
    const size_t offset =
        data->payload_offset >= 0 ? (size_t)data->payload_offset : 0;
    const size_t chunk = (size_t)data->data_len;
    if (total > STACKCHAN_REALTIME_MAX_EVENT_BYTES ||
        offset > total || chunk > total - offset) {
        note_error("Realtime server event exceeded the safety limit");
        s_message_expected = 0;
        return;
    }

    if (offset == 0) {
        s_message_expected = total;
        if (data->op_code != 0x0) {
            s_message_opcode = data->op_code;
        }
        if (s_message_capacity < total + 1) {
            heap_caps_free(s_message_buffer);
            s_message_buffer = heap_caps_malloc(
                total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_message_buffer == NULL) {
                s_message_capacity = 0;
                s_message_expected = 0;
                note_error("Not enough PSRAM for a Realtime server event");
                return;
            }
            s_message_capacity = total + 1;
        }
    }
    if (s_message_expected != total || s_message_buffer == NULL) {
        note_error("Unexpected fragmented Realtime server event");
        s_message_expected = 0;
        return;
    }

    memcpy(s_message_buffer + offset, data->data_ptr, chunk);
    if (offset + chunk == total && data->fin) {
        if (s_message_opcode == 0x2) {
            __atomic_fetch_add(
                &s_binary_audio_frames_received, 1, __ATOMIC_RELAXED);
            accept_output_pcm(s_message_buffer, total);
        } else {
            s_message_buffer[total] = '\0';
            process_server_event((const char *)s_message_buffer, total);
        }
        s_message_expected = 0;
        s_message_opcode = 0;
    }
}

static void websocket_event(
    void *handler_argument, esp_event_base_t event_base, int32_t event_id,
    void *event_data)
{
    (void)handler_argument;
    (void)event_base;
    esp_websocket_event_data_t *data = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        atomic_set_bool(&s_connected, true);
        atomic_set_bool(&s_session_ready, false);
        app_status_set_realtime(true, false);
        copy_state_string(s_last_event, sizeof(s_last_event), "connected");
        xTaskNotify(s_control_task, CONTROL_CONFIGURE, eSetBits);
        ESP_LOGI(TAG, "Secure Realtime WebSocket connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        atomic_set_bool(&s_connected, false);
        atomic_set_bool(&s_session_ready, false);
        atomic_set_bool(&s_streaming, false);
        atomic_set_bool(&s_vad_speech_active, false);
        atomic_set_bool(&s_response_active, false);
        atomic_set_bool(&s_generation_active, false);
        atomic_set_bool(&s_accept_output, false);
        app_status_set_realtime(false, false);
        request_output_flush();
        app_status_set_phase(
            APP_PHASE_CONNECTING,
            STACKCHAN_REALTIME_PROVIDER_DISPLAY
            " disconnected; reconnecting");
        if (!diagnostics_blocks_realtime()) {
            ui_set_status(
                "Reconnecting",
                STACKCHAN_REALTIME_PROVIDER_DISPLAY
                " connection was interrupted");
            ui_set_button_label("Please wait");
        }
        ESP_LOGW(TAG, "Realtime WebSocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        accept_websocket_data(data);
        break;
    case WEBSOCKET_EVENT_ERROR: {
        __atomic_fetch_add(&s_websocket_errors, 1, __ATOMIC_RELAXED);
        char error[192];
        snprintf(
            error, sizeof(error),
            "WebSocket error: type=%d tls=%s handshake=%d socket=%d",
            data ? data->error_handle.error_type : -1,
            data ? esp_err_to_name(
                       data->error_handle.esp_tls_last_esp_err)
                 : "unknown",
            data ? data->error_handle.esp_ws_handshake_status_code : 0,
            data ? data->error_handle.esp_transport_sock_errno : 0);
        note_error(error);
        break;
    }
    default:
        break;
    }
}

esp_err_t realtime_client_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (STACKCHAN_REALTIME_API_KEY[0] == '\0') {
        ESP_LOGE(
            TAG, "%s Realtime API key is not configured",
            STACKCHAN_REALTIME_PROVIDER_DISPLAY);
        return ESP_ERR_INVALID_STATE;
    }

    log_heap("Before Realtime initialization");
    s_state_lock = xSemaphoreCreateMutex();
    if (s_state_lock == NULL) {
        ESP_LOGE(TAG, "Unable to allocate Realtime state mutex");
        return ESP_ERR_NO_MEM;
    }
    if (STACKCHAN_REALTIME_NEEDS_RESAMPLING) {
        ESP_RETURN_ON_ERROR(
            open_asrc(
                STACKCHAN_AUDIO_SAMPLE_RATE,
                STACKCHAN_REALTIME_SAMPLE_RATE, &s_input_asrc),
            TAG, "Initialize microphone ASRC");
        log_heap("After microphone ASRC");
        ESP_RETURN_ON_ERROR(
            open_asrc(
                STACKCHAN_REALTIME_SAMPLE_RATE,
                STACKCHAN_AUDIO_SAMPLE_RATE, &s_output_asrc),
            TAG, "Initialize response ASRC");
        log_heap("After response ASRC");

        ESP_RETURN_ON_ERROR(
            query_asrc_capacity(
                s_input_asrc, INPUT_CHUNK_SAMPLES, "Microphone",
                &s_input_provider_capacity),
            TAG, "Size microphone ASRC output");
        ESP_RETURN_ON_ERROR(
            query_asrc_capacity(
                s_output_asrc, PLAYOUT_INPUT_SAMPLES, "Response",
                &s_playout_device_capacity),
            TAG, "Size response ASRC output");
    } else {
        ESP_LOGI(
            TAG,
            "Native %u Hz provider path: both ASRC stages are disabled",
            (unsigned)STACKCHAN_AUDIO_SAMPLE_RATE);
    }

    s_input_device = psram_audio_buffer(INPUT_CHUNK_SAMPLES);
    s_playout_provider = psram_audio_buffer(PLAYOUT_INPUT_SAMPLES);
    if (STACKCHAN_REALTIME_NEEDS_RESAMPLING) {
        s_input_provider =
            psram_audio_buffer(s_input_provider_capacity);
        s_playout_device =
            psram_audio_buffer(s_playout_device_capacity);
    }
    s_output_stream_storage = heap_caps_malloc(
        OUTPUT_STREAM_STORAGE_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_input_device == NULL || s_playout_provider == NULL ||
        s_output_stream_storage == NULL ||
        (STACKCHAN_REALTIME_NEEDS_RESAMPLING &&
         (s_input_provider == NULL || s_playout_device == NULL))) {
        ESP_LOGE(TAG, "Unable to allocate Realtime audio buffers");
        return ESP_ERR_NO_MEM;
    }
    s_output_stream = xStreamBufferCreateStatic(
        OUTPUT_STREAM_BYTES, 1, s_output_stream_storage,
        &s_output_stream_control);
    if (s_output_stream == NULL) {
        ESP_LOGE(TAG, "Unable to create Realtime output stream");
        return ESP_ERR_NO_MEM;
    }
    log_heap("After Realtime PSRAM audio buffers");

    BaseType_t created = xTaskCreateWithCaps(
        control_task, "realtime_control",
        STACKCHAN_REALTIME_CONTROL_TASK_STACK, NULL, 6,
        &s_control_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create PSRAM-backed Realtime control task");
        return ESP_ERR_NO_MEM;
    }
    created = xTaskCreateWithCaps(
        playout_task, "realtime_playout",
        STACKCHAN_REALTIME_PLAYOUT_TASK_STACK, NULL, 7, NULL,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create PSRAM-backed Realtime playout task");
        return ESP_ERR_NO_MEM;
    }
    log_heap("After Realtime task creation");

    const int header_bytes = STACKCHAN_REALTIME_BINARY_TRANSPORT
                                 ? snprintf(
                                       s_auth_headers,
                                       sizeof(s_auth_headers),
                                       "Authorization: Bearer %s\r\n",
                                       STACKCHAN_REALTIME_API_KEY)
                                 : snprintf(
                                       s_auth_headers,
                                       sizeof(s_auth_headers),
                                       "Authorization: Bearer %s\r\n"
                                       "OpenAI-Safety-Identifier: "
                                       "stackchan-device-lab\r\n",
                                       STACKCHAN_REALTIME_API_KEY);
    if (header_bytes <= 0 ||
        (size_t)header_bytes >= sizeof(s_auth_headers)) {
        ESP_LOGE(
            TAG, "%s authorization header is too long",
            STACKCHAN_REALTIME_PROVIDER_DISPLAY);
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_websocket_client_config_t config = {
        .uri = STACKCHAN_REALTIME_URI,
        .disable_auto_reconnect = false,
        .task_prio = 5,
        .task_name = STACKCHAN_REALTIME_WS_TASK_NAME,
        .task_stack = STACKCHAN_REALTIME_WS_TASK_STACK,
        .buffer_size = STACKCHAN_REALTIME_WS_BUFFER_BYTES,
        .headers = s_auth_headers,
        .pingpong_timeout_sec = 15,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .keep_alive_idle = 10,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 10,
    };
    s_client = esp_websocket_client_init(&config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Unable to allocate Realtime WebSocket client");
        return ESP_ERR_NO_MEM;
    }
    log_heap("After WebSocket client allocation");
    ESP_RETURN_ON_ERROR(
        esp_websocket_register_events(
            s_client, WEBSOCKET_EVENT_ANY, websocket_event, NULL),
        TAG, "Register Realtime WebSocket events");

    atomic_set_bool(&s_initialized, true);
    app_status_set_phase(
        APP_PHASE_CONNECTING,
        "Opening secure " STACKCHAN_REALTIME_PROVIDER_DISPLAY " WebSocket");
    ui_set_status(
        "Connecting",
        "Opening secure " STACKCHAN_REALTIME_PROVIDER_DISPLAY
        " WebSocket...");
    ui_set_button_label("Please wait");
    ESP_RETURN_ON_ERROR(
        esp_websocket_client_start(s_client), TAG,
        "Start Realtime WebSocket");
    ESP_LOGI(
        TAG,
        "Realtime client started: provider=%s model=%s voice=%s rate=%u "
        "transport=%s resampling=%s",
        STACKCHAN_REALTIME_PROVIDER, STACKCHAN_REALTIME_MODEL,
        STACKCHAN_REALTIME_VOICE,
        (unsigned)STACKCHAN_REALTIME_SAMPLE_RATE,
        STACKCHAN_REALTIME_BINARY_TRANSPORT ? "binary" : "json_base64",
        STACKCHAN_REALTIME_NEEDS_RESAMPLING ? "true" : "false");
    return ESP_OK;
}

esp_err_t realtime_client_cancel(void)
{
    if (!s_initialized || s_control_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_bool(&s_response_active) &&
        !atomic_bool(&s_generation_active) &&
        !atomic_bool(&s_accept_output) &&
        !atomic_bool(&s_response_done) &&
        xStreamBufferBytesAvailable(s_output_stream) == 0 &&
        audio_pipeline_playback_pending_samples() == 0) {
        return ESP_OK;
    }
    xTaskNotify(s_control_task, CONTROL_CANCEL, eSetBits);
    return ESP_OK;
}

esp_err_t realtime_client_run_output_probe(void)
{
    if (!s_initialized || s_control_task == NULL ||
        !atomic_bool(&s_session_ready)) {
        return ESP_ERR_INVALID_STATE;
    }
    xTaskNotify(s_control_task, CONTROL_OUTPUT_PROBE, eSetBits);
    return ESP_OK;
}

bool realtime_client_is_streaming(void)
{
    return atomic_bool(&s_streaming);
}

void realtime_client_snapshot(realtime_client_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->initialized = atomic_bool(&s_initialized);
    snapshot->connected = atomic_bool(&s_connected);
    snapshot->session_ready = atomic_bool(&s_session_ready);
    snapshot->streaming = atomic_bool(&s_streaming);
    snapshot->speech_active = atomic_bool(&s_vad_speech_active);
    snapshot->response_active = atomic_bool(&s_response_active);
    snapshot->turns =
        __atomic_load_n(&s_turns, __ATOMIC_RELAXED);
    snapshot->captured_input_samples =
        __atomic_load_n(&s_captured_input_samples, __ATOMIC_RELAXED);
    snapshot->uploaded_input_samples =
        __atomic_load_n(&s_uploaded_input_samples, __ATOMIC_RELAXED);
    snapshot->received_output_samples =
        __atomic_load_n(&s_received_output_samples, __ATOMIC_RELAXED);
    snapshot->played_output_samples =
        __atomic_load_n(&s_played_output_samples, __ATOMIC_RELAXED);
    snapshot->dropped_output_samples =
        __atomic_load_n(&s_dropped_output_samples, __ATOMIC_RELAXED);
    snapshot->binary_audio_frames_received =
        __atomic_load_n(
            &s_binary_audio_frames_received, __ATOMIC_RELAXED);
    snapshot->events_received =
        __atomic_load_n(&s_events_received, __ATOMIC_RELAXED);
    snapshot->websocket_errors =
        __atomic_load_n(&s_websocket_errors, __ATOMIC_RELAXED);
    snapshot->resampler_errors =
        __atomic_load_n(&s_resampler_errors, __ATOMIC_RELAXED);
    snapshot->input_resampler_capacity_samples =
        s_input_provider_capacity;
    snapshot->output_resampler_capacity_samples =
        s_playout_device_capacity;
    snapshot->speech_end_to_first_playback_ms =
        __atomic_load_n(
            &s_speech_end_to_first_playback_ms, __ATOMIC_RELAXED);

    if (s_state_lock != NULL) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        snprintf(
            snapshot->input_transcript,
            sizeof(snapshot->input_transcript), "%s", s_input_transcript);
        snprintf(
            snapshot->output_transcript,
            sizeof(snapshot->output_transcript), "%s",
            s_output_transcript);
        snprintf(
            snapshot->last_event, sizeof(snapshot->last_event), "%s",
            s_last_event);
        snprintf(
            snapshot->last_error, sizeof(snapshot->last_error), "%s",
            s_last_error);
        xSemaphoreGive(s_state_lock);
    }
}
