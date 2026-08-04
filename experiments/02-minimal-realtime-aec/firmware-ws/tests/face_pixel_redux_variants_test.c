#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pixel_redux_variants.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 256,
    AUTHORED_FAMILY_COUNT = FACE_PIXEL_VARIANT_COUNT / 3,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_PIXEL_REDUX_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t random_state = 0x6a09e667U;

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
        frame->before[index] = 0x51a70000U + (uint32_t)index;
        frame->after[index] = 0xa7150000U + (uint32_t)index;
    }
}

static void assert_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x51a70000U + index);
        assert(frame->after[index] == 0xa7150000U + index);
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

static uint16_t test_rgb565(
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    return (uint16_t)(
        (((uint16_t)red & 0xf8U) << 8U) |
        (((uint16_t)green & 0xfcU) << 3U) |
        ((uint16_t)blue >> 3U));
}

static bool is_dmg_color(uint16_t color)
{
    return color == test_rgb565(15U, 56U, 15U) ||
        color == test_rgb565(48U, 98U, 48U) ||
        color == test_rgb565(139U, 172U, 15U) ||
        color == test_rgb565(155U, 188U, 15U);
}

static void assert_strict_dmg_palette(const uint16_t *pixels)
{
    unsigned colors = 0U;
    for (size_t index = 0U;
         index < FACE_PIXEL_REDUX_PIXEL_COUNT;
         ++index) {
        const uint16_t color = pixels[index];
        assert(is_dmg_color(color));
        colors |= color == test_rgb565(15U, 56U, 15U) ? 1U : 0U;
        colors |= color == test_rgb565(48U, 98U, 48U) ? 2U : 0U;
        colors |= color == test_rgb565(139U, 172U, 15U) ? 4U : 0U;
        colors |= color == test_rgb565(155U, 188U, 15U) ? 8U : 0U;
    }
    assert(colors == 15U);
}

static void test_metadata_and_contract(void)
{
    assert(sizeof(face_render_key_t) == FACE_RENDER_KEY_BYTES);
    assert(face_pixel_redux_variant_count() ==
        FACE_PIXEL_VARIANT_COUNT);
    unsigned family_counts[FACE_PIXEL_REDUX_ACTOR_COUNT] = {0U};
    for (size_t raw = 0U; raw < FACE_PIXEL_VARIANT_COUNT; ++raw) {
        const face_pixel_redux_variant_t variant =
            (face_pixel_redux_variant_t)raw;
        face_pixel_redux_variant_info_t info;
        assert(face_pixel_redux_variant_info(variant, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.base_actor < FACE_PIXEL_REDUX_ACTOR_COUNT);
        assert(info.authored_version >= 1U);
        assert(info.authored_version <= 3U);
        assert(info.palette_size >= 4U);
        assert(info.estimated_ops_per_pixel >= 1U);
        assert(strcmp(
            info.slug, face_pixel_redux_variant_slug(variant)) == 0);
        assert(strcmp(
            info.name, face_pixel_redux_variant_name(variant)) == 0);
        ++family_counts[info.base_actor];
        for (size_t earlier = 0U; earlier < raw; ++earlier) {
            assert(strcmp(
                info.slug,
                face_pixel_redux_variant_slug(
                    (face_pixel_redux_variant_t)earlier)) != 0);
        }
    }
    for (size_t family = 0U;
         family < FACE_PIXEL_REDUX_ACTOR_COUNT;
         ++family) {
        const unsigned expected =
            family == FACE_PIXEL_REDUX_POCKET_RPG ? 6U : 3U;
        assert(family_counts[family] == expected);
    }
    assert(face_pixel_redux_variant_slug(
        FACE_PIXEL_VARIANT_COUNT) == NULL);
    assert(face_pixel_redux_variant_name(
        (face_pixel_redux_variant_t)-1) == NULL);
    assert(!face_pixel_redux_variant_info(
        FACE_PIXEL_VARIANT_COUNT, NULL));
}

static void test_rejection_and_determinism(void)
{
    const face_render_key_t key = baseline_key();
    guarded_frame_t first;
    guarded_frame_t second;
    init_guard(&first);
    init_guard(&second);
    assert(!face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_COUNT, &key, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_EGA_SUNBLADE_RANGER, NULL, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_EGA_SUNBLADE_RANGER, &key, 0U,
        NULL, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(!face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_EGA_SUNBLADE_RANGER, &key, 0U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT - 1U));
    assert(face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT, &key, 4933U,
        first.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(face_pixel_redux_variant_render(
        FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT, &key, 4933U,
        second.pixels, FACE_PIXEL_REDUX_PIXEL_COUNT));
    assert(memcmp(
        first.pixels, second.pixels, sizeof(first.pixels)) == 0);
    assert_guard(&first);
    assert_guard(&second);
}

static void test_all_expressions_and_visemes(void)
{
    for (size_t raw = 0U; raw < FACE_PIXEL_VARIANT_COUNT; ++raw) {
        const face_pixel_redux_variant_t variant =
            (face_pixel_redux_variant_t)raw;
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
            assert(face_pixel_redux_variant_render(
                variant, &key, 4096U, frame,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            expression_hashes[expression] = hash_pixels(frame);
            if (variant >= FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT) {
                assert_strict_dmg_palette(frame);
            }
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                assert(expression_hashes[expression] !=
                    expression_hashes[FACE_EXPRESSION_NEUTRAL]);
            }
        }

        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        size_t unique = 0U;
        for (uint8_t viseme = 0U;
             viseme < FACE_VISEME_COUNT;
             ++viseme) {
            face_render_key_t key = baseline_key();
            key.stage_expression = FACE_EXPRESSION_WARM;
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            uint16_t frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
            assert(face_pixel_redux_variant_render(
                variant, &key, 7211U, frame,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            viseme_hashes[viseme] = hash_pixels(frame);
            bool seen = false;
            for (uint8_t earlier = 0U;
                 earlier < viseme;
                 ++earlier) {
                seen |= viseme_hashes[viseme] ==
                    viseme_hashes[earlier];
            }
            unique += !seen;
        }
        if (unique < 10U) {
            fprintf(
                stderr, "%s: only %zu distinct viseme rasters\n",
                face_pixel_redux_variant_slug(variant), unique);
        }
        assert(unique >= 10U);
    }
}

static void test_three_authored_versions_are_distinct(void)
{
    const face_render_key_t key = baseline_key();
    for (size_t family = 0U;
         family < AUTHORED_FAMILY_COUNT;
         ++family) {
        uint16_t frames[3][FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint32_t hashes[3];
        for (size_t version = 0U; version < 3U; ++version) {
            const face_pixel_redux_variant_t variant =
                (face_pixel_redux_variant_t)(family * 3U + version);
            assert(face_pixel_redux_variant_render(
                variant, &key, 6144U,
                frames[version], FACE_PIXEL_REDUX_PIXEL_COUNT));
            hashes[version] = hash_pixels(frames[version]);
        }
        assert(hashes[0] != hashes[1]);
        assert(hashes[0] != hashes[2]);
        assert(hashes[1] != hashes[2]);
        assert(differing_pixels(frames[0], frames[1]) > 5000U);
        assert(differing_pixels(frames[0], frames[2]) > 5000U);
        assert(differing_pixels(frames[1], frames[2]) > 5000U);
    }
}

static void test_mossling_natural_motion(void)
{
    face_render_key_t key = baseline_key();
    key.controls.flags = 0U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.viseme = FACE_VISEME_SIL;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_weight = 0U;
    key.stage_expression = FACE_EXPRESSION_WARM;
    for (size_t raw = FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT;
         raw <= FACE_PIXEL_VARIANT_POCKET_MOONCAP_FAMILIAR;
         ++raw) {
        uint16_t neutral[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t bob[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t turn[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t blink_lead[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t blink_hold[FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint16_t blink_trail[FACE_PIXEL_REDUX_PIXEL_COUNT];
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 0U,
            neutral, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 16000U,
            bob, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 96000U,
            turn, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 48100U,
            blink_lead, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 48650U,
            blink_hold, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(face_pixel_redux_variant_render(
            (face_pixel_redux_variant_t)raw, &key, 49200U,
            blink_trail, FACE_PIXEL_REDUX_PIXEL_COUNT));
        assert(differing_pixels(neutral, bob) > 0U);
        assert(differing_pixels(neutral, turn) > 0U);
        assert(differing_pixels(blink_lead, blink_hold) > 0U);
        assert(differing_pixels(blink_hold, blink_trail) > 0U);
        assert(differing_pixels(blink_lead, blink_trail) > 0U);
    }
}

static void test_temporal_acting_and_continuity(void)
{
    for (size_t raw = 0U; raw < FACE_PIXEL_VARIANT_COUNT; ++raw) {
        const face_pixel_redux_variant_t variant =
            (face_pixel_redux_variant_t)raw;
        face_render_key_t keys[5];
        for (size_t index = 0U; index < 5U; ++index) {
            keys[index] = baseline_key();
        }
        keys[0].controls.flags = 0U;
        keys[0].controls.expression = FACE_ACTIVITY_LISTENING;
        keys[0].speech_phase = FACE_SPEECH_IDLE;
        keys[0].viseme = FACE_VISEME_SIL;
        keys[0].viseme_secondary = FACE_VISEME_SIL;
        keys[0].viseme_weight = 0U;
        keys[1] = keys[0];
        keys[1].controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        keys[1].controls.expression = FACE_ACTIVITY_SPEAKING;
        keys[1].speech_phase = FACE_SPEECH_STARTING;
        keys[2] = keys[1];
        keys[2].speech_phase = FACE_SPEECH_ACTIVE;
        keys[2].viseme = FACE_VISEME_AA;
        keys[2].viseme_secondary = FACE_VISEME_E;
        keys[2].viseme_weight = 255U;
        keys[3] = keys[2];
        keys[3].speech_phase = FACE_SPEECH_ENDING;
        keys[3].viseme = FACE_VISEME_SIL;
        keys[3].viseme_secondary = FACE_VISEME_SIL;
        keys[4] = keys[0];

        uint16_t frames[5][FACE_PIXEL_REDUX_PIXEL_COUNT];
        uint32_t hashes[5];
        for (size_t index = 0U; index < 5U; ++index) {
            assert(face_pixel_redux_variant_render(
                variant, &keys[index], (uint32_t)index * 533U,
                frames[index], FACE_PIXEL_REDUX_PIXEL_COUNT));
            hashes[index] = hash_pixels(frames[index]);
            if (index > 0U) {
                const size_t changed =
                    differing_pixels(frames[index - 1U], frames[index]);
                assert(changed > 0U);
                assert(changed < 10000U);
            }
        }
        assert(hashes[0] != hashes[1]);
        assert(hashes[1] != hashes[2]);
        assert(hashes[2] != hashes[3]);
        assert(hashes[3] != hashes[4]);
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
        const uint32_t clock = next_random();
        for (size_t raw = 0U;
             raw < FACE_PIXEL_VARIANT_COUNT;
             ++raw) {
            guarded_frame_t frame;
            init_guard(&frame);
            assert(face_pixel_redux_variant_render(
                (face_pixel_redux_variant_t)raw,
                &key,
                clock,
                frame.pixels,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            assert_guard(&frame);
            assert_safe_border(frame.pixels);
            if (raw >= FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT) {
                assert_strict_dmg_palette(frame.pixels);
            }
        }
    }
}

int main(void)
{
    test_metadata_and_contract();
    test_rejection_and_determinism();
    test_all_expressions_and_visemes();
    test_three_authored_versions_are_distinct();
    test_mossling_natural_motion();
    test_temporal_acting_and_continuity();
    test_adversarial_clipping();
    printf(
        "face_pixel_redux_variants_test: PASS "
        "(%d authored variants, 11 emotions, >=10 visemes, "
        "anticipation/settle, %d fuzz frames)\n",
        FACE_PIXEL_VARIANT_COUNT,
        FUZZ_CASES * FACE_PIXEL_VARIANT_COUNT);
    return 0;
}
