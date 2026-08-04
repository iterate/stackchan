#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_pose.h"
#include "face_salvage_actors.h"
#include "face_stage.h"

enum {
    EXPRESSION_COUNT = 11,
    VISEME_COUNT = 15,
    SPEECH_FRAMES = 16,
};

static const char *const EXPRESSION_NAMES[EXPRESSION_COUNT] = {
    "neutral", "warm", "joy", "concern", "surprise", "thoughtful",
    "skeptical", "determined", "sleepy", "excited", "embarrassed",
};

static const char *const VISEME_NAMES[VISEME_COUNT] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

static const uint8_t SPEECH_VISEMES[SPEECH_FRAMES] = {
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
    for (size_t y = 0U; y < FACE_SALVAGE_ACTOR_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_SALVAGE_ACTOR_HEIGHT + y) * sheet_width +
                column * FACE_SALVAGE_ACTOR_WIDTH,
            frame + y * FACE_SALVAGE_ACTOR_WIDTH,
            FACE_SALVAGE_ACTOR_WIDTH * sizeof(uint16_t));
    }
}

static face_render_key_t preview_key(uint8_t expression, bool speaking)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = speaking ? 138U : 12U;
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
    key.audio_level = speaking ? 142U : 8U;
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

static face_render_key_t speech_key(size_t frame)
{
    face_render_key_t key =
        preview_key(FACE_EXPRESSION_WARM, true);
    key.viseme = SPEECH_VISEMES[frame];
    key.viseme_secondary =
        SPEECH_VISEMES[
            frame + 1U < SPEECH_FRAMES ? frame + 1U : frame];
    key.viseme_blend = (uint8_t)((frame * 31U) & 127U);
    key.controls.mouth_open =
        (uint8_t)(24U + (frame * 53U) % 210U);
    key.controls.mouth_width =
        (uint8_t)(118U + (frame * 29U) % 126U);
    key.controls.mouth_round =
        (uint8_t)((frame * 67U) & 255U);
    key.audio_level =
        frame < 2U || frame >= 14U
        ? 7U
        : (uint8_t)(92U + (frame * 31U) % 145U);
    key.controls.look_x = (int8_t)((int)frame * 5 - 36);
    key.controls.look_y =
        (int8_t)(frame < 8U ? -10 + (int)frame * 2
                            : 8 - ((int)frame - 8) * 2);
    key.speech_phase =
        frame == 0U ? FACE_SPEECH_IDLE
        : frame == 1U ? FACE_SPEECH_STARTING
        : frame >= 14U ? FACE_SPEECH_ENDING
        : FACE_SPEECH_ACTIVE;
    if (frame == 0U) {
        key.controls.flags = 0U;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
    }
    if (frame >= 11U) {
        key.stage_expression = FACE_EXPRESSION_JOY;
    }
    if (frame == 13U) {
        key.stage_expression = FACE_EXPRESSION_EXCITED;
    }
    return key;
}

static int dump_expression_sheets(const char *directory)
{
    const size_t width =
        EXPRESSION_COUNT * FACE_SALVAGE_ACTOR_WIDTH;
    const size_t combined_height =
        FACE_SALVAGE_ACTOR_COUNT * FACE_SALVAGE_ACTOR_HEIGHT;
    uint16_t *row = calloc(
        width * FACE_SALVAGE_ACTOR_HEIGHT, sizeof(uint16_t));
    uint16_t *combined =
        calloc(width * combined_height, sizeof(uint16_t));
    if (row == NULL || combined == NULL) {
        free(row);
        free(combined);
        return 1;
    }
    for (size_t style = 0U;
         style < FACE_SALVAGE_ACTOR_COUNT;
         ++style) {
        memset(
            row,
            0,
            width * FACE_SALVAGE_ACTOR_HEIGHT * sizeof(uint16_t));
        for (uint8_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
            const face_render_key_t key =
                preview_key(expression, false);
            if (!face_salvage_actor_render(
                    (face_salvage_actor_style_t)style,
                    &key,
                    5064U,
                    frame,
                    FACE_SALVAGE_ACTOR_PIXEL_COUNT)) {
                free(row);
                free(combined);
                return 1;
            }
            copy_frame(row, width, expression, 0U, frame);
            copy_frame(combined, width, expression, style, frame);
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/%02zu-%s-expressions.ppm",
            directory,
            style,
            face_salvage_actor_slug(
                (face_salvage_actor_style_t)style));
        if (write_ppm(
                path, row, width, FACE_SALVAGE_ACTOR_HEIGHT) != 0) {
            free(row);
            free(combined);
            return 1;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-expressions.ppm", directory);
    const int result =
        write_ppm(path, combined, width, combined_height);
    free(row);
    free(combined);
    return result;
}

static int dump_speech_strips(const char *directory)
{
    const size_t width = SPEECH_FRAMES * FACE_SALVAGE_ACTOR_WIDTH;
    const size_t combined_height =
        FACE_SALVAGE_ACTOR_COUNT * FACE_SALVAGE_ACTOR_HEIGHT;
    uint16_t *row = calloc(
        width * FACE_SALVAGE_ACTOR_HEIGHT, sizeof(uint16_t));
    uint16_t *combined =
        calloc(width * combined_height, sizeof(uint16_t));
    if (row == NULL || combined == NULL) {
        free(row);
        free(combined);
        return 1;
    }
    for (size_t style = 0U;
         style < FACE_SALVAGE_ACTOR_COUNT;
         ++style) {
        memset(
            row,
            0,
            width * FACE_SALVAGE_ACTOR_HEIGHT * sizeof(uint16_t));
        for (size_t frame_index = 0U;
             frame_index < SPEECH_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
            const face_render_key_t key = speech_key(frame_index);
            if (!face_salvage_actor_render(
                    (face_salvage_actor_style_t)style,
                    &key,
                    (uint32_t)frame_index * 533U,
                    frame,
                    FACE_SALVAGE_ACTOR_PIXEL_COUNT)) {
                free(row);
                free(combined);
                return 1;
            }
            copy_frame(row, width, frame_index, 0U, frame);
            copy_frame(
                combined, width, frame_index, style, frame);
        }
        char path[1024];
        snprintf(
            path,
            sizeof(path),
            "%s/%02zu-%s-speech-16f.ppm",
            directory,
            style,
            face_salvage_actor_slug(
                (face_salvage_actor_style_t)style));
        if (write_ppm(
                path, row, width, FACE_SALVAGE_ACTOR_HEIGHT) != 0) {
            free(row);
            free(combined);
            return 1;
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-speech-16f.ppm", directory);
    const int result =
        write_ppm(path, combined, width, combined_height);
    free(row);
    free(combined);
    return result;
}

static int dump_visemes(const char *directory)
{
    const size_t width = VISEME_COUNT * FACE_SALVAGE_ACTOR_WIDTH;
    const size_t height =
        FACE_SALVAGE_ACTOR_COUNT * FACE_SALVAGE_ACTOR_HEIGHT;
    uint16_t *sheet = calloc(width * height, sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t style = 0U;
         style < FACE_SALVAGE_ACTOR_COUNT;
         ++style) {
        for (uint8_t viseme = 0U; viseme < VISEME_COUNT; ++viseme) {
            uint16_t frame[FACE_SALVAGE_ACTOR_PIXEL_COUNT];
            face_render_key_t key =
                preview_key(FACE_EXPRESSION_WARM, true);
            key.viseme = viseme;
            key.viseme_secondary = viseme;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            if (!face_salvage_actor_render(
                    (face_salvage_actor_style_t)style,
                    &key,
                    18231U,
                    frame,
                    FACE_SALVAGE_ACTOR_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            copy_frame(sheet, width, viseme, style, frame);
        }
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/all-visemes.ppm", directory);
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
        "Every cell is a native 160x120 RGB565 frame.\n"
        "Expression columns:\n");
    for (size_t index = 0U; index < EXPRESSION_COUNT; ++index) {
        fprintf(file, "  %zu %s\n", index, EXPRESSION_NAMES[index]);
    }
    fprintf(file, "Renderer rows:\n");
    for (size_t style = 0U;
         style < FACE_SALVAGE_ACTOR_COUNT;
         ++style) {
        face_salvage_actor_info_t info;
        if (!face_salvage_actor_info(
                (face_salvage_actor_style_t)style, &info)) {
            fclose(file);
            return 1;
        }
        fprintf(
            file,
            "  %zu legacy=%u %s — %s\n",
            style,
            info.legacy_profile_id,
            info.slug,
            info.name);
    }
    fprintf(file, "Viseme columns:\n");
    for (size_t index = 0U; index < VISEME_COUNT; ++index) {
        fprintf(file, "  %zu %s\n", index, VISEME_NAMES[index]);
    }
    fprintf(
        file,
        "Speech strips are chronological left-to-right at 30 fps "
        "(533 PCM samples per frame).\n");
    return fclose(file) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (dump_expression_sheets(argv[1]) != 0 ||
        dump_speech_strips(argv[1]) != 0 ||
        dump_visemes(argv[1]) != 0 ||
        write_manifest(argv[1]) != 0) {
        return 1;
    }
    printf("face salvage actor artifacts written to %s\n", argv[1]);
    return 0;
}
