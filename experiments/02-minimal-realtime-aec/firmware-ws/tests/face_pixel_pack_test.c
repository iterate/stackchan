#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pixel_pack.h"
#include "face_stage.h"

enum {
    CANARY_WORDS = 32,
    SAMPLE_RATE = 16000,
    MOTION_FRAMES = 180,
};

static uint16_t guarded_frame[
    CANARY_WORDS + FACE_PIXEL_PACK_PIXEL_COUNT + CANARY_WORDS];
static uint16_t expression_frames[
    FACE_EXPRESSION_COUNT][FACE_PIXEL_PACK_PIXEL_COUNT];
static uint16_t comparison_frame[FACE_PIXEL_PACK_PIXEL_COUNT];
static uint16_t motion_previous[FACE_PIXEL_PACK_PIXEL_COUNT];
static uint16_t motion_current[FACE_PIXEL_PACK_PIXEL_COUNT];

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 116U;
    key.controls.mouth_width = 150U;
    key.controls.mouth_round = 44U;
    key.controls.mouth_press = 8U;
    key.controls.mouth_teeth = 72U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = 1U; /* listening activity, never an emotion */
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 10U;
    key.viseme_weight = 224U;
    key.audio_level = 148U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 11U;
    key.viseme_blend = 36U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static bool bounds_inside(
    face_pixel_pack_bounds_t bounds, unsigned margin)
{
    return bounds.width > 0U && bounds.height > 0U &&
           bounds.x >= margin && bounds.y >= margin &&
           (unsigned)bounds.x + bounds.width <=
               FACE_PIXEL_PACK_WIDTH - margin &&
           (unsigned)bounds.y + bounds.height <=
               FACE_PIXEL_PACK_HEIGHT - margin;
}

static size_t distinct_colours(const uint16_t *frame)
{
    uint16_t seen[64];
    size_t count = 0U;
    for (size_t index = 0U; index < FACE_PIXEL_PACK_PIXEL_COUNT; ++index) {
        size_t match = 0U;
        while (match < count && seen[match] != frame[index]) {
            ++match;
        }
        if (match == count && count < sizeof(seen) / sizeof(seen[0])) {
            seen[count++] = frame[index];
        }
    }
    return count;
}

static uint32_t frame_hash(const uint16_t *frame)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < FACE_PIXEL_PACK_PIXEL_COUNT; ++index) {
        hash ^= frame[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t changed_pixels(
    const uint16_t *first, const uint16_t *second)
{
    size_t changed = 0U;
    for (size_t index = 0U; index < FACE_PIXEL_PACK_PIXEL_COUNT; ++index) {
        changed += first[index] != second[index];
    }
    return changed;
}

static void test_public_contract(void)
{
    assert(sizeof(face_render_key_t) == 40U);
    assert(FACE_EXPRESSION_COUNT == 11);
    assert(face_pixel_pack_profile_count() == FACE_PIXEL_PACK_PROFILE_COUNT);
    assert(FACE_PIXEL_PACK_FRAME_BYTES == 38400);
    assert(FACE_PIXEL_PACK_CONTEXT_BYTES == 0);

    for (size_t index = 0U; index < FACE_PIXEL_PACK_PROFILE_COUNT; ++index) {
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)index;
        face_pixel_pack_style_t style;
        assert(face_pixel_pack_profile_slug(profile) != NULL);
        assert(face_pixel_pack_profile_name(profile) != NULL);
        assert(face_pixel_pack_profile_style(profile, &style));
        assert(style.native_width > 0U);
        assert(style.native_height > 0U);
        assert(style.palette_colours >= 2U);
    }

    face_render_key_t key = base_key();
    uint16_t *frame = guarded_frame + CANARY_WORDS;
    assert(!face_pixel_pack_render(
        (face_pixel_pack_profile_t)FACE_PIXEL_PACK_PROFILE_COUNT,
        &key, 0U, frame, FACE_PIXEL_PACK_PIXEL_COUNT));
    assert(!face_pixel_pack_render(
        FACE_PIXEL_PACK_EGA_QUEST, NULL, 0U, frame,
        FACE_PIXEL_PACK_PIXEL_COUNT));
    assert(!face_pixel_pack_render(
        FACE_PIXEL_PACK_EGA_QUEST, &key, 0U, NULL,
        FACE_PIXEL_PACK_PIXEL_COUNT));
    assert(!face_pixel_pack_render(
        FACE_PIXEL_PACK_EGA_QUEST, &key, 0U, frame,
        FACE_PIXEL_PACK_PIXEL_COUNT - 1U));
    assert(!face_pixel_pack_profile_style(
        FACE_PIXEL_PACK_EGA_QUEST, NULL));
}

static void test_profiles_are_bounded_character_art(void)
{
    const uint16_t canary = 0xa55aU;
    uint32_t hashes[FACE_PIXEL_PACK_PROFILE_COUNT];
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        for (size_t index = 0U;
             index < sizeof(guarded_frame) / sizeof(guarded_frame[0]);
             ++index) {
            guarded_frame[index] = canary;
        }
        uint16_t *frame = guarded_frame + CANARY_WORDS;
        face_render_key_t key = base_key();
        face_pixel_pack_landmarks_t landmarks;
        face_pixel_pack_style_t style;
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        assert(face_pixel_pack_profile_style(profile, &style));
        assert(face_pixel_pack_render_checked(
            profile, &key, SAMPLE_RATE * 3U + 211U,
            frame, FACE_PIXEL_PACK_PIXEL_COUNT, &landmarks));
        const size_t minimum_colours =
            style.palette_colours < 8U
                ? style.palette_colours
                : 8U;
        assert(distinct_colours(frame) >= minimum_colours);
        if (!bounds_inside(landmarks.face, 4U) ||
            !bounds_inside(landmarks.left_eye, 8U) ||
            !bounds_inside(landmarks.right_eye, 8U) ||
            !bounds_inside(landmarks.mouth, 8U)) {
            fprintf(
                stderr,
                "%s unsafe bounds: face=%u,%u %ux%u "
                "left=%u,%u %ux%u right=%u,%u %ux%u "
                "mouth=%u,%u %ux%u\n",
                face_pixel_pack_profile_slug(profile),
                landmarks.face.x, landmarks.face.y,
                landmarks.face.width, landmarks.face.height,
                landmarks.left_eye.x, landmarks.left_eye.y,
                landmarks.left_eye.width, landmarks.left_eye.height,
                landmarks.right_eye.x, landmarks.right_eye.y,
                landmarks.right_eye.width, landmarks.right_eye.height,
                landmarks.mouth.x, landmarks.mouth.y,
                landmarks.mouth.width, landmarks.mouth.height);
        }
        assert(bounds_inside(landmarks.face, 4U));
        assert(bounds_inside(landmarks.left_eye, 8U));
        assert(bounds_inside(landmarks.right_eye, 8U));
        assert(bounds_inside(landmarks.mouth, 8U));
        assert(landmarks.left_eye.x < landmarks.right_eye.x);
        assert(landmarks.left_eye.y < landmarks.mouth.y);
        assert(landmarks.right_eye.y < landmarks.mouth.y);
        for (size_t index = 0U; index < CANARY_WORDS; ++index) {
            assert(guarded_frame[index] == canary);
            assert(guarded_frame[
                       CANARY_WORDS +
                       FACE_PIXEL_PACK_PIXEL_COUNT + index] ==
                   canary);
        }
        hashes[profile_index] = frame_hash(frame);
    }
    for (size_t first = 0U;
         first < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++first) {
        for (size_t second = first + 1U;
             second < FACE_PIXEL_PACK_PROFILE_COUNT;
             ++second) {
            assert(hashes[first] != hashes[second]);
        }
    }
}

static void test_all_stage_expressions_are_visibly_separable(void)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (size_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            key.expression_weight = 255U;
            assert(face_pixel_pack_render(
                profile, &key, fixed_clock,
                expression_frames[expression],
                FACE_PIXEL_PACK_PIXEL_COUNT));
            hashes[expression] =
                frame_hash(expression_frames[expression]);
        }
        size_t minimum_pair_change = FACE_PIXEL_PACK_PIXEL_COUNT;
        size_t weakest_first = 0U;
        size_t weakest_second = 0U;
        for (size_t first = 0U;
             first < FACE_EXPRESSION_COUNT;
             ++first) {
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                if (hashes[first] == hashes[second]) {
                    fprintf(
                        stderr,
                        "%s identical expressions: %zu and %zu\n",
                        face_pixel_pack_profile_slug(profile),
                        first, second);
                }
                assert(hashes[first] != hashes[second]);
                const size_t changed = changed_pixels(
                    expression_frames[first],
                    expression_frames[second]);
                if (changed < minimum_pair_change) {
                    minimum_pair_change = changed;
                    weakest_first = first;
                    weakest_second = second;
                }
            }
        }
        if (minimum_pair_change < 90U) {
            fprintf(
                stderr,
                "%s weak expression pair %zu/%zu: %zu pixels\n",
                face_pixel_pack_profile_slug(profile),
                weakest_first, weakest_second,
                minimum_pair_change);
        }
        assert(minimum_pair_change >= 90U);

        /* Activity and authored emotion are orthogonal IR layers. */
        face_render_key_t reference_key = base_key();
        reference_key.stage_expression = FACE_EXPRESSION_SKEPTICAL;
        reference_key.expression_weight = 255U;
        reference_key.controls.expression = FACE_ACTIVITY_IDLE;
        assert(face_pixel_pack_render(
            profile, &reference_key, fixed_clock,
            comparison_frame, FACE_PIXEL_PACK_PIXEL_COUNT));
        const uint32_t reference_hash = frame_hash(comparison_frame);
        for (uint8_t activity = FACE_ACTIVITY_LISTENING;
             activity <= FACE_ACTIVITY_SPEAKING;
             ++activity) {
            face_render_key_t key = reference_key;
            key.controls.expression = activity;
            assert(face_pixel_pack_render(
                profile, &key, fixed_clock,
                comparison_frame, FACE_PIXEL_PACK_PIXEL_COUNT));
            assert(frame_hash(comparison_frame) == reference_hash);
        }
    }
}

static void test_pcm_and_visemes_keep_driving_the_mouth(void)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 9U + 137U;
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        face_render_key_t closed = base_key();
        closed.stage_expression = FACE_EXPRESSION_JOY;
        closed.expression_weight = 255U;
        closed.controls.mouth_open = 0U;
        closed.controls.mouth_press = 230U;
        closed.viseme = FACE_VISEME_SIL;
        closed.viseme_secondary = FACE_VISEME_NONE;
        closed.viseme_weight = 255U;
        assert(face_pixel_pack_render(
            profile, &closed, fixed_clock,
            expression_frames[0], FACE_PIXEL_PACK_PIXEL_COUNT));

        face_render_key_t open = closed;
        open.controls.mouth_open = 250U;
        open.controls.mouth_press = 0U;
        open.controls.mouth_width = 172U;
        open.viseme = FACE_VISEME_AA;
        assert(face_pixel_pack_render(
            profile, &open, fixed_clock,
            expression_frames[1], FACE_PIXEL_PACK_PIXEL_COUNT));
        const size_t pcm_change = changed_pixels(
            expression_frames[0], expression_frames[1]);
        assert(pcm_change >= 60U);

        face_render_key_t rounded = base_key();
        rounded.viseme = FACE_VISEME_O;
        rounded.viseme_secondary = FACE_VISEME_NONE;
        rounded.viseme_weight = 255U;
        assert(face_pixel_pack_render(
            profile, &rounded, fixed_clock,
            expression_frames[2], FACE_PIXEL_PACK_PIXEL_COUNT));
        face_render_key_t pressed = rounded;
        pressed.viseme = FACE_VISEME_PP;
        assert(face_pixel_pack_render(
            profile, &pressed, fixed_clock,
            expression_frames[3], FACE_PIXEL_PACK_PIXEL_COUNT));
        const size_t viseme_change = changed_pixels(
            expression_frames[2], expression_frames[3]);
        assert(viseme_change >= 24U);
    }
}

static uint32_t fuzz_next(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void test_extreme_ir_bytes_stay_bounded(void)
{
    const uint16_t canary = 0x6d5aU;
    uint32_t state = 0xface4028U;
    for (unsigned iteration = 0U; iteration < 256U; ++iteration) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t byte = 0U; byte < sizeof(key); ++byte) {
            bytes[byte] = (uint8_t)(fuzz_next(&state) >> 24U);
        }
        for (size_t profile_index = 0U;
             profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
             ++profile_index) {
            for (size_t index = 0U;
                 index < sizeof(guarded_frame) /
                             sizeof(guarded_frame[0]);
                 ++index) {
                guarded_frame[index] = canary;
            }
            face_pixel_pack_landmarks_t landmarks;
            assert(face_pixel_pack_render_checked(
                (face_pixel_pack_profile_t)profile_index,
                &key, fuzz_next(&state),
                guarded_frame + CANARY_WORDS,
                FACE_PIXEL_PACK_PIXEL_COUNT, &landmarks));
            if (!bounds_inside(landmarks.face, 3U) ||
                !bounds_inside(landmarks.left_eye, 6U) ||
                !bounds_inside(landmarks.right_eye, 6U) ||
                !bounds_inside(landmarks.mouth, 6U)) {
                fprintf(
                    stderr,
                    "fuzz %u profile %zu unsafe: "
                    "face=%u,%u %ux%u eyes=%u,%u/%u,%u "
                    "mouth=%u,%u %ux%u\n",
                    iteration, profile_index,
                    landmarks.face.x, landmarks.face.y,
                    landmarks.face.width, landmarks.face.height,
                    landmarks.left_eye.x, landmarks.left_eye.y,
                    landmarks.right_eye.x, landmarks.right_eye.y,
                    landmarks.mouth.x, landmarks.mouth.y,
                    landmarks.mouth.width, landmarks.mouth.height);
            }
            assert(bounds_inside(landmarks.face, 3U));
            assert(bounds_inside(landmarks.left_eye, 6U));
            assert(bounds_inside(landmarks.right_eye, 6U));
            assert(bounds_inside(landmarks.mouth, 6U));
            for (size_t index = 0U; index < CANARY_WORDS; ++index) {
                assert(guarded_frame[index] == canary);
                assert(guarded_frame[
                           CANARY_WORDS +
                           FACE_PIXEL_PACK_PIXEL_COUNT + index] ==
                       canary);
            }
        }
    }
}

static void test_clock_motion_is_smooth_not_frozen(void)
{
    const uint32_t frame_samples = SAMPLE_RATE / 30U;
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        face_render_key_t key = base_key();
        key.stage_expression = FACE_EXPRESSION_WARM;
        key.expression_weight = 255U;
        const uint32_t start_clock = 1777U;
        assert(face_pixel_pack_render(
            profile, &key, start_clock,
            motion_previous, FACE_PIXEL_PACK_PIXEL_COUNT));
        size_t maximum_change = 0U;
        size_t moving_pairs = 0U;
        uint64_t total_change = 0U;
        for (uint32_t frame = 1U; frame < MOTION_FRAMES; ++frame) {
            assert(face_pixel_pack_render(
                profile, &key,
                start_clock + frame * frame_samples,
                motion_current, FACE_PIXEL_PACK_PIXEL_COUNT));
            const size_t changed =
                changed_pixels(motion_previous, motion_current);
            maximum_change =
                changed > maximum_change ? changed : maximum_change;
            moving_pairs += changed > 0U;
            total_change += changed;
            memcpy(
                motion_previous, motion_current,
                sizeof(motion_previous));
        }
        if (maximum_change > 3800U || moving_pairs < 5U) {
            fprintf(
                stderr,
                "%s temporal: max=%zu mean=%llu moving=%zu/%u\n",
                face_pixel_pack_profile_slug(profile),
                maximum_change,
                (unsigned long long)(
                    total_change / (MOTION_FRAMES - 1U)),
                moving_pairs, MOTION_FRAMES - 1U);
        }
        assert(moving_pairs >= 5U);
        assert(maximum_change <= 3800U);
    }
}

typedef struct {
    double roi_delta;
    double roi_changed_fraction;
} quality_difference_t;

static uint8_t quality_red(uint16_t pixel)
{
    const uint8_t value = (uint8_t)((pixel >> 11U) & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t quality_green(uint16_t pixel)
{
    const uint8_t value = (uint8_t)((pixel >> 5U) & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static uint8_t quality_blue(uint16_t pixel)
{
    const uint8_t value = (uint8_t)(pixel & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint32_t quality_absolute_difference(
    uint8_t first, uint8_t second)
{
    return first > second
               ? (uint32_t)first - second
               : (uint32_t)second - first;
}

static quality_difference_t quality_compare(
    const uint16_t *first, const uint16_t *second)
{
    uint64_t channel_delta = 0U;
    size_t changed = 0U;
    size_t pixels = 0U;
    for (size_t y = 10U; y < FACE_PIXEL_PACK_HEIGHT - 10U; ++y) {
        for (size_t x = 16U; x < FACE_PIXEL_PACK_WIDTH - 16U; ++x) {
            const size_t index = y * FACE_PIXEL_PACK_WIDTH + x;
            channel_delta += quality_absolute_difference(
                quality_red(first[index]), quality_red(second[index]));
            channel_delta += quality_absolute_difference(
                quality_green(first[index]), quality_green(second[index]));
            channel_delta += quality_absolute_difference(
                quality_blue(first[index]), quality_blue(second[index]));
            changed += first[index] != second[index];
            ++pixels;
        }
    }
    const quality_difference_t difference = {
        .roi_delta =
            (double)channel_delta / ((double)pixels * 3.0 * 255.0),
        .roi_changed_fraction = (double)changed / (double)pixels,
    };
    return difference;
}

static face_render_key_t quality_base_key(void)
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
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
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

static face_stage_cue_t quality_expression_cue(
    face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static uint8_t quality_triangle(uint32_t frame, uint32_t period)
{
    const uint32_t phase = frame % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

static face_render_key_t quality_motion_key(uint32_t frame)
{
    face_render_key_t key = quality_base_key();
    const uint8_t jaw_wave = quality_triangle(frame + 7U, 42U);
    const uint8_t form_wave = quality_triangle(frame + 19U, 74U);
    const uint8_t gaze_wave = quality_triangle(frame + 11U, 120U);
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
        (int8_t)(
            (int32_t)quality_triangle(frame + 37U, 156U) / 6 - 21);
    key.audio_level =
        (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_blend = form_wave;
    key.head_roll =
        (int8_t)(
            (int32_t)quality_triangle(frame + 13U, 180U) / 12 - 10);
    return key;
}

static face_stage_cue_t quality_motion_cue(void)
{
    face_stage_cue_t cue =
        quality_expression_cue(FACE_EXPRESSION_JOY);
    cue.start_sample = SAMPLE_RATE;
    cue.attack_samples = SAMPLE_RATE;
    cue.hold_samples = SAMPLE_RATE * 2U;
    cue.release_samples = SAMPLE_RATE;
    cue.flags = 0U;
    cue.gesture = FACE_GESTURE_NOD;
    cue.intensity = 238U;
    return cue;
}

/*
 * This mirrors the production tools/run_face_render_quality.py gate for the
 * four pixel-pack profiles. Keep it here as the fast renderer-local regression
 * test, then run the production probe as the final integration check.
 */
static void test_production_quality_gate(void)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    bool quality_ok = true;
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        unsigned clear_nonneutral = 0U;
        double pair_sum = 0.0;
        for (size_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = quality_base_key();
            const face_stage_cue_t cue =
                quality_expression_cue((face_expression_t)expression);
            assert(face_stage_cue_apply(&cue, fixed_clock, &key));
            assert(face_pixel_pack_render(
                profile, &key, fixed_clock,
                expression_frames[expression],
                FACE_PIXEL_PACK_PIXEL_COUNT));
            hashes[expression] =
                frame_hash(expression_frames[expression]);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                const quality_difference_t difference = quality_compare(
                    expression_frames[FACE_EXPRESSION_NEUTRAL],
                    expression_frames[expression]);
                clear_nonneutral +=
                    difference.roi_delta >= 0.008 &&
                    difference.roi_changed_fraction >= 0.018;
            }
        }
        unsigned distinct_hashes = 0U;
        unsigned weak_pairs = 0U;
        for (size_t first = 0U;
             first < FACE_EXPRESSION_COUNT;
             ++first) {
            bool first_occurrence = true;
            for (size_t earlier = 0U; earlier < first; ++earlier) {
                first_occurrence &=
                    hashes[earlier] != hashes[first];
            }
            distinct_hashes += first_occurrence;
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                const quality_difference_t difference = quality_compare(
                    expression_frames[first],
                    expression_frames[second]);
                pair_sum += difference.roi_delta;
                weak_pairs +=
                    difference.roi_delta < 0.006 ||
                    difference.roi_changed_fraction < 0.015;
            }
        }
        const double mean_pair_delta = pair_sum / 55.0;
        if (distinct_hashes < 9U || clear_nonneutral < 8U ||
            mean_pair_delta < 0.010) {
            fprintf(
                stderr,
                "%s production expression gate: distinct=%u "
                "clear=%u weak=%u mean=%.6f\n",
                face_pixel_pack_profile_slug(profile),
                distinct_hashes, clear_nonneutral,
                weak_pairs, mean_pair_delta);
            quality_ok = false;
        }

        const face_stage_cue_t cue = quality_motion_cue();
        face_render_key_t key = quality_motion_key(0U);
        (void)face_stage_cue_apply(&cue, 0U, &key);
        assert(face_pixel_pack_render(
            profile, &key, 0U, motion_previous,
            FACE_PIXEL_PACK_PIXEL_COUNT));
        unsigned abrupt_jumps = 0U;
        for (uint32_t frame = 1U; frame < MOTION_FRAMES; ++frame) {
            const uint32_t sample_clock =
                (uint32_t)(
                    (uint64_t)frame * SAMPLE_RATE / 30U);
            key = quality_motion_key(frame);
            (void)face_stage_cue_apply(&cue, sample_clock, &key);
            assert(face_pixel_pack_render(
                profile, &key, sample_clock,
                motion_current, FACE_PIXEL_PACK_PIXEL_COUNT));
            const quality_difference_t difference =
                quality_compare(motion_previous, motion_current);
            if (difference.roi_delta > 0.035 &&
                difference.roi_changed_fraction >= 0.08) {
                ++abrupt_jumps;
                fprintf(
                    stderr,
                    "%s abrupt frame %u: delta=%.6f changed=%.6f\n",
                    face_pixel_pack_profile_slug(profile), frame,
                    difference.roi_delta,
                    difference.roi_changed_fraction);
            }
            memcpy(
                motion_previous, motion_current,
                sizeof(motion_previous));
        }
        if (abrupt_jumps != 0U) {
            fprintf(
                stderr, "%s production motion gate: %u abrupt jumps\n",
                face_pixel_pack_profile_slug(profile), abrupt_jumps);
            quality_ok = false;
        }
    }
    assert(quality_ok);
}

int main(void)
{
    test_public_contract();
    test_profiles_are_bounded_character_art();
    test_all_stage_expressions_are_visibly_separable();
    test_pcm_and_visemes_keep_driving_the_mouth();
    test_extreme_ir_bytes_stay_bounded();
    test_clock_motion_is_smooth_not_frozen();
    test_production_quality_gate();
    puts("face_pixel_pack_test: PASS");
    return 0;
}
