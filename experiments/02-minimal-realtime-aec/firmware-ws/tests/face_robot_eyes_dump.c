#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_robot_eyes.h"
#include "face_stage.h"

static face_render_key_t preview_key(uint8_t expression)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 148U;
    key.controls.mouth_width = 164U;
    key.controls.mouth_round = 76U;
    key.controls.mouth_teeth = 82U;
    key.controls.eye_left_open = 244U;
    key.controls.eye_right_open = 244U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_AA;
    key.viseme_weight = 210U;
    key.audio_level = 128U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 44U;
    key.affect_arousal = 128U;
    key.attention = 210U;
    key.expression_weight = 255U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static uint8_t expand5(uint16_t value)
{
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(uint16_t value)
{
    return (uint8_t)((value << 2) | (value >> 4));
}

static int write_sheet(const char *path)
{
    const size_t width =
        FACE_EXPRESSION_COUNT * FACE_ROBOT_EYES_WIDTH;
    const size_t height =
        FACE_ROBOT_EYES_PROFILE_COUNT * FACE_ROBOT_EYES_HEIGHT;
    uint16_t *pixels =
        (uint16_t *)malloc(width * height * sizeof(uint16_t));
    if (pixels == NULL) {
        return 1;
    }
    for (size_t raw_profile = 0U;
         raw_profile < FACE_ROBOT_EYES_PROFILE_COUNT;
         ++raw_profile) {
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            uint16_t frame[FACE_ROBOT_EYES_PIXEL_COUNT];
            const face_render_key_t key = preview_key(expression);
            if (!face_robot_eyes_render(
                    (face_robot_eyes_profile_t)raw_profile,
                    &key,
                    16000U + 211U,
                    frame,
                    FACE_ROBOT_EYES_PIXEL_COUNT)) {
                free(pixels);
                return 1;
            }
            for (size_t y = 0U; y < FACE_ROBOT_EYES_HEIGHT; ++y) {
                memcpy(
                    pixels +
                        (raw_profile * FACE_ROBOT_EYES_HEIGHT + y) * width +
                        expression * FACE_ROBOT_EYES_WIDTH,
                    frame + y * FACE_ROBOT_EYES_WIDTH,
                    FACE_ROBOT_EYES_WIDTH * sizeof(uint16_t));
            }
        }
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(pixels);
        return 1;
    }
    fprintf(file, "P6\n%zu %zu\n255\n", width, height);
    for (size_t index = 0U; index < width * height; ++index) {
        const uint16_t color = pixels[index];
        const uint8_t rgb[3] = {
            expand5((uint16_t)((color >> 11) & 31U)),
            expand6((uint16_t)((color >> 5) & 63U)),
            expand5((uint16_t)(color & 31U)),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            free(pixels);
            return 1;
        }
    }
    const int close_result = fclose(file);
    free(pixels);
    return close_result == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.ppm\n", argv[0]);
        return 2;
    }
    if (write_sheet(argv[1]) != 0) {
        fprintf(stderr, "failed to write %s\n", argv[1]);
        return 1;
    }
    printf(
        "wrote %s (%d profiles x %d expressions)\n",
        argv[1],
        FACE_ROBOT_EYES_PROFILE_COUNT,
        FACE_EXPRESSION_COUNT);
    return 0;
}
