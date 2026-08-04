#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t processed_samples;
    uint32_t limited_samples;
    uint32_t overrange_samples;
    uint32_t source_peak;
    uint32_t pre_limiter_peak;
    uint32_t output_peak;
} speech_leveler_metrics_t;

bool speech_leveler_gain_is_valid(int gain_db);

/*
 * Applies speech gain and a smooth limiter in place.
 *
 * The limiter is linear below -0.92 dBFS, then approaches full scale without
 * hard clipping. The resulting samples must be used as the AEC reference.
 */
void speech_leveler_process(int16_t *samples, size_t sample_count, int gain_db,
                            speech_leveler_metrics_t *metrics);
