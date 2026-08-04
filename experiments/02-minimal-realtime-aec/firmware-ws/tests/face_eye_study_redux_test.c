#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_eye_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    ESR_TEST_GUARD_WORDS = 24,
    ESR_TEST_FUZZ_CASES = 256,
    ESR_TEST_TEMPORAL_FRAMES = 210,
};

typedef struct {
    uint32_t before[ESR_TEST_GUARD_WORDS];
    uint16_t pixels[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
    uint32_t after[ESR_TEST_GUARD_WORDS];
} esr_guarded_frame_t;

static uint16_t expression_frames
    [FACE_EXPRESSION_COUNT][FACE_EYE_STUDY_REDUX_PIXEL_COUNT];

static face_render_key_t esr_baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 121U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 34U;
    key.controls.mouth_press = 11U;
    key.controls.mouth_teeth = 105U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 234U;
    key.controls.look_x = 3;
    key.controls.look_y = -2;
    key.controls.brow = 4;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 70U;
    key.audio_level = 19U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.viseme_blend = 0U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.mouth_corner_left = 2;
    key.mouth_corner_right = 1;
    key.cheek = 17U;
    key.eye_left_squint = 3U;
    key.eye_right_squint = 5U;
    key.brow_inner = 2;
    key.brow_outer_left = -2;
    key.brow_outer_right = 3;
    key.head_roll = 1;
    key.affect_valence = 8;
    key.affect_arousal = 112U;
    key.head_yaw = 2;
    key.head_pitch = -1;
    key.body_lean_x = 1;
    key.body_lean_y = -1;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static void esr_init_guard(esr_guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < ESR_TEST_GUARD_WORDS; ++index) {
        frame->before[index] = 0x13570000U + (uint32_t)index;
        frame->after[index] = 0xeca80000U + (uint32_t)index;
    }
}

static void esr_assert_guard(const esr_guarded_frame_t *frame)
{
    for (size_t index = 0U; index < ESR_TEST_GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x13570000U + index);
        assert(frame->after[index] == 0xeca80000U + index);
    }
}

static uint32_t esr_hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_EYE_STUDY_REDUX_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t esr_differing_pixels(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t differing = 0U;
    for (size_t index = 0U;
         index < FACE_EYE_STUDY_REDUX_PIXEL_COUNT;
         ++index) {
        differing += first[index] != second[index];
    }
    return differing;
}

static uint32_t esr_open_clock(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    face_eye_study_redux_pose_t pose;
    for (uint32_t time_ms = 0U; time_ms < 16000U; time_ms += 25U) {
        assert(face_eye_study_redux_resolve(
            profile, key, time_ms * 16U, &pose));
        if (pose.blink_q8 == 256 && pose.saccade_active == 0U) {
            return time_ms * 16U;
        }
    }
    assert(false);
    return 0U;
}

static bool esr_in_bounds(
    size_t x,
    size_t y,
    const face_eye_study_redux_bounds_t *bounds)
{
    return x >= bounds->x && y >= bounds->y &&
        x < (size_t)bounds->x + bounds->width &&
        y < (size_t)bounds->y + bounds->height;
}

static void test_metadata_and_mapping(void)
{
    static const char *const EXPECTED_SLUGS[8] = {
        "saccade-lab",
        "brow-dialogue",
        "lid-anticipation",
        "iris-parallax",
        "sleep-wake",
        "curious-tilt",
        "dot-matrix-eyes",
        "cat-optics",
    };
    assert(face_eye_study_redux_profile_count() == 8U);
    for (size_t index = 0U; index < 8U; ++index) {
        face_eye_study_redux_profile_t profile;
        face_eye_study_redux_profile_t mapped;
        assert(face_eye_study_redux_profile_from_index(index, &profile));
        assert((uint8_t)profile == 15U + index);
        assert(face_eye_study_redux_profile_from_legacy_id(
            (uint8_t)(15U + index), &mapped));
        assert(mapped == profile);
        assert(strcmp(
            face_eye_study_redux_profile_slug(profile),
            EXPECTED_SLUGS[index]) == 0);
        assert(face_eye_study_redux_profile_name(profile) != NULL);
    }
    face_eye_study_redux_profile_t profile;
    assert(!face_eye_study_redux_profile_from_index(8U, &profile));
    assert(!face_eye_study_redux_profile_from_index(0U, NULL));
    assert(!face_eye_study_redux_profile_from_legacy_id(14U, &profile));
    assert(!face_eye_study_redux_profile_from_legacy_id(23U, &profile));
    assert(!face_eye_study_redux_profile_from_legacy_id(15U, NULL));
    assert(face_eye_study_redux_profile_slug(
        (face_eye_study_redux_profile_t)14) == NULL);
    assert(face_eye_study_redux_profile_name(
        (face_eye_study_redux_profile_t)23) == NULL);
}

static void test_rejection_determinism_and_full_ir(void)
{
    const face_render_key_t key = esr_baseline_key();
    esr_guarded_frame_t first;
    esr_guarded_frame_t second;
    esr_init_guard(&first);
    esr_init_guard(&second);
    face_eye_study_redux_pose_t pose;
    assert(!face_eye_study_redux_resolve(
        (face_eye_study_redux_profile_t)14, &key, 0U, &pose));
    assert(!face_eye_study_redux_resolve(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, NULL, 0U, &pose));
    assert(!face_eye_study_redux_resolve(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, &key, 0U, NULL));
    assert(!face_eye_study_redux_render(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, &key, 0U, NULL,
        FACE_EYE_STUDY_REDUX_PIXEL_COUNT));
    assert(!face_eye_study_redux_render(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, &key, 0U, first.pixels,
        FACE_EYE_STUDY_REDUX_PIXEL_COUNT - 1U));
    assert(face_eye_study_redux_render(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, &key, 754321U,
        first.pixels, FACE_EYE_STUDY_REDUX_PIXEL_COUNT));
    assert(face_eye_study_redux_render(
        FACE_EYE_STUDY_REDUX_SACCADE_LAB, &key, 754321U,
        second.pixels, FACE_EYE_STUDY_REDUX_PIXEL_COUNT));
    assert(memcmp(first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    esr_assert_guard(&first);
    esr_assert_guard(&second);

    face_eye_study_redux_pose_t original;
    assert(face_eye_study_redux_resolve(
        FACE_EYE_STUDY_REDUX_IRIS_PARALLAX,
        &key, 754321U, &original));
    assert(memcmp(&original.source, &key, sizeof(key)) == 0);
    for (size_t byte = 0U; byte < FACE_RENDER_KEY_BYTES; ++byte) {
        face_render_key_t changed = key;
        ((uint8_t *)&changed)[byte] ^= 0x5aU;
        face_eye_study_redux_pose_t changed_pose;
        assert(face_eye_study_redux_resolve(
            FACE_EYE_STUDY_REDUX_IRIS_PARALLAX,
            &changed, 754321U, &changed_pose));
        assert(memcmp(
            &changed_pose.source, &changed, sizeof(changed)) == 0);
        assert(changed_pose.input_signature != original.input_signature);
    }
}

static void esr_assert_pose_bounds(
    const face_eye_study_redux_pose_t *pose)
{
    assert(pose->gaze_x_q8 >= -256 && pose->gaze_x_q8 <= 256);
    assert(pose->gaze_y_q8 >= -224 && pose->gaze_y_q8 <= 224);
    assert(pose->blink_q8 >= 0 && pose->blink_q8 <= 256);
    assert(pose->pupil_scale_q8 >= 150 &&
        pose->pupil_scale_q8 <= 344);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->openness_q8[eye] >= 0 &&
            pose->openness_q8[eye] <= 256);
        assert(pose->width_scale_q8[eye] >= 176 &&
            pose->width_scale_q8[eye] <= 354);
        assert(pose->height_scale_q8[eye] >= 154 &&
            pose->height_scale_q8[eye] <= 350);
        assert(pose->brow_raise_q8[eye] >= -96 &&
            pose->brow_raise_q8[eye] <= 160);
        assert(pose->brow_tilt_q8[eye] >= -128 &&
            pose->brow_tilt_q8[eye] <= 128);
    }
}

static uint32_t esr_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void test_adversarial_bounds_and_canaries(void)
{
    uint32_t random_state = 0x91e10da5U;
    for (size_t iteration = 0U;
         iteration < ESR_TEST_FUZZ_CASES;
         ++iteration) {
        face_render_key_t key;
        for (size_t byte = 0U; byte < sizeof(key); ++byte) {
            ((uint8_t *)&key)[byte] =
                (uint8_t)esr_random(&random_state);
        }
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + iteration % 8U);
        face_eye_study_redux_pose_t pose;
        esr_guarded_frame_t frame;
        face_eye_study_redux_landmarks_t landmarks;
        esr_init_guard(&frame);
        assert(face_eye_study_redux_resolve(
            profile, &key, esr_random(&random_state), &pose));
        esr_assert_pose_bounds(&pose);
        assert(face_eye_study_redux_render_resolved(
            profile, &pose, frame.pixels,
            FACE_EYE_STUDY_REDUX_PIXEL_COUNT, &landmarks));
        esr_assert_guard(&frame);
        assert(landmarks.face.x + landmarks.face.width <=
            FACE_EYE_STUDY_REDUX_WIDTH);
        assert(landmarks.face.y + landmarks.face.height <=
            FACE_EYE_STUDY_REDUX_HEIGHT);
        assert(landmarks.left_eye.x + landmarks.left_eye.width <=
            FACE_EYE_STUDY_REDUX_WIDTH);
        assert(landmarks.right_eye.x + landmarks.right_eye.width <=
            FACE_EYE_STUDY_REDUX_WIDTH);
        assert(landmarks.left_eye.y + landmarks.left_eye.height <=
            FACE_EYE_STUDY_REDUX_HEIGHT);
        assert(landmarks.right_eye.y + landmarks.right_eye.height <=
            FACE_EYE_STUDY_REDUX_HEIGHT);
    }
}

static void test_fixed_sockets_and_clipped_gaze(void)
{
    face_render_key_t key = esr_baseline_key();
    for (size_t raw = 0U; raw < 8U; ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + raw);
        const uint32_t clock = esr_open_clock(profile, &key);
        face_eye_study_redux_pose_t center_pose;
        face_eye_study_redux_pose_t extreme_pose;
        uint16_t center[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        uint16_t extreme[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        face_eye_study_redux_landmarks_t landmarks;
        assert(face_eye_study_redux_resolve(
            profile, &key, clock, &center_pose));
        key.controls.look_x = 127;
        key.controls.look_y = -127;
        key.head_yaw = 127;
        key.head_pitch = -127;
        key.body_lean_x = 127;
        key.body_lean_y = -127;
        assert(face_eye_study_redux_resolve(
            profile, &key, clock, &extreme_pose));
        assert(center_pose.left_center_x_q8 ==
            extreme_pose.left_center_x_q8);
        assert(center_pose.right_center_x_q8 ==
            extreme_pose.right_center_x_q8);
        assert(center_pose.center_y_q8 == extreme_pose.center_y_q8);
        assert(face_eye_study_redux_render_resolved(
            profile, &center_pose, center,
            FACE_EYE_STUDY_REDUX_PIXEL_COUNT, &landmarks));
        assert(face_eye_study_redux_render_resolved(
            profile, &extreme_pose, extreme,
            FACE_EYE_STUDY_REDUX_PIXEL_COUNT, NULL));
        if (profile != FACE_EYE_STUDY_REDUX_DOT_MATRIX_EYES) {
            size_t outside_changes = 0U;
            for (size_t y = 0U;
                 y < FACE_EYE_STUDY_REDUX_HEIGHT;
                 ++y) {
                for (size_t x = 0U;
                     x < FACE_EYE_STUDY_REDUX_WIDTH;
                     ++x) {
                    const size_t index =
                        y * FACE_EYE_STUDY_REDUX_WIDTH + x;
                    if (center[index] != extreme[index] &&
                        !esr_in_bounds(x, y, &landmarks.left_eye) &&
                        !esr_in_bounds(x, y, &landmarks.right_eye)) {
                        ++outside_changes;
                    }
                }
            }
            assert(outside_changes == 0U);
        }
        key = esr_baseline_key();
    }
}

static void test_all_emotions_change_geometry(void)
{
    face_render_key_t key = esr_baseline_key();
    for (size_t raw = 0U; raw < 8U; ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + raw);
        const uint32_t clock = esr_open_clock(profile, &key);
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        face_eye_study_redux_pose_t poses[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            key.stage_expression = expression;
            assert(face_eye_study_redux_resolve(
                profile, &key, clock, &poses[expression]));
            assert(face_eye_study_redux_render_resolved(
                profile, &poses[expression],
                expression_frames[expression],
                FACE_EYE_STUDY_REDUX_PIXEL_COUNT, NULL));
            hashes[expression] =
                esr_hash_pixels(expression_frames[expression]);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                assert(esr_differing_pixels(
                    expression_frames[FACE_EXPRESSION_NEUTRAL],
                    expression_frames[expression]) >= 90U);
                bool geometric_channel_changed = false;
                for (size_t eye = 0U; eye < 2U; ++eye) {
                    geometric_channel_changed |=
                        poses[expression].openness_q8[eye] !=
                            poses[FACE_EXPRESSION_NEUTRAL].
                                openness_q8[eye] ||
                        poses[expression].width_scale_q8[eye] !=
                            poses[FACE_EXPRESSION_NEUTRAL].
                                width_scale_q8[eye] ||
                        poses[expression].height_scale_q8[eye] !=
                            poses[FACE_EXPRESSION_NEUTRAL].
                                height_scale_q8[eye] ||
                        poses[expression].brow_raise_q8[eye] !=
                            poses[FACE_EXPRESSION_NEUTRAL].
                                brow_raise_q8[eye] ||
                        poses[expression].brow_tilt_q8[eye] !=
                            poses[FACE_EXPRESSION_NEUTRAL].
                                brow_tilt_q8[eye];
                }
                assert(geometric_channel_changed);
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

static void test_authored_closed_lids_remain_readable(void)
{
    for (size_t raw = 0U; raw < 8U; ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + raw);
        face_render_key_t open_key = esr_baseline_key();
        const uint32_t clock = esr_open_clock(profile, &open_key);
        face_render_key_t closed_key = open_key;
        closed_key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
        uint16_t open_frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        uint16_t closed_frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        face_eye_study_redux_pose_t closed_pose;
        assert(face_eye_study_redux_render(
            profile, &open_key, clock, open_frame,
            FACE_EYE_STUDY_REDUX_PIXEL_COUNT));
        assert(face_eye_study_redux_resolve(
            profile, &closed_key, clock, &closed_pose));
        assert(closed_pose.openness_q8[0] == 0);
        assert(closed_pose.openness_q8[1] == 0);
        assert(face_eye_study_redux_render_resolved(
            profile, &closed_pose, closed_frame,
            FACE_EYE_STUDY_REDUX_PIXEL_COUNT, NULL));
        assert(esr_differing_pixels(open_frame, closed_frame) >= 220U);

        if (profile != FACE_EYE_STUDY_REDUX_DOT_MATRIX_EYES) {
            for (size_t eye = 0U; eye < 2U; ++eye) {
                size_t changed_columns = 0U;
                const size_t x0 = eye == 0U ? 14U : 82U;
                const size_t x1 = eye == 0U ? 78U : 148U;
                for (size_t x = x0; x < x1; ++x) {
                    bool changed = false;
                    for (size_t y = 45U; y < 82U; ++y) {
                        const size_t index =
                            y * FACE_EYE_STUDY_REDUX_WIDTH + x;
                        changed |= open_frame[index] != closed_frame[index];
                    }
                    changed_columns += changed;
                }
                assert(changed_columns >= 22U);
            }
        }
    }
}

static void test_speech_attention_acting(void)
{
    for (size_t raw = 0U; raw < 8U; ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + raw);
        face_render_key_t rest = esr_baseline_key();
        const uint32_t clock = esr_open_clock(profile, &rest);
        face_render_key_t starting = rest;
        starting.controls.expression = FACE_ACTIVITY_SPEAKING;
        starting.controls.flags |= FACE_KEYFRAME_FLAG_SPEAKING;
        starting.audio_level = 136U;
        starting.speech_phase = FACE_SPEECH_STARTING;
        face_render_key_t active = starting;
        active.audio_level = 232U;
        active.speech_phase = FACE_SPEECH_ACTIVE;
        face_render_key_t ending = starting;
        ending.audio_level = 61U;
        ending.speech_phase = FACE_SPEECH_ENDING;
        face_eye_study_redux_pose_t poses[4];
        assert(face_eye_study_redux_resolve(
            profile, &rest, clock, &poses[0]));
        assert(face_eye_study_redux_resolve(
            profile, &starting, clock, &poses[1]));
        assert(face_eye_study_redux_resolve(
            profile, &active, clock, &poses[2]));
        assert(face_eye_study_redux_resolve(
            profile, &ending, clock, &poses[3]));
        assert(poses[0].left_center_x_q8 == poses[1].left_center_x_q8);
        assert(poses[0].right_center_x_q8 == poses[3].right_center_x_q8);
        assert(poses[1].speech_energy_q8 < poses[2].speech_energy_q8);
        assert(poses[1].brow_raise_q8[0] != poses[0].brow_raise_q8[0]);
        assert(poses[2].openness_q8[0] != poses[0].openness_q8[0]);
        /*
         * A saturated aperture can legitimately be equal in ACTIVE and
         * ENDING; the settle must still move at least one visible acting
         * channel.  Do not force every style to encode phase in openness.
         */
        assert(
            poses[3].openness_q8[0] != poses[2].openness_q8[0] ||
            poses[3].upper_lid_q8[0] != poses[2].upper_lid_q8[0] ||
            poses[3].brow_raise_q8[0] != poses[2].brow_raise_q8[0] ||
            poses[3].gaze_y_q8 != poses[2].gaze_y_q8);
    }
}

static void test_character_activity_does_not_rephase_blink(void)
{
    static const face_eye_study_redux_profile_t PROFILES[5] = {
        FACE_EYE_STUDY_REDUX_BROW_DIALOGUE,
        FACE_EYE_STUDY_REDUX_IRIS_PARALLAX,
        FACE_EYE_STUDY_REDUX_SLEEP_WAKE,
        FACE_EYE_STUDY_REDUX_CURIOUS_TILT,
        FACE_EYE_STUDY_REDUX_CAT_OPTICS,
    };
    for (size_t profile_index = 0U;
         profile_index < sizeof(PROFILES) / sizeof(PROFILES[0]);
         ++profile_index) {
        face_render_key_t listening = esr_baseline_key();
        face_render_key_t speaking = listening;
        speaking.controls.expression = FACE_ACTIVITY_SPEAKING;
        speaking.controls.flags |= FACE_KEYFRAME_FLAG_SPEAKING;
        speaking.speech_phase = FACE_SPEECH_ACTIVE;
        speaking.audio_level = 184U;
        for (uint32_t time_ms = 0U;
             time_ms < 9000U;
             time_ms += 37U) {
            face_eye_study_redux_pose_t listening_pose;
            face_eye_study_redux_pose_t speaking_pose;
            assert(face_eye_study_redux_resolve(
                PROFILES[profile_index],
                &listening,
                time_ms * 16U,
                &listening_pose));
            assert(face_eye_study_redux_resolve(
                PROFILES[profile_index],
                &speaking,
                time_ms * 16U,
                &speaking_pose));
            /*
             * Speech changes the acting pose, but must not teleport the
             * autonomous blink clock into a different phase.  This catches
             * the former curious-tilt failure where every speech fixture
             * landed on a closed-eye frame.
             */
            assert(
                listening_pose.blink_q8 ==
                speaking_pose.blink_q8);
        }
    }
}

static uint32_t esr_rgb565_distance(uint16_t first, uint16_t second)
{
    const int32_t red =
        (int32_t)((first >> 11U) & 31U) -
        (int32_t)((second >> 11U) & 31U);
    const int32_t green =
        (int32_t)((first >> 5U) & 63U) -
        (int32_t)((second >> 5U) & 63U);
    const int32_t blue =
        (int32_t)(first & 31U) - (int32_t)(second & 31U);
    return (uint32_t)(
        (red < 0 ? -red : red) * 8 +
        (green < 0 ? -green : green) * 4 +
        (blue < 0 ? -blue : blue) * 8);
}

static void test_blink_saccade_temporal_continuity(void)
{
    size_t abrupt_transitions = 0U;
    size_t active_saccade_frames = 0U;
    size_t blink_frames = 0U;
    size_t worst_changed = 0U;
    uint32_t worst_mean_delta = 0U;
    for (size_t raw = 0U; raw < 8U; ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + raw);
        face_render_key_t key = esr_baseline_key();
        key.attention = 110U;
        uint16_t previous[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        uint16_t current[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        bool have_previous = false;
        int32_t previous_blink = 256;
        for (size_t frame = 0U;
             frame < ESR_TEST_TEMPORAL_FRAMES;
             ++frame) {
            const uint32_t clock =
                (uint32_t)(frame * 533U);
            face_eye_study_redux_pose_t pose;
            assert(face_eye_study_redux_resolve(
                profile, &key, clock, &pose));
            assert(face_eye_study_redux_render_resolved(
                profile, &pose, current,
                FACE_EYE_STUDY_REDUX_PIXEL_COUNT, NULL));
            active_saccade_frames += pose.saccade_active != 0U;
            blink_frames += pose.blink_q8 < 256;
            const int32_t prior_blink = previous_blink;
            assert(pose.blink_q8 - previous_blink <= 154);
            assert(previous_blink - pose.blink_q8 <= 154);
            previous_blink = pose.blink_q8;
            if (have_previous) {
                size_t changed = 0U;
                uint64_t total_delta = 0U;
                for (size_t pixel = 0U;
                     pixel < FACE_EYE_STUDY_REDUX_PIXEL_COUNT;
                     ++pixel) {
                    if (previous[pixel] != current[pixel]) {
                        ++changed;
                        total_delta += esr_rgb565_distance(
                            previous[pixel], current[pixel]);
                    }
                }
                const uint32_t mean_delta =
                    changed > 0U
                        ? (uint32_t)(total_delta / changed)
                        : 0U;
                if (changed > worst_changed) {
                    worst_changed = changed;
                }
                if (mean_delta > worst_mean_delta) {
                    worst_mean_delta = mean_delta;
                }
                if (changed > 2600U && mean_delta > 132U) {
                    ++abrupt_transitions;
                    fprintf(
                        stderr,
                        "  abrupt profile=%zu frame=%zu changed=%zu "
                        "mean=%u blink=%d->%d saccade=%u\n",
                        raw, frame, changed, mean_delta,
                        prior_blink, pose.blink_q8,
                        pose.saccade_active);
                }
                assert(changed < 6200U);
            }
            memcpy(previous, current, sizeof(previous));
            have_previous = true;
        }
    }
    fprintf(
        stderr,
        "eye-study temporal: abrupt=%zu, worst_changed=%zu/%d, "
        "worst_mean_delta=%u, saccade_frames=%zu, blink_frames=%zu\n",
        abrupt_transitions,
        worst_changed,
        FACE_EYE_STUDY_REDUX_PIXEL_COUNT,
        worst_mean_delta,
        active_saccade_frames,
        blink_frames);
    assert(active_saccade_frames > 0U);
    assert(blink_frames > 0U);
    assert(abrupt_transitions == 0U);
}

int main(void)
{
    _Static_assert(
        FACE_RENDER_KEY_BYTES == 40,
        "eye study must cover the complete 40-byte IR");
    _Static_assert(
        FACE_EYE_STUDY_REDUX_CONTEXT_BYTES == 0,
        "eye study must remain stateless and heap-free");
    test_metadata_and_mapping();
    test_rejection_determinism_and_full_ir();
    test_adversarial_bounds_and_canaries();
    test_fixed_sockets_and_clipped_gaze();
    test_all_emotions_change_geometry();
    test_authored_closed_lids_remain_readable();
    test_speech_attention_acting();
    test_character_activity_does_not_rephase_blink();
    test_blink_saccade_temporal_continuity();
    puts("face_eye_study_redux_test: all checks passed");
    return 0;
}
