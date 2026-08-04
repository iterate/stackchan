#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_mouth_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    MSR_DUMP_LABEL_HEIGHT = 12,
    MSR_DUMP_TILE_HEIGHT =
        FACE_MOUTH_STUDY_REDUX_HEIGHT + MSR_DUMP_LABEL_HEIGHT,
    MSR_DUMP_CONTACT_DIVISOR = 4,
};

static const char *const MSR_EXPRESSION_NAMES[FACE_EXPRESSION_COUNT] = {
    "NEUTRAL",
    "WARM",
    "JOY",
    "CONCERN",
    "SURPRISE",
    "THOUGHTFUL",
    "SKEPTICAL",
    "DETERMINED",
    "SLEEPY",
    "EXCITED",
    "EMBARRASSED",
};

static const char *const MSR_OVR_NAMES[FACE_VISEME_COUNT] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

static const char *const MSR_VRM_NAMES[5] = {
    "AA", "E", "I", "O", "U",
};

static const char *const MSR_PRESTON_NAMES[9] = {
    "SIL", "MBP", "FV", "TH", "E", "AA", "O", "U", "SZ",
};

static const face_mouth_study_redux_profile_t MSR_POLISH_PROFILES[4] = {
    FACE_MOUTH_STUDY_REDUX_PRESTON,
    FACE_MOUTH_STUDY_REDUX_JALI,
    FACE_MOUTH_STUDY_REDUX_LED_VU,
    FACE_MOUTH_STUDY_REDUX_ORIGAMI,
};

static face_render_key_t msr_preview_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 68U;
    key.controls.mouth_width = 140U;
    key.controls.mouth_round = 18U;
    key.controls.mouth_press = 5U;
    key.controls.mouth_teeth = 105U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 235U;
    key.controls.look_x = 2;
    key.controls.look_y = -1;
    key.controls.brow = 3;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 64U;
    key.audio_level = 14U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.cheek = 20U;
    key.affect_valence = 6;
    key.affect_arousal = 114U;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static uint32_t msr_open_clock(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    for (uint32_t time_ms = 0U; time_ms < 16000U; time_ms += 25U) {
        face_mouth_study_redux_pose_t pose;
        if (!face_mouth_study_redux_resolve(
                profile, key, time_ms * 16U, &pose)) {
            return 0U;
        }
        if (pose.blink_q8 == 255U) {
            return time_ms * 16U;
        }
    }
    return 0U;
}

static uint8_t msr_expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t msr_expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int msr_write_ppm(
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
    if (fprintf(file, "P6\n%zu %zu\n255\n", width, height) < 0) {
        fclose(file);
        return 1;
    }
    for (size_t index = 0U; index < width * height; ++index) {
        const uint16_t color = pixels[index];
        const uint8_t rgb[3] = {
            msr_expand5((uint16_t)((color >> 11U) & 31U)),
            msr_expand6((uint16_t)((color >> 5U) & 63U)),
            msr_expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fprintf(stderr, "write %s failed\n", path);
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static uint8_t msr_glyph_row(char character, size_t row)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const uint8_t LETTERS[26][7] = {
        {14, 17, 17, 31, 17, 17, 17},
        {30, 17, 17, 30, 17, 17, 30},
        {14, 17, 16, 16, 16, 17, 14},
        {30, 17, 17, 17, 17, 17, 30},
        {31, 16, 16, 30, 16, 16, 31},
        {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 15},
        {17, 17, 17, 31, 17, 17, 17},
        {31, 4, 4, 4, 4, 4, 31},
        {7, 2, 2, 2, 18, 18, 12},
        {17, 18, 20, 24, 20, 18, 17},
        {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17},
        {17, 25, 21, 19, 17, 17, 17},
        {14, 17, 17, 17, 17, 17, 14},
        {30, 17, 17, 30, 16, 16, 16},
        {14, 17, 17, 17, 21, 18, 13},
        {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30},
        {31, 4, 4, 4, 4, 4, 4},
        {17, 17, 17, 17, 17, 17, 14},
        {17, 17, 17, 17, 17, 10, 4},
        {17, 17, 17, 21, 21, 21, 10},
        {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4},
        {31, 1, 2, 4, 8, 16, 31},
    };
    static const uint8_t DIGITS[10][7] = {
        {14, 17, 19, 21, 25, 17, 14},
        {4, 12, 4, 4, 4, 4, 14},
        {14, 17, 1, 2, 4, 8, 31},
        {30, 1, 1, 14, 1, 1, 30},
        {2, 6, 10, 18, 31, 2, 2},
        {31, 16, 16, 30, 1, 1, 30},
        {14, 16, 16, 30, 17, 17, 14},
        {31, 1, 2, 4, 8, 8, 8},
        {14, 17, 17, 14, 17, 17, 14},
        {14, 17, 17, 15, 1, 1, 14},
    };
    const char *position = strchr(ALPHABET, character);
    if (position != NULL) {
        return LETTERS[(size_t)(position - ALPHABET)][row];
    }
    if (character >= '0' && character <= '9') {
        return DIGITS[(size_t)(character - '0')][row];
    }
    if (character == '-') {
        return row == 3U ? 31U : 0U;
    }
    if (character == ':') {
        return row == 2U || row == 5U ? 4U : 0U;
    }
    return 0U;
}

static void msr_label(
    uint16_t *sheet,
    size_t sheet_width,
    size_t tile_x,
    size_t tile_y,
    const char *text)
{
    const size_t origin_x =
        tile_x * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t origin_y = tile_y * MSR_DUMP_TILE_HEIGHT;
    for (size_t y = 0U; y < MSR_DUMP_LABEL_HEIGHT; ++y) {
        for (size_t x = 0U;
             x < FACE_MOUTH_STUDY_REDUX_WIDTH;
             ++x) {
            sheet[(origin_y + y) * sheet_width + origin_x + x] =
                0x1082U;
        }
    }
    size_t cursor_x = origin_x + 3U;
    for (size_t character = 0U;
         text[character] != '\0' &&
             cursor_x + 5U <
                 origin_x + FACE_MOUTH_STUDY_REDUX_WIDTH;
         ++character) {
        for (size_t row = 0U; row < 7U; ++row) {
            const uint8_t bits =
                msr_glyph_row(text[character], row);
            for (size_t column = 0U; column < 5U; ++column) {
                if ((bits &
                     (uint8_t)(1U << (4U - column))) != 0U) {
                    sheet[
                        (origin_y + 2U + row) * sheet_width +
                        cursor_x + column] = 0xffffU;
                }
            }
        }
        cursor_x += 6U;
    }
}

static void msr_copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t tile_x,
    size_t tile_y,
    const uint16_t *frame)
{
    const size_t origin_x =
        tile_x * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t origin_y =
        tile_y * MSR_DUMP_TILE_HEIGHT + MSR_DUMP_LABEL_HEIGHT;
    for (size_t y = 0U;
         y < FACE_MOUTH_STUDY_REDUX_HEIGHT;
         ++y) {
        memcpy(
            sheet + (origin_y + y) * sheet_width + origin_x,
            frame + y * FACE_MOUTH_STUDY_REDUX_WIDTH,
            FACE_MOUTH_STUDY_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static void msr_copy_native_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t tile_x,
    size_t tile_y,
    const uint16_t *frame)
{
    const size_t origin_x =
        tile_x * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t origin_y =
        tile_y * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    for (size_t y = 0U;
         y < FACE_MOUTH_STUDY_REDUX_HEIGHT;
         ++y) {
        memcpy(
            sheet + (origin_y + y) * sheet_width + origin_x,
            frame + y * FACE_MOUTH_STUDY_REDUX_WIDTH,
            FACE_MOUTH_STUDY_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static uint16_t *msr_make_contact_sheet(
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    const size_t width =
        native_width / MSR_DUMP_CONTACT_DIVISOR;
    const size_t height =
        native_height / MSR_DUMP_CONTACT_DIVISOR;
    uint16_t *contact =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (contact == NULL) {
        return NULL;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            const size_t source_y =
                y * MSR_DUMP_CONTACT_DIVISOR +
                MSR_DUMP_CONTACT_DIVISOR / 2U;
            const size_t source_x =
                x * MSR_DUMP_CONTACT_DIVISOR +
                MSR_DUMP_CONTACT_DIVISOR / 2U;
            contact[y * width + x] =
                native[source_y * native_width + source_x];
        }
    }
    return contact;
}

static int msr_write_sheet(
    const char *directory,
    const char *filename,
    const uint16_t *pixels,
    size_t width,
    size_t height)
{
    char path[1024];
    const int written = snprintf(
        path, sizeof(path), "%s/%s", directory, filename);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return 1;
    }
    return msr_write_ppm(path, pixels, width, height);
}

static int msr_write_native_and_contact(
    const char *directory,
    const char *native_filename,
    const char *contact_filename,
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    if (msr_write_sheet(
            directory,
            native_filename,
            native,
            native_width,
            native_height) != 0) {
        return 1;
    }
    uint16_t *contact = msr_make_contact_sheet(
        native, native_width, native_height);
    if (contact == NULL) {
        return 1;
    }
    const int result = msr_write_sheet(
        directory,
        contact_filename,
        contact,
        native_width / MSR_DUMP_CONTACT_DIVISOR,
        native_height / MSR_DUMP_CONTACT_DIVISOR);
    free(contact);
    return result;
}

static uint16_t *msr_allocate_sheet(
    size_t columns,
    size_t rows,
    size_t *width,
    size_t *height)
{
    *width = columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    *height = rows * MSR_DUMP_TILE_HEIGHT;
    return (uint16_t *)calloc(*width * *height, sizeof(uint16_t));
}

static int msr_dump_expressions(const char *directory)
{
    const size_t columns = FACE_EXPRESSION_COUNT;
    const size_t rows = FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT;
    size_t width;
    size_t height;
    uint16_t *sheet = msr_allocate_sheet(
        columns,
        rows,
        &width,
        &height);
    const size_t native_width =
        columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t native_height =
        rows * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    uint16_t *native = (uint16_t *)calloc(
        native_width * native_height, sizeof(uint16_t));
    if (sheet == NULL || native == NULL) {
        free(sheet);
        free(native);
        return 1;
    }
    face_render_key_t key = msr_preview_key();
    for (size_t row = 0U;
         row < rows;
         ++row) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + row);
        const uint32_t clock = msr_open_clock(profile, &key);
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            char label[64];
            key.stage_expression = expression;
            if (!face_mouth_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                free(native);
                return 1;
            }
            snprintf(
                label, sizeof(label), "%u %s",
                (unsigned)(23U + row),
                MSR_EXPRESSION_NAMES[expression]);
            msr_label(sheet, width, expression, row, label);
            msr_copy_frame(sheet, width, expression, row, frame);
            msr_copy_native_frame(
                native,
                native_width,
                expression,
                row,
                frame);
        }
    }
    int result = msr_write_sheet(
        directory,
        "mouth-study-expressions-6x11.ppm",
        sheet,
        width,
        height);
    if (result == 0) {
        result = msr_write_native_and_contact(
            directory,
            "mouth-study-expressions-native-6x11.ppm",
            "mouth-study-expressions-40x30-6x11.ppm",
            native,
            native_width,
            native_height);
    }
    free(sheet);
    free(native);
    return result;
}

static void msr_temporal_pose(
    size_t column,
    face_render_key_t *key,
    char *label,
    size_t label_capacity)
{
    *key = msr_preview_key();
    if (column == 0U) {
        snprintf(label, label_capacity, "00 REST");
        return;
    }
    key->controls.expression = FACE_ACTIVITY_SPEAKING;
    key->controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key->stage_expression = FACE_EXPRESSION_WARM;
    if (column <= 3U) {
        key->speech_phase = FACE_SPEECH_STARTING;
        key->viseme = FACE_VISEME_AA;
        key->viseme_weight = (uint8_t)(column * 64U);
        key->controls.mouth_open = (uint8_t)(30U + column * 18U);
        key->audio_level = (uint8_t)(18U + column * 24U);
        key->controls.look_x = -54;
        key->controls.look_y = -72;
        key->controls.eye_left_open =
            (uint8_t)(166U + column * 22U);
        key->controls.eye_right_open =
            (uint8_t)(154U + column * 24U);
        snprintf(label, label_capacity, "%02zu ANTIC", column);
        return;
    }
    if (column <= 19U) {
        static const uint8_t PRIMARY[4] = {
            FACE_VISEME_AA,
            FACE_VISEME_E,
            FACE_VISEME_O,
            FACE_VISEME_U,
        };
        static const uint8_t SECONDARY[4] = {
            FACE_VISEME_E,
            FACE_VISEME_O,
            FACE_VISEME_U,
            FACE_VISEME_PP,
        };
        const size_t speech_index = column - 4U;
        const size_t segment = speech_index / 4U;
        const size_t blend_step = speech_index % 4U;
        static const int8_t FOCUS_X[4] = {
            -72, -24, 42, 72,
        };
        static const int8_t FOCUS_Y[4] = {
            -66, -30, 18, -42,
        };
        static const uint8_t EYE_LEFT[4] = {
            242U, 148U, 232U, 164U,
        };
        static const uint8_t EYE_RIGHT[4] = {
            212U, 232U, 154U, 218U,
        };
        key->speech_phase = FACE_SPEECH_ACTIVE;
        key->viseme = PRIMARY[segment];
        key->viseme_secondary = SECONDARY[segment];
        key->viseme_blend = (uint8_t)(blend_step * 85U);
        key->viseme_weight = 255U;
        const size_t envelope =
            speech_index < 8U ? speech_index : 15U - speech_index;
        key->controls.mouth_open =
            (uint8_t)(84U + envelope * 7U);
        key->audio_level =
            (uint8_t)(92U + envelope * 13U);
        key->controls.look_x = FOCUS_X[segment];
        key->controls.look_y = FOCUS_Y[segment];
        key->controls.eye_left_open = EYE_LEFT[segment];
        key->controls.eye_right_open = EYE_RIGHT[segment];
        snprintf(
            label, label_capacity, "%02zu %s-%s",
            column,
            MSR_OVR_NAMES[PRIMARY[segment]],
            MSR_OVR_NAMES[SECONDARY[segment]]);
        return;
    }
    if (column <= 22U) {
        key->speech_phase = FACE_SPEECH_ENDING;
        key->viseme = FACE_VISEME_PP;
        key->viseme_weight =
            (uint8_t)(255U - (column - 20U) * 86U);
        key->controls.mouth_open =
            (uint8_t)(88U - (column - 20U) * 28U);
        key->audio_level =
            (uint8_t)(66U - (column - 20U) * 24U);
        key->controls.look_x =
            (int8_t)(42 - (int32_t)(column - 20U) * 21);
        key->controls.look_y =
            (int8_t)(18 + (int32_t)(column - 20U) * 18);
        key->controls.eye_left_open =
            (uint8_t)(224U - (column - 20U) * 10U);
        key->controls.eye_right_open =
            (uint8_t)(238U - (column - 20U) * 6U);
        snprintf(label, label_capacity, "%02zu SETTLE", column);
        return;
    }
    key->controls.flags = 0U;
    key->controls.expression = FACE_ACTIVITY_LISTENING;
    key->speech_phase = FACE_SPEECH_IDLE;
    key->viseme = FACE_VISEME_SIL;
    key->viseme_weight = 0U;
    key->audio_level = 0U;
    key->controls.mouth_open = 30U;
    key->stage_expression = FACE_EXPRESSION_WARM;
    snprintf(label, label_capacity, "23 RELEASE");
}

static int msr_dump_temporal(const char *directory)
{
    const size_t columns = 24U;
    const size_t rows = FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT;
    size_t width;
    size_t height;
    uint16_t *sheet = msr_allocate_sheet(
        columns,
        rows,
        &width,
        &height);
    const size_t native_width =
        columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t native_height =
        rows * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    uint16_t *native = (uint16_t *)calloc(
        native_width * native_height, sizeof(uint16_t));
    if (sheet == NULL || native == NULL) {
        free(sheet);
        free(native);
        return 1;
    }
    for (size_t row = 0U;
         row < rows;
         ++row) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + row);
        const face_render_key_t preview = msr_preview_key();
        const uint32_t clock = msr_open_clock(profile, &preview);
        for (size_t column = 0U; column < columns; ++column) {
            face_render_key_t key;
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            char label[64];
            char full_label[80];
            msr_temporal_pose(
                column, &key, label, sizeof(label));
            if (!face_mouth_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                free(native);
                return 1;
            }
            snprintf(
                full_label, sizeof(full_label), "%u %s",
                (unsigned)(23U + row), label);
            msr_label(sheet, width, column, row, full_label);
            msr_copy_frame(sheet, width, column, row, frame);
            msr_copy_native_frame(
                native,
                native_width,
                column,
                row,
                frame);
        }
    }
    int result = msr_write_sheet(
        directory,
        "mouth-study-temporal-speech-6x24.ppm",
        sheet,
        width,
        height);
    if (result == 0) {
        result = msr_write_native_and_contact(
            directory,
            "mouth-study-temporal-speech-native-6x24.ppm",
            "mouth-study-temporal-speech-40x30-6x24.ppm",
            native,
            native_width,
            native_height);
    }
    free(sheet);
    free(native);
    return result;
}

static int msr_dump_vocabularies(const char *directory)
{
    const size_t columns = 29U;
    size_t width;
    size_t height;
    uint16_t *sheet = msr_allocate_sheet(
        columns,
        FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT,
        &width,
        &height);
    if (sheet == NULL) {
        return 1;
    }
    for (size_t row = 0U;
         row < FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT;
         ++row) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(23U + row);
        face_render_key_t key = msr_preview_key();
        key.controls.expression = FACE_ACTIVITY_SPEAKING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.controls.mouth_open = 52U;
        key.controls.mouth_width = 128U;
        key.controls.mouth_round = 0U;
        key.controls.mouth_press = 0U;
        key.controls.mouth_teeth = 0U;
        key.viseme_weight = 255U;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        const uint32_t clock = msr_open_clock(profile, &key);
        for (size_t column = 0U; column < columns; ++column) {
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            char label[64];
            if (column < 15U) {
                key.viseme_set = FACE_VISEME_SET_OVR15;
                key.viseme = (uint8_t)column;
                snprintf(
                    label, sizeof(label), "%u OVR-%s",
                    (unsigned)(23U + row), MSR_OVR_NAMES[column]);
            } else if (column < 20U) {
                const size_t index = column - 15U;
                key.viseme_set = FACE_VISEME_SET_VRM5;
                key.viseme = (uint8_t)index;
                snprintf(
                    label, sizeof(label), "%u VRM-%s",
                    (unsigned)(23U + row), MSR_VRM_NAMES[index]);
            } else {
                const size_t index = column - 20U;
                key.viseme_set = FACE_VISEME_SET_PRESTON9;
                key.viseme = (uint8_t)index;
                snprintf(
                    label, sizeof(label), "%u PRE-%s",
                    (unsigned)(23U + row), MSR_PRESTON_NAMES[index]);
            }
            if (!face_mouth_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            msr_label(sheet, width, column, row, label);
            msr_copy_frame(sheet, width, column, row, frame);
        }
    }
    const int result = msr_write_sheet(
        directory,
        "mouth-study-vocabularies-6x29.ppm",
        sheet,
        width,
        height);
    free(sheet);
    return result;
}

static int msr_dump_polish_expressions(const char *directory)
{
    const size_t columns = FACE_EXPRESSION_COUNT;
    const size_t rows =
        sizeof(MSR_POLISH_PROFILES) / sizeof(MSR_POLISH_PROFILES[0]);
    const size_t native_width =
        columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t native_height =
        rows * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    uint16_t *native = (uint16_t *)calloc(
        native_width * native_height, sizeof(uint16_t));
    if (native == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        const face_mouth_study_redux_profile_t profile =
            MSR_POLISH_PROFILES[row];
        face_render_key_t key = msr_preview_key();
        const uint32_t clock = msr_open_clock(profile, &key);
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            key.stage_expression = expression;
            if (!face_mouth_study_redux_render(
                    profile,
                    &key,
                    clock,
                    frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(native);
                return 1;
            }
            msr_copy_native_frame(
                native,
                native_width,
                expression,
                row,
                frame);
        }
    }
    const int result = msr_write_native_and_contact(
        directory,
        "mouth-study-polish-expressions-native-4x11.ppm",
        "mouth-study-polish-expressions-40x30-4x11.ppm",
        native,
        native_width,
        native_height);
    free(native);
    return result;
}

static int msr_dump_polish_visemes(const char *directory)
{
    const size_t columns = FACE_VISEME_COUNT;
    const size_t rows =
        sizeof(MSR_POLISH_PROFILES) / sizeof(MSR_POLISH_PROFILES[0]);
    const size_t native_width =
        columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t native_height =
        rows * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    uint16_t *native = (uint16_t *)calloc(
        native_width * native_height, sizeof(uint16_t));
    if (native == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        const face_mouth_study_redux_profile_t profile =
            MSR_POLISH_PROFILES[row];
        face_render_key_t key = msr_preview_key();
        key.controls.expression = FACE_ACTIVITY_SPEAKING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.controls.mouth_open = 52U;
        key.controls.mouth_width = 128U;
        key.controls.mouth_round = 0U;
        key.controls.mouth_press = 0U;
        key.controls.mouth_teeth = 0U;
        key.viseme_set = FACE_VISEME_SET_OVR15;
        key.viseme_secondary = FACE_VISEME_NONE;
        key.viseme_blend = 0U;
        key.viseme_weight = 255U;
        key.audio_level = 72U;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        const uint32_t clock = msr_open_clock(profile, &key);
        for (uint8_t viseme = 0U;
             viseme < FACE_VISEME_COUNT;
             ++viseme) {
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            key.viseme = viseme;
            if (!face_mouth_study_redux_render(
                    profile,
                    &key,
                    clock,
                    frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(native);
                return 1;
            }
            msr_copy_native_frame(
                native,
                native_width,
                viseme,
                row,
                frame);
        }
    }
    const int result = msr_write_native_and_contact(
        directory,
        "mouth-study-polish-visemes-native-4x15.ppm",
        "mouth-study-polish-visemes-40x30-4x15.ppm",
        native,
        native_width,
        native_height);
    free(native);
    return result;
}

static void msr_polish_temporal_pose(
    size_t frame,
    face_render_key_t *key)
{
    *key = msr_preview_key();
    key->stage_expression = FACE_EXPRESSION_WARM;
    if (frame == 0U || frame == 15U) {
        key->controls.flags = 0U;
        key->controls.expression = FACE_ACTIVITY_LISTENING;
        key->speech_phase = FACE_SPEECH_IDLE;
        key->viseme = FACE_VISEME_SIL;
        key->viseme_weight = 0U;
        key->audio_level = 0U;
        key->controls.mouth_open = 28U;
        return;
    }
    key->controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key->controls.expression = FACE_ACTIVITY_SPEAKING;
    if (frame <= 3U) {
        static const uint8_t WEIGHT[3] = {
            72U, 168U, 255U,
        };
        static const uint8_t OPEN[3] = {
            48U, 100U, 150U,
        };
        static const uint8_t AUDIO[3] = {
            20U, 70U, 110U,
        };
        const size_t index = frame - 1U;
        key->speech_phase = FACE_SPEECH_STARTING;
        key->viseme = FACE_VISEME_AA;
        key->viseme_secondary = FACE_VISEME_NONE;
        key->viseme_blend = 0U;
        key->viseme_weight = WEIGHT[index];
        key->controls.mouth_open = OPEN[index];
        key->audio_level = AUDIO[index];
        key->controls.look_x =
            (int8_t)(-36 + (int32_t)index * 8);
        key->controls.look_y =
            (int8_t)(-54 + (int32_t)index * 12);
        return;
    }
    if (frame <= 11U) {
        static const uint8_t PRIMARY[8] = {
            FACE_VISEME_AA,
            FACE_VISEME_AA,
            FACE_VISEME_E,
            FACE_VISEME_E,
            FACE_VISEME_I,
            FACE_VISEME_O,
            FACE_VISEME_O,
            FACE_VISEME_U,
        };
        static const uint8_t SECONDARY[8] = {
            FACE_VISEME_E,
            FACE_VISEME_E,
            FACE_VISEME_I,
            FACE_VISEME_I,
            FACE_VISEME_O,
            FACE_VISEME_U,
            FACE_VISEME_U,
            FACE_VISEME_PP,
        };
        static const uint8_t BLEND[8] = {
            0U, 128U, 0U, 192U, 160U, 0U, 192U, 128U,
        };
        static const uint8_t AUDIO[8] = {
            96U, 124U, 154U, 188U,
            202U, 176U, 138U, 92U,
        };
        static const int8_t LOOK_X[8] = {
            -16, -8, 0, 8, 18, 26, 20, 12,
        };
        static const int8_t LOOK_Y[8] = {
            -24, -20, -14, -8, 2, 10, 4, -6,
        };
        const size_t index = frame - 4U;
        key->speech_phase = FACE_SPEECH_ACTIVE;
        key->viseme = PRIMARY[index];
        key->viseme_secondary = SECONDARY[index];
        key->viseme_blend = BLEND[index];
        key->viseme_weight = 255U;
        key->controls.mouth_open =
            (uint8_t)(90U + AUDIO[index] / 3U);
        key->audio_level = AUDIO[index];
        key->controls.look_x = LOOK_X[index];
        key->controls.look_y = LOOK_Y[index];
        key->controls.eye_left_open =
            (uint8_t)(218U + index % 3U * 8U);
        key->controls.eye_right_open =
            (uint8_t)(224U - index % 3U * 6U);
        return;
    }
    static const uint8_t WEIGHT[3] = {
        192U, 96U, 24U,
    };
    static const uint8_t OPEN[3] = {
        70U, 45U, 30U,
    };
    static const uint8_t AUDIO[3] = {
        50U, 25U, 5U,
    };
    const size_t index = frame - 12U;
    key->speech_phase = FACE_SPEECH_ENDING;
    key->viseme =
        index < 2U ? FACE_VISEME_PP : FACE_VISEME_SIL;
    key->viseme_secondary = FACE_VISEME_NONE;
    key->viseme_weight = WEIGHT[index];
    key->controls.mouth_open = OPEN[index];
    key->audio_level = AUDIO[index];
    key->controls.look_x =
        (int8_t)(12 - (int32_t)index * 6);
    key->controls.look_y =
        (int8_t)(8 + (int32_t)index * 10);
}

static int msr_dump_polish_temporal(const char *directory)
{
    const size_t columns = 16U;
    const size_t rows =
        sizeof(MSR_POLISH_PROFILES) / sizeof(MSR_POLISH_PROFILES[0]);
    const size_t native_width =
        columns * FACE_MOUTH_STUDY_REDUX_WIDTH;
    const size_t native_height =
        rows * FACE_MOUTH_STUDY_REDUX_HEIGHT;
    uint16_t *native = (uint16_t *)calloc(
        native_width * native_height, sizeof(uint16_t));
    if (native == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        const face_mouth_study_redux_profile_t profile =
            MSR_POLISH_PROFILES[row];
        const face_render_key_t preview = msr_preview_key();
        const uint32_t clock = msr_open_clock(profile, &preview);
        for (size_t frame_index = 0U;
             frame_index < columns;
             ++frame_index) {
            face_render_key_t key;
            uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
            msr_polish_temporal_pose(frame_index, &key);
            if (!face_mouth_study_redux_render(
                    profile,
                    &key,
                    clock,
                    frame,
                    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                free(native);
                return 1;
            }
            msr_copy_native_frame(
                native,
                native_width,
                frame_index,
                row,
                frame);
        }
    }
    const int result = msr_write_native_and_contact(
        directory,
        "mouth-study-polish-temporal-native-4x16.ppm",
        "mouth-study-polish-temporal-40x30-4x16.ppm",
        native,
        native_width,
        native_height);
    free(native);
    return result;
}

int main(int argument_count, char **arguments)
{
    if (argument_count != 2) {
        fprintf(
            stderr,
            "usage: %s ARTIFACT_DIRECTORY\n",
            arguments[0]);
        return 2;
    }
    if (msr_dump_expressions(arguments[1]) != 0 ||
        msr_dump_temporal(arguments[1]) != 0 ||
        msr_dump_vocabularies(arguments[1]) != 0 ||
        msr_dump_polish_expressions(arguments[1]) != 0 ||
        msr_dump_polish_visemes(arguments[1]) != 0 ||
        msr_dump_polish_temporal(arguments[1]) != 0) {
        return 1;
    }
    puts("face_mouth_study_redux_dump: wrote contact sheets");
    return 0;
}
