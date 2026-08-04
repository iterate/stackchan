#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_authored_variants.h"
#include "face_stage.h"

static int failures;
static int checks;

#define CHECK(condition, ...)                                             \
    do {                                                                  \
        ++checks;                                                         \
        if (!(condition)) {                                               \
            ++failures;                                                   \
            fprintf(stderr, "FAIL %s:%d: ", __func__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                 \
            fputc('\n', stderr);                                          \
        }                                                                 \
    } while (0)

enum {
    FAV_SAMPLE_RATE = 16000,
    FAV_SAMPLES_PER_FRAME = 533,
    FAV_EXPRESSION_CLOCK = FAV_SAMPLE_RATE * 7 + 211,
    FAV_EXACT_WIDTH = 40,
    FAV_EXACT_HEIGHT = 30,
    FAV_EXACT_PIXELS = FAV_EXACT_WIDTH * FAV_EXACT_HEIGHT,
    FAV_GUARD_PIXELS = 64,
    FAV_FUZZ_CASES = 384,
};

static uint16_t arena[FAV_GUARD_PIXELS + FAV_PIXEL_COUNT + FAV_GUARD_PIXELS];
static uint16_t *const frame = arena + FAV_GUARD_PIXELS;
static uint16_t scratch[FAV_PIXEL_COUNT];
static uint16_t exact_expression[FACE_EXPRESSION_COUNT][FAV_EXACT_PIXELS];

static const uint8_t VISEME_SHAPES[15][5] = {
    {236, 205, 24, 0, 18},  {155, 246, 0, 0, 128},
    {102, 255, 0, 0, 155},  {214, 112, 255, 0, 16},
    {112, 82, 244, 0, 10},  {12, 164, 18, 255, 0},
    {66, 224, 0, 0, 210},   {88, 190, 0, 0, 235},
    {82, 194, 0, 0, 164},   {38, 198, 0, 176, 235},
    {120, 181, 20, 0, 78},  {72, 202, 0, 0, 142},
    {104, 148, 94, 0, 72},  {86, 158, 46, 34, 184},
    {0, 110, 30, 0, 0},
};

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 152U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 76U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U;
    key.phoneme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 1U;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_render_key_t idle_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_width = 128U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.viseme = 14U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 255U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.affect_arousal = 72U;
    key.attention = 192U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(700U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static uint8_t red8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 11U) & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t green8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 5U) & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static uint8_t blue8(uint16_t color)
{
    const uint8_t value = (uint8_t)(color & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint16_t pack565(uint32_t red, uint32_t green, uint32_t blue)
{
    return (uint16_t)(
        (((uint16_t)red & 0xf8U) << 8U) |
        (((uint16_t)green & 0xfcU) << 3U) |
        ((uint16_t)blue >> 3U));
}

static void downsample_exact40(
    const uint16_t *source, uint16_t *destination)
{
    for (int y = 0; y < FAV_EXACT_HEIGHT; ++y) {
        for (int x = 0; x < FAV_EXACT_WIDTH; ++x) {
            uint32_t red = 0U;
            uint32_t green = 0U;
            uint32_t blue = 0U;
            for (int yy = 0; yy < 4; ++yy) {
                for (int xx = 0; xx < 4; ++xx) {
                    const uint16_t pixel =
                        source[(y * 4 + yy) * FAV_FRAME_WIDTH +
                               (x * 4 + xx)];
                    red += red8(pixel);
                    green += green8(pixel);
                    blue += blue8(pixel);
                }
            }
            destination[y * FAV_EXACT_WIDTH + x] =
                pack565((red + 8U) / 16U, (green + 8U) / 16U,
                        (blue + 8U) / 16U);
        }
    }
}

static uint32_t frame_hash(const uint16_t *pixels, size_t count)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < count; ++index) {
        hash ^= pixels[index] & 0xffU;
        hash *= 16777619U;
        hash ^= pixels[index] >> 8U;
        hash *= 16777619U;
    }
    return hash;
}

static int changed_pixels(
    const uint16_t *left, const uint16_t *right, size_t count)
{
    int changed = 0;
    for (size_t index = 0U; index < count; ++index) {
        changed += left[index] != right[index];
    }
    return changed;
}

static void fill_guards(void)
{
    for (int index = 0; index < FAV_GUARD_PIXELS; ++index) {
        arena[index] = 0xa5a5U;
        arena[FAV_GUARD_PIXELS + FAV_PIXEL_COUNT + index] = 0x5a5aU;
    }
}

static bool guards_intact(void)
{
    for (int index = 0; index < FAV_GUARD_PIXELS; ++index) {
        if (arena[index] != 0xa5a5U ||
            arena[FAV_GUARD_PIXELS + FAV_PIXEL_COUNT + index] != 0x5a5aU) {
            return false;
        }
    }
    return true;
}

static void test_api(void)
{
    CHECK(fav_profile_count() == 9U, "nine authored variants");
    CHECK(sizeof(face_render_key_t) == 40U, "40-byte renderer IR");
    CHECK(sizeof(fav_info_t) == 12U, "compact info struct");
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        fav_info_t info;
        CHECK(fav_profile_slug(profile) != NULL, "slug %zu", index);
        CHECK(fav_profile_name(profile) != NULL, "name %zu", index);
        CHECK(fav_profile_info(profile, &info), "info %zu", index);
        CHECK(
            info.width == FAV_FRAME_WIDTH &&
                info.height == FAV_FRAME_HEIGHT &&
                info.framebuffer_bytes == FAV_FRAME_BYTES,
            "frame info %zu", index);
        CHECK(
            (unsigned)fav_profile_lineage(profile) <=
                FAV_LINEAGE_VGA_STAR_CAPTAIN,
            "lineage %zu", index);
    }
    const face_render_key_t key = base_key();
    CHECK(
        !fav_render_frame(
            (fav_profile_t)99, &key, 0U, frame, FAV_PIXEL_COUNT),
        "reject bad profile");
    CHECK(
        !fav_render_frame(
            FAV_PROFILE_BEAN_APPEAL_SCOUT, NULL, 0U,
            frame, FAV_PIXEL_COUNT),
        "reject NULL key");
    CHECK(
        !fav_render_frame(
            FAV_PROFILE_BEAN_APPEAL_SCOUT, &key, 0U,
            NULL, FAV_PIXEL_COUNT),
        "reject NULL frame");
    CHECK(
        !fav_render_frame(
            FAV_PROFILE_BEAN_APPEAL_SCOUT, &key, 0U,
            frame, FAV_PIXEL_COUNT - 1U),
        "reject short frame");
}

static void test_purity_and_guards(void)
{
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        const face_render_key_t key = base_key();
        fill_guards();
        for (int pixel = 0; pixel < FAV_PIXEL_COUNT; ++pixel) {
            frame[pixel] = 0xdeadU;
        }
        CHECK(
            fav_render_frame(
                profile, &key, 77777U, frame, FAV_PIXEL_COUNT),
            "render %s", fav_profile_slug(profile));
        CHECK(guards_intact(), "guards %s", fav_profile_slug(profile));
        const uint32_t first = frame_hash(frame, FAV_PIXEL_COUNT);
        memcpy(scratch, frame, sizeof(scratch));

        face_render_key_t other = idle_key();
        CHECK(
            fav_render_frame(
                (fav_profile_t)((index + 1U) % fav_profile_count()),
                &other, 4321U, frame, FAV_PIXEL_COUNT),
            "interleaved render %zu", index);
        CHECK(
            fav_render_frame(
                profile, &key, 77777U, frame, FAV_PIXEL_COUNT),
            "rerender %s", fav_profile_slug(profile));
        CHECK(
            frame_hash(frame, FAV_PIXEL_COUNT) == first &&
                memcmp(frame, scratch, sizeof(scratch)) == 0,
            "pure %s", fav_profile_slug(profile));
    }
}

static void test_expression_matrix(void)
{
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        int minimum_changed = FAV_EXACT_PIXELS;
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)expression);
            CHECK(
                face_stage_cue_apply(
                    &cue, FAV_EXPRESSION_CLOCK, &key),
                "apply expression %s/%d",
                fav_profile_slug(profile), expression);
            CHECK(
                fav_render_frame(
                    profile, &key, FAV_EXPRESSION_CLOCK,
                    frame, FAV_PIXEL_COUNT),
                "render expression %s/%d",
                fav_profile_slug(profile), expression);
            downsample_exact40(frame, exact_expression[expression]);
            hashes[expression] =
                frame_hash(exact_expression[expression], FAV_EXACT_PIXELS);
        }
        int distinct = 0;
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            bool seen = false;
            for (int previous = 0; previous < expression; ++previous) {
                seen |= hashes[expression] == hashes[previous];
            }
            distinct += !seen;
            if (expression != FACE_EXPRESSION_NEUTRAL) {
                const int delta = changed_pixels(
                    exact_expression[FACE_EXPRESSION_NEUTRAL],
                    exact_expression[expression], FAV_EXACT_PIXELS);
                if (delta < minimum_changed) {
                    minimum_changed = delta;
                }
            }
        }
        CHECK(
            distinct == FACE_EXPRESSION_COUNT,
            "%s exact40 distinct expressions %d/11",
            fav_profile_slug(profile), distinct);
        CHECK(
            minimum_changed >= 24,
            "%s exact40 weakest emotion changes %d/1200 pixels",
            fav_profile_slug(profile), minimum_changed);
        printf(
            "  expression %-29s distinct=%d min_exact40_delta=%d\n",
            fav_profile_slug(profile), distinct, minimum_changed);
    }
}

static void test_viseme_matrix(void)
{
    uint16_t exact[FAV_EXACT_PIXELS];
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        uint32_t native_hashes[15];
        uint32_t exact_hashes[15];
        for (int viseme = 0; viseme < 15; ++viseme) {
            face_render_key_t key = base_key();
            key.viseme = (uint8_t)viseme;
            key.viseme_secondary = 255U;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            key.controls.mouth_open = VISEME_SHAPES[viseme][0];
            key.controls.mouth_width = VISEME_SHAPES[viseme][1];
            key.controls.mouth_round = VISEME_SHAPES[viseme][2];
            key.controls.mouth_press = VISEME_SHAPES[viseme][3];
            key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
            CHECK(
                fav_render_frame(
                    profile, &key,
                    FAV_EXPRESSION_CLOCK + (uint32_t)viseme * 13U,
                    frame, FAV_PIXEL_COUNT),
                "render viseme %s/%d", fav_profile_slug(profile), viseme);
            native_hashes[viseme] = frame_hash(frame, FAV_PIXEL_COUNT);
            downsample_exact40(frame, exact);
            exact_hashes[viseme] = frame_hash(exact, FAV_EXACT_PIXELS);
        }
        int native_distinct = 0;
        int exact_distinct = 0;
        for (int viseme = 0; viseme < 15; ++viseme) {
            bool native_seen = false;
            bool exact_seen = false;
            for (int previous = 0; previous < viseme; ++previous) {
                native_seen |= native_hashes[viseme] == native_hashes[previous];
                exact_seen |= exact_hashes[viseme] == exact_hashes[previous];
            }
            native_distinct += !native_seen;
            exact_distinct += !exact_seen;
        }
        CHECK(
            native_distinct == 15,
            "%s native visemes %d/15", fav_profile_slug(profile),
            native_distinct);
        CHECK(
            exact_distinct >= 13,
            "%s exact40 visemes %d/15", fav_profile_slug(profile),
            exact_distinct);
        printf(
            "  viseme    %-29s native=%d exact40=%d\n",
            fav_profile_slug(profile), native_distinct, exact_distinct);
    }
}

static face_render_key_t temporal_key(int step)
{
    face_render_key_t key = base_key();
    const face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_WARM);
    (void)face_stage_cue_apply(
        &cue, (uint32_t)step * FAV_SAMPLES_PER_FRAME, &key);
    if (step < 4) {
        key = idle_key();
        key.speech_phase = FACE_SPEECH_IDLE;
    } else if (step < 8) {
        key.speech_phase = FACE_SPEECH_STARTING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.audio_level = (uint8_t)(64 + (step - 4) * 36);
        key.controls.mouth_open = (uint8_t)(18 + (step - 4) * 14);
    } else if (step < 18) {
        const int viseme = (step - 8) % 5;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.audio_level = (uint8_t)(118 + ((step * 37) % 110));
        key.viseme = (uint8_t)viseme;
        key.viseme_secondary = (uint8_t)((viseme + 1) % 5);
        key.viseme_blend = (uint8_t)((step * 43) & 0xff);
        key.controls.mouth_open = VISEME_SHAPES[viseme][0];
        key.controls.mouth_width = VISEME_SHAPES[viseme][1];
        key.controls.mouth_round = VISEME_SHAPES[viseme][2];
        key.controls.mouth_press = VISEME_SHAPES[viseme][3];
        key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
        if (step == 12 || step == 13) {
            key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
        }
    } else if (step < 21) {
        key.speech_phase = FACE_SPEECH_ENDING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.controls.mouth_open = (uint8_t)(70 - (step - 18) * 22);
        key.audio_level = (uint8_t)(96 - (step - 18) * 28);
    } else {
        key = idle_key();
    }
    return key;
}

static void test_temporal(void)
{
    uint16_t exact_previous[FAV_EXACT_PIXELS];
    uint16_t exact_current[FAV_EXACT_PIXELS];
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        uint32_t phase_hash[24];
        int max_delta = 0;
        for (int step = 0; step < 24; ++step) {
            const face_render_key_t key = temporal_key(step);
            CHECK(
                fav_render_frame(
                    profile, &key,
                    (uint32_t)step * FAV_SAMPLES_PER_FRAME,
                    frame, FAV_PIXEL_COUNT),
                "temporal render %s/%d", fav_profile_slug(profile), step);
            downsample_exact40(frame, exact_current);
            phase_hash[step] = frame_hash(exact_current, FAV_EXACT_PIXELS);
            if (step > 0) {
                const int delta = changed_pixels(
                    exact_previous, exact_current, FAV_EXACT_PIXELS);
                if (delta > max_delta) {
                    max_delta = delta;
                }
            }
            memcpy(exact_previous, exact_current, sizeof(exact_previous));
        }
        CHECK(
            phase_hash[3] != phase_hash[6],
            "%s speech anticipation visible", fav_profile_slug(profile));
        CHECK(
            phase_hash[6] != phase_hash[10],
            "%s active speech visible", fav_profile_slug(profile));
        CHECK(
            phase_hash[11] != phase_hash[12] &&
                phase_hash[12] != phase_hash[14],
            "%s blink visible", fav_profile_slug(profile));
        CHECK(
            phase_hash[18] != phase_hash[22],
            "%s ending/idle distinct", fav_profile_slug(profile));
        CHECK(
            max_delta < 700,
            "%s temporal maximum exact40 jump %d/1200",
            fav_profile_slug(profile), max_delta);
        printf(
            "  temporal  %-29s max_exact40_delta=%d\n",
            fav_profile_slug(profile), max_delta);
    }
}

static uint32_t fuzz_state = 0xa7193d65U;

static uint8_t fuzz_u8(void)
{
    fuzz_state = fuzz_state * 1664525U + 1013904223U;
    return (uint8_t)(fuzz_state >> 24U);
}

static void test_adversarial(void)
{
    for (size_t index = 0U; index < fav_profile_count(); ++index) {
        const fav_profile_t profile = (fav_profile_t)index;
        for (int fuzz = 0; fuzz < FAV_FUZZ_CASES; ++fuzz) {
            face_render_key_t key;
            uint8_t *bytes = (uint8_t *)&key;
            for (size_t byte = 0U; byte < sizeof(key); ++byte) {
                bytes[byte] = fuzz_u8();
            }
            key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
            fill_guards();
            CHECK(
                fav_render_frame(
                    profile, &key,
                    (uint32_t)fuzz * 7919U + fuzz_state,
                    frame, FAV_PIXEL_COUNT),
                "fuzz render %s/%d", fav_profile_slug(profile), fuzz);
            CHECK(
                guards_intact(),
                "fuzz guards %s/%d", fav_profile_slug(profile), fuzz);
        }
    }
}

static void benchmark(void)
{
    enum { BENCH_FRAMES = 180 };
    const clock_t start = clock();
    uint32_t checksum = 0U;
    for (size_t profile = 0U; profile < fav_profile_count(); ++profile) {
        for (int step = 0; step < BENCH_FRAMES; ++step) {
            const face_render_key_t key = temporal_key(step % 24);
            (void)fav_render_frame(
                (fav_profile_t)profile, &key,
                (uint32_t)step * FAV_SAMPLES_PER_FRAME,
                frame, FAV_PIXEL_COUNT);
            checksum ^= frame[(step * 101 + (int)profile * 37) %
                              FAV_PIXEL_COUNT];
        }
    }
    const clock_t elapsed = clock() - start;
    const double frames =
        (double)(fav_profile_count() * (size_t)BENCH_FRAMES);
    const double microseconds =
        elapsed > 0
            ? (double)elapsed * 1000000.0 / (double)CLOCKS_PER_SEC / frames
            : 0.0;
    printf(
        "face_authored_variants benchmark: %.2f us/frame "
        "(checksum=%08" PRIx32 ")\n",
        microseconds, checksum);
}

int main(void)
{
    test_api();
    test_purity_and_guards();
    test_expression_matrix();
    test_viseme_matrix();
    test_temporal();
    test_adversarial();
    benchmark();
    if (failures != 0) {
        fprintf(
            stderr, "face_authored_variants_test: %d/%d checks failed\n",
            failures, checks);
        return 1;
    }
    printf(
        "face_authored_variants_test: PASS (%d checks)\n", checks);
    return 0;
}

