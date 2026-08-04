#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_pose.h"
#include "face_sprite_redux_actors.h"
#include "face_stage.h"

enum {
    EXPRESSION_COUNT = 11,
    VISEME_COUNT = 15,
    SPEECH_FRAMES = 16,
    AUDIT_DIVISOR = 4,
};

typedef face_render_key_t (*key_builder_t)(size_t index);

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

static const int8_t SPEECH_GAZE_X[SPEECH_FRAMES] = {
    0, -3, -5, -3, 0, 2, 4, 5,
    4, 2, 0, -2, -3, -2, 0, 0,
};

static const int8_t SPEECH_GAZE_Y[SPEECH_FRAMES] = {
    0, -1, -2, -2, -1, 0, 1, 1,
    0, -1, -2, -1, 0, 1, 0, 0,
};

static const uint8_t SPEECH_LEVEL[SPEECH_FRAMES] = {
    18U, 64U, 136U, 106U, 84U, 154U, 116U, 54U,
    82U, 112U, 76U, 126U, 94U, 168U, 80U, 10U,
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
    for (size_t y = 0U; y < FACE_SPRITE_REDUX_HEIGHT; ++y) {
        memcpy(
            sheet +
                (row * FACE_SPRITE_REDUX_HEIGHT + y) * sheet_width +
                column * FACE_SPRITE_REDUX_WIDTH,
            frame + y * FACE_SPRITE_REDUX_WIDTH,
            FACE_SPRITE_REDUX_WIDTH * sizeof(uint16_t));
    }
}

static uint16_t *make_exact_40x30_sheet(
    const uint16_t *native,
    size_t native_width,
    size_t native_height)
{
    const size_t width = native_width / AUDIT_DIVISOR;
    const size_t height = native_height / AUDIT_DIVISOR;
    uint16_t *audit = calloc(width * height, sizeof(uint16_t));
    if (audit == NULL) {
        return NULL;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            const size_t source_y =
                y * AUDIT_DIVISOR + AUDIT_DIVISOR / 2U;
            const size_t source_x =
                x * AUDIT_DIVISOR + AUDIT_DIVISOR / 2U;
            audit[y * width + x] =
                native[source_y * native_width + source_x];
        }
    }
    return audit;
}

static face_render_key_t preview_key(size_t expression)
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
    key.audio_level = 5U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_SIL;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.cheek = 14U;
    key.affect_arousal = 118U;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = (uint8_t)expression;
    return key;
}

static face_render_key_t speech_key(size_t frame)
{
    face_render_key_t key = preview_key(FACE_EXPRESSION_WARM);
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.viseme = SPEECH_VISEMES[frame];
    key.viseme_secondary =
        SPEECH_VISEMES[
            frame + 1U < SPEECH_FRAMES ? frame + 1U : frame];
    key.viseme_blend =
        frame < 2U || frame >= 14U
        ? 0U
        : (uint8_t)(32U + (frame * 37U) % 80U);
    key.viseme_weight =
        frame == 0U ? 32U :
        frame == 1U ? 132U :
        frame == 14U ? 180U :
        frame == 15U ? 24U :
        238U;
    key.controls.mouth_open =
        frame == 0U ? 16U :
        frame == 1U ? 48U :
        frame == 14U ? 72U :
        frame == 15U ? 10U :
        (uint8_t)(62U + (frame * 47U) % 152U);
    key.controls.mouth_width =
        (uint8_t)(112U + (frame * 23U) % 122U);
    key.controls.mouth_round =
        frame < 2U || frame >= 14U
        ? 22U
        : (uint8_t)((frame * 61U) & 255U);
    key.controls.mouth_press =
        SPEECH_VISEMES[frame] == FACE_VISEME_PP ? 244U : 18U;
    key.controls.mouth_teeth =
        SPEECH_VISEMES[frame] == FACE_VISEME_E ||
        SPEECH_VISEMES[frame] == FACE_VISEME_SS ||
        SPEECH_VISEMES[frame] == FACE_VISEME_FF
        ? 232U
        : 62U;
    key.tongue =
        SPEECH_VISEMES[frame] == FACE_VISEME_TH ? 250U :
        SPEECH_VISEMES[frame] == FACE_VISEME_AA ? 132U : 24U;
    key.phoneme =
        frame >= 2U && frame < 14U
        ? (uint8_t)(frame * 3U + 1U)
        : FACE_PHONEME_NONE;
    key.audio_level = SPEECH_LEVEL[frame];
    key.controls.look_x = SPEECH_GAZE_X[frame];
    key.controls.look_y = SPEECH_GAZE_Y[frame];
    key.controls.eye_left_open =
        (uint8_t)(204U + (frame % 4U) * 12U);
    key.controls.eye_right_open =
        (uint8_t)(210U + ((frame + 2U) % 4U) * 10U);
    key.speech_phase =
        frame < 2U ? FACE_SPEECH_STARTING :
        frame >= 14U ? FACE_SPEECH_ENDING :
        FACE_SPEECH_ACTIVE;
    key.stage_expression =
        frame == 13U ? FACE_EXPRESSION_EXCITED :
        frame >= 11U && frame <= 12U ? FACE_EXPRESSION_JOY :
        FACE_EXPRESSION_WARM;
    key.cheek = frame >= 11U && frame <= 13U ? 96U : 28U;
    return key;
}

static face_render_key_t viseme_key(size_t viseme)
{
    face_render_key_t key = preview_key(FACE_EXPRESSION_NEUTRAL);
    key.controls.mouth_open = 126U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 46U;
    key.controls.mouth_press = 28U;
    key.controls.mouth_teeth = 118U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.viseme = (uint8_t)viseme;
    key.viseme_secondary = (uint8_t)viseme;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    key.audio_level = 136U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.tongue = 74U;
    key.attention = 204U;
    return key;
}

static int dump_sequence(
    const char *directory,
    size_t columns,
    key_builder_t build_key,
    const char *native_name,
    const char *audit_name)
{
    const size_t width = columns * FACE_SPRITE_REDUX_WIDTH;
    const size_t combined_height =
        FACE_SPRITE_REDUX_ACTOR_COUNT * FACE_SPRITE_REDUX_HEIGHT;
    uint16_t *combined =
        calloc(width * combined_height, sizeof(uint16_t));
    if (combined == NULL) {
        free(combined);
        return 1;
    }
    for (size_t actor = 0U;
         actor < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++actor) {
        for (size_t column = 0U; column < columns; ++column) {
            uint16_t frame[FACE_SPRITE_REDUX_PIXEL_COUNT];
            const face_render_key_t key = build_key(column);
            if (!face_sprite_redux_actor_render(
                    (face_sprite_redux_actor_t)actor,
                    &key,
                    (uint32_t)column * 533U,
                    frame,
                    FACE_SPRITE_REDUX_PIXEL_COUNT)) {
                free(combined);
                return 1;
            }
            copy_frame(combined, width, column, actor, frame);
        }
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, native_name);
    if (write_ppm(path, combined, width, combined_height) != 0) {
        free(combined);
        return 1;
    }
    uint16_t *audit =
        make_exact_40x30_sheet(combined, width, combined_height);
    if (audit == NULL) {
        free(combined);
        return 1;
    }
    snprintf(
        path, sizeof(path), "%s/%s.ppm",
        directory, audit_name);
    const int result = write_ppm(
        path,
        audit,
        width / AUDIT_DIVISOR,
        combined_height / AUDIT_DIVISOR);
    free(audit);
    free(combined);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (dump_sequence(
            argv[1],
            EXPRESSION_COUNT,
            preview_key,
            "sprite-redux-expressions-native-3x11",
            "sprite-redux-expressions-exact-40x30-3x11") != 0 ||
        dump_sequence(
            argv[1],
            VISEME_COUNT,
            viseme_key,
            "sprite-redux-visemes-native-3x15",
            "sprite-redux-visemes-exact-40x30-3x15") != 0 ||
        dump_sequence(
            argv[1],
            SPEECH_FRAMES,
            speech_key,
            "sprite-redux-temporal-native-3x16",
            "sprite-redux-temporal-exact-40x30-3x16") != 0) {
        return 1;
    }
    printf(
        "dumped native and exact unsmoothed 40x30 expression (3x11), "
        "viseme (3x15), and temporal (3x16) sheets to %s\n",
        argv[1]);
    return 0;
}
