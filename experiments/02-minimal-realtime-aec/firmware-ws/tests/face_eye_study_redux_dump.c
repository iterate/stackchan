#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_eye_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    ESR_DUMP_LABEL_HEIGHT = 12,
    ESR_DUMP_TILE_HEIGHT =
        FACE_EYE_STUDY_REDUX_HEIGHT + ESR_DUMP_LABEL_HEIGHT,
    ESR_DUMP_CONTACT_DIVISOR = 4,
    ESR_DUMP_CONTACT_WIDTH =
        FACE_EYE_STUDY_REDUX_WIDTH / ESR_DUMP_CONTACT_DIVISOR,
    ESR_DUMP_CONTACT_HEIGHT =
        FACE_EYE_STUDY_REDUX_HEIGHT / ESR_DUMP_CONTACT_DIVISOR,
};

static const char *const ESR_EXPRESSION_NAMES[FACE_EXPRESSION_COUNT] = {
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

static face_render_key_t esr_preview_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 55U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 22U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_NONE;
    key.audio_level = 18U;
    key.cheek = 17U;
    key.affect_valence = 7;
    key.affect_arousal = 108U;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static uint8_t esr_expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t esr_expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int esr_write_ppm(
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
            esr_expand5((uint16_t)((color >> 11U) & 31U)),
            esr_expand6((uint16_t)((color >> 5U) & 63U)),
            esr_expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fprintf(stderr, "write %s failed\n", path);
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static uint8_t esr_glyph_row(char character, size_t row)
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
    if (character == '+') {
        return row == 3U ? 31U : (row >= 1U && row <= 5U ? 4U : 0U);
    }
    if (character == ':') {
        return row == 2U || row == 5U ? 4U : 0U;
    }
    return 0U;
}

static void esr_label(
    uint16_t *sheet,
    size_t sheet_width,
    size_t tile_x,
    size_t tile_y,
    const char *text)
{
    const size_t origin_x =
        tile_x * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t origin_y = tile_y * ESR_DUMP_TILE_HEIGHT;
    const uint16_t background = 0x1082U;
    const uint16_t foreground = 0xffffU;
    for (size_t y = 0U; y < ESR_DUMP_LABEL_HEIGHT; ++y) {
        for (size_t x = 0U; x < FACE_EYE_STUDY_REDUX_WIDTH; ++x) {
            sheet[(origin_y + y) * sheet_width + origin_x + x] =
                background;
        }
    }
    size_t cursor_x = origin_x + 3U;
    for (size_t character = 0U;
         text[character] != '\0' &&
             cursor_x + 5U < origin_x + FACE_EYE_STUDY_REDUX_WIDTH;
         ++character) {
        for (size_t row = 0U; row < 7U; ++row) {
            const uint8_t bits = esr_glyph_row(text[character], row);
            for (size_t column = 0U; column < 5U; ++column) {
                if ((bits & (uint8_t)(1U << (4U - column))) != 0U) {
                    sheet[
                        (origin_y + 2U + row) * sheet_width +
                        cursor_x + column] = foreground;
                }
            }
        }
        cursor_x += 6U;
    }
}

static void esr_copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t tile_x,
    size_t tile_y,
    const uint16_t *frame)
{
    const size_t origin_x =
        tile_x * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t origin_y =
        tile_y * ESR_DUMP_TILE_HEIGHT + ESR_DUMP_LABEL_HEIGHT;
    for (size_t y = 0U; y < FACE_EYE_STUDY_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet + (origin_y + y) * sheet_width + origin_x,
            frame + y * FACE_EYE_STUDY_REDUX_WIDTH,
            FACE_EYE_STUDY_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static uint32_t esr_open_clock(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    face_eye_study_redux_pose_t pose;
    for (uint32_t time_ms = 0U; time_ms < 10000U; time_ms += 25U) {
        if (!face_eye_study_redux_resolve(
                profile, key, time_ms * 16U, &pose)) {
            return 0U;
        }
        if (pose.blink_q8 == 256 && pose.saccade_active == 0U) {
            return time_ms * 16U;
        }
    }
    return 0U;
}

static int esr_write_sheet(
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
    return esr_write_ppm(path, pixels, width, height);
}

static int esr_write_contact_sheet(
    const char *directory,
    const char *filename,
    const uint16_t *native_sheet,
    size_t columns,
    size_t rows)
{
    const size_t native_width =
        columns * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t contact_width =
        columns * ESR_DUMP_CONTACT_WIDTH;
    const size_t contact_height =
        rows * ESR_DUMP_CONTACT_HEIGHT;
    uint16_t *contact =
        (uint16_t *)calloc(
            contact_width * contact_height,
            sizeof(uint16_t));
    if (contact == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        for (size_t column = 0U; column < columns; ++column) {
            for (size_t y = 0U; y < ESR_DUMP_CONTACT_HEIGHT; ++y) {
                for (size_t x = 0U; x < ESR_DUMP_CONTACT_WIDTH; ++x) {
                    const size_t source_x =
                        column * FACE_EYE_STUDY_REDUX_WIDTH +
                        x * ESR_DUMP_CONTACT_DIVISOR;
                    const size_t source_y =
                        row * ESR_DUMP_TILE_HEIGHT +
                        ESR_DUMP_LABEL_HEIGHT +
                        y * ESR_DUMP_CONTACT_DIVISOR;
                    const size_t destination_x =
                        column * ESR_DUMP_CONTACT_WIDTH + x;
                    const size_t destination_y =
                        row * ESR_DUMP_CONTACT_HEIGHT + y;
                    contact[
                        destination_y * contact_width +
                        destination_x] =
                        native_sheet[
                            source_y * native_width + source_x];
                }
            }
        }
    }
    const int result = esr_write_sheet(
        directory,
        filename,
        contact,
        contact_width,
        contact_height);
    free(contact);
    return result;
}

/*
 * Preserve a readable 5x7 label above each true 40x30 face.  The full native
 * label is intentionally clipped at the 40-pixel tile edge ("16 NEU",
 * "20 -144", and so on): that is enough to identify every ordered column
 * without scaling the face or hiding pixels under an overlay.
 */
static int esr_write_labeled_contact_sheet(
    const char *directory,
    const char *filename,
    const uint16_t *native_sheet,
    size_t columns,
    size_t rows)
{
    const size_t native_width =
        columns * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t tile_height =
        ESR_DUMP_LABEL_HEIGHT + ESR_DUMP_CONTACT_HEIGHT;
    const size_t contact_width =
        columns * ESR_DUMP_CONTACT_WIDTH;
    const size_t contact_height = rows * tile_height;
    uint16_t *contact =
        (uint16_t *)calloc(
            contact_width * contact_height,
            sizeof(uint16_t));
    if (contact == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        for (size_t column = 0U; column < columns; ++column) {
            const size_t native_origin_x =
                column * FACE_EYE_STUDY_REDUX_WIDTH;
            const size_t native_origin_y =
                row * ESR_DUMP_TILE_HEIGHT;
            const size_t contact_origin_x =
                column * ESR_DUMP_CONTACT_WIDTH;
            const size_t contact_origin_y = row * tile_height;
            for (size_t y = 0U; y < ESR_DUMP_LABEL_HEIGHT; ++y) {
                memcpy(
                    contact +
                        (contact_origin_y + y) * contact_width +
                        contact_origin_x,
                    native_sheet +
                        (native_origin_y + y) * native_width +
                        native_origin_x,
                    ESR_DUMP_CONTACT_WIDTH * sizeof(uint16_t));
            }
            for (size_t y = 0U; y < ESR_DUMP_CONTACT_HEIGHT; ++y) {
                for (size_t x = 0U; x < ESR_DUMP_CONTACT_WIDTH; ++x) {
                    const size_t source_x =
                        native_origin_x +
                        x * ESR_DUMP_CONTACT_DIVISOR;
                    const size_t source_y =
                        native_origin_y +
                        ESR_DUMP_LABEL_HEIGHT +
                        y * ESR_DUMP_CONTACT_DIVISOR;
                    contact[
                        (contact_origin_y + ESR_DUMP_LABEL_HEIGHT + y) *
                            contact_width +
                        contact_origin_x + x] =
                        native_sheet[
                            source_y * native_width + source_x];
                }
            }
        }
    }
    const int result = esr_write_sheet(
        directory,
        filename,
        contact,
        contact_width,
        contact_height);
    free(contact);
    return result;
}

static int esr_dump_expressions(const char *directory)
{
    const size_t columns = FACE_EXPRESSION_COUNT;
    const size_t rows = FACE_EYE_STUDY_REDUX_PROFILE_COUNT;
    const size_t width = columns * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t height = rows * ESR_DUMP_TILE_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    face_render_key_t key = esr_preview_key();
    for (size_t row = 0U; row < rows; ++row) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + row);
        const uint32_t clock = esr_open_clock(profile, &key);
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
            char label[64];
            key.stage_expression = expression;
            if (!face_eye_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_EYE_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            snprintf(
                label, sizeof(label), "%u %s",
                (unsigned)(15U + row),
                ESR_EXPRESSION_NAMES[expression]);
            esr_label(sheet, width, expression, row, label);
            esr_copy_frame(sheet, width, expression, row, frame);
        }
    }
    int result = esr_write_sheet(
        directory, "eye-study-expressions-8x11.ppm",
        sheet, width, height);
    if (result == 0) {
        result = esr_write_contact_sheet(
            directory,
            "eye-study-expressions-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    if (result == 0) {
        result = esr_write_labeled_contact_sheet(
            directory,
            "eye-study-expressions-labeled-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    free(sheet);
    return result;
}

static void esr_speech_pose(
    size_t column,
    face_render_key_t *key,
    const char **label)
{
    static const char *const LABELS[12] = {
        "REST",
        "LISTEN",
        "THINK",
        "ANTICIPATE",
        "ACTIVE-32",
        "ACTIVE-96",
        "ACTIVE-180",
        "ACTIVE-255",
        "SETTLE",
        "LOW-ATTN",
        "LOOK-LEFT",
        "LOOK-UP-R",
    };
    *key = esr_preview_key();
    *label = LABELS[column];
    if (column == 1U) {
        key->controls.expression = FACE_ACTIVITY_LISTENING;
        key->attention = 255U;
    } else if (column == 2U) {
        key->controls.expression = FACE_ACTIVITY_THINKING;
        key->attention = 178U;
        key->stage_expression = FACE_EXPRESSION_THOUGHTFUL;
    } else if (column >= 3U && column <= 8U) {
        key->controls.expression = FACE_ACTIVITY_SPEAKING;
        key->controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key->speech_phase =
            column == 3U ? FACE_SPEECH_STARTING :
            column == 8U ? FACE_SPEECH_ENDING :
            FACE_SPEECH_ACTIVE;
        static const uint8_t LEVELS[6] = {
            96U, 32U, 96U, 180U, 255U, 58U,
        };
        key->audio_level = LEVELS[column - 3U];
    } else if (column == 9U) {
        key->attention = 18U;
    } else if (column == 10U) {
        key->controls.look_x = -118;
        key->head_yaw = -34;
    } else if (column == 11U) {
        key->controls.look_x = 91;
        key->controls.look_y = -105;
        key->head_pitch = -31;
    }
}

static int esr_dump_speech_attention(const char *directory)
{
    const size_t columns = 12U;
    const size_t rows = FACE_EYE_STUDY_REDUX_PROFILE_COUNT;
    const size_t width = columns * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t height = rows * ESR_DUMP_TILE_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + row);
        face_render_key_t clock_key = esr_preview_key();
        const uint32_t clock = esr_open_clock(profile, &clock_key);
        for (size_t column = 0U; column < columns; ++column) {
            face_render_key_t key;
            const char *label;
            uint16_t frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
            char full_label[64];
            esr_speech_pose(column, &key, &label);
            if (!face_eye_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_EYE_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            snprintf(
                full_label, sizeof(full_label), "%u %s",
                (unsigned)(15U + row), label);
            esr_label(sheet, width, column, row, full_label);
            esr_copy_frame(sheet, width, column, row, frame);
        }
    }
    int result = esr_write_sheet(
        directory, "eye-study-speech-attention-8x12.ppm",
        sheet, width, height);
    if (result == 0) {
        result = esr_write_contact_sheet(
            directory,
            "eye-study-speech-attention-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    if (result == 0) {
        result = esr_write_labeled_contact_sheet(
            directory,
            "eye-study-speech-attention-labeled-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    free(sheet);
    return result;
}

static uint32_t esr_find_blink_minimum(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    uint32_t best_clock = 0U;
    int32_t best_blink = 257;
    for (uint32_t time_ms = 0U; time_ms <= 6500U; time_ms += 5U) {
        face_eye_study_redux_pose_t pose;
        if (!face_eye_study_redux_resolve(
                profile, key, time_ms * 16U, &pose)) {
            return 0U;
        }
        if (pose.blink_q8 < best_blink) {
            best_blink = pose.blink_q8;
            best_clock = time_ms * 16U;
        }
    }
    return best_clock;
}

static uint32_t esr_find_saccade_start(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *key)
{
    bool previous_active = true;
    uint32_t best_clock = 0U;
    int32_t best_motion = -1;
    for (uint32_t time_ms = 0U; time_ms <= 30000U; time_ms += 5U) {
        face_eye_study_redux_pose_t pose;
        if (!face_eye_study_redux_resolve(
                profile, key, time_ms * 16U, &pose)) {
            return 0U;
        }
        if (!previous_active && pose.saccade_active != 0U) {
            bool clear_blink = time_ms >= 160U;
            for (int32_t relative_ms = -160;
                 clear_blink && relative_ms <= 208;
                 relative_ms += 16) {
                face_eye_study_redux_pose_t window_pose;
                const uint32_t clock =
                    (uint32_t)((int32_t)time_ms + relative_ms) * 16U;
                clear_blink =
                    face_eye_study_redux_resolve(
                        profile, key, clock, &window_pose) &&
                    window_pose.blink_q8 == 256;
            }
            if (clear_blink) {
                face_eye_study_redux_pose_t before;
                face_eye_study_redux_pose_t after;
                const uint32_t before_clock = (time_ms - 144U) * 16U;
                const uint32_t after_clock = (time_ms + 176U) * 16U;
                if (!face_eye_study_redux_resolve(
                        profile, key, before_clock, &before) ||
                    !face_eye_study_redux_resolve(
                        profile, key, after_clock, &after)) {
                    return 0U;
                }
                int32_t dx = before.gaze_x_q8 - after.gaze_x_q8;
                int32_t dy = before.gaze_y_q8 - after.gaze_y_q8;
                dx = dx < 0 ? -dx : dx;
                dy = dy < 0 ? -dy : dy;
                if (dx + dy > best_motion) {
                    best_motion = dx + dy;
                    best_clock = time_ms * 16U;
                }
            }
        }
        previous_active = pose.saccade_active != 0U;
    }
    return best_clock;
}

static int esr_dump_chronology(
    const char *directory,
    bool blink)
{
    const size_t columns = blink ? 24U : 22U;
    const size_t rows = FACE_EYE_STUDY_REDUX_PROFILE_COUNT;
    const size_t width = columns * FACE_EYE_STUDY_REDUX_WIDTH;
    const size_t height = rows * ESR_DUMP_TILE_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t row = 0U; row < rows; ++row) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(15U + row);
        face_render_key_t key = esr_preview_key();
        key.attention = blink ? 224U : 72U;
        const uint32_t event_clock =
            blink
                ? esr_find_blink_minimum(profile, &key)
                : esr_find_saccade_start(profile, &key);
        const int32_t step_ms = blink ? 20 : 16;
        const int32_t zero_column = blink ? 11 : 9;
        for (size_t column = 0U; column < columns; ++column) {
            const int32_t relative_ms =
                ((int32_t)column - zero_column) * step_ms;
            const int64_t signed_clock =
                (int64_t)event_clock + (int64_t)relative_ms * 16;
            const uint32_t clock =
                signed_clock > 0 ? (uint32_t)signed_clock : 0U;
            uint16_t frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
            char label[64];
            if (!face_eye_study_redux_render(
                    profile, &key, clock, frame,
                    FACE_EYE_STUDY_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            snprintf(
                label, sizeof(label), "%u T%+dMS",
                (unsigned)(15U + row), relative_ms);
            esr_label(sheet, width, column, row, label);
            esr_copy_frame(sheet, width, column, row, frame);
        }
    }
    int result = esr_write_sheet(
        directory,
        blink
            ? "eye-study-blink-chronology-8x24.ppm"
            : "eye-study-saccade-chronology-8x22.ppm",
        sheet, width, height);
    if (result == 0) {
        result = esr_write_contact_sheet(
            directory,
            blink
                ? "eye-study-blink-contact-40x30.ppm"
                : "eye-study-saccade-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    if (result == 0) {
        result = esr_write_labeled_contact_sheet(
            directory,
            blink
                ? "eye-study-blink-labeled-contact-40x30.ppm"
                : "eye-study-saccade-labeled-contact-40x30.ppm",
            sheet,
            columns,
            rows);
    }
    free(sheet);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (esr_dump_expressions(argv[1]) != 0 ||
        esr_dump_speech_attention(argv[1]) != 0 ||
        esr_dump_chronology(argv[1], true) != 0 ||
        esr_dump_chronology(argv[1], false) != 0) {
        return 1;
    }
    printf(
        "wrote eye-study expression, speech/attention, blink, "
        "and saccade sheets to %s\n",
        argv[1]);
    return 0;
}
