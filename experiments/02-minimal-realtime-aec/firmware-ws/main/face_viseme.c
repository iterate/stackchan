#include "face_viseme.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/*
 * PCM acoustic front-end and Gaussian prototype classifier adapted from
 * met4citizen/HeadAudio (MIT, Copyright 2025 Mika Suominen). The immutable
 * English model remains a separate binary asset. See THIRD_PARTY_NOTICES.md.
 */

enum {
    AUDIO_SAMPLE_RATE = 16000,
    FIR_SAMPLES = 32,
    FFT_SAMPLES = 512,
    FFT_COMPLEX_FLOATS = FFT_SAMPLES * 2,
    FEATURE_HOP = 256,
    MEL_BANDS = 40,
    MFCC_COEFFICIENTS = FACE_VISEME_FEATURE_COUNT,
    MODEL_MEAN_OFFSET = 8,
    MODEL_COVARIANCE_OFFSET =
        MODEL_MEAN_OFFSET + MFCC_COEFFICIENTS * sizeof(float),
    MODEL_COVARIANCE_FLOATS =
        MFCC_COEFFICIENTS * (MFCC_COEFFICIENTS + 1) / 2,
    MAX_VOTE_WINDOW = 6,
    SILENCE_SENSITIVITY_PERCENT = 120,
    FIRST_BLINK_SAMPLES = AUDIO_SAMPLE_RATE * 22 / 10,
    FIRST_GAZE_SAMPLES = AUDIO_SAMPLE_RATE * 7 / 10,
};

static const float PREEMPHASIS_ALPHA = 0.97f;
static const float FLOAT_PCM_SCALE = 1.0f / 32768.0f;

#include "face_viseme_tables.inc"

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
} viseme_shape_t;

static const viseme_shape_t s_shapes[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {236, 205, 24, 0, 18},
    [FACE_VISEME_E] = {155, 246, 0, 0, 128},
    [FACE_VISEME_I] = {102, 255, 0, 0, 155},
    [FACE_VISEME_O] = {214, 112, 255, 0, 16},
    [FACE_VISEME_U] = {112, 82, 244, 0, 10},
    [FACE_VISEME_PP] = {12, 164, 18, 255, 0},
    [FACE_VISEME_SS] = {66, 224, 0, 0, 210},
    [FACE_VISEME_TH] = {88, 190, 0, 0, 235},
    [FACE_VISEME_DD] = {82, 194, 0, 0, 164},
    [FACE_VISEME_FF] = {38, 198, 0, 176, 235},
    [FACE_VISEME_KK] = {120, 181, 20, 0, 78},
    [FACE_VISEME_NN] = {72, 202, 0, 0, 142},
    [FACE_VISEME_RR] = {104, 148, 94, 0, 72},
    [FACE_VISEME_CH] = {86, 158, 46, 34, 184},
    [FACE_VISEME_SIL] = {0, 110, 30, 0, 0},
};

const face_viseme_config_t FACE_VISEME_DEFAULT_CONFIG = {
    .model_data = NULL,
    .model_size = 0,
    .speaker_mean_hz = 150,
    .vad_open_level = 36,
    .vad_close_level = 18,
    .mouth_level_range = 3000,
    .attack_ms = 45,
    .release_ms = 85,
    .shape_ms = 60,
    .vad_release_hops = 4,
    .vote_window = 4,
    .prime_vote_on_speech = true,
};

typedef struct {
    face_viseme_config_t config;
    face_pose_t pose;
    uint32_t published_sequence;
    uint32_t total_samples;
    uint32_t next_blink_sample;
    uint32_t next_gaze_sample;
    uint32_t hop_sum_abs;
    uint16_t hop_samples;
    uint16_t sample_write;
    uint16_t sample_count;
    uint16_t samples_since_feature;
    uint16_t mel_bins[MEL_BANDS + 2];
    uint8_t vote_ring[MAX_VOTE_WINDOW];
    uint8_t vote_write;
    uint8_t quiet_hops;
    uint8_t blink_phase;
    uint8_t fir_write;
    uint8_t attack_alpha;
    uint8_t release_alpha;
    uint8_t shape_alpha;
    bool vad_active;
    float preemphasis_previous;
    float fir_ring[FIR_SAMPLES];
    float sample_ring[FFT_SAMPLES];
    float spectrum[FFT_COMPLEX_FLOATS];
} face_viseme_state_t;

static uint32_t deterministic_random(uint32_t value)
{
    value += 0x9e3779b9U;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void publish_begin(face_viseme_state_t *state)
{
    (void)__atomic_fetch_add(
        &state->published_sequence, 1U, __ATOMIC_ACQ_REL);
}

static void publish_end(face_viseme_state_t *state)
{
    (void)__atomic_fetch_add(
        &state->published_sequence, 1U, __ATOMIC_RELEASE);
}

size_t face_viseme_model_record_count(size_t model_size)
{
    return model_size / FACE_VISEME_MODEL_RECORD_BYTES;
}

static uint32_t read_big_endian_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

bool face_viseme_model_phoneme(
    const uint8_t *model_data, size_t model_size, uint8_t prototype,
    uint32_t *first_codepoint, uint32_t *second_codepoint)
{
    const size_t count = face_viseme_model_record_count(model_size);
    if (model_data == NULL || prototype >= count ||
        first_codepoint == NULL || second_codepoint == NULL) {
        return false;
    }
    const uint32_t packed = read_big_endian_u32(
        model_data + (size_t)prototype * FACE_VISEME_MODEL_RECORD_BYTES);
    *first_codepoint = packed >> 16;
    *second_codepoint = packed & UINT16_MAX;
    return *first_codepoint != 0;
}

bool face_viseme_model_valid(const uint8_t *model_data, size_t model_size)
{
    if (model_data == NULL || model_size == 0 ||
        model_size % FACE_VISEME_MODEL_RECORD_BYTES != 0 ||
        ((uintptr_t)model_data % _Alignof(float)) != 0) {
        return false;
    }

    const size_t records = face_viseme_model_record_count(model_size);
    if (records > UINT8_MAX) {
        return false;
    }
    for (size_t record_index = 0; record_index < records; ++record_index) {
        const uint8_t *record =
            model_data + record_index * FACE_VISEME_MODEL_RECORD_BYTES;
        if (record[7] >= FACE_VISEME_COUNT) {
            return false;
        }
        const float *values = (const float *)(record + MODEL_MEAN_OFFSET);
        for (size_t index = 0;
             index < MFCC_COEFFICIENTS + MODEL_COVARIANCE_FLOATS;
             ++index) {
            if (!isfinite(values[index])) {
                return false;
            }
        }
    }
    return true;
}

static uint8_t smoothing_alpha(uint16_t time_ms)
{
    if (time_ms == 0) {
        return UINT8_MAX;
    }
    const float hop_ms =
        (float)FEATURE_HOP * 1000.0f / AUDIO_SAMPLE_RATE;
    const float alpha = 1.0f - expf(-hop_ms / time_ms);
    const long scaled = lroundf(alpha * UINT8_MAX);
    if (scaled <= 0) {
        return 1;
    }
    return (uint8_t)(scaled > UINT8_MAX ? UINT8_MAX : scaled);
}

static uint8_t smooth_u8(uint8_t current, uint8_t target, uint8_t alpha)
{
    if (current == target) {
        return current;
    }
    const uint32_t delta = current < target
                               ? (uint32_t)target - current
                               : (uint32_t)current - target;
    uint32_t movement = (delta * alpha + 127U) / UINT8_MAX;
    if (movement == 0) {
        movement = 1;
    }
    if (current < target) {
        const uint32_t next = (uint32_t)current + movement;
        return (uint8_t)(next > target ? target : next);
    }
    return movement > current - target
               ? target
               : (uint8_t)(current - movement);
}

static uint8_t scale_u8(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint32_t)value * scale + 127U) / UINT8_MAX);
}

static void build_mel_bins(face_viseme_state_t *state)
{
    const float mean_hz = state->config.speaker_mean_hz;
    float warp = mean_hz / 150.0f;
    if (warp < 0.6f) {
        warp = 0.6f;
    } else if (warp > 1.8f) {
        warp = 1.8f;
    }
    const float mel_low = 2595.0f * log10f(1.0f + 30.0f / 700.0f);
    const float mel_high =
        2595.0f * log10f(1.0f + 7800.0f / 700.0f);
    for (size_t index = 0; index < MEL_BANDS + 2; ++index) {
        const float mel =
            mel_low +
            (mel_high - mel_low) * index / (MEL_BANDS + 1);
        const float warped = mel_low + (mel - mel_low) * warp;
        const float frequency =
            700.0f * (powf(10.0f, warped / 2595.0f) - 1.0f);
        uint32_t bin =
            (uint32_t)floorf(frequency * FFT_SAMPLES / AUDIO_SAMPLE_RATE);
        if (bin >= FFT_SAMPLES / 2) {
            bin = FFT_SAMPLES / 2 - 1;
        }
        state->mel_bins[index] = (uint16_t)bin;
    }
}

static uint16_t reverse_nine_bits(uint16_t value)
{
    uint16_t reversed = 0;
    for (uint8_t bit = 0; bit < 9; ++bit) {
        reversed =
            (uint16_t)((reversed << 1) | ((value >> bit) & 1U));
    }
    return reversed;
}

static void hamming_fft(face_viseme_state_t *state)
{
    float *spectrum = state->spectrum;
    const uint16_t oldest = state->sample_write;
    for (uint16_t index = 0; index < FFT_SAMPLES; ++index) {
        const uint16_t source =
            (uint16_t)((oldest + index) & (FFT_SAMPLES - 1));
        const uint16_t destination = reverse_nine_bits(index);
        spectrum[2U * destination] =
            state->sample_ring[source] * s_hamming[index];
        spectrum[2U * destination + 1U] = 0.0f;
    }

    for (uint16_t stage = 0, half = 1;
         half < FFT_SAMPLES;
         ++stage, half <<= 1) {
        const uint16_t step = (uint16_t)(half << 1);
        const float step_real = s_fft_stage_cos[stage];
        const float step_imaginary = s_fft_stage_sin[stage];
        float twiddle_real = 1.0f;
        float twiddle_imaginary = 0.0f;

        for (uint16_t offset = 0; offset < half; ++offset) {
            for (uint16_t index = offset; index < FFT_SAMPLES;
                 index = (uint16_t)(index + step)) {
                const uint16_t first = (uint16_t)(index << 1);
                const uint16_t second =
                    (uint16_t)((index + half) << 1);
                const float first_real = spectrum[first];
                const float first_imaginary = spectrum[first + 1];
                const float second_real = spectrum[second];
                const float second_imaginary = spectrum[second + 1];
                const float value_real =
                    second_real * twiddle_real -
                    second_imaginary * twiddle_imaginary;
                const float value_imaginary =
                    second_real * twiddle_imaginary +
                    second_imaginary * twiddle_real;

                spectrum[first] = first_real + value_real;
                spectrum[first + 1] = first_imaginary + value_imaginary;
                spectrum[second] = first_real - value_real;
                spectrum[second + 1] =
                    first_imaginary - value_imaginary;
            }
            const float next_real =
                twiddle_real * step_real -
                twiddle_imaginary * step_imaginary;
            twiddle_imaginary =
                twiddle_real * step_imaginary +
                twiddle_imaginary * step_real;
            twiddle_real = next_real;
        }
    }
}

static void extract_features(
    face_viseme_state_t *state, float features[MFCC_COEFFICIENTS])
{
    hamming_fft(state);
    float *power = state->spectrum;
    for (uint16_t bin = 0; bin < FFT_SAMPLES / 2; ++bin) {
        const float real = state->spectrum[2U * bin];
        const float imaginary = state->spectrum[2U * bin + 1U];
        power[bin] =
            (real * real + imaginary * imaginary) / FFT_SAMPLES;
    }

    float mel_energies[MEL_BANDS];
    for (uint16_t band = 0; band < MEL_BANDS; ++band) {
        const uint16_t left = state->mel_bins[band];
        const uint16_t centre = state->mel_bins[band + 1];
        const uint16_t right = state->mel_bins[band + 2];
        float sum = 0.0f;
        if (centre > left) {
            for (uint16_t bin = left; bin < centre; ++bin) {
                const float weight =
                    (float)(bin - left) / (centre - left);
                sum += power[bin] * weight;
            }
        }
        if (right > centre) {
            for (uint16_t bin = centre; bin < right; ++bin) {
                const float weight =
                    (float)(right - bin) / (right - centre);
                sum += power[bin] * weight;
            }
        }
        mel_energies[band] = log10f(sum + 1e-10f);
    }

    for (uint16_t coefficient = 0;
         coefficient < MFCC_COEFFICIENTS; ++coefficient) {
        float sum = 0.0f;
        const float *row = s_dct + coefficient * MEL_BANDS;
        for (uint16_t band = 0; band < MEL_BANDS; ++band) {
            sum += row[band] * mel_energies[band];
        }
        features[coefficient] =
            tanhf(sum * s_lifter[coefficient]);
    }
}

static float mahalanobis_distance(
    const float vector[MFCC_COEFFICIENTS],
    const float *mean, const float *inverse_covariance)
{
    float difference[MFCC_COEFFICIENTS];
    for (uint16_t index = 0; index < MFCC_COEFFICIENTS; ++index) {
        difference[index] = vector[index] - mean[index];
    }

    float distance = 0.0f;
    size_t covariance_index = 0;
    for (uint16_t row = 0; row < MFCC_COEFFICIENTS; ++row) {
        for (uint16_t column = 0; column <= row; ++column) {
            const float coefficient =
                inverse_covariance[covariance_index++];
            const float term =
                coefficient * difference[row] * difference[column];
            distance += column == row ? term : 2.0f * term;
        }
    }
    return distance;
}

typedef struct {
    uint8_t prototype;
    uint8_t viseme;
    uint8_t confidence;
} raw_prediction_t;

static raw_prediction_t classify(
    const face_viseme_state_t *state,
    const float features[MFCC_COEFFICIENTS])
{
    float best_distance = INFINITY;
    float second_distance = INFINITY;
    uint8_t best_prototype = 0;
    const size_t count =
        face_viseme_model_record_count(state->config.model_size);
    for (size_t prototype = 0; prototype < count; ++prototype) {
        const uint8_t *record =
            state->config.model_data +
            prototype * FACE_VISEME_MODEL_RECORD_BYTES;
        const float *mean =
            (const float *)(record + MODEL_MEAN_OFFSET);
        const float *inverse_covariance =
            (const float *)(record + MODEL_COVARIANCE_OFFSET);
        float distance =
            mahalanobis_distance(features, mean, inverse_covariance);
        if (record[7] == FACE_VISEME_SIL) {
            distance =
                distance * 100.0f / SILENCE_SENSITIVITY_PERCENT;
        }
        if (distance <= best_distance) {
            second_distance = best_distance;
            best_distance = distance;
            best_prototype = (uint8_t)prototype;
        } else if (distance < second_distance) {
            second_distance = distance;
        }
    }

    float confidence = 0.0f;
    if (isfinite(second_distance)) {
        confidence =
            (second_distance - best_distance) /
            (fabsf(second_distance) + 1e-6f);
    }
    if (confidence < 0.0f) {
        confidence = 0.0f;
    } else if (confidence > 1.0f) {
        confidence = 1.0f;
    }
    return (raw_prediction_t){
        .prototype = best_prototype,
        .viseme = state->config.model_data[
            (size_t)best_prototype * FACE_VISEME_MODEL_RECORD_BYTES + 7],
        .confidence = (uint8_t)lroundf(confidence * UINT8_MAX),
    };
}

static void fill_votes(face_viseme_state_t *state, uint8_t viseme)
{
    for (uint8_t index = 0; index < state->config.vote_window; ++index) {
        state->vote_ring[index] = viseme;
    }
    state->vote_write = 0;
}

static uint8_t vote(face_viseme_state_t *state, uint8_t candidate)
{
    state->vote_ring[state->vote_write] = candidate;
    state->vote_write =
        (uint8_t)((state->vote_write + 1U) % state->config.vote_window);
    uint8_t counts[FACE_VISEME_COUNT] = {0};
    for (uint8_t index = 0; index < state->config.vote_window; ++index) {
        counts[state->vote_ring[index]] += 1;
    }
    uint8_t winner = 0;
    uint8_t maximum = 0;
    for (uint8_t viseme = 0; viseme < FACE_VISEME_COUNT; ++viseme) {
        if (counts[viseme] >= maximum) {
            maximum = counts[viseme];
            winner = viseme;
        }
    }
    return winner;
}

static void update_idle_motion(face_viseme_state_t *state)
{
    static const uint8_t blink[] = {176, 64, 0, 64, 176, 255};
    const uint32_t sample = state->total_samples;
    if (sample >= state->next_gaze_sample) {
        const uint32_t random =
            deterministic_random(sample ^ 0x5f356495U);
        state->pose.gaze_x =
            (int8_t)((int32_t)(random % 17U) - 8);
        state->pose.gaze_y =
            (int8_t)((int32_t)((random >> 8) % 11U) - 5);
        state->next_gaze_sample =
            sample + AUDIO_SAMPLE_RATE * 12 / 10 +
            deterministic_random(random) %
                (AUDIO_SAMPLE_RATE * 14 / 10 + 1U);
    }

    if (state->blink_phase != 0) {
        const size_t phase = (size_t)state->blink_phase - 1U;
        state->pose.eye_open = blink[phase];
        state->blink_phase += 1;
        if (state->blink_phase > sizeof(blink) / sizeof(blink[0])) {
            state->blink_phase = 0;
            state->next_blink_sample =
                sample + AUDIO_SAMPLE_RATE * 24 / 10 +
                deterministic_random(sample ^ 0x81e72f39U) %
                    (AUDIO_SAMPLE_RATE * 18 / 10 + 1U);
        }
    } else if (sample >= state->next_blink_sample) {
        state->blink_phase = 2;
        state->pose.eye_open = blink[0];
    } else {
        state->pose.eye_open = UINT8_MAX;
    }
}

static uint8_t speech_scale(
    const face_viseme_state_t *state, uint32_t level)
{
    if (level <= state->config.vad_close_level) {
        return 150;
    }
    uint32_t intensity =
        (level - state->config.vad_close_level) * UINT8_MAX /
        state->config.mouth_level_range;
    if (intensity > UINT8_MAX) {
        intensity = UINT8_MAX;
    }
    return (uint8_t)(160U + intensity * 95U / UINT8_MAX);
}

static void update_pose(
    face_viseme_state_t *state, uint32_t level,
    raw_prediction_t raw)
{
    const bool was_active = state->vad_active;
    if (state->vad_active) {
        if (level <= state->config.vad_close_level) {
            if (state->quiet_hops < UINT8_MAX) {
                state->quiet_hops += 1;
            }
            if (state->quiet_hops >= state->config.vad_release_hops) {
                state->vad_active = false;
                fill_votes(state, FACE_VISEME_SIL);
            }
        } else {
            state->quiet_hops = 0;
        }
    } else if (level >= state->config.vad_open_level) {
        state->vad_active = true;
        state->quiet_hops = 0;
        if (state->config.prime_vote_on_speech) {
            fill_votes(state, raw.viseme);
        }
    }

    uint8_t viseme = FACE_VISEME_SIL;
    if (state->vad_active) {
        viseme = vote(state, raw.viseme);
    }
    const viseme_shape_t *shape = &s_shapes[viseme];
    const uint8_t scale =
        viseme == FACE_VISEME_SIL ? 0 : speech_scale(state, level);
    const uint8_t target_open = scale_u8(shape->open, scale);
    state->pose.frame_index += 1;
    state->pose.level =
        (uint16_t)(level > UINT16_MAX ? UINT16_MAX : level);
    state->pose.mouth_open = smooth_u8(
        state->pose.mouth_open, target_open,
        target_open > state->pose.mouth_open
            ? state->attack_alpha
            : state->release_alpha);
    state->pose.mouth_width = smooth_u8(
        state->pose.mouth_width, shape->width, state->shape_alpha);
    state->pose.mouth_round = smooth_u8(
        state->pose.mouth_round, shape->round, state->shape_alpha);
    state->pose.mouth_press = smooth_u8(
        state->pose.mouth_press, shape->press, state->shape_alpha);
    state->pose.mouth_teeth = smooth_u8(
        state->pose.mouth_teeth, shape->teeth, state->shape_alpha);
    state->pose.viseme = viseme;
    state->pose.phoneme =
        state->vad_active ? raw.prototype : FACE_PHONEME_NONE;
    state->pose.confidence =
        state->vad_active ? raw.confidence : 0;
    state->pose.speaking =
        state->vad_active || state->pose.mouth_open > 2 ||
        state->pose.mouth_press > 8;
    if (state->pose.speaking) {
        state->pose.activity = FACE_ACTIVITY_SPEAKING;
    } else if (was_active || state->pose.activity == FACE_ACTIVITY_SPEAKING) {
        state->pose.activity = FACE_ACTIVITY_THINKING;
    }
    update_idle_motion(state);
}

static void finish_feature(face_viseme_state_t *state)
{
    float features[MFCC_COEFFICIENTS];
    extract_features(state, features);
    const raw_prediction_t prediction = classify(state, features);
    const uint32_t level =
        state->hop_samples == 0
            ? 0
            : state->hop_sum_abs / state->hop_samples;
    update_pose(state, level, prediction);
    state->hop_sum_abs = 0;
    state->hop_samples = 0;
    state->samples_since_feature = 0;
}

static float filter_native_16k(
    face_viseme_state_t *state, float sample)
{
    state->fir_ring[state->fir_write] = sample;
    state->fir_write =
        (uint8_t)((state->fir_write + 1U) % FIR_SAMPLES);
    float filtered = 0.0f;
    for (uint8_t index = 0; index < FIR_SAMPLES; ++index) {
        const uint8_t source =
            (uint8_t)((state->fir_write + index) % FIR_SAMPLES);
        filtered += state->fir_ring[source] * s_native_16k_fir[index];
    }
    return filtered;
}

static bool viseme_init(void *raw_state, uint32_t sample_rate,
                        const void *raw_config, size_t config_size)
{
    face_viseme_state_t *state = raw_state;
    if (sample_rate != AUDIO_SAMPLE_RATE ||
        (raw_config != NULL &&
         config_size != sizeof(face_viseme_config_t)) ||
        (raw_config == NULL && config_size != 0)) {
        return false;
    }
    const face_viseme_config_t *config =
        raw_config == NULL ? &FACE_VISEME_DEFAULT_CONFIG : raw_config;
    if (!face_viseme_model_valid(
            config->model_data, config->model_size) ||
        config->speaker_mean_hz == 0 ||
        config->vad_open_level <= config->vad_close_level ||
        config->mouth_level_range == 0 ||
        config->vad_release_hops == 0 ||
        config->vote_window == 0 ||
        config->vote_window > MAX_VOTE_WINDOW) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->config = *config;
    state->next_blink_sample = FIRST_BLINK_SAMPLES;
    state->next_gaze_sample = FIRST_GAZE_SAMPLES;
    state->pose.eye_open = UINT8_MAX;
    state->pose.viseme = FACE_VISEME_SIL;
    state->pose.phoneme = FACE_PHONEME_NONE;
    state->attack_alpha = smoothing_alpha(config->attack_ms);
    state->release_alpha = smoothing_alpha(config->release_ms);
    state->shape_alpha = smoothing_alpha(config->shape_ms);
    build_mel_bins(state);
    fill_votes(state, FACE_VISEME_SIL);
    return true;
}

static void viseme_push_pcm(
    void *raw_state, const int16_t *samples, size_t sample_count)
{
    face_viseme_state_t *state = raw_state;
    if (state == NULL || samples == NULL) {
        return;
    }
    publish_begin(state);
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t integer_sample = samples[index];
        const uint32_t magnitude =
            (uint32_t)(integer_sample < 0
                           ? -integer_sample
                           : integer_sample);
        const float original = integer_sample * FLOAT_PCM_SCALE;
        const float emphasised =
            original - PREEMPHASIS_ALPHA * state->preemphasis_previous;
        state->preemphasis_previous = original;
        const float filtered = filter_native_16k(state, emphasised);

        state->sample_ring[state->sample_write] = filtered;
        state->sample_write =
            (uint16_t)((state->sample_write + 1U) & (FFT_SAMPLES - 1));
        if (state->sample_count < FFT_SAMPLES) {
            state->sample_count += 1;
        }
        state->total_samples += 1;
        state->samples_since_feature += 1;
        state->hop_sum_abs += magnitude;
        state->hop_samples += 1;

        if (state->sample_count == FFT_SAMPLES &&
            state->samples_since_feature >=
                (state->pose.frame_index == 0
                     ? FFT_SAMPLES
                     : FEATURE_HOP)) {
            finish_feature(state);
        }
    }
    state->pose.playout_samples = state->total_samples;
    publish_end(state);
}

static void viseme_push_event(
    void *raw_state, const face_stream_event_t *event)
{
    face_viseme_state_t *state = raw_state;
    uint8_t activity;
    switch (event->type) {
    case FACE_STREAM_USER_SPEECH_STARTED:
        activity = FACE_ACTIVITY_LISTENING;
        break;
    case FACE_STREAM_USER_SPEECH_STOPPED:
    case FACE_STREAM_ASSISTANT_RESPONSE_STARTED:
        activity = FACE_ACTIVITY_THINKING;
        break;
    case FACE_STREAM_ASSISTANT_RESPONSE_DONE:
        activity = FACE_ACTIVITY_LISTENING;
        break;
    default:
        return;
    }
    if (!__atomic_load_n(&state->pose.speaking, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(
            &state->pose.activity, activity, __ATOMIC_RELEASE);
    }
}

static void viseme_snapshot(const void *raw_state, face_pose_t *pose)
{
    const face_viseme_state_t *state = raw_state;
    if (state == NULL || pose == NULL) {
        return;
    }
    for (;;) {
        const uint32_t before = __atomic_load_n(
            &state->published_sequence, __ATOMIC_ACQUIRE);
        if ((before & 1U) != 0) {
            continue;
        }
        memcpy(pose, &state->pose, sizeof(*pose));
        const uint32_t after = __atomic_load_n(
            &state->published_sequence, __ATOMIC_ACQUIRE);
        if (before == after) {
            return;
        }
    }
}

const face_algorithm_t FACE_ALGORITHM_VISEME = {
    .name = "viseme",
    .state_size = sizeof(face_viseme_state_t),
    .state_alignment = _Alignof(face_viseme_state_t),
    .init = viseme_init,
    .push_pcm = viseme_push_pcm,
    .push_event = viseme_push_event,
    .snapshot = viseme_snapshot,
};

#ifdef STACKCHAN_HOST_TEST
bool face_viseme_test_extract_features(
    const int16_t *samples, size_t sample_count,
    uint16_t speaker_mean_hz,
    float features[FACE_VISEME_FEATURE_COUNT])
{
    if (samples == NULL || sample_count != FFT_SAMPLES ||
        speaker_mean_hz == 0 || features == NULL) {
        return false;
    }
    face_viseme_state_t state;
    memset(&state, 0, sizeof(state));
    state.config.speaker_mean_hz = speaker_mean_hz;
    build_mel_bins(&state);
    for (size_t index = 0; index < sample_count; ++index) {
        const float original = samples[index] * FLOAT_PCM_SCALE;
        const float emphasised =
            original - PREEMPHASIS_ALPHA * state.preemphasis_previous;
        state.preemphasis_previous = original;
        state.sample_ring[state.sample_write] =
            filter_native_16k(&state, emphasised);
        state.sample_write =
            (uint16_t)((state.sample_write + 1U) & (FFT_SAMPLES - 1));
    }
    extract_features(&state, features);
    return true;
}
#endif
