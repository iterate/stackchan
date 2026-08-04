#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_eye_actors.h"
#include "face_pose.h"
#include "face_stage.h"

static const char *const EXPRESSION_NAMES[FACE_EXPRESSION_COUNT] = {
    "neutral", "warm", "joy", "concern", "surprise", "thoughtful",
    "skeptical", "determined", "sleepy", "excited", "embarrassed",
};

static const uint8_t SPEECH_SEQUENCE[12] = {
    FACE_VISEME_PP, FACE_VISEME_AA, FACE_VISEME_E,
    FACE_VISEME_I, FACE_VISEME_O, FACE_VISEME_U,
    FACE_VISEME_SS, FACE_VISEME_TH, FACE_VISEME_FF,
    FACE_VISEME_KK, FACE_VISEME_CH, FACE_VISEME_SIL,
};

static const face_eye_actor_style_t ANKI_STYLES[4] = {
    FACE_EYE_ACTOR_VECTOR_FELT,
    FACE_EYE_ACTOR_COZMO_TILES,
    FACE_EYE_ACTOR_VECTOR_STAGE,
    FACE_EYE_ACTOR_COZMO_CONSOLE,
};

/*
 * Brute-force selected outside the autonomous blink window for
 * every actor. Contact sheets should compare authored poses, not blink phases.
 */
enum {
    PREVIEW_CLOCK = 1921088U,
};

static face_render_key_t preview_key(uint8_t expression, bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 132U : 32U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 44U;
    key.controls.mouth_press = 13U;
    key.controls.mouth_teeth = 116U;
    key.controls.eye_left_open = 234U;
    key.controls.eye_right_open = 238U;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.viseme = speaking ? FACE_VISEME_AA : FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = speaking ? 230U : 72U;
    key.audio_level = speaking ? 142U : 18U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.speech_phase =
        speaking ? FACE_SPEECH_ACTIVE : FACE_SPEECH_IDLE;
    key.tongue = speaking ? 96U : 0U;
    key.cheek = 18U;
    key.affect_arousal = 137U;
    key.expression_weight = 255U;
    key.attention = 214U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static uint8_t expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int write_ppm(
    const char *path,
    const uint16_t *pixels,
    size_t width,
    size_t height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "fopen %s: %s\n", path, strerror(errno));
        return 1;
    }
    fprintf(file, "P6\n%zu %zu\n255\n", width, height);
    for (size_t index = 0U; index < width * height; ++index) {
        const uint16_t color = pixels[index];
        const uint8_t rgb[3] = {
            expand5((uint16_t)((color >> 11U) & 31U)),
            expand6((uint16_t)((color >> 5U) & 63U)),
            expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            fprintf(stderr, "write %s failed\n", path);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t cell_x,
    size_t cell_y,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < FACE_EYE_ACTOR_HEIGHT; ++y) {
        memcpy(
            sheet +
                (cell_y * FACE_EYE_ACTOR_HEIGHT + y) * sheet_width +
                cell_x * FACE_EYE_ACTOR_WIDTH,
            frame + y * FACE_EYE_ACTOR_WIDTH,
            FACE_EYE_ACTOR_WIDTH * sizeof(uint16_t));
    }
}

static int dump_expression_sheet(const char *directory)
{
    const size_t width = FACE_EXPRESSION_COUNT * FACE_EYE_ACTOR_WIDTH;
    const size_t height = FACE_EYE_ACTOR_COUNT * FACE_EYE_ACTOR_HEIGHT;
    const size_t anki_height = 4U * FACE_EYE_ACTOR_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    uint16_t *anki_sheet =
        (uint16_t *)calloc(width * anki_height, sizeof(uint16_t));
    if (sheet == NULL || anki_sheet == NULL) {
        free(sheet);
        free(anki_sheet);
        return 1;
    }
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_EYE_ACTOR_PIXEL_COUNT];
            const face_render_key_t key = preview_key(expression, false);
            if (!face_eye_actor_render(
                    (face_eye_actor_style_t)style,
                    &key,
                    PREVIEW_CLOCK,
                    frame,
                    FACE_EYE_ACTOR_PIXEL_COUNT)) {
                free(sheet);
                free(anki_sheet);
                return 1;
            }
            copy_frame(sheet, width, expression, style, frame);
            for (size_t row = 0U; row < 4U; ++row) {
                if ((face_eye_actor_style_t)style == ANKI_STYLES[row]) {
                    copy_frame(
                        anki_sheet, width, expression, row, frame);
                }
            }
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/face-eye-actors-expressions.ppm",
        directory);
    int result = write_ppm(path, sheet, width, height);
    snprintf(path, sizeof(path),
        "%s/face-eye-actors-anki-expressions.ppm", directory);
    if (result == 0) {
        result = write_ppm(path, anki_sheet, width, anki_height);
    }
    free(sheet);
    free(anki_sheet);
    return result;
}

static int dump_neutral_sheet(const char *directory)
{
    enum {
        COLUMNS = 5,
        ROWS = (FACE_EYE_ACTOR_COUNT + COLUMNS - 1) / COLUMNS,
    };
    const size_t width = COLUMNS * FACE_EYE_ACTOR_WIDTH;
    const size_t height = ROWS * FACE_EYE_ACTOR_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        uint16_t frame[FACE_EYE_ACTOR_PIXEL_COUNT];
        const face_render_key_t key =
            preview_key(FACE_EXPRESSION_NEUTRAL, false);
        if (!face_eye_actor_render(
                (face_eye_actor_style_t)style,
                &key,
                PREVIEW_CLOCK,
                frame,
                FACE_EYE_ACTOR_PIXEL_COUNT)) {
            free(sheet);
            return 1;
        }
        copy_frame(sheet, width, style % COLUMNS, style / COLUMNS, frame);
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/face-eye-actors-neutral.ppm", directory);
    const int result = write_ppm(path, sheet, width, height);
    free(sheet);
    return result;
}

static int dump_speaking_strips(const char *directory)
{
    const size_t strip_width = 12U * FACE_EYE_ACTOR_WIDTH;
    const size_t strip_height = FACE_EYE_ACTOR_HEIGHT;
    const size_t sheet_height =
        FACE_EYE_ACTOR_COUNT * FACE_EYE_ACTOR_HEIGHT;
    const size_t anki_sheet_height = 4U * FACE_EYE_ACTOR_HEIGHT;
    uint16_t *strip =
        (uint16_t *)calloc(strip_width * strip_height, sizeof(uint16_t));
    uint16_t *sheet =
        (uint16_t *)calloc(strip_width * sheet_height, sizeof(uint16_t));
    uint16_t *anki_sheet = (uint16_t *)calloc(
        strip_width * anki_sheet_height, sizeof(uint16_t));
    if (strip == NULL || sheet == NULL || anki_sheet == NULL) {
        free(strip);
        free(sheet);
        free(anki_sheet);
        return 1;
    }
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        for (size_t frame_index = 0U; frame_index < 12U; ++frame_index) {
            uint16_t frame[FACE_EYE_ACTOR_PIXEL_COUNT];
            face_render_key_t key =
                preview_key(FACE_EXPRESSION_WARM, true);
            key.viseme = SPEECH_SEQUENCE[frame_index];
            key.viseme_secondary =
                SPEECH_SEQUENCE[(frame_index + 1U) % 12U];
            key.viseme_blend = (uint8_t)(frame_index * 19U);
            key.audio_level =
                (uint8_t)(72U + (frame_index % 5U) * 35U);
            key.speech_phase =
                frame_index == 0U ? FACE_SPEECH_STARTING
                : frame_index == 11U ? FACE_SPEECH_ENDING
                : FACE_SPEECH_ACTIVE;
            if (!face_eye_actor_render(
                    (face_eye_actor_style_t)style,
                    &key,
                    /*
                     * Hold the autonomous clock still so this comparison
                     * strip isolates speech acting from saccades and blinks.
                     */
                    PREVIEW_CLOCK,
                    frame,
                    FACE_EYE_ACTOR_PIXEL_COUNT)) {
                free(strip);
                free(sheet);
                free(anki_sheet);
                return 1;
            }
            copy_frame(strip, strip_width, frame_index, 0U, frame);
            copy_frame(sheet, strip_width, frame_index, style, frame);
            for (size_t row = 0U; row < 4U; ++row) {
                if ((face_eye_actor_style_t)style == ANKI_STYLES[row]) {
                    copy_frame(
                        anki_sheet, strip_width, frame_index, row, frame);
                }
            }
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/speaking-%02zu-%s.ppm",
            directory, style, face_eye_actor_slug(
                (face_eye_actor_style_t)style));
        if (write_ppm(path, strip, strip_width, strip_height) != 0) {
            free(strip);
            free(sheet);
            free(anki_sheet);
            return 1;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/face-eye-actors-speaking.ppm",
        directory);
    int result =
        write_ppm(path, sheet, strip_width, sheet_height);
    snprintf(path, sizeof(path), "%s/face-eye-actors-anki-speaking.ppm",
        directory);
    if (result == 0) {
        result = write_ppm(
            path, anki_sheet, strip_width, anki_sheet_height);
    }
    free(strip);
    free(sheet);
    free(anki_sheet);
    return result;
}

static uint32_t preview_hash32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static int dump_anki_blink_strips(const char *directory)
{
    enum {
        BLINK_FRAMES = 12,
        BLINK_STEP_SAMPLES = 500,
        ANKI_STYLE_COUNT = 4,
    };
    const size_t strip_width =
        BLINK_FRAMES * FACE_EYE_ACTOR_WIDTH;
    const size_t strip_height = FACE_EYE_ACTOR_HEIGHT;
    const size_t sheet_height =
        ANKI_STYLE_COUNT * FACE_EYE_ACTOR_HEIGHT;
    uint16_t *strip =
        (uint16_t *)calloc(strip_width * strip_height, sizeof(uint16_t));
    uint16_t *sheet =
        (uint16_t *)calloc(strip_width * sheet_height, sizeof(uint16_t));
    if (strip == NULL || sheet == NULL) {
        free(strip);
        free(sheet);
        return 1;
    }

    for (size_t row = 0U; row < ANKI_STYLE_COUNT; ++row) {
        const face_eye_actor_style_t style = ANKI_STYLES[row];
        const uint32_t period =
            47000U + preview_hash32((uint32_t)style + 91U) % 11000U;
        const uint32_t offset = (uint32_t)style * 1877U % period;
        const uint32_t blink_start = period - offset;
        face_render_key_t key =
            preview_key(FACE_EXPRESSION_NEUTRAL, false);
        key.controls.eye_left_open = 255U;
        key.controls.eye_right_open = 255U;
        key.controls.expression = FACE_ACTIVITY_IDLE;
        key.controls.flags = 0U;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.affect_arousal = 128U;
        key.attention = 128U;

        for (size_t frame_index = 0U;
             frame_index < BLINK_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_EYE_ACTOR_PIXEL_COUNT];
            if (!face_eye_actor_render(
                    style,
                    &key,
                    blink_start +
                        (uint32_t)frame_index * BLINK_STEP_SAMPLES,
                    frame,
                    FACE_EYE_ACTOR_PIXEL_COUNT)) {
                free(strip);
                free(sheet);
                return 1;
            }
            copy_frame(strip, strip_width, frame_index, 0U, frame);
            copy_frame(sheet, strip_width, frame_index, row, frame);
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/blink-%02zu-%s.ppm",
            directory, (size_t)style, face_eye_actor_slug(style));
        if (write_ppm(path, strip, strip_width, strip_height) != 0) {
            free(strip);
            free(sheet);
            return 1;
        }
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/face-eye-actors-anki-blink.ppm",
        directory);
    const int result =
        write_ppm(path, sheet, strip_width, sheet_height);
    free(strip);
    free(sheet);
    return result;
}

static int write_manifest(const char *directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.txt", directory);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(file,
        "Each cell is the native 160x120 RGB565 render converted to PPM.\n"
        "Anki-only sheet rows: vector-felt (7), cozmo-tiles (8), "
        "vector-stage (40), cozmo-console (41).\n"
        "Blink strips: 12 chronological frames at 500-sample intervals, "
        "open -> closed -> open.\n"
        "Expression columns: ");
    for (size_t expression = 0U;
         expression < FACE_EXPRESSION_COUNT;
         ++expression) {
        fprintf(file, "%s%s",
            expression == 0U ? "" : ", ",
            EXPRESSION_NAMES[expression]);
    }
    fputc('\n', file);
    for (size_t style = 0U; style < FACE_EYE_ACTOR_COUNT; ++style) {
        face_eye_actor_info_t info;
        if (!face_eye_actor_info(
                (face_eye_actor_style_t)style, &info)) {
            fclose(file);
            return 1;
        }
        fprintf(file, "%02zu legacy=%u %s — %s%s\n",
            style, info.legacy_profile_id, info.slug, info.name,
            info.deliberate_monocular ? " [authored one-eye]" : "");
    }
    return fclose(file) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (dump_expression_sheet(argv[1]) != 0 ||
        dump_neutral_sheet(argv[1]) != 0 ||
        dump_speaking_strips(argv[1]) != 0 ||
        dump_anki_blink_strips(argv[1]) != 0 ||
        write_manifest(argv[1]) != 0) {
        fprintf(stderr, "failed to write actor artifacts in %s\n", argv[1]);
        return 1;
    }
    printf("wrote %d actors x %d expressions, speech strips, and "
        "4 x 12-frame blink strips to %s\n",
        FACE_EYE_ACTOR_COUNT, FACE_EXPRESSION_COUNT, argv[1]);
    return 0;
}
