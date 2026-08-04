#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pose.h"
#include "face_salvage_redux_actors.h"
#include "face_stage.h"

enum {
    SR_TEST_GUARD_WORDS = 16,
    SR_TEST_FUZZ_CASES = 256,
};

typedef struct {
    uint32_t before[SR_TEST_GUARD_WORDS];
    uint16_t pixels[FACE_SALVAGE_REDUX_PIXEL_COUNT];
    uint32_t after[SR_TEST_GUARD_WORDS];
} sr_test_guarded_frame_t;

static uint32_t sr_test_rng_state = 0x243f6a88U;

static uint32_t sr_test_rng_next(void)
{
    uint32_t value = sr_test_rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    sr_test_rng_state = value;
    return value;
}

static face_render_key_t sr_test_key(bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 136U : 8U;
    key.controls.mouth_width = 176U;
    key.controls.mouth_round = 42U;
    key.controls.mouth_press = speaking ? 12U : 218U;
    key.controls.mouth_teeth = 124U;
    key.controls.eye_left_open = 232U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = 5;
    key.controls.look_y = -3;
    key.controls.brow = 7;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.viseme = speaking ? FACE_VISEME_AA : FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = speaking ? 238U : 0U;
    key.audio_level = speaking ? 148U : 4U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = speaking ? FACE_VISEME_E : FACE_VISEME_SIL;
    key.viseme_blend = speaking ? 24U : 0U;
    key.speech_phase =
        speaking ? FACE_SPEECH_ACTIVE : FACE_SPEECH_IDLE;
    key.mouth_corner_left = 8;
    key.mouth_corner_right = 5;
    key.tongue = speaking ? 86U : 0U;
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

static void sr_test_init_guard(sr_test_guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < SR_TEST_GUARD_WORDS; ++index) {
        frame->before[index] = 0x51ed0000U + (uint32_t)index;
        frame->after[index] = 0xed510000U + (uint32_t)index;
    }
}

static void sr_test_check_guard(const sr_test_guarded_frame_t *frame)
{
    for (size_t index = 0U; index < SR_TEST_GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x51ed0000U + index);
        assert(frame->after[index] == 0xed510000U + index);
    }
}

static void sr_test_check_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_SALVAGE_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_SALVAGE_REDUX_WIDTH; ++x) {
            if (x < 4U || x >= FACE_SALVAGE_REDUX_WIDTH - 4U ||
                y < 4U || y >= FACE_SALVAGE_REDUX_HEIGHT - 4U) {
                assert(
                    pixels[y * FACE_SALVAGE_REDUX_WIDTH + x] ==
                    background);
            }
        }
    }
}

static uint32_t sr_test_hash(
    const uint16_t *pixels,
    size_t pixel_count)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < pixel_count; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t sr_test_contact_hash(
    const uint16_t *pixels,
    size_t left,
    size_t top,
    size_t right,
    size_t bottom)
{
    uint32_t hash = 2166136261U;
    for (size_t y = top; y < bottom; ++y) {
        for (size_t x = left; x < right; ++x) {
            const size_t source_x = x * 4U + 2U;
            const size_t source_y = y * 4U + 2U;
            hash ^= pixels[
                source_y * FACE_SALVAGE_REDUX_WIDTH + source_x];
            hash *= 16777619U;
        }
    }
    return hash;
}

static size_t sr_test_difference(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t difference = 0U;
    for (size_t index = 0U;
         index < FACE_SALVAGE_REDUX_PIXEL_COUNT;
         ++index) {
        difference += first[index] != second[index];
    }
    return difference;
}

static size_t sr_test_non_background(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    size_t count = 0U;
    for (size_t index = 0U;
         index < FACE_SALVAGE_REDUX_PIXEL_COUNT;
         ++index) {
        count += pixels[index] != background;
    }
    return count;
}

static uint32_t sr_test_pose_hash(
    const face_salvage_redux_pose_t *pose)
{
    const int16_t values[] = {
        pose->eye_w[0],
        pose->eye_w[1],
        pose->eye_open[0],
        pose->eye_open[1],
        pose->pupil_x[0],
        pose->pupil_x[1],
        pose->pupil_y[0],
        pose->pupil_y[1],
        pose->pupil_radius[0],
        pose->pupil_radius[1],
        pose->brow_y[0],
        pose->brow_y[1],
        pose->brow_slope[0],
        pose->brow_slope[1],
        pose->mouth_w,
        pose->mouth_h,
        pose->mouth_corner[0],
        pose->mouth_corner[1],
        pose->mouth_asymmetry,
    };
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < sizeof(values) / sizeof(values[0]);
         ++index) {
        hash ^= (uint16_t)values[index];
        hash *= 16777619U;
    }
    return hash;
}

static void sr_test_metadata(void)
{
    static const uint8_t IDS[FACE_SALVAGE_REDUX_COUNT] = {
        4U, 6U, 29U, 35U, 36U, 52U,
    };
    assert(face_salvage_redux_count() == FACE_SALVAGE_REDUX_COUNT);
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        face_salvage_redux_info_t info;
        assert(face_salvage_redux_info(style, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.legacy_profile_id == IDS[raw]);
        assert(info.grammar == raw);
        assert(info.mouthless ==
            (style == FACE_SALVAGE_REDUX_VELA_EYES));
        assert(info.pixel_hybrid ==
            (style == FACE_SALVAGE_REDUX_POCKET_COURIER));
        assert(strcmp(info.slug, face_salvage_redux_slug(style)) == 0);
        assert(strcmp(info.name, face_salvage_redux_name(style)) == 0);
        face_salvage_redux_style_t mapped = FACE_SALVAGE_REDUX_COUNT;
        assert(face_salvage_redux_from_legacy_id(IDS[raw], &mapped));
        assert(mapped == style);
        for (size_t previous = 0U; previous < raw; ++previous) {
            face_salvage_redux_info_t prior;
            assert(face_salvage_redux_info(
                (face_salvage_redux_style_t)previous, &prior));
            assert(strcmp(prior.slug, info.slug) != 0);
            assert(prior.grammar != info.grammar);
        }
    }
    face_salvage_redux_style_t mapped =
        FACE_SALVAGE_REDUX_STORY_SCOUT;
    assert(!face_salvage_redux_from_legacy_id(5U, &mapped));
    assert(!face_salvage_redux_from_legacy_id(63U, &mapped));
    assert(!face_salvage_redux_from_legacy_id(4U, NULL));
    assert(face_salvage_redux_slug(FACE_SALVAGE_REDUX_COUNT) == NULL);
    assert(face_salvage_redux_name(
        (face_salvage_redux_style_t)-1) == NULL);
    assert(!face_salvage_redux_info(FACE_SALVAGE_REDUX_COUNT, NULL));
}

static void sr_test_pose_bounds(
    face_salvage_redux_style_t style,
    const face_salvage_redux_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->eye_x[eye] >= 48 && pose->eye_x[eye] <= 112);
        assert(pose->eye_y[eye] >= 45 && pose->eye_y[eye] <= 64);
        assert(pose->eye_w[eye] >= 18 && pose->eye_w[eye] <= 54);
        assert(pose->eye_h[eye] >= 27 && pose->eye_h[eye] <= 36);
        assert(pose->eye_open[eye] >= 2);
        assert(pose->eye_open[eye] <= pose->eye_h[eye]);
        assert(pose->pupil_x[eye] >= 35 && pose->pupil_x[eye] <= 125);
        assert(pose->pupil_y[eye] >= 35 && pose->pupil_y[eye] <= 75);
        assert(pose->pupil_radius[eye] >= 2);
        assert(pose->pupil_radius[eye] <= 11);
        assert(pose->brow_y[eye] >= 10 && pose->brow_y[eye] <= 49);
        assert(pose->brow_slope[eye] >= -13);
        assert(pose->brow_slope[eye] <= 13);
    }
    assert(pose->mouth_x == 80);
    if (style == FACE_SALVAGE_REDUX_VELA_EYES) {
        assert(pose->mouth_y == 0);
        assert(pose->mouth_w == 0);
        assert(pose->mouth_h == 0);
    } else {
        assert(pose->mouth_y >= 82 && pose->mouth_y <= 90);
        assert(pose->mouth_w >= 14 && pose->mouth_w <= 64);
        assert(pose->mouth_h >= 2 && pose->mouth_h <= 28);
    }
    assert(pose->mouth_corner[0] >= -14);
    assert(pose->mouth_corner[0] <= 14);
    assert(pose->mouth_corner[1] >= -14);
    assert(pose->mouth_corner[1] <= 14);
    assert(pose->mouth_asymmetry >= -12);
    assert(pose->mouth_asymmetry <= 12);
    assert(pose->body_lean_x >= -8 && pose->body_lean_x <= 8);
    assert(pose->body_lean_y >= -6 && pose->body_lean_y <= 6);
    assert(pose->speech_wave >= -4 && pose->speech_wave <= 4);
}

static void sr_test_rejection_determinism_and_silhouettes(void)
{
    const face_render_key_t key = sr_test_key(true);
    sr_test_guarded_frame_t first;
    sr_test_guarded_frame_t second;
    sr_test_init_guard(&first);
    sr_test_init_guard(&second);
    assert(!face_salvage_redux_render(
        FACE_SALVAGE_REDUX_COUNT,
        &key,
        0U,
        first.pixels,
        FACE_SALVAGE_REDUX_PIXEL_COUNT));
    assert(!face_salvage_redux_render(
        FACE_SALVAGE_REDUX_STORY_SCOUT,
        NULL,
        0U,
        first.pixels,
        FACE_SALVAGE_REDUX_PIXEL_COUNT));
    assert(!face_salvage_redux_render(
        FACE_SALVAGE_REDUX_STORY_SCOUT,
        &key,
        0U,
        NULL,
        FACE_SALVAGE_REDUX_PIXEL_COUNT));
    assert(!face_salvage_redux_render(
        FACE_SALVAGE_REDUX_STORY_SCOUT,
        &key,
        0U,
        first.pixels,
        FACE_SALVAGE_REDUX_PIXEL_COUNT - 1U));

    uint32_t hashes[FACE_SALVAGE_REDUX_COUNT];
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        assert(face_salvage_redux_render(
            style,
            &key,
            17231U,
            first.pixels,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        assert(face_salvage_redux_render(
            style,
            &key,
            17231U,
            second.pixels,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        assert(memcmp(
            first.pixels, second.pixels, sizeof(first.pixels)) == 0);
        sr_test_check_guard(&first);
        sr_test_check_guard(&second);
        sr_test_check_border(first.pixels);
        assert(sr_test_non_background(first.pixels) > 3000U);
        hashes[raw] = sr_test_hash(
            first.pixels, FACE_SALVAGE_REDUX_PIXEL_COUNT);
        for (size_t previous = 0U; previous < raw; ++previous) {
            assert(hashes[raw] != hashes[previous]);
        }
    }

    face_salvage_redux_pose_t pose;
    assert(face_salvage_redux_resolve(
        FACE_SALVAGE_REDUX_FELT_FAMILIAR, &key, 9U, &pose));
    assert(memcmp(&pose.source, &key, FACE_RENDER_KEY_BYTES) == 0);
    assert(!face_salvage_redux_resolve(
        FACE_SALVAGE_REDUX_COUNT, &key, 9U, &pose));
    assert(!face_salvage_redux_resolve(
        FACE_SALVAGE_REDUX_FELT_FAMILIAR, NULL, 9U, &pose));
    assert(!face_salvage_redux_resolve(
        FACE_SALVAGE_REDUX_FELT_FAMILIAR, &key, 9U, NULL));
}

static void sr_test_expressions_and_visemes(void)
{
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        uint32_t frame_hashes[FACE_EXPRESSION_COUNT];
        uint32_t pose_hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = sr_test_key(false);
            key.stage_expression = expression;
            key.expression_weight = 255U;
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            face_salvage_redux_pose_t pose;
            assert(face_salvage_redux_render(
                style,
                &key,
                5064U,
                frame,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            assert(face_salvage_redux_resolve(
                style, &key, 5064U, &pose));
            sr_test_pose_bounds(style, &pose);
            frame_hashes[expression] = sr_test_hash(
                frame, FACE_SALVAGE_REDUX_PIXEL_COUNT);
            pose_hashes[expression] = sr_test_pose_hash(&pose);
            for (uint8_t previous = 0U;
                 previous < expression;
                 ++previous) {
                assert(frame_hashes[expression] != frame_hashes[previous]);
                assert(pose_hashes[expression] != pose_hashes[previous]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            face_render_key_t key = sr_test_key(true);
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            assert(face_salvage_redux_render(
                style,
                &key,
                11531U,
                frame,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            viseme_hashes[viseme] = sr_test_hash(
                frame, FACE_SALVAGE_REDUX_PIXEL_COUNT);
            for (uint8_t previous = 0U;
                 previous < viseme;
                 ++previous) {
                assert(viseme_hashes[viseme] !=
                    viseme_hashes[previous]);
            }
        }
    }
}

static void sr_test_contact_scale_acting(void)
{
    static const face_salvage_redux_style_t STYLES[] = {
        FACE_SALVAGE_REDUX_POCKET_COURIER,
        FACE_SALVAGE_REDUX_FELT_FAMILIAR,
    };
    for (size_t style_index = 0U;
         style_index < sizeof(STYLES) / sizeof(STYLES[0]);
         ++style_index) {
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = sr_test_key(false);
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            key.stage_expression = expression;
            assert(face_salvage_redux_render(
                STYLES[style_index],
                &key,
                5064U,
                frame,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            expression_hashes[expression] = sr_test_contact_hash(
                frame, 0U, 0U, 40U, 30U);
            for (uint8_t previous = 0U;
                 previous < expression;
                 ++previous) {
                assert(expression_hashes[expression] !=
                    expression_hashes[previous]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        size_t distinct_visemes = 0U;
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            face_render_key_t key = sr_test_key(true);
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            assert(face_salvage_redux_render(
                STYLES[style_index],
                &key,
                11531U,
                frame,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            /*
             * Hash only the exact-40x30 mouth/muzzle ROI.  Static head
             * furniture cannot make a collapsed mouth family pass.
             */
            viseme_hashes[viseme] = sr_test_contact_hash(
                frame, 10U, 17U, 30U, 27U);
            bool seen = false;
            for (uint8_t previous = 0U; previous < viseme; ++previous) {
                seen = seen ||
                    viseme_hashes[viseme] == viseme_hashes[previous];
            }
            distinct_visemes += seen ? 0U : 1U;
        }
        assert(distinct_visemes >= 10U);
    }
}

static void sr_test_all_ir_bytes_influence_every_actor(void)
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
    face_render_key_t baseline = sr_test_key(true);
    baseline.viseme_weight = 128U;
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        uint16_t reference[FACE_SALVAGE_REDUX_PIXEL_COUNT];
        uint16_t changed[FACE_SALVAGE_REDUX_PIXEL_COUNT];
        assert(face_salvage_redux_render(
            style,
            &baseline,
            17231U,
            reference,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        for (size_t byte = 0U; byte < FACE_RENDER_KEY_BYTES; ++byte) {
            face_render_key_t probe = baseline;
            uint8_t *bytes = (uint8_t *)&probe;
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
                const int8_t current = (int8_t)bytes[byte];
                bytes[byte] = (uint8_t)(
                    current >= 0 ? (int8_t)-127 : (int8_t)127);
            } else {
                bytes[byte] = bytes[byte] < 128U ? 255U : 0U;
            }
            assert(face_salvage_redux_render(
                style,
                &probe,
                17231U,
                changed,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            const size_t difference =
                sr_test_difference(reference, changed);
            if (difference == 0U) {
                face_salvage_redux_pose_t reference_pose;
                face_salvage_redux_pose_t changed_pose;
                assert(face_salvage_redux_resolve(
                    style, &baseline, 17231U, &reference_pose));
                assert(face_salvage_redux_resolve(
                    style, &probe, 17231U, &changed_pose));
                fprintf(
                    stderr,
                    "IR byte has no influence: style=%zu byte=%zu "
                    "detail=%u/%u eye=%d/%d mouth=%dx%d/%dx%d\n",
                    raw,
                    byte,
                    reference_pose.detail_phase,
                    changed_pose.detail_phase,
                    reference_pose.eye_open[0],
                    changed_pose.eye_open[0],
                    reference_pose.mouth_w,
                    reference_pose.mouth_h,
                    changed_pose.mouth_w,
                    changed_pose.mouth_h);
            }
            assert(difference > 0U);
        }
    }
}

static void sr_test_fixed_anchors_anticipation_and_settle(void)
{
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        face_render_key_t key = sr_test_key(true);
        face_salvage_redux_pose_t active;
        face_salvage_redux_pose_t anticipation_one;
        face_salvage_redux_pose_t anticipation_two;
        face_salvage_redux_pose_t settle_one;
        face_salvage_redux_pose_t settle_two;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        assert(face_salvage_redux_resolve(
            style, &key, 3198U, &active));
        key.speech_phase = FACE_SPEECH_STARTING;
        assert(face_salvage_redux_resolve(
            style, &key, 533U, &anticipation_one));
        assert(face_salvage_redux_resolve(
            style, &key, 1066U, &anticipation_two));
        key.speech_phase = FACE_SPEECH_ENDING;
        assert(face_salvage_redux_resolve(
            style, &key, 7995U, &settle_one));
        assert(face_salvage_redux_resolve(
            style, &key, 8528U, &settle_two));
        for (size_t eye = 0U; eye < 2U; ++eye) {
            assert(active.eye_x[eye] == anticipation_one.eye_x[eye]);
            assert(active.eye_x[eye] == anticipation_two.eye_x[eye]);
            assert(active.eye_x[eye] == settle_one.eye_x[eye]);
            assert(active.eye_x[eye] == settle_two.eye_x[eye]);
            assert(active.eye_y[eye] == anticipation_one.eye_y[eye]);
            assert(active.eye_y[eye] == anticipation_two.eye_y[eye]);
            assert(active.eye_y[eye] == settle_one.eye_y[eye]);
            assert(active.eye_y[eye] == settle_two.eye_y[eye]);
        }
        assert(active.mouth_x == anticipation_one.mouth_x);
        assert(active.mouth_x == anticipation_two.mouth_x);
        assert(active.mouth_x == settle_one.mouth_x);
        assert(active.mouth_x == settle_two.mouth_x);
        assert(active.mouth_y == anticipation_one.mouth_y);
        assert(active.mouth_y == anticipation_two.mouth_y);
        assert(active.mouth_y == settle_one.mouth_y);
        assert(active.mouth_y == settle_two.mouth_y);
        if (style == FACE_SALVAGE_REDUX_VELA_EYES) {
            assert(anticipation_one.eye_open[0] >= active.eye_open[0]);
            assert(anticipation_two.eye_open[0] >= active.eye_open[0]);
            assert(settle_one.eye_open[0] <= active.eye_open[0]);
            assert(settle_two.eye_open[0] <= active.eye_open[0]);
        } else {
            assert(anticipation_one.mouth_h < active.mouth_h);
            assert(anticipation_two.mouth_h < active.mouth_h);
            assert(settle_one.mouth_h < active.mouth_h);
            assert(settle_two.mouth_h < active.mouth_h);
        }

        /* Expressions and extreme input never move facial anchors. */
        const face_salvage_redux_pose_t reference = active;
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            key = sr_test_key(true);
            key.stage_expression = expression;
            key.head_yaw = (int8_t)(expression * 12 - 60);
            key.head_pitch = (int8_t)(50 - expression * 10);
            key.body_lean_x = (int8_t)(expression * 11 - 55);
            key.body_lean_y = (int8_t)(45 - expression * 9);
            face_salvage_redux_pose_t expression_pose;
            assert(face_salvage_redux_resolve(
                style, &key, expression * 971U, &expression_pose));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(expression_pose.eye_x[eye] ==
                    reference.eye_x[eye]);
                assert(expression_pose.eye_y[eye] ==
                    reference.eye_y[eye]);
                assert(expression_pose.eye_h[eye] ==
                    reference.eye_h[eye]);
            }
            assert(expression_pose.mouth_x == reference.mouth_x);
            assert(expression_pose.mouth_y == reference.mouth_y);
        }
    }
}

static void sr_test_slow_wave_and_temporal_continuity(void)
{
    face_render_key_t key = sr_test_key(true);
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        face_salvage_redux_pose_t first;
        face_salvage_redux_pose_t old_period;
        face_salvage_redux_pose_t repeat;
        assert(face_salvage_redux_resolve(
            style, &key, 2400U, &first));
        assert(face_salvage_redux_resolve(
            style, &key, 2400U + 1066U, &old_period));
        assert(face_salvage_redux_resolve(
            style, &key, 2400U + 9600U, &repeat));
        assert(first.speech_wave != old_period.speech_wave);
        assert(first.speech_wave == repeat.speech_wave);

        uint16_t previous[FACE_SALVAGE_REDUX_PIXEL_COUNT];
        uint16_t current[FACE_SALVAGE_REDUX_PIXEL_COUNT];
        assert(face_salvage_redux_render(
            style,
            &key,
            0U,
            previous,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        for (uint32_t frame = 1U; frame < 180U; ++frame) {
            assert(face_salvage_redux_render(
                style,
                &key,
                frame * 533U,
                current,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            assert(sr_test_difference(previous, current) < 1600U);
            memcpy(previous, current, sizeof(previous));
        }
    }
}

static void sr_test_adversarial(void)
{
    for (size_t case_index = 0U;
         case_index < SR_TEST_FUZZ_CASES;
         ++case_index) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t byte = 0U; byte < sizeof(key); ++byte) {
            bytes[byte] = (uint8_t)sr_test_rng_next();
        }
        const uint32_t sample_clock = sr_test_rng_next();
        for (size_t raw = 0U;
             raw < FACE_SALVAGE_REDUX_COUNT;
             ++raw) {
            const face_salvage_redux_style_t style =
                (face_salvage_redux_style_t)raw;
            sr_test_guarded_frame_t frame;
            face_salvage_redux_pose_t pose;
            sr_test_init_guard(&frame);
            assert(face_salvage_redux_render(
                style,
                &key,
                sample_clock,
                frame.pixels,
                FACE_SALVAGE_REDUX_PIXEL_COUNT));
            assert(face_salvage_redux_resolve(
                style, &key, sample_clock, &pose));
            sr_test_check_guard(&frame);
            sr_test_check_border(frame.pixels);
            sr_test_pose_bounds(style, &pose);
        }
    }
}

static void sr_test_legacy_wrapper(void)
{
    const face_render_key_t key = sr_test_key(true);
    uint16_t direct[FACE_SALVAGE_REDUX_PIXEL_COUNT];
    uint16_t legacy[FACE_SALVAGE_REDUX_PIXEL_COUNT];
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        const face_salvage_redux_style_t style =
            (face_salvage_redux_style_t)raw;
        face_salvage_redux_info_t info;
        assert(face_salvage_redux_info(style, &info));
        assert(face_salvage_redux_render(
            style,
            &key,
            9119U,
            direct,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        assert(face_salvage_redux_render_legacy(
            info.legacy_profile_id,
            &key,
            9119U,
            legacy,
            FACE_SALVAGE_REDUX_PIXEL_COUNT));
        assert(memcmp(direct, legacy, sizeof(direct)) == 0);
    }
    assert(!face_salvage_redux_render_legacy(
        63U,
        &key,
        0U,
        legacy,
        FACE_SALVAGE_REDUX_PIXEL_COUNT));
}

int main(void)
{
    sr_test_metadata();
    sr_test_rejection_determinism_and_silhouettes();
    sr_test_expressions_and_visemes();
    sr_test_contact_scale_acting();
    sr_test_all_ir_bytes_influence_every_actor();
    sr_test_fixed_anchors_anticipation_and_settle();
    sr_test_slow_wave_and_temporal_continuity();
    sr_test_adversarial();
    sr_test_legacy_wrapper();
    printf(
        "face_salvage_redux_actors_test: PASS "
        "(6 distinct grammars, 11 expressions, 15 visemes, "
        "10+ contact-scale mouth families, 40-byte IR, fixed anchors, "
        "2 anticipation + 2 settle frames, "
        "9600-sample speech wave, %d adversarial renders)\n",
        SR_TEST_FUZZ_CASES * FACE_SALVAGE_REDUX_COUNT);
    return 0;
}
