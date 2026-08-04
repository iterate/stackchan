#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pose.h"
#include "face_robot_redux_actors.h"
#include "face_stage.h"

enum {
    RR_GUARD_WORDS = 16,
    RR_FUZZ_CASES = 256,
    RR_SPEECH_FRAMES = 16,
};

typedef struct {
    uint32_t before[RR_GUARD_WORDS];
    uint16_t pixels[FACE_ROBOT_REDUX_PIXEL_COUNT];
    uint32_t after[RR_GUARD_WORDS];
} rr_guarded_frame_t;

static uint32_t rr_rng_state = 0x243f6a88U;

static uint32_t rr_rng_next(void)
{
    uint32_t value = rr_rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rr_rng_state = value;
    return value;
}

static int rr_test_abs(int value)
{
    return value < 0 ? -value : value;
}

static face_render_key_t rr_baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 132U;
    key.controls.mouth_width = 176U;
    key.controls.mouth_round = 38U;
    key.controls.mouth_press = 14U;
    key.controls.mouth_teeth = 122U;
    key.controls.eye_left_open = 232U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = 5;
    key.controls.look_y = -3;
    key.controls.brow = 7;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 12U;
    key.viseme_weight = 238U;
    key.audio_level = 148U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 24U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 8;
    key.mouth_corner_right = 5;
    key.tongue = 86U;
    key.cheek = 32U;
    key.eye_left_squint = 5U;
    key.eye_right_squint = 3U;
    key.brow_inner = 5;
    key.brow_outer_left = -4;
    key.brow_outer_right = 6;
    key.head_roll = 3;
    key.affect_valence = 12;
    key.affect_arousal = 146U;
    key.head_yaw = 4;
    key.head_pitch = -3;
    key.body_lean_x = 3;
    key.body_lean_y = -2;
    key.expression_weight = 255U;
    key.attention = 218U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

static void rr_init_guard(rr_guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < RR_GUARD_WORDS; ++index) {
        frame->before[index] = 0x514f0000U + (uint32_t)index;
        frame->after[index] = 0xf4150000U + (uint32_t)index;
    }
}

static void rr_check_guard(const rr_guarded_frame_t *frame)
{
    for (size_t index = 0U; index < RR_GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x514f0000U + index);
        assert(frame->after[index] == 0xf4150000U + index);
    }
}

static void rr_check_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_ROBOT_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_ROBOT_REDUX_WIDTH; ++x) {
            if (x < 4U || x >= FACE_ROBOT_REDUX_WIDTH - 4U ||
                y < 4U || y >= FACE_ROBOT_REDUX_HEIGHT - 4U) {
                assert(
                    pixels[y * FACE_ROBOT_REDUX_WIDTH + x] ==
                    background);
            }
        }
    }
}

static uint32_t rr_hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_ROBOT_REDUX_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t rr_differing_pixels(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t count = 0U;
    for (size_t index = 0U;
         index < FACE_ROBOT_REDUX_PIXEL_COUNT;
         ++index) {
        count += first[index] != second[index];
    }
    return count;
}

static size_t rr_differing_contact_pixels(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t count = 0U;
    for (size_t y = 2U; y < FACE_ROBOT_REDUX_HEIGHT; y += 4U) {
        for (size_t x = 2U; x < FACE_ROBOT_REDUX_WIDTH; x += 4U) {
            const size_t index =
                y * FACE_ROBOT_REDUX_WIDTH + x;
            count += first[index] != second[index];
        }
    }
    return count;
}

static face_render_key_t rr_speech_key(size_t frame)
{
    static const uint8_t VISEMES[RR_SPEECH_FRAMES] = {
        FACE_VISEME_SIL,
        FACE_VISEME_SIL,
        FACE_VISEME_AA,
        FACE_VISEME_E,
        FACE_VISEME_I,
        FACE_VISEME_O,
        FACE_VISEME_U,
        FACE_VISEME_PP,
        FACE_VISEME_FF,
        FACE_VISEME_TH,
        FACE_VISEME_SS,
        FACE_VISEME_RR,
        FACE_VISEME_CH,
        FACE_VISEME_AA,
        FACE_VISEME_SIL,
        FACE_VISEME_SIL,
    };
    static const uint8_t AUDIO[RR_SPEECH_FRAMES] = {
        4U, 42U, 92U, 128U, 164U, 196U, 144U, 108U,
        186U, 156U, 124U, 174U, 116U, 72U, 30U, 4U,
    };
    static const uint8_t OPEN[RR_SPEECH_FRAMES] = {
        4U, 24U, 86U, 108U, 72U, 190U, 118U, 6U,
        36U, 72U, 46U, 112U, 76U, 48U, 18U, 4U,
    };
    face_render_key_t key = rr_baseline_key();
    key.controls.mouth_open = OPEN[frame];
    key.controls.mouth_width =
        (uint8_t)(142U + (frame * 17U) % 86U);
    key.controls.mouth_round =
        (uint8_t)((frame * 43U) & 255U);
    key.audio_level = AUDIO[frame];
    key.viseme = VISEMES[frame];
    key.viseme_secondary =
        VISEMES[frame + 1U < RR_SPEECH_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        frame == 0U || frame == RR_SPEECH_FRAMES - 1U
        ? 0U
        : 64U;
    key.controls.look_x =
        (int8_t)(frame < 8U ? -8 + (int)frame * 2
                            : 8 - ((int)frame - 8) * 2);
    key.controls.look_y =
        (int8_t)(frame < 8U ? -3 + (int)frame
                            : 5 - ((int)frame - 8));
    key.speech_phase =
        frame == 0U || frame == 15U ? FACE_SPEECH_IDLE
        : frame == 1U ? FACE_SPEECH_STARTING
        : frame >= 13U ? FACE_SPEECH_ENDING
        : FACE_SPEECH_ACTIVE;
    if (frame == 0U || frame == 15U) {
        key.controls.flags = 0U;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        key.viseme_weight = 0U;
    }
    key.stage_expression =
        frame >= 10U && frame <= 12U
        ? FACE_EXPRESSION_JOY
        : FACE_EXPRESSION_WARM;
    return key;
}

static void rr_test_metadata_mapping_and_rejection(void)
{
    static const uint8_t IDS[FACE_ROBOT_REDUX_COUNT] = {
        9U, 10U, 11U, 12U, 13U, 14U,
    };
    assert(face_robot_redux_count() == FACE_ROBOT_REDUX_COUNT);
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        face_robot_redux_info_t info;
        assert(face_robot_redux_info(style, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.legacy_profile_id == IDS[raw]);
        assert(strcmp(info.slug, face_robot_redux_slug(style)) == 0);
        assert(strcmp(info.name, face_robot_redux_name(style)) == 0);
        assert(info.deliberate_mouthless ==
            (raw < 2U || raw >= 4U));
        for (size_t previous = 0U; previous < raw; ++previous) {
            assert(strcmp(
                info.slug,
                face_robot_redux_slug(
                    (face_robot_redux_style_t)previous)) != 0);
        }
        face_robot_redux_style_t mapped = FACE_ROBOT_REDUX_COUNT;
        assert(face_robot_redux_from_legacy_id(IDS[raw], &mapped));
        assert(mapped == style);
    }
    face_robot_redux_style_t mapped =
        FACE_ROBOT_REDUX_ROBOEYES_ALERT;
    assert(!face_robot_redux_from_legacy_id(8U, &mapped));
    assert(!face_robot_redux_from_legacy_id(15U, &mapped));
    assert(!face_robot_redux_from_legacy_id(9U, NULL));
    assert(face_robot_redux_slug(FACE_ROBOT_REDUX_COUNT) == NULL);
    assert(face_robot_redux_name((face_robot_redux_style_t)-1) == NULL);
    assert(!face_robot_redux_info(FACE_ROBOT_REDUX_COUNT, NULL));

    const face_render_key_t key = rr_baseline_key();
    uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
    assert(!face_robot_redux_render(
        FACE_ROBOT_REDUX_COUNT,
        &key,
        0U,
        frame,
        FACE_ROBOT_REDUX_PIXEL_COUNT));
    assert(!face_robot_redux_render(
        FACE_ROBOT_REDUX_ROBOEYES_ALERT,
        NULL,
        0U,
        frame,
        FACE_ROBOT_REDUX_PIXEL_COUNT));
    assert(!face_robot_redux_render(
        FACE_ROBOT_REDUX_ROBOEYES_ALERT,
        &key,
        0U,
        NULL,
        FACE_ROBOT_REDUX_PIXEL_COUNT));
    assert(!face_robot_redux_render(
        FACE_ROBOT_REDUX_ROBOEYES_ALERT,
        &key,
        0U,
        frame,
        FACE_ROBOT_REDUX_PIXEL_COUNT - 1U));
}

static void rr_assert_pose_bounds(
    face_robot_redux_style_t style,
    const face_robot_redux_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (style == FACE_ROBOT_REDUX_JIBO_ORB && eye == 1U) {
            assert(pose->eye_w[eye] == 0);
            assert(pose->eye_h[eye] == 0);
            assert(pose->eye_open[eye] == 0);
            continue;
        }
        assert(pose->eye_x[eye] >= 35 && pose->eye_x[eye] <= 125);
        assert(pose->eye_y[eye] >= 35 && pose->eye_y[eye] <= 75);
        assert(pose->eye_w[eye] >= 18 && pose->eye_w[eye] <= 78);
        assert(pose->eye_h[eye] >= 20 && pose->eye_h[eye] <= 64);
        assert(pose->eye_open[eye] >= 2);
        assert(pose->eye_open[eye] <= pose->eye_h[eye]);
        assert(pose->pupil_x[eye] >= 20 && pose->pupil_x[eye] <= 140);
        assert(pose->pupil_y[eye] >= 20 && pose->pupil_y[eye] <= 90);
        assert(pose->pupil_radius[eye] >= 2);
        assert(pose->pupil_radius[eye] <= 20);
        assert(pose->brow_y[eye] >= 10 && pose->brow_y[eye] <= 52);
        assert(pose->brow_slope[eye] >= -13);
        assert(pose->brow_slope[eye] <= 13);
    }
    assert(pose->mouth_x == 80);
    assert(pose->mouth_y >= 80 && pose->mouth_y <= 96);
    assert(pose->mouth_w >= 13 && pose->mouth_w <= 70);
    assert(pose->mouth_h >= 2 && pose->mouth_h <= 31);
    assert(pose->face_shift_x >= -7 && pose->face_shift_x <= 7);
    assert(pose->face_shift_y >= -6 && pose->face_shift_y <= 7);
    assert(pose->body_lean_x >= -10 && pose->body_lean_x <= 10);
    assert(pose->body_lean_y >= -8 && pose->body_lean_y <= 8);
}

static void rr_test_determinism_bounds_and_legacy(void)
{
    const face_render_key_t key = rr_baseline_key();
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        rr_guarded_frame_t first;
        rr_guarded_frame_t second;
        rr_init_guard(&first);
        rr_init_guard(&second);
        assert(face_robot_redux_render(
            style, &key, 17231U, first.pixels,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        assert(face_robot_redux_render(
            style, &key, 17231U, second.pixels,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        assert(memcmp(first.pixels, second.pixels,
                      sizeof(first.pixels)) == 0);
        rr_check_guard(&first);
        rr_check_guard(&second);
        rr_check_safe_border(first.pixels);

        face_robot_redux_pose_t pose;
        assert(face_robot_redux_resolve(
            style, &key, 17231U, &pose));
        assert(memcmp(&pose.source, &key, FACE_RENDER_KEY_BYTES) == 0);
        rr_assert_pose_bounds(style, &pose);

        face_robot_redux_info_t info;
        uint16_t legacy[FACE_ROBOT_REDUX_PIXEL_COUNT];
        assert(face_robot_redux_info(style, &info));
        assert(face_robot_redux_render_legacy(
            info.legacy_profile_id,
            &key,
            17231U,
            legacy,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        assert(memcmp(first.pixels, legacy, sizeof(legacy)) == 0);
    }
    face_robot_redux_pose_t pose;
    assert(!face_robot_redux_resolve(
        FACE_ROBOT_REDUX_COUNT, &key, 0U, &pose));
    assert(!face_robot_redux_resolve(
        FACE_ROBOT_REDUX_ROBOEYES_ALERT, NULL, 0U, &pose));
    assert(!face_robot_redux_resolve(
        FACE_ROBOT_REDUX_ROBOEYES_ALERT, &key, 0U, NULL));
    uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
    assert(!face_robot_redux_render_legacy(
        63U, &key, 0U, frame, FACE_ROBOT_REDUX_PIXEL_COUNT));
}

static void rr_test_expression_and_viseme_response(void)
{
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = rr_baseline_key();
            key.controls.flags = 0U;
            key.speech_phase = FACE_SPEECH_IDLE;
            key.stage_expression = expression;
            key.expression_weight = 255U;
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            assert(face_robot_redux_render(
                style, &key, 5064U, frame,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            expression_hashes[expression] = rr_hash_pixels(frame);
            for (uint8_t previous = 0U;
                 previous < expression;
                 ++previous) {
                if (expression_hashes[expression] ==
                    expression_hashes[previous]) {
                    fprintf(
                        stderr,
                        "expression collision style=%zu current=%u "
                        "previous=%u\n",
                        raw,
                        expression,
                        previous);
                }
                assert(expression_hashes[expression] !=
                    expression_hashes[previous]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            face_render_key_t key = rr_baseline_key();
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            key.phoneme = FACE_PHONEME_NONE;
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            assert(face_robot_redux_render(
                style, &key, 11531U, frame,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            viseme_hashes[viseme] = rr_hash_pixels(frame);
            for (uint8_t previous = 0U; previous < viseme; ++previous) {
                if (viseme_hashes[viseme] ==
                    viseme_hashes[previous]) {
                    fprintf(
                        stderr,
                        "viseme collision style=%zu current=%u "
                        "previous=%u\n",
                        raw,
                        viseme,
                        previous);
                }
                assert(viseme_hashes[viseme] !=
                    viseme_hashes[previous]);
            }
        }
    }
}

static void rr_test_all_ir_bytes_have_visual_influence(void)
{
    static const uint8_t SIGNED_BYTE[FACE_RENDER_KEY_BYTES] = {
        [7] = 1U,
        [8] = 1U,
        [9] = 1U,
        [20] = 1U,
        [21] = 1U,
        [26] = 1U,
        [27] = 1U,
        [28] = 1U,
        [29] = 1U,
        [30] = 1U,
        [32] = 1U,
        [33] = 1U,
        [34] = 1U,
        [35] = 1U,
    };
    face_render_key_t baseline = rr_baseline_key();
    baseline.viseme_weight = 128U;
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        uint16_t reference[FACE_ROBOT_REDUX_PIXEL_COUNT];
        uint16_t changed[FACE_ROBOT_REDUX_PIXEL_COUNT];
        assert(face_robot_redux_render(
            style, &baseline, 17231U, reference,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        for (size_t byte = 0U; byte < FACE_RENDER_KEY_BYTES; ++byte) {
            face_render_key_t probe = baseline;
            uint8_t *probe_bytes = (uint8_t *)&probe;
            if (byte == 10U) {
                probe.controls.expression = FACE_ACTIVITY_IDLE;
            } else if (byte == 11U) {
                probe.controls.flags = FACE_KEYFRAME_FLAG_BLINKING;
            } else if (byte == 12U) {
                probe.viseme = FACE_VISEME_O;
            } else if (byte == 13U) {
                probe.phoneme = 29U;
            } else if (byte == 16U) {
                probe.viseme_set = FACE_VISEME_SET_VRM5;
            } else if (byte == 17U) {
                probe.viseme_secondary = FACE_VISEME_U;
            } else if (byte == 19U) {
                probe.speech_phase = FACE_SPEECH_ENDING;
            } else if (byte == 38U) {
                probe.schema_version = 7U;
            } else if (byte == 39U) {
                probe.stage_expression = FACE_EXPRESSION_SKEPTICAL;
            } else if (SIGNED_BYTE[byte] != 0U) {
                const int8_t current = (int8_t)probe_bytes[byte];
                probe_bytes[byte] =
                    (uint8_t)(current >= 0 ? (int8_t)-127 : (int8_t)127);
            } else {
                probe_bytes[byte] =
                    probe_bytes[byte] < 128U ? 255U : 0U;
            }
            assert(face_robot_redux_render(
                style, &probe, 17231U, changed,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            const size_t difference =
                rr_differing_pixels(reference, changed);
            if (difference == 0U) {
                face_robot_redux_pose_t baseline_pose;
                face_robot_redux_pose_t probe_pose;
                assert(face_robot_redux_resolve(
                    style, &baseline, 17231U, &baseline_pose));
                assert(face_robot_redux_resolve(
                    style, &probe, 17231U, &probe_pose));
                fprintf(
                    stderr,
                    "IR byte has no visual influence: style=%zu byte=%zu "
                    "open=%d/%d speech=%u/%u press=%u/%u detail=%u/%u\n",
                    raw,
                    byte,
                    baseline_pose.eye_open[0],
                    probe_pose.eye_open[0],
                    baseline_pose.speech_open,
                    probe_pose.speech_open,
                    baseline_pose.speech_press,
                    probe_pose.speech_press,
                    baseline_pose.detail_phase,
                    probe_pose.detail_phase);
            }
            assert(difference > 0U);
        }
    }
}

static void rr_test_speech_anticipation_settle_and_fixed_sockets(void)
{
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        uint16_t previous[FACE_ROBOT_REDUX_PIXEL_COUNT];
        uint16_t current[FACE_ROBOT_REDUX_PIXEL_COUNT];
        size_t distinct = 0U;
        face_robot_redux_pose_t first_pose;
        face_robot_redux_pose_t pose;
        face_render_key_t key = rr_speech_key(0U);
        assert(face_robot_redux_resolve(
            style, &key, 0U, &first_pose));
        assert(face_robot_redux_render(
            style, &key, 0U, previous,
            FACE_ROBOT_REDUX_PIXEL_COUNT));

        size_t anticipation_change = 0U;
        size_t first_settle_change = 0U;
        size_t second_settle_change = 0U;
        for (size_t frame = 1U; frame < RR_SPEECH_FRAMES; ++frame) {
            key = rr_speech_key(frame);
            assert(face_robot_redux_resolve(
                style, &key, (uint32_t)frame * 533U, &pose));
            assert(pose.eye_x[0] == first_pose.eye_x[0]);
            assert(pose.eye_y[0] == first_pose.eye_y[0]);
            if (style != FACE_ROBOT_REDUX_JIBO_ORB) {
                assert(pose.eye_x[1] == first_pose.eye_x[1]);
                assert(pose.eye_y[1] == first_pose.eye_y[1]);
            }
            assert(face_robot_redux_render(
                style,
                &key,
                (uint32_t)frame * 533U,
                current,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            const size_t difference =
                rr_differing_pixels(previous, current);
            distinct += difference > 0U;
            assert(difference < 7200U);
            if (frame == 1U) {
                anticipation_change = difference;
            } else if (frame == 13U) {
                first_settle_change = difference;
            } else if (frame == 14U) {
                second_settle_change = difference;
            }
            memcpy(previous, current, sizeof(previous));
        }
        assert(distinct >= 13U);
        assert(anticipation_change > 20U);
        assert(first_settle_change > 20U);
        assert(second_settle_change > 10U);
    }
}

static void rr_assert_eye_only_has_no_mouth_zone(
    const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 101U; y < FACE_ROBOT_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_ROBOT_REDUX_WIDTH; ++x) {
            assert(
                pixels[y * FACE_ROBOT_REDUX_WIDTH + x] ==
                background);
        }
    }
}

static void rr_test_featured_eye_only_quality(void)
{
    uint16_t actor_frames[2][FACE_ROBOT_REDUX_PIXEL_COUNT];
    for (size_t raw = FACE_ROBOT_REDUX_ROBOEYES_ALERT;
         raw <= FACE_ROBOT_REDUX_ROBOEYES_SOFT;
         ++raw) {
        const face_robot_redux_style_t style =
            (face_robot_redux_style_t)raw;
        face_render_key_t neutral_key = rr_baseline_key();
        neutral_key.controls.flags = 0U;
        neutral_key.controls.expression = FACE_ACTIVITY_LISTENING;
        neutral_key.speech_phase = FACE_SPEECH_IDLE;
        neutral_key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        neutral_key.viseme_weight = 0U;
        neutral_key.controls.look_x = 0;
        neutral_key.controls.look_y = 0;
        face_robot_redux_pose_t neutral_pose;
        assert(face_robot_redux_resolve(
            style, &neutral_key, 5064U, &neutral_pose));
        assert(face_robot_redux_render(
            style,
            &neutral_key,
            5064U,
            actor_frames[raw],
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        rr_assert_eye_only_has_no_mouth_zone(actor_frames[raw]);

        for (uint8_t expression = FACE_EXPRESSION_WARM;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = neutral_key;
            key.stage_expression = expression;
            face_robot_redux_pose_t pose;
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            assert(face_robot_redux_resolve(
                style, &key, 5064U, &pose));
            assert(face_robot_redux_render(
                style,
                &key,
                5064U,
                frame,
                FACE_ROBOT_REDUX_PIXEL_COUNT));

            /*
             * Socket anchors are invariant. Each emotion must still create a
             * substantial native and 40x30 contact-scale geometry change.
             */
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(pose.eye_x[eye] == neutral_pose.eye_x[eye]);
                assert(pose.eye_y[eye] == neutral_pose.eye_y[eye]);
            }
            const size_t native_difference = rr_differing_pixels(
                actor_frames[raw], frame);
            const size_t contact_difference =
                rr_differing_contact_pixels(actor_frames[raw], frame);
            if (native_difference < 120U ||
                contact_difference < 6U) {
                fprintf(
                    stderr,
                    "weak eye-only expression style=%zu expression=%u "
                    "native=%zu contact=%zu\n",
                    raw,
                    expression,
                    native_difference,
                    contact_difference);
            }
            assert(native_difference >= 120U);
            assert(contact_difference >= 6U);

            size_t geometry_fields = 0U;
            for (size_t eye = 0U; eye < 2U; ++eye) {
                geometry_fields +=
                    pose.eye_w[eye] != neutral_pose.eye_w[eye];
                geometry_fields +=
                    pose.eye_open[eye] != neutral_pose.eye_open[eye];
                geometry_fields +=
                    pose.brow_y[eye] != neutral_pose.brow_y[eye];
                geometry_fields +=
                    pose.brow_slope[eye] !=
                        neutral_pose.brow_slope[eye];
                geometry_fields +=
                    pose.pupil_x[eye] != neutral_pose.pupil_x[eye];
                geometry_fields +=
                    pose.pupil_y[eye] != neutral_pose.pupil_y[eye];
            }
            assert(geometry_fields >= 2U);
            rr_assert_eye_only_has_no_mouth_zone(frame);
        }

        uint16_t previous[FACE_ROBOT_REDUX_PIXEL_COUNT];
        memcpy(
            previous,
            actor_frames[raw],
            sizeof(previous));
        face_robot_redux_pose_t previous_pose = neutral_pose;
        for (size_t frame_index = 1U;
             frame_index < RR_SPEECH_FRAMES;
             ++frame_index) {
            const face_render_key_t key =
                rr_speech_key(frame_index);
            face_robot_redux_pose_t pose;
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            assert(face_robot_redux_resolve(
                style,
                &key,
                (uint32_t)frame_index * 533U,
                &pose));
            assert(face_robot_redux_render(
                style,
                &key,
                (uint32_t)frame_index * 533U,
                frame,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(pose.eye_x[eye] == neutral_pose.eye_x[eye]);
                assert(pose.eye_y[eye] == neutral_pose.eye_y[eye]);
                assert(rr_test_abs(
                    pose.eye_w[eye] -
                    previous_pose.eye_w[eye]) <= 14);
                assert(rr_test_abs(
                    pose.eye_open[eye] -
                    previous_pose.eye_open[eye]) <= 18);
            }
            const size_t temporal_difference =
                rr_differing_pixels(previous, frame);
            if (temporal_difference >= 4200U) {
                fprintf(
                    stderr,
                    "eye-only topology jump style=%zu frame=%zu "
                    "difference=%zu\n",
                    raw,
                    frame_index,
                    temporal_difference);
            }
            assert(temporal_difference < 4200U);
            rr_assert_eye_only_has_no_mouth_zone(frame);
            memcpy(previous, frame, sizeof(previous));
            previous_pose = pose;
        }

        face_render_key_t left_key = neutral_key;
        face_render_key_t right_key = neutral_key;
        left_key.controls.look_x = -60;
        right_key.controls.look_x = 60;
        uint16_t left[FACE_ROBOT_REDUX_PIXEL_COUNT];
        uint16_t right[FACE_ROBOT_REDUX_PIXEL_COUNT];
        assert(face_robot_redux_render(
            style,
            &left_key,
            5064U,
            left,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        assert(face_robot_redux_render(
            style,
            &right_key,
            5064U,
            right,
            FACE_ROBOT_REDUX_PIXEL_COUNT));
        assert(rr_differing_contact_pixels(left, right) >= 12U);
    }

    assert(rr_differing_pixels(
        actor_frames[FACE_ROBOT_REDUX_ROBOEYES_ALERT],
        actor_frames[FACE_ROBOT_REDUX_ROBOEYES_SOFT]) >= 1000U);
}

static void rr_test_adversarial_frames(void)
{
    for (size_t case_index = 0U;
         case_index < RR_FUZZ_CASES;
         ++case_index) {
        face_render_key_t key;
        for (size_t byte = 0U; byte < sizeof(key); ++byte) {
            ((uint8_t *)&key)[byte] = (uint8_t)rr_rng_next();
        }
        const uint32_t sample_clock = rr_rng_next();
        for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
            const face_robot_redux_style_t style =
                (face_robot_redux_style_t)raw;
            rr_guarded_frame_t frame;
            rr_init_guard(&frame);
            assert(face_robot_redux_render(
                style,
                &key,
                sample_clock,
                frame.pixels,
                FACE_ROBOT_REDUX_PIXEL_COUNT));
            rr_check_guard(&frame);
            rr_check_safe_border(frame.pixels);
            face_robot_redux_pose_t pose;
            assert(face_robot_redux_resolve(
                style, &key, sample_clock, &pose));
            rr_assert_pose_bounds(style, &pose);
        }
    }
}

int main(void)
{
    rr_test_metadata_mapping_and_rejection();
    rr_test_determinism_bounds_and_legacy();
    rr_test_expression_and_viseme_response();
    rr_test_all_ir_bytes_have_visual_influence();
    rr_test_speech_anticipation_settle_and_fixed_sockets();
    rr_test_featured_eye_only_quality();
    rr_test_adversarial_frames();
    printf(
        "face_robot_redux_actors_test: PASS "
        "(6 replacements, 11 expressions, 15 visemes, 40-byte IR, "
        "featured eye-only native/contact quality, no mouth zone, "
        "1 anticipation + 2 settle frames, %d adversarial renders)\n",
        RR_FUZZ_CASES * FACE_ROBOT_REDUX_COUNT);
    return 0;
}
