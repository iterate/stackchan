#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_cyber_wildcards.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    EXPRESSION_COUNT = 11,
    VISEME_COUNT = 15,
    MOTION_FRAMES = 16,
};

static const char *const EXPRESSION_NAMES[EXPRESSION_COUNT] = {
    "neutral", "warm", "joy", "concern", "surprise", "thoughtful",
    "skeptical", "determined", "sleepy", "excited", "embarrassed",
};

static const char *const VISEME_NAMES[VISEME_COUNT] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

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
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
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

static void copy_cell(
    uint16_t *sheet,
    size_t sheet_width,
    size_t cell_width,
    size_t cell_height,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < cell_height; ++y) {
        memcpy(
            sheet + (row * cell_height + y) * sheet_width +
                column * cell_width,
            frame + y * cell_width,
            cell_width * sizeof(uint16_t));
    }
}

static void copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    copy_cell(
        sheet,
        sheet_width,
        FACE_CYBER_WILDCARD_WIDTH,
        FACE_CYBER_WILDCARD_HEIGHT,
        column,
        row,
        frame);
}

static face_render_key_t preview_key(uint8_t expression, bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 126U : 24U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 40U;
    key.controls.mouth_press = speaking ? 8U : 90U;
    key.controls.mouth_teeth = 126U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 240U;
    key.controls.look_x = 4;
    key.controls.look_y = -3;
    key.controls.brow = 4;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.viseme = speaking ? FACE_VISEME_AA : FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = speaking ? 230U : 0U;
    key.audio_level = speaking ? 142U : 12U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 28U;
    key.speech_phase =
        speaking ? FACE_SPEECH_ACTIVE : FACE_SPEECH_IDLE;
    key.tongue = speaking ? 70U : 0U;
    key.cheek = 28U;
    key.affect_arousal = 138U;
    key.expression_weight = 255U;
    key.attention = 218U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static face_render_key_t motion_key(size_t frame)
{
    static const uint8_t VISEMES[MOTION_FRAMES] = {
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
    static const uint8_t LEVELS[MOTION_FRAMES] = {
        8U, 24U, 88U, 116U, 132U, 148U, 122U, 74U,
        96U, 112U, 126U, 138U, 118U, 86U, 42U, 10U,
    };
    static const uint8_t OPEN[MOTION_FRAMES] = {
        12U, 24U, 86U, 112U, 132U, 154U, 120U, 48U,
        70U, 88U, 104U, 116U, 102U, 76U, 34U, 14U,
    };
    face_render_key_t key = preview_key(FACE_EXPRESSION_WARM, true);
    key.viseme = VISEMES[frame];
    key.viseme_secondary =
        VISEMES[frame + 1U < MOTION_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        frame > 1U && frame < 14U ? 52U : 0U;
    key.audio_level = LEVELS[frame];
    key.controls.mouth_open = OPEN[frame];
    key.controls.mouth_width =
        (uint8_t)(154U + (frame % 5U) * 10U);
    key.controls.mouth_round =
        (uint8_t)(32U + (frame % 4U) * 38U);
    key.controls.look_x = 4;
    key.controls.look_y = -3;
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

static int dump_expressions(const char *directory)
{
    const size_t width =
        EXPRESSION_COUNT * FACE_CYBER_WILDCARD_WIDTH;
    const size_t height = FACE_CYBER_WILDCARD_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        memset(sheet, 0, width * height * sizeof(uint16_t));
        for (uint8_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            const face_render_key_t key =
                preview_key(expression, false);
            if (!face_cyber_wildcard_render(
                    (face_cyber_wildcard_profile_t)raw,
                    &key,
                    5064U,
                    frame,
                    FACE_CYBER_WILDCARD_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            copy_frame(sheet, width, expression, 0U, frame);
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/%02zu-%s-expressions.ppm",
            directory,
            raw,
            face_cyber_wildcard_slug(
                (face_cyber_wildcard_profile_t)raw));
        if (write_ppm(path, sheet, width, height) != 0) {
            free(sheet);
            return 1;
        }
    }
    free(sheet);
    return 0;
}

static int dump_visemes(const char *directory)
{
    const size_t width =
        VISEME_COUNT * FACE_CYBER_WILDCARD_WIDTH;
    const size_t height =
        FACE_CYBER_WILDCARD_COUNT * FACE_CYBER_WILDCARD_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        for (uint8_t viseme = 0U; viseme < VISEME_COUNT; ++viseme) {
            uint16_t frame[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            face_render_key_t key =
                preview_key(FACE_EXPRESSION_WARM, true);
            key.viseme = viseme;
            key.viseme_secondary = FACE_VISEME_SIL;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            if (!face_cyber_wildcard_render(
                    (face_cyber_wildcard_profile_t)raw,
                    &key,
                    18231U,
                    frame,
                    FACE_CYBER_WILDCARD_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            copy_frame(sheet, width, viseme, raw, frame);
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-visemes.ppm", directory);
    const int result = write_ppm(path, sheet, width, height);
    free(sheet);
    return result;
}

static int dump_motion(const char *directory)
{
    const size_t width =
        MOTION_FRAMES * FACE_CYBER_WILDCARD_WIDTH;
    const size_t height =
        FACE_CYBER_WILDCARD_COUNT * FACE_CYBER_WILDCARD_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        for (size_t frame_index = 0U;
             frame_index < MOTION_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            const face_render_key_t key = motion_key(frame_index);
            if (!face_cyber_wildcard_render(
                    (face_cyber_wildcard_profile_t)raw,
                    &key,
                    16000U + (uint32_t)frame_index * 533U,
                    frame,
                    FACE_CYBER_WILDCARD_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            copy_frame(sheet, width, frame_index, raw, frame);
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/chronological-motion-16.ppm", directory);
    const int result = write_ppm(path, sheet, width, height);
    free(sheet);
    return result;
}

typedef enum {
    MATRIX_EXPRESSIONS = 0,
    MATRIX_VISEMES,
    MATRIX_SPEECH,
} matrix_kind_t;

static size_t matrix_columns(matrix_kind_t kind)
{
    switch (kind) {
    case MATRIX_EXPRESSIONS:
        return EXPRESSION_COUNT;
    case MATRIX_VISEMES:
        return VISEME_COUNT;
    case MATRIX_SPEECH:
        return MOTION_FRAMES;
    default:
        return 0U;
    }
}

static const char *matrix_name(matrix_kind_t kind)
{
    switch (kind) {
    case MATRIX_EXPRESSIONS:
        return "all-expressions";
    case MATRIX_VISEMES:
        return "all-visemes";
    case MATRIX_SPEECH:
        return "speech-16f";
    default:
        return "invalid";
    }
}

static face_render_key_t matrix_key(
    matrix_kind_t kind, size_t column)
{
    if (kind == MATRIX_EXPRESSIONS) {
        return preview_key((uint8_t)column, false);
    }
    if (kind == MATRIX_SPEECH) {
        return motion_key(column);
    }
    face_render_key_t key =
        preview_key(FACE_EXPRESSION_WARM, true);
    key.viseme = (uint8_t)column;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    return key;
}

static int dump_matrix(
    const char *directory,
    matrix_kind_t kind,
    size_t divisor)
{
    const size_t columns = matrix_columns(kind);
    const size_t cell_width =
        FACE_CYBER_WILDCARD_WIDTH / divisor;
    const size_t cell_height =
        FACE_CYBER_WILDCARD_HEIGHT / divisor;
    const size_t width = columns * cell_width;
    const size_t height =
        FACE_CYBER_WILDCARD_COUNT * cell_height;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL || columns == 0U) {
        free(sheet);
        return 1;
    }
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        for (size_t column = 0U; column < columns; ++column) {
            uint16_t native[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            uint16_t scaled[FACE_CYBER_WILDCARD_PIXEL_COUNT];
            const face_render_key_t key = matrix_key(kind, column);
            const uint32_t clock = kind == MATRIX_SPEECH
                ? 16000U + (uint32_t)column * 533U
                : 19231U;
            if (!face_cyber_wildcard_render(
                    (face_cyber_wildcard_profile_t)raw,
                    &key,
                    clock,
                    native,
                    FACE_CYBER_WILDCARD_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            const uint16_t *cell = native;
            if (divisor != 1U) {
                downsample_box(native, scaled, divisor);
                cell = scaled;
            }
            copy_cell(
                sheet,
                width,
                cell_width,
                cell_height,
                column,
                raw,
                cell);
        }
    }
    char path[1024];
    snprintf(
        path,
        sizeof(path),
        "%s/%s-%zux%zu.ppm",
        directory,
        matrix_name(kind),
        cell_width,
        cell_height);
    const int result = write_ppm(path, sheet, width, height);
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
    fprintf(
        file,
        "Native cells are exactly 160x120 RGB565; contact sheets use "
        "2x2 or 4x4 RGB565 box filtering for exact 80x60 and 40x30 cells.\n"
        "Expression columns: ");
    for (size_t index = 0U; index < EXPRESSION_COUNT; ++index) {
        fprintf(
            file,
            "%s%s",
            index == 0U ? "" : ", ",
            EXPRESSION_NAMES[index]);
    }
    fprintf(file, "\nViseme columns: ");
    for (size_t index = 0U; index < VISEME_COUNT; ++index) {
        fprintf(
            file,
            "%s%s",
            index == 0U ? "" : ", ",
            VISEME_NAMES[index]);
    }
    fprintf(
        file,
        "\nSpeech columns are chronological at 533 samples/frame: "
        "idle, anticipation, 12 active/coarticulated frames, 2 settle frames."
        "\n");
    for (size_t raw = 0U;
         raw < FACE_CYBER_WILDCARD_COUNT;
         ++raw) {
        face_cyber_wildcard_info_t info;
        if (!face_cyber_wildcard_info(
                (face_cyber_wildcard_profile_t)raw, &info)) {
            fclose(file);
            return 1;
        }
        fprintf(
            file,
            "%zu legacy=%u %s -- %s\n",
            raw,
            info.legacy_profile_id,
            info.slug,
            info.name);
    }
    return fclose(file) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (dump_expressions(argv[1]) != 0 ||
        dump_visemes(argv[1]) != 0 ||
        dump_motion(argv[1]) != 0 ||
        write_manifest(argv[1]) != 0) {
        fprintf(stderr, "failed to write cyber wildcard artifacts\n");
        return 1;
    }
    static const size_t DIVISORS[] = {1U, 2U, 4U};
    for (size_t index = 0U;
         index < sizeof(DIVISORS) / sizeof(DIVISORS[0]);
         ++index) {
        if (dump_matrix(
                argv[1], MATRIX_EXPRESSIONS, DIVISORS[index]) != 0 ||
            dump_matrix(
                argv[1], MATRIX_VISEMES, DIVISORS[index]) != 0 ||
            dump_matrix(
                argv[1], MATRIX_SPEECH, DIVISORS[index]) != 0) {
            fprintf(stderr, "failed to write contact matrices\n");
            return 1;
        }
    }
    printf(
        "wrote native + 80x60 + 40x30 expression, viseme, and "
        "16-frame speech matrices to %s\n",
        argv[1]);
    return 0;
}
