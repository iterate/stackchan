#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_mouth_actors.h"

enum {
    LABEL_HEIGHT = 16,
    TILE_HEIGHT = FACE_MOUTH_ACTORS_HEIGHT + LABEL_HEIGHT,
    SPEECH_FRAMES = 12,
};

static const char *const EXPRESSION_NAMES[11] = {
    "NEUTRAL", "WARM", "JOY", "CONCERN", "SURPRISE", "THOUGHTFUL",
    "SKEPTICAL", "DETERMINED", "SLEEPY", "EXCITED", "EMBARRASSED",
};

static const char *const SPEECH_NAMES[SPEECH_FRAMES] = {
    "REST", "ANT", "AA", "AE", "EE", "EO",
    "OH", "OM", "MBP", "FV", "TH", "SET",
};

static const char *const PROFILE_SHORT[FACE_MOUTH_ACTOR_COUNT] = {
    "PRESTON", "JALI", "RIBBON", "MUNCH", "VU5", "ORIGAMI",
};

static uint8_t glyph_row(char character, unsigned row)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    static const uint8_t GLYPHS[][5] = {
        {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3},
        {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7}, {7, 4, 6, 4, 4},
        {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7},
        {1, 1, 1, 5, 2}, {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7},
        {5, 7, 7, 5, 5}, {5, 7, 7, 7, 5}, {2, 5, 5, 5, 2},
        {6, 5, 6, 4, 4}, {2, 5, 5, 3, 1}, {6, 5, 6, 5, 5},
        {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2}, {5, 5, 5, 5, 7},
        {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5},
        {5, 5, 2, 2, 2}, {7, 1, 2, 4, 7},
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {6, 1, 2, 4, 7},
        {6, 1, 2, 1, 6}, {5, 5, 7, 1, 1}, {7, 4, 6, 1, 6},
        {3, 4, 6, 5, 2}, {7, 1, 2, 2, 2}, {2, 5, 2, 5, 2},
        {2, 5, 3, 1, 6}, {0, 0, 7, 0, 0},
    };
    const char *found = strchr(ALPHABET, character);
    if (found == NULL || row >= 5U) {
        return 0U;
    }
    return GLYPHS[(size_t)(found - ALPHABET)][row];
}

static void draw_text(
    uint16_t *pixels,
    size_t width,
    size_t height,
    int x,
    int y,
    const char *text,
    uint16_t color)
{
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        for (unsigned row = 0U; row < 5U; ++row) {
            const uint8_t bits = glyph_row(text[index], row);
            for (unsigned column = 0U; column < 3U; ++column) {
                if ((bits & (1U << (2U - column))) != 0U) {
                    const int px = x + (int)index * 4 + (int)column;
                    const int py = y + (int)row;
                    if ((unsigned)px < width && (unsigned)py < height) {
                        pixels[(size_t)py * width + (size_t)px] = color;
                    }
                }
            }
        }
    }
}

static face_render_key_t preview_key(uint8_t expression)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 92U;
    key.controls.mouth_width = 150U;
    key.controls.mouth_round = 38U;
    key.controls.mouth_press = 6U;
    key.controls.mouth_teeth = 128U;
    key.controls.eye_left_open = 245U;
    key.controls.eye_right_open = 245U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_E;
    key.phoneme = 4U;
    key.viseme_weight = 180U;
    key.audio_level = 118U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_AA;
    key.viseme_blend = 44U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 116U;
    key.attention = 225U;
    key.expression_weight = 255U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = expression;
    return key;
}

static face_render_key_t speech_key(size_t frame)
{
    static const uint8_t VISEMES[SPEECH_FRAMES] = {
        FACE_VISEME_SIL, FACE_VISEME_SIL, FACE_VISEME_AA,
        FACE_VISEME_AA, FACE_VISEME_E, FACE_VISEME_E,
        FACE_VISEME_O, FACE_VISEME_O, FACE_VISEME_PP,
        FACE_VISEME_FF, FACE_VISEME_TH, FACE_VISEME_CH,
    };
    static const uint8_t SECONDARY[SPEECH_FRAMES] = {
        FACE_VISEME_SIL, FACE_VISEME_AA, FACE_VISEME_E,
        FACE_VISEME_E, FACE_VISEME_O, FACE_VISEME_O,
        FACE_VISEME_PP, FACE_VISEME_PP, FACE_VISEME_FF,
        FACE_VISEME_TH, FACE_VISEME_CH, FACE_VISEME_SIL,
    };
    static const uint8_t BLEND[SPEECH_FRAMES] = {
        0, 72, 24, 128, 24, 144, 24, 152, 24, 64, 80, 180,
    };
    static const uint8_t OPEN[SPEECH_FRAMES] = {
        8, 30, 236, 186, 124, 166, 210, 92, 12, 46, 112, 55,
    };
    static const uint8_t ROUND[SPEECH_FRAMES] = {
        18, 16, 20, 14, 8, 108, 240, 154, 12, 6, 44, 28,
    };
    face_render_key_t key = preview_key(1U);
    key.viseme = VISEMES[frame];
    key.viseme_secondary = SECONDARY[frame];
    key.viseme_blend = BLEND[frame];
    key.controls.mouth_open = OPEN[frame];
    key.controls.mouth_round = ROUND[frame];
    key.controls.mouth_press =
        frame == 8U ? 255U
                    : (frame == 7U ? 80U : (uint8_t)(frame * 3U));
    key.controls.mouth_teeth =
        frame == 9U ? 255U : 118U;
    key.tongue =
        frame == 10U ? 255U : (uint8_t)(frame * 5U);
    key.audio_level =
        frame == 0U ? 0U
                    : (uint8_t)(70U + (frame < 8U ? frame : 8U) * 20U);
    key.phoneme = (uint8_t)(frame * 3U + 1U);
    key.speech_phase = frame == 0U
                           ? FACE_SPEECH_IDLE
                           : (frame == 1U
                                  ? FACE_SPEECH_STARTING
                                  : (frame == SPEECH_FRAMES - 1U
                                         ? FACE_SPEECH_ENDING
                                         : FACE_SPEECH_ACTIVE));
    if (frame == 0U) {
        key.controls.flags = 0U;
    }
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

static int write_ppm(
    const char *path,
    const uint16_t *pixels,
    size_t width,
    size_t height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
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
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static int write_expression_sheet(
    const char *directory, face_mouth_actor_profile_t profile)
{
    const size_t width = 11U * FACE_MOUTH_ACTORS_WIDTH;
    const size_t height = TILE_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)malloc(width * height * sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t index = 0U; index < width * height; ++index) {
        sheet[index] = 0x0842U;
    }
    for (uint8_t expression = 0U; expression < 11U; ++expression) {
        uint16_t frame[FACE_MOUTH_ACTORS_PIXEL_COUNT];
        const face_render_key_t key = preview_key(expression);
        if (!face_mouth_actors_render(
                profile,
                &key,
                16000U * 4U + 211U,
                frame,
                FACE_MOUTH_ACTORS_PIXEL_COUNT)) {
            free(sheet);
            return 1;
        }
        for (size_t y = 0U; y < FACE_MOUTH_ACTORS_HEIGHT; ++y) {
            memcpy(
                sheet + (y + LABEL_HEIGHT) * width +
                    (size_t)expression * FACE_MOUTH_ACTORS_WIDTH,
                frame + y * FACE_MOUTH_ACTORS_WIDTH,
                FACE_MOUTH_ACTORS_WIDTH * sizeof(uint16_t));
        }
        char label[40];
        snprintf(
            label,
            sizeof(label),
            "%02u-%s",
            (unsigned)expression,
            EXPRESSION_NAMES[expression]);
        draw_text(
            sheet,
            width,
            height,
            expression * FACE_MOUTH_ACTORS_WIDTH + 4,
            5,
            label,
            0xffffU);
    }
    char path[512];
    snprintf(
        path,
        sizeof(path),
        "%s/%s-expressions.ppm",
        directory,
        face_mouth_actors_profile_slug(profile));
    const int result = write_ppm(path, sheet, width, height);
    free(sheet);
    if (result == 0) {
        printf("wrote %s\n", path);
    }
    return result;
}

static int write_speech_strip(const char *directory)
{
    const size_t width = SPEECH_FRAMES * FACE_MOUTH_ACTORS_WIDTH;
    const size_t height = FACE_MOUTH_ACTOR_COUNT * TILE_HEIGHT;
    uint16_t *sheet =
        (uint16_t *)malloc(width * height * sizeof(uint16_t));
    if (sheet == NULL) {
        return 1;
    }
    for (size_t index = 0U; index < width * height; ++index) {
        sheet[index] = 0x0842U;
    }
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        for (size_t frame_index = 0U;
             frame_index < SPEECH_FRAMES;
             ++frame_index) {
            uint16_t frame[FACE_MOUTH_ACTORS_PIXEL_COUNT];
            const face_render_key_t key = speech_key(frame_index);
            if (!face_mouth_actors_render(
                    (face_mouth_actor_profile_t)profile,
                    &key,
                    (uint32_t)(frame_index * 16000U / 12U + 317U),
                    frame,
                    FACE_MOUTH_ACTORS_PIXEL_COUNT)) {
                free(sheet);
                return 1;
            }
            const size_t top = profile * TILE_HEIGHT + LABEL_HEIGHT;
            const size_t left = frame_index * FACE_MOUTH_ACTORS_WIDTH;
            for (size_t y = 0U; y < FACE_MOUTH_ACTORS_HEIGHT; ++y) {
                memcpy(
                    sheet + (top + y) * width + left,
                    frame + y * FACE_MOUTH_ACTORS_WIDTH,
                    FACE_MOUTH_ACTORS_WIDTH * sizeof(uint16_t));
            }
            char label[40];
            snprintf(
                label,
                sizeof(label),
                "%s-%s",
                frame_index == 0U ? PROFILE_SHORT[profile] : "T",
                SPEECH_NAMES[frame_index]);
            draw_text(
                sheet,
                width,
                height,
                (int)left + 4,
                (int)(profile * TILE_HEIGHT) + 5,
                label,
                0xffffU);
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/actor-speech-temporal.ppm", directory);
    const int result = write_ppm(path, sheet, width, height);
    free(sheet);
    if (result == 0) {
        printf("wrote %s\n", path);
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        if (write_expression_sheet(
                argv[1], (face_mouth_actor_profile_t)profile) != 0) {
            return 1;
        }
    }
    return write_speech_strip(argv[1]);
}
