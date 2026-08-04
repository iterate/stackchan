#include "web_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_status.h"
#include "audio_pipeline.h"
#include "audio_trace.h"
#include "debug_log.h"
#include "diagnostics.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "realtime_client.h"
#include "ui.h"

#define WAV_CHANNELS 3
#define WAV_BITS_PER_SAMPLE 16
#define HTTP_READ_CHUNK 4096
#define CAPTURE_DOWNLOAD_FRAMES 512

static const char *TAG = "web";

static web_control_callbacks_t s_callbacks;
static bool s_diagnostic_display_active;
static char s_diagnostic_participant[16] = "quiet";

static esp_err_t send_json(httpd_req_t *request, const char *json)
{
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *message)
{
    char body[192];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
    httpd_resp_set_status(request, status);
    return send_json(request, body);
}

static esp_err_t root_handler(httpd_req_t *request)
{
    static const char response[] =
        "{"
        "\"firmware\":\"stackchan-selectable-realtime-websocket\","
        "\"audio\":\"16kHz full-duplex software AEC\","
        "\"realtime_provider\":\"" STACKCHAN_REALTIME_PROVIDER "\","
        "\"realtime_transport\":\""
#if STACKCHAN_REALTIME_BINARY_TRANSPORT
        "binary"
#else
        "json_base64"
#endif
        "\","
        "\"diagnostic\":\"raw/reference/clean synchronized WAV\","
        "\"status\":\"/api/status\","
        "\"screen\":\"/api/debug/screen.bmp\","
        "\"logs\":\"/api/debug/logs.txt\","
        "\"recent_audio\":\"/api/debug/recent.wav\","
        "\"debug_state\":\"/api/debug/state\","
        "\"realtime\":\"/api/realtime\","
        "\"realtime_output_probe\":\"POST /api/realtime/probe\","
        "\"speaker_volume\":\"/api/audio/volume\","
        "\"speech_gain\":\"/api/audio/gain\","
        "\"microphone_gain\":\"/api/audio/mic-gain\","
        "\"aec_reference_offset\":\"/api/audio/aec-reference-offset\","
        "\"aec_nlp\":\"/api/audio/aec-nlp\""
        "}";
    return send_json(request, response);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    app_status_snapshot_t app;
    diagnostics_snapshot_t diagnostic;
    audio_pipeline_level_snapshot_t level;
    audio_pipeline_timing_snapshot_t timing;
    app_status_snapshot(&app);
    diagnostics_snapshot(&diagnostic);
    audio_pipeline_level_snapshot(&level);
    audio_pipeline_timing_snapshot(&timing);

    char response[1536];
    snprintf(
        response, sizeof(response),
        "{"
        "\"phase\":\"%s\","
        "\"detail\":\"%s\","
        "\"wifi\":%s,"
        "\"ip\":\"%s\","
        "\"audio_ready\":%s,"
        "\"speaker_volume\":%d,"
        "\"playback_gain_db\":%d,"
        "\"microphone_gain_db\":%d,"
        "\"aec_reference_offset_ms\":%d,"
        "\"aec_mode\":\"%s\","
        "\"aec_filter_length\":%d,"
        "\"aec_nlp_level\":%d,"
        "\"playback_limited_samples\":%u,"
        "\"playback_overrange_samples\":%u,"
        "\"realtime_active\":%s,"
        "\"realtime_session_ready\":%s,"
        "\"audio_frames\":%u,"
        "\"audio_read_errors\":%u,"
        "\"audio_write_errors\":%u,"
        "\"playback_underruns\":%u,"
        "\"audio_timing\":{"
        "\"epoch\":%u,"
        "\"frames\":%u,"
        "\"over_budget_frames\":%u,"
        "\"last_frame_us\":%u,"
        "\"maximum_frame_us\":%u,"
        "\"last_write_us\":%u,"
        "\"maximum_write_us\":%u,"
        "\"last_read_us\":%u,"
        "\"maximum_read_us\":%u,"
        "\"last_reference_us\":%u,"
        "\"maximum_reference_us\":%u,"
        "\"last_aec_us\":%u,"
        "\"maximum_aec_us\":%u,"
        "\"last_aec_linear_us\":%u,"
        "\"maximum_aec_linear_us\":%u,"
        "\"last_aec_nlp_us\":%u,"
        "\"maximum_aec_nlp_us\":%u,"
        "\"average_frame_us\":%u,"
        "\"average_write_us\":%u,"
        "\"average_read_us\":%u,"
        "\"average_reference_us\":%u,"
        "\"average_aec_us\":%u,"
        "\"average_aec_linear_us\":%u,"
        "\"average_aec_nlp_us\":%u,"
        "\"tx_dma_events\":%u,"
        "\"tx_queue_overflows\":%u,"
        "\"rx_dma_events\":%u,"
        "\"rx_queue_overflows\":%u,"
        "\"audio_stack_minimum_free_bytes\":%u"
        "},"
        "\"diagnostic_state\":\"%s\""
        "}",
        app_status_phase_name(app.phase),
        app.detail,
        app.wifi_connected ? "true" : "false",
        app.ip_address,
        audio_pipeline_is_ready() ? "true" : "false",
        audio_pipeline_speaker_volume(),
        level.gain_db,
        audio_pipeline_microphone_gain(),
        audio_pipeline_aec_reference_delay_ms(),
        audio_pipeline_aec_mode_name(),
        STACKCHAN_AEC_FILTER_LENGTH,
        audio_pipeline_aec_nlp_level(),
        (unsigned)level.limited_samples,
        (unsigned)level.overrange_samples,
        app.realtime_active ? "true" : "false",
        app.realtime_session_ready ? "true" : "false",
        (unsigned)app.audio_frames,
        (unsigned)app.audio_read_errors,
        (unsigned)app.audio_write_errors,
        (unsigned)app.playback_underruns,
        (unsigned)timing.epoch,
        (unsigned)timing.frames,
        (unsigned)timing.over_budget_frames,
        (unsigned)timing.last_frame_us,
        (unsigned)timing.maximum_frame_us,
        (unsigned)timing.last_write_us,
        (unsigned)timing.maximum_write_us,
        (unsigned)timing.last_read_us,
        (unsigned)timing.maximum_read_us,
        (unsigned)timing.last_reference_us,
        (unsigned)timing.maximum_reference_us,
        (unsigned)timing.last_aec_us,
        (unsigned)timing.maximum_aec_us,
        (unsigned)timing.last_aec_linear_us,
        (unsigned)timing.maximum_aec_linear_us,
        (unsigned)timing.last_aec_nlp_us,
        (unsigned)timing.maximum_aec_nlp_us,
        (unsigned)timing.average_frame_us,
        (unsigned)timing.average_write_us,
        (unsigned)timing.average_read_us,
        (unsigned)timing.average_reference_us,
        (unsigned)timing.average_aec_us,
        (unsigned)timing.average_aec_linear_us,
        (unsigned)timing.average_aec_nlp_us,
        (unsigned)timing.tx_dma_events,
        (unsigned)timing.tx_queue_overflows,
        (unsigned)timing.rx_dma_events,
        (unsigned)timing.rx_queue_overflows,
        (unsigned)timing.audio_stack_minimum_free_bytes,
        diagnostics_state_name(diagnostic.state));
    return send_json(request, response);
}

static esp_err_t realtime_status_handler(httpd_req_t *request)
{
    realtime_client_snapshot_t snapshot;
    realtime_client_snapshot(&snapshot);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return send_error(request, "503 Service Unavailable",
                          "Unable to allocate Realtime status");
    }
    cJSON_AddStringToObject(
        root, "provider", STACKCHAN_REALTIME_PROVIDER);
    cJSON_AddStringToObject(root, "model", STACKCHAN_REALTIME_MODEL);
    cJSON_AddStringToObject(root, "voice", STACKCHAN_REALTIME_VOICE);
    cJSON_AddStringToObject(
        root, "transport",
        STACKCHAN_REALTIME_BINARY_TRANSPORT ? "binary" : "json_base64");
    cJSON_AddNumberToObject(
        root, "device_sample_rate_hz", STACKCHAN_AUDIO_SAMPLE_RATE);
    cJSON_AddNumberToObject(
        root, "provider_sample_rate_hz", STACKCHAN_REALTIME_SAMPLE_RATE);
    cJSON_AddBoolToObject(
        root, "resampling_active",
        STACKCHAN_REALTIME_NEEDS_RESAMPLING);
    cJSON_AddBoolToObject(root, "initialized", snapshot.initialized);
    cJSON_AddBoolToObject(root, "connected", snapshot.connected);
    cJSON_AddBoolToObject(root, "session_ready", snapshot.session_ready);
    cJSON_AddStringToObject(
        root, "input_mode", "continuous_server_vad");
    cJSON_AddBoolToObject(root, "streaming", snapshot.streaming);
    cJSON_AddBoolToObject(
        root, "speech_active", snapshot.speech_active);
    cJSON_AddBoolToObject(
        root, "response_active", snapshot.response_active);
    cJSON_AddNumberToObject(root, "turns", snapshot.turns);
    cJSON_AddNumberToObject(
        root, "captured_input_samples",
        snapshot.captured_input_samples);
    cJSON_AddNumberToObject(
        root, "uploaded_input_samples",
        snapshot.uploaded_input_samples);
    cJSON_AddNumberToObject(
        root, "received_output_samples",
        snapshot.received_output_samples);
    cJSON_AddNumberToObject(
        root, "played_output_samples",
        snapshot.played_output_samples);
    cJSON_AddNumberToObject(
        root, "dropped_output_samples",
        snapshot.dropped_output_samples);
    cJSON_AddNumberToObject(
        root, "binary_audio_frames_received",
        snapshot.binary_audio_frames_received);
    cJSON_AddNumberToObject(
        root, "events_received", snapshot.events_received);
    cJSON_AddNumberToObject(
        root, "websocket_errors", snapshot.websocket_errors);
    cJSON_AddNumberToObject(
        root, "resampler_errors", snapshot.resampler_errors);
    cJSON_AddNumberToObject(
        root, "input_resampler_capacity_samples",
        snapshot.input_resampler_capacity_samples);
    cJSON_AddNumberToObject(
        root, "output_resampler_capacity_samples",
        snapshot.output_resampler_capacity_samples);
    cJSON_AddNumberToObject(
        root, "speech_end_to_first_playback_ms",
        snapshot.speech_end_to_first_playback_ms);
    cJSON_AddStringToObject(
        root, "input_transcript", snapshot.input_transcript);
    cJSON_AddStringToObject(
        root, "output_transcript", snapshot.output_transcript);
    cJSON_AddStringToObject(root, "last_event", snapshot.last_event);
    cJSON_AddStringToObject(root, "last_error", snapshot.last_error);

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response == NULL) {
        return send_error(request, "503 Service Unavailable",
                          "Unable to render Realtime status");
    }
    const esp_err_t result = send_json(request, response);
    cJSON_free(response);
    return result;
}

static esp_err_t volume_get_handler(httpd_req_t *request)
{
    char response[64];
    snprintf(response, sizeof(response), "{\"volume\":%d}",
             audio_pipeline_speaker_volume());
    return send_json(request, response);
}

static esp_err_t volume_set_handler(httpd_req_t *request)
{
    char query[48];
    char value[8];
    if (httpd_req_get_url_query_str(
            request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query, "value", value, sizeof(value)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Use ?value=0..100");
    }
    char *end = NULL;
    const long volume = strtol(value, &end, 10);
    if (end == value || *end != '\0' || volume < 0 || volume > 100) {
        return send_error(request, "400 Bad Request",
                          "Volume must be 0..100");
    }
    if (audio_pipeline_set_speaker_volume((int)volume) != ESP_OK) {
        return send_error(request, "503 Service Unavailable",
                          "Unable to set speaker volume");
    }
    return volume_get_handler(request);
}

static esp_err_t gain_get_handler(httpd_req_t *request)
{
    audio_pipeline_level_snapshot_t level;
    audio_pipeline_level_snapshot(&level);
    char response[256];
    snprintf(
        response, sizeof(response),
        "{"
        "\"gain_db\":%d,"
        "\"processed_samples\":%u,"
        "\"limited_samples\":%u,"
        "\"overrange_samples\":%u,"
        "\"source_peak\":%u,"
        "\"pre_limiter_peak\":%u,"
        "\"output_peak\":%u"
        "}",
        level.gain_db,
        (unsigned)level.processed_samples,
        (unsigned)level.limited_samples,
        (unsigned)level.overrange_samples,
        (unsigned)level.source_peak,
        (unsigned)level.pre_limiter_peak,
        (unsigned)level.output_peak);
    return send_json(request, response);
}

static esp_err_t gain_set_handler(httpd_req_t *request)
{
    char query[48];
    char value[8];
    if (httpd_req_get_url_query_str(
            request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query, "db", value, sizeof(value)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Use ?db=0..12");
    }
    char *end = NULL;
    const long gain_db = strtol(value, &end, 10);
    if (end == value || *end != '\0' ||
        audio_pipeline_set_playback_gain((int)gain_db) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Gain must be 0..12 dB");
    }
    return gain_get_handler(request);
}

static esp_err_t microphone_gain_get_handler(httpd_req_t *request)
{
    char response[64];
    snprintf(response, sizeof(response), "{\"gain_db\":%d}",
             audio_pipeline_microphone_gain());
    return send_json(request, response);
}

static esp_err_t microphone_gain_set_handler(httpd_req_t *request)
{
    char query[48];
    char value[8];
    if (httpd_req_get_url_query_str(
            request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query, "db", value, sizeof(value)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Use ?db=0..37");
    }
    char *end = NULL;
    const long gain_db = strtol(value, &end, 10);
    if (end == value || *end != '\0' ||
        audio_pipeline_set_microphone_gain((int)gain_db) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Microphone gain must be 0..37 dB");
    }
    return microphone_gain_get_handler(request);
}

static esp_err_t aec_reference_offset_get_handler(httpd_req_t *request)
{
    char response[112];
    snprintf(
        response, sizeof(response),
        "{\"offset_ms\":%d,\"minimum_ms\":0,\"maximum_ms\":%d}",
        audio_pipeline_aec_reference_delay_ms(),
        STACKCHAN_AEC_REFERENCE_DELAY_MAX_MS);
    return send_json(request, response);
}

static esp_err_t aec_reference_offset_set_handler(httpd_req_t *request)
{
    char query[48];
    char value[8];
    if (httpd_req_get_url_query_str(
            request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query, "ms", value, sizeof(value)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Use ?ms=0..64");
    }
    char *end = NULL;
    const long offset_ms = strtol(value, &end, 10);
    if (end == value || *end != '\0' ||
        audio_pipeline_set_aec_reference_delay_ms(
            (int)offset_ms) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "AEC reference offset must be 0..64 ms");
    }
    return aec_reference_offset_get_handler(request);
}

static esp_err_t aec_nlp_get_handler(httpd_req_t *request)
{
    char response[96];
    snprintf(
        response, sizeof(response),
        "{\"level\":%d,\"levels\":{\"normal\":0,\"aggressive\":1,"
        "\"very_aggressive\":2}}",
        audio_pipeline_aec_nlp_level());
    return send_json(request, response);
}

static esp_err_t aec_nlp_set_handler(httpd_req_t *request)
{
    char query[48];
    char value[8];
    if (httpd_req_get_url_query_str(
            request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(
            query, "level", value, sizeof(value)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Use ?level=0..2");
    }
    char *end = NULL;
    const long level = strtol(value, &end, 10);
    if (end == value || *end != '\0' ||
        audio_pipeline_set_aec_nlp_level((int)level) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "AEC NLP level must be 0..2");
    }
    return aec_nlp_get_handler(request);
}

static esp_err_t debug_state_handler(httpd_req_t *request)
{
    const esp_app_desc_t *description = esp_app_get_description();
    char response[1024];
    snprintf(
        response, sizeof(response),
        "{"
        "\"project\":\"%s\","
        "\"version\":\"%s\","
        "\"idf\":\"%s\","
        "\"build_date\":\"%s\","
        "\"build_time\":\"%s\","
        "\"uptime_ms\":%llu,"
        "\"reset_reason\":%u,"
        "\"heap\":{"
        "\"free\":%u,"
        "\"minimum_free\":%u,"
        "\"internal_free\":%u,"
        "\"internal_largest\":%u,"
        "\"psram_free\":%u,"
        "\"psram_minimum_free\":%u,"
        "\"psram_largest\":%u"
        "}"
        "}",
        description->project_name,
        description->version,
        description->idf_ver,
        description->date,
        description->time,
        (unsigned long long)(esp_timer_get_time() / 1000),
        (unsigned)esp_reset_reason(),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                          MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                   MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                          MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM |
                                                  MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                   MALLOC_CAP_8BIT));
    return send_json(request, response);
}

static esp_err_t debug_logs_handler(httpd_req_t *request)
{
    char *logs = heap_caps_malloc(
        STACKCHAN_DEBUG_LOG_BYTES + 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (logs == NULL) {
        return send_error(request, "503 Service Unavailable",
                          "Not enough PSRAM to snapshot logs");
    }
    const size_t bytes =
        debug_log_copy(logs, STACKCHAN_DEBUG_LOG_BYTES);
    logs[bytes] = '\0';

    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result =
        httpd_resp_send(request, logs, (ssize_t)bytes);
    heap_caps_free(logs);
    return result;
}

static esp_err_t receive_request_body(httpd_req_t *request, uint8_t *buffer,
                                      size_t bytes)
{
    size_t received = 0;
    while (received < bytes) {
        int result = httpd_req_recv(
            request, (char *)buffer + received, bytes - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    return ESP_OK;
}

static esp_err_t diagnostic_signal_handler(httpd_req_t *request)
{
    const size_t bytes = (size_t)request->content_len;
    if (bytes == 0 || bytes > STACKCHAN_HTTP_MAX_UPLOAD_BYTES ||
        bytes % sizeof(int16_t) != 0) {
        return send_error(request, "400 Bad Request",
                          "Expected up to 10 seconds of 16-bit mono PCM");
    }

    char sample_rate[16] = {0};
    if (httpd_req_get_hdr_value_len(request, "X-Sample-Rate") > 0 &&
        (httpd_req_get_hdr_value_str(
             request, "X-Sample-Rate", sample_rate,
             sizeof(sample_rate)) != ESP_OK ||
         atoi(sample_rate) != STACKCHAN_AUDIO_SAMPLE_RATE)) {
        return send_error(request, "400 Bad Request",
                          "X-Sample-Rate must be 16000");
    }

    int16_t *body =
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        return send_error(request, "503 Service Unavailable",
                          "Not enough PSRAM for signal");
    }
    esp_err_t result = receive_request_body(request, (uint8_t *)body, bytes);
    if (result == ESP_OK) {
        result = diagnostics_set_signal(body, bytes / sizeof(int16_t));
    }
    heap_caps_free(body);

    if (result == ESP_ERR_INVALID_STATE) {
        return send_error(request, "409 Conflict",
                          "A diagnostic capture is running");
    }
    if (result != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "Unable to store diagnostic signal");
    }
    return send_json(request, "{\"ok\":true}");
}

static esp_err_t diagnostic_start_handler(httpd_req_t *request)
{
    if (!audio_pipeline_is_ready()) {
        return send_error(request, "503 Service Unavailable",
                          "Audio pipeline is not running");
    }

    char participant[sizeof(s_diagnostic_participant)] = "quiet";
    if (httpd_req_get_hdr_value_len(request, "X-Diagnostic-Participant") > 0 &&
        httpd_req_get_hdr_value_str(
            request, "X-Diagnostic-Participant", participant,
            sizeof(participant)) != ESP_OK) {
        return send_error(request, "400 Bad Request",
                          "Diagnostic participant header is too long");
    }
    if (strcmp(participant, "quiet") != 0 &&
        strcmp(participant, "speak") != 0) {
        return send_error(request, "400 Bad Request",
                          "X-Diagnostic-Participant must be quiet or speak");
    }

    if (s_callbacks.stop_conversation != NULL) {
        (void)s_callbacks.stop_conversation(s_callbacks.context);
    }
    audio_pipeline_flush_playback();
    audio_pipeline_flush_clean();

    const esp_err_t result = diagnostics_start();
    if (result == ESP_ERR_INVALID_STATE) {
        return send_error(request, "409 Conflict",
                          "Upload a signal or wait for the running capture");
    }
    if (result != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "Unable to start synchronized capture");
    }

    snprintf(s_diagnostic_participant, sizeof(s_diagnostic_participant), "%s",
             participant);
    s_diagnostic_display_active = true;
    if (strcmp(participant, "speak") == 0) {
        ui_set_status("SPEAK NOW",
                      "Talk near StackChan until this message changes");
    } else {
        ui_set_status("QUIET - TESTING",
                      "Please do not speak until this message changes");
    }

    char response[96];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"state\":\"running\",\"participant\":\"%s\"}",
             participant);
    return send_json(request, response);
}

static esp_err_t diagnostic_status_handler(httpd_req_t *request)
{
    diagnostics_snapshot_t snapshot;
    diagnostics_snapshot(&snapshot);

    if (s_diagnostic_display_active &&
        snapshot.state != DIAGNOSTICS_RUNNING) {
        if (snapshot.state == DIAGNOSTICS_ERROR) {
            ui_set_status("Capture error", snapshot.error);
            diagnostics_release_realtime();
        } else {
            ui_set_status("Capture complete", "You can speak normally");
        }
        s_diagnostic_display_active = false;
    }

    char response[368];
    snprintf(
        response, sizeof(response),
        "{"
        "\"state\":\"%s\","
        "\"participant\":\"%s\","
        "\"signal_samples\":%u,"
        "\"captured_samples\":%u,"
        "\"target_samples\":%u,"
        "\"sample_rate\":%u,"
        "\"channels\":[\"raw\",\"reference\",\"clean\"],"
        "\"error\":\"%s\""
        "}",
        diagnostics_state_name(snapshot.state),
        s_diagnostic_participant,
        (unsigned)snapshot.signal_samples,
        (unsigned)snapshot.captured_samples,
        (unsigned)snapshot.target_samples,
        STACKCHAN_AUDIO_SAMPLE_RATE,
        snapshot.error);
    return send_json(request, response);
}

static void put_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xff);
    destination[1] = (uint8_t)((value >> 8) & 0xff);
}

static void put_le32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xff);
    destination[1] = (uint8_t)((value >> 8) & 0xff);
    destination[2] = (uint8_t)((value >> 16) & 0xff);
    destination[3] = (uint8_t)((value >> 24) & 0xff);
}

static void make_wav_header(uint8_t header[44], size_t sample_frames)
{
    const uint32_t data_bytes = (uint32_t)(
        sample_frames * WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8));
    const uint32_t byte_rate = STACKCHAN_AUDIO_SAMPLE_RATE * WAV_CHANNELS *
                               (WAV_BITS_PER_SAMPLE / 8);
    memset(header, 0, 44);
    memcpy(header + 0, "RIFF", 4);
    put_le32(header + 4, 36 + data_bytes);
    memcpy(header + 8, "WAVEfmt ", 8);
    put_le32(header + 16, 16);
    put_le16(header + 20, 1);
    put_le16(header + 22, WAV_CHANNELS);
    put_le32(header + 24, STACKCHAN_AUDIO_SAMPLE_RATE);
    put_le32(header + 28, byte_rate);
    put_le16(header + 32, WAV_CHANNELS * (WAV_BITS_PER_SAMPLE / 8));
    put_le16(header + 34, WAV_BITS_PER_SAMPLE);
    memcpy(header + 36, "data", 4);
    put_le32(header + 40, data_bytes);
}

static esp_err_t recent_audio_handler(httpd_req_t *request)
{
    audio_trace_view_t view;
    esp_err_t result = audio_trace_lock(&view);
    if (result != ESP_OK || view.frame_count == 0) {
        if (result == ESP_OK) {
            audio_trace_unlock();
        }
        return send_error(request, "409 Conflict",
                          "No rolling audio trace is available");
    }

    httpd_resp_set_type(request, "audio/wav");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request, "Content-Disposition",
        "attachment; filename=\"stackchan-recent-audio.wav\"");

    uint8_t header[44];
    make_wav_header(header, view.frame_count);
    result =
        httpd_resp_send_chunk(request, (const char *)header, sizeof(header));

    const size_t first_frames =
        view.frame_count < view.capacity_frames - view.start_frame
            ? view.frame_count
            : view.capacity_frames - view.start_frame;
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(
            request,
            (const char *)(view.interleaved +
                           view.start_frame *
                               STACKCHAN_AUDIO_TRACE_CHANNELS),
            first_frames * STACKCHAN_AUDIO_TRACE_CHANNELS *
                sizeof(int16_t));
    }
    const size_t second_frames = view.frame_count - first_frames;
    if (result == ESP_OK && second_frames > 0) {
        result = httpd_resp_send_chunk(
            request, (const char *)view.interleaved,
            second_frames * STACKCHAN_AUDIO_TRACE_CHANNELS *
                sizeof(int16_t));
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, NULL, 0);
    }
    audio_trace_unlock();
    return result;
}

static void make_bmp_header(uint8_t header[54], uint32_t width,
                            uint32_t height, uint32_t row_bytes)
{
    const uint32_t image_bytes = row_bytes * height;
    memset(header, 0, 54);
    memcpy(header, "BM", 2);
    put_le32(header + 2, 54 + image_bytes);
    put_le32(header + 10, 54);
    put_le32(header + 14, 40);
    put_le32(header + 18, width);
    put_le32(header + 22, height);
    put_le16(header + 26, 1);
    put_le16(header + 28, 24);
    put_le32(header + 34, image_bytes);
    put_le32(header + 38, 2835);
    put_le32(header + 42, 2835);
}

static esp_err_t screen_handler(httpd_req_t *request)
{
    ui_snapshot_t snapshot;
    const esp_err_t capture_result = ui_snapshot_rgb565(&snapshot);
    if (capture_result != ESP_OK) {
        return send_error(request, "503 Service Unavailable",
                          "Unable to render screen snapshot");
    }

    const uint32_t row_bytes = (snapshot.width * 3 + 3) & ~3U;
    uint8_t *row = heap_caps_calloc(
        row_bytes, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (row == NULL) {
        ui_snapshot_release(&snapshot);
        return send_error(request, "503 Service Unavailable",
                          "Unable to allocate BMP row");
    }

    httpd_resp_set_type(request, "image/bmp");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request, "Content-Disposition",
        "attachment; filename=\"stackchan-screen.bmp\"");
    uint8_t header[54];
    make_bmp_header(
        header, snapshot.width, snapshot.height, row_bytes);
    esp_err_t result =
        httpd_resp_send_chunk(request, (const char *)header, sizeof(header));

    for (uint32_t output_y = 0;
         result == ESP_OK && output_y < snapshot.height; ++output_y) {
        const uint32_t source_y = snapshot.height - 1 - output_y;
        const uint16_t *source = (const uint16_t *)(
            snapshot.pixels + source_y * snapshot.stride);
        memset(row, 0, row_bytes);
        for (uint32_t x = 0; x < snapshot.width; ++x) {
            const uint16_t pixel = source[x];
            row[x * 3] =
                (uint8_t)(((pixel & 0x1f) * 255 + 15) / 31);
            row[x * 3 + 1] =
                (uint8_t)((((pixel >> 5) & 0x3f) * 255 + 31) / 63);
            row[x * 3 + 2] =
                (uint8_t)((((pixel >> 11) & 0x1f) * 255 + 15) / 31);
        }
        result = httpd_resp_send_chunk(
            request, (const char *)row, row_bytes);
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, NULL, 0);
    }
    heap_caps_free(row);
    ui_snapshot_release(&snapshot);
    return result;
}

static esp_err_t diagnostic_capture_handler(httpd_req_t *request)
{
    diagnostics_capture_view_t capture = {0};
    if (diagnostics_capture_lock(&capture) != ESP_OK) {
        return send_error(request, "409 Conflict",
                          "No completed diagnostic capture");
    }

    int16_t *interleaved = heap_caps_malloc(
        CAPTURE_DOWNLOAD_FRAMES * WAV_CHANNELS * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (interleaved == NULL) {
        diagnostics_capture_unlock();
        diagnostics_release_realtime();
        return send_error(request, "500 Internal Server Error",
                          "Unable to allocate capture download buffer");
    }

    httpd_resp_set_type(request, "audio/wav");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request, "Content-Disposition",
        "attachment; filename=\"stackchan-aec-capture.wav\"");

    uint8_t header[44];
    make_wav_header(header, capture.sample_frames);
    esp_err_t result =
        httpd_resp_send_chunk(request, (const char *)header, sizeof(header));

    for (size_t start = 0;
         result == ESP_OK && start < capture.sample_frames;
         start += CAPTURE_DOWNLOAD_FRAMES) {
        const size_t remaining = capture.sample_frames - start;
        const size_t frames = remaining < CAPTURE_DOWNLOAD_FRAMES
                                  ? remaining
                                  : CAPTURE_DOWNLOAD_FRAMES;
        for (size_t index = 0; index < frames; ++index) {
            interleaved[index * WAV_CHANNELS + 0] =
                capture.raw[start + index];
            interleaved[index * WAV_CHANNELS + 1] =
                capture.reference[start + index];
            interleaved[index * WAV_CHANNELS + 2] =
                capture.clean[start + index];
        }
        result = httpd_resp_send_chunk(
            request, (const char *)interleaved,
            frames * WAV_CHANNELS * sizeof(int16_t));
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(request, NULL, 0);
    }
    heap_caps_free(interleaved);
    diagnostics_capture_unlock();
    diagnostics_release_realtime();
    return result;
}

static esp_err_t conversation_start_handler(httpd_req_t *request)
{
    if (s_callbacks.start_conversation == NULL) {
        return send_error(request, "503 Service Unavailable",
                          "Realtime control is unavailable");
    }
    const esp_err_t result =
        s_callbacks.start_conversation(s_callbacks.context);
    if (result != ESP_OK) {
        return send_error(request, "409 Conflict",
                          "Continuous Realtime stream is not ready");
    }
    return send_json(
        request,
        "{\"ok\":true,\"mode\":\"continuous_server_vad\","
        "\"streaming\":true}");
}

static esp_err_t conversation_stop_handler(httpd_req_t *request)
{
    if (s_callbacks.stop_conversation != NULL) {
        (void)s_callbacks.stop_conversation(s_callbacks.context);
    }
    return send_json(
        request, "{\"ok\":true,\"requested\":\"cancel_response\","
                 "\"streaming\":true}");
}

static esp_err_t realtime_probe_handler(httpd_req_t *request)
{
    (void)request;
    const esp_err_t result = realtime_client_run_output_probe();
    if (result != ESP_OK) {
        return send_error(request, "409 Conflict",
                          "Realtime session is not ready for a probe");
    }
    return send_json(
        request, "{\"ok\":true,\"requested\":\"output_probe\"}");
}

static const httpd_uri_t s_routes[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_handler},
    {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
    {.uri = "/api/realtime",
     .method = HTTP_GET,
     .handler = realtime_status_handler},
    {.uri = "/api/realtime/probe",
     .method = HTTP_POST,
     .handler = realtime_probe_handler},
    {.uri = "/api/audio/volume",
     .method = HTTP_GET,
     .handler = volume_get_handler},
    {.uri = "/api/audio/volume",
     .method = HTTP_POST,
     .handler = volume_set_handler},
    {.uri = "/api/audio/gain",
     .method = HTTP_GET,
     .handler = gain_get_handler},
    {.uri = "/api/audio/gain",
     .method = HTTP_POST,
     .handler = gain_set_handler},
    {.uri = "/api/audio/mic-gain",
     .method = HTTP_GET,
     .handler = microphone_gain_get_handler},
    {.uri = "/api/audio/mic-gain",
     .method = HTTP_POST,
     .handler = microphone_gain_set_handler},
    {.uri = "/api/audio/aec-reference-offset",
     .method = HTTP_GET,
     .handler = aec_reference_offset_get_handler},
    {.uri = "/api/audio/aec-reference-offset",
     .method = HTTP_POST,
     .handler = aec_reference_offset_set_handler},
    {.uri = "/api/audio/aec-nlp",
     .method = HTTP_GET,
     .handler = aec_nlp_get_handler},
    {.uri = "/api/audio/aec-nlp",
     .method = HTTP_POST,
     .handler = aec_nlp_set_handler},
    {.uri = "/api/debug/state",
     .method = HTTP_GET,
     .handler = debug_state_handler},
    {.uri = "/api/debug/logs.txt",
     .method = HTTP_GET,
     .handler = debug_logs_handler},
    {.uri = "/api/debug/screen.bmp",
     .method = HTTP_GET,
     .handler = screen_handler},
    {.uri = "/api/debug/recent.wav",
     .method = HTTP_GET,
     .handler = recent_audio_handler},
    {.uri = "/api/diag/signal",
     .method = HTTP_POST,
     .handler = diagnostic_signal_handler},
    {.uri = "/api/diag/start",
     .method = HTTP_POST,
     .handler = diagnostic_start_handler},
    {.uri = "/api/diag/status",
     .method = HTTP_GET,
     .handler = diagnostic_status_handler},
    {.uri = "/api/diag/capture.wav",
     .method = HTTP_GET,
     .handler = diagnostic_capture_handler},
    {.uri = "/api/conversation/start",
     .method = HTTP_POST,
     .handler = conversation_start_handler},
    {.uri = "/api/conversation/stop",
     .method = HTTP_POST,
     .handler = conversation_stop_handler},
};

esp_err_t web_control_start(const web_control_callbacks_t *callbacks)
{
    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = STACKCHAN_HTTP_PORT;
    config.max_uri_handlers =
        sizeof(s_routes) / sizeof(s_routes[0]);
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(
        httpd_start(&server, &config), TAG, "Start HTTP control server");
    for (size_t index = 0;
         index < sizeof(s_routes) / sizeof(s_routes[0]); ++index) {
        ESP_RETURN_ON_ERROR(
            httpd_register_uri_handler(server, &s_routes[index]),
            TAG, "Register HTTP route");
    }
    ESP_LOGI(TAG, "Control and synchronized capture API listening on port %u",
             STACKCHAN_HTTP_PORT);
    return ESP_OK;
}
