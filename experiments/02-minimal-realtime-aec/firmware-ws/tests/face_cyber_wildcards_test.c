#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_cyber_wildcards.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 16,
    FUZZ_CASES = 512,
    CONTACT_HALF_WIDTH = FACE_CYBER_WILDCARD_WIDTH / 2,
    CONTACT_HALF_HEIGHT = FACE_CYBER_WILDCARD_HEIGHT / 2,
    CONTACT_QUARTER_WIDTH = FACE_CYBER_WILDCARD_WIDTH / 4,
    CONTACT_QUARTER_HEIGHT = FACE_CYBER_WILDCARD_HEIGHT / 4,
    SPEECH_FRAMES = 16,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_CYBER_WILDCARD_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t hash_pixels(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_CYBER_WILDCARD_PIXEL_COUNT;
         ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t differing_pixels(
    const uint16_t *first, const uint16_t *second)
{
    size_t count = 0U;
    for (size_t index = 0U;
         index < FACE_CYBER_WILDCARD_PIXEL_COUNT;
         ++index) {
        count += first[index] != second[index];
    }
    return count;
}

static uint16_t pack_rgb565(
    uint32_t red, uint32_t green, uint32_t blue)
{
    return (uint16_t)(
        ((red & 31U) << 11U) |
        ((green & 63U) << 5U) |
        (blue & 31U));
}

static void downsample_box(
    const uint16_t *source,
    uint16_t *destination,
    size_t divisor)
{
    const size_t width = FACE_CYBER_WILDCARD_WIDTH / divisor;
    const size_t height = FACE_CYBER_WILDCARD_HEIGHT / divisor;
    const uint32_t samples = (uint32_t)(divisor * divisor);
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            uint32_t red = 0U;
            uint32_t green = 0U;
            uint32_t blue = 0U;
            for (size_t yy = 0U; yy < divisor; ++yy) {
                for (size_t xx = 0U; xx < divisor; ++xx) {
                    const uint16_t pixel = source[
                        (y * divisor + yy) *
                            FACE_CYBER_WILDCARD_WIDTH +
                        x * divisor + xx];
                    red += (pixel >> 11U) & 31U;
                    green += (pixel >> 5U) & 63U;
                    blue += pixel & 31U;
                }
            }
            destination[y * width + x] = pack_rgb565(
                (red + samples / 2U) / samples,
                (green + samples / 2U) / samples,
                (blue + samples / 2U) / samples);
        }
    }
}

static size_t differing_contact_pixels(
    const uint16_t *first,
    const uint16_t *second,
    size_t count)
{
    size_t differing = 0U;
    for (size_t index = 0U; index < count; ++index) {
        differing += first[index] != second[index];
    }
    return differing;
}

static uint32_t hash_contact_pixels(
    const uint16_t *pixels, size_t count)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < count; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t pose_geometry_delta(
    const face_cyber_wildcard_pose_t *first,
    const face_cyber_wildcard_pose_t *second)
{
    const int32_t a[] = {
        first->gaze_x,
        first->gaze_y,
        first->eye_open_left,
        first->eye_open_right,
        first->brow_y_left,
        first->brow_y_right,
        first->brow_slope_left,
        first->brow_slope_right,
        first->mouth_width,
        first->mouth_open,
        first->mouth_round,
        first->mouth_press,
        first->mouth_corner_left,
        first->mouth_corner_right,
        first->head_roll,
        first->cheek,
    };
    const int32_t b[] = {
        second->gaze_x,
        second->gaze_y,
        second->eye_open_left,
        second->eye_open_right,
        second->brow_y_left,
        second->brow_y_right,
        second->brow_slope_left,
        second->brow_slope_right,
        second->mouth_width,
        second->mouth_open,
        second->mouth_round,
        second->mouth_press,
        second->mouth_corner_left,
        second->mouth_corner_right,
        second->head_roll,
        second->cheek,
    };
    size_t delta = 0U;
    for (size_t index = 0U; index < sizeof(a) / sizeof(a[0]); ++index) {
        delta += a[index] != b[index];
    }
    return delta;
}

static uint32_t pose_geometry_hash(
    const face_cyber_wildcard_pose_t *pose)
{
    const int32_t values[] = {
        pose->gaze_x,
        pose->gaze_y,
        pose->eye_open_left,
        pose->eye_open_right,
        pose->brow_y_left,
        pose->brow_y_right,
        pose->brow_slope_left,
        pose->brow_slope_right,
        pose->mouth_width,
        pose->mouth_open,
        pose->mouth_round,
        pose->mouth_press,
        pose->mouth_corner_left,
        pose->mouth_corner_right,
        pose->head_roll,
        pose->cheek,
    };
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < sizeof(values) / sizeof(values[0]);
         ++index) {
        hash ^= (uint32_t)values[index];
        hash *= 16777619U;
    }
    return hash;
}

static face_render_key_t baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 124U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 42U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 128U;
    key.controls.eye_left_open = 236U;
    key.controls.eye_right_open = 240U;
    key.controls.look_x = 5;
    key.controls.look_y = -3;
    key.controls.brow = 5;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 220U;
    key.audio_level = 136U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 36U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 8;
    key.mouth_corner_right = 4;
    key.tongue = 72U;
    key.cheek = 34U;
    key.eye_left_squint = 5U;
    key.eye_right_squint = 7U;
    key.brow_inner = 5;
    key.brow_outer_left = -3;
    key.brow_outer_right = 4;
    key.head_roll = 3;
    key.affect_valence = 24;
    key.affect_arousal = 142U;
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

static void check_outer_border(const uint16_t *pixels)
{
    const uint16_t background = pixels[0];
    for (size_t y = 0U; y < FACE_CYBER_WILDCARD_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_CYBER_WILDCARD_WIDTH; ++x) {
            if (x < 3U || x >= FACE_CYBER_WILDCARD_WIDTH - 3U ||
                y < 3U || y >= FACE_CYBER_WILDCARD_HEIGHT - 3U) {
                assert(
                    pixels[y * FACE_CYBER_WILDCARD_WIDTH + x] ==
                    background);
            }
        }
    }
}

static void test_metadata_mapping_and_rejection(void)
{
    static const uint8_t EXPECTED_IDS[FACE_CYBER_WILDCARD_COUNT] = {
        33U, 38U, 39U,
    };
    assert(face_cyber_wildcard_count() == FACE_CYBER_WILDCARD_COUNT);
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        const face_cyber_wildcard_profile_t profile =
            (face_cyber_wildcard_profile_t)raw;
        face_cyber_wildcard_info_t info;
        face_cyber_wildcard_profile_t mapped =
            FACE_CYBER_WILDCARD_COUNT;
        assert(face_cyber_wildcard_info(profile, &info));
        assert(info.slug != NULL && info.slug[0] != '\0');
        assert(info.name != NULL && info.name[0] != '\0');
        assert(info.legacy_profile_id == EXPECTED_IDS[raw]);
        assert(strcmp(info.slug, face_cyber_wildcard_slug(profile)) == 0);
        assert(strcmp(info.name, face_cyber_wildcard_name(profile)) == 0);
        assert(face_cyber_wildcard_from_legacy_id(
            EXPECTED_IDS[raw], &mapped));
        assert(mapped == profile);
    }
    assert(!face_cyber_wildcard_from_legacy_id(32U, NULL));
    face_cyber_wildcard_profile_t mapped = FACE_CYBER_WILDCARD_CHLADNI;
    assert(!face_cyber_wildcard_from_legacy_id(37U, &mapped));
    assert(face_cyber_wildcard_slug(FACE_CYBER_WILDCARD_COUNT) == NULL);
    assert(face_cyber_wildcard_name(
        (face_cyber_wildcard_profile_t)-1) == NULL);
    assert(!face_cyber_wildcard_info(
        FACE_CYBER_WILDCARD_COUNT, NULL));

    guarded_frame_t frame;
    init_guard(&frame);
    face_render_key_t key = baseline_key();
    assert(!face_cyber_wildcard_render(
        FACE_CYBER_WILDCARD_COUNT,
        &key,
        0U,
        frame.pixels,
        FACE_CYBER_WILDCARD_PIXEL_COUNT));
    assert(!face_cyber_wildcard_render(
        FACE_CYBER_WILDCARD_CHLADNI,
        NULL,
        0U,
        frame.pixels,
        FACE_CYBER_WILDCARD_PIXEL_COUNT));
    assert(!face_cyber_wildcard_render(
        FACE_CYBER_WILDCARD_CHLADNI,
        &key,
        0U,
        NULL,
        FACE_CYBER_WILDCARD_PIXEL_COUNT));
    assert(!face_cyber_wildcard_render(
        FACE_CYBER_WILDCARD_CHLADNI,
        &key,
        0U,
        frame.pixels,
        FACE_CYBER_WILDCARD_PIXEL_COUNT - 1U));
}

static void assert_pose_bounds(
    const face_cyber_wildcard_pose_t *pose)
{
    assert(pose->face_x == 80);
    assert(pose->face_y == 59);
    assert(pose->gaze_x >= -7 && pose->gaze_x <= 7);
    assert(pose->gaze_y >= -5 && pose->gaze_y <= 5);
    assert(pose->eye_y == 48);
    assert(pose->eye_spacing == 62);
    assert(pose->eye_width == 32);
    assert(pose->eye_open_left >= 3 && pose->eye_open_left <= 25);
    assert(pose->eye_open_right >= 3 && pose->eye_open_right <= 25);
    assert(pose->brow_y_left >= 19 && pose->brow_y_left <= 38);
    assert(pose->brow_y_right >= 19 && pose->brow_y_right <= 38);
    assert(pose->brow_slope_left >= -8 &&
        pose->brow_slope_left <= 8);
    assert(pose->brow_slope_right >= -8 &&
        pose->brow_slope_right <= 8);
    assert(pose->mouth_x == 80);
    assert(pose->mouth_y == 87);
    assert(pose->mouth_width >= 18 && pose->mouth_width <= 52);
    assert(pose->mouth_open >= 3 && pose->mouth_open <= 28);
    assert(pose->mouth_x - pose->mouth_width / 2 >= 49);
    assert(pose->mouth_x + pose->mouth_width / 2 <= 111);
    assert(pose->mouth_y - pose->mouth_open / 2 >= 73);
    assert(pose->mouth_y + pose->mouth_open / 2 <= 101);
}

static uint32_t random_state = 0x6b91c3e5U;

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_full_ir_clamping_guards_and_determinism(void)
{
    size_t checked = 0U;
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
        for (size_t raw = 0U;
             raw < FACE_CYBER_WILDCARD_COUNT;
             ++raw) {
            const face_cyber_wildcard_profile_t profile =
                (face_cyber_wildcard_profile_t)raw;
            face_cyber_wildcard_pose_t pose;
            guarded_frame_t first;
            guarded_frame_t second;
            init_guard(&first);
            init_guard(&second);
            const uint32_t clock = next_random();
            assert(face_cyber_wildcard_resolve(
                profile, &key, clock, &pose));
            assert(memcmp(&pose.source, &key, sizeof(key)) == 0);
            assert_pose_bounds(&pose);
            assert(face_cyber_wildcard_render(
                profile,
                &key,
                clock,
                first.pixels,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            assert(face_cyber_wildcard_render(
                profile,
                &key,
                clock,
                second.pixels,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            assert(memcmp(
                first.pixels,
                second.pixels,
                sizeof(first.pixels)) == 0);
            check_guard(&first);
            check_guard(&second);
            check_outer_border(first.pixels);
            ++checked;
        }
    }
    printf("full-IR sanitizer corpus: %zu frames\n", checked);
}

static void test_every_ir_byte_is_observable(void)
{
    face_render_key_t base = baseline_key();
    const uint8_t *base_bytes = (const uint8_t *)&base;
    size_t smallest_delta = FACE_CYBER_WILDCARD_PIXEL_COUNT;
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        uint16_t baseline[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        face_cyber_wildcard_pose_t baseline_pose;
        assert(face_cyber_wildcard_resolve(
            (face_cyber_wildcard_profile_t)raw,
            &base,
            20000U,
            &baseline_pose));
        assert(face_cyber_wildcard_render(
            (face_cyber_wildcard_profile_t)raw,
            &base,
            20000U,
            baseline,
            FACE_CYBER_WILDCARD_PIXEL_COUNT));
        for (size_t byte = 0U; byte < sizeof(base); ++byte) {
            face_render_key_t changed = base;
            uint8_t *changed_bytes = (uint8_t *)&changed;
            changed_bytes[byte] =
                (uint8_t)(base_bytes[byte] ^ (uint8_t)(0x5bU + byte));
            uint16_t comparison[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            face_cyber_wildcard_pose_t pose;
            assert(face_cyber_wildcard_resolve(
                (face_cyber_wildcard_profile_t)raw,
                &changed,
                20000U,
                &pose));
            assert(memcmp(&pose.source, &changed, sizeof(changed)) == 0);
            assert(
                pose.input_signature != baseline_pose.input_signature);
            assert(face_cyber_wildcard_render(
                (face_cyber_wildcard_profile_t)raw,
                &changed,
                20000U,
                comparison,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            const size_t delta =
                differing_pixels(baseline, comparison);
            if (delta < smallest_delta) {
                smallest_delta = delta;
            }
            assert(delta >= 8U);
        }
    }
    printf(
        "full 40-byte IR observability: 3x40 mutations min=%zu px\n",
        smallest_delta);
}

static void test_expression_and_viseme_separability(void)
{
    size_t minimum_expression_delta =
        FACE_CYBER_WILDCARD_PIXEL_COUNT;
    size_t minimum_expression_half_delta =
        CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT;
    size_t minimum_expression_quarter_delta =
        CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT;
    size_t minimum_expression_geometry_delta = 100U;
    size_t minimum_viseme_delta =
        FACE_CYBER_WILDCARD_PIXEL_COUNT;
    size_t minimum_viseme_quarter_delta =
        CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT;
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        uint16_t neutral[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        uint16_t comparison[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        uint16_t neutral_half[
            CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT];
        uint16_t comparison_half[
            CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT];
        uint16_t neutral_quarter[
            CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT];
        uint16_t comparison_quarter[
            CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT];
        uint32_t expression_hashes[FACE_EXPRESSION_COUNT];
        uint32_t expression_half_hashes[FACE_EXPRESSION_COUNT];
        uint32_t expression_quarter_hashes[FACE_EXPRESSION_COUNT];
        uint32_t expression_geometry_hashes[FACE_EXPRESSION_COUNT];
        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        face_render_key_t key = baseline_key();
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        key.controls.flags = 0U;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.viseme_weight = 0U;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        assert(face_cyber_wildcard_render(
            (face_cyber_wildcard_profile_t)raw,
            &key,
            19231U,
            neutral,
            FACE_CYBER_WILDCARD_PIXEL_COUNT));
        downsample_box(neutral, neutral_half, 2U);
        downsample_box(neutral, neutral_quarter, 4U);
        face_cyber_wildcard_pose_t neutral_pose;
        assert(face_cyber_wildcard_resolve(
            (face_cyber_wildcard_profile_t)raw,
            &key,
            19231U,
            &neutral_pose));
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            key.stage_expression = expression;
            face_cyber_wildcard_pose_t pose;
            assert(face_cyber_wildcard_resolve(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                19231U,
                &pose));
            assert(face_cyber_wildcard_render(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                19231U,
                comparison,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            expression_hashes[expression] = hash_pixels(comparison);
            downsample_box(comparison, comparison_half, 2U);
            downsample_box(comparison, comparison_quarter, 4U);
            expression_half_hashes[expression] =
                hash_contact_pixels(
                    comparison_half,
                    CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT);
            expression_quarter_hashes[expression] =
                hash_contact_pixels(
                    comparison_quarter,
                    CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT);
            expression_geometry_hashes[expression] =
                pose_geometry_hash(&pose);
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                const size_t delta =
                    differing_pixels(neutral, comparison);
                const size_t half_delta = differing_contact_pixels(
                    neutral_half,
                    comparison_half,
                    CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT);
                const size_t quarter_delta = differing_contact_pixels(
                    neutral_quarter,
                    comparison_quarter,
                    CONTACT_QUARTER_WIDTH *
                        CONTACT_QUARTER_HEIGHT);
                const size_t geometry_delta =
                    pose_geometry_delta(&neutral_pose, &pose);
                if (delta < minimum_expression_delta) {
                    minimum_expression_delta = delta;
                }
                if (half_delta < minimum_expression_half_delta) {
                    minimum_expression_half_delta = half_delta;
                }
                if (quarter_delta < minimum_expression_quarter_delta) {
                    minimum_expression_quarter_delta = quarter_delta;
                }
                if (geometry_delta < minimum_expression_geometry_delta) {
                    minimum_expression_geometry_delta = geometry_delta;
                }
                assert(delta >= 20U);
                assert(half_delta >= 8U);
                assert(quarter_delta >= 3U);
                /* No expression may be only a palette swap. */
                assert(geometry_delta >= 3U);
            }
        }
        for (size_t first = 0U;
             first < FACE_EXPRESSION_COUNT;
             ++first) {
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                assert(expression_hashes[first] !=
                    expression_hashes[second]);
                assert(expression_half_hashes[first] !=
                    expression_half_hashes[second]);
                assert(expression_quarter_hashes[first] !=
                    expression_quarter_hashes[second]);
                assert(expression_geometry_hashes[first] !=
                    expression_geometry_hashes[second]);
            }
        }

        key = baseline_key();
        key.viseme_secondary = FACE_VISEME_SIL;
        key.viseme_blend = 0U;
        key.viseme_weight = 255U;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT; ++viseme) {
            key.viseme = viseme;
            assert(face_cyber_wildcard_render(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                19231U,
                comparison,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            viseme_hashes[viseme] = hash_pixels(comparison);
        }
        key.viseme = FACE_VISEME_SIL;
        assert(face_cyber_wildcard_render(
            (face_cyber_wildcard_profile_t)raw,
            &key,
            19231U,
            neutral,
            FACE_CYBER_WILDCARD_PIXEL_COUNT));
        downsample_box(neutral, neutral_quarter, 4U);
        for (uint8_t viseme = 0U; viseme < FACE_VISEME_COUNT - 1U;
             ++viseme) {
            key.viseme = viseme;
            assert(face_cyber_wildcard_render(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                19231U,
                comparison,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            const size_t delta = differing_pixels(neutral, comparison);
            if (delta < minimum_viseme_delta) {
                minimum_viseme_delta = delta;
            }
            downsample_box(comparison, comparison_quarter, 4U);
            const size_t quarter_delta = differing_contact_pixels(
                neutral_quarter,
                comparison_quarter,
                CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT);
            if (quarter_delta < minimum_viseme_quarter_delta) {
                minimum_viseme_quarter_delta = quarter_delta;
            }
            assert(delta >= 8U);
            assert(quarter_delta >= 1U);
        }
        for (size_t first = 0U; first < FACE_VISEME_COUNT; ++first) {
            for (size_t second = first + 1U;
                 second < FACE_VISEME_COUNT;
                 ++second) {
                assert(viseme_hashes[first] != viseme_hashes[second]);
            }
        }
    }
    printf(
        "separability: expressions native=%zu half=%zu quarter=%zu "
        "geometry=%zu; visemes native=%zu quarter=%zu\n",
        minimum_expression_delta,
        minimum_expression_half_delta,
        minimum_expression_quarter_delta,
        minimum_expression_geometry_delta,
        minimum_viseme_delta,
        minimum_viseme_quarter_delta);
}

static face_render_key_t speech_key(size_t frame)
{
    static const uint8_t VISEMES[SPEECH_FRAMES] = {
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
        FACE_VISEME_DD,
        FACE_VISEME_RR,
        FACE_VISEME_CH,
        FACE_VISEME_SIL,
        FACE_VISEME_SIL,
    };
    static const uint8_t LEVELS[SPEECH_FRAMES] = {
        8U, 24U, 88U, 116U, 132U, 148U, 122U, 74U,
        96U, 112U, 126U, 138U, 118U, 86U, 42U, 10U,
    };
    static const uint8_t OPEN[SPEECH_FRAMES] = {
        12U, 24U, 86U, 112U, 132U, 154U, 120U, 48U,
        70U, 88U, 104U, 116U, 102U, 76U, 34U, 14U,
    };
    face_render_key_t key = baseline_key();
    key.stage_expression = FACE_EXPRESSION_WARM;
    key.viseme = VISEMES[frame];
    key.viseme_secondary =
        VISEMES[frame + 1U < SPEECH_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        frame > 1U && frame < 14U ? 52U : 0U;
    key.audio_level = LEVELS[frame];
    key.controls.mouth_open = OPEN[frame];
    key.controls.mouth_width =
        (uint8_t)(154U + (frame % 5U) * 10U);
    key.controls.mouth_round =
        (uint8_t)(32U + (frame % 4U) * 38U);
    key.speech_phase =
        frame == 0U ? FACE_SPEECH_IDLE
        : frame == 1U ? FACE_SPEECH_STARTING
        : frame >= 14U ? FACE_SPEECH_ENDING
        : FACE_SPEECH_ACTIVE;
    if (frame == 0U) {
        key.controls.flags = 0U;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        key.viseme_weight = 0U;
    } else if (frame == 1U) {
        key.viseme_weight = 96U;
    } else if (frame >= 14U) {
        key.viseme_weight = (uint8_t)(frame == 14U ? 96U : 32U);
    } else {
        key.viseme_weight = 238U;
    }
    return key;
}

static bool is_speech_dynamic_region(
    face_cyber_wildcard_profile_t profile, size_t x, size_t y)
{
    if (x >= 29U && x <= 131U && y >= 18U && y <= 100U) {
        return true;
    }
    if (x >= 32U && x <= 126U && y >= 101U && y <= 106U) {
        return true;
    }
    return profile == FACE_CYBER_WILDCARD_TELETEXT &&
        x >= 139U && x <= 144U && y >= 11U && y <= 15U;
}

static size_t differing_outside_speech_region(
    face_cyber_wildcard_profile_t profile,
    const uint16_t *first,
    const uint16_t *second)
{
    size_t count = 0U;
    for (size_t y = 0U; y < FACE_CYBER_WILDCARD_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_CYBER_WILDCARD_WIDTH; ++x) {
            if (!is_speech_dynamic_region(profile, x, y)) {
                const size_t index =
                    y * FACE_CYBER_WILDCARD_WIDTH + x;
                count += first[index] != second[index];
            }
        }
    }
    return count;
}

static void test_speech_alignment_continuity_and_no_clipping(void)
{
    size_t largest_native_step = 0U;
    size_t largest_half_step = 0U;
    size_t largest_quarter_step = 0U;
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        uint16_t previous[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        uint16_t current[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        uint16_t previous_half[
            CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT];
        uint16_t current_half[
            CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT];
        uint16_t previous_quarter[
            CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT];
        uint16_t current_quarter[
            CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT];
        face_cyber_wildcard_pose_t poses[SPEECH_FRAMES];
        bool saw_active_eye_change = false;
        for (size_t frame = 0U; frame < SPEECH_FRAMES; ++frame) {
            const face_render_key_t key = speech_key(frame);
            assert(face_cyber_wildcard_resolve(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                16000U + (uint32_t)frame * 533U,
                &poses[frame]));
            assert_pose_bounds(&poses[frame]);
            assert(face_cyber_wildcard_render(
                (face_cyber_wildcard_profile_t)raw,
                &key,
                16000U + (uint32_t)frame * 533U,
                current,
                FACE_CYBER_WILDCARD_PIXEL_COUNT));
            check_outer_border(current);
            downsample_box(current, current_half, 2U);
            downsample_box(current, current_quarter, 4U);
            if (frame > 0U) {
                const size_t native_step =
                    differing_pixels(previous, current);
                const size_t half_step = differing_contact_pixels(
                    previous_half,
                    current_half,
                    CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT);
                const size_t quarter_step = differing_contact_pixels(
                    previous_quarter,
                    current_quarter,
                    CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT);
                if (native_step > largest_native_step) {
                    largest_native_step = native_step;
                }
                if (half_step > largest_half_step) {
                    largest_half_step = half_step;
                }
                if (quarter_step > largest_quarter_step) {
                    largest_quarter_step = quarter_step;
                }
                assert(native_step < 3200U);
                assert(half_step < 1100U);
                assert(quarter_step < 360U);
                assert(differing_outside_speech_region(
                    (face_cyber_wildcard_profile_t)raw,
                    previous,
                    current) == 0U);
                if (frame >= 3U && frame <= 13U &&
                    (poses[frame].eye_open_left !=
                            poses[frame - 1U].eye_open_left ||
                     poses[frame].eye_open_right !=
                            poses[frame - 1U].eye_open_right)) {
                    saw_active_eye_change = true;
                }
            }
            memcpy(previous, current, sizeof(previous));
            memcpy(previous_half, current_half, sizeof(previous_half));
            memcpy(
                previous_quarter,
                current_quarter,
                sizeof(previous_quarter));
        }
        assert(poses[0].face_x == poses[15].face_x);
        assert(poses[0].eye_y == poses[15].eye_y);
        assert(poses[0].eye_spacing == poses[15].eye_spacing);
        assert(poses[0].mouth_x == poses[15].mouth_x);
        assert(poses[0].mouth_y == poses[15].mouth_y);
        assert(poses[1].eye_open_left != poses[0].eye_open_left ||
            poses[1].brow_y_left != poses[0].brow_y_left);
        assert(pose_geometry_delta(&poses[1], &poses[2]) >= 3U);
        assert(pose_geometry_delta(&poses[13], &poses[14]) >= 3U);
        assert(saw_active_eye_change);
    }
    printf(
        "16f speech continuity: max native=%zu half=%zu quarter=%zu; "
        "fixed sockets and exterior\n",
        largest_native_step,
        largest_half_step,
        largest_quarter_step);
}

static void test_distinct_visual_languages_and_motion(void)
{
    face_render_key_t key = baseline_key();
    uint16_t frames[FACE_CYBER_WILDCARD_COUNT]
        [FACE_CYBER_WILDCARD_PIXEL_COUNT];
    uint16_t half_frames[FACE_CYBER_WILDCARD_COUNT]
        [CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT];
    uint16_t quarter_frames[FACE_CYBER_WILDCARD_COUNT]
        [CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT];
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        assert(face_cyber_wildcard_render(
            (face_cyber_wildcard_profile_t)raw,
            &key,
            19231U,
            frames[raw],
            FACE_CYBER_WILDCARD_PIXEL_COUNT));
        downsample_box(frames[raw], half_frames[raw], 2U);
        downsample_box(frames[raw], quarter_frames[raw], 4U);
    }
    size_t closest = FACE_CYBER_WILDCARD_PIXEL_COUNT;
    size_t closest_half = CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT;
    size_t closest_quarter =
        CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT;
    for (size_t first = 0U;
         first < FACE_CYBER_WILDCARD_COUNT;
         ++first) {
        for (size_t second = first + 1U;
             second < FACE_CYBER_WILDCARD_COUNT;
             ++second) {
            const size_t delta =
                differing_pixels(frames[first], frames[second]);
            if (delta < closest) {
                closest = delta;
            }
            const size_t half_delta = differing_contact_pixels(
                half_frames[first],
                half_frames[second],
                CONTACT_HALF_WIDTH * CONTACT_HALF_HEIGHT);
            const size_t quarter_delta = differing_contact_pixels(
                quarter_frames[first],
                quarter_frames[second],
                CONTACT_QUARTER_WIDTH * CONTACT_QUARTER_HEIGHT);
            if (half_delta < closest_half) {
                closest_half = half_delta;
            }
            if (quarter_delta < closest_quarter) {
                closest_quarter = quarter_delta;
            }
            assert(delta > 7000U);
            assert(half_delta > 1800U);
            assert(quarter_delta > 420U);
        }
    }

    /*
     * Sample clocks align events; they are not permission for decorative
     * shimmer.  A repeated IR frame must be bit-for-bit stable at any clock.
     */
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        uint16_t comparison[FACE_CYBER_WILDCARD_PIXEL_COUNT];
        assert(face_cyber_wildcard_render(
            (face_cyber_wildcard_profile_t)raw,
            &key,
            0xffffffffU,
            comparison,
            FACE_CYBER_WILDCARD_PIXEL_COUNT));
        assert(memcmp(
            frames[raw], comparison, sizeof(comparison)) == 0);
    }
    printf(
        "visual grammar closest native=%zu half=%zu quarter=%zu; "
        "time-only chatter=0 px\n",
        closest,
        closest_half,
        closest_quarter);
}

int main(void)
{
    test_metadata_mapping_and_rejection();
    test_full_ir_clamping_guards_and_determinism();
    test_every_ir_byte_is_observable();
    test_expression_and_viseme_separability();
    test_speech_alignment_continuity_and_no_clipping();
    test_distinct_visual_languages_and_motion();
    puts("face_cyber_wildcards_test: ok");
    return 0;
}
