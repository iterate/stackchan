#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_abstract_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    AR_DUMP_EXPRESSION_COUNT = 11,
    AR_DUMP_TEMPORAL_FRAMES = 16,
    AR_DUMP_CONTACT_DIVISOR = 4,
};

static uint8_t ar_dump_expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t ar_dump_expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int ar_dump_write_ppm(
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
            ar_dump_expand5(
                (uint16_t)((color >> 11U) & 31U)),
            ar_dump_expand6(
                (uint16_t)((color >> 5U) & 63U)),
            ar_dump_expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void ar_dump_copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < FACE_ABSTRACT_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_ABSTRACT_REDUX_HEIGHT + y) *
                    sheet_width +
                column * FACE_ABSTRACT_REDUX_WIDTH,
            frame + y * FACE_ABSTRACT_REDUX_WIDTH,
            FACE_ABSTRACT_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static uint16_t *ar_dump_make_contact(
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    const size_t width =
        native_width / AR_DUMP_CONTACT_DIVISOR;
    const size_t height =
        native_height / AR_DUMP_CONTACT_DIVISOR;
    uint16_t *contact =
        calloc(width * height, sizeof(uint16_t));
    if (contact == NULL) {
        return NULL;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            const size_t source_x =
                x * AR_DUMP_CONTACT_DIVISOR +
                AR_DUMP_CONTACT_DIVISOR / 2U;
            const size_t source_y =
                y * AR_DUMP_CONTACT_DIVISOR +
                AR_DUMP_CONTACT_DIVISOR / 2U;
            contact[y * width + x] =
                native[source_y * native_width + source_x];
        }
    }
    return contact;
}

static int ar_dump_write_pair(
    const char *directory,
    const char *native_name,
    const char *contact_name,
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    char path[1024];
    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        directory,
        native_name);
    if (ar_dump_write_ppm(
            path, native, native_width, native_height) != 0) {
        return 1;
    }
    uint16_t *contact = ar_dump_make_contact(
        native, native_width, native_height);
    if (contact == NULL) {
        return 1;
    }
    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        directory,
        contact_name);
    const int result = ar_dump_write_ppm(
        path,
        contact,
        native_width / AR_DUMP_CONTACT_DIVISOR,
        native_height / AR_DUMP_CONTACT_DIVISOR);
    free(contact);
    return result;
}

static face_render_key_t ar_dump_expression_key(
    uint8_t expression)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 40U;
    key.controls.mouth_width = 146U;
    key.controls.mouth_round = 20U;
    key.controls.mouth_press = 48U;
    key.controls.eye_left_open = 220U;
    key.controls.eye_right_open = 216U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_SIL;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.cheek = 18U;
    key.affect_arousal = 118U;
    key.expression_weight = 255U;
    key.attention = 214U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static face_render_key_t ar_dump_temporal_key(size_t frame)
{
    static const uint8_t start_drive[2] = {
        48U, 176U,
    };
    static const uint8_t end_drive[3] = {
        192U, 104U, 32U,
    };
    static const uint8_t visemes[3] = {
        FACE_VISEME_AA,
        FACE_VISEME_E,
        FACE_VISEME_O,
    };
    static const uint8_t open[3] = {
        204U, 108U, 184U,
    };
    static const uint8_t width[3] = {
        162U, 232U, 92U,
    };
    static const uint8_t round[3] = {
        18U, 8U, 244U,
    };
    static const int8_t gaze_x[3] = {
        -46, 18, 58,
    };
    static const int8_t gaze_y[3] = {
        -28, -8, 24,
    };
    face_render_key_t key =
        ar_dump_expression_key(FACE_EXPRESSION_WARM);
    if (frame == 0U || frame == AR_DUMP_TEMPORAL_FRAMES - 1U) {
        return key;
    }
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 126U;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 48U;
    key.tongue = 74U;
    key.cheek = 52U;
    key.audio_level = frame % 2U == 0U ? 250U : 8U;
    if (frame <= 2U) {
        const size_t step = frame - 1U;
        const uint8_t drive = start_drive[step];
        key.speech_phase = FACE_SPEECH_STARTING;
        key.viseme = FACE_VISEME_AA;
        key.viseme_weight = drive;
        key.controls.mouth_open = drive;
        key.controls.mouth_width = 166U;
        key.controls.mouth_round = 22U;
        key.controls.look_x =
            (int8_t)(-16 - (int)step * 12);
        key.controls.look_y =
            (int8_t)(-8 - (int)step * 8);
        key.controls.eye_left_open =
            (uint8_t)(202U + step * 18U);
        key.controls.eye_right_open =
            (uint8_t)(198U + step * 15U);
        return key;
    }
    if (frame <= 11U) {
        const size_t section = (frame - 3U) / 3U;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.viseme = visemes[section];
        key.viseme_secondary = visemes[section];
        key.viseme_weight = 238U;
        key.controls.mouth_open = open[section];
        key.controls.mouth_width = width[section];
        key.controls.mouth_round = round[section];
        key.controls.look_x = gaze_x[section];
        key.controls.look_y = gaze_y[section];
        key.controls.eye_left_open =
            section == 1U ? 188U : 228U;
        key.controls.eye_right_open =
            section == 2U ? 194U : 218U;
        key.stage_expression =
            section == 0U ? FACE_EXPRESSION_WARM :
            section == 1U ? FACE_EXPRESSION_DETERMINED :
            FACE_EXPRESSION_JOY;
        return key;
    }
    const size_t step = frame - 12U;
    const uint8_t drive = end_drive[step];
    key.speech_phase = FACE_SPEECH_ENDING;
    key.viseme = FACE_VISEME_O;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.viseme_weight = drive;
    key.controls.mouth_open = drive;
    key.controls.mouth_width = 112U;
    key.controls.mouth_round = 212U;
    key.controls.look_x =
        (int8_t)(30 - (int)step * 15);
    key.controls.look_y =
        (int8_t)(14 - (int)step * 7);
    key.controls.eye_left_open =
        (uint8_t)(224U - step * 8U);
    key.controls.eye_right_open =
        (uint8_t)(210U - step * 5U);
    return key;
}

static int ar_dump_expressions(const char *directory)
{
    const size_t width =
        AR_DUMP_EXPRESSION_COUNT * FACE_ABSTRACT_REDUX_WIDTH;
    const size_t height =
        FACE_ABSTRACT_REDUX_COUNT * FACE_ABSTRACT_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U;
         style < FACE_ABSTRACT_REDUX_COUNT;
         ++style) {
        for (uint8_t expression = 0U;
             expression < AR_DUMP_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
            const face_render_key_t key =
                ar_dump_expression_key(expression);
            if (!face_abstract_redux_render(
                    (face_abstract_redux_style_t)style,
                    &key,
                    0U,
                    frame,
                    FACE_ABSTRACT_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            ar_dump_copy_frame(
                sheet, width, expression, style, frame);
        }
    }
    const int result = ar_dump_write_pair(
        directory,
        "abstract-redux-expressions-native-5x11.ppm",
        "abstract-redux-expressions-40x30-5x11.ppm",
        sheet,
        width,
        height);
    free(sheet);
    return result;
}

static int ar_dump_temporal(const char *directory)
{
    const size_t width =
        AR_DUMP_TEMPORAL_FRAMES * FACE_ABSTRACT_REDUX_WIDTH;
    const size_t height =
        FACE_ABSTRACT_REDUX_COUNT * FACE_ABSTRACT_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U;
         style < FACE_ABSTRACT_REDUX_COUNT;
         ++style) {
        for (size_t frame_index = 0U;
             frame_index < AR_DUMP_TEMPORAL_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
            const face_render_key_t key =
                ar_dump_temporal_key(frame_index);
            if (!face_abstract_redux_render(
                    (face_abstract_redux_style_t)style,
                    &key,
                    (uint32_t)frame_index * 533U,
                    frame,
                    FACE_ABSTRACT_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            ar_dump_copy_frame(
                sheet,
                width,
                frame_index,
                style,
                frame);
        }
    }
    const int result = ar_dump_write_pair(
        directory,
        "abstract-redux-temporal-native-5x16.ppm",
        "abstract-redux-temporal-40x30-5x16.ppm",
        sheet,
        width,
        height);
    free(sheet);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (ar_dump_expressions(argv[1]) != 0 ||
        ar_dump_temporal(argv[1]) != 0) {
        return 1;
    }
    puts("face_abstract_redux_actors_dump: wrote contact sheets");
    return 0;
}
