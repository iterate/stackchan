#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_pixel_redux_variants.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    EXPRESSION_COUNT = 11,
    VISEME_COUNT = FACE_VISEME_COUNT,
    MOTION_FRAMES = 24,
    IDLE_FRAMES = 32,
    CONTACT_DIVISOR = 4,
};

static const uint8_t MOTION_VISEMES[MOTION_FRAMES] = {
    FACE_VISEME_SIL,
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
    FACE_VISEME_KK,
    FACE_VISEME_CH,
    FACE_VISEME_NN,
    FACE_VISEME_RR,
    FACE_VISEME_AA,
    FACE_VISEME_E,
    FACE_VISEME_O,
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
};

static const uint8_t MOTION_LEVELS[MOTION_FRAMES] = {
    4U, 5U, 42U, 150U, 112U, 84U, 174U, 132U,
    42U, 86U, 120U, 94U, 105U, 142U, 78U, 91U,
    128U, 183U, 116U, 152U, 62U, 28U, 8U, 4U,
};

static const uint32_t IDLE_CLOCKS[IDLE_FRAMES] = {
    0U, 4000U, 8000U, 12000U, 16000U, 24000U, 32000U, 40000U,
    46000U, 47500U, 48100U, 48650U, 49200U, 51000U, 56000U, 64000U,
    68000U, 72000U, 76000U, 80000U, 88000U, 96000U, 104000U, 112000U,
    120000U, 128000U, 136000U, 144000U, 152000U, 160000U, 168000U,
    176000U,
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

static void copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < FACE_PIXEL_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_PIXEL_REDUX_HEIGHT + y) * sheet_width +
                column * FACE_PIXEL_REDUX_WIDTH,
            frame + y * FACE_PIXEL_REDUX_WIDTH,
            FACE_PIXEL_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static uint16_t *make_contact_sheet(
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    const size_t width = native_width / CONTACT_DIVISOR;
    const size_t height = native_height / CONTACT_DIVISOR;
    uint16_t *contact = calloc(width * height, sizeof(uint16_t));
    if (contact == NULL) {
        return NULL;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            const size_t source_y =
                y * CONTACT_DIVISOR + CONTACT_DIVISOR / 2U;
            const size_t source_x =
                x * CONTACT_DIVISOR + CONTACT_DIVISOR / 2U;
            contact[y * width + x] =
                native[source_y * native_width + source_x];
        }
    }
    return contact;
}

static face_render_key_t rest_key(uint8_t expression)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 7U;
    key.controls.mouth_width = 144U;
    key.controls.mouth_round = 24U;
    key.controls.mouth_press = 182U;
    key.controls.eye_left_open = 222U;
    key.controls.eye_right_open = 222U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.cheek = 14U;
    key.affect_arousal = 118U;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static face_render_key_t viseme_key(uint8_t viseme)
{
    face_render_key_t key = rest_key(FACE_EXPRESSION_WARM);
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.viseme = viseme;
    key.viseme_secondary = viseme;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    key.controls.mouth_open =
        (uint8_t)(64U + (viseme * 43U) % 150U);
    key.controls.mouth_width =
        (uint8_t)(108U + (viseme * 29U) % 130U);
    key.controls.mouth_round =
        (uint8_t)((viseme * 67U) & 255U);
    key.controls.mouth_press =
        viseme == FACE_VISEME_PP ? 245U : 18U;
    key.controls.mouth_teeth =
        viseme == FACE_VISEME_E ||
        viseme == FACE_VISEME_SS ||
        viseme == FACE_VISEME_FF
        ? 236U : 58U;
    key.tongue =
        viseme == FACE_VISEME_TH ? 250U :
        viseme == FACE_VISEME_AA ? 148U : 26U;
    key.phoneme = (uint8_t)(viseme * 3U + 1U);
    key.audio_level = (uint8_t)(92U + (viseme * 31U) % 90U);
    return key;
}

static face_render_key_t motion_key(size_t frame)
{
    face_render_key_t key = rest_key(FACE_EXPRESSION_WARM);
    const bool speaking = frame >= 2U && frame <= 21U;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.viseme = MOTION_VISEMES[frame];
    key.viseme_secondary =
        MOTION_VISEMES[
            frame + 1U < MOTION_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        speaking ? (uint8_t)(32U + (frame * 29U) % 170U) : 0U;
    key.viseme_weight = speaking ? 240U : 0U;
    key.controls.mouth_open =
        speaking ? (uint8_t)(58U + (frame * 47U) % 158U) : 6U;
    key.controls.mouth_width =
        speaking ? (uint8_t)(110U + (frame * 23U) % 126U) : 138U;
    key.controls.mouth_round =
        speaking ? (uint8_t)((frame * 61U) & 255U) : 22U;
    key.controls.mouth_press =
        MOTION_VISEMES[frame] == FACE_VISEME_PP ? 244U : 18U;
    key.controls.mouth_teeth =
        MOTION_VISEMES[frame] == FACE_VISEME_E ||
        MOTION_VISEMES[frame] == FACE_VISEME_SS ||
        MOTION_VISEMES[frame] == FACE_VISEME_FF
        ? 232U : 62U;
    key.tongue =
        MOTION_VISEMES[frame] == FACE_VISEME_TH ? 250U :
        MOTION_VISEMES[frame] == FACE_VISEME_AA ? 132U : 24U;
    key.phoneme = speaking
        ? (uint8_t)(frame * 3U + 1U)
        : FACE_PHONEME_NONE;
    key.audio_level = MOTION_LEVELS[frame];
    key.controls.look_x =
        (int8_t)(frame < 12U ? (int)frame - 6 : 18 - (int)frame);
    key.controls.look_y =
        (int8_t)((int)(frame % 7U) - 3);
    key.controls.eye_left_open =
        (uint8_t)(205U + (frame % 4U) * 12U);
    key.controls.eye_right_open =
        (uint8_t)(210U + ((frame + 2U) % 4U) * 10U);
    if (frame == 10U || frame == 12U) {
        key.controls.eye_left_open = 96U;
        key.controls.eye_right_open = 96U;
    } else if (frame == 11U) {
        key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
    }
    key.speech_phase =
        frame < 2U || frame > 21U ? FACE_SPEECH_IDLE :
        frame == 2U ? FACE_SPEECH_STARTING :
        frame >= 20U ? FACE_SPEECH_ENDING :
        FACE_SPEECH_ACTIVE;
    key.stage_expression =
        frame >= 16U && frame <= 19U ? FACE_EXPRESSION_JOY :
        frame == 14U || frame == 15U ? FACE_EXPRESSION_EXCITED :
        FACE_EXPRESSION_WARM;
    key.cheek =
        frame >= 14U && frame <= 19U ? 108U : 28U;
    return key;
}

static face_render_key_t matrix_key(
    unsigned kind, size_t column)
{
    if (kind == 0U) {
        return rest_key((uint8_t)column);
    }
    if (kind == 1U) {
        return viseme_key((uint8_t)column);
    }
    if (kind == 2U) {
        return motion_key(column);
    }
    return rest_key(FACE_EXPRESSION_WARM);
}

static uint32_t matrix_clock(unsigned kind, size_t column)
{
    if (kind == 3U) {
        return IDLE_CLOCKS[column];
    }
    return kind == 2U ? (uint32_t)column * 533U : 0U;
}

static int dump_matrix(
    const char *directory,
    const char *stem,
    unsigned kind,
    size_t columns)
{
    const size_t width = columns * FACE_PIXEL_REDUX_WIDTH;
    const size_t combined_height =
        FACE_PIXEL_VARIANT_COUNT * FACE_PIXEL_REDUX_HEIGHT;
    uint16_t *row = calloc(
        width * FACE_PIXEL_REDUX_HEIGHT, sizeof(uint16_t));
    uint16_t *combined =
        calloc(width * combined_height, sizeof(uint16_t));
    if (row == NULL || combined == NULL) {
        free(row);
        free(combined);
        return 1;
    }
    for (size_t variant = 0U;
         variant < FACE_PIXEL_VARIANT_COUNT;
         ++variant) {
        memset(
            row, 0,
            width * FACE_PIXEL_REDUX_HEIGHT * sizeof(uint16_t));
        for (size_t column = 0U; column < columns; ++column) {
            uint16_t frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
            const face_render_key_t key = matrix_key(kind, column);
            if (!face_pixel_redux_variant_render(
                    (face_pixel_redux_variant_t)variant,
                    &key,
                    matrix_clock(kind, column),
                    frame,
                    FACE_PIXEL_REDUX_PIXEL_COUNT)) {
                free(row);
                free(combined);
                return 1;
            }
            copy_frame(row, width, column, 0U, frame);
            copy_frame(combined, width, column, variant, frame);
        }
        char path[1024];
        snprintf(
            path, sizeof(path), "%s/%02zu-%s-%s.ppm",
            directory,
            variant,
            face_pixel_redux_variant_slug(
                (face_pixel_redux_variant_t)variant),
            stem);
        if (write_ppm(
                path, row, width, FACE_PIXEL_REDUX_HEIGHT) != 0) {
            free(row);
            free(combined);
            return 1;
        }
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/all-%s-native.ppm", directory, stem);
    if (write_ppm(path, combined, width, combined_height) != 0) {
        free(row);
        free(combined);
        return 1;
    }
    uint16_t *contact =
        make_contact_sheet(combined, width, combined_height);
    if (contact == NULL) {
        free(row);
        free(combined);
        return 1;
    }
    snprintf(
        path, sizeof(path), "%s/all-%s-exact40x30.ppm",
        directory, stem);
    const int result = write_ppm(
        path,
        contact,
        width / CONTACT_DIVISOR,
        combined_height / CONTACT_DIVISOR);
    free(contact);
    free(row);
    free(combined);
    return result;
}

static int dump_mossling_hero_matrix(
    const char *directory,
    const char *stem,
    unsigned kind,
    size_t columns)
{
    const size_t rows = 4U;
    const size_t width = columns * FACE_PIXEL_REDUX_WIDTH;
    const size_t height = rows * FACE_PIXEL_REDUX_HEIGHT;
    uint16_t *combined = calloc(width * height, sizeof(uint16_t));
    if (combined == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        for (size_t column = 0U; column < columns; ++column) {
            uint16_t frame[FACE_PIXEL_REDUX_PIXEL_COUNT];
            const face_render_key_t key = matrix_key(kind, column);
            const bool rendered = row == 0U
                ? face_pixel_redux_actor_render(
                    FACE_PIXEL_REDUX_POCKET_RPG,
                    &key,
                    matrix_clock(kind, column),
                    frame,
                    FACE_PIXEL_REDUX_PIXEL_COUNT)
                : face_pixel_redux_variant_render(
                    (face_pixel_redux_variant_t)(
                        FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT +
                        row - 1U),
                    &key,
                    matrix_clock(kind, column),
                    frame,
                    FACE_PIXEL_REDUX_PIXEL_COUNT);
            if (!rendered) {
                free(combined);
                return 1;
            }
            copy_frame(combined, width, column, row, frame);
        }
    }
    char path[1024];
    snprintf(
        path, sizeof(path), "%s/hero-mossling-%s-native.ppm",
        directory, stem);
    if (write_ppm(path, combined, width, height) != 0) {
        free(combined);
        return 1;
    }
    uint16_t *contact = make_contact_sheet(combined, width, height);
    if (contact == NULL) {
        free(combined);
        return 1;
    }
    snprintf(
        path, sizeof(path), "%s/hero-mossling-%s-exact40x30.ppm",
        directory, stem);
    const int result = write_ppm(
        path,
        contact,
        width / CONTACT_DIVISOR,
        height / CONTACT_DIVISOR);
    free(contact);
    free(combined);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (dump_matrix(
            argv[1], "expressions-11", 0U, EXPRESSION_COUNT) != 0 ||
        dump_matrix(
            argv[1], "visemes-15", 1U, VISEME_COUNT) != 0 ||
        dump_matrix(
            argv[1], "speech-blink-24f", 2U, MOTION_FRAMES) != 0 ||
        dump_matrix(
            argv[1], "idle-turn-blink-32f", 3U, IDLE_FRAMES) != 0 ||
        dump_mossling_hero_matrix(
            argv[1], "expressions-11", 0U, EXPRESSION_COUNT) != 0 ||
        dump_mossling_hero_matrix(
            argv[1], "visemes-15", 1U, VISEME_COUNT) != 0 ||
        dump_mossling_hero_matrix(
            argv[1], "speech-blink-24f", 2U, MOTION_FRAMES) != 0) {
        return 1;
    }
    if (dump_mossling_hero_matrix(
            argv[1], "idle-turn-blink-32f", 3U, IDLE_FRAMES) != 0) {
        return 1;
    }
    printf(
        "dumped %d variants: native and exact40 11-expression, "
        "15-viseme, 24-frame speech/blink, and 32-frame idle acting "
        "sheets to %s\n",
        FACE_PIXEL_VARIANT_COUNT,
        argv[1]);
    return 0;
}
