#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_pose.h"
#include "face_robot_redux_actors.h"
#include "face_stage.h"

enum {
    RR_EXPRESSION_COUNT = 11,
    RR_VISEME_COUNT = 15,
    RR_SPEECH_FRAMES = 16,
    RR_CONTACT_DIVISOR = 4,
};

static const char *const RR_EXPRESSION_NAMES[RR_EXPRESSION_COUNT] = {
    "neutral", "warm", "joy", "concern", "surprise", "thoughtful",
    "skeptical", "determined", "sleepy", "excited", "embarrassed",
};

static const char *const RR_VISEME_NAMES[RR_VISEME_COUNT] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

static const uint8_t RR_SPEECH_VISEMES[RR_SPEECH_FRAMES] = {
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
    FACE_VISEME_RR,
    FACE_VISEME_CH,
    FACE_VISEME_AA,
    FACE_VISEME_SIL,
    FACE_VISEME_SIL,
};

static const uint8_t RR_SPEECH_AUDIO[RR_SPEECH_FRAMES] = {
    4U, 42U, 92U, 128U, 164U, 196U, 144U, 108U,
    186U, 156U, 124U, 174U, 116U, 72U, 30U, 4U,
};

static const uint8_t RR_SPEECH_OPEN[RR_SPEECH_FRAMES] = {
    4U, 24U, 86U, 108U, 72U, 190U, 118U, 6U,
    36U, 72U, 46U, 112U, 76U, 48U, 18U, 4U,
};

static uint8_t rr_expand5(uint16_t value)
{
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t rr_expand6(uint16_t value)
{
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static int rr_write_ppm(
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
            rr_expand5((uint16_t)((color >> 11U) & 31U)),
            rr_expand6((uint16_t)((color >> 5U) & 63U)),
            rr_expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void rr_copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t column,
    size_t row,
    const uint16_t *frame)
{
    for (size_t y = 0U; y < FACE_ROBOT_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_ROBOT_REDUX_HEIGHT + y) * sheet_width +
                column * FACE_ROBOT_REDUX_WIDTH,
            frame + y * FACE_ROBOT_REDUX_WIDTH,
            FACE_ROBOT_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static int rr_write_contact_sheet(
    const char *directory,
    const char *name,
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    const size_t width = native_width / RR_CONTACT_DIVISOR;
    const size_t height = native_height / RR_CONTACT_DIVISOR;
    uint16_t *contact = calloc(width * height, sizeof(uint16_t));
    if (contact == NULL) {
        return 1;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            const size_t source_x =
                x * RR_CONTACT_DIVISOR + RR_CONTACT_DIVISOR / 2U;
            const size_t source_y =
                y * RR_CONTACT_DIVISOR + RR_CONTACT_DIVISOR / 2U;
            contact[y * width + x] =
                native[source_y * native_width + source_x];
        }
    }
    char path[1024];
    snprintf(
        path,
        sizeof(path),
        "%s/%s-contact.ppm",
        directory,
        name);
    const int result =
        rr_write_ppm(path, contact, width, height);
    free(contact);
    return result;
}

static face_render_key_t rr_preview_key(
    uint8_t expression,
    bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 138U : 4U;
    key.controls.mouth_width = 178U;
    key.controls.mouth_round = 38U;
    key.controls.mouth_press = speaking ? 8U : 180U;
    key.controls.mouth_teeth = 132U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 240U;
    key.controls.look_x = 3;
    key.controls.look_y = -2;
    key.controls.brow = 4;
    key.controls.expression =
        speaking ? FACE_ACTIVITY_SPEAKING : FACE_ACTIVITY_LISTENING;
    key.controls.flags =
        speaking ? FACE_KEYFRAME_FLAG_SPEAKING : 0U;
    key.viseme = speaking ? FACE_VISEME_AA : FACE_VISEME_SIL;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = speaking ? 240U : 0U;
    key.audio_level = speaking ? 142U : 4U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
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
    key.stage_expression = expression;
    return key;
}

static face_render_key_t rr_speech_key(size_t frame)
{
    face_render_key_t key =
        rr_preview_key(FACE_EXPRESSION_WARM, true);
    key.controls.mouth_open = RR_SPEECH_OPEN[frame];
    key.controls.mouth_width =
        (uint8_t)(142U + (frame * 17U) % 86U);
    key.controls.mouth_round =
        (uint8_t)((frame * 43U) & 255U);
    key.audio_level = RR_SPEECH_AUDIO[frame];
    key.viseme = RR_SPEECH_VISEMES[frame];
    key.viseme_secondary =
        RR_SPEECH_VISEMES[
            frame + 1U < RR_SPEECH_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        frame == 0U || frame == RR_SPEECH_FRAMES - 1U
        ? 0U
        : 64U;
    key.controls.look_x =
        (int8_t)(frame < 8U ? -8 + (int)frame * 2
                            : 8 - ((int)frame - 8) * 2);
    key.controls.look_y =
        (int8_t)(frame < 8U ? -3 + (int)frame
                            : 5 - ((int)frame - 8));
    key.speech_phase =
        frame == 0U || frame == 15U ? FACE_SPEECH_IDLE
        : frame == 1U ? FACE_SPEECH_STARTING
        : frame >= 13U ? FACE_SPEECH_ENDING
        : FACE_SPEECH_ACTIVE;
    if (frame == 0U || frame == 15U) {
        key.controls.flags = 0U;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        key.viseme_weight = 0U;
    }
    key.stage_expression =
        frame >= 10U && frame <= 12U
        ? FACE_EXPRESSION_JOY
        : FACE_EXPRESSION_WARM;
    return key;
}

static int rr_dump_expression_sheets(const char *directory)
{
    const size_t width =
        RR_EXPRESSION_COUNT * FACE_ROBOT_REDUX_WIDTH;
    const size_t total_height =
        FACE_ROBOT_REDUX_COUNT * FACE_ROBOT_REDUX_HEIGHT;
    uint16_t *row = calloc(
        width * FACE_ROBOT_REDUX_HEIGHT, sizeof(uint16_t));
    uint16_t *all = calloc(width * total_height, sizeof(uint16_t));
    if (row == NULL || all == NULL) {
        free(row);
        free(all);
        return 1;
    }
    for (size_t style = 0U; style < FACE_ROBOT_REDUX_COUNT; ++style) {
        memset(
            row,
            0,
            width * FACE_ROBOT_REDUX_HEIGHT * sizeof(uint16_t));
        for (uint8_t expression = 0U;
             expression < RR_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            const face_render_key_t key =
                rr_preview_key(expression, false);
            if (!face_robot_redux_render(
                    (face_robot_redux_style_t)style,
                    &key,
                    5064U,
                    frame,
                    FACE_ROBOT_REDUX_PIXEL_COUNT)) {
                free(row);
                free(all);
                return 1;
            }
            rr_copy_frame(row, width, expression, 0U, frame);
            rr_copy_frame(all, width, expression, style, frame);
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/%02zu-%s-expressions.ppm",
            directory,
            style,
            face_robot_redux_slug((face_robot_redux_style_t)style));
        if (rr_write_ppm(
                path, row, width, FACE_ROBOT_REDUX_HEIGHT) != 0) {
            free(row);
            free(all);
            return 1;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-expressions.ppm", directory);
    int result = rr_write_ppm(path, all, width, total_height);
    if (result == 0) {
        result = rr_write_contact_sheet(
            directory,
            "all-expressions",
            all,
            width,
            total_height);
    }
    free(row);
    free(all);
    return result;
}

static int rr_dump_speech_sheets(const char *directory)
{
    const size_t width =
        RR_SPEECH_FRAMES * FACE_ROBOT_REDUX_WIDTH;
    const size_t total_height =
        FACE_ROBOT_REDUX_COUNT * FACE_ROBOT_REDUX_HEIGHT;
    uint16_t *row = calloc(
        width * FACE_ROBOT_REDUX_HEIGHT, sizeof(uint16_t));
    uint16_t *all = calloc(width * total_height, sizeof(uint16_t));
    if (row == NULL || all == NULL) {
        free(row);
        free(all);
        return 1;
    }
    for (size_t style = 0U; style < FACE_ROBOT_REDUX_COUNT; ++style) {
        memset(
            row,
            0,
            width * FACE_ROBOT_REDUX_HEIGHT * sizeof(uint16_t));
        for (size_t frame_index = 0U;
             frame_index < RR_SPEECH_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            const face_render_key_t key =
                rr_speech_key(frame_index);
            if (!face_robot_redux_render(
                    (face_robot_redux_style_t)style,
                    &key,
                    (uint32_t)frame_index * 533U,
                    frame,
                    FACE_ROBOT_REDUX_PIXEL_COUNT)) {
                free(row);
                free(all);
                return 1;
            }
            rr_copy_frame(row, width, frame_index, 0U, frame);
            rr_copy_frame(all, width, frame_index, style, frame);
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/%02zu-%s-speech-16f.ppm",
            directory,
            style,
            face_robot_redux_slug((face_robot_redux_style_t)style));
        if (rr_write_ppm(
                path, row, width, FACE_ROBOT_REDUX_HEIGHT) != 0) {
            free(row);
            free(all);
            return 1;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-speech-16f.ppm", directory);
    int result = rr_write_ppm(path, all, width, total_height);
    if (result == 0) {
        result = rr_write_contact_sheet(
            directory,
            "all-speech-16f",
            all,
            width,
            total_height);
    }
    free(row);
    free(all);
    return result;
}

static int rr_dump_visemes(const char *directory)
{
    const size_t width =
        RR_VISEME_COUNT * FACE_ROBOT_REDUX_WIDTH;
    const size_t height =
        FACE_ROBOT_REDUX_COUNT * FACE_ROBOT_REDUX_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U; style < FACE_ROBOT_REDUX_COUNT; ++style) {
        for (uint8_t viseme = 0U;
             viseme < RR_VISEME_COUNT;
             ++viseme) {
            uint16_t frame[FACE_ROBOT_REDUX_PIXEL_COUNT];
            face_render_key_t key =
                rr_preview_key(FACE_EXPRESSION_WARM, true);
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            if (!face_robot_redux_render(
                    (face_robot_redux_style_t)style,
                    &key,
                    18231U,
                    frame,
                    FACE_ROBOT_REDUX_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            rr_copy_frame(sheet, width, viseme, style, frame);
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-visemes.ppm", directory);
    const int result = rr_write_ppm(path, sheet, width, height);
    free(sheet);
    return result;
}

static int rr_write_manifest(const char *directory)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.txt", directory);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return 1;
    }
    fprintf(
        file,
        "Every cell is a native 160x120 RGB565 frame.\n"
        "Expression columns:\n");
    for (size_t index = 0U; index < RR_EXPRESSION_COUNT; ++index) {
        fprintf(
            file, "  %zu %s\n", index, RR_EXPRESSION_NAMES[index]);
    }
    fprintf(file, "Renderer rows:\n");
    for (size_t style = 0U; style < FACE_ROBOT_REDUX_COUNT; ++style) {
        face_robot_redux_info_t info;
        if (!face_robot_redux_info(
                (face_robot_redux_style_t)style, &info)) {
            fclose(file);
            return 1;
        }
        fprintf(
            file,
            "  %zu legacy=%u %s - %s%s\n",
            style,
            info.legacy_profile_id,
            info.slug,
            info.name,
            info.deliberate_mouthless ? " [mouthless]" : "");
    }
    fprintf(file, "Viseme columns:\n");
    for (size_t index = 0U; index < RR_VISEME_COUNT; ++index) {
        fprintf(file, "  %zu %s\n", index, RR_VISEME_NAMES[index]);
    }
    fprintf(
        file,
        "Speech columns are chronological at 30 fps. Frame 1 is "
        "anticipation; frames 13-14 are settle; frame 15 is rest.\n"
        "Contact sheets use 40x30 nearest samples per actor frame.\n");
    return fclose(file) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (rr_dump_expression_sheets(argv[1]) != 0 ||
        rr_dump_speech_sheets(argv[1]) != 0 ||
        rr_dump_visemes(argv[1]) != 0 ||
        rr_write_manifest(argv[1]) != 0) {
        return 1;
    }
    printf("robot redux actor artifacts written to %s\n", argv[1]);
    return 0;
}
