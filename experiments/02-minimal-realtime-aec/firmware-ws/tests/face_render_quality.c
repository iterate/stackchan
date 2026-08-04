#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_render.h"
#include "face_stage.h"

/*
 * Deterministic, device-free visual quality probe for the production renderer.
 *
 * This is intentionally not a golden-image test: it measures whether stage
 * directions are visibly separable and whether smooth input produces large
 * temporal jumps.  It also writes reviewable PPM contact sheets.  The Python
 * front-end in tools/run_face_render_quality.py compiles this file, converts
 * the PPMs to deterministic PNGs, and builds an HTML report.
 */

enum {
    SAMPLE_RATE = 16000,
    FRAMES_PER_SECOND = 30,
    MOTION_FRAME_COUNT = 180,
    MOTION_PEAK_FRAME_COUNT = 12,
    EXPRESSION_SHEET_COLUMNS = 4,
    EXPRESSION_SHEET_ROWS = 3,
    MOTION_SHEET_COLUMNS = 6,
    MOTION_SHEET_ROWS = 2,
    LABEL_HEIGHT = 10,
    ATLAS_SCALE = 2,
    ATLAS_PROFILE_LABEL_WIDTH = 16,
    PATH_BUFFER_BYTES = 1024,
};

_Static_assert(
    FACE_EXPRESSION_COUNT == 11,
    "quality report layout must be updated for new stage expressions");

typedef struct {
    double mean_delta;
    double roi_mean_delta;
    double changed_fraction;
    double roi_changed_fraction;
    double edge_changed_fraction;
} frame_difference_t;

typedef struct {
    size_t profile_index;
    uint32_t distinct_expression_hashes;
    uint32_t identical_expression_pairs;
    uint32_t weak_expression_pairs;
    uint32_t clear_nonneutral_expressions;
    double minimum_pair_roi_delta;
    double median_pair_roi_delta;
    double mean_pair_roi_delta;
    double maximum_pair_roi_delta;
    double minimum_nonneutral_roi_delta;
    double mean_nonneutral_roi_delta;
    double maximum_expression_edge_change;
    double expression_score;
    double neutral_roi_delta[FACE_EXPRESSION_COUNT];
    double neutral_changed_fraction[FACE_EXPRESSION_COUNT];
    double motion_median_delta;
    double motion_p95_delta;
    double motion_maximum_delta;
    double motion_maximum_changed_fraction;
    double motion_jump_threshold;
    double motion_jump_ratio;
    double motion_score;
    uint32_t motion_abrupt_jumps;
    uint16_t motion_abrupt_frames[MOTION_FRAME_COUNT];
    uint32_t motion_frozen_pairs;
    uint32_t motion_peak_frame;
    const char *expression_status;
    const char *motion_status;
} profile_metrics_t;

static uint16_t
    expression_frames[FACE_EXPRESSION_COUNT][FACE_RENDER_PIXEL_COUNT];
static uint16_t
    motion_peak_frames[MOTION_PEAK_FRAME_COUNT][FACE_RENDER_PIXEL_COUNT];
static uint16_t previous_frame[FACE_RENDER_PIXEL_COUNT];
static uint16_t current_frame[FACE_RENDER_PIXEL_COUNT];

static const char *const EXPRESSION_NAMES[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = "neutral",
    [FACE_EXPRESSION_WARM] = "warm",
    [FACE_EXPRESSION_JOY] = "joy",
    [FACE_EXPRESSION_CONCERN] = "concern",
    [FACE_EXPRESSION_SURPRISE] = "surprise",
    [FACE_EXPRESSION_THOUGHTFUL] = "thoughtful",
    [FACE_EXPRESSION_SKEPTICAL] = "skeptical",
    [FACE_EXPRESSION_DETERMINED] = "determined",
    [FACE_EXPRESSION_SLEEPY] = "sleepy",
    [FACE_EXPRESSION_EXCITED] = "excited",
    [FACE_EXPRESSION_EMBARRASSED] = "embarrassed",
};

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < FACE_RENDER_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static uint8_t rgb565_red(uint16_t pixel)
{
    const uint8_t value = (uint8_t)((pixel >> 11U) & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t rgb565_green(uint16_t pixel)
{
    const uint8_t value = (uint8_t)((pixel >> 5U) & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static uint8_t rgb565_blue(uint16_t pixel)
{
    const uint8_t value = (uint8_t)(pixel & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint32_t absolute_difference_u8(uint8_t first, uint8_t second)
{
    return first > second
               ? (uint32_t)first - second
               : (uint32_t)second - first;
}

static bool pixel_in_roi(size_t index)
{
    const size_t x = index % FACE_RENDER_WIDTH;
    const size_t y = index / FACE_RENDER_WIDTH;
    return x >= 16U && x < FACE_RENDER_WIDTH - 16U &&
           y >= 10U && y < FACE_RENDER_HEIGHT - 10U;
}

static bool pixel_in_edge(size_t index)
{
    const size_t x = index % FACE_RENDER_WIDTH;
    const size_t y = index / FACE_RENDER_WIDTH;
    return x < 4U || x >= FACE_RENDER_WIDTH - 4U ||
           y < 4U || y >= FACE_RENDER_HEIGHT - 4U;
}

static frame_difference_t compare_frames(
    const uint16_t *first, const uint16_t *second)
{
    uint64_t channel_delta = 0U;
    uint64_t roi_channel_delta = 0U;
    size_t changed = 0U;
    size_t roi_changed = 0U;
    size_t edge_changed = 0U;
    size_t roi_pixels = 0U;
    size_t edge_pixels = 0U;

    for (size_t index = 0; index < FACE_RENDER_PIXEL_COUNT; ++index) {
        const uint16_t first_pixel = first[index];
        const uint16_t second_pixel = second[index];
        const uint32_t pixel_delta =
            absolute_difference_u8(
                rgb565_red(first_pixel), rgb565_red(second_pixel)) +
            absolute_difference_u8(
                rgb565_green(first_pixel), rgb565_green(second_pixel)) +
            absolute_difference_u8(
                rgb565_blue(first_pixel), rgb565_blue(second_pixel));
        channel_delta += pixel_delta;
        changed += first_pixel != second_pixel;

        if (pixel_in_roi(index)) {
            roi_channel_delta += pixel_delta;
            roi_changed += first_pixel != second_pixel;
            ++roi_pixels;
        }
        if (pixel_in_edge(index)) {
            edge_changed += first_pixel != second_pixel;
            ++edge_pixels;
        }
    }

    const double channel_denominator =
        (double)FACE_RENDER_PIXEL_COUNT * 3.0 * 255.0;
    const double roi_channel_denominator =
        (double)roi_pixels * 3.0 * 255.0;
    frame_difference_t difference = {
        .mean_delta = (double)channel_delta / channel_denominator,
        .roi_mean_delta =
            (double)roi_channel_delta / roi_channel_denominator,
        .changed_fraction =
            (double)changed / (double)FACE_RENDER_PIXEL_COUNT,
        .roi_changed_fraction =
            (double)roi_changed / (double)roi_pixels,
        .edge_changed_fraction =
            (double)edge_changed / (double)edge_pixels,
    };
    return difference;
}

static int compare_double(const void *left, const void *right)
{
    const double first = *(const double *)left;
    const double second = *(const double *)right;
    return first < second ? -1 : first > second ? 1 : 0;
}

static double clamp_double(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static face_render_key_t base_render_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 152U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 76U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = 0;
    key.controls.look_y = 0;
    key.controls.brow = 0;
    key.controls.expression = FACE_EXPRESSION_NEUTRAL;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_sample = 0U;
    cue.attack_samples = 0U;
    cue.hold_samples = 0U;
    cue.release_samples = 0U;
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gesture = FACE_GESTURE_NONE;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static bool render_expression_frames(face_render_profile_t profile)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        face_render_key_t key = base_render_key();
        const face_stage_cue_t cue =
            expression_cue((face_expression_t)expression);
        if (!face_stage_cue_apply(&cue, fixed_clock, &key) ||
            !face_render_frame(
                profile, &key, fixed_clock,
                expression_frames[expression],
                FACE_RENDER_PIXEL_COUNT)) {
            return false;
        }
    }
    return true;
}

static void evaluate_expression_metrics(profile_metrics_t *metrics)
{
    enum {
        PAIR_COUNT =
            FACE_EXPRESSION_COUNT * (FACE_EXPRESSION_COUNT - 1) / 2,
    };
    double pair_roi_deltas[PAIR_COUNT];
    uint32_t hashes[FACE_EXPRESSION_COUNT];
    size_t pair_index = 0U;
    double pair_sum = 0.0;
    double edge_change_maximum = 0.0;
    double nonneutral_sum = 0.0;
    double nonneutral_minimum = 1.0;
    uint32_t clear_nonneutral = 0U;
    uint32_t weak_pairs = 0U;
    uint32_t identical_pairs = 0U;

    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        hashes[expression] = frame_hash(expression_frames[expression]);
        const frame_difference_t neutral_difference = compare_frames(
            expression_frames[FACE_EXPRESSION_NEUTRAL],
            expression_frames[expression]);
        metrics->neutral_roi_delta[expression] =
            neutral_difference.roi_mean_delta;
        metrics->neutral_changed_fraction[expression] =
            neutral_difference.roi_changed_fraction;
        if (expression != FACE_EXPRESSION_NEUTRAL) {
            nonneutral_sum += neutral_difference.roi_mean_delta;
            if (neutral_difference.roi_mean_delta < nonneutral_minimum) {
                nonneutral_minimum = neutral_difference.roi_mean_delta;
            }
            if (neutral_difference.roi_mean_delta >= 0.008 &&
                neutral_difference.roi_changed_fraction >= 0.018) {
                ++clear_nonneutral;
            }
            if (neutral_difference.edge_changed_fraction >
                edge_change_maximum) {
                edge_change_maximum =
                    neutral_difference.edge_changed_fraction;
            }
        }
    }

    for (size_t first = 0U; first < FACE_EXPRESSION_COUNT; ++first) {
        for (size_t second = first + 1U;
             second < FACE_EXPRESSION_COUNT;
             ++second) {
            const frame_difference_t difference = compare_frames(
                expression_frames[first], expression_frames[second]);
            pair_roi_deltas[pair_index++] = difference.roi_mean_delta;
            pair_sum += difference.roi_mean_delta;
            identical_pairs += hashes[first] == hashes[second];
            if (difference.roi_mean_delta < 0.006 ||
                difference.roi_changed_fraction < 0.015) {
                ++weak_pairs;
            }
        }
    }
    qsort(
        pair_roi_deltas, PAIR_COUNT,
        sizeof(pair_roi_deltas[0]), compare_double);

    uint32_t distinct_hashes = 0U;
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        bool first_occurrence = true;
        for (size_t earlier = 0U; earlier < expression; ++earlier) {
            if (hashes[earlier] == hashes[expression]) {
                first_occurrence = false;
                break;
            }
        }
        distinct_hashes += first_occurrence;
    }

    metrics->distinct_expression_hashes = distinct_hashes;
    metrics->identical_expression_pairs = identical_pairs;
    metrics->weak_expression_pairs = weak_pairs;
    metrics->clear_nonneutral_expressions = clear_nonneutral;
    metrics->minimum_pair_roi_delta = pair_roi_deltas[0];
    metrics->median_pair_roi_delta = pair_roi_deltas[PAIR_COUNT / 2U];
    metrics->mean_pair_roi_delta = pair_sum / (double)PAIR_COUNT;
    metrics->maximum_pair_roi_delta = pair_roi_deltas[PAIR_COUNT - 1U];
    metrics->minimum_nonneutral_roi_delta = nonneutral_minimum;
    metrics->mean_nonneutral_roi_delta =
        nonneutral_sum / (double)(FACE_EXPRESSION_COUNT - 1U);
    metrics->maximum_expression_edge_change = edge_change_maximum;

    const double clear_component =
        (double)clear_nonneutral /
        (double)(FACE_EXPRESSION_COUNT - 1U);
    const double distance_component =
        clamp_double(metrics->mean_pair_roi_delta / 0.035, 0.0, 1.0);
    const double duplicate_component =
        (double)distinct_hashes / (double)FACE_EXPRESSION_COUNT;
    metrics->expression_score =
        100.0 * (0.55 * clear_component +
                 0.30 * distance_component +
                 0.15 * duplicate_component);

    if (distinct_hashes < 6U || clear_nonneutral < 5U) {
        metrics->expression_status = "fail";
    } else if (distinct_hashes < 9U || clear_nonneutral < 8U ||
               metrics->mean_pair_roi_delta < 0.010) {
        metrics->expression_status = "warn";
    } else {
        metrics->expression_status = "pass";
    }
}

static uint8_t triangle_u8(uint32_t frame, uint32_t period)
{
    const uint32_t phase = frame % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

static face_render_key_t motion_render_key(uint32_t frame)
{
    face_render_key_t key = base_render_key();
    const uint8_t jaw_wave = triangle_u8(frame + 7U, 42U);
    const uint8_t form_wave = triangle_u8(frame + 19U, 74U);
    const uint8_t gaze_wave = triangle_u8(frame + 11U, 120U);
    key.controls.mouth_open =
        (uint8_t)(24U + (uint16_t)jaw_wave * 204U / 255U);
    key.controls.mouth_width =
        (uint8_t)(104U + (uint16_t)form_wave * 104U / 255U);
    key.controls.mouth_round =
        (uint8_t)(218U - (uint16_t)form_wave * 174U / 255U);
    key.controls.mouth_teeth =
        (uint8_t)(24U + (uint16_t)jaw_wave * 104U / 255U);
    key.controls.look_x = (int8_t)((int32_t)gaze_wave / 3 - 42);
    key.controls.look_y =
        (int8_t)((int32_t)triangle_u8(frame + 37U, 156U) / 6 - 21);
    key.audio_level =
        (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_blend = form_wave;
    key.head_roll =
        (int8_t)((int32_t)triangle_u8(frame + 13U, 180U) / 12 - 10);
    return key;
}

static face_stage_cue_t motion_stage_cue(void)
{
    face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_JOY);
    cue.start_sample = SAMPLE_RATE;
    cue.attack_samples = SAMPLE_RATE;
    cue.hold_samples = SAMPLE_RATE * 2U;
    cue.release_samples = SAMPLE_RATE;
    cue.flags = 0U;
    cue.gesture = FACE_GESTURE_NOD;
    cue.intensity = 238U;
    return cue;
}

static bool render_motion_frame(
    face_render_profile_t profile,
    uint32_t frame,
    uint16_t *pixels)
{
    const uint32_t sample_clock =
        (uint32_t)((uint64_t)frame * SAMPLE_RATE / FRAMES_PER_SECOND);
    face_render_key_t key = motion_render_key(frame);
    const face_stage_cue_t cue = motion_stage_cue();
    (void)face_stage_cue_apply(&cue, sample_clock, &key);
    return face_render_frame(
        profile, &key, sample_clock,
        pixels, FACE_RENDER_PIXEL_COUNT);
}

static bool evaluate_motion_metrics(
    face_render_profile_t profile, profile_metrics_t *metrics)
{
    enum {
        TRANSITION_COUNT = MOTION_FRAME_COUNT - 1,
    };
    double deltas[TRANSITION_COUNT];
    double sorted_deltas[TRANSITION_COUNT];
    double changed_fractions[TRANSITION_COUNT];
    uint32_t frozen_pairs = 0U;
    uint32_t peak_frame = 1U;
    double maximum_delta = 0.0;
    double maximum_changed_fraction = 0.0;

    if (!render_motion_frame(profile, 0U, previous_frame)) {
        return false;
    }
    for (uint32_t frame = 1U; frame < MOTION_FRAME_COUNT; ++frame) {
        if (!render_motion_frame(profile, frame, current_frame)) {
            return false;
        }
        const frame_difference_t difference =
            compare_frames(previous_frame, current_frame);
        deltas[frame - 1U] = difference.roi_mean_delta;
        changed_fractions[frame - 1U] =
            difference.roi_changed_fraction;
        frozen_pairs += difference.roi_changed_fraction == 0.0;
        if (difference.roi_mean_delta > maximum_delta) {
            maximum_delta = difference.roi_mean_delta;
            peak_frame = frame;
        }
        if (difference.roi_changed_fraction >
            maximum_changed_fraction) {
            maximum_changed_fraction =
                difference.roi_changed_fraction;
        }
        memcpy(
            previous_frame, current_frame,
            sizeof(previous_frame));
    }

    memcpy(sorted_deltas, deltas, sizeof(deltas));
    qsort(
        sorted_deltas, TRANSITION_COUNT,
        sizeof(sorted_deltas[0]), compare_double);
    const double median = sorted_deltas[TRANSITION_COUNT / 2U];
    const size_t p95_index =
        (TRANSITION_COUNT * 95U + 99U) / 100U - 1U;
    const double p95 = sorted_deltas[p95_index];
    double jump_threshold = median * 7.0 + 0.008;
    if (jump_threshold < 0.035) {
        jump_threshold = 0.035;
    }
    uint32_t abrupt_jumps = 0U;
    for (size_t index = 0U; index < TRANSITION_COUNT; ++index) {
        if (deltas[index] > jump_threshold &&
            changed_fractions[index] >= 0.08) {
            metrics->motion_abrupt_frames[abrupt_jumps] =
                (uint16_t)(index + 1U);
            ++abrupt_jumps;
        }
    }

    metrics->motion_median_delta = median;
    metrics->motion_p95_delta = p95;
    metrics->motion_maximum_delta = maximum_delta;
    metrics->motion_maximum_changed_fraction =
        maximum_changed_fraction;
    metrics->motion_jump_threshold = jump_threshold;
    metrics->motion_jump_ratio =
        maximum_delta / (p95 > 0.001 ? p95 : 0.001);
    metrics->motion_abrupt_jumps = abrupt_jumps;
    metrics->motion_frozen_pairs = frozen_pairs;
    metrics->motion_peak_frame = peak_frame;

    const double jump_penalty =
        clamp_double((double)abrupt_jumps / 6.0, 0.0, 1.0);
    const double peak_penalty =
        clamp_double((maximum_delta - 0.06) / 0.18, 0.0, 1.0);
    const double frozen_penalty =
        clamp_double((double)frozen_pairs / 45.0, 0.0, 1.0);
    metrics->motion_score =
        100.0 *
        (1.0 - 0.50 * jump_penalty -
         0.35 * peak_penalty - 0.15 * frozen_penalty);

    if (abrupt_jumps >= 4U || maximum_delta >= 0.24) {
        metrics->motion_status = "fail";
    } else if (abrupt_jumps > 0U || maximum_delta >= 0.14 ||
               frozen_pairs >= 30U) {
        metrics->motion_status = "warn";
    } else {
        metrics->motion_status = "pass";
    }
    return true;
}

static bool write_rgb(FILE *file, uint16_t pixel)
{
    const uint8_t rgb[3] = {
        rgb565_red(pixel),
        rgb565_green(pixel),
        rgb565_blue(pixel),
    };
    return fwrite(rgb, sizeof(rgb), 1U, file) == 1U;
}

static bool write_ppm_header(FILE *file, size_t width, size_t height)
{
    return fprintf(file, "P6\n%zu %zu\n255\n", width, height) > 0;
}

static bool digit_pixel(uint8_t digit, size_t x, size_t y)
{
    static const uint8_t DIGITS[10][5] = {
        {0x7U, 0x5U, 0x5U, 0x5U, 0x7U},
        {0x2U, 0x6U, 0x2U, 0x2U, 0x7U},
        {0x7U, 0x1U, 0x7U, 0x4U, 0x7U},
        {0x7U, 0x1U, 0x7U, 0x1U, 0x7U},
        {0x5U, 0x5U, 0x7U, 0x1U, 0x1U},
        {0x7U, 0x4U, 0x7U, 0x1U, 0x7U},
        {0x7U, 0x4U, 0x7U, 0x5U, 0x7U},
        {0x7U, 0x1U, 0x1U, 0x1U, 0x1U},
        {0x7U, 0x5U, 0x7U, 0x5U, 0x7U},
        {0x7U, 0x5U, 0x7U, 0x1U, 0x7U},
    };
    return digit < 10U && x < 3U && y < 5U &&
           (DIGITS[digit][y] & (1U << (2U - x))) != 0U;
}

static uint16_t label_pixel(uint32_t value, size_t x, size_t y)
{
    if (y == LABEL_HEIGHT - 1U) {
        return 0x2104U;
    }
    if (y < 2U || y >= 7U) {
        return 0x0000U;
    }
    const uint8_t tens = (uint8_t)((value / 10U) % 10U);
    const uint8_t ones = (uint8_t)(value % 10U);
    const bool lit =
        (x >= 3U && x < 6U &&
         digit_pixel(tens, x - 3U, y - 2U)) ||
        (x >= 8U && x < 11U &&
         digit_pixel(ones, x - 8U, y - 2U));
    return lit ? 0xffffU : 0x0000U;
}

static bool write_expression_sheet(
    const char *directory, face_render_profile_t profile)
{
    char path[PATH_BUFFER_BYTES];
    const int length = snprintf(
        path, sizeof(path), "%s/%s.ppm", directory,
        face_render_profile_slug(profile));
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }

    const size_t sheet_width =
        EXPRESSION_SHEET_COLUMNS * FACE_RENDER_WIDTH;
    const size_t cell_height = FACE_RENDER_HEIGHT + LABEL_HEIGHT;
    const size_t sheet_height = EXPRESSION_SHEET_ROWS * cell_height;
    bool ok = write_ppm_header(file, sheet_width, sheet_height);
    for (size_t cell_row = 0U;
         ok && cell_row < EXPRESSION_SHEET_ROWS;
         ++cell_row) {
        for (size_t y = 0U; ok && y < cell_height; ++y) {
            for (size_t cell_column = 0U;
                 ok && cell_column < EXPRESSION_SHEET_COLUMNS;
                 ++cell_column) {
                const size_t expression =
                    cell_row * EXPRESSION_SHEET_COLUMNS + cell_column;
                for (size_t x = 0U; ok && x < FACE_RENDER_WIDTH; ++x) {
                    uint16_t pixel = 0x0000U;
                    if (expression < FACE_EXPRESSION_COUNT) {
                        pixel =
                            y < LABEL_HEIGHT
                                ? label_pixel(
                                      (uint32_t)expression, x, y)
                                : expression_frames[expression]
                                      [(y - LABEL_HEIGHT) *
                                           FACE_RENDER_WIDTH +
                                       x];
                    }
                    ok = write_rgb(file, pixel);
                }
            }
        }
    }
    ok = fclose(file) == 0 && ok;
    return ok;
}

static bool write_atlas_profile_rows(
    FILE *file, face_render_profile_t profile)
{
    for (size_t y = 0U;
         y < FACE_RENDER_HEIGHT;
         y += ATLAS_SCALE) {
        for (size_t x = 0U;
             x < ATLAS_PROFILE_LABEL_WIDTH;
             ++x) {
            if (!write_rgb(
                    file,
                    label_pixel(
                        (uint32_t)profile,
                        x,
                        y / ATLAS_SCALE))) {
                return false;
            }
        }
        for (size_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            for (size_t x = 0U;
                 x < FACE_RENDER_WIDTH;
                 x += ATLAS_SCALE) {
                if (!write_rgb(
                        file,
                        expression_frames[expression]
                                         [y * FACE_RENDER_WIDTH + x])) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool render_motion_peak_frames(
    face_render_profile_t profile, uint32_t peak_frame,
    uint32_t *first_frame)
{
    uint32_t start =
        peak_frame >= MOTION_PEAK_FRAME_COUNT / 2U
            ? peak_frame - MOTION_PEAK_FRAME_COUNT / 2U
            : 0U;
    if (start + MOTION_PEAK_FRAME_COUNT > MOTION_FRAME_COUNT) {
        start = MOTION_FRAME_COUNT - MOTION_PEAK_FRAME_COUNT;
    }
    for (uint32_t index = 0U;
         index < MOTION_PEAK_FRAME_COUNT;
         ++index) {
        if (!render_motion_frame(
                profile, start + index,
                motion_peak_frames[index])) {
            return false;
        }
    }
    *first_frame = start;
    return true;
}

static bool write_motion_sheet(
    const char *directory,
    face_render_profile_t profile,
    uint32_t first_frame)
{
    char path[PATH_BUFFER_BYTES];
    const int length = snprintf(
        path, sizeof(path), "%s/%s.ppm", directory,
        face_render_profile_slug(profile));
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }

    const size_t frame_width = FACE_RENDER_WIDTH / 2U;
    const size_t frame_height = FACE_RENDER_HEIGHT / 2U;
    const size_t cell_height = frame_height + LABEL_HEIGHT;
    const size_t sheet_width = MOTION_SHEET_COLUMNS * frame_width;
    const size_t sheet_height = MOTION_SHEET_ROWS * cell_height;
    bool ok = write_ppm_header(file, sheet_width, sheet_height);
    for (size_t cell_row = 0U;
         ok && cell_row < MOTION_SHEET_ROWS;
         ++cell_row) {
        for (size_t y = 0U; ok && y < cell_height; ++y) {
            for (size_t cell_column = 0U;
                 ok && cell_column < MOTION_SHEET_COLUMNS;
                 ++cell_column) {
                const size_t frame_index =
                    cell_row * MOTION_SHEET_COLUMNS + cell_column;
                for (size_t x = 0U; ok && x < frame_width; ++x) {
                    const uint16_t pixel =
                        y < LABEL_HEIGHT
                            ? label_pixel(
                                  first_frame + (uint32_t)frame_index,
                                  x, y)
                            : motion_peak_frames[frame_index]
                                  [(y - LABEL_HEIGHT) * 2U *
                                       FACE_RENDER_WIDTH +
                                   x * 2U];
                    ok = write_rgb(file, pixel);
                }
            }
        }
    }
    ok = fclose(file) == 0 && ok;
    return ok;
}

static void write_json_string(FILE *file, const char *value)
{
    fputc('"', file);
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0';
         ++cursor) {
        switch (*cursor) {
        case '"':
            fputs("\\\"", file);
            break;
        case '\\':
            fputs("\\\\", file);
            break;
        case '\b':
            fputs("\\b", file);
            break;
        case '\f':
            fputs("\\f", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            if (*cursor < 0x20U) {
                fprintf(file, "\\u%04x", *cursor);
            } else {
                fputc(*cursor, file);
            }
            break;
        }
    }
    fputc('"', file);
}

static void write_profile_json(
    FILE *file, const profile_metrics_t *metrics, bool last)
{
    const face_render_profile_t profile =
        (face_render_profile_t)metrics->profile_index;
    face_render_info_t info;
    memset(&info, 0, sizeof(info));
    (void)face_render_profile_info(profile, &info);

    fputs("    {\n      \"index\": ", file);
    fprintf(file, "%zu,\n      \"slug\": ", metrics->profile_index);
    write_json_string(file, face_render_profile_slug(profile));
    fputs(",\n      \"name\": ", file);
    write_json_string(file, face_render_profile_name(profile));
    fputs(",\n      \"family\": ", file);
    write_json_string(file, face_render_profile_family_name(profile));
    fprintf(
        file,
        ",\n      \"flags\": %u,\n"
        "      \"expression\": {\n"
        "        \"status\": \"%s\",\n"
        "        \"score\": %.6f,\n"
        "        \"distinct_frame_hashes\": %" PRIu32 ",\n"
        "        \"identical_pairs\": %" PRIu32 ",\n"
        "        \"weak_pairs\": %" PRIu32 ",\n"
        "        \"clear_nonneutral\": %" PRIu32 ",\n"
        "        \"minimum_pair_roi_delta\": %.9f,\n"
        "        \"median_pair_roi_delta\": %.9f,\n"
        "        \"mean_pair_roi_delta\": %.9f,\n"
        "        \"maximum_pair_roi_delta\": %.9f,\n"
        "        \"minimum_nonneutral_roi_delta\": %.9f,\n"
        "        \"mean_nonneutral_roi_delta\": %.9f,\n"
        "        \"maximum_edge_change_fraction\": %.9f,\n"
        "        \"neutral_to_expression\": [\n",
        info.flags,
        metrics->expression_status,
        metrics->expression_score,
        metrics->distinct_expression_hashes,
        metrics->identical_expression_pairs,
        metrics->weak_expression_pairs,
        metrics->clear_nonneutral_expressions,
        metrics->minimum_pair_roi_delta,
        metrics->median_pair_roi_delta,
        metrics->mean_pair_roi_delta,
        metrics->maximum_pair_roi_delta,
        metrics->minimum_nonneutral_roi_delta,
        metrics->mean_nonneutral_roi_delta,
        metrics->maximum_expression_edge_change);
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        fputs("          {\"expression\": ", file);
        write_json_string(file, EXPRESSION_NAMES[expression]);
        fprintf(
            file,
            ", \"roi_delta\": %.9f, "
            "\"roi_changed_fraction\": %.9f}%s\n",
            metrics->neutral_roi_delta[expression],
            metrics->neutral_changed_fraction[expression],
            expression + 1U == FACE_EXPRESSION_COUNT ? "" : ",");
    }
    fprintf(
        file,
        "        ]\n"
        "      },\n"
        "      \"motion\": {\n"
        "        \"status\": \"%s\",\n"
        "        \"score\": %.6f,\n"
        "        \"frames\": %d,\n"
        "        \"fps\": %d,\n"
        "        \"median_roi_delta\": %.9f,\n"
        "        \"p95_roi_delta\": %.9f,\n"
        "        \"maximum_roi_delta\": %.9f,\n"
        "        \"maximum_roi_changed_fraction\": %.9f,\n"
        "        \"jump_threshold\": %.9f,\n"
        "        \"maximum_to_p95_ratio\": %.9f,\n"
        "        \"abrupt_jumps\": %" PRIu32 ",\n",
        metrics->motion_status,
        metrics->motion_score,
        MOTION_FRAME_COUNT,
        FRAMES_PER_SECOND,
        metrics->motion_median_delta,
        metrics->motion_p95_delta,
        metrics->motion_maximum_delta,
        metrics->motion_maximum_changed_fraction,
        metrics->motion_jump_threshold,
        metrics->motion_jump_ratio,
        metrics->motion_abrupt_jumps);
    fputs("        \"abrupt_transition_frames\": [", file);
    for (uint32_t index = 0U;
         index < metrics->motion_abrupt_jumps;
         ++index) {
        fprintf(
            file,
            "%s%u",
            index == 0U ? "" : ", ",
            (unsigned)metrics->motion_abrupt_frames[index]);
    }
    fprintf(
        file,
        "],\n"
        "        \"frozen_pairs\": %" PRIu32 ",\n"
        "        \"peak_transition_frame\": %" PRIu32 "\n"
        "      },\n"
        "      \"artifacts\": {\n"
        "        \"expressions_ppm\": \"expressions-ppm/%s.ppm\",\n"
        "        \"expressions_png\": \"expressions-png/%s.png\",\n"
        "        \"motion_peak_ppm\": \"motion-peaks-ppm/%s.ppm\",\n"
        "        \"motion_peak_png\": \"motion-peaks-png/%s.png\"\n"
        "      }\n"
        "    }%s\n",
        metrics->motion_frozen_pairs,
        metrics->motion_peak_frame,
        face_render_profile_slug(profile),
        face_render_profile_slug(profile),
        face_render_profile_slug(profile),
        face_render_profile_slug(profile),
        last ? "" : ",");
}

static bool write_metrics_json(
    const char *path, const profile_metrics_t *metrics)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    uint32_t expression_pass = 0U;
    uint32_t expression_warn = 0U;
    uint32_t expression_fail = 0U;
    uint32_t motion_pass = 0U;
    uint32_t motion_warn = 0U;
    uint32_t motion_fail = 0U;
    uint32_t profiles_with_duplicate_expressions = 0U;
    uint32_t total_abrupt_jumps = 0U;
    double expression_score_sum = 0.0;
    double motion_score_sum = 0.0;
    for (size_t profile = 0U;
         profile < face_render_profile_count();
         ++profile) {
        expression_pass +=
            strcmp(metrics[profile].expression_status, "pass") == 0;
        expression_warn +=
            strcmp(metrics[profile].expression_status, "warn") == 0;
        expression_fail +=
            strcmp(metrics[profile].expression_status, "fail") == 0;
        motion_pass +=
            strcmp(metrics[profile].motion_status, "pass") == 0;
        motion_warn +=
            strcmp(metrics[profile].motion_status, "warn") == 0;
        motion_fail +=
            strcmp(metrics[profile].motion_status, "fail") == 0;
        profiles_with_duplicate_expressions +=
            metrics[profile].identical_expression_pairs > 0U;
        total_abrupt_jumps += metrics[profile].motion_abrupt_jumps;
        expression_score_sum += metrics[profile].expression_score;
        motion_score_sum += metrics[profile].motion_score;
    }
    const size_t profile_count = face_render_profile_count();
    const bool quality_pass =
        expression_fail == 0U && expression_warn == 0U &&
        motion_fail == 0U;

    fprintf(
        file,
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"deterministic\": true,\n"
        "  \"renderer\": {\n"
        "    \"width\": %d,\n"
        "    \"height\": %d,\n"
        "    \"rgb_format\": \"RGB565\",\n"
        "    \"render_key_bytes\": %zu,\n"
        "    \"stage_cue_bytes\": %zu,\n"
        "    \"profile_count\": %zu\n"
        "  },\n"
        "  \"expression_fixture\": {\n"
        "    \"sample_clock\": %u,\n"
        "    \"roi\": {\"x\": 16, \"y\": 10, "
        "\"width\": 128, \"height\": 100},\n"
        "    \"clear_threshold\": {\"roi_delta\": 0.008, "
        "\"roi_changed_fraction\": 0.018},\n"
        "    \"weak_pair_threshold\": {\"roi_delta\": 0.006, "
        "\"roi_changed_fraction\": 0.015},\n"
        "    \"expressions\": [",
        FACE_RENDER_WIDTH,
        FACE_RENDER_HEIGHT,
        sizeof(face_render_key_t),
        sizeof(face_stage_cue_t),
        profile_count,
        SAMPLE_RATE * 7U + 211U);
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        if (expression > 0U) {
            fputs(", ", file);
        }
        write_json_string(file, EXPRESSION_NAMES[expression]);
    }
    fprintf(
        file,
        "]\n"
        "  },\n"
        "  \"motion_fixture\": {\n"
        "    \"sample_rate\": %d,\n"
        "    \"fps\": %d,\n"
        "    \"frames\": %d,\n"
        "    \"description\": \"smooth triangle-wave articulation plus "
        "one sample-clock-addressed joy cue with attack and release\"\n"
        "  },\n"
        "  \"summary\": {\n"
        "    \"quality_pass\": %s,\n"
        "    \"expression\": {\"pass\": %" PRIu32
        ", \"warn\": %" PRIu32 ", \"fail\": %" PRIu32
        ", \"mean_score\": %.6f},\n"
        "    \"motion\": {\"pass\": %" PRIu32
        ", \"warn\": %" PRIu32 ", \"fail\": %" PRIu32
        ", \"mean_score\": %.6f},\n"
        "    \"profiles_with_exact_duplicate_expressions\": %" PRIu32 ",\n"
        "    \"total_abrupt_jumps\": %" PRIu32 "\n"
        "  },\n"
        "  \"artifacts\": {\n"
        "    \"expression_atlas_ppm\": \"expression-atlas.ppm\",\n"
        "    \"expression_atlas_png\": \"expression-atlas.png\",\n"
        "    \"expression_atlas_layout\": "
        "{\"profile_label_width\": %d, "
        "\"profile_label_is_renderer_index\": true},\n"
        "    \"expression_sheet_layout\": "
        "{\"columns\": 4, \"rows\": 3, \"label_is_expression_index\": true},\n"
        "    \"motion_sheet_layout\": "
        "{\"columns\": 6, \"rows\": 2, \"label_is_frame_index_mod_100\": true}\n"
        "  },\n"
        "  \"profiles\": [\n",
        SAMPLE_RATE,
        FRAMES_PER_SECOND,
        MOTION_FRAME_COUNT,
        quality_pass ? "true" : "false",
        expression_pass,
        expression_warn,
        expression_fail,
        expression_score_sum / (double)profile_count,
        motion_pass,
        motion_warn,
        motion_fail,
        motion_score_sum / (double)profile_count,
        profiles_with_duplicate_expressions,
        total_abrupt_jumps,
        ATLAS_PROFILE_LABEL_WIDTH);
    for (size_t profile = 0U; profile < profile_count; ++profile) {
        write_profile_json(
            file, &metrics[profile], profile + 1U == profile_count);
    }
    fputs("  ]\n}\n", file);
    return fclose(file) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(
            stderr,
            "usage: %s METRICS_JSON EXPRESSION_PPM_DIR "
            "MOTION_PPM_DIR ATLAS_PPM STATUS_TXT\n",
            argv[0]);
        return 2;
    }
    const char *metrics_path = argv[1];
    const char *expression_directory = argv[2];
    const char *motion_directory = argv[3];
    const char *atlas_path = argv[4];
    const char *status_path = argv[5];
    profile_metrics_t metrics[FACE_RENDER_PROFILE_COUNT];
    memset(metrics, 0, sizeof(metrics));

    FILE *atlas = fopen(atlas_path, "wb");
    if (atlas == NULL) {
        fprintf(
            stderr, "cannot open %s: %s\n",
            atlas_path, strerror(errno));
        return 1;
    }
    const size_t atlas_width =
        ATLAS_PROFILE_LABEL_WIDTH +
        FACE_EXPRESSION_COUNT * FACE_RENDER_WIDTH / ATLAS_SCALE;
    const size_t atlas_height =
        face_render_profile_count() *
        FACE_RENDER_HEIGHT / ATLAS_SCALE;
    if (!write_ppm_header(atlas, atlas_width, atlas_height)) {
        fclose(atlas);
        return 1;
    }

    for (size_t raw_profile = 0U;
         raw_profile < face_render_profile_count();
         ++raw_profile) {
        const face_render_profile_t profile =
            (face_render_profile_t)raw_profile;
        profile_metrics_t *profile_metrics = &metrics[raw_profile];
        profile_metrics->profile_index = raw_profile;
        if (!render_expression_frames(profile)) {
            fprintf(
                stderr, "failed to render expressions for %s\n",
                face_render_profile_slug(profile));
            fclose(atlas);
            return 1;
        }
        evaluate_expression_metrics(profile_metrics);
        if (!write_expression_sheet(expression_directory, profile) ||
            !write_atlas_profile_rows(atlas, profile)) {
            fclose(atlas);
            return 1;
        }
        if (!evaluate_motion_metrics(profile, profile_metrics)) {
            fprintf(
                stderr, "failed to render motion for %s\n",
                face_render_profile_slug(profile));
            fclose(atlas);
            return 1;
        }
        uint32_t first_peak_frame = 0U;
        if (!render_motion_peak_frames(
                profile, profile_metrics->motion_peak_frame,
                &first_peak_frame) ||
            !write_motion_sheet(
                motion_directory, profile, first_peak_frame)) {
            fclose(atlas);
            return 1;
        }
    }
    if (fclose(atlas) != 0 ||
        !write_metrics_json(metrics_path, metrics)) {
        return 1;
    }

    FILE *status = fopen(status_path, "wb");
    if (status == NULL) {
        fprintf(
            stderr, "cannot open %s: %s\n",
            status_path, strerror(errno));
        return 1;
    }
    fprintf(
        status,
        "face_render_quality: PASS (probe completed for %zu profiles; "
        "consult metrics.json for visual quality grades)\n",
        face_render_profile_count());
    if (fclose(status) != 0) {
        return 1;
    }
    return 0;
}
