#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define STACKCHAN_AUDIO_TRACE_CHANNELS 3

typedef struct {
    const int16_t *interleaved;
    size_t capacity_frames;
    size_t start_frame;
    size_t frame_count;
} audio_trace_view_t;

esp_err_t audio_trace_init(void);
void audio_trace_record(const int16_t *raw, const int16_t *reference,
                        const int16_t *clean, size_t sample_count);
esp_err_t audio_trace_lock(audio_trace_view_t *view);
void audio_trace_unlock(void);
