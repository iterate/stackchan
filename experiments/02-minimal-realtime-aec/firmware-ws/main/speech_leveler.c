#include "speech_leveler.h"

#include <string.h>

#define Q16_SHIFT 16
#define Q16_HALF (1 << (Q16_SHIFT - 1))
#define LIMITER_KNEE 29490
#define PCM_MAX 32767

/* 10^(dB/20), represented as unsigned Q16, for 0 through 12 dB. */
static const uint32_t s_gain_q16[] = {
    65536,  73533,  82505,  92572,  103868, 116541, 130762,
    146717, 164619, 184706, 207243, 232531, 260904,
};

static uint32_t magnitude(int32_t sample)
{
    return sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
}

static void note_peak(uint32_t *peak, uint32_t candidate)
{
    if (candidate > *peak) {
        *peak = candidate;
    }
}

bool speech_leveler_gain_is_valid(int gain_db)
{
    return gain_db >= 0 &&
           gain_db < (int)(sizeof(s_gain_q16) / sizeof(s_gain_q16[0]));
}

void speech_leveler_process(int16_t *samples, size_t sample_count, int gain_db,
                            speech_leveler_metrics_t *metrics)
{
    speech_leveler_metrics_t local = {0};
    if (samples == NULL || !speech_leveler_gain_is_valid(gain_db)) {
        if (metrics != NULL) {
            *metrics = local;
        }
        return;
    }

    const uint32_t gain_q16 = s_gain_q16[gain_db];
    const uint32_t limiter_headroom = PCM_MAX - LIMITER_KNEE;

    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t input = samples[index];
        const int64_t product = (int64_t)input * gain_q16;
        const int32_t amplified =
            (int32_t)((product + (product < 0 ? -Q16_HALF : Q16_HALF)) /
                      (1 << Q16_SHIFT));
        const uint32_t input_magnitude = magnitude(input);
        uint32_t output_magnitude = magnitude(amplified);

        note_peak(&local.source_peak, input_magnitude);
        note_peak(&local.pre_limiter_peak, output_magnitude);
        if (output_magnitude > PCM_MAX) {
            local.overrange_samples++;
        }
        if (output_magnitude > LIMITER_KNEE) {
            const uint32_t excess = output_magnitude - LIMITER_KNEE;
            output_magnitude =
                LIMITER_KNEE +
                (uint32_t)(((uint64_t)limiter_headroom * excess) /
                           (excess + limiter_headroom));
            local.limited_samples++;
        }
        note_peak(&local.output_peak, output_magnitude);
        samples[index] =
            (int16_t)(amplified < 0 ? -(int32_t)output_magnitude
                                    : (int32_t)output_magnitude);
    }
    local.processed_samples = (uint32_t)sample_count;
    if (metrics != NULL) {
        *metrics = local;
    }
}
