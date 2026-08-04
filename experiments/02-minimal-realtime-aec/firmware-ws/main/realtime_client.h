#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool initialized;
    bool connected;
    bool session_ready;
    bool streaming;
    bool speech_active;
    bool response_active;
    uint32_t turns;
    uint32_t captured_input_samples;
    uint32_t uploaded_input_samples;
    uint32_t received_output_samples;
    uint32_t played_output_samples;
    uint32_t dropped_output_samples;
    uint32_t binary_audio_frames_received;
    uint32_t events_received;
    uint32_t websocket_errors;
    uint32_t resampler_errors;
    uint32_t input_resampler_capacity_samples;
    uint32_t output_resampler_capacity_samples;
    int32_t speech_end_to_first_playback_ms;
    char input_transcript[256];
    char output_transcript[256];
    char last_event[64];
    char last_error[192];
} realtime_client_snapshot_t;

esp_err_t realtime_client_init(void);
esp_err_t realtime_client_cancel(void);
esp_err_t realtime_client_run_output_probe(void);
bool realtime_client_is_streaming(void);
void realtime_client_snapshot(realtime_client_snapshot_t *snapshot);
