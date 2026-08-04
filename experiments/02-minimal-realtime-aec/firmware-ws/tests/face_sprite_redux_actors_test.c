#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pose.h"
#include "face_sprite_redux_actors.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 512,
    AUDIT_WIDTH = 40,
    AUDIT_HEIGHT = 30,
    AUDIT_DIVISOR = 4,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t random_state = 0x72656475U;

static uint32_t next_random(void)
{
    uint32_t value = random_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    random_state = value;
    return value;
}

static face_render_key_t baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 126U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 46U;
    key.controls.mouth_press = 28U;
    key.controls.mouth_teeth = 118U;
    key.controls.eye_left_open = 224U;
    key.controls.eye_right_open = 218U;
    key.controls.look_x = 11;
    key.controls.look_y = -7;
    key.controls.brow = 12;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_TH;
    key.phoneme = 12U;
    key.viseme_weight = 214U;
    key.audio_level = 136U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 46U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 11;
    key.mouth_corner_right = 7;
    key.tongue = 74U;
    key.cheek = 24U;
    key.eye_left_squint = 9U;
    key.eye_right_squint = 6U;
    key.brow_inner = 8;
    key.brow_outer_left = -7;
    key.brow_outer_right = 9;
    key.head_roll = 4;
    key.affect_valence = 14;
    key.affect_arousal = 142U;
    key.head_yaw = 5;
    key.head_pitch = -4;
    key.body_lean_x = 4;
    key.body_lean_y = -3;
    key.expression_weight = 255U;
    key.attention = 204U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

static face_render_key_t preview_key(uint8_t expression)
{
    face_render_key_t key = baseline_key();
    key.controls.mouth_open = 7U;
    key.controls.mouth_width = 144U;
    key.controls.mouth_round = 24U;
    key.controls.mouth_press = 182U;
    key.controls.mouth_teeth = 0U;
    key.controls.eye_left_open = 222U;
    key.controls.eye_right_open = 222U;
    key.controls.look_x = 0;
    key.controls.look_y = 0;
    key.controls.brow = 0;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.controls.flags = 0U;
    key.viseme = FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 0U;
    key.audio_level = 5U;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_blend = 0U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.mouth_corner_left = 0;
    key.mouth_corner_right = 0;
    key.tongue = 0U;
    key.cheek = 14U;
    key.eye_left_squint = 0U;
    key.eye_right_squint = 0U;
    key.brow_inner = 0;
    key.brow_outer_left = 0;
    key.brow_outer_right = 0;
    key.head_roll = 0;
    key.affect_valence = 0;
    key.affect_arousal = 118U;
    key.head_yaw = 0;
    key.head_pitch = 0;
    key.body_lean_x = 0;
    key.body_lean_y = 0;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.stage_expression = expression;
    return key;
}

static void init_guard(guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0x51a70000U + (uint32_t)index;
        frame->after[index] = 0xa7510000U + (uint32_t)index;
    }
}

static void assert_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x51a70000U + index);
        assert(frame->after[index] == 0xa7510000U + index);
    }
}

static void assert_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_SPRITE_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_SPRITE_REDUX_WIDTH; ++x) {
            if (x < 8U || x >= FACE_SPRITE_REDUX_WIDTH - 8U ||
                y < 8U || y >= FACE_SPRITE_REDUX_HEIGHT - 8U) {
                assert(
                    pixels[y * FACE_SPRITE_REDUX_WIDTH + x] ==
                    background);
            }
        }
    }
}

static void downsample_exact_40x30(
    const uint16_t *native,
    uint16_t *audit)
{
    for (size_t y = 0U; y < AUDIT_HEIGHT; ++y) {
        for (size_t x = 0U; x < AUDIT_WIDTH; ++x) {
            const size_t source_x =
                x * AUDIT_DIVISOR + AUDIT_DIVISOR / 2U;
            const size_t source_y =
                y * AUDIT_DIVISOR + AUDIT_DIVISOR / 2U;
            audit[y * AUDIT_WIDTH + x] =
                native[source_y * FACE_SPRITE_REDUX_WIDTH + source_x];
        }
    }
}

static void assert_audit_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < AUDIT_HEIGHT; ++y) {
        for (size_t x = 0U; x < AUDIT_WIDTH; ++x) {
            if (x < 2U || x >= AUDIT_WIDTH - 2U ||
                y < 2U || y >= AUDIT_HEIGHT - 2U) {
                assert(pixels[y * AUDIT_WIDTH + x] == background);
            }
        }
    }
}

static uint32_t hash_audit_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < AUDIT_WIDTH * AUDIT_HEIGHT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t differing_audit_pixels(
    const uint16_t *left,
    const uint16_t *right)
{
    size_t changed = 0U;
    for (size_t index = 0U; index < AUDIT_WIDTH * AUDIT_HEIGHT; ++index) {
        changed += left[index] != right[index] ? 1U : 0U;
    }
    return changed;
}

static bool anatomy_differs(
    const face_sprite_redux_pose_t *left,
    const face_sprite_redux_pose_t *right)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (left->eye_w[eye] != right->eye_w[eye] ||
            left->eye_open[eye] != right->eye_open[eye] ||
            left->pupil_x[eye] != right->pupil_x[eye] ||
            left->pupil_y[eye] != right->pupil_y[eye] ||
            left->brow_outer_y[eye] != right->brow_outer_y[eye] ||
            left->brow_inner_y[eye] != right->brow_inner_y[eye] ||
            left->silhouette_lift[eye] !=
                right->silhouette_lift[eye] ||
            left->silhouette_tilt[eye] !=
                right->silhouette_tilt[eye]) {
            return true;
        }
    }
    return left->mouth_w != right->mouth_w ||
        left->mouth_h != right->mouth_h ||
        left->mouth_corner_y[0] != right->mouth_corner_y[0] ||
        left->mouth_corner_y[1] != right->mouth_corner_y[1] ||
        left->body_lean_x != right->body_lean_x ||
        left->body_lean_y != right->body_lean_y;
}

static uint32_t hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_SPRITE_REDUX_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t differing_pixels(
    const uint16_t *left,
    const uint16_t *right)
{
    size_t changed = 0U;
    for (size_t index = 0U;
         index < FACE_SPRITE_REDUX_PIXEL_COUNT;
         ++index) {
        changed += left[index] != right[index] ? 1U : 0U;
    }
    return changed;
}

static void assert_pose_bounds(const face_sprite_redux_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->eye_x[eye] >= 20 && pose->eye_x[eye] <= 60);
        assert(pose->eye_y[eye] >= 18 && pose->eye_y[eye] <= 34);
        assert(pose->eye_w[eye] >= 8 && pose->eye_w[eye] <= 18);
        assert(pose->eye_h[eye] >= 7 && pose->eye_h[eye] <= 10);
        assert(pose->eye_open[eye] >= 1);
        assert(pose->eye_open[eye] <= pose->eye_h[eye]);
        assert(pose->pupil_x[eye] >= pose->eye_x[eye] - 6);
        assert(pose->pupil_x[eye] <= pose->eye_x[eye] + 6);
        assert(pose->pupil_y[eye] >= pose->eye_y[eye] - 5);
        assert(pose->pupil_y[eye] <= pose->eye_y[eye] + 5);
        assert(pose->brow_outer_y[eye] >= 8);
        assert(pose->brow_outer_y[eye] <= 29);
        assert(pose->brow_inner_y[eye] >= 8);
        assert(pose->brow_inner_y[eye] <= 29);
    }
    assert(pose->mouth_x == 40);
    assert(pose->mouth_y >= 40 && pose->mouth_y <= 46);
    assert(pose->mouth_w >= 7 && pose->mouth_w <= 31);
    assert(pose->mouth_h >= 1 && pose->mouth_h <= 14);
    assert(pose->mouth_corner_y[0] >= -6);
    assert(pose->mouth_corner_y[0] <= 6);
    assert(pose->mouth_corner_y[1] >= -6);
    assert(pose->mouth_corner_y[1] <= 6);
    assert(pose->body_lean_x >= -4 && pose->body_lean_x <= 4);
    assert(pose->body_lean_y >= -3 && pose->body_lean_y <= 3);
    assert(pose->head_yaw >= -3 && pose->head_yaw <= 3);
    assert(pose->head_pitch >= -2 && pose->head_pitch <= 2);
    assert(pose->head_roll >= -3 && pose->head_roll <= 3);
    assert(pose->mouth_cel < FACE_VISEME_COUNT);
    assert(pose->viseme_index < FACE_VISEME_COUNT);
    for (size_t side = 0U; side < 2U; ++side) {
        assert(pose->silhouette_lift[side] >= -6);
        assert(pose->silhouette_lift[side] <= 5);
        assert(pose->silhouette_tilt[side] >= -7);
        assert(pose->silhouette_tilt[side] <= 7);
    }
}

static void test_metadata_mapping_and_contract(void)
{
    static const uint8_t legacy_ids[] = {58U, 59U, 60U};
    static const uint8_t palette_sizes[] = {24U, 24U, 4U};
    static const char *const slugs[] = {
        "sprite-redux-talkie-moon-mechanic",
        "sprite-redux-jrpg-storm-familiar",
        "sprite-redux-handheld-forest-pet",
    };
    assert(face_sprite_redux_actor_count() ==
        FACE_SPRITE_REDUX_ACTOR_COUNT);
    assert(strcmp(
        face_sprite_redux_actor_slug(
            FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC),
        slugs[0]) == 0);
    assert(strcmp(
        face_sprite_redux_actor_slug(
            (face_sprite_redux_actor_t)99),
        "invalid-sprite-redux-actor") == 0);
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        face_sprite_redux_actor_info_t info;
        assert(face_sprite_redux_actor_info(
            (face_sprite_redux_actor_t)raw, &info));
        assert(strcmp(info.slug, slugs[raw]) == 0);
        assert(strlen(info.name) > 20U);
        assert(info.legacy_profile_id == legacy_ids[raw]);
        assert(info.logical_width == 80U);
        assert(info.logical_height == 60U);
        assert(info.palette_size == palette_sizes[raw]);
        face_sprite_redux_actor_t mapped;
        assert(face_sprite_redux_actor_from_legacy_id(
            legacy_ids[raw], &mapped));
        assert(mapped == (face_sprite_redux_actor_t)raw);
    }
    face_sprite_redux_actor_t invalid;
    assert(!face_sprite_redux_actor_from_legacy_id(57U, &invalid));
    assert(!face_sprite_redux_actor_from_legacy_id(58U, NULL));
    assert(!face_sprite_redux_actor_info(
        (face_sprite_redux_actor_t)99, NULL));
}

static void test_rejection_determinism_and_source(void)
{
    face_render_key_t key = baseline_key();
    uint16_t first[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint16_t second[FACE_SPRITE_REDUX_PIXEL_COUNT];
    assert(!face_sprite_redux_actor_render(
        (face_sprite_redux_actor_t)99, &key, 0U,
        first, FACE_SPRITE_REDUX_PIXEL_COUNT));
    assert(!face_sprite_redux_actor_render(
        FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC, NULL, 0U,
        first, FACE_SPRITE_REDUX_PIXEL_COUNT));
    assert(!face_sprite_redux_actor_render(
        FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC, &key, 0U,
        NULL, FACE_SPRITE_REDUX_PIXEL_COUNT));
    assert(!face_sprite_redux_actor_render(
        FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC, &key, 0U,
        first, FACE_SPRITE_REDUX_PIXEL_COUNT - 1U));
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        const face_sprite_redux_actor_t actor =
            (face_sprite_redux_actor_t)raw;
        assert(face_sprite_redux_actor_render(
            actor, &key, 7123U,
            first, FACE_SPRITE_REDUX_PIXEL_COUNT));
        assert(face_sprite_redux_actor_render(
            actor, &key, 7123U,
            second, FACE_SPRITE_REDUX_PIXEL_COUNT));
        assert(memcmp(first, second, sizeof(first)) == 0);
        face_sprite_redux_pose_t pose;
        assert(face_sprite_redux_actor_resolve(
            actor, &key, 7123U, &pose));
        assert(memcmp(
            &pose.source, &key, sizeof(key)) == 0);
        assert_pose_bounds(&pose);
    }
}

static void test_all_expressions_and_visemes(void)
{
    uint16_t neutral[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint16_t current[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint16_t neutral_audit[AUDIT_WIDTH * AUDIT_HEIGHT];
    uint16_t current_audit[AUDIT_WIDTH * AUDIT_HEIGHT];
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        const face_sprite_redux_actor_t actor =
            (face_sprite_redux_actor_t)raw;
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        uint32_t expression_audit_hashes[FACE_EXPRESSION_COUNT];
        face_sprite_redux_pose_t expression_poses[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key = preview_key(expression);
            assert(face_sprite_redux_actor_resolve(
                actor,
                &key,
                4096U,
                &expression_poses[expression]));
            assert(face_sprite_redux_actor_render(
                actor, &key, 4096U, current,
                FACE_SPRITE_REDUX_PIXEL_COUNT));
            expression_hashes[expression] = hash_pixels(current);
            downsample_exact_40x30(current, current_audit);
            assert_audit_safe_border(current_audit);
            expression_audit_hashes[expression] =
                hash_audit_pixels(current_audit);
            if (expression == FACE_EXPRESSION_NEUTRAL) {
                memcpy(neutral, current, sizeof(neutral));
                memcpy(
                    neutral_audit,
                    current_audit,
                    sizeof(neutral_audit));
            } else {
                assert(differing_pixels(neutral, current) > 120U);
                assert(differing_audit_pixels(
                    neutral_audit, current_audit) > 12U);
            }
        }
        for (size_t left = 0U;
             left < FACE_EXPRESSION_COUNT;
             ++left) {
            for (size_t right = left + 1U;
                 right < FACE_EXPRESSION_COUNT;
                 ++right) {
                assert(expression_hashes[left] !=
                    expression_hashes[right]);
                assert(expression_audit_hashes[left] !=
                    expression_audit_hashes[right]);
                assert(anatomy_differs(
                    &expression_poses[left],
                    &expression_poses[right]));
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        uint32_t viseme_audit_hashes[FACE_VISEME_COUNT];
        size_t unique = 0U;
        size_t audit_unique = 0U;
        for (uint8_t viseme = 0U;
             viseme < FACE_VISEME_COUNT;
             ++viseme) {
            face_render_key_t key = baseline_key();
            key.stage_expression = FACE_EXPRESSION_NEUTRAL;
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            assert(face_sprite_redux_actor_render(
                actor, &key, 8192U, current,
                FACE_SPRITE_REDUX_PIXEL_COUNT));
            viseme_hashes[viseme] = hash_pixels(current);
            downsample_exact_40x30(current, current_audit);
            assert_audit_safe_border(current_audit);
            viseme_audit_hashes[viseme] =
                hash_audit_pixels(current_audit);
            face_sprite_redux_pose_t pose;
            assert(face_sprite_redux_actor_resolve(
                actor, &key, 8192U, &pose));
            assert(pose.mouth_cel == viseme);
            bool first = true;
            bool first_audit = true;
            for (uint8_t before = 0U; before < viseme; ++before) {
                if (viseme_hashes[before] == viseme_hashes[viseme]) {
                    first = false;
                }
                if (viseme_audit_hashes[before] ==
                    viseme_audit_hashes[viseme]) {
                    first_audit = false;
                }
            }
            unique += first ? 1U : 0U;
            audit_unique += first_audit ? 1U : 0U;
        }
        assert(unique >= 10U);
        assert(audit_unique >= 10U);
    }
}

static void test_full_ir_visual_consumption(void)
{
    static const uint8_t candidates[] = {
        0x00U, 0x01U, 0x02U, 0x35U, 0x7fU, 0x80U, 0xaaU, 0xffU,
    };
    uint16_t baseline[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint16_t changed[FACE_SPRITE_REDUX_PIXEL_COUNT];
    bool all_visible = true;
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        const face_sprite_redux_actor_t actor =
            (face_sprite_redux_actor_t)raw;
        const face_render_key_t key = baseline_key();
        assert(face_sprite_redux_actor_render(
            actor, &key, 6571U,
            baseline, FACE_SPRITE_REDUX_PIXEL_COUNT));
        for (size_t byte = 0U;
             byte < sizeof(face_render_key_t);
             ++byte) {
            bool visible = false;
            const uint8_t original =
                ((const uint8_t *)&key)[byte];
            for (size_t candidate = 0U;
                 candidate < sizeof(candidates);
                 ++candidate) {
                if (candidates[candidate] == original) {
                    continue;
                }
                face_render_key_t mutated = key;
                ((uint8_t *)&mutated)[byte] = candidates[candidate];
                assert(face_sprite_redux_actor_render(
                    actor, &mutated, 6571U,
                    changed, FACE_SPRITE_REDUX_PIXEL_COUNT));
                if (memcmp(baseline, changed, sizeof(baseline)) != 0) {
                    visible = true;
                    break;
                }
            }
            if (!visible) {
                all_visible = false;
                fprintf(
                    stderr,
                    "IR byte not visually consumed: actor=%zu byte=%zu\n",
                    raw, byte);
            }
        }
    }
    assert(all_visible);
}

static void test_fixed_anchors_and_speech_acting(void)
{
    static const uint8_t speech_visemes[16] = {
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
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        const face_sprite_redux_actor_t actor =
            (face_sprite_redux_actor_t)raw;
        face_render_key_t neutral_key = preview_key(FACE_EXPRESSION_NEUTRAL);
        face_sprite_redux_pose_t anchor;
        assert(face_sprite_redux_actor_resolve(
            actor, &neutral_key, 0U, &anchor));
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = preview_key(expression);
            key.controls.look_x = expression % 2U == 0U ? 80 : -80;
            key.controls.look_y = expression % 3U == 0U ? 70 : -70;
            face_sprite_redux_pose_t pose;
            assert(face_sprite_redux_actor_resolve(
                actor, &key, expression * 533U, &pose));
            assert(pose.eye_x[0] == anchor.eye_x[0]);
            assert(pose.eye_x[1] == anchor.eye_x[1]);
            assert(pose.eye_y[0] == anchor.eye_y[0]);
            assert(pose.eye_y[1] == anchor.eye_y[1]);
            assert(pose.mouth_x == anchor.mouth_x);
            assert(pose.mouth_y == anchor.mouth_y);
        }

        face_render_key_t idle = preview_key(FACE_EXPRESSION_WARM);
        face_render_key_t anticipation_low = baseline_key();
        face_render_key_t anticipation_high = baseline_key();
        face_render_key_t active = baseline_key();
        face_render_key_t ending_low = baseline_key();
        face_render_key_t ending_high = baseline_key();
        anticipation_low.speech_phase = FACE_SPEECH_STARTING;
        anticipation_low.viseme_weight = 32U;
        anticipation_low.controls.mouth_open = 16U;
        anticipation_low.audio_level = 18U;
        anticipation_high.speech_phase = FACE_SPEECH_STARTING;
        anticipation_high.viseme_weight = 132U;
        anticipation_high.controls.mouth_open = 48U;
        anticipation_high.audio_level = 64U;
        active.speech_phase = FACE_SPEECH_ACTIVE;
        active.viseme = FACE_VISEME_AA;
        active.viseme_secondary = FACE_VISEME_AA;
        ending_low.speech_phase = FACE_SPEECH_ENDING;
        ending_low.viseme = FACE_VISEME_SIL;
        ending_low.viseme_secondary = FACE_VISEME_SIL;
        ending_low.viseme_weight = 180U;
        ending_low.controls.mouth_open = 72U;
        ending_low.audio_level = 80U;
        ending_high = ending_low;
        ending_high.viseme_weight = 24U;
        ending_high.controls.mouth_open = 10U;
        ending_high.audio_level = 10U;
        face_sprite_redux_pose_t poses[7];
        assert(face_sprite_redux_actor_resolve(
            actor, &idle, 0U, &poses[0]));
        assert(face_sprite_redux_actor_resolve(
            actor, &anticipation_low, 533U, &poses[1]));
        assert(face_sprite_redux_actor_resolve(
            actor, &anticipation_high, 1066U, &poses[2]));
        assert(face_sprite_redux_actor_resolve(
            actor, &active, 1599U, &poses[3]));
        assert(face_sprite_redux_actor_resolve(
            actor, &ending_low, 2132U, &poses[4]));
        assert(face_sprite_redux_actor_resolve(
            actor, &ending_high, 2665U, &poses[5]));
        assert(face_sprite_redux_actor_resolve(
            actor, &idle, 3198U, &poses[6]));
        assert(poses[1].anticipation_q8 > 0U);
        assert(poses[2].anticipation_q8 > poses[1].anticipation_q8);
        assert(poses[4].settle_q8 > 0U);
        assert(poses[5].settle_q8 > poses[4].settle_q8);
        assert(poses[1].eye_open[0] != poses[0].eye_open[0] ||
            poses[1].eye_open[1] != poses[0].eye_open[1]);
        assert(poses[2].eye_open[0] != poses[1].eye_open[0] ||
            poses[2].eye_open[1] != poses[1].eye_open[1] ||
            poses[2].mouth_w != poses[1].mouth_w);
        assert(poses[3].mouth_h != poses[2].mouth_h ||
            poses[3].mouth_w != poses[2].mouth_w ||
            poses[3].mouth_cel != poses[2].mouth_cel);
        assert(poses[4].mouth_h != poses[3].mouth_h ||
            poses[4].eye_open[0] != poses[3].eye_open[0]);
        assert(poses[5].mouth_h != poses[4].mouth_h ||
            poses[5].mouth_w != poses[4].mouth_w);
        assert(poses[6].mouth_h != poses[5].mouth_h ||
            poses[6].mouth_w != poses[5].mouth_w ||
            poses[6].eye_open[0] != poses[5].eye_open[0]);

        uint16_t previous[FACE_SPRITE_REDUX_PIXEL_COUNT];
        uint16_t current[FACE_SPRITE_REDUX_PIXEL_COUNT];
        uint16_t audit[AUDIT_WIDTH * AUDIT_HEIGHT];
        uint32_t hashes[16];
        uint32_t audit_hashes[16];
        uint16_t cel_mask = 0U;
        for (size_t frame = 0U; frame < 16U; ++frame) {
            face_render_key_t key = preview_key(FACE_EXPRESSION_WARM);
            key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
            key.controls.expression = FACE_ACTIVITY_SPEAKING;
            key.viseme = speech_visemes[frame];
            key.viseme_secondary = speech_visemes[
                frame + 1U < 16U ? frame + 1U : frame];
            key.viseme_blend =
                frame < 2U || frame >= 14U
                ? 0U
                : (uint8_t)(32U + frame * 37U % 80U);
            key.viseme_weight =
                frame == 0U ? 32U :
                frame == 1U ? 132U :
                frame == 14U ? 180U :
                frame == 15U ? 24U :
                238U;
            key.controls.mouth_open =
                frame == 0U ? 16U :
                frame == 1U ? 48U :
                frame == 14U ? 72U :
                frame == 15U ? 10U :
                (uint8_t)(62U + frame * 47U % 152U);
            key.controls.mouth_width =
                (uint8_t)(112U + frame * 23U % 122U);
            key.controls.mouth_round =
                frame < 2U || frame >= 14U
                ? 22U
                : (uint8_t)(frame * 61U);
            key.controls.mouth_press =
                speech_visemes[frame] == FACE_VISEME_PP ? 244U : 18U;
            key.audio_level =
                frame == 0U ? 18U :
                frame == 1U ? 64U :
                frame == 14U ? 80U :
                frame == 15U ? 10U :
                (uint8_t)(72U + frame * 29U % 112U);
            key.controls.look_x =
                (int8_t)((int)(frame % 9U) - 4);
            key.controls.look_y =
                (int8_t)((int)(frame % 5U) - 2);
            key.speech_phase =
                frame < 2U ? FACE_SPEECH_STARTING :
                frame >= 14U ? FACE_SPEECH_ENDING :
                FACE_SPEECH_ACTIVE;
            key.stage_expression =
                frame == 13U ? FACE_EXPRESSION_EXCITED :
                frame >= 11U && frame <= 12U ? FACE_EXPRESSION_JOY :
                FACE_EXPRESSION_WARM;
            face_sprite_redux_pose_t pose;
            assert(face_sprite_redux_actor_resolve(
                actor, &key, (uint32_t)frame * 533U, &pose));
            cel_mask |= (uint16_t)(1U << pose.mouth_cel);
            assert(face_sprite_redux_actor_render(
                actor, &key, (uint32_t)frame * 533U,
                current, FACE_SPRITE_REDUX_PIXEL_COUNT));
            hashes[frame] = hash_pixels(current);
            downsample_exact_40x30(current, audit);
            assert_audit_safe_border(audit);
            audit_hashes[frame] = hash_audit_pixels(audit);
            if (frame > 0U) {
                assert(differing_pixels(previous, current) < 10000U);
            }
            memcpy(previous, current, sizeof(previous));
        }
        size_t unique = 0U;
        for (size_t frame = 0U; frame < 16U; ++frame) {
            bool first = true;
            for (size_t before = 0U; before < frame; ++before) {
                if (hashes[before] == hashes[frame]) {
                    first = false;
                    break;
                }
            }
            unique += first ? 1U : 0U;
        }
        assert(unique >= 12U);
        size_t audit_unique = 0U;
        for (size_t frame = 0U; frame < 16U; ++frame) {
            bool first = true;
            for (size_t before = 0U; before < frame; ++before) {
                if (audit_hashes[before] == audit_hashes[frame]) {
                    first = false;
                    break;
                }
            }
            audit_unique += first ? 1U : 0U;
        }
        assert(audit_unique >= 12U);
        unsigned cel_count = 0U;
        for (unsigned bit = 0U; bit < FACE_VISEME_COUNT; ++bit) {
            cel_count += (cel_mask & (1U << bit)) != 0U ? 1U : 0U;
        }
        if (cel_count < 10U) {
            fprintf(
                stderr,
                "weak speech cel variety: actor=%zu mask=%04x count=%u\n",
                raw, (unsigned)cel_mask, cel_count);
        }
        assert(cel_count >= 10U);
    }
}

static void test_adversarial_clipping(void)
{
    for (size_t case_index = 0U;
         case_index < FUZZ_CASES;
         ++case_index) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0U; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)next_random();
        }
        const uint32_t sample_clock = next_random();
        for (size_t raw = 0U;
             raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
             ++raw) {
            const face_sprite_redux_actor_t actor =
                (face_sprite_redux_actor_t)raw;
            guarded_frame_t frame;
            init_guard(&frame);
            assert(face_sprite_redux_actor_render(
                actor, &key, sample_clock,
                frame.pixels, FACE_SPRITE_REDUX_PIXEL_COUNT));
            assert_guard(&frame);
            assert_safe_border(frame.pixels);
            face_sprite_redux_pose_t pose;
            assert(face_sprite_redux_actor_resolve(
                actor, &key, sample_clock, &pose));
            assert_pose_bounds(&pose);
        }
    }
}

static void test_legacy_render(void)
{
    const face_render_key_t key = baseline_key();
    uint16_t direct[FACE_SPRITE_REDUX_PIXEL_COUNT];
    uint16_t legacy[FACE_SPRITE_REDUX_PIXEL_COUNT];
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        const face_sprite_redux_actor_t actor =
            (face_sprite_redux_actor_t)raw;
        face_sprite_redux_actor_info_t info;
        assert(face_sprite_redux_actor_info(actor, &info));
        assert(face_sprite_redux_actor_render(
            actor, &key, 8191U,
            direct, FACE_SPRITE_REDUX_PIXEL_COUNT));
        assert(face_sprite_redux_actor_render_legacy(
            info.legacy_profile_id, &key, 8191U,
            legacy, FACE_SPRITE_REDUX_PIXEL_COUNT));
        assert(memcmp(direct, legacy, sizeof(direct)) == 0);
    }
    assert(!face_sprite_redux_actor_render_legacy(
        57U, &key, 0U, legacy, FACE_SPRITE_REDUX_PIXEL_COUNT));
}

int main(void)
{
    test_metadata_mapping_and_contract();
    test_rejection_determinism_and_source();
    test_all_expressions_and_visemes();
    test_full_ir_visual_consumption();
    test_fixed_anchors_and_speech_acting();
    test_adversarial_clipping();
    test_legacy_render();
    printf(
        "face_sprite_redux_actors_test: PASS "
        "(3 original sprite rigs, 11 emotions, >=10 visemes, "
        "all 40 IR bytes, anticipation/settle, %d fuzz frames)\n",
        FUZZ_CASES * FACE_SPRITE_REDUX_ACTOR_COUNT);
    return 0;
}
