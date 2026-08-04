#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_eye_actors.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 192,
    PREVIEW_CLOCK = 1921088U,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_EYE_ACTOR_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < FACE_EYE_ACTOR_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t differing_pixels(
    const uint16_t *first, const uint16_t *second)
{
    size_t count = 0U;
    for (size_t index = 0U; index < FACE_EYE_ACTOR_PIXEL_COUNT; ++index) {
        count += first[index] != second[index];
    }
    return count;
}

static bool is_anki_test_style(face_eye_actor_style_t style)
{
    return style == FACE_EYE_ACTOR_VECTOR_FELT ||
        style == FACE_EYE_ACTOR_COZMO_TILES ||
        style == FACE_EYE_ACTOR_VECTOR_STAGE ||
        style == FACE_EYE_ACTOR_COZMO_CONSOLE;
}

static face_render_key_t baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 132U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 44U;
    key.controls.mouth_press = 13U;
    key.controls.mouth_teeth = 116U;
    key.controls.eye_left_open = 234U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = 4;
    key.controls.look_y = -3;
    key.controls.brow = 7;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_O;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 174U;
    key.audio_level = 132U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 4;
    key.mouth_corner_right = 2;
    key.tongue = 78U;
    key.cheek = 22U;
    key.eye_left_squint = 8U;
    key.eye_right_squint = 5U;
    key.brow_inner = 4;
    key.brow_outer_left = -3;
    key.brow_outer_right = 5;
    key.head_roll = 2;
    key.affect_valence = 8;
    key.affect_arousal = 137U;
    key.head_yaw = 3;
    key.head_pitch = -2;
    key.body_lean_x = 2;
    key.body_lean_y = -2;
    key.expression_weight = 255U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

static void init_guard(guarded_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0xface0000U + (uint32_t)index;
        frame->after[index] = 0xcafe0000U + (uint32_t)index;
    }
}

static void check_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0xface0000U + index);
        assert(frame->after[index] == 0xcafe0000U + index);
    }
}

static void check_safe_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_EYE_ACTOR_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_EYE_ACTOR_WIDTH; ++x) {
            if (x < 4U || x >= FACE_EYE_ACTOR_WIDTH - 4U ||
                y < 4U || y >= FACE_EYE_ACTOR_HEIGHT - 4U) {
                assert(pixels[y * FACE_EYE_ACTOR_WIDTH + x] == background);
            }
        }
    }
}

static void test_metadata_and_legacy_mapping(void)
{
    assert(face_eye_actor_count() == FACE_EYE_ACTOR_COUNT);
    for (size_t raw = 0U; raw < FACE_EYE_ACTOR_COUNT; ++raw) {
        const face_eye_actor_style_t style = (face_eye_actor_style_t)raw;
        face_eye_actor_info_t info;
        assert(face_eye_actor_info(style, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        const uint8_t expected_id = (uint8_t)(
            raw <= FACE_EYE_ACTOR_CAT_LANTERN
                ? FACE_EYE_ACTOR_FIRST_LEGACY_ID + raw
                : FACE_EYE_ACTOR_FIRST_RIG_LEGACY_ID +
                    raw - FACE_EYE_ACTOR_VECTOR_STAGE);
        assert(info.legacy_profile_id == expected_id);
        assert(info.deliberate_monocular ==
            (style == FACE_EYE_ACTOR_JIBO_MONOCLE));
        assert(strcmp(info.slug, face_eye_actor_slug(style)) == 0);
        assert(strcmp(info.name, face_eye_actor_name(style)) == 0);
        for (size_t earlier = 0U; earlier < raw; ++earlier) {
            assert(strcmp(
                info.slug,
                face_eye_actor_slug((face_eye_actor_style_t)earlier)) != 0);
        }
        face_eye_actor_style_t mapped = FACE_EYE_ACTOR_COUNT;
        assert(face_eye_actor_from_legacy_id(
            info.legacy_profile_id, &mapped));
        assert(mapped == style);
    }
    face_eye_actor_style_t mapped = FACE_EYE_ACTOR_VECTOR_FELT;
    assert(!face_eye_actor_from_legacy_id(6U, &mapped));
    assert(!face_eye_actor_from_legacy_id(23U, &mapped));
    assert(!face_eye_actor_from_legacy_id(39U, &mapped));
    assert(!face_eye_actor_from_legacy_id(47U, &mapped));
    assert(!face_eye_actor_from_legacy_id(7U, NULL));
    assert(face_eye_actor_slug(FACE_EYE_ACTOR_COUNT) == NULL);
    assert(face_eye_actor_name((face_eye_actor_style_t)-1) == NULL);
    assert(!face_eye_actor_info(FACE_EYE_ACTOR_COUNT, NULL));
}

static void test_rejection_and_determinism(void)
{
    face_render_key_t key = baseline_key();
    guarded_frame_t first;
    guarded_frame_t second;
    init_guard(&first);
    init_guard(&second);
    assert(!face_eye_actor_render(
        FACE_EYE_ACTOR_COUNT, &key, 0U,
        first.pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
    assert(!face_eye_actor_render(
        FACE_EYE_ACTOR_VECTOR_FELT, NULL, 0U,
        first.pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
    assert(!face_eye_actor_render(
        FACE_EYE_ACTOR_VECTOR_FELT, &key, 0U,
        NULL, FACE_EYE_ACTOR_PIXEL_COUNT));
    assert(!face_eye_actor_render(
        FACE_EYE_ACTOR_VECTOR_FELT, &key, 0U,
        first.pixels, FACE_EYE_ACTOR_PIXEL_COUNT - 1U));
    assert(face_eye_actor_render(
        FACE_EYE_ACTOR_VECTOR_FELT, &key, 17231U,
        first.pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
    assert(face_eye_actor_render(
        FACE_EYE_ACTOR_VECTOR_FELT, &key, 17231U,
        second.pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
    assert(memcmp(first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    check_guard(&first);
    check_guard(&second);
}

static void assert_pose_bounds(
    face_eye_actor_style_t style, const face_eye_actor_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (style == FACE_EYE_ACTOR_JIBO_MONOCLE && eye == 1U) {
            assert(pose->eye_w[eye] == 0);
            assert(pose->eye_h[eye] == 0);
            assert(pose->eye_aperture[eye] == 0);
            continue;
        }
        assert(pose->eye_x[eye] >= 27 && pose->eye_x[eye] <= 133);
        assert(pose->eye_y[eye] >= 28 && pose->eye_y[eye] <= 66);
        assert(pose->eye_w[eye] >= 22 && pose->eye_w[eye] <= 54);
        assert(pose->eye_h[eye] >= 12 && pose->eye_h[eye] <= 54);
        assert(pose->eye_aperture[eye] >= 4);
        if (!is_anki_test_style(style)) {
            assert(pose->eye_aperture[eye] <= pose->eye_h[eye]);
        }
        assert(pose->pupil_radius[eye] >= 2 &&
            pose->pupil_radius[eye] <= 10);
        if (is_anki_test_style(style)) {
            assert(pose->eye_x[eye] == (eye == 0U ? 49 : 111));
            assert(pose->eye_y[eye] == 59);
            assert(pose->eye_translate_x[eye] >= -9 &&
                pose->eye_translate_x[eye] <= 9);
            assert(pose->eye_translate_y[eye] >= -8 &&
                pose->eye_translate_y[eye] <= 8);
            assert(pose->eye_scale_x_q8[eye] >= 210 &&
                pose->eye_scale_x_q8[eye] <= 306);
            assert(pose->eye_scale_y_q8[eye] >= 190 &&
                pose->eye_scale_y_q8[eye] <= 310);
            assert(pose->eye_angle[eye] >= -8 &&
                pose->eye_angle[eye] <= 8);
            for (size_t corner = 0U; corner < 4U; ++corner) {
                assert(pose->eye_corner_radius[eye][corner] >= 2 &&
                    pose->eye_corner_radius[eye][corner] <= 13);
            }
            assert(pose->upper_lid_cover[eye] >= 0);
            assert(pose->lower_lid_cover[eye] >= 0);
            assert(pose->upper_lid_angle[eye] >= -34 &&
                pose->upper_lid_angle[eye] <= 34);
            assert(pose->lower_lid_angle[eye] >= -18 &&
                pose->lower_lid_angle[eye] <= 18);
            assert(pose->upper_lid_bend[eye] >= 0 &&
                pose->upper_lid_bend[eye] <= 10);
            assert(pose->lower_lid_bend[eye] >= 0 &&
                pose->lower_lid_bend[eye] <= 11);
            const int32_t center_x =
                pose->eye_x[eye] + pose->eye_translate_x[eye];
            const int32_t center_y =
                pose->eye_y[eye] + pose->eye_translate_y[eye];
            const int32_t actual_w =
                pose->eye_w[eye] * pose->eye_scale_x_q8[eye] / 256;
            const int32_t actual_h =
                pose->eye_h[eye] * pose->eye_scale_y_q8[eye] / 256;
            assert(pose->eye_aperture[eye] <= actual_h);
            assert(pose->pupil_x[eye] >= center_x - actual_w / 2);
            assert(pose->pupil_x[eye] <= center_x + actual_w / 2);
            assert(pose->pupil_y[eye] >= center_y - actual_h / 2);
            assert(pose->pupil_y[eye] <= center_y + actual_h / 2);
        } else {
            assert(pose->pupil_x[eye] >=
                pose->eye_x[eye] - pose->eye_w[eye] / 2);
            assert(pose->pupil_x[eye] <=
                pose->eye_x[eye] + pose->eye_w[eye] / 2);
            assert(pose->pupil_y[eye] >=
                pose->eye_y[eye] - pose->eye_aperture[eye] / 2);
            assert(pose->pupil_y[eye] <=
                pose->eye_y[eye] + pose->eye_aperture[eye] / 2);
        }
        assert(pose->brow_y[eye] >= 12 && pose->brow_y[eye] <= 54);
        assert(pose->brow_slope[eye] >= -12 &&
            pose->brow_slope[eye] <= 12);
    }
    assert(pose->mouth_x >= 72 && pose->mouth_x <= 88);
    assert(pose->mouth_y >= 78 && pose->mouth_y <= 101);
    assert(pose->mouth_w >= 14 && pose->mouth_w <= 62);
    assert(pose->mouth_h >= 2 && pose->mouth_h <= 25);
    assert(pose->mouth_x - pose->mouth_w / 2 >= 41);
    assert(pose->mouth_x + pose->mouth_w / 2 <= 119);
    assert(pose->mouth_y + pose->mouth_h / 2 <= 113);
}

static uint32_t random_state = 0x8c41a57dU;

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_clamping_and_guards(void)
{
    size_t frames_checked = 0U;
    for (size_t iteration = 0U; iteration < FUZZ_CASES; ++iteration) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0U; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)next_random();
        }
        if ((iteration & 1U) == 0U) {
            key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
            key.stage_expression =
                (uint8_t)(iteration % FACE_EXPRESSION_COUNT);
        }
        for (size_t raw = 0U; raw < FACE_EYE_ACTOR_COUNT; ++raw) {
            const face_eye_actor_style_t style =
                (face_eye_actor_style_t)raw;
            guarded_frame_t frame;
            face_eye_actor_pose_t pose;
            init_guard(&frame);
            assert(face_eye_actor_resolve(
                style, &key, next_random(), &pose));
            assert(memcmp(&pose.source, &key, sizeof(key)) == 0);
            assert_pose_bounds(style, &pose);
            assert(face_eye_actor_render(
                style, &key, next_random(),
                frame.pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
            check_guard(&frame);
            check_safe_border(frame.pixels);
            ++frames_checked;
        }
    }
    printf("clamp/sanitizer corpus: %zu adversarial frames\n", frames_checked);
}

static void test_expression_separability(void)
{
    size_t minimum_delta = FACE_EYE_ACTOR_PIXEL_COUNT;
    for (size_t raw = 0U; raw < FACE_EYE_ACTOR_COUNT; ++raw) {
        uint16_t frames[FACE_EXPRESSION_COUNT][FACE_EYE_ACTOR_PIXEL_COUNT];
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = baseline_key();
            key.controls.expression = FACE_ACTIVITY_LISTENING;
            key.controls.flags = 0U;
            key.speech_phase = FACE_SPEECH_IDLE;
            key.stage_expression = expression;
            key.expression_weight = 255U;
            assert(face_eye_actor_render(
                (face_eye_actor_style_t)raw,
                &key,
                19231U,
                frames[expression],
                FACE_EYE_ACTOR_PIXEL_COUNT));
            hashes[expression] = hash_pixels(frames[expression]);
        }
        for (size_t first = 0U; first < FACE_EXPRESSION_COUNT; ++first) {
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                assert(hashes[first] != hashes[second]);
            }
        }
        for (size_t expression = 1U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const size_t delta =
                differing_pixels(frames[0], frames[expression]);
            if (delta < minimum_delta) {
                minimum_delta = delta;
            }
            assert(delta >= 24U);
        }
    }
    printf(
        "expression separability: %d x 11 unique, min neutral delta=%zu px\n",
        FACE_EYE_ACTOR_COUNT, minimum_delta);
}

static void test_distinct_style_grammars(void)
{
    face_render_key_t key = baseline_key();
    key.stage_expression = FACE_EXPRESSION_WARM;
    uint16_t frames[FACE_EYE_ACTOR_COUNT][FACE_EYE_ACTOR_PIXEL_COUNT];
    size_t closest = FACE_EYE_ACTOR_PIXEL_COUNT;
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        assert(face_eye_actor_render(
            (face_eye_actor_style_t)style,
            &key,
            19231U,
            frames[style],
            FACE_EYE_ACTOR_PIXEL_COUNT));
    }
    for (size_t first = 0U; first < FACE_EYE_ACTOR_COUNT; ++first) {
        for (size_t second = first + 1U;
             second < FACE_EYE_ACTOR_COUNT;
             ++second) {
            const size_t delta =
                differing_pixels(frames[first], frames[second]);
            if (delta < closest) {
                closest = delta;
            }
            assert(delta > FACE_EYE_ACTOR_PIXEL_COUNT / 3U);
        }
    }
    printf("style separability: closest pair differs by %zu px\n", closest);
}

static void test_bilateral_mass_and_readable_mouths(void)
{
    for (size_t raw = 0U; raw < FACE_EYE_ACTOR_COUNT; ++raw) {
        const face_eye_actor_style_t style =
            (face_eye_actor_style_t)raw;
        face_render_key_t key = baseline_key();
        key.stage_expression = FACE_EXPRESSION_DETERMINED;
        uint16_t pixels[FACE_EYE_ACTOR_PIXEL_COUNT];
        assert(face_eye_actor_render(
            style, &key, 19231U, pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
        const uint16_t background = pixels[0];
        size_t eye_mass[2] = {0U, 0U};
        for (size_t y = 12U; y < 76U; ++y) {
            for (size_t x = 18U; x < 75U; ++x) {
                eye_mass[0] +=
                    pixels[y * FACE_EYE_ACTOR_WIDTH + x] != background;
            }
            for (size_t x = 85U; x < 142U; ++x) {
                eye_mass[1] +=
                    pixels[y * FACE_EYE_ACTOR_WIDTH + x] != background;
            }
        }
        if (style != FACE_EYE_ACTOR_JIBO_MONOCLE) {
            assert(eye_mass[0] >= 80U);
            assert(eye_mass[1] >= 80U);
            const size_t smaller =
                eye_mass[0] < eye_mass[1] ? eye_mass[0] : eye_mass[1];
            const size_t larger =
                eye_mass[0] > eye_mass[1] ? eye_mass[0] : eye_mass[1];
            assert(larger < smaller * 3U);
        }

        uint32_t mouth_hashes[FACE_VISEME_COUNT];
        face_eye_actor_info_t info;
        assert(face_eye_actor_info(style, &info));
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            key.viseme = viseme;
            key.viseme_secondary = FACE_VISEME_NONE;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            assert(face_eye_actor_render(
                /*
                 * This assertion measures the authored viseme vocabulary,
                 * so keep the autonomous blink clock outside its window.
                 */
                style, &key, PREVIEW_CLOCK,
                pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
            uint32_t hash = 2166136261U;
            if (info.mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE) {
                hash = hash_pixels(pixels);
            } else {
                for (size_t y = 74U; y < 115U; ++y) {
                    for (size_t x = 36U; x < 124U; ++x) {
                        hash ^= pixels[y * FACE_EYE_ACTOR_WIDTH + x];
                        hash *= 16777619U;
                    }
                }
            }
            mouth_hashes[viseme] = hash;
        }
        size_t unique = 0U;
        for (size_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            bool seen = false;
            for (size_t earlier = 0U; earlier < viseme; ++earlier) {
                seen |= mouth_hashes[earlier] == mouth_hashes[viseme];
            }
            unique += !seen;
        }
        if (unique < 10U) {
            fprintf(stderr, "%s: only %zu distinct viseme rasters\n",
                face_eye_actor_slug(style), unique);
        }
        assert(unique >= 10U);
    }
    puts("eye/mouth invariants: bilateral mass and >=10 raster visemes/style");
}

static void mutate_ir_byte(face_render_key_t *key, size_t offset)
{
    switch (offset) {
    case 0: key->controls.mouth_open = 12U; break;
    case 1: key->controls.mouth_width = 28U; break;
    case 2: key->controls.mouth_round = 240U; break;
    case 3: key->controls.mouth_press = 230U; break;
    case 4: key->controls.mouth_teeth = 245U; break;
    case 5: key->controls.eye_left_open = 70U; break;
    case 6: key->controls.eye_right_open = 74U; break;
    case 7: key->controls.look_x = 105; break;
    case 8: key->controls.look_y = -104; break;
    case 9: key->controls.brow = -105; break;
    case 10: key->controls.expression = FACE_ACTIVITY_THINKING; break;
    case 11: key->controls.flags |= FACE_KEYFRAME_FLAG_BLINKING; break;
    case 12: key->viseme = FACE_VISEME_U; break;
    case 13: key->phoneme = 6U; break;
    case 14: key->viseme_weight = 36U; break;
    case 15: key->audio_level = 250U; break;
    case 16: key->viseme_set = FACE_VISEME_SET_VRM5; break;
    case 17: key->viseme_secondary = FACE_VISEME_U; break;
    case 18: key->viseme_blend = 230U; break;
    case 19: key->speech_phase = FACE_SPEECH_ENDING; break;
    case 20: key->mouth_corner_left = -104; break;
    case 21: key->mouth_corner_right = 105; break;
    case 22: key->tongue = 250U; break;
    case 23: key->cheek = 245U; break;
    case 24: key->eye_left_squint = 220U; break;
    case 25: key->eye_right_squint = 225U; break;
    case 26: key->brow_inner = 110; break;
    case 27: key->brow_outer_left = -110; break;
    case 28: key->brow_outer_right = 111; break;
    case 29: key->head_roll = 112; break;
    case 30: key->affect_valence = -115; break;
    case 31: key->affect_arousal = 250U; break;
    case 32: key->head_yaw = 108; break;
    case 33: key->head_pitch = -109; break;
    case 34: key->body_lean_x = 111; break;
    case 35: key->body_lean_y = -112; break;
    case 36: key->expression_weight = 42U; break;
    case 37: key->attention = 22U; break;
    case 38: key->schema_version = 1U; break;
    case 39: key->stage_expression = FACE_EXPRESSION_DETERMINED; break;
    default: assert(false);
    }
}

static uint32_t aggregate_hash(const face_render_key_t *key)
{
    uint16_t pixels[FACE_EYE_ACTOR_PIXEL_COUNT];
    uint32_t hash = 2166136261U;
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        assert(face_eye_actor_render(
            (face_eye_actor_style_t)style,
            key,
            19231U,
            pixels,
            FACE_EYE_ACTOR_PIXEL_COUNT));
        hash ^= hash_pixels(pixels);
        hash *= 16777619U;
    }
    return hash;
}

static void test_full_ir_consumption(void)
{
    assert(sizeof(face_render_key_t) == 40U);
    const face_render_key_t base = baseline_key();
    const uint32_t baseline = aggregate_hash(&base);
    for (size_t offset = 0U; offset < sizeof(base); ++offset) {
        face_render_key_t changed = base;
        mutate_ir_byte(&changed, offset);
        const uint32_t mutated = aggregate_hash(&changed);
        if (mutated == baseline) {
            fprintf(stderr, "IR byte %zu had no aggregate visual effect\n", offset);
        }
        assert(mutated != baseline);
    }
    puts("IR coverage: all 40 byte positions have a visual effect");
}

static uint32_t anki_style_hash(
    face_eye_actor_style_t style, const face_render_key_t *key)
{
    uint16_t pixels[FACE_EYE_ACTOR_PIXEL_COUNT];
    assert(face_eye_actor_render(
        style, key, PREVIEW_CLOCK,
        pixels, FACE_EYE_ACTOR_PIXEL_COUNT));
    return hash_pixels(pixels);
}

static void test_anki_ir_and_expression_semantics(void)
{
    const face_render_key_t base = baseline_key();
    static const face_eye_actor_style_t styles[4] = {
        FACE_EYE_ACTOR_VECTOR_FELT,
        FACE_EYE_ACTOR_COZMO_TILES,
        FACE_EYE_ACTOR_VECTOR_STAGE,
        FACE_EYE_ACTOR_COZMO_CONSOLE,
    };
    bool all_bytes_visible = true;
    for (size_t index = 0U; index < 4U; ++index) {
        const uint32_t baseline =
            anki_style_hash(styles[index], &base);
        for (size_t offset = 0U; offset < sizeof(base); ++offset) {
            face_render_key_t changed = base;
            mutate_ir_byte(&changed, offset);
            if (anki_style_hash(styles[index], &changed) == baseline) {
                fprintf(stderr, "%s ignored IR byte %zu\n",
                    face_eye_actor_slug(styles[index]), offset);
                all_bytes_visible = false;
            }
        }
    }
    assert(all_bytes_visible);

    for (size_t index = 0U; index < 4U; ++index) {
        face_render_key_t key = baseline_key();
        key.controls.expression = FACE_ACTIVITY_IDLE;
        key.controls.flags = 0U;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.expression_weight = 255U;
        face_eye_actor_pose_t poses[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            key.stage_expression = expression;
            assert(face_eye_actor_resolve(
                styles[index], &key, PREVIEW_CLOCK, &poses[expression]));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(poses[expression].eye_x[eye] ==
                    poses[FACE_EXPRESSION_NEUTRAL].eye_x[eye]);
                assert(poses[expression].eye_y[eye] ==
                    poses[FACE_EXPRESSION_NEUTRAL].eye_y[eye]);
                assert(poses[expression].eye_w[eye] ==
                    poses[FACE_EXPRESSION_NEUTRAL].eye_w[eye]);
                assert(poses[expression].eye_h[eye] ==
                    poses[FACE_EXPRESSION_NEUTRAL].eye_h[eye]);
                assert(poses[expression].pupil_x[0] <= 77);
                assert(poses[expression].pupil_x[1] >= 83);
            }
        }
        const face_eye_actor_pose_t *neutral =
            &poses[FACE_EXPRESSION_NEUTRAL];
        const face_eye_actor_pose_t *warm =
            &poses[FACE_EXPRESSION_WARM];
        const face_eye_actor_pose_t *joy =
            &poses[FACE_EXPRESSION_JOY];
        const face_eye_actor_pose_t *concern =
            &poses[FACE_EXPRESSION_CONCERN];
        const face_eye_actor_pose_t *surprise =
            &poses[FACE_EXPRESSION_SURPRISE];
        const face_eye_actor_pose_t *determined =
            &poses[FACE_EXPRESSION_DETERMINED];
        const face_eye_actor_pose_t *sleepy =
            &poses[FACE_EXPRESSION_SLEEPY];
        const face_eye_actor_pose_t *embarrassed =
            &poses[FACE_EXPRESSION_EMBARRASSED];
        for (size_t eye = 0U; eye < 2U; ++eye) {
            assert(warm->lower_lid_cover[eye] >
                neutral->lower_lid_cover[eye]);
            assert(warm->lower_lid_bend[eye] >
                neutral->lower_lid_bend[eye]);
            assert(joy->eye_scale_y_q8[eye] <
                neutral->eye_scale_y_q8[eye]);
            assert(joy->lower_lid_bend[eye] >
                warm->lower_lid_bend[eye]);
            assert(surprise->eye_scale_x_q8[eye] >
                neutral->eye_scale_x_q8[eye] + 20);
            assert(surprise->eye_scale_y_q8[eye] >
                neutral->eye_scale_y_q8[eye] + 20);
            assert(surprise->pupil_radius[eye] <
                neutral->pupil_radius[eye]);
            assert(sleepy->upper_lid_cover[eye] >
                neutral->upper_lid_cover[eye]);
            assert(sleepy->lower_lid_cover[eye] >
                neutral->lower_lid_cover[eye]);
            assert(embarrassed->upper_lid_cover[eye] >
                neutral->upper_lid_cover[eye]);
            assert(embarrassed->pupil_y[eye] >
                neutral->pupil_y[eye]);
        }
        assert(concern->upper_lid_angle[0] > 0);
        assert(concern->upper_lid_angle[1] < 0);
        assert(determined->upper_lid_angle[0] < 0);
        assert(determined->upper_lid_angle[1] > 0);
        assert(embarrassed->eye_translate_x[0] >
            neutral->eye_translate_x[0]);
        assert(embarrassed->eye_translate_x[1] <
            neutral->eye_translate_x[1]);
    }
    puts("Anki rigs: every rig uses all IR bytes + authored semantics");
}

static uint32_t test_hash32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static void test_blink_boundary_continuity(void)
{
    enum {
        BLINK_SAMPLES = 4800U,
        BLINK_STEP_SAMPLES = 500U,
        BLINK_FRAMES =
            BLINK_SAMPLES / BLINK_STEP_SAMPLES + 2U,
    };
    static const face_eye_actor_style_t styles[4] = {
        FACE_EYE_ACTOR_VECTOR_FELT,
        FACE_EYE_ACTOR_COZMO_TILES,
        FACE_EYE_ACTOR_VECTOR_STAGE,
        FACE_EYE_ACTOR_COZMO_CONSOLE,
    };
    for (size_t index = 0U; index < 4U; ++index) {
        const face_eye_actor_style_t style = styles[index];
        const uint32_t period =
            47000U + test_hash32((uint32_t)style + 91U) % 11000U;
        const uint32_t offset =
            (uint32_t)style * 1877U % period;
        const uint32_t start = period - offset;
        face_render_key_t key = baseline_key();
        key.controls.eye_left_open = 255U;
        key.controls.eye_right_open = 255U;
        key.controls.expression = FACE_ACTIVITY_IDLE;
        key.controls.flags = 0U;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        key.affect_arousal = 128U;

        face_eye_actor_pose_t boundary[6];
        const uint32_t clocks[6] = {
            start - 1U, start, start + 1U,
            start + BLINK_SAMPLES - 1U,
            start + BLINK_SAMPLES,
            start + BLINK_SAMPLES + 1U,
        };
        for (size_t sample = 0U; sample < 6U; ++sample) {
            assert(face_eye_actor_resolve(
                style, &key, clocks[sample], &boundary[sample]));
        }
        for (size_t eye = 0U; eye < 2U; ++eye) {
            assert(boundary[0].eye_aperture[eye] ==
                boundary[1].eye_aperture[eye]);
            assert(boundary[1].eye_aperture[eye] ==
                boundary[2].eye_aperture[eye]);
            assert(boundary[3].eye_aperture[eye] ==
                boundary[4].eye_aperture[eye]);
            assert(boundary[4].eye_aperture[eye] ==
                boundary[5].eye_aperture[eye]);
        }

        face_eye_actor_pose_t previous;
        bool have_previous = false;
        int32_t minimum = INT32_MAX;
        int32_t maximum = INT32_MIN;
        for (size_t frame = 0U; frame < BLINK_FRAMES; ++frame) {
            face_eye_actor_pose_t pose;
            assert(face_eye_actor_resolve(
                style,
                &key,
                start + (uint32_t)frame * BLINK_STEP_SAMPLES,
                &pose));
            if (pose.eye_aperture[0] < minimum) {
                minimum = pose.eye_aperture[0];
            }
            if (pose.eye_aperture[0] > maximum) {
                maximum = pose.eye_aperture[0];
            }
            if (have_previous) {
                const int32_t delta =
                    pose.eye_aperture[0] - previous.eye_aperture[0];
                /*
                 * A natural eight-to-nine-frame blink moves faster than the
                 * old 750 ms one, but it must still take multiple frames.
                 */
                assert(delta >= -11 && delta <= 11);
                assert(pose.eye_x[0] == previous.eye_x[0]);
                assert(pose.eye_y[0] == previous.eye_y[0]);
            }
            previous = pose;
            have_previous = true;
        }
        assert(maximum >= 30);
        /*
         * The 500-sample probe intentionally does not land on the exact
         * 2,400-sample midpoint; it should still reach a convincing squint.
         */
        assert(minimum <= 10);
    }
    puts("blink continuity: 4 Anki rigs close/open without boundary snap");
}

static void test_fixed_sockets_and_eye_only_speech(void)
{
    static const uint8_t sequence[12] = {
        FACE_VISEME_PP, FACE_VISEME_AA, FACE_VISEME_E,
        FACE_VISEME_I, FACE_VISEME_O, FACE_VISEME_U,
        FACE_VISEME_SS, FACE_VISEME_TH, FACE_VISEME_FF,
        FACE_VISEME_KK, FACE_VISEME_CH, FACE_VISEME_SIL,
    };
    size_t eye_only_styles = 0U;
    for (size_t raw = 0U; raw < FACE_EYE_ACTOR_COUNT; ++raw) {
        const face_eye_actor_style_t style =
            (face_eye_actor_style_t)raw;
        face_render_key_t rest_key = baseline_key();
        rest_key.controls.expression = FACE_ACTIVITY_LISTENING;
        rest_key.controls.flags = 0U;
        rest_key.speech_phase = FACE_SPEECH_IDLE;
        rest_key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        face_eye_actor_pose_t rest;
        assert(face_eye_actor_resolve(
            style, &rest_key, PREVIEW_CLOCK, &rest));

        /* Expressions and speech may move lids, never the socket itself. */
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t expression_key = rest_key;
            expression_key.stage_expression = expression;
            face_eye_actor_pose_t expression_pose;
            assert(face_eye_actor_resolve(
                style, &expression_key, PREVIEW_CLOCK, &expression_pose));
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(expression_pose.eye_x[eye] == rest.eye_x[eye]);
                assert(expression_pose.eye_y[eye] == rest.eye_y[eye]);
                assert(expression_pose.eye_w[eye] == rest.eye_w[eye]);
                assert(expression_pose.eye_h[eye] == rest.eye_h[eye]);
            }
        }

        face_eye_actor_info_t info;
        assert(face_eye_actor_info(style, &info));
        if (info.mouth_kind != FACE_EYE_ACTOR_MOUTH_NONE) {
            continue;
        }
        ++eye_only_styles;
        assert(rest.eye_speech_pulse == 0);
        assert(rest.eye_speech_spacing == 0);
        assert(rest.eye_speech_corner == 0);

        face_eye_actor_pose_t previous;
        bool have_previous = false;
        int32_t minimum_aperture = INT32_MAX;
        int32_t maximum_aperture = INT32_MIN;
        for (size_t frame = 0U; frame < 12U; ++frame) {
            face_render_key_t key = baseline_key();
            key.viseme = sequence[frame];
            key.viseme_secondary = sequence[(frame + 1U) % 12U];
            key.viseme_blend = (uint8_t)(frame * 19U);
            key.audio_level = (uint8_t)(72U + (frame % 5U) * 35U);
            key.speech_phase = frame == 0U ? FACE_SPEECH_STARTING
                : frame == 11U ? FACE_SPEECH_ENDING
                : FACE_SPEECH_ACTIVE;
            face_eye_actor_pose_t pose;
            assert(face_eye_actor_resolve(
                style, &key, PREVIEW_CLOCK, &pose));
            assert(pose.eye_speech_pulse >= 1 &&
                pose.eye_speech_pulse <= 5);
            assert(pose.eye_speech_spacing >= 1 &&
                pose.eye_speech_spacing <= 3);
            assert(pose.eye_speech_corner >= 0 &&
                pose.eye_speech_corner <= 3);
            if (frame == 0U) {
                assert(pose.eye_speech_pulse == 4);
                assert(pose.eye_speech_spacing == 2);
            } else if (frame == 11U) {
                assert(pose.eye_speech_pulse == 1);
                assert(pose.eye_speech_spacing == 1);
            }
            for (size_t eye = 0U; eye < 2U; ++eye) {
                assert(pose.eye_x[eye] == rest.eye_x[eye]);
                assert(pose.eye_y[eye] == rest.eye_y[eye]);
                assert(pose.eye_w[eye] == rest.eye_w[eye]);
                assert(pose.eye_h[eye] == rest.eye_h[eye]);
                if (pose.eye_aperture[eye] < minimum_aperture) {
                    minimum_aperture = pose.eye_aperture[eye];
                }
                if (pose.eye_aperture[eye] > maximum_aperture) {
                    maximum_aperture = pose.eye_aperture[eye];
                }
                if (have_previous) {
                    const int32_t aperture_delta =
                        pose.eye_aperture[eye] -
                        previous.eye_aperture[eye];
                    if (aperture_delta < -3 || aperture_delta > 3) {
                        fprintf(stderr,
                            "%s frame=%zu eye=%zu aperture jump=%ld\n",
                            info.slug, frame, eye,
                            (long)aperture_delta);
                    }
                    assert(aperture_delta >= -3 &&
                        aperture_delta <= 3);
                }
            }
            if (have_previous) {
                const int32_t spacing_delta =
                    pose.eye_speech_spacing -
                    previous.eye_speech_spacing;
                const int32_t corner_delta =
                    pose.eye_speech_corner -
                    previous.eye_speech_corner;
                assert(spacing_delta >= -2 && spacing_delta <= 2);
                assert(corner_delta >= -2 && corner_delta <= 2);
            }
            previous = pose;
            have_previous = true;
        }
        if (maximum_aperture - minimum_aperture < 3) {
            fprintf(stderr, "%s: eye-only aperture range %ld..%ld\n",
                info.slug, (long)minimum_aperture,
                (long)maximum_aperture);
        }
        assert(maximum_aperture - minimum_aperture >= 3);
    }
    assert(eye_only_styles == 4U);
    puts("eye-only speech: 4 fixed sockets, bounded pulse + lead/settle");
}

static void test_temporal_speaking(void)
{
    static const uint8_t sequence[12] = {
        FACE_VISEME_PP, FACE_VISEME_AA, FACE_VISEME_E,
        FACE_VISEME_I, FACE_VISEME_O, FACE_VISEME_U,
        FACE_VISEME_SS, FACE_VISEME_TH, FACE_VISEME_FF,
        FACE_VISEME_KK, FACE_VISEME_CH, FACE_VISEME_SIL,
    };
    uint16_t pixels[FACE_EYE_ACTOR_PIXEL_COUNT];
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        uint32_t hashes[12];
        for (size_t frame = 0U; frame < 12U; ++frame) {
            face_render_key_t key = baseline_key();
            key.viseme = sequence[frame];
            key.viseme_secondary = sequence[(frame + 1U) % 12U];
            key.viseme_blend = (uint8_t)(frame * 19U);
            key.audio_level = (uint8_t)(72U + (frame % 5U) * 35U);
            key.speech_phase = frame == 0U ? FACE_SPEECH_STARTING
                : frame == 11U ? FACE_SPEECH_ENDING
                : FACE_SPEECH_ACTIVE;
            assert(face_eye_actor_render(
                (face_eye_actor_style_t)style,
                &key,
                PREVIEW_CLOCK,
                pixels,
                FACE_EYE_ACTOR_PIXEL_COUNT));
            hashes[frame] = hash_pixels(pixels);
        }
        size_t unique = 0U;
        for (size_t frame = 0U; frame < 12U; ++frame) {
            bool seen = false;
            for (size_t earlier = 0U; earlier < frame; ++earlier) {
                seen |= hashes[earlier] == hashes[frame];
            }
            unique += !seen;
        }
        if (unique < 10U) {
            fprintf(stderr, "%s: only %zu distinct temporal frames\n",
                face_eye_actor_slug((face_eye_actor_style_t)style), unique);
        }
        assert(unique >= 10U);
    }
    puts("temporal speech: >=10/12 unique authored frames per style");
}

int main(void)
{
    test_metadata_and_legacy_mapping();
    test_rejection_and_determinism();
    test_clamping_and_guards();
    test_expression_separability();
    test_distinct_style_grammars();
    test_bilateral_mass_and_readable_mouths();
    test_full_ir_consumption();
    test_anki_ir_and_expression_semantics();
    test_blink_boundary_continuity();
    test_fixed_sockets_and_eye_only_speech();
    test_temporal_speaking();
    puts("face_eye_actors_test: all checks passed");
    return 0;
}
