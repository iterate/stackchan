#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_mouth_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    MSR_TEST_GUARD_WORDS = 32,
    MSR_TEST_FUZZ_CASES = 256,
};

typedef struct {
    uint32_t before[MSR_TEST_GUARD_WORDS];
    uint16_t pixels[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
    uint32_t after[MSR_TEST_GUARD_WORDS];
} msr_guarded_frame_t;

static uint16_t expression_frames
    [FACE_EXPRESSION_COUNT][FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
static uint16_t comparison[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];

static face_render_key_t msr_baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 76U;
    key.controls.mouth_width = 142U;
    key.controls.mouth_round = 24U;
    key.controls.mouth_press = 8U;
    key.controls.mouth_teeth = 128U;
    key.controls.eye_left_open = 235U;
    key.controls.eye_right_open = 231U;
    key.controls.look_x = 2;
    key.controls.look_y = -1;
    key.controls.brow = 4;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 220U;
    key.audio_level = 86U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.viseme_blend = 0U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 3;
    key.mouth_corner_right = 5;
    key.tongue = 24U;
    key.cheek = 25U;
    key.eye_left_squint = 5U;
    key.eye_right_squint = 7U;
    key.brow_inner = 3;
    key.brow_outer_left = -2;
    key.brow_outer_right = 2;
    key.head_roll = 1;
    key.affect_valence = 7;
    key.affect_arousal = 126U;
    key.head_yaw = 2;
    key.head_pitch = -2;
    key.body_lean_x = 1;
    key.body_lean_y = -1;
    key.expression_weight = 255U;
    key.attention = 225U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static uint32_t msr_open_clock(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    face_mouth_study_redux_pose_t pose;
    for (uint32_t time_ms = 0U; time_ms < 16000U; time_ms += 25U) {
        assert(face_mouth_study_redux_resolve(
            profile, key, time_ms * 16U, &pose));
        if (pose.blink_q8 == 255U) {
            return time_ms * 16U;
        }
    }
    assert(false);
    return 0U;
}

static void msr_init_guard(msr_guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < MSR_TEST_GUARD_WORDS; ++index) {
        frame->before[index] = 0x37c10000U + (uint32_t)index;
        frame->after[index] = 0xc83e0000U + (uint32_t)index;
    }
}

static void msr_assert_guard(const msr_guarded_frame_t *frame)
{
    for (size_t index = 0U; index < MSR_TEST_GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x37c10000U + index);
        assert(frame->after[index] == 0xc83e0000U + index);
    }
}

static uint32_t msr_hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t msr_differing_pixels(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t differing = 0U;
    for (size_t index = 0U;
         index < FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT;
         ++index) {
        differing += first[index] != second[index];
    }
    return differing;
}

static uint32_t msr_hash_region(
    const uint16_t *pixels,
    size_t left,
    size_t top,
    size_t right,
    size_t bottom)
{
    uint32_t hash = 2166136261U;
    for (size_t y = top; y < bottom; ++y) {
        for (size_t x = left; x < right; ++x) {
            hash ^= pixels[
                y * FACE_MOUTH_STUDY_REDUX_WIDTH + x];
            hash *= 16777619U;
        }
    }
    return hash;
}

static size_t msr_differing_region(
    const uint16_t *first,
    const uint16_t *second,
    size_t left,
    size_t top,
    size_t right,
    size_t bottom)
{
    size_t differing = 0U;
    for (size_t y = top; y < bottom; ++y) {
        for (size_t x = left; x < right; ++x) {
            const size_t pixel =
                y * FACE_MOUTH_STUDY_REDUX_WIDTH + x;
            differing += first[pixel] != second[pixel];
        }
    }
    return differing;
}

static bool msr_bounds_inside(
    face_mouth_study_redux_bounds_t bounds,
    unsigned margin)
{
    return bounds.width > 0U && bounds.height > 0U &&
        bounds.x >= margin && bounds.y >= margin &&
        (unsigned)bounds.x + bounds.width <=
            FACE_MOUTH_STUDY_REDUX_WIDTH - margin &&
        (unsigned)bounds.y + bounds.height <=
            FACE_MOUTH_STUDY_REDUX_HEIGHT - margin;
}

static bool msr_point_in_bounds(
    size_t x,
    size_t y,
    face_mouth_study_redux_bounds_t bounds)
{
    return x >= bounds.x && y >= bounds.y &&
        x < (size_t)bounds.x + bounds.width &&
        y < (size_t)bounds.y + bounds.height;
}

static void msr_assert_pose_bounds(
    face_mouth_study_redux_profile_t profile,
    const face_mouth_study_redux_pose_t *pose)
{
    assert(pose->face_center_x == 80);
    assert(pose->face_center_y == 59);
    assert(pose->left_eye_x >= 50 && pose->left_eye_x <= 53);
    assert(pose->right_eye_x >= 107 && pose->right_eye_x <= 110);
    assert(pose->eye_y >= 37 && pose->eye_y <= 46);
    assert(pose->gaze_x >= -9 && pose->gaze_x <= 9);
    assert(pose->gaze_y >= -6 && pose->gaze_y <= 6);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->eye_open[eye] >= 2 &&
            pose->eye_open[eye] <= 21);
        assert(pose->brow_raise[eye] >= -7 &&
            pose->brow_raise[eye] <= 11);
        assert(pose->brow_slant[eye] >= -9 &&
            pose->brow_slant[eye] <= 9);
    }
    assert(pose->mouth_center_x >= 75 &&
        pose->mouth_center_x <= 87);
    assert(pose->mouth_center_y >= 78 &&
        pose->mouth_center_y <= 87);
    assert(pose->mouth_open >= 2 && pose->mouth_open <= 34);
    assert(pose->mouth_width >= 27 && pose->mouth_width <= 72);
    assert(pose->mouth_width >= pose->mouth_open + 16);
    assert(pose->mouth_round_q8 >= 0 &&
        pose->mouth_round_q8 <= 255);
    assert(pose->lip_press_q8 >= 0 &&
        pose->lip_press_q8 <= 255);
    assert(pose->corner_y[0] >= -11 && pose->corner_y[0] <= 11);
    assert(pose->corner_y[1] >= -11 && pose->corner_y[1] <= 11);
    assert(pose->cheek_lift >= -4 && pose->cheek_lift <= 12);
    assert(pose->jaw_drop >= 2 && pose->jaw_drop <= 25);
    assert(pose->teeth_q8 >= 0 && pose->teeth_q8 <= 255);
    assert(pose->tongue_q8 >= 0 && pose->tongue_q8 <= 255);
    assert(pose->tongue_x >= -5 && pose->tongue_x <= 5);
    assert(pose->jaw_skew >= 0 && pose->jaw_skew <= 5);
    assert(pose->mouth_anchor_x[0] >= 39);
    assert(pose->mouth_anchor_x[1] <= 123);
    assert(pose->mouth_anchor_y[0] >= 67);
    assert(pose->mouth_anchor_y[0] <= 98);
    assert(pose->mouth_anchor_y[1] >= 67);
    assert(pose->mouth_anchor_y[1] <= 98);
    for (size_t side = 0U; side < 2U; ++side) {
        if (profile == FACE_MOUTH_STUDY_REDUX_JALI ||
            profile == FACE_MOUTH_STUDY_REDUX_ORIGAMI) {
            assert(pose->cheek_x[side] ==
                pose->mouth_anchor_x[side]);
            assert(pose->cheek_y[side] ==
                pose->mouth_anchor_y[side]);
        } else {
            const int32_t direction = side == 0U ? -1 : 1;
            assert(pose->mouth_anchor_x[side] ==
                pose->mouth_center_x +
                    direction * (pose->mouth_width / 2));
            assert(pose->cheek_x[side] ==
                pose->mouth_center_x +
                    direction * (pose->mouth_width / 2 + 8));
            assert(pose->cheek_y[side] ==
                pose->mouth_center_y + pose->corner_y[side] -
                    7 - pose->cheek_lift / 2);
        }
    }
}

static uint32_t msr_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void test_metadata_mapping_and_rejection(void)
{
    static const char *const EXPECTED_SLUGS[6] = {
        "preston-sprites-redux",
        "polygon-jali-redux",
        "bezier-ribbon-redux",
        "teeth-tongue-redux",
        "led-vu-mouth-redux",
        "origami-mask-redux",
    };
    assert(face_mouth_study_redux_profile_count() == 6U);
    for (size_t index = 0U; index < 6U; ++index) {
        face_mouth_study_redux_profile_t profile;
        face_mouth_study_redux_profile_t mapped;
        assert(face_mouth_study_redux_profile_from_index(index, &profile));
        assert((uint8_t)profile == 23U + index);
        assert(face_mouth_study_redux_profile_from_legacy_id(
            (uint8_t)(23U + index), &mapped));
        assert(mapped == profile);
        assert(strcmp(
            face_mouth_study_redux_profile_slug(profile),
            EXPECTED_SLUGS[index]) == 0);
        assert(face_mouth_study_redux_profile_name(profile) != NULL);
    }
    face_mouth_study_redux_profile_t profile;
    assert(!face_mouth_study_redux_profile_from_index(6U, &profile));
    assert(!face_mouth_study_redux_profile_from_index(0U, NULL));
    assert(!face_mouth_study_redux_profile_from_legacy_id(22U, &profile));
    assert(!face_mouth_study_redux_profile_from_legacy_id(29U, &profile));
    assert(!face_mouth_study_redux_profile_from_legacy_id(23U, NULL));
    assert(face_mouth_study_redux_profile_slug(
        (face_mouth_study_redux_profile_t)22) == NULL);
    assert(face_mouth_study_redux_profile_name(
        (face_mouth_study_redux_profile_t)29) == NULL);

    const face_render_key_t key = msr_baseline_key();
    uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
    face_mouth_study_redux_pose_t pose;
    assert(!face_mouth_study_redux_resolve(
        (face_mouth_study_redux_profile_t)22, &key, 0U, &pose));
    assert(!face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_PRESTON, NULL, 0U, &pose));
    assert(!face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_PRESTON, &key, 0U, NULL));
    assert(!face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_PRESTON, &key, 0U, NULL,
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    assert(!face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_PRESTON, &key, 0U, frame,
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT - 1U));
}

static void test_determinism_and_full_ir(void)
{
    const face_render_key_t key = msr_baseline_key();
    msr_guarded_frame_t first;
    msr_guarded_frame_t second;
    msr_init_guard(&first);
    msr_init_guard(&second);
    assert(face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_RIBBON,
        &key, 947113U, first.pixels,
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    assert(face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_RIBBON,
        &key, 947113U, second.pixels,
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    assert(memcmp(first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    msr_assert_guard(&first);
    msr_assert_guard(&second);

    face_mouth_study_redux_pose_t original;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_RIBBON,
        &key, 947113U, &original));
    assert(memcmp(&original.source, &key, sizeof(key)) == 0);
    for (size_t byte = 0U; byte < FACE_RENDER_KEY_BYTES; ++byte) {
        face_render_key_t changed = key;
        ((uint8_t *)&changed)[byte] ^=
            (uint8_t)(0x5aU + (uint8_t)byte);
        face_mouth_study_redux_pose_t changed_pose;
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_RIBBON,
            &changed, 947113U, &changed_pose));
        assert(memcmp(
            &changed_pose.source, &changed, sizeof(changed)) == 0);
        assert(changed_pose.input_signature != original.input_signature);
    }
}

static void test_adversarial_bounds_canaries_and_landmarks(void)
{
    uint32_t random_state = 0x716e4a95U;
    for (size_t iteration = 0U;
         iteration < MSR_TEST_FUZZ_CASES;
         ++iteration) {
        face_render_key_t key;
        for (size_t byte = 0U; byte < sizeof(key); ++byte) {
            ((uint8_t *)&key)[byte] =
                (uint8_t)msr_random(&random_state);
        }
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(
                23U + iteration % 6U);
        face_mouth_study_redux_pose_t pose;
        face_mouth_study_redux_landmarks_t landmarks;
        msr_guarded_frame_t frame;
        msr_init_guard(&frame);
        assert(face_mouth_study_redux_resolve(
            profile, &key, msr_random(&random_state), &pose));
        msr_assert_pose_bounds(profile, &pose);
        assert(face_mouth_study_redux_render_resolved(
            profile, &pose, frame.pixels,
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT, &landmarks));
        msr_assert_guard(&frame);
        assert(msr_bounds_inside(landmarks.face, 3U));
        assert(msr_bounds_inside(landmarks.left_eye, 4U));
        assert(msr_bounds_inside(landmarks.right_eye, 4U));
        assert(msr_bounds_inside(landmarks.mouth, 2U));
        assert(msr_bounds_inside(landmarks.jaw, 1U));
        assert(landmarks.left_eye.x < landmarks.right_eye.x);
        assert(landmarks.left_eye.y < landmarks.mouth.y);
        assert(landmarks.right_eye.y < landmarks.mouth.y);
    }
}

static void test_parented_rig_and_speech_acting(void)
{
    for (size_t raw = 0U; raw < 6U; ++raw) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + raw);
        face_render_key_t rest = msr_baseline_key();
        rest.controls.flags = 0U;
        rest.controls.expression = FACE_ACTIVITY_LISTENING;
        rest.speech_phase = FACE_SPEECH_IDLE;
        rest.audio_level = 0U;
        const uint32_t clock = msr_open_clock(profile, &rest);
        face_render_key_t starting = rest;
        starting.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        starting.controls.expression = FACE_ACTIVITY_SPEAKING;
        starting.speech_phase = FACE_SPEECH_STARTING;
        starting.audio_level = 96U;
        face_render_key_t active = starting;
        active.speech_phase = FACE_SPEECH_ACTIVE;
        active.audio_level = 232U;
        active.controls.mouth_open = 220U;
        face_render_key_t ending = active;
        ending.speech_phase = FACE_SPEECH_ENDING;
        ending.audio_level = 54U;

        face_mouth_study_redux_pose_t poses[4];
        assert(face_mouth_study_redux_resolve(
            profile, &rest, clock, &poses[0]));
        assert(face_mouth_study_redux_resolve(
            profile, &starting, clock, &poses[1]));
        assert(face_mouth_study_redux_resolve(
            profile, &active, clock, &poses[2]));
        assert(face_mouth_study_redux_resolve(
            profile, &ending, clock, &poses[3]));
        for (size_t index = 1U; index < 4U; ++index) {
            assert(poses[index].face_center_x == poses[0].face_center_x);
            assert(poses[index].face_center_y == poses[0].face_center_y);
            assert(poses[index].left_eye_x == poses[0].left_eye_x);
            assert(poses[index].right_eye_x == poses[0].right_eye_x);
            msr_assert_pose_bounds(profile, &poses[index]);
        }
        assert(poses[1].mouth_open < poses[2].mouth_open);
        assert(poses[3].mouth_open < poses[2].mouth_open);
        assert(poses[1].mouth_width > poses[0].mouth_width);
        assert(poses[1].brow_raise[0] > poses[0].brow_raise[0]);
        assert(poses[3].gaze_y >= poses[2].gaze_y);
        assert(poses[2].jaw_drop > poses[0].jaw_drop);
    }
}

static void test_all_emotions_are_readable(void)
{
    face_render_key_t key = msr_baseline_key();
    for (size_t raw = 0U; raw < 6U; ++raw) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + raw);
        const uint32_t clock = msr_open_clock(profile, &key);
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        face_mouth_study_redux_pose_t poses[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            key.stage_expression = expression;
            assert(face_mouth_study_redux_resolve(
                profile, &key, clock, &poses[expression]));
            assert(face_mouth_study_redux_render_resolved(
                profile, &poses[expression],
                expression_frames[expression],
                FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT, NULL));
            hashes[expression] =
                msr_hash_pixels(expression_frames[expression]);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                assert(msr_differing_pixels(
                    expression_frames[FACE_EXPRESSION_NEUTRAL],
                    expression_frames[expression]) >= 80U);
                assert(
                    poses[expression].eye_open[0] !=
                        poses[FACE_EXPRESSION_NEUTRAL].eye_open[0] ||
                    poses[expression].eye_open[1] !=
                        poses[FACE_EXPRESSION_NEUTRAL].eye_open[1] ||
                    poses[expression].brow_raise[0] !=
                        poses[FACE_EXPRESSION_NEUTRAL].brow_raise[0] ||
                    poses[expression].brow_slant[0] !=
                        poses[FACE_EXPRESSION_NEUTRAL].brow_slant[0] ||
                    poses[expression].mouth_open !=
                        poses[FACE_EXPRESSION_NEUTRAL].mouth_open ||
                    poses[expression].mouth_width !=
                        poses[FACE_EXPRESSION_NEUTRAL].mouth_width ||
                    poses[expression].corner_y[0] !=
                        poses[FACE_EXPRESSION_NEUTRAL].corner_y[0] ||
                    poses[expression].corner_y[1] !=
                        poses[FACE_EXPRESSION_NEUTRAL].corner_y[1]);
            }
        }
        for (size_t first = 0U;
             first < FACE_EXPRESSION_COUNT;
             ++first) {
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                assert(hashes[first] != hashes[second]);
            }
        }
    }
}

static void msr_assert_unique_vocabulary(
    face_mouth_study_redux_profile_t profile,
    uint8_t set,
    size_t count)
{
    uint32_t hashes[FACE_VISEME_COUNT];
    face_render_key_t key = msr_baseline_key();
    key.viseme_set = set;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    key.controls.mouth_open = 52U;
    key.controls.mouth_width = 128U;
    key.controls.mouth_round = 0U;
    key.controls.mouth_press = 0U;
    key.controls.mouth_teeth = 0U;
    key.audio_level = 18U;
    const uint32_t clock = msr_open_clock(profile, &key);
    for (size_t viseme = 0U; viseme < count; ++viseme) {
        key.viseme = (uint8_t)viseme;
        assert(face_mouth_study_redux_render(
            profile, &key, clock, comparison,
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
        hashes[viseme] = msr_hash_pixels(comparison);
    }
    for (size_t first = 0U; first < count; ++first) {
        for (size_t second = first + 1U; second < count; ++second) {
            if (hashes[first] == hashes[second]) {
                fprintf(
                    stderr,
                    "duplicate vocabulary render: profile=%u set=%u "
                    "first=%zu second=%zu hash=%08" PRIx32 "\n",
                    (unsigned)profile,
                    (unsigned)set,
                    first,
                    second,
                    hashes[first]);
            }
            assert(hashes[first] != hashes[second]);
        }
    }
}

static void test_viseme_vocabularies_and_coarticulation(void)
{
    for (size_t raw = 0U; raw < 6U; ++raw) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + raw);
        msr_assert_unique_vocabulary(
            profile, FACE_VISEME_SET_OVR15, 15U);
        msr_assert_unique_vocabulary(
            profile, FACE_VISEME_SET_VRM5, 5U);
        msr_assert_unique_vocabulary(
            profile, FACE_VISEME_SET_PRESTON9, 9U);

        face_render_key_t key = msr_baseline_key();
        key.viseme = FACE_VISEME_AA;
        key.viseme_secondary = FACE_VISEME_U;
        key.viseme_weight = 255U;
        key.controls.mouth_open = 70U;
        key.controls.mouth_width = 128U;
        key.controls.mouth_round = 0U;
        key.controls.mouth_press = 0U;
        const uint32_t clock = msr_open_clock(profile, &key);
        size_t maximum_step_change = 0U;
        uint16_t previous[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
        key.viseme_blend = 0U;
        assert(face_mouth_study_redux_render(
            profile, &key, clock, previous,
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
        for (unsigned blend = 1U; blend <= 255U; ++blend) {
            key.viseme_blend = (uint8_t)blend;
            assert(face_mouth_study_redux_render(
                profile, &key, clock, comparison,
                FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
            const size_t change =
                msr_differing_pixels(previous, comparison);
            if (change > maximum_step_change) {
                maximum_step_change = change;
            }
            memcpy(previous, comparison, sizeof(previous));
        }
        assert(maximum_step_change < 650U);
    }
}

static void test_second_pass_acting_constraints(void)
{
    face_render_key_t key = msr_baseline_key();
    key.controls.mouth_open = 255U;
    key.controls.mouth_width = 220U;
    key.controls.mouth_press = 0U;
    key.viseme = FACE_VISEME_AA;
    key.viseme_weight = 255U;
    key.audio_level = 255U;
    key.stage_expression = FACE_EXPRESSION_SURPRISE;
    face_mouth_study_redux_pose_t ribbon_peak;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_RIBBON,
        &key, 64000U, &ribbon_peak));
    assert(ribbon_peak.mouth_open <= 23);
    assert(ribbon_peak.mouth_open * 3 < 72);

    static const uint8_t ANTICIPATION_WEIGHT[3] = {
        48U, 112U, 192U,
    };
    face_mouth_study_redux_pose_t anticipation[3];
    for (size_t frame = 0U; frame < 3U; ++frame) {
        key = msr_baseline_key();
        key.speech_phase = FACE_SPEECH_STARTING;
        key.viseme = FACE_VISEME_AA;
        key.viseme_weight = ANTICIPATION_WEIGHT[frame];
        key.controls.mouth_open =
            (uint8_t)(44U + frame * 42U);
        key.audio_level =
            (uint8_t)(24U + frame * 30U);
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_RIBBON,
            &key, 64000U, &anticipation[frame]));
    }
    assert(anticipation[0].anticipation_q8 <
        anticipation[1].anticipation_q8);
    assert(anticipation[1].anticipation_q8 <
        anticipation[2].anticipation_q8);
    assert(anticipation[0].mouth_open <=
        anticipation[1].mouth_open);
    assert(anticipation[1].mouth_open <=
        anticipation[2].mouth_open);
    assert(
        anticipation[0].eye_open[0] <
            anticipation[2].eye_open[0] ||
        anticipation[0].brow_raise[0] <
            anticipation[2].brow_raise[0] ||
        anticipation[0].cheek_lift <
            anticipation[2].cheek_lift);

    static const uint8_t SETTLE_WEIGHT[3] = {
        206U, 112U, 28U,
    };
    face_mouth_study_redux_pose_t settle[3];
    for (size_t frame = 0U; frame < 3U; ++frame) {
        key = msr_baseline_key();
        key.speech_phase = FACE_SPEECH_ENDING;
        key.viseme = FACE_VISEME_AA;
        key.viseme_weight = SETTLE_WEIGHT[frame];
        key.controls.mouth_open =
            (uint8_t)(174U - frame * 64U);
        key.audio_level =
            (uint8_t)(116U - frame * 48U);
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_RIBBON,
            &key, 64000U, &settle[frame]));
    }
    assert(settle[0].settle_q8 < settle[1].settle_q8);
    assert(settle[1].settle_q8 < settle[2].settle_q8);
    assert(settle[0].mouth_open >= settle[1].mouth_open);
    assert(settle[1].mouth_open >= settle[2].mouth_open);

    key = msr_baseline_key();
    key.controls.look_x = 127;
    key.controls.look_y = -127;
    key.head_yaw = 127;
    key.head_pitch = -127;
    face_mouth_study_redux_pose_t jali;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_JALI,
        &key, 64000U, &jali));
    assert(jali.eye_y == 42);
    assert(jali.gaze_x >= -5 && jali.gaze_x <= 5);
    assert(jali.gaze_y >= -3 && jali.gaze_y <= 3);
    assert(jali.mouth_anchor_x[0] == 53);
    assert(jali.mouth_anchor_x[1] == 107);
    assert(jali.cheek_x[0] == jali.mouth_anchor_x[0]);
    assert(jali.cheek_y[0] == jali.mouth_anchor_y[0]);
    assert(jali.cheek_x[1] == jali.mouth_anchor_x[1]);
    assert(jali.cheek_y[1] == jali.mouth_anchor_y[1]);

    key = msr_baseline_key();
    key.controls.flags = 0U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.audio_level = 0U;
    face_mouth_study_redux_pose_t monster_rest;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE,
        &key, 64000U, &monster_rest));
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.speech_phase = FACE_SPEECH_STARTING;
    key.viseme_weight = 176U;
    key.controls.mouth_open = 150U;
    key.audio_level = 92U;
    face_mouth_study_redux_pose_t monster_anticipation;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE,
        &key, 64000U, &monster_anticipation));
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.viseme_weight = 255U;
    key.controls.mouth_open = 255U;
    key.audio_level = 230U;
    face_mouth_study_redux_pose_t monster_peak;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE,
        &key, 64000U, &monster_peak));
    assert(monster_anticipation.mouth_open <
        monster_peak.mouth_open);
    assert(
        monster_anticipation.eye_open[0] >
            monster_rest.eye_open[0] ||
        monster_anticipation.eye_open[1] >
            monster_rest.eye_open[1] ||
        monster_anticipation.brow_raise[0] >
            monster_rest.brow_raise[0] ||
        monster_anticipation.brow_raise[1] >
            monster_rest.brow_raise[1]);
    key.speech_phase = FACE_SPEECH_ENDING;
    key.viseme_weight = 32U;
    key.controls.mouth_open = 28U;
    key.audio_level = 8U;
    face_mouth_study_redux_pose_t monster_settle;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE,
        &key, 64000U, &monster_settle));
    assert(monster_settle.jaw_skew >= 3);
    assert(monster_settle.corner_y[0] !=
        monster_settle.corner_y[1]);

    face_render_key_t quiet_led = msr_baseline_key();
    quiet_led.audio_level = 0U;
    face_render_key_t loud_led = quiet_led;
    loud_led.audio_level = 255U;
    face_mouth_study_redux_pose_t quiet_led_pose;
    face_mouth_study_redux_pose_t loud_led_pose;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_LED_VU,
        &quiet_led, 64000U, &quiet_led_pose));
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_LED_VU,
        &loud_led, 64000U, &loud_led_pose));
    assert(loud_led_pose.mouth_open - quiet_led_pose.mouth_open <= 3);
    assert(loud_led_pose.eye_open[0] >= quiet_led_pose.eye_open[0]);

    face_render_key_t first = msr_baseline_key();
    face_render_key_t second = first;
    first.head_yaw = -127;
    first.head_pitch = 127;
    first.body_lean_x = -127;
    first.body_lean_y = 127;
    second.head_yaw = 127;
    second.head_pitch = -127;
    second.body_lean_x = 127;
    second.body_lean_y = -127;
    face_mouth_study_redux_pose_t first_origami;
    face_mouth_study_redux_pose_t second_origami;
    face_mouth_study_redux_landmarks_t marks;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_ORIGAMI,
        &first, 64000U, &first_origami));
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_ORIGAMI,
        &second, 64000U, &second_origami));
    assert(first_origami.eye_y == second_origami.eye_y);
    assert(first_origami.mouth_center_x ==
        second_origami.mouth_center_x);
    assert(first_origami.mouth_center_y ==
        second_origami.mouth_center_y);
    assert(first_origami.mouth_anchor_x[0] ==
        second_origami.mouth_anchor_x[0]);
    assert(first_origami.mouth_anchor_x[1] ==
        second_origami.mouth_anchor_x[1]);
    assert(face_mouth_study_redux_render_resolved(
        FACE_MOUTH_STUDY_REDUX_ORIGAMI,
        &first_origami, expression_frames[0],
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT, &marks));
    assert(face_mouth_study_redux_render_resolved(
        FACE_MOUTH_STUDY_REDUX_ORIGAMI,
        &second_origami, expression_frames[1],
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT, NULL));
    size_t outside_eye_changes = 0U;
    for (size_t y = 0U; y < FACE_MOUTH_STUDY_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_MOUTH_STUDY_REDUX_WIDTH; ++x) {
            const size_t pixel =
                y * FACE_MOUTH_STUDY_REDUX_WIDTH + x;
            if (expression_frames[0][pixel] !=
                    expression_frames[1][pixel] &&
                !msr_point_in_bounds(x, y, marks.left_eye) &&
                !msr_point_in_bounds(x, y, marks.right_eye)) {
                ++outside_eye_changes;
            }
        }
    }
    assert(outside_eye_changes == 0U);

    for (size_t raw = 0U; raw < 6U; ++raw) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + raw);
        face_render_key_t rest_key = msr_baseline_key();
        rest_key.controls.flags = 0U;
        rest_key.speech_phase = FACE_SPEECH_IDLE;
        rest_key.audio_level = 0U;
        face_render_key_t active_key = rest_key;
        active_key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        active_key.speech_phase = FACE_SPEECH_ACTIVE;
        active_key.viseme_weight = 255U;
        active_key.audio_level = 190U;
        face_mouth_study_redux_pose_t rest_pose;
        face_mouth_study_redux_pose_t active_pose;
        assert(face_mouth_study_redux_resolve(
            profile, &rest_key, 64000U, &rest_pose));
        assert(face_mouth_study_redux_resolve(
            profile, &active_key, 64000U, &active_pose));
        assert(
            rest_pose.eye_open[0] != active_pose.eye_open[0] ||
            rest_pose.eye_open[1] != active_pose.eye_open[1] ||
            rest_pose.brow_raise[0] != active_pose.brow_raise[0] ||
            rest_pose.brow_raise[1] != active_pose.brow_raise[1] ||
            rest_pose.gaze_y != active_pose.gaze_y);
    }
}

static void test_third_pass_polished_actor_contracts(void)
{
    /*
     * Pip: every authored emotion must act through the upper face, not only
     * through a mouth swap.
     */
    face_render_key_t key = msr_baseline_key();
    key.controls.flags = 0U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.viseme = FACE_VISEME_SIL;
    key.viseme_weight = 0U;
    key.audio_level = 0U;
    const uint32_t pip_clock = msr_open_clock(
        FACE_MOUTH_STUDY_REDUX_PRESTON,
        &key);
    face_mouth_study_redux_pose_t pip_neutral;
    assert(face_mouth_study_redux_resolve(
        FACE_MOUTH_STUDY_REDUX_PRESTON,
        &key,
        pip_clock,
        &pip_neutral));
    for (uint8_t expression = FACE_EXPRESSION_WARM;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        face_mouth_study_redux_pose_t emotional;
        face_mouth_study_redux_pose_t weighted_off;
        key.stage_expression = expression;
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_PRESTON,
            &key,
            pip_clock,
            &emotional));
        assert(
            emotional.eye_open[0] != pip_neutral.eye_open[0] ||
            emotional.eye_open[1] != pip_neutral.eye_open[1] ||
            emotional.brow_raise[0] != pip_neutral.brow_raise[0] ||
            emotional.brow_raise[1] != pip_neutral.brow_raise[1] ||
            emotional.brow_slant[0] != pip_neutral.brow_slant[0] ||
            emotional.brow_slant[1] != pip_neutral.brow_slant[1] ||
            emotional.gaze_x != pip_neutral.gaze_x ||
            emotional.gaze_y != pip_neutral.gaze_y);
        key.expression_weight = 0U;
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_PRESTON,
            &key,
            pip_clock,
            &weighted_off));
        assert(weighted_off.eye_open[0] == pip_neutral.eye_open[0]);
        assert(weighted_off.eye_open[1] == pip_neutral.eye_open[1]);
        assert(weighted_off.brow_raise[0] ==
            pip_neutral.brow_raise[0]);
        assert(weighted_off.brow_raise[1] ==
            pip_neutral.brow_raise[1]);
        assert(weighted_off.brow_slant[0] ==
            pip_neutral.brow_slant[0]);
        assert(weighted_off.brow_slant[1] ==
            pip_neutral.brow_slant[1]);
        assert(weighted_off.gaze_x == pip_neutral.gaze_x);
        assert(weighted_off.gaze_y == pip_neutral.gaze_y);
        key.expression_weight = 255U;
    }

    /*
     * A dense coarticulation sweep catches the old one-frame tooth strobe.
     * Restrict the comparison to the mouth so unrelated eye acting cannot
     * hide an abrupt dental topology change.
     */
    key = msr_baseline_key();
    key.stage_expression = FACE_EXPRESSION_WARM;
    key.viseme = FACE_VISEME_AA;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_weight = 255U;
    key.controls.mouth_open = 92U;
    key.audio_level = 84U;
    uint16_t previous[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
    key.viseme_blend = 0U;
    assert(face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_PRESTON,
        &key,
        pip_clock,
        previous,
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    size_t pip_maximum_mouth_step = 0U;
    for (unsigned blend = 1U; blend <= 255U; ++blend) {
        key.viseme_blend = (uint8_t)blend;
        assert(face_mouth_study_redux_render(
            FACE_MOUTH_STUDY_REDUX_PRESTON,
            &key,
            pip_clock,
            comparison,
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
        const size_t change = msr_differing_region(
            previous,
            comparison,
            40U,
            66U,
            121U,
            113U);
        if (change > pip_maximum_mouth_step) {
            pip_maximum_mouth_step = change;
        }
        memcpy(previous, comparison, sizeof(previous));
    }
    assert(pip_maximum_mouth_step < 300U);

    /*
     * LED: loudness may energise acting elsewhere, but the same phoneme must
     * produce the same categorical block glyph.
     */
    face_render_key_t quiet_led = msr_baseline_key();
    quiet_led.viseme = FACE_VISEME_O;
    quiet_led.viseme_secondary = FACE_VISEME_NONE;
    quiet_led.viseme_weight = 255U;
    quiet_led.audio_level = 0U;
    face_render_key_t loud_led = quiet_led;
    loud_led.audio_level = 255U;
    const uint32_t led_clock = msr_open_clock(
        FACE_MOUTH_STUDY_REDUX_LED_VU,
        &quiet_led);
    assert(face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_LED_VU,
        &quiet_led,
        led_clock,
        expression_frames[0],
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    assert(face_mouth_study_redux_render(
        FACE_MOUTH_STUDY_REDUX_LED_VU,
        &loud_led,
        led_clock,
        expression_frames[1],
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
    assert(msr_differing_region(
        expression_frames[0],
        expression_frames[1],
        58U,
        68U,
        102U,
        98U) == 0U);

    uint32_t led_hashes[FACE_VISEME_COUNT];
    uint32_t origami_hashes[FACE_VISEME_COUNT];
    key = msr_baseline_key();
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    key.controls.mouth_open = 52U;
    key.controls.mouth_width = 128U;
    key.controls.mouth_round = 0U;
    key.controls.mouth_press = 0U;
    key.controls.mouth_teeth = 0U;
    key.audio_level = 18U;
    const uint32_t origami_clock = msr_open_clock(
        FACE_MOUTH_STUDY_REDUX_ORIGAMI,
        &key);
    for (uint8_t viseme = 0U;
         viseme < FACE_VISEME_COUNT;
         ++viseme) {
        face_mouth_study_redux_pose_t origami_pose;
        key.viseme = viseme;
        assert(face_mouth_study_redux_render(
            FACE_MOUTH_STUDY_REDUX_LED_VU,
            &key,
            led_clock,
            expression_frames[0],
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT));
        led_hashes[viseme] = msr_hash_region(
            expression_frames[0],
            47U,
            68U,
            112U,
            98U);
        assert(face_mouth_study_redux_resolve(
            FACE_MOUTH_STUDY_REDUX_ORIGAMI,
            &key,
            origami_clock,
            &origami_pose));
        assert(origami_pose.mouth_anchor_x[0] == 55);
        assert(origami_pose.mouth_anchor_x[1] == 105);
        assert(origami_pose.cheek_x[0] ==
            origami_pose.mouth_anchor_x[0]);
        assert(origami_pose.cheek_x[1] ==
            origami_pose.mouth_anchor_x[1]);
        assert(face_mouth_study_redux_render_resolved(
            FACE_MOUTH_STUDY_REDUX_ORIGAMI,
            &origami_pose,
            expression_frames[1],
            FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT,
            NULL));
        origami_hashes[viseme] = msr_hash_region(
            expression_frames[1],
            46U,
            68U,
            115U,
            111U);
    }
    for (size_t first = 0U;
         first < FACE_VISEME_COUNT;
         ++first) {
        for (size_t second = first + 1U;
             second < FACE_VISEME_COUNT;
             ++second) {
            assert(led_hashes[first] != led_hashes[second]);
            assert(origami_hashes[first] != origami_hashes[second]);
        }
    }
}

int main(void)
{
    _Static_assert(
        FACE_RENDER_KEY_BYTES == 40,
        "mouth study must preserve the complete 40-byte IR");
    _Static_assert(
        FACE_MOUTH_STUDY_REDUX_CONTEXT_BYTES == 0,
        "mouth study must remain stateless and heap-free");
    _Static_assert(
        FACE_MOUTH_STUDY_REDUX_FRAME_BYTES == 38400,
        "mouth study must remain exactly 160x120 RGB565");
    test_metadata_mapping_and_rejection();
    test_determinism_and_full_ir();
    test_adversarial_bounds_canaries_and_landmarks();
    test_parented_rig_and_speech_acting();
    test_all_emotions_are_readable();
    test_viseme_vocabularies_and_coarticulation();
    test_second_pass_acting_constraints();
    test_third_pass_polished_actor_contracts();
    puts("face_mouth_study_redux_test: all checks passed");
    return 0;
}
