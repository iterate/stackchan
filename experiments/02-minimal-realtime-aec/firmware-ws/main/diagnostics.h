#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    DIAGNOSTICS_IDLE = 0,
    DIAGNOSTICS_RUNNING,
    DIAGNOSTICS_READY,
    DIAGNOSTICS_ERROR,
} diagnostics_state_t;

typedef struct {
    diagnostics_state_t state;
    size_t signal_samples;
    size_t captured_samples;
    size_t target_samples;
    char error[96];
} diagnostics_snapshot_t;

typedef struct {
    const int16_t *raw;
    const int16_t *reference;
    const int16_t *clean;
    size_t sample_frames;
} diagnostics_capture_view_t;

void diagnostics_init(void);
esp_err_t diagnostics_set_signal(const int16_t *signal, size_t samples);
esp_err_t diagnostics_start(void);
bool diagnostics_is_running(void);
bool diagnostics_blocks_realtime(void);

/*
 * Called only by the audio task. The playback reference and all three
 * captured channels therefore share the same sample clock.
 */
void diagnostics_fill_playback(int16_t *reference, size_t samples);
void diagnostics_record_frame(const int16_t *raw, const int16_t *reference,
                              const int16_t *clean, size_t samples);
void diagnostics_fail(const char *message);

void diagnostics_snapshot(diagnostics_snapshot_t *snapshot);
const char *diagnostics_state_name(diagnostics_state_t state);

/*
 * A successful lock pins immutable planar raw/reference/clean capture views
 * against replacement until diagnostics_capture_unlock(). It does not hold
 * the diagnostic mutex while the HTTP task streams the file.
 */
esp_err_t diagnostics_capture_lock(diagnostics_capture_view_t *view);
void diagnostics_capture_unlock(void);
void diagnostics_release_realtime(void);
