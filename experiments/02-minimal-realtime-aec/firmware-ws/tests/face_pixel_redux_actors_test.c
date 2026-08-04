#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pixel_redux_actors.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 512,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_PIXEL_REDUX_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t random_state = 0x243f6a88U;

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

static void init_guard(guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0x75a10000U + (uint32_t)index;
        frame->after[index] = 0xa1570000U + (uint32_t)index;
    }
}

static void assert_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x75a10000U + index);
        assert(frame->after[index] == 0xa1570000U + index);
    }
}

static void assert_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_PIXEL_REDUX_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_PIXEL_REDUX_WIDTH; ++x) {
            if (x < 4U || x >= FACE_PIXEL_REDUX_WIDTH - 4U ||
                y < 4U || y >= FACE_PIXEL_REDUX_HEIGHT - 4U) {
                assert(
                    pixels[y * FACE_PIXEL_REDUX_WIDTH + x] ==
                    background);
            }
        }
    }
}

static uint32_t hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_PIXEL_REDUX_PIXEL_COUNT;
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
    size_t changed = 0U;
    for (size_t index = 0U;
         index < FACE_PIXEL_REDUX_PIXEL_COUNT;
         ++index) {
        changed += first[index] != second[index];
    }
    return changed;
}

static void mutate_ir_byte(face_render_key_t *key, size_t offset)
{
    switch (offset) {
    case 0: key->controls.mouth_open = 9U; break;
    case 1: key->controls.mouth_width = 35U; break;
    case 2: key->controls.mouth_round = 238U; break;
    case 3: key->controls.mouth_press = 238U; break;
    case 4: key->controls.mouth_teeth = 248U; break;
    case 5: key->controls.eye_left_open = 52U; break;
    case 6: key->controls.eye_right_open = 58U; break;
    case 7: key->controls.look_x = 106; break;
    case 8: key->controls.look_y = -108; break;
    case 9: key->controls.brow = -109; break;
    case 10: key->controls.expression = FACE_ACTIVITY_THINKING; break;
    case 11: key->controls.flags |= FACE_KEYFRAME_FLAG_BLINKING; break;
    case 12: key->viseme = FACE_VISEME_U; break;
    case 13: key->phoneme = 6U; break;
    case 14: key->viseme_weight = 28U; break;
    case 15: key->audio_level = 251U; break;
    case 16: key->viseme_set = FACE_VISEME_SET_VRM5; break;
    case 17: key->viseme_secondary = FACE_VISEME_U; break;
    case 18: key->viseme_blend = 232U; break;
    case 19: key->speech_phase = FACE_SPEECH_ENDING; break;
    case 20: key->mouth_corner_left = -110; break;
    case 21: key->mouth_corner_right = 108; break;
    case 22: key->tongue = 252U; break;
    case 23: key->cheek = 250U; break;
    case 24: key->eye_left_squint = 222U; break;
    case 25: key->eye_right_squint = 226U; break;
    case 26: key->brow_inner = 112; break;
    case 27: key->brow_outer_left = -111; break;
    case 28: key->brow_outer_right = 109; break;
    case 29: key->head_roll = 113; break;
    case 30: key->affect_valence = -116; break;
    case 31: key->affect_arousal = 252U; break;
    case 32: key->head_yaw = 110; break;
    case 33: key->head_pitch = -112; break;
    case 34: key->body_lean_x = 114; break;
    case 35: key->body_lean_y = -115; break;
    case 36: key->expression_weight = 38U; break;
    case 37: key->attention = 18U; break;
    case 38: key->schema_version = 1U; break;
    case 39: key->stage_expression = FACE_EXPRESSION_DETERMINED; break;
    default: assert(false);
    }
}

static void assert_pose_bounds(
    const face_pixel_redux_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        assert(pose->eye_x[eye] >= 24 && pose->eye_x[eye] <= 55);
        assert(pose->eye_y[eye] >= 20 && pose->eye_y[eye] <= 31);
        assert(pose->eye_w[eye] >= 7 && pose->eye_w[eye] <= 20);
        assert(pose->eye_h[eye] >= 8 && pose->eye_h[eye] <= 10);
        assert(pose->eye_open[eye] >= 1);
        assert(pose->eye_open[eye] <= pose->eye_h[eye]);
        assert(pose->pupil_x[eye] >= 20 && pose->pupil_x[eye] <= 59);
        assert(pose->pupil_y[eye] >= 18 && pose->pupil_y[eye] <= 35);
        assert(pose->pupil_radius[eye] >= 1);
        assert(pose->pupil_radius[eye] <= 3);
        assert(pose->brow_outer_y[eye] >= 8);
        assert(pose->brow_outer_y[eye] <= 27);
        assert(pose->brow_inner_y[eye] >= 8);
        assert(pose->brow_inner_y[eye] <= 27);
    }
    assert(pose->mouth_x == 40);
    assert(pose->mouth_y >= 40 && pose->mouth_y <= 43);
    assert(pose->mouth_w >= 6 && pose->mouth_w <= 31);
    assert(pose->mouth_h >= 1 && pose->mouth_h <= 14);
    assert(pose->mouth_corner_y[0] >= -7);
    assert(pose->mouth_corner_y[0] <= 7);
    assert(pose->mouth_corner_y[1] >= -7);
    assert(pose->mouth_corner_y[1] <= 7);
    assert(pose->body_lean_x >= -4 && pose->body_lean_x <= 4);
    assert(pose->body_lean_y >= -3 && pose->body_lean_y <= 3);
}

static void test_metadata_mapping_and_contract(void)
{
    static const uint8_t LEGACY_IDS[FACE_PIXEL_REDUX_ACTOR_COUNT] = {
        0U, 1U, 2U, 3U, 5U,
    };
    assert(sizeof(face_render_key_t) == FACE_RENDER_KEY_BYTES);
    assert(FACE_PIXEL_REDUX_FRAME_BYTES == 38400);
    assert(face_pixel_redux_actor_count() ==
        FACE_PIXEL_REDUX_ACTOR_COUNT);
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        const face_pixel_redux_actor_t actor =
            (face_pixel_redux_actor_t)raw;
        face_pixel_redux_actor_info_t info;
        assert(face_pixel_redux_actor_info(actor, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.legacy_profile_id == LEGACY_IDS[raw]);
        assert(info.logical_width == 80U);
        assert(info.logical_height == 60U);
        assert(info.palette_size >= 4U && info.palette_size <= 48U);
        assert(strcmp(info.slug, face_pixel_redux_actor_slug(actor)) == 0);
        assert(strcmp(info.name, face_pixel_redux_actor_name(actor)) == 0);
        for (size_t earlier = 0U; earlier < raw; ++earlier) {
            assert(strcmp(
                info.slug,
                face_pixel_redux_actor_slug(
                    (face_pixel_redux_actor_t)earlier)) != 0);
        }
        face_pixel_redux_actor_t mapped =
            FACE_PIXEL_REDUX_ACTOR_COUNT;
        assert(face_pixel_redux_actor_from_legacy_id(
            LEGACY_IDS[raw], &mapped));
        assert(mapped == actor);
    }
    face_pixel_redux_actor_t actor = FACE_PIXEL_REDUX_EGA_QUEST;
    assert(!face_pixel_redux_actor_from_legacy_id(4U, &actor));
    assert(!face_pixel_redux_actor_from_legacy_id(62U, &actor));
    assert(!face_pixel_redux_actor_from_legacy_id(0U, NULL));
    assert(face_pixel_redux_actor_slug(
        FACE_PIXEL_REDUX_ACTOR_COUNT) == NULL);
    assert(face_pixel_redux_actor_name(
        (face_pixel_redux_actor_t)-1) == NULL);
    assert(!face_pixel_redux_actor_info(
        FACE_PIXEL_REDUX_ACTOR_COUNT, NULL));
}

static void test_rejection_determinism_and_source(void)
{
    const face_render_key_t key = baseline_key();
    guarded_frame_t first;
    guarded_frame_t second;
    init_guard(&first);
    init_guard(&second);
    assert(!face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_ACTOR_COUNT, &key, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_EGA_QUEST, NULL, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_EGA_QUEST, &key, 0U,
        NULL, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_EGA_QUEST, &key, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT - 1U));
    assert(face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_EGA_QUEST, &key, 4933U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(face_pixel_redux_actor_render(
        FACE_PIXEL_REDUX_EGA_QUEST, &key, 4933U,
        second.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(memcmp(first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    assert_guard(&first);
    assert_guard(&second);

    face_pixel_redux_pose_t pose;
    assert(face_pixel_redux_actor_resolve(
        FACE_PIXEL_REDUX_TALKIE_CLOSEUP, &key, 4933U, &pose));
    assert(memcmp(&pose.source, &key, FACE_RENDER_KEY_BYTES) == 0);
    assert(!face_pixel_redux_actor_resolve(
        FACE_PIXEL_REDUX_ACTOR_COUNT, &key, 0U, &pose));
    assert(!face_pixel_redux_actor_resolve(
        FACE_PIXEL_REDUX_EGA_QUEST, NULL, 0U, &pose));
    assert(!face_pixel_redux_actor_resolve(
        FACE_PIXEL_REDUX_EGA_QUEST, &key, 0U, NULL));
}

static void test_all_expressions_and_visemes(void)
{
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        const face_pixel_redux_actor_t actor =
            (face_pixel_redux_actor_t)raw;
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = baseline_key();
            key.controls.flags = 0U;
            key.controls.expression = FACE_ACTIVITY_LISTENING;
            key.speech_phase = FACE_SPEECH_IDLE;
            key.controls.mouth_open = 8U;
            key.viseme = FACE_VISEME_SIL;
            key.viseme_secondary = FACE_VISEME_SIL;
            key.viseme_weight = 0U;
            key.stage_expression = expression;
            uint16_t frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
            assert(face_pixel_redux_actor_render(
                actor, &key, 4096U, frame,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            expression_hashes[expression] = hash_pixels(frame);
            face_pixel_redux_pose_t pose;
            assert(face_pixel_redux_actor_resolve(
                actor, &key, 4096U, &pose));
            assert_pose_bounds(&pose);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                assert(expression_hashes[expression] !=
                    expression_hashes[FACE_EXPRESSION_NEUTRAL]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        size_t unique = 0U;
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            face_render_key_t key = baseline_key();
            key.stage_expression = FACE_EXPRESSION_NEUTRAL;
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            uint16_t frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
            assert(face_pixel_redux_actor_render(
                actor, &key, 7211U, frame,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            viseme_hashes[viseme] = hash_pixels(frame);
            bool seen = false;
            for (uint8_t earlier = 0U; earlier < viseme; ++earlier) {
                seen |= viseme_hashes[viseme] ==
                    viseme_hashes[earlier];
            }
            unique += !seen;
        }
        if (unique < 10U) {
            fprintf(
                stderr, "%s: only %zu distinct viseme rasters\n",
                face_pixel_redux_actor_slug(actor), unique);
        }
        assert(unique >= 10U);
    }
}

static void test_full_ir_visual_consumption(void)
{
    const face_render_key_t base = baseline_key();
    bool all_visible = true;
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        const face_pixel_redux_actor_t actor =
            (face_pixel_redux_actor_t)raw;
        uint16_t baseline[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t changed_frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
        assert(face_pixel_redux_actor_render(
            actor, &base, 1733U, baseline,
            FACE_PIXEL_REDUX_PIXEL_COUNT));
        const uint32_t base_hash = hash_pixels(baseline);
        for (size_t offset = 0U; offset < FACE_RENDER_KEY_BYTES; ++offset) {
            face_render_key_t changed = base;
            mutate_ir_byte(&changed, offset);
            assert(face_pixel_redux_actor_render(
                actor, &changed, 1733U, changed_frame,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            if (hash_pixels(changed_frame) == base_hash) {
                fprintf(
                    stderr, "%s ignored IR byte %zu\n",
                    face_pixel_redux_actor_slug(actor), offset);
                all_visible = false;
            }
        }
    }
    assert(all_visible);
}

static void test_fixed_anchors_and_speech_acting(void)
{
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        const face_pixel_redux_actor_t actor =
            (face_pixel_redux_actor_t)raw;
        face_render_key_t key = baseline_key();
        face_pixel_redux_pose_t neutral;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        assert(face_pixel_redux_actor_resolve(
            actor, &key, 0U, &neutral));
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_pixel_redux_pose_t pose;
            key.stage_expression = expression;
            key.head_yaw = (int8_t)(expression * 8 - 40);
            key.head_pitch = (int8_t)(35 - expression * 6);
            assert(face_pixel_redux_actor_resolve(
                actor, &key, expression * 533U, &pose));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(pose.eye_x[eye] == neutral.eye_x[eye]);
                assert(pose.eye_y[eye] == neutral.eye_y[eye]);
                assert(pose.eye_h[eye] == neutral.eye_h[eye]);
            }
            assert(pose.mouth_x == neutral.mouth_x);
            assert(pose.mouth_y == neutral.mouth_y);
        }

        /*
         * Speech has an explicit one-frame anticipation and a two-step
         * ending/rest settle. Eye aperture and gaze remain live throughout.
         */
        face_render_key_t idle = baseline_key();
        idle.controls.flags = 0U;
        idle.controls.expression = FACE_ACTIVITY_LISTENING;
        idle.speech_phase = FACE_SPEECH_IDLE;
        idle.viseme = FACE_VISEME_SIL;
        idle.viseme_secondary = FACE_VISEME_SIL;
        idle.controls.mouth_open = 4U;
        face_render_key_t anticipation = idle;
        anticipation.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        anticipation.controls.expression = FACE_ACTIVITY_SPEAKING;
        anticipation.speech_phase = FACE_SPEECH_STARTING;
        face_render_key_t active = anticipation;
        active.speech_phase = FACE_SPEECH_ACTIVE;
        active.viseme = FACE_VISEME_AA;
        active.viseme_secondary = FACE_VISEME_E;
        face_render_key_t ending = active;
        ending.speech_phase = FACE_SPEECH_ENDING;
        ending.viseme = FACE_VISEME_SIL;
        ending.viseme_secondary = FACE_VISEME_SIL;

        face_pixel_redux_pose_t poses[5];
        assert(face_pixel_redux_actor_resolve(
            actor, &idle, 0U, &poses[0]));
        assert(face_pixel_redux_actor_resolve(
            actor, &anticipation, 533U, &poses[1]));
        assert(face_pixel_redux_actor_resolve(
            actor, &active, 1066U, &poses[2]));
        assert(face_pixel_redux_actor_resolve(
            actor, &ending, 1599U, &poses[3]));
        assert(face_pixel_redux_actor_resolve(
            actor, &idle, 2132U, &poses[4]));
        assert(poses[1].eye_open[0] != poses[0].eye_open[0] ||
            poses[1].eye_open[1] != poses[0].eye_open[1]);
        assert(poses[2].mouth_h != poses[1].mouth_h ||
            poses[2].mouth_w != poses[1].mouth_w);
        assert(poses[3].mouth_h != poses[2].mouth_h ||
            poses[3].eye_open[0] != poses[2].eye_open[0]);
        assert(poses[4].mouth_h != poses[3].mouth_h ||
            poses[4].mouth_w != poses[3].mouth_w);

        uint16_t previous[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t current[FACE_PIXEL_REDUX_PIXEL_COUNT];
        key = baseline_key();
        assert(face_pixel_redux_actor_render(
            actor, &key, 0U, previous,
            FACE_PIXEL_REDUX_PIXEL_COUNT));
        for (uint32_t frame = 1U; frame < 160U; ++frame) {
            key.controls.look_x =
                (int8_t)((int)(frame % 25U) - 12);
            key.controls.look_y =
                (int8_t)((int)(frame % 17U) - 8);
            assert(face_pixel_redux_actor_render(
                actor, &key, frame * 533U, current,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            const size_t changed = differing_pixels(previous, current);
            assert(changed < 9000U);
            memcpy(previous, current, sizeof(previous));
        }
    }
}

static void test_adversarial_clipping(void)
{
    for (size_t case_index = 0U; case_index < FUZZ_CASES; ++case_index) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0U; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)next_random();
        }
        const uint32_t clock = next_random();
        for (size_t raw = 0U;
             raw < FACE_PIXEL_REDUX_ACTOR_COUNT;
             ++raw) {
            const face_pixel_redux_actor_t actor =
                (face_pixel_redux_actor_t)raw;
            guarded_frame_t frame;
            init_guard(&frame);
            assert(face_pixel_redux_actor_render(
                actor, &key, clock, frame.pixels,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            assert_guard(&frame);
            assert_safe_border(frame.pixels);
            face_pixel_redux_pose_t pose;
            assert(face_pixel_redux_actor_resolve(
                actor, &key, clock, &pose));
            assert_pose_bounds(&pose);
        }
    }
}

static void test_legacy_render(void)
{
    const face_render_key_t key = baseline_key();
    uint16_t direct[FACE_PIXEL_REDUX_PIXEL_COUNT];
    uint16_t legacy[FACE_PIXEL_REDUX_PIXEL_COUNT];
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        const face_pixel_redux_actor_t actor =
            (face_pixel_redux_actor_t)raw;
        face_pixel_redux_actor_info_t info;
        assert(face_pixel_redux_actor_info(actor, &info));
        assert(face_pixel_redux_actor_render(
            actor, &key, 8191U, direct,
            FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_actor_render_legacy(
            info.legacy_profile_id, &key, 8191U, legacy,
            FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(memcmp(direct, legacy, sizeof(direct)) == 0);
    }
    assert(!face_pixel_redux_actor_render_legacy(
        63U, &key, 0U, legacy, FACE_PIXEL_REDUX_PIXEL_COUNT));
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
        "face_pixel_redux_actors_test: PASS "
        "(5 original pixel rigs, 11 emotions, >=10 visemes, "
        "all 40 IR bytes, anticipation/settle, %d fuzz frames)\n",
        FUZZ_CASES * FACE_PIXEL_REDUX_ACTOR_COUNT);
    return 0;
}
