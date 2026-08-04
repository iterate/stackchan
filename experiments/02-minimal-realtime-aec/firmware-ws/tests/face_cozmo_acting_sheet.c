#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_cozmo_acting.h"
#include "face_render.h"
#include "face_stage.h"

/*
 * Contact-sheet generator for the face_cozmo_acting module.
 *
 * Writes reviewable PPMs into the directory given as argv[1]:
 *   - <slug>-expressions.ppm      4x3 grid of the 11 stage expressions,
 *                                 same fixture as the production QA probe;
 *   - <slug>-motion.ppm           6x2 strip across a blink+dart window at
 *                                 30 fps, half scale;
 *   - chatter-speech.ppm          viseme sweep for the mouth profile;
 *   - cozmo-acting-vs-robot-rig.ppm
 *                                 one row per new profile plus the two
 *                                 existing Cozmo-like rows, half scale,
 *                                 for a direct appeal comparison.
 */

enum {
    SAMPLE_RATE = 16000,
    FPS = 30,
    LABEL_HEIGHT = 10,
    SHEET_COLUMNS = 4,
    SHEET_ROWS = 3,
    MOTION_COLUMNS = 6,
    MOTION_ROWS = 2,
    MOTION_COUNT = MOTION_COLUMNS * MOTION_ROWS,
    PATH_BYTES = 1024,
    FIXED_CLOCK = SAMPLE_RATE * 7 + 211,
};

static uint16_t expression_frames[FACE_EXPRESSION_COUNT]
                                 [FACE_COZMO_ACTING_PIXEL_COUNT];
static uint16_t motion_frames[MOTION_COUNT][FACE_COZMO_ACTING_PIXEL_COUNT];

static uint8_t rgb565_red(uint16_t pixel)
{
    const uint8_t v = (uint8_t)((pixel >> 11U) & 0x1FU);
    return (uint8_t)((v << 3U) | (v >> 2U));
}

static uint8_t rgb565_green(uint16_t pixel)
{
    const uint8_t v = (uint8_t)((pixel >> 5U) & 0x3FU);
    return (uint8_t)((v << 2U) | (v >> 4U));
}

static uint8_t rgb565_blue(uint16_t pixel)
{
    const uint8_t v = (uint8_t)(pixel & 0x1FU);
    return (uint8_t)((v << 3U) | (v >> 2U));
}

static bool write_rgb(FILE *file, uint16_t pixel)
{
    const uint8_t rgb[3] = {
        rgb565_red(pixel), rgb565_green(pixel), rgb565_blue(pixel),
    };
    return fwrite(rgb, sizeof(rgb), 1U, file) == 1U;
}

static bool digit_pixel(uint8_t digit, size_t x, size_t y)
{
    static const uint8_t DIGITS[10][5] = {
        { 0x7U, 0x5U, 0x5U, 0x5U, 0x7U },
        { 0x2U, 0x6U, 0x2U, 0x2U, 0x7U },
        { 0x7U, 0x1U, 0x7U, 0x4U, 0x7U },
        { 0x7U, 0x1U, 0x7U, 0x1U, 0x7U },
        { 0x5U, 0x5U, 0x7U, 0x1U, 0x1U },
        { 0x7U, 0x4U, 0x7U, 0x1U, 0x7U },
        { 0x7U, 0x4U, 0x7U, 0x5U, 0x7U },
        { 0x7U, 0x1U, 0x1U, 0x1U, 0x1U },
        { 0x7U, 0x5U, 0x7U, 0x5U, 0x7U },
        { 0x7U, 0x5U, 0x7U, 0x1U, 0x7U },
    };
    return digit < 10U && x < 3U && y < 5U &&
           (DIGITS[digit][y] & (1U << (2U - x))) != 0U;
}

static uint16_t label_pixel(uint32_t value, size_t x, size_t y)
{
    if (y == LABEL_HEIGHT - 1U) {
        return 0x2104U;
    }
    if (y < 2U || y >= 7U) {
        return 0x0000U;
    }
    const uint8_t tens = (uint8_t)((value / 10U) % 10U);
    const uint8_t ones = (uint8_t)(value % 10U);
    const bool lit =
        (x >= 3U && x < 6U && digit_pixel(tens, x - 3U, y - 2U)) ||
        (x >= 8U && x < 11U && digit_pixel(ones, x - 8U, y - 2U));
    return lit ? 0xFFFFU : 0x0000U;
}

static face_render_key_t base_render_key(void)
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
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_sample = 0U;
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static bool render_expression_set(face_cozmo_acting_profile_t profile)
{
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        face_render_key_t key = base_render_key();
        const face_stage_cue_t cue =
            expression_cue((face_expression_t)expression);
        if (!face_stage_cue_apply(&cue, FIXED_CLOCK, &key) ||
            !face_cozmo_acting_render(
                profile, &key, FIXED_CLOCK,
                expression_frames[expression],
                FACE_COZMO_ACTING_PIXEL_COUNT)) {
            return false;
        }
    }
    return true;
}

static bool write_ppm_header(FILE *file, size_t width, size_t height)
{
    return fprintf(file, "P6\n%zu %zu\n255\n", width, height) > 0;
}

static bool write_expression_sheet(
    const char *directory, face_cozmo_acting_profile_t profile)
{
    char path[PATH_BYTES];
    const int n = snprintf(
        path, sizeof(path), "%s/%s-expressions.ppm", directory,
        face_cozmo_acting_profile_slug(profile));
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    const size_t sheet_w = SHEET_COLUMNS * FACE_COZMO_ACTING_WIDTH;
    const size_t cell_h = FACE_COZMO_ACTING_HEIGHT + LABEL_HEIGHT;
    bool ok = write_ppm_header(file, sheet_w, SHEET_ROWS * cell_h);
    for (size_t row = 0U; ok && row < SHEET_ROWS; ++row) {
        for (size_t y = 0U; ok && y < cell_h; ++y) {
            for (size_t column = 0U;
                 ok && column < SHEET_COLUMNS;
                 ++column) {
                const size_t expression = row * SHEET_COLUMNS + column;
                for (size_t x = 0U;
                     ok && x < FACE_COZMO_ACTING_WIDTH;
                     ++x) {
                    uint16_t pixel = 0x0000U;
                    if (expression < FACE_EXPRESSION_COUNT) {
                        pixel = y < LABEL_HEIGHT
                            ? label_pixel((uint32_t)expression, x, y)
                            : expression_frames[expression]
                                  [(y - LABEL_HEIGHT) *
                                       FACE_COZMO_ACTING_WIDTH + x];
                    }
                    ok = write_rgb(file, pixel);
                }
            }
        }
    }
    return fclose(file) == 0 && ok;
}

/*
 * Motion strip: twelve consecutive 30 fps frames straddling a window that
 * contains autonomous motion, with speech articulation waves running.
 */
static face_render_key_t motion_key(uint32_t frame)
{
    face_render_key_t key = base_render_key();
    const uint32_t jaw = (frame * 21U + 7U) % 255U;
    key.controls.mouth_open = (uint8_t)(24U + (jaw * 204U) / 255U);
    key.audio_level = (uint8_t)(18U + (jaw * 202U) / 255U);
    key.stage_expression = FACE_EXPRESSION_WARM;
    key.expression_weight = 210U;
    return key;
}

static bool write_motion_sheet(
    const char *directory,
    face_cozmo_acting_profile_t profile,
    uint32_t first_frame)
{
    for (uint32_t index = 0U; index < MOTION_COUNT; ++index) {
        const uint32_t frame = first_frame + index;
        const uint32_t clock =
            (uint32_t)(((uint64_t)frame * SAMPLE_RATE) / FPS);
        const face_render_key_t key = motion_key(frame);
        if (!face_cozmo_acting_render(
                profile, &key, clock, motion_frames[index],
                FACE_COZMO_ACTING_PIXEL_COUNT)) {
            return false;
        }
    }
    char path[PATH_BYTES];
    const int n = snprintf(
        path, sizeof(path), "%s/%s-motion.ppm", directory,
        face_cozmo_acting_profile_slug(profile));
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    const size_t half_w = FACE_COZMO_ACTING_WIDTH / 2U;
    const size_t half_h = FACE_COZMO_ACTING_HEIGHT / 2U;
    const size_t cell_h = half_h + LABEL_HEIGHT;
    bool ok = write_ppm_header(
        file, MOTION_COLUMNS * half_w, MOTION_ROWS * cell_h);
    for (size_t row = 0U; ok && row < MOTION_ROWS; ++row) {
        for (size_t y = 0U; ok && y < cell_h; ++y) {
            for (size_t column = 0U;
                 ok && column < MOTION_COLUMNS;
                 ++column) {
                const size_t index = row * MOTION_COLUMNS + column;
                for (size_t x = 0U; ok && x < half_w; ++x) {
                    const uint16_t pixel = y < LABEL_HEIGHT
                        ? label_pixel(
                              first_frame + (uint32_t)index, x, y)
                        : motion_frames[index]
                              [(y - LABEL_HEIGHT) * 2U *
                                   FACE_COZMO_ACTING_WIDTH + x * 2U];
                    ok = write_rgb(file, pixel);
                }
            }
        }
    }
    return fclose(file) == 0 && ok;
}

/* Viseme sweep for the mouth profile: primary shapes at full weight. */
static bool write_speech_sheet(const char *directory)
{
    static const uint8_t VISEMES[12] = {
        FACE_VISEME_SIL, FACE_VISEME_AA, FACE_VISEME_E, FACE_VISEME_I,
        FACE_VISEME_O, FACE_VISEME_U, FACE_VISEME_PP, FACE_VISEME_SS,
        FACE_VISEME_TH, FACE_VISEME_FF, FACE_VISEME_RR, FACE_VISEME_CH,
    };
    for (uint32_t index = 0U; index < 12U; ++index) {
        face_render_key_t key = base_render_key();
        key.viseme = VISEMES[index];
        key.viseme_secondary = VISEMES[index];
        key.viseme_blend = 0U;
        key.viseme_weight = 246U;
        key.audio_level = 178U;
        key.controls.mouth_open = 150U;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
        key.expression_weight = 0U;
        if (!face_cozmo_acting_render(
                FACE_COZMO_ACTING_CHATTER, &key, FIXED_CLOCK,
                motion_frames[index % MOTION_COUNT],
                FACE_COZMO_ACTING_PIXEL_COUNT)) {
            return false;
        }
        memcpy(
            expression_frames[index % FACE_EXPRESSION_COUNT],
            motion_frames[index % MOTION_COUNT],
            sizeof(motion_frames[0]));
    }
    char path[PATH_BYTES];
    const int n = snprintf(
        path, sizeof(path), "%s/chatter-speech.ppm", directory);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    /* 6 x 2 grid of full-size frames, labeled by sweep index. */
    const size_t cell_h = FACE_COZMO_ACTING_HEIGHT + LABEL_HEIGHT;
    bool ok = write_ppm_header(
        file, 6U * FACE_COZMO_ACTING_WIDTH, 2U * cell_h);
    for (size_t row = 0U; ok && row < 2U; ++row) {
        for (size_t y = 0U; ok && y < cell_h; ++y) {
            for (size_t column = 0U; ok && column < 6U; ++column) {
                const size_t index = row * 6U + column;
                /* Re-render each cell: static buffers cannot hold all
                 * twelve full frames at once alongside expressions. */
                face_render_key_t key = base_render_key();
                key.viseme = VISEMES[index];
                key.viseme_secondary = VISEMES[index];
                key.viseme_blend = 0U;
                key.viseme_weight = 246U;
                key.audio_level = 178U;
                key.controls.mouth_open = 150U;
                key.stage_expression = FACE_EXPRESSION_NEUTRAL;
                key.expression_weight = 0U;
                if (y == 0U) {
                    if (!face_cozmo_acting_render(
                            FACE_COZMO_ACTING_CHATTER, &key,
                            FIXED_CLOCK, motion_frames[index],
                            FACE_COZMO_ACTING_PIXEL_COUNT)) {
                        ok = false;
                        break;
                    }
                }
                for (size_t x = 0U;
                     ok && x < FACE_COZMO_ACTING_WIDTH;
                     ++x) {
                    const uint16_t pixel = y < LABEL_HEIGHT
                        ? label_pixel((uint32_t)index, x, y)
                        : motion_frames[index]
                              [(y - LABEL_HEIGHT) *
                                   FACE_COZMO_ACTING_WIDTH + x];
                    ok = write_rgb(file, pixel);
                }
            }
        }
    }
    return fclose(file) == 0 && ok;
}

/*
 * The appeal comparison: one half-scale row of all 11 expressions per new
 * profile, then the two existing Cozmo-like production rows rendered
 * through face_render_frame with the identical fixture.
 */
static bool write_comparison_atlas(const char *directory)
{
    static const face_render_profile_t EXISTING[2] = {
        FACE_RENDER_ROBOT_RIG_COZMO_CUBIC,
        FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED,
    };
    char path[PATH_BYTES];
    const int n = snprintf(
        path, sizeof(path), "%s/cozmo-acting-vs-robot-rig.ppm",
        directory);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    const size_t row_count = FACE_COZMO_ACTING_PROFILE_COUNT + 2U;
    const size_t half_w = FACE_COZMO_ACTING_WIDTH / 2U;
    const size_t half_h = FACE_COZMO_ACTING_HEIGHT / 2U;
    bool ok = write_ppm_header(
        file, FACE_EXPRESSION_COUNT * half_w, row_count * half_h);

    for (size_t row = 0U; ok && row < row_count; ++row) {
        /* Render this row's 11 expressions into the static buffer. */
        for (size_t expression = 0U;
             ok && expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)expression);
            ok = face_stage_cue_apply(&cue, FIXED_CLOCK, &key);
            if (!ok) {
                break;
            }
            if (row < FACE_COZMO_ACTING_PROFILE_COUNT) {
                ok = face_cozmo_acting_render(
                    (face_cozmo_acting_profile_t)row, &key,
                    FIXED_CLOCK, expression_frames[expression],
                    FACE_COZMO_ACTING_PIXEL_COUNT);
            } else {
                ok = face_render_frame(
                    EXISTING[row - FACE_COZMO_ACTING_PROFILE_COUNT],
                    &key, FIXED_CLOCK,
                    expression_frames[expression],
                    FACE_RENDER_PIXEL_COUNT);
            }
        }
        for (size_t y = 0U; ok && y < half_h; ++y) {
            for (size_t expression = 0U;
                 ok && expression < FACE_EXPRESSION_COUNT;
                 ++expression) {
                for (size_t x = 0U; ok && x < half_w; ++x) {
                    ok = write_rgb(
                        file,
                        expression_frames[expression]
                            [y * 2U * FACE_COZMO_ACTING_WIDTH +
                             x * 2U]);
                }
            }
        }
    }
    return fclose(file) == 0 && ok;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    const char *directory = argv[1];
    for (size_t raw = 0U;
         raw < face_cozmo_acting_profile_count();
         ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        if (!render_expression_set(profile) ||
            !write_expression_sheet(directory, profile)) {
            fprintf(
                stderr, "expression sheet failed for %s\n",
                face_cozmo_acting_profile_slug(profile));
            return 1;
        }
        /* Straddle a spontaneous blink: frame 30*4=120 -> t=4.0 s. */
        if (!write_motion_sheet(directory, profile, 118U)) {
            fprintf(
                stderr, "motion sheet failed for %s\n",
                face_cozmo_acting_profile_slug(profile));
            return 1;
        }
    }
    if (!write_speech_sheet(directory)) {
        fprintf(stderr, "speech sheet failed\n");
        return 1;
    }
    if (!write_comparison_atlas(directory)) {
        fprintf(stderr, "comparison atlas failed\n");
        return 1;
    }
    printf("face_cozmo_acting_sheet: wrote sheets to %s\n", directory);
    return 0;
}
