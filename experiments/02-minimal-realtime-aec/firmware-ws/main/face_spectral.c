#include "face_spectral.h"

#include <limits.h>
#include <string.h>

enum {
    ANALYSIS_WINDOW_SAMPLES = 320,
    COEFFICIENT_SCALE = 16384,
    TRIANGLE_PEAK_INDEX = ANALYSIS_WINDOW_SAMPLES / 2 - 1,
    FIRST_BLINK_FRAME = 110,
    FIRST_GAZE_FRAME = 35,
};

/*
 * 2*cos(2*pi*f/16000) in Q14 for:
 * 350, 550, 800, 1100, 1600, 2400, and 3200 Hz.
 */
static const int16_t BAND_COEFFICIENTS[FACE_SPECTRAL_BAND_COUNT] = {
    32459, 32007, 31164, 29758, 26510, 19261, 10126,
};

typedef struct {
    uint8_t open_scale;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
} mouth_shape_t;

static const mouth_shape_t VOWEL_SHAPES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {255, 185, 20, 0, 20},
    [FACE_VISEME_E] = {220, 245, 5, 0, 105},
    [FACE_VISEME_I] = {175, 255, 0, 0, 125},
    [FACE_VISEME_O] = {235, 120, 230, 0, 15},
    [FACE_VISEME_U] = {185, 70, 255, 0, 5},
    [FACE_VISEME_SS] = {115, 240, 5, 0, 190},
    [FACE_VISEME_SIL] = {0, 0, 0, 0, 0},
};

const face_spectral_config_t FACE_SPECTRAL_DEFAULT_CONFIG = {
    .speech_floor = 220,
    .mouth_dynamic_range = 4400,
    .attack_percent = 72,
    .release_percent = 22,
    .shape_percent = 58,
    .fricative_percent = 34,
};

static uint32_t deterministic_random(uint32_t value)
{
    value += 0x9e3779b9U;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void publish_begin(face_spectral_state_t *state)
{
    (void)__atomic_fetch_add(
        &state->published_sequence, 1U, __ATOMIC_ACQ_REL);
}

static void publish_end(face_spectral_state_t *state)
{
    (void)__atomic_fetch_add(
        &state->published_sequence, 1U, __ATOMIC_RELEASE);
}

static uint8_t smooth_value(uint8_t current, uint8_t target,
                            uint8_t percent)
{
    if (target > current) {
        const uint32_t delta = (uint32_t)target - current;
        const uint32_t movement =
            (delta * percent + 99U) / 100U;
        return (uint8_t)(current + movement);
    }
    if (target < current) {
        const uint32_t delta = (uint32_t)current - target;
        const uint32_t movement =
            (delta * percent + 99U) / 100U;
        return (uint8_t)(current - movement);
    }
    return current;
}

static uint8_t mouth_open_for_level(
    const face_spectral_state_t *state, uint32_t level)
{
    if (level <= state->config.speech_floor) {
        return 0;
    }
    const uint32_t scaled =
        ((level - state->config.speech_floor) * UINT8_MAX) /
        state->config.mouth_dynamic_range;
    return (uint8_t)(scaled > UINT8_MAX ? UINT8_MAX : scaled);
}

static uint64_t goertzel_energy(
    int32_t recurrence_1, int32_t recurrence_2, int16_t coefficient)
{
    const int64_t first = recurrence_1;
    const int64_t second = recurrence_2;
    const int64_t cross =
        ((int64_t)coefficient * first * second) /
        COEFFICIENT_SCALE;
    const int64_t energy =
        first * first + second * second - cross;
    return energy > 0 ? (uint64_t)energy : 0;
}

static uint8_t ratio_u8(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }
    const uint64_t ratio = numerator * UINT8_MAX / denominator;
    return (uint8_t)(ratio > UINT8_MAX ? UINT8_MAX : ratio);
}

static uint8_t strongest_band(const uint64_t *energies,
                              size_t first, size_t count,
                              uint64_t *group_sum,
                              uint64_t *winner_energy)
{
    uint8_t winner = 0;
    uint64_t strongest = 0;
    uint64_t sum = 0;
    for (size_t index = 0; index < count; ++index) {
        const uint64_t energy = energies[first + index];
        sum += energy;
        if (energy > strongest) {
            strongest = energy;
            winner = (uint8_t)index;
        }
    }
    *group_sum = sum;
    *winner_energy = strongest;
    return winner;
}

static uint8_t classify_shape(
    const face_spectral_state_t *state,
    const uint64_t energies[FACE_SPECTRAL_BAND_COUNT],
    uint8_t *confidence)
{
    uint64_t total = 0;
    for (size_t index = 0; index < FACE_SPECTRAL_BAND_COUNT; ++index) {
        total += energies[index];
    }
    if (total == 0) {
        *confidence = 0;
        return FACE_VISEME_SIL;
    }

    const uint64_t fricative = energies[FACE_SPECTRAL_BAND_COUNT - 1];
    if (fricative * 100U >=
        total * state->config.fricative_percent) {
        *confidence = ratio_u8(fricative, total);
        return FACE_VISEME_SS;
    }

    uint64_t low_sum;
    uint64_t low_winner;
    uint64_t high_sum;
    uint64_t high_winner;
    const uint8_t first_formant =
        strongest_band(energies, 0, 3, &low_sum, &low_winner);
    const uint8_t second_formant =
        strongest_band(energies, 3, 3, &high_sum, &high_winner);
    const uint8_t low_confidence = ratio_u8(low_winner, low_sum);
    const uint8_t high_confidence = ratio_u8(high_winner, high_sum);
    *confidence =
        low_confidence < high_confidence
            ? low_confidence
            : high_confidence;

    if (first_formant == 2) {
        return FACE_VISEME_AA;
    }
    if (first_formant == 1) {
        return second_formant == 0
                   ? FACE_VISEME_O
                   : FACE_VISEME_E;
    }
    return second_formant == 0
               ? FACE_VISEME_U
               : FACE_VISEME_I;
}

static void update_idle_motion(face_spectral_state_t *state)
{
    static const uint8_t blink[] = {176, 64, 0, 64, 176, 255};
    const uint32_t frame_index = state->pose.frame_index;

    if (frame_index >= state->next_gaze_frame) {
        const uint32_t value =
            deterministic_random(frame_index ^ 0x5f356495U);
        state->pose.gaze_x =
            (int8_t)((int32_t)(value % 17U) - 8);
        state->pose.gaze_y =
            (int8_t)((int32_t)((value >> 8) % 11U) - 5);
        state->next_gaze_frame =
            frame_index + 60U + deterministic_random(value) % 71U;
    }

    if (state->blink_phase != 0) {
        const size_t phase = (size_t)state->blink_phase - 1U;
        state->pose.eye_open = blink[phase];
        state->blink_phase += 1;
        if (state->blink_phase > sizeof(blink) / sizeof(blink[0])) {
            state->blink_phase = 0;
            state->next_blink_frame =
                frame_index + 120U +
                deterministic_random(frame_index ^ 0x81e72f39U) % 91U;
        }
    } else if (frame_index >= state->next_blink_frame) {
        state->blink_phase = 2;
        state->pose.eye_open = blink[0];
    } else {
        state->pose.eye_open = UINT8_MAX;
    }
}

static void finish_window(face_spectral_state_t *state)
{
    uint64_t energies[FACE_SPECTRAL_BAND_COUNT];
    for (size_t band = 0; band < FACE_SPECTRAL_BAND_COUNT; ++band) {
        energies[band] = goertzel_energy(
            state->recurrence_1[band],
            state->recurrence_2[band],
            BAND_COEFFICIENTS[band]);
        state->recurrence_1[band] = 0;
        state->recurrence_2[band] = 0;
    }

    const uint32_t level =
        state->sum_abs / ANALYSIS_WINDOW_SAMPLES;
    const uint8_t level_open = mouth_open_for_level(state, level);
    uint8_t confidence = 0;
    const uint8_t viseme =
        level_open == 0
            ? FACE_VISEME_SIL
            : classify_shape(state, energies, &confidence);
    const mouth_shape_t shape = VOWEL_SHAPES[viseme];
    const uint8_t target_open =
        (uint8_t)((uint32_t)level_open * shape.open_scale / UINT8_MAX);
    const uint8_t mouth_percent =
        target_open > state->pose.mouth_open
            ? state->config.attack_percent
            : state->config.release_percent;

    state->pose.frame_index += 1;
    state->pose.level =
        (uint16_t)(level > UINT16_MAX ? UINT16_MAX : level);
    state->pose.mouth_open = smooth_value(
        state->pose.mouth_open, target_open, mouth_percent);
    state->pose.mouth_width = smooth_value(
        state->pose.mouth_width, shape.width,
        state->config.shape_percent);
    state->pose.mouth_round = smooth_value(
        state->pose.mouth_round, shape.round,
        state->config.shape_percent);
    state->pose.mouth_press = smooth_value(
        state->pose.mouth_press, shape.press,
        state->config.shape_percent);
    state->pose.mouth_teeth = smooth_value(
        state->pose.mouth_teeth, shape.teeth,
        state->config.shape_percent);
    state->pose.viseme = viseme;
    state->pose.phoneme = FACE_PHONEME_NONE;
    state->pose.confidence = confidence;
    state->pose.speaking =
        level_open != 0 || state->pose.mouth_open != 0;
    if (level_open != 0) {
        state->pose.activity = FACE_ACTIVITY_SPEAKING;
    } else if (state->pose.activity == FACE_ACTIVITY_SPEAKING) {
        state->pose.activity = FACE_ACTIVITY_THINKING;
    }
    update_idle_motion(state);
    state->window_fill = 0;
    state->sum_abs = 0;
}

static bool spectral_init(
    void *raw_state, uint32_t sample_rate,
    const void *raw_config, size_t config_size)
{
    if (raw_state == NULL || sample_rate != FACE_SPECTRAL_SAMPLE_RATE ||
        (raw_config == NULL && config_size != 0) ||
        (raw_config != NULL &&
         config_size != sizeof(face_spectral_config_t))) {
        return false;
    }
    face_spectral_state_t *state = raw_state;
    const face_spectral_config_t *config =
        raw_config == NULL ? &FACE_SPECTRAL_DEFAULT_CONFIG : raw_config;
    if (config->mouth_dynamic_range == 0 ||
        config->attack_percent == 0 ||
        config->attack_percent > 100 ||
        config->release_percent == 0 ||
        config->release_percent > 100 ||
        config->shape_percent == 0 ||
        config->shape_percent > 100 ||
        config->fricative_percent == 0 ||
        config->fricative_percent > 100) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->config = *config;
    state->next_blink_frame = FIRST_BLINK_FRAME;
    state->next_gaze_frame = FIRST_GAZE_FRAME;
    state->pose.eye_open = UINT8_MAX;
    state->pose.viseme = FACE_VISEME_SIL;
    state->pose.phoneme = FACE_PHONEME_NONE;
    return true;
}

static void spectral_push_pcm(
    void *raw_state, const int16_t *samples, size_t sample_count)
{
    face_spectral_state_t *state = raw_state;
    if (state == NULL || samples == NULL) {
        return;
    }

    publish_begin(state);
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude =
            (uint32_t)(sample < 0 ? -sample : sample);
        const uint32_t triangle_index =
            state->window_fill <= TRIANGLE_PEAK_INDEX
                ? state->window_fill
                : ANALYSIS_WINDOW_SAMPLES - 1U - state->window_fill;
        const int32_t window_weight =
            (int32_t)(triangle_index * UINT8_MAX /
                      TRIANGLE_PEAK_INDEX);
        const int32_t windowed =
            (sample / 16) * window_weight / UINT8_MAX;

        state->sum_abs += magnitude;
        for (size_t band = 0; band < FACE_SPECTRAL_BAND_COUNT; ++band) {
            const int32_t recurrence =
                windowed +
                (int32_t)(
                    (int64_t)BAND_COEFFICIENTS[band] *
                    state->recurrence_1[band] /
                    COEFFICIENT_SCALE) -
                state->recurrence_2[band];
            state->recurrence_2[band] =
                state->recurrence_1[band];
            state->recurrence_1[band] = recurrence;
        }
        state->window_fill += 1;
        state->pose.playout_samples += 1;
        if (state->window_fill == ANALYSIS_WINDOW_SAMPLES) {
            finish_window(state);
        }
    }
    publish_end(state);
}

static void spectral_push_event(
    void *raw_state, const face_stream_event_t *event)
{
    face_spectral_state_t *state = raw_state;
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
    __atomic_store_n(
        &state->pose.activity, activity, __ATOMIC_RELEASE);
}

static void spectral_snapshot(
    const void *raw_state, face_pose_t *pose)
{
    const face_spectral_state_t *state = raw_state;
    if (state == NULL || pose == NULL) {
        return;
    }
    for (;;) {
        const uint32_t before = __atomic_load_n(
            &state->published_sequence, __ATOMIC_ACQUIRE);
        if ((before & 1U) != 0) {
            continue;
        }
        pose->frame_index = __atomic_load_n(
            &state->pose.frame_index, __ATOMIC_RELAXED);
        pose->playout_samples = __atomic_load_n(
            &state->pose.playout_samples, __ATOMIC_RELAXED);
        pose->level = __atomic_load_n(
            &state->pose.level, __ATOMIC_RELAXED);
        pose->mouth_open = __atomic_load_n(
            &state->pose.mouth_open, __ATOMIC_RELAXED);
        pose->mouth_width = __atomic_load_n(
            &state->pose.mouth_width, __ATOMIC_RELAXED);
        pose->mouth_round = __atomic_load_n(
            &state->pose.mouth_round, __ATOMIC_RELAXED);
        pose->mouth_press = __atomic_load_n(
            &state->pose.mouth_press, __ATOMIC_RELAXED);
        pose->mouth_teeth = __atomic_load_n(
            &state->pose.mouth_teeth, __ATOMIC_RELAXED);
        pose->eye_open = __atomic_load_n(
            &state->pose.eye_open, __ATOMIC_RELAXED);
        pose->gaze_x = __atomic_load_n(
            &state->pose.gaze_x, __ATOMIC_RELAXED);
        pose->gaze_y = __atomic_load_n(
            &state->pose.gaze_y, __ATOMIC_RELAXED);
        pose->viseme = __atomic_load_n(
            &state->pose.viseme, __ATOMIC_RELAXED);
        pose->phoneme = __atomic_load_n(
            &state->pose.phoneme, __ATOMIC_RELAXED);
        pose->confidence = __atomic_load_n(
            &state->pose.confidence, __ATOMIC_RELAXED);
        pose->activity = __atomic_load_n(
            &state->pose.activity, __ATOMIC_RELAXED);
        pose->speaking = __atomic_load_n(
            &state->pose.speaking, __ATOMIC_RELAXED);
        const uint32_t after = __atomic_load_n(
            &state->published_sequence, __ATOMIC_ACQUIRE);
        if (before == after) {
            return;
        }
    }
}

const face_algorithm_t FACE_ALGORITHM_SPECTRAL = {
    .name = "spectral",
    .state_size = sizeof(face_spectral_state_t),
    .state_alignment = _Alignof(face_spectral_state_t),
    .init = spectral_init,
    .push_pcm = spectral_push_pcm,
    .push_event = spectral_push_event,
    .snapshot = spectral_snapshot,
};

