#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_abstract_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    AR_TEST_GUARD_WORDS = 16,
    AR_TEST_FUZZ_CASES = 256,
    AR_TEST_EXPRESSION_COUNT = 11,
    AR_TEST_SAMPLE_RATE = 16000,
    AR_TEST_MOTION_FPS = 30,
    AR_TEST_MOTION_FRAMES = 180,
};

typedef struct {
    uint32_t before[AR_TEST_GUARD_WORDS];
    uint16_t pixels[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    uint32_t after[AR_TEST_GUARD_WORDS];
} ar_guarded_frame_t;

static uint32_t ar_test_random_state = 0x6a09e667U;

static uint32_t ar_test_random(void)
{
    uint32_t value = ar_test_random_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    ar_test_random_state = value;
    return value;
}

static face_render_key_t ar_test_key(uint8_t expression)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 40U;
    key.controls.mouth_width = 146U;
    key.controls.mouth_round = 20U;
    key.controls.mouth_press = 48U;
    key.controls.eye_left_open = 220U;
    key.controls.eye_right_open = 216U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.cheek = 18U;
    key.affect_arousal = 118U;
    key.expression_weight = 255U;
    key.attention = 214U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static face_render_key_t ar_test_speech_key(
    uint8_t phase, uint8_t drive)
{
    face_render_key_t key = ar_test_key(FACE_EXPRESSION_WARM);
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.mouth_open = drive;
    key.controls.mouth_width = 176U;
    key.controls.mouth_round = 42U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 126U;
    key.controls.look_x = -18;
    key.controls.look_y = 8;
    key.viseme = FACE_VISEME_AA;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_weight = drive;
    key.viseme_blend = 48U;
    key.audio_level = 144U;
    key.speech_phase = phase;
    key.tongue = 84U;
    key.cheek = 48U;
    return key;
}

static void ar_test_init_guard(ar_guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < AR_TEST_GUARD_WORDS; ++index) {
        frame->before[index] = 0x5a110000U + (uint32_t)index;
        frame->after[index] = 0x11a50000U + (uint32_t)index;
    }
}

static void ar_test_assert_guard(const ar_guarded_frame_t *frame)
{
    for (size_t index = 0U; index < AR_TEST_GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x5a110000U + index);
        assert(frame->after[index] == 0x11a50000U + index);
    }
}

static uint32_t ar_test_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_ABSTRACT_REDUX_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t ar_test_contact_diff(
    const uint16_t *first, const uint16_t *second)
{
    size_t changed = 0U;
    for (size_t y = 2U;
         y < FACE_ABSTRACT_REDUX_HEIGHT;
         y += 4U) {
        for (size_t x = 2U;
             x < FACE_ABSTRACT_REDUX_WIDTH;
             x += 4U) {
            changed +=
                first[y * FACE_ABSTRACT_REDUX_WIDTH + x] !=
                second[y * FACE_ABSTRACT_REDUX_WIDTH + x];
        }
    }
    return changed;
}

static void ar_test_assert_pose_bounds(
    const face_abstract_redux_pose_t *pose)
{
    assert(pose->face_center_x == 80);
    assert(pose->face_center_y >= 53 && pose->face_center_y <= 59);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->eye_center_x[eye] >= 50);
        assert(pose->eye_center_x[eye] <= 110);
        assert(pose->eye_center_y[eye] >= 53);
        assert(pose->eye_center_y[eye] <= 59);
        assert(pose->eye_open[eye] >= 1);
        assert(pose->eye_open[eye] <= 31);
        assert(pose->brow_y[eye] >= 14);
        assert(pose->brow_y[eye] <= 43);
        assert(pose->brow_slope[eye] >= -11);
        assert(pose->brow_slope[eye] <= 11);
    }
    assert(pose->mouth_center_x == 80);
    assert(pose->mouth_center_y >= 84);
    assert(pose->mouth_center_y <= 92);
    assert(pose->gaze_x >= -12 && pose->gaze_x <= 12);
    assert(pose->gaze_y >= -8 && pose->gaze_y <= 8);
    assert(pose->mouth_width >= 14 && pose->mouth_width <= 70);
    assert(pose->mouth_height >= 2 && pose->mouth_height <= 32);
    assert(pose->mouth_corner[0] >= -12);
    assert(pose->mouth_corner[0] <= 12);
    assert(pose->mouth_corner[1] >= -12);
    assert(pose->mouth_corner[1] <= 12);
}

static void ar_test_metadata_and_errors(void)
{
    static const uint8_t legacy_ids[FACE_ABSTRACT_REDUX_COUNT] = {
        30U, 31U, 32U, 34U, 37U,
    };
    assert(sizeof(face_render_key_t) == FACE_RENDER_KEY_BYTES);
    assert(FACE_RENDER_KEY_BYTES == 40);
    assert(face_abstract_redux_count() == FACE_ABSTRACT_REDUX_COUNT);
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        const face_abstract_redux_style_t style =
            (face_abstract_redux_style_t)raw;
        face_abstract_redux_info_t info;
        face_abstract_redux_style_t mapped =
            FACE_ABSTRACT_REDUX_COUNT;
        assert(face_abstract_redux_info(style, &info));
        assert(info.legacy_profile_id == legacy_ids[raw]);
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(strcmp(info.slug, face_abstract_redux_slug(style)) == 0);
        assert(strcmp(info.name, face_abstract_redux_name(style)) == 0);
        assert(face_abstract_redux_from_legacy_id(
            legacy_ids[raw], &mapped));
        assert(mapped == style);
    }
    face_render_key_t key = ar_test_key(FACE_EXPRESSION_NEUTRAL);
    uint16_t pixels[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    face_abstract_redux_pose_t pose;
    assert(!face_abstract_redux_info(
        FACE_ABSTRACT_REDUX_COUNT, NULL));
    assert(!face_abstract_redux_from_legacy_id(33U, NULL));
    assert(!face_abstract_redux_resolve(
        FACE_ABSTRACT_REDUX_COUNT, &key, 0U, &pose));
    assert(!face_abstract_redux_resolve(
        FACE_ABSTRACT_REDUX_NEON_RIBBON, NULL, 0U, &pose));
    assert(!face_abstract_redux_render(
        FACE_ABSTRACT_REDUX_NEON_RIBBON,
        &key,
        0U,
        pixels,
        FACE_ABSTRACT_REDUX_PIXEL_COUNT - 1U));
    assert(!face_abstract_redux_render_legacy(
        33U,
        &key,
        0U,
        pixels,
        FACE_ABSTRACT_REDUX_PIXEL_COUNT));
}

static void ar_test_full_ir_and_fuzz(void)
{
    const face_render_key_t baseline =
        ar_test_speech_key(FACE_SPEECH_ACTIVE, 210U);
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        face_abstract_redux_pose_t reference;
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &baseline,
            3200U,
            &reference));
        assert(memcmp(
            &reference.source, &baseline, sizeof(baseline)) == 0);
        ar_test_assert_pose_bounds(&reference);
        for (size_t offset = 0U;
             offset < FACE_RENDER_KEY_BYTES;
             ++offset) {
            face_render_key_t changed = baseline;
            ((uint8_t *)&changed)[offset] ^=
                (uint8_t)(0x5bU + offset);
            face_abstract_redux_pose_t pose;
            assert(face_abstract_redux_resolve(
                (face_abstract_redux_style_t)raw,
                &changed,
                3200U,
                &pose));
            assert(memcmp(
                &pose.source, &changed, sizeof(changed)) == 0);
            assert(pose.input_signature != reference.input_signature);
            ar_test_assert_pose_bounds(&pose);
        }
    }
    for (size_t iteration = 0U;
         iteration < AR_TEST_FUZZ_CASES;
         ++iteration) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0U; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)ar_test_random();
        }
        for (size_t raw = 0U;
             raw < FACE_ABSTRACT_REDUX_COUNT;
             ++raw) {
            ar_guarded_frame_t frame;
            face_abstract_redux_pose_t pose;
            ar_test_init_guard(&frame);
            assert(face_abstract_redux_resolve(
                (face_abstract_redux_style_t)raw,
                &key,
                ar_test_random(),
                &pose));
            ar_test_assert_pose_bounds(&pose);
            assert(face_abstract_redux_render(
                (face_abstract_redux_style_t)raw,
                &key,
                ar_test_random(),
                frame.pixels,
                FACE_ABSTRACT_REDUX_PIXEL_COUNT));
            ar_test_assert_guard(&frame);
        }
    }
}

static void ar_test_expression_readability(void)
{
    uint16_t frames[AR_TEST_EXPRESSION_COUNT]
        [FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        face_abstract_redux_pose_t neutral_pose;
        face_render_key_t neutral_key =
            ar_test_key(FACE_EXPRESSION_NEUTRAL);
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &neutral_key,
            0U,
            &neutral_pose));
        for (uint8_t expression = 0U;
             expression < AR_TEST_EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key = ar_test_key(expression);
            face_abstract_redux_pose_t pose;
            assert(face_abstract_redux_resolve(
                (face_abstract_redux_style_t)raw,
                &key,
                0U,
                &pose));
            assert(face_abstract_redux_render(
                (face_abstract_redux_style_t)raw,
                &key,
                0U,
                frames[expression],
                FACE_ABSTRACT_REDUX_PIXEL_COUNT));
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                const int eye_brow_delta =
                    (pose.eye_open[0] != neutral_pose.eye_open[0]) +
                    (pose.eye_open[1] != neutral_pose.eye_open[1]) +
                    (pose.brow_y[0] != neutral_pose.brow_y[0]) +
                    (pose.brow_y[1] != neutral_pose.brow_y[1]) +
                    (pose.brow_slope[0] !=
                        neutral_pose.brow_slope[0]) +
                    (pose.brow_slope[1] !=
                        neutral_pose.brow_slope[1]) +
                    (pose.gaze_x != neutral_pose.gaze_x) +
                    (pose.gaze_y != neutral_pose.gaze_y);
                assert(eye_brow_delta >= 2);
                assert(ar_test_contact_diff(
                    frames[FACE_EXPRESSION_NEUTRAL],
                    frames[expression]) >= 4U);
            }
        }
        for (size_t first = 0U;
             first < AR_TEST_EXPRESSION_COUNT;
             ++first) {
            for (size_t second = first + 1U;
                 second < AR_TEST_EXPRESSION_COUNT;
                 ++second) {
                assert(ar_test_hash(frames[first]) !=
                    ar_test_hash(frames[second]));
            }
        }
    }
}

static void ar_test_temporal_acting(void)
{
    static const uint8_t starting_drive[3] = {
        32U, 104U, 192U,
    };
    static const uint8_t ending_drive[3] = {
        192U, 104U, 32U,
    };
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        face_abstract_redux_pose_t rest;
        face_render_key_t rest_key =
            ar_test_key(FACE_EXPRESSION_WARM);
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &rest_key,
            0U,
            &rest));
        face_abstract_redux_pose_t starting[3];
        face_abstract_redux_pose_t ending[3];
        for (size_t frame = 0U; frame < 3U; ++frame) {
            face_render_key_t key = ar_test_speech_key(
                FACE_SPEECH_STARTING, starting_drive[frame]);
            assert(face_abstract_redux_resolve(
                (face_abstract_redux_style_t)raw,
                &key,
                (uint32_t)frame * 533U,
                &starting[frame]));
            key = ar_test_speech_key(
                FACE_SPEECH_ENDING, ending_drive[frame]);
            assert(face_abstract_redux_resolve(
                (face_abstract_redux_style_t)raw,
                &key,
                (uint32_t)(frame + 20U) * 533U,
                &ending[frame]));
        }
        assert(starting[0].anticipation_q8 <
            starting[1].anticipation_q8);
        assert(starting[1].anticipation_q8 <
            starting[2].anticipation_q8);
        assert(starting[0].mouth_height <=
            starting[1].mouth_height);
        assert(starting[1].mouth_height <=
            starting[2].mouth_height);
        assert(starting[0].eye_open[0] != rest.eye_open[0] ||
            starting[0].brow_y[0] != rest.brow_y[0] ||
            starting[0].gaze_y != rest.gaze_y);
        assert(starting[2].eye_open[0] >=
            starting[0].eye_open[0]);
        assert(ending[0].settle_q8 < ending[1].settle_q8);
        assert(ending[1].settle_q8 < ending[2].settle_q8);
        assert(ending[0].mouth_height >= ending[1].mouth_height);
        assert(ending[1].mouth_height >= ending[2].mouth_height);

        face_render_key_t quiet =
            ar_test_speech_key(FACE_SPEECH_ACTIVE, 204U);
        face_render_key_t loud = quiet;
        quiet.audio_level = 0U;
        loud.audio_level = 255U;
        face_abstract_redux_pose_t quiet_pose;
        face_abstract_redux_pose_t loud_pose;
        uint16_t quiet_pixels[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
        uint16_t loud_pixels[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &quiet,
            6400U,
            &quiet_pose));
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &loud,
            6400U,
            &loud_pose));
        assert(quiet_pose.speech_envelope_q8 ==
            loud_pose.speech_envelope_q8);
        assert(quiet_pose.mouth_height == loud_pose.mouth_height);
        assert(quiet_pose.eye_open[0] == loud_pose.eye_open[0]);
        assert(quiet_pose.brow_y[0] == loud_pose.brow_y[0]);
        assert(face_abstract_redux_render(
            (face_abstract_redux_style_t)raw,
            &quiet,
            6400U,
            quiet_pixels,
            FACE_ABSTRACT_REDUX_PIXEL_COUNT));
        assert(face_abstract_redux_render(
            (face_abstract_redux_style_t)raw,
            &loud,
            6400U,
            loud_pixels,
            FACE_ABSTRACT_REDUX_PIXEL_COUNT));
        assert(memcmp(
            quiet_pixels,
            loud_pixels,
            sizeof(quiet_pixels)) == 0);
    }
}

static void ar_test_fixed_face_anchors(void)
{
    face_render_key_t first =
        ar_test_key(FACE_EXPRESSION_THOUGHTFUL);
    face_render_key_t second = first;
    first.head_roll = -127;
    first.head_pitch = -127;
    first.body_lean_x = -127;
    first.body_lean_y = -127;
    second.head_roll = 127;
    second.head_pitch = 127;
    second.body_lean_x = 127;
    second.body_lean_y = 127;
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        face_abstract_redux_pose_t first_pose;
        face_abstract_redux_pose_t second_pose;
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &first,
            0U,
            &first_pose));
        assert(face_abstract_redux_resolve(
            (face_abstract_redux_style_t)raw,
            &second,
            0U,
            &second_pose));
        assert(first_pose.face_center_x == second_pose.face_center_x);
        assert(first_pose.face_center_y == second_pose.face_center_y);
        assert(first_pose.mouth_center_x ==
            second_pose.mouth_center_x);
        assert(first_pose.mouth_center_y ==
            second_pose.mouth_center_y);
        assert(memcmp(
            first_pose.eye_center_x,
            second_pose.eye_center_x,
            sizeof(first_pose.eye_center_x)) == 0);
        assert(memcmp(
            first_pose.eye_center_y,
            second_pose.eye_center_y,
            sizeof(first_pose.eye_center_y)) == 0);
    }
}

static uint8_t ar_test_triangle_u8(uint32_t frame, uint32_t period)
{
    const uint32_t phase = frame % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

static face_render_key_t ar_test_quality_motion_key(uint32_t frame)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    const uint8_t jaw_wave = ar_test_triangle_u8(frame + 7U, 42U);
    const uint8_t form_wave = ar_test_triangle_u8(frame + 19U, 74U);
    const uint8_t gaze_wave = ar_test_triangle_u8(frame + 11U, 120U);
    key.controls.mouth_open =
        (uint8_t)(24U + (uint16_t)jaw_wave * 204U / 255U);
    key.controls.mouth_width =
        (uint8_t)(104U + (uint16_t)form_wave * 104U / 255U);
    key.controls.mouth_round =
        (uint8_t)(218U - (uint16_t)form_wave * 174U / 255U);
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth =
        (uint8_t)(24U + (uint16_t)jaw_wave * 104U / 255U);
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = (int8_t)((int32_t)gaze_wave / 3 - 42);
    key.controls.look_y = (int8_t)(
        (int32_t)ar_test_triangle_u8(frame + 37U, 156U) / 6 - 21);
    key.controls.expression = FACE_EXPRESSION_NEUTRAL;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.viseme_weight = 220U;
    key.audio_level =
        (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = form_wave;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.head_roll = (int8_t)(
        (int32_t)ar_test_triangle_u8(frame + 13U, 180U) / 12 - 10);
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t ar_test_quality_motion_cue(void)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_sample = AR_TEST_SAMPLE_RATE;
    cue.attack_samples = AR_TEST_SAMPLE_RATE;
    cue.hold_samples = AR_TEST_SAMPLE_RATE * 2U;
    cue.release_samples = AR_TEST_SAMPLE_RATE;
    cue.cue_id = 100U + FACE_EXPRESSION_JOY;
    cue.expression = FACE_EXPRESSION_JOY;
    cue.gesture = FACE_GESTURE_NOD;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 238U;
    return cue;
}

static uint8_t ar_test_expand5(uint16_t pixel)
{
    const uint8_t value = (uint8_t)(pixel & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t ar_test_expand6(uint16_t pixel)
{
    const uint8_t value = (uint8_t)(pixel & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static void ar_test_assert_quality_motion_continuity(void)
{
    uint16_t previous[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    uint16_t current[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    const face_stage_cue_t cue = ar_test_quality_motion_cue();
    for (uint32_t frame = 0U;
         frame < AR_TEST_MOTION_FRAMES;
         ++frame) {
        const uint32_t sample_clock = (uint32_t)(
            (uint64_t)frame * AR_TEST_SAMPLE_RATE /
            AR_TEST_MOTION_FPS);
        face_render_key_t key = ar_test_quality_motion_key(frame);
        (void)face_stage_cue_apply(&cue, sample_clock, &key);
        assert(face_abstract_redux_render(
            FACE_ABSTRACT_REDUX_VOICE_ORBIT,
            &key,
            sample_clock,
            current,
            FACE_ABSTRACT_REDUX_PIXEL_COUNT));
        if (frame > 0U) {
            uint64_t channel_delta = 0U;
            size_t changed = 0U;
            size_t roi_pixels = 0U;
            for (size_t y = 10U;
                 y < FACE_ABSTRACT_REDUX_HEIGHT - 10U;
                 ++y) {
                for (size_t x = 16U;
                     x < FACE_ABSTRACT_REDUX_WIDTH - 16U;
                     ++x) {
                    const size_t pixel =
                        y * FACE_ABSTRACT_REDUX_WIDTH + x;
                    const uint16_t first = previous[pixel];
                    const uint16_t second = current[pixel];
                    const uint8_t first_red =
                        ar_test_expand5(first >> 11U);
                    const uint8_t second_red =
                        ar_test_expand5(second >> 11U);
                    const uint8_t first_green =
                        ar_test_expand6(first >> 5U);
                    const uint8_t second_green =
                        ar_test_expand6(second >> 5U);
                    const uint8_t first_blue =
                        ar_test_expand5(first);
                    const uint8_t second_blue =
                        ar_test_expand5(second);
                    channel_delta += first_red > second_red
                        ? first_red - second_red
                        : second_red - first_red;
                    channel_delta += first_green > second_green
                        ? first_green - second_green
                        : second_green - first_green;
                    channel_delta += first_blue > second_blue
                        ? first_blue - second_blue
                        : second_blue - first_blue;
                    changed += first != second;
                    ++roi_pixels;
                }
            }
            const uint64_t channel_denominator =
                (uint64_t)roi_pixels * 3U * 255U;
            const bool large_delta =
                channel_delta * 1000U >
                channel_denominator * 35U;
            const bool broad_change =
                changed * 100U >= roi_pixels * 8U;
            assert(!(large_delta && broad_change));
        }
        memcpy(previous, current, sizeof(previous));
    }
}

int main(void)
{
    ar_test_metadata_and_errors();
    ar_test_full_ir_and_fuzz();
    ar_test_expression_readability();
    ar_test_temporal_acting();
    ar_test_fixed_face_anchors();
    ar_test_assert_quality_motion_continuity();
    puts("face_abstract_redux_actors_test: all checks passed");
    return 0;
}
