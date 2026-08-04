#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "face_pose.h"
#include "face_sprite_actors.h"
#include "face_stage.h"

enum {
    GUARD_PIXELS = 16,
    EXPRESSION_COLUMNS = FACE_EXPRESSION_COUNT,
    VISEME_COLUMNS = FACE_VISEME_COUNT,
    MOTION_COLUMNS = 12,
};

static uint32_t hash_pixels(const uint16_t *pixels, size_t count)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < count; ++index) {
        hash ^= pixels[index] & 0xffU;
        hash *= 16777619U;
        hash ^= pixels[index] >> 8U;
        hash *= 16777619U;
    }
    return hash;
}

static size_t pixel_difference(
    const uint16_t *left, const uint16_t *right, size_t count)
{
    size_t different = 0U;
    for (size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            ++different;
        }
    }
    return different;
}

static size_t region_difference(
    const uint16_t *left,
    const uint16_t *right,
    size_t x0,
    size_t y0,
    size_t x1,
    size_t y1)
{
    size_t different = 0U;
    for (size_t y = y0; y < y1; ++y) {
        for (size_t x = x0; x < x1; ++x) {
            const size_t index = y * FSA_WIDTH + x;
            different += left[index] != right[index];
        }
    }
    return different;
}

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 150U;
    key.controls.mouth_width = 154U;
    key.controls.eye_left_open = 255U;
    key.controls.eye_right_open = 255U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 255U;
    key.audio_level = 150U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.expression_weight = 255U;
    key.attention = 255U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static void ppm_write_header(
    FILE *file, size_t width, size_t height)
{
    fprintf(file, "P6\n%zu %zu\n255\n", width, height);
}

static void ppm_write_rgb565(FILE *file, uint16_t value)
{
    const uint8_t r5 = (uint8_t)((value >> 11U) & 31U);
    const uint8_t g6 = (uint8_t)((value >> 5U) & 63U);
    const uint8_t b5 = (uint8_t)(value & 31U);
    const uint8_t rgb[3] = {
        (uint8_t)((r5 * 255U + 15U) / 31U),
        (uint8_t)((g6 * 255U + 31U) / 63U),
        (uint8_t)((b5 * 255U + 15U) / 31U),
    };
    assert(fwrite(rgb, sizeof(rgb), 1U, file) == 1U);
}

static void write_grid(
    const char *path,
    const uint16_t *frames,
    size_t rows,
    size_t columns)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    ppm_write_header(
        file, columns * FSA_WIDTH, rows * FSA_HEIGHT);
    for (size_t row = 0; row < rows; ++row) {
        for (size_t y = 0; y < FSA_HEIGHT; ++y) {
            for (size_t column = 0; column < columns; ++column) {
                const uint16_t *frame =
                    &frames[(row * columns + column) * FSA_PIXEL_COUNT];
                for (size_t x = 0; x < FSA_WIDTH; ++x) {
                    ppm_write_rgb565(file, frame[y * FSA_WIDTH + x]);
                }
            }
        }
    }
    assert(fclose(file) == 0);
}

static void test_metadata(void)
{
    assert(fsa_profile_count() == FSA_PROFILE_COUNT);
    assert(strcmp(fsa_profile_slug(FSA_PROFILE_EGA_COURT_MAGE),
                  "sprite-ega-court-mage") == 0);
    assert(strcmp(fsa_profile_slug((fsa_profile_t)99),
                  "invalid-sprite-actor") == 0);
    for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
        fsa_info_t info;
        assert(fsa_profile_info((fsa_profile_t)profile, &info));
        assert(info.width == FSA_WIDTH);
        assert(info.height == FSA_HEIGHT);
        assert(info.work_width == 80U);
        assert(info.work_height == 60U);
    }
    assert(!fsa_profile_info((fsa_profile_t)99, NULL));
}

static void test_external_descriptor(void)
{
    static const uint16_t palette[] = { 0x0000U, 0xffffU, 0xf800U };
    static const uint8_t atlas[64] = {
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 2, 2, 2, 2, 2, 2, 1,
        1, 2, 1, 2, 2, 1, 2, 1,
        1, 2, 2, 2, 2, 2, 2, 1,
        1, 2, 2, 1, 1, 2, 2, 1,
        1, 2, 1, 2, 2, 1, 2, 1,
        1, 2, 2, 2, 2, 2, 2, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
    };
    static const fsa_cell_t cells[] = {
        { 0, 0, 8, 8, 0, 0 },
    };
    static const fsa_pose_t poses[] = {
        {
            .base = 0,
            .overlay = FSA_CELL_NONE,
            .mouth_bank = 0,
            .eye_left = {
                FSA_CELL_NONE, FSA_CELL_NONE,
                FSA_CELL_NONE, FSA_CELL_NONE,
            },
            .eye_right = {
                FSA_CELL_NONE, FSA_CELL_NONE,
                FSA_CELL_NONE, FSA_CELL_NONE,
            },
            .brow_left = FSA_CELL_NONE,
            .brow_right = FSA_CELL_NONE,
            .pupil_left = FSA_CELL_NONE,
            .pupil_right = FSA_CELL_NONE,
            .blink_frame_count = 1,
        },
    };
    static const uint16_t mouths[] = { FSA_CELL_NONE };
    static const uint8_t expression_pose[FACE_EXPRESSION_COUNT] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    static const uint8_t fallback[FACE_VISEME_COUNT] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    const fsa_sheet_t sheet = {
        .magic = FSA_SHEET_MAGIC,
        .version = FSA_SHEET_VERSION,
        .native_width = 8,
        .native_height = 8,
        .scale = 2,
        .transparent_index = 0,
        .palette_count = 3,
        .atlas_width = 8,
        .atlas_height = 8,
        .cell_count = 1,
        .pose_count = 1,
        .mouth_bank_count = 1,
        .mouth_frames = 1,
        .background_rgb565 = 0,
        .palette_rgb565 = palette,
        .atlas_pixels = atlas,
        .cells = cells,
        .poses = poses,
        .mouth_cells = mouths,
        .expression_pose = expression_pose,
        .fallback_mouth = fallback,
        .timing = {
            800, 640, 1600, 2400, 640, 480, 960,
            FSA_TRANSITION_HOLD_CUT, 192, 56000,
        },
        .name = "external descriptor canary",
    };
    assert(fsa_validate_sheet(&sheet));
    uint16_t frame[FSA_PIXEL_COUNT];
    face_render_key_t key = base_key();
    fsa_resolved_t resolved;
    assert(fsa_resolve(&sheet, &key, 1000U, &resolved));
    assert(fsa_render_sheet_frame(
        &sheet, &key, 1000U, frame, FSA_PIXEL_COUNT));
    fsa_sheet_t broken = sheet;
    broken.scale = 40U;
    assert(!fsa_validate_sheet(&broken));
    broken = sheet;
    broken.expression_pose = NULL;
    assert(!fsa_validate_sheet(&broken));
}

static void test_canary_purity_and_expressions(void)
{
    uint16_t guarded[FSA_PIXEL_COUNT + GUARD_PIXELS * 2];
    uint16_t second[FSA_PIXEL_COUNT];
    uint16_t neutral[FSA_PIXEL_COUNT];
    for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (size_t expression = 0;
             expression < FACE_EXPRESSION_COUNT; ++expression) {
            for (size_t index = 0;
                 index < FSA_PIXEL_COUNT + GUARD_PIXELS * 2; ++index) {
                guarded[index] = 0xa55aU;
            }
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            uint16_t *frame = &guarded[GUARD_PIXELS];
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key, 12000U,
                frame, FSA_PIXEL_COUNT));
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key, 12000U,
                second, FSA_PIXEL_COUNT));
            assert(memcmp(
                frame, second, sizeof(second)) == 0);
            for (size_t index = 0; index < GUARD_PIXELS; ++index) {
                assert(guarded[index] == 0xa55aU);
                assert(guarded[
                    GUARD_PIXELS + FSA_PIXEL_COUNT + index] == 0xa55aU);
            }
            hashes[expression] =
                hash_pixels(frame, FSA_PIXEL_COUNT);
            if (expression == FACE_EXPRESSION_NEUTRAL) {
                memcpy(neutral, frame, sizeof(neutral));
            } else {
                const size_t changed = pixel_difference(
                    neutral, frame, FSA_PIXEL_COUNT);
                if (changed <= 150U) {
                    fprintf(
                        stderr,
                        "weak expression: profile=%zu expression=%zu "
                        "changed=%zu\n",
                        profile, expression, changed);
                }
                assert(changed > 150U);
            }
        }
        for (size_t left = 0; left < FACE_EXPRESSION_COUNT; ++left) {
            for (size_t right = left + 1U;
                 right < FACE_EXPRESSION_COUNT; ++right) {
                assert(hashes[left] != hashes[right]);
            }
        }
    }
}

static uint32_t fuzz_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void test_fuzz(void)
{
    uint16_t guarded[FSA_PIXEL_COUNT + GUARD_PIXELS * 2];
    uint32_t state = 0x51a7e123U;
    for (size_t iteration = 0; iteration < 3000U; ++iteration) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t index = 0; index < sizeof(key); ++index) {
            bytes[index] = (uint8_t)fuzz_next(&state);
        }
        for (size_t index = 0;
             index < FSA_PIXEL_COUNT + GUARD_PIXELS * 2; ++index) {
            guarded[index] = 0x5aa5U;
        }
        const fsa_profile_t profile =
            (fsa_profile_t)(fuzz_next(&state) % FSA_PROFILE_COUNT);
        assert(fsa_render_frame(
            profile, &key, fuzz_next(&state),
            &guarded[GUARD_PIXELS], FSA_PIXEL_COUNT));
        for (size_t index = 0; index < GUARD_PIXELS; ++index) {
            assert(guarded[index] == 0x5aa5U);
            assert(guarded[
                GUARD_PIXELS + FSA_PIXEL_COUNT + index] == 0x5aa5U);
        }
    }
}

static void test_temporal_acting(void)
{
    uint16_t rest_a[FSA_PIXEL_COUNT];
    uint16_t rest_b[FSA_PIXEL_COUNT];
    uint16_t anticipate[FSA_PIXEL_COUNT];
    uint16_t active[FSA_PIXEL_COUNT];
    uint16_t blink[FSA_PIXEL_COUNT];
    uint16_t settle[FSA_PIXEL_COUNT];
    for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
        face_render_key_t key = base_key();
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        key.controls.flags = 0U;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.viseme_weight = 0U;
        key.audio_level = 0U;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 0U,
            rest_a, FSA_PIXEL_COUNT));
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 100U,
            rest_b, FSA_PIXEL_COUNT));
        /* No 1px clock chatter inside the authored hold. */
        assert(memcmp(rest_a, rest_b, sizeof(rest_a)) == 0);

        key.speech_phase = FACE_SPEECH_STARTING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 100U,
            anticipate, FSA_PIXEL_COUNT));
        const size_t anticipation_delta = pixel_difference(
            rest_a, anticipate, FSA_PIXEL_COUNT);
        if (anticipation_delta <= 80U) {
            fprintf(
                stderr,
                "weak anticipation: profile=%zu changed=%zu\n",
                profile,
                anticipation_delta);
        }
        assert(anticipation_delta > 80U);

        key.stage_expression = FACE_EXPRESSION_EXCITED;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.viseme_weight = 255U;
        key.viseme = FACE_VISEME_E;
        key.audio_level = 176U;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 3200U,
            active, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            anticipate, active, FSA_PIXEL_COUNT) > 180U);

        key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 3200U,
            blink, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            active, blink, FSA_PIXEL_COUNT) > 100U);

        key.controls.flags = 0U;
        key.stage_expression = FACE_EXPRESSION_WARM;
        key.speech_phase = FACE_SPEECH_ENDING;
        key.viseme_weight = 0U;
        key.audio_level = 0U;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 4800U,
            settle, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            active, settle, FSA_PIXEL_COUNT) > 160U);
    }
}

static void test_mid_speech_stage_mouth_coupling(void)
{
    uint16_t neutral[FSA_PIXEL_COUNT];
    uint16_t expressive[FSA_PIXEL_COUNT];
    for (size_t profile = FSA_PROFILE_EGA_COURT_MAGE;
         profile <= FSA_PROFILE_VGA_STAR_CAPTAIN;
         ++profile) {
        face_render_key_t key = base_key();
        key.viseme = FACE_VISEME_DD;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        assert(fsa_render_frame(
            (fsa_profile_t)profile,
            &key,
            12000U,
            neutral,
            FSA_PIXEL_COUNT));
        key.stage_expression = FACE_EXPRESSION_SURPRISE;
        assert(fsa_render_frame(
            (fsa_profile_t)profile,
            &key,
            12000U,
            expressive,
            FSA_PIXEL_COUNT));
        /*
         * At native 160x120, this box contains the attached mouth but not
         * the brows or expression icons.  Stage direction must therefore
         * remain visible even while the DD viseme owns articulation.
         */
        assert(region_difference(
            neutral, expressive, 54U, 76U, 106U, 108U) > 24U);
    }
}

static void test_visemes_and_action_channels(void)
{
    uint16_t baseline[FSA_PIXEL_COUNT];
    uint16_t changed[FSA_PIXEL_COUNT];
    for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
        uint32_t viseme_hashes[FACE_VISEME_COUNT];
        face_render_key_t key = base_key();
        key.stage_expression = FACE_EXPRESSION_WARM;
        for (size_t viseme = 0; viseme < FACE_VISEME_COUNT; ++viseme) {
            key.viseme = (uint8_t)viseme;
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key, 18000U,
                changed, FSA_PIXEL_COUNT));
            viseme_hashes[viseme] =
                hash_pixels(changed, FSA_PIXEL_COUNT);
        }
        for (size_t left = 0; left < FACE_VISEME_COUNT; ++left) {
            for (size_t right = left + 1U;
                 right < FACE_VISEME_COUNT; ++right) {
                if (viseme_hashes[left] == viseme_hashes[right]) {
                    fprintf(
                        stderr,
                        "duplicate viseme: profile=%zu left=%zu right=%zu\n",
                        profile, left, right);
                }
                assert(viseme_hashes[left] != viseme_hashes[right]);
            }
        }

        key = base_key();
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 18000U,
            baseline, FSA_PIXEL_COUNT));

        key.controls.look_x = 110;
        key.controls.look_y = -80;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 18000U,
            changed, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            baseline, changed, FSA_PIXEL_COUNT) > 20U);

        key = base_key();
        key.head_yaw = 110;
        key.head_pitch = -90;
        key.head_roll = 100;
        key.body_lean_x = 80;
        key.body_lean_y = -60;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 18000U,
            changed, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            baseline, changed, FSA_PIXEL_COUNT) > 500U);

        key = base_key();
        key.brow_inner = 110;
        key.brow_outer_left = -100;
        key.brow_outer_right = 100;
        key.eye_left_squint = 210U;
        key.eye_right_squint = 80U;
        key.cheek = 220U;
        key.mouth_corner_left = 110;
        key.mouth_corner_right = -110;
        key.tongue = 240U;
        assert(fsa_render_frame(
            (fsa_profile_t)profile, &key, 18000U,
            changed, FSA_PIXEL_COUNT));
        assert(pixel_difference(
            baseline, changed, FSA_PIXEL_COUNT) > 100U);
    }
}

static void make_previews(const char *directory)
{
    const size_t expression_frames =
        FSA_PROFILE_COUNT * EXPRESSION_COLUMNS;
    const size_t viseme_frames =
        FSA_PROFILE_COUNT * VISEME_COLUMNS;
    const size_t motion_frames =
        FSA_PROFILE_COUNT * MOTION_COLUMNS;
    uint16_t *expressions = calloc(
        expression_frames * FSA_PIXEL_COUNT, sizeof(uint16_t));
    uint16_t *visemes = calloc(
        viseme_frames * FSA_PIXEL_COUNT, sizeof(uint16_t));
    uint16_t *motion = calloc(
        motion_frames * FSA_PIXEL_COUNT, sizeof(uint16_t));
    assert(expressions != NULL && visemes != NULL && motion != NULL);

    for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
        for (size_t expression = 0;
             expression < EXPRESSION_COLUMNS; ++expression) {
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            key.viseme = FACE_VISEME_DD;
            key.audio_level = 96U;
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key, 12000U,
                &expressions[
                    (profile * EXPRESSION_COLUMNS + expression) *
                    FSA_PIXEL_COUNT],
                FSA_PIXEL_COUNT));
        }
        for (size_t viseme = 0; viseme < VISEME_COLUMNS; ++viseme) {
            face_render_key_t key = base_key();
            key.stage_expression = FACE_EXPRESSION_WARM;
            key.viseme = (uint8_t)viseme;
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key, 18000U,
                &visemes[
                    (profile * VISEME_COLUMNS + viseme) *
                    FSA_PIXEL_COUNT],
                FSA_PIXEL_COUNT));
        }
        for (size_t frame = 0; frame < MOTION_COLUMNS; ++frame) {
            face_render_key_t key = base_key();
            key.stage_expression =
                frame < 4U ? FACE_EXPRESSION_NEUTRAL
                : frame < 9U ? FACE_EXPRESSION_EXCITED
                : FACE_EXPRESSION_WARM;
            key.speech_phase =
                frame < 2U ? FACE_SPEECH_IDLE
                : frame < 4U ? FACE_SPEECH_STARTING
                : frame < 9U ? FACE_SPEECH_ACTIVE
                : FACE_SPEECH_ENDING;
            key.controls.flags = frame >= 2U && frame < 9U
                ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
            key.viseme = (uint8_t)(
                frame >= 4U && frame < 9U
                    ? (frame - 4U) % 5U : FACE_VISEME_SIL);
            key.viseme_weight = frame >= 4U && frame < 9U
                ? 255U : 0U;
            key.audio_level = (uint8_t)(
                frame >= 4U && frame < 9U
                    ? 96U + (frame & 1U) * 100U : 0U);
            if (frame == 7U) {
                key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
            }
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key,
                (uint32_t)(frame * 1600U),
                &motion[
                    (profile * MOTION_COLUMNS + frame) *
                    FSA_PIXEL_COUNT],
                FSA_PIXEL_COUNT));
        }
    }

    char path[1024];
    snprintf(
        path, sizeof(path),
        "%s/sprite-actors__6-languages__11-stage-emotions__mid-speech.ppm",
        directory);
    write_grid(
        path, expressions, FSA_PROFILE_COUNT, EXPRESSION_COLUMNS);
    snprintf(
        path, sizeof(path),
        "%s/sprite-actors__6-languages__15-ovr-visemes__warm-active.ppm",
        directory);
    write_grid(
        path, visemes, FSA_PROFILE_COUNT, VISEME_COLUMNS);
    snprintf(
        path, sizeof(path),
        "%s/sprite-actors__6-languages__speech-start-blink-settle__motion.ppm",
        directory);
    write_grid(
        path, motion, FSA_PROFILE_COUNT, MOTION_COLUMNS);
    free(expressions);
    free(visemes);
    free(motion);
}

static void benchmark(void)
{
    uint16_t frame[FSA_PIXEL_COUNT];
    face_render_key_t key = base_key();
    volatile uint32_t checksum = 0U;
    const size_t loops = 300U;
    const clock_t start = clock();
    for (size_t loop = 0; loop < loops; ++loop) {
        for (size_t profile = 0; profile < FSA_PROFILE_COUNT; ++profile) {
            key.stage_expression =
                (uint8_t)((loop + profile) % FACE_EXPRESSION_COUNT);
            key.viseme =
                (uint8_t)((loop + profile) % FACE_VISEME_COUNT);
            assert(fsa_render_frame(
                (fsa_profile_t)profile, &key,
                (uint32_t)(loop * 533U), frame, FSA_PIXEL_COUNT));
            checksum ^= hash_pixels(frame, FSA_PIXEL_COUNT);
        }
    }
    const double elapsed =
        (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    const double frames = (double)(loops * FSA_PROFILE_COUNT);
    printf(
        "face_sprite_actors benchmark: %.2f us/frame, "
        "%.1f full-pack sweeps/s (checksum=%" PRIu32 ")\n",
        elapsed * 1000000.0 / frames,
        (double)loops / elapsed,
        (uint32_t)checksum);
}

int main(int argc, char **argv)
{
    test_metadata();
    test_external_descriptor();
    test_canary_purity_and_expressions();
    test_fuzz();
    test_temporal_acting();
    test_mid_speech_stage_mouth_coupling();
    test_visemes_and_action_channels();
    benchmark();
    if (argc > 1) {
        make_previews(argv[1]);
    }
    puts("face_sprite_actors_test: PASS");
    return 0;
}
