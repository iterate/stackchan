#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pose.h"
#include "face_salvage_actors.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 256,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t rng_state = 0x6a09e667U;

static uint32_t rng_next(void)
{
    uint32_t value = rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rng_state = value;
    return value;
}

static face_render_key_t baseline_key(void)
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

static void init_guard(guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0x51a70000U + (uint32_t)index;
        frame->after[index] = 0xa7150000U + (uint32_t)index;
    }
}

static void check_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x51a70000U + index);
        assert(frame->after[index] == 0xa7150000U + index);
    }
}

static void check_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_SALVAGE_ACTOR_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_SALVAGE_ACTOR_WIDTH; ++x) {
            if (x < 4U || x >= FACE_SALVAGE_ACTOR_WIDTH - 4U ||
                y < 4U || y >= FACE_SALVAGE_ACTOR_HEIGHT - 4U) {
                assert(
                    pixels[y * FACE_SALVAGE_ACTOR_WIDTH + x] ==
                    background);
            }
        }
    }
}

static uint32_t hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_SALVAGE_ACTOR_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t differing_pixels(
    const uint16_t *first,
    const uint16_t *second)
{
    size_t count = 0U;
    for (size_t index = 0U;
         index < FACE_SALVAGE_ACTOR_PIXEL_COUNT;
         ++index) {
        count += first[index] != second[index];
    }
    return count;
}

static void test_metadata_and_mapping(void)
{
    static const uint8_t IDS[FACE_SALVAGE_ACTOR_COUNT] = {
        4U, 6U, 29U, 35U, 36U, 52U,
    };
    assert(face_salvage_actor_count() == FACE_SALVAGE_ACTOR_COUNT);
    for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
        const face_salvage_actor_style_t style =
            (face_salvage_actor_style_t)raw;
        face_salvage_actor_info_t info;
        assert(face_salvage_actor_info(style, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.legacy_profile_id == IDS[raw]);
        assert(strcmp(info.slug, face_salvage_actor_slug(style)) == 0);
        assert(strcmp(info.name, face_salvage_actor_name(style)) == 0);
        assert(info.deliberate_monocular ==
            (style == FACE_SALVAGE_ACTOR_RED_OPTIC));
        for (size_t previous = 0U; previous < raw; ++previous) {
            assert(strcmp(
                info.slug,
                face_salvage_actor_slug(
                    (face_salvage_actor_style_t)previous)) != 0);
        }
        face_salvage_actor_style_t mapped =
            FACE_SALVAGE_ACTOR_COUNT;
        assert(face_salvage_actor_from_legacy_id(IDS[raw], &mapped));
        assert(mapped == style);
    }
    face_salvage_actor_style_t mapped =
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL;
    assert(!face_salvage_actor_from_legacy_id(5U, &mapped));
    assert(!face_salvage_actor_from_legacy_id(62U, &mapped));
    assert(!face_salvage_actor_from_legacy_id(4U, NULL));
    assert(face_salvage_actor_slug(FACE_SALVAGE_ACTOR_COUNT) == NULL);
    assert(face_salvage_actor_name((face_salvage_actor_style_t)-1) == NULL);
    assert(!face_salvage_actor_info(FACE_SALVAGE_ACTOR_COUNT, NULL));
}

static void test_rejection_determinism_and_ir_boundary(void)
{
    const face_render_key_t key = baseline_key();
    guarded_frame_t first;
    guarded_frame_t second;
    init_guard(&first);
    init_guard(&second);
    assert(!face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_COUNT,
        &key,
        0U,
        first.pixels,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT));
    assert(!face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL,
        NULL,
        0U,
        first.pixels,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT));
    assert(!face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL,
        &key,
        0U,
        NULL,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT));
    assert(!face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL,
        &key,
        0U,
        first.pixels,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT - 1U));
    assert(face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL,
        &key,
        17231U,
        first.pixels,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT));
    assert(face_salvage_actor_render(
        FACE_SALVAGE_ACTOR_AMBER_TERMINAL,
        &key,
        17231U,
        second.pixels,
        FACE_SALVAGE_ACTOR_PIXEL_COUNT));
    assert(memcmp(first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    check_guard(&first);
    check_guard(&second);

    face_salvage_actor_pose_t pose;
    assert(face_salvage_actor_resolve(
        FACE_SALVAGE_ACTOR_ZINE_ROGUE, &key, 9U, &pose));
    assert(memcmp(&pose.source, &key, FACE_RENDER_KEY_BYTES) == 0);
    assert(!face_salvage_actor_resolve(
        FACE_SALVAGE_ACTOR_COUNT, &key, 9U, &pose));
    assert(!face_salvage_actor_resolve(
        FACE_SALVAGE_ACTOR_ZINE_ROGUE, NULL, 9U, &pose));
    assert(!face_salvage_actor_resolve(
        FACE_SALVAGE_ACTOR_ZINE_ROGUE, &key, 9U, NULL));
}

static void assert_pose_bounds(
    face_salvage_actor_style_t style,
    const face_salvage_actor_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (style == FACE_SALVAGE_ACTOR_RED_OPTIC && eye == 1U) {
            assert(pose->eye_w[eye] == 0);
            assert(pose->eye_h[eye] == 0);
            assert(pose->eye_aperture[eye] == 0);
            continue;
        }
        assert(pose->eye_x[eye] >= 35 && pose->eye_x[eye] <= 125);
        assert(pose->eye_y[eye] >= 35 && pose->eye_y[eye] <= 70);
        assert(pose->eye_w[eye] >= 18 && pose->eye_w[eye] <= 72);
        assert(pose->eye_h[eye] >= 20 && pose->eye_h[eye] <= 64);
        assert(pose->eye_aperture[eye] >= 2);
        assert(pose->eye_aperture[eye] <= pose->eye_h[eye]);
        assert(pose->pupil_x[eye] >= 20 && pose->pupil_x[eye] <= 140);
        assert(pose->pupil_y[eye] >= 25 && pose->pupil_y[eye] <= 85);
        assert(pose->pupil_radius[eye] >= 2);
        assert(pose->pupil_radius[eye] <= 17);
        assert(pose->brow_y[eye] >= 12 && pose->brow_y[eye] <= 50);
        assert(pose->brow_slope[eye] >= -11);
        assert(pose->brow_slope[eye] <= 11);
    }
    assert(pose->mouth_x == 80);
    assert(pose->mouth_y >= 80 && pose->mouth_y <= 95);
    assert(pose->mouth_w >= 14 && pose->mouth_w <= 68);
    assert(pose->mouth_h >= 2 && pose->mouth_h <= 29);
    assert(pose->shoulder_lean_x >= -8);
    assert(pose->shoulder_lean_x <= 8);
    assert(pose->shoulder_lean_y >= -6);
    assert(pose->shoulder_lean_y <= 6);
}

static void test_expression_viseme_and_action_response(void)
{
    for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
        const face_salvage_actor_style_t style =
            (face_salvage_actor_style_t)raw;
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = baseline_key();
            key.stage_expression = expression;
            key.expression_weight = 255U;
            uint16_t frame[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
            assert(face_salvage_actor_render(
                style, &key, 5064U, frame, FACE_SALVAGE_ACTOR_PIXEL_COUNT));
            expression_hashes[expression] = hash_pixels(frame);
            face_salvage_actor_pose_t pose;
            assert(face_salvage_actor_resolve(
                style, &key, 5064U, &pose));
            assert_pose_bounds(style, &pose);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                assert(expression_hashes[expression] !=
                    expression_hashes[FACE_EXPRESSION_NEUTRAL]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            face_render_key_t key = baseline_key();
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            uint16_t frame[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
            assert(face_salvage_actor_render(
                style, &key, 11531U, frame,
                FACE_SALVAGE_ACTOR_PIXEL_COUNT));
            viseme_hashes[viseme] = hash_pixels(frame);
            for (uint8_t previous = 0U; previous < viseme; ++previous) {
                if (viseme_hashes[viseme] == viseme_hashes[previous]) {
                    fprintf(
                        stderr,
                        "viseme collision: style=%zu viseme=%u previous=%u\n",
                        raw,
                        viseme,
                        previous);
                }
                assert(viseme_hashes[viseme] !=
                    viseme_hashes[previous]);
            }
        }

        face_render_key_t rest = baseline_key();
        rest.controls.flags = 0U;
        rest.controls.expression = FACE_ACTIVITY_LISTENING;
        rest.speech_phase = FACE_SPEECH_IDLE;
        rest.controls.mouth_open = 0U;
        rest.viseme = FACE_VISEME_SIL;
        rest.viseme_secondary = FACE_VISEME_SIL;
        uint16_t first[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
        uint16_t second[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
        assert(face_salvage_actor_render(
            style, &rest, 4000U, first, FACE_SALVAGE_ACTOR_PIXEL_COUNT));
        face_render_key_t action = rest;
        action.head_yaw = 90;
        action.head_pitch = -70;
        action.body_lean_x = 70;
        action.body_lean_y = -60;
        action.affect_valence = -90;
        action.affect_arousal = 240U;
        action.attention = 25U;
        assert(face_salvage_actor_render(
            style, &action, 4000U, second, FACE_SALVAGE_ACTOR_PIXEL_COUNT));
        assert(differing_pixels(first, second) > 20U);
    }
}

static void test_fixed_anchors_and_continuity(void)
{
    for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
        const face_salvage_actor_style_t style =
            (face_salvage_actor_style_t)raw;
        face_render_key_t key = baseline_key();
        face_salvage_actor_pose_t neutral;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        assert(face_salvage_actor_resolve(style, &key, 0U, &neutral));
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_salvage_actor_pose_t expression_pose;
            key.stage_expression = expression;
            key.head_yaw = (int8_t)(expression * 7 - 35);
            key.head_pitch = (int8_t)(30 - expression * 5);
            assert(face_salvage_actor_resolve(
                style, &key, expression * 971U, &expression_pose));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(expression_pose.eye_x[eye] == neutral.eye_x[eye]);
                assert(expression_pose.eye_y[eye] == neutral.eye_y[eye]);
                assert(expression_pose.eye_h[eye] == neutral.eye_h[eye]);
            }
            assert(expression_pose.mouth_x == neutral.mouth_x);
            assert(expression_pose.mouth_y == neutral.mouth_y);
        }

        key = baseline_key();
        key.stage_expression = FACE_EXPRESSION_WARM;
        uint16_t previous[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
        uint16_t current[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
        assert(face_salvage_actor_render(
            style, &key, 0U, previous, FACE_SALVAGE_ACTOR_PIXEL_COUNT));
        size_t largest_change = 0U;
        for (uint32_t frame = 1U; frame < 180U; ++frame) {
            assert(face_salvage_actor_render(
                style,
                &key,
                frame * 533U,
                current,
                FACE_SALVAGE_ACTOR_PIXEL_COUNT));
            const size_t changed = differing_pixels(previous, current);
            if (changed > largest_change) {
                largest_change = changed;
            }
            assert(changed < 1800U);
            memcpy(previous, current, sizeof(previous));
        }
        (void)largest_change;
    }
}

static void test_adversarial_frames(void)
{
    for (size_t case_index = 0U; case_index < FUZZ_CASES; ++case_index) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0U; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)rng_next();
        }
        const uint32_t sample_clock = rng_next();
        for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
            const face_salvage_actor_style_t style =
                (face_salvage_actor_style_t)raw;
            guarded_frame_t frame;
            init_guard(&frame);
            assert(face_salvage_actor_render(
                style,
                &key,
                sample_clock,
                frame.pixels,
                FACE_SALVAGE_ACTOR_PIXEL_COUNT));
            check_guard(&frame);
            check_safe_border(frame.pixels);
            face_salvage_actor_pose_t pose;
            assert(face_salvage_actor_resolve(
                style, &key, sample_clock, &pose));
            assert_pose_bounds(style, &pose);
        }
    }
}

static void test_legacy_render(void)
{
    const face_render_key_t key = baseline_key();
    uint16_t direct[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
    uint16_t legacy[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
    for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
        face_salvage_actor_info_t info;
        assert(face_salvage_actor_info(
            (face_salvage_actor_style_t)raw, &info));
        assert(face_salvage_actor_render(
            (face_salvage_actor_style_t)raw,
            &key,
            9119U,
            direct,
            FACE_SALVAGE_ACTOR_PIXEL_COUNT));
        assert(face_salvage_actor_render_legacy(
            info.legacy_profile_id,
            &key,
            9119U,
            legacy,
            FACE_SALVAGE_ACTOR_PIXEL_COUNT));
        assert(memcmp(direct, legacy, sizeof(direct)) == 0);
    }
    assert(!face_salvage_actor_render_legacy(
        63U, &key, 0U, legacy, FACE_SALVAGE_ACTOR_PIXEL_COUNT));
}

int main(void)
{
    test_metadata_and_mapping();
    test_rejection_determinism_and_ir_boundary();
    test_expression_viseme_and_action_response();
    test_fixed_anchors_and_continuity();
    test_adversarial_frames();
    test_legacy_render();
    printf(
        "face_salvage_actors_test: PASS "
        "(6 replacements, 11 expressions, 15 visemes, "
        "%d adversarial frames, fixed anchors, schema-v2 IR)\n",
        FUZZ_CASES * FACE_SALVAGE_ACTOR_COUNT);
    return 0;
}
