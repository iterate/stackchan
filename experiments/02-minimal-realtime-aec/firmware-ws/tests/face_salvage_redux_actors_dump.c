#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_pose.h"
#include "face_salvage_redux_actors.h"
#include "face_stage.h"

enum {
    SR_DUMP_EXPRESSION_COUNT = 11,
    SR_DUMP_VISEME_COUNT = 15,
    SR_DUMP_TEMPORAL_FRAMES = 16,
    SR_DUMP_CONTACT_WIDTH = 80,
    SR_DUMP_CONTACT_HEIGHT = 60,
    SR_DUMP_DEVICE_WIDTH = 40,
    SR_DUMP_DEVICE_HEIGHT = 30,
};

static const char *const SR_DUMP_EXPRESSION_NAMES[] = {
    "neutral",
    "warm",
    "joy",
    "concern",
    "surprise",
    "thoughtful",
    "skeptical",
    "determined",
    "sleepy",
    "excited",
    "embarrassed",
};

static const char *const SR_DUMP_VISEME_NAMES[] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

static const uint8_t SR_DUMP_TEMPORAL_VISEMES[] = {
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
    FACE_VISEME_AA,
    FACE_VISEME_AA,
    FACE_VISEME_E,
    FACE_VISEME_I,
    FACE_VISEME_O,
    FACE_VISEME_U,
    FACE_VISEME_PP,
    FACE_VISEME_FF,
    FACE_VISEME_TH,
    FACE_VISEME_SS,
    FACE_VISEME_RR,
    FACE_VISEME_CH,
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
};

static uint8_t sr_dump_expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t sr_dump_expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int sr_dump_write_ppm(
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
            sr_dump_expand5((uint16_t)((color >> 11U) & 31U)),
            sr_dump_expand6((uint16_t)((color >> 5U) & 63U)),
            sr_dump_expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void sr_dump_copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < FACE_SALVAGE_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_SALVAGE_REDUX_HEIGHT + y) * sheet_width +
                column * FACE_SALVAGE_REDUX_WIDTH,
            frame + y * FACE_SALVAGE_REDUX_WIDTH,
            FACE_SALVAGE_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static int sr_dump_write_contact(
    const char *directory,
    const char *stem,
    const uint16_t *native,
    size_t columns,
    size_t rows,
    size_t cell_width,
    size_t cell_height,
    size_t sample_stride,
    const char *suffix)
{
    const size_t native_width = columns * FACE_SALVAGE_REDUX_WIDTH;
    const size_t contact_width = columns * cell_width;
    const size_t contact_height = rows * cell_height;
    char path[1024];
    uint16_t *contact = calloc(
        contact_width * contact_height, sizeof(uint16_t));
    if (contact == NULL) {
        return 1;
    }
    /*
     * Deliberately use one center sample per source block.  This is a harsh
     * contact-size readability test, not a smoothing pass that can conceal
     * one-pixel clipping or detached contours.
     */
    for (size_t row = 0U; row < rows; ++row) {
        for (size_t column = 0U; column < columns; ++column) {
            for (size_t y = 0U; y < cell_height; ++y) {
                for (size_t x = 0U; x < cell_width; ++x) {
                    const size_t source_x =
                        column * FACE_SALVAGE_REDUX_WIDTH +
                        x * sample_stride + sample_stride / 2U;
                    const size_t source_y =
                        row * FACE_SALVAGE_REDUX_HEIGHT +
                        y * sample_stride + sample_stride / 2U;
                    const size_t destination_x =
                        column * cell_width + x;
                    const size_t destination_y =
                        row * cell_height + y;
                    contact[
                        destination_y * contact_width + destination_x] =
                        native[source_y * native_width + source_x];
                }
            }
        }
    }
    snprintf(
        path,
        sizeof(path),
        "%s/%s-%s.ppm",
        directory,
        stem,
        suffix);
    const int result = sr_dump_write_ppm(
        path, contact, contact_width, contact_height);
    free(contact);
    return result;
}

static int sr_dump_write_review_set(
    const char *directory,
    const char *stem,
    const uint16_t *native,
    size_t columns,
    size_t rows)
{
    const size_t native_width = columns * FACE_SALVAGE_REDUX_WIDTH;
    const size_t native_height = rows * FACE_SALVAGE_REDUX_HEIGHT;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, stem);
    if (sr_dump_write_ppm(
            path, native, native_width, native_height) != 0) {
        return 1;
    }
    if (sr_dump_write_contact(
            directory,
            stem,
            native,
            columns,
            rows,
            SR_DUMP_CONTACT_WIDTH,
            SR_DUMP_CONTACT_HEIGHT,
            2U,
            "contact-80x60") != 0) {
        return 1;
    }
    return sr_dump_write_contact(
        directory,
        stem,
        native,
        columns,
        rows,
        SR_DUMP_DEVICE_WIDTH,
        SR_DUMP_DEVICE_HEIGHT,
        4U,
        "device-40x30");
}

static face_render_key_t sr_dump_base_key(bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 142U : 6U;
    key.controls.mouth_width = 178U;
    key.controls.mouth_round = 38U;
    key.controls.mouth_press = speaking ? 8U : 224U;
    key.controls.mouth_teeth = 132U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 240U;
    key.controls.look_x = 2;
    key.controls.look_y = -2;
    key.controls.brow = 4;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.viseme = speaking ? FACE_VISEME_AA : FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = speaking ? 240U : 0U;
    key.audio_level = speaking ? 142U : 5U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = speaking ? FACE_VISEME_E : FACE_VISEME_SIL;
    key.viseme_blend = 0U;
    key.speech_phase =
        speaking ? FACE_SPEECH_ACTIVE : FACE_SPEECH_IDLE;
    key.tongue = speaking ? 82U : 0U;
    key.cheek = 30U;
    key.affect_valence = 4;
    key.affect_arousal = speaking ? 150U : 116U;
    key.expression_weight = 255U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static face_render_key_t sr_dump_temporal_key(size_t frame)
{
    const bool active = frame != 0U && frame != 15U;
    face_render_key_t key = sr_dump_base_key(active);
    key.viseme = SR_DUMP_TEMPORAL_VISEMES[frame];
    key.viseme_secondary = SR_DUMP_TEMPORAL_VISEMES[
        frame + 1U < SR_DUMP_TEMPORAL_FRAMES ? frame + 1U : frame];
    key.viseme_blend = (uint8_t)((frame * 29U) & 127U);
    key.controls.look_x = (int8_t)((int)frame * 4 - 34);
    key.controls.look_y = (int8_t)(
        frame < 9U ? -8 + (int)frame * 2
                   : 8 - ((int)frame - 9) * 2);
    if (frame == 0U || frame == 15U) {
        key.controls.flags = 0U;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        key.speech_phase = FACE_SPEECH_IDLE;
        key.controls.mouth_open = 4U;
        key.controls.mouth_press = 230U;
        key.viseme_weight = 0U;
        key.audio_level = 4U;
        key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    } else if (frame <= 2U) {
        key.speech_phase = FACE_SPEECH_STARTING;
        key.controls.mouth_open = frame == 1U ? 20U : 68U;
        key.controls.mouth_press = frame == 1U ? 188U : 104U;
        key.viseme_weight = frame == 1U ? 24U : 112U;
        key.audio_level = frame == 1U ? 22U : 72U;
        key.stage_expression = FACE_EXPRESSION_WARM;
    } else if (frame >= 13U) {
        key.speech_phase = FACE_SPEECH_ENDING;
        key.controls.mouth_open = frame == 13U ? 74U : 24U;
        key.controls.mouth_press = frame == 13U ? 92U : 190U;
        key.viseme_weight = frame == 13U ? 126U : 32U;
        key.audio_level = frame == 13U ? 66U : 18U;
        key.stage_expression = FACE_EXPRESSION_WARM;
    } else {
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.controls.mouth_open =
            (uint8_t)(42U + (frame * 53U) % 184U);
        key.controls.mouth_width =
            (uint8_t)(112U + (frame * 31U) % 132U);
        key.controls.mouth_round =
            (uint8_t)((frame * 67U) & 255U);
        key.audio_level =
            (uint8_t)(92U + (frame * 37U) % 145U);
        key.stage_expression =
            frame < 10U ? FACE_EXPRESSION_WARM
                        : frame == 10U ? FACE_EXPRESSION_EXCITED
                                       : FACE_EXPRESSION_JOY;
    }
    return key;
}

static int sr_dump_expressions(const char *directory)
{
    const size_t columns = SR_DUMP_EXPRESSION_COUNT;
    const size_t rows = FACE_SALVAGE_REDUX_COUNT;
    const size_t width = columns * FACE_SALVAGE_REDUX_WIDTH;
    const size_t height = rows * FACE_SALVAGE_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U; style < rows; ++style) {
        for (uint8_t expression = 0U;
             expression < SR_DUMP_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            face_render_key_t key = sr_dump_base_key(false);
            key.stage_expression = expression;
            if (!face_salvage_redux_render(
                    (face_salvage_redux_style_t)style,
                    &key,
                    5064U,
                    frame,
                    FACE_SALVAGE_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            sr_dump_copy_frame(sheet, width, expression, style, frame);
        }
    }
    const int result = sr_dump_write_review_set(
        directory,
        "salvage-redux__6-actors__11-emotions__quiet",
        sheet,
        columns,
        rows);
    free(sheet);
    return result;
}

static int sr_dump_temporal(const char *directory)
{
    const size_t columns = SR_DUMP_TEMPORAL_FRAMES;
    const size_t rows = FACE_SALVAGE_REDUX_COUNT;
    const size_t width = columns * FACE_SALVAGE_REDUX_WIDTH;
    const size_t height = rows * FACE_SALVAGE_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U; style < rows; ++style) {
        for (size_t temporal = 0U;
             temporal < SR_DUMP_TEMPORAL_FRAMES;
             ++temporal) {
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            const face_render_key_t key =
                sr_dump_temporal_key(temporal);
            if (!face_salvage_redux_render(
                    (face_salvage_redux_style_t)style,
                    &key,
                    (uint32_t)temporal * 533U,
                    frame,
                    FACE_SALVAGE_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            sr_dump_copy_frame(sheet, width, temporal, style, frame);
        }
    }
    const int result = sr_dump_write_review_set(
        directory,
        "salvage-redux__6-actors__16f-speech__2-anticipation-2-settle",
        sheet,
        columns,
        rows);
    free(sheet);
    return result;
}

static int sr_dump_visemes(const char *directory)
{
    const size_t columns = SR_DUMP_VISEME_COUNT;
    const size_t rows = FACE_SALVAGE_REDUX_COUNT;
    const size_t width = columns * FACE_SALVAGE_REDUX_WIDTH;
    const size_t height = rows * FACE_SALVAGE_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U; style < rows; ++style) {
        for (uint8_t viseme = 0U;
             viseme < SR_DUMP_VISEME_COUNT;
             ++viseme) {
            uint16_t frame[FACE_SALVAGE_REDUX_PIXEL_COUNT];
            face_render_key_t key = sr_dump_base_key(true);
            key.stage_expression = FACE_EXPRESSION_WARM;
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            if (!face_salvage_redux_render(
                    (face_salvage_redux_style_t)style,
                    &key,
                    18231U,
                    frame,
                    FACE_SALVAGE_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            sr_dump_copy_frame(sheet, width, viseme, style, frame);
        }
    }
    const int result = sr_dump_write_review_set(
        directory,
        "salvage-redux__6-actors__15-ovr-visemes__warm-active",
        sheet,
        columns,
        rows);
    free(sheet);
    return result;
}

static int sr_dump_manifest(const char *directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.txt", directory);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(
        file,
        "Every native cell is 160x120 RGB565. Review cells are unsmoothed "
        "center samples at 80x60 and exact device-contact 40x30.\n"
        "Rows (style, legacy ID, grammar):\n");
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        face_salvage_redux_info_t info;
        if (!face_salvage_redux_info(
                (face_salvage_redux_style_t)raw, &info)) {
            fclose(file);
            return 1;
        }
        fprintf(
            file,
            "  %zu legacy=%u grammar=%u %s — %s%s%s\n",
            raw,
            info.legacy_profile_id,
            info.grammar,
            info.slug,
            info.name,
            info.mouthless ? " [mouthless]" : "",
            info.pixel_hybrid ? " [pixel/vector hybrid]" : "");
    }
    fprintf(file, "Expression columns:\n");
    for (size_t index = 0U;
         index < SR_DUMP_EXPRESSION_COUNT;
         ++index) {
        fprintf(
            file,
            "  %zu %s\n",
            index,
            SR_DUMP_EXPRESSION_NAMES[index]);
    }
    fprintf(file, "Viseme columns:\n");
    for (size_t index = 0U; index < SR_DUMP_VISEME_COUNT; ++index) {
        fprintf(
            file,
            "  %zu %s\n",
            index,
            SR_DUMP_VISEME_NAMES[index]);
    }
    fprintf(
        file,
        "Temporal columns: 0 rest; 1-2 anticipation; 3-12 active; "
        "13-14 settle; 15 rest. Frames are 533 PCM samples apart.\n");
    return fclose(file) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (sr_dump_expressions(argv[1]) != 0 ||
        sr_dump_temporal(argv[1]) != 0 ||
        sr_dump_visemes(argv[1]) != 0 ||
        sr_dump_manifest(argv[1]) != 0) {
        return 1;
    }
    printf("salvage redux artifacts written to %s\n", argv[1]);
    return 0;
}
