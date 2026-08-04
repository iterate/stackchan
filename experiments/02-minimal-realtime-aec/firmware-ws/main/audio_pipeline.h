#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "face_animator.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    int gain_db;
    uint32_t processed_samples;
    uint32_t limited_samples;
    uint32_t overrange_samples;
    uint32_t source_peak;
    uint32_t pre_limiter_peak;
    uint32_t output_peak;
} audio_pipeline_level_snapshot_t;

typedef struct {
    uint32_t epoch;
    uint32_t frames;
    uint32_t over_budget_frames;
    uint32_t last_frame_us;
    uint32_t maximum_frame_us;
    uint32_t last_write_us;
    uint32_t maximum_write_us;
    uint32_t last_read_us;
    uint32_t maximum_read_us;
    uint32_t last_reference_us;
    uint32_t maximum_reference_us;
    uint32_t last_aec_us;
    uint32_t maximum_aec_us;
    uint32_t last_aec_linear_us;
    uint32_t maximum_aec_linear_us;
    uint32_t last_aec_nlp_us;
    uint32_t maximum_aec_nlp_us;
    uint32_t average_frame_us;
    uint32_t average_write_us;
    uint32_t average_read_us;
    uint32_t average_reference_us;
    uint32_t average_aec_us;
    uint32_t average_aec_linear_us;
    uint32_t average_aec_nlp_us;
    uint32_t tx_dma_events;
    uint32_t tx_queue_overflows;
    uint32_t rx_dma_events;
    uint32_t rx_queue_overflows;
    uint32_t dma_tap_pairs;
    uint32_t dma_tap_tx_drops;
    uint32_t dma_tap_rx_drops;
    uint32_t dma_tap_sequence_skips;
    uint32_t audio_stack_minimum_free_bytes;
} audio_pipeline_timing_snapshot_t;

esp_err_t audio_pipeline_init(void);
bool audio_pipeline_is_ready(void);
esp_err_t audio_pipeline_set_speaker_volume(int volume_percent);
int audio_pipeline_speaker_volume(void);
esp_err_t audio_pipeline_set_playback_gain(int gain_db);
void audio_pipeline_level_snapshot(audio_pipeline_level_snapshot_t *snapshot);
esp_err_t audio_pipeline_set_microphone_gain(int gain_db);
int audio_pipeline_microphone_gain(void);
esp_err_t audio_pipeline_set_aec_reference_delay_ms(int delay_ms);
int audio_pipeline_aec_reference_delay_ms(void);
esp_err_t audio_pipeline_set_aec_nlp_level(int level);
int audio_pipeline_aec_nlp_level(void);
const char *audio_pipeline_aec_mode_name(void);
void audio_pipeline_timing_snapshot(
    audio_pipeline_timing_snapshot_t *snapshot);
void audio_pipeline_timing_reset(void);
void audio_pipeline_face_snapshot(face_animator_state_t *snapshot);
void audio_pipeline_face_event(const face_stream_event_t *event);

/* All stream data is 16 kHz mono signed 16-bit PCM. */
size_t audio_pipeline_write_playback(const int16_t *samples, size_t sample_count,
                                     TickType_t timeout);
size_t audio_pipeline_read_clean(int16_t *samples, size_t sample_capacity,
                                 TickType_t timeout);
void audio_pipeline_flush_playback(void);
void audio_pipeline_flush_clean(void);
size_t audio_pipeline_playback_pending_samples(void);
size_t audio_pipeline_clean_pending_samples(void);
size_t audio_pipeline_frame_samples(void);
