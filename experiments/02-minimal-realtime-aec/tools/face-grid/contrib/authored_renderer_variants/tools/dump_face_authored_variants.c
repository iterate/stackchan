#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_authored_variants.h"
#include "face_stage.h"

enum {
    DUMP_SAMPLE_RATE = 16000,
    DUMP_SAMPLES_PER_FRAME = 533,
    DUMP_EXPRESSION_CLOCK = DUMP_SAMPLE_RATE * 7 + 211,
    DUMP_EXACT_WIDTH = 40,
    DUMP_EXACT_HEIGHT = 30,
    DUMP_TEMPORAL_FRAMES = 24,
};

static uint16_t frame[FAV_PIXEL_COUNT];
static uint16_t exact[DUMP_EXACT_WIDTH * DUMP_EXACT_HEIGHT];

static const uint8_t VISEME_SHAPES[15][5] = {
    {236, 205, 24, 0, 18},  {155, 246, 0, 0, 128},
    {102, 255, 0, 0, 155},  {214, 112, 255, 0, 16},
    {112, 82, 244, 0, 10},  {12, 164, 18, 255, 0},
    {66, 224, 0, 0, 210},   {88, 190, 0, 0, 235},
    {82, 194, 0, 0, 164},   {38, 198, 0, 176, 235},
    {120, 181, 20, 0, 78},  {72, 202, 0, 0, 142},
    {104, 148, 94, 0, 72},  {86, 158, 46, 34, 184},
    {0, 110, 30, 0, 0},
};

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 152U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 76U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 1U;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_render_key_t idle_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_width = 128U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.viseme = 14U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 255U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.affect_arousal = 72U;
    key.attention = 192U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(700U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static face_render_key_t expression_key(int expression)
{
    face_render_key_t key = base_key();
    const face_stage_cue_t cue =
        expression_cue((face_expression_t)expression);
    (void)face_stage_cue_apply(&cue, DUMP_EXPRESSION_CLOCK, &key);
    return key;
}

static face_render_key_t viseme_key(int viseme)
{
    face_render_key_t key = base_key();
    key.viseme = (uint8_t)viseme;
    key.viseme_secondary = 255U;
    key.viseme_blend = 0U;
    key.viseme_weight = 255U;
    key.controls.mouth_open = VISEME_SHAPES[viseme][0];
    key.controls.mouth_width = VISEME_SHAPES[viseme][1];
    key.controls.mouth_round = VISEME_SHAPES[viseme][2];
    key.controls.mouth_press = VISEME_SHAPES[viseme][3];
    key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
    return key;
}

static face_render_key_t temporal_key(int step)
{
    face_render_key_t key = base_key();
    const face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_WARM);
    (void)face_stage_cue_apply(
        &cue, (uint32_t)step * DUMP_SAMPLES_PER_FRAME, &key);
    if (step < 4) {
        key = idle_key();
    } else if (step < 8) {
        key.speech_phase = FACE_SPEECH_STARTING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.audio_level = (uint8_t)(64 + (step - 4) * 36);
        key.controls.mouth_open = (uint8_t)(18 + (step - 4) * 14);
    } else if (step < 18) {
        const int viseme = (step - 8) % 5;
        key.speech_phase = FACE_SPEECH_ACTIVE;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.audio_level = (uint8_t)(118 + ((step * 37) % 110));
        key.viseme = (uint8_t)viseme;
        key.viseme_secondary = (uint8_t)((viseme + 1) % 5);
        key.viseme_blend = (uint8_t)((step * 43) & 0xff);
        key.controls.mouth_open = VISEME_SHAPES[viseme][0];
        key.controls.mouth_width = VISEME_SHAPES[viseme][1];
        key.controls.mouth_round = VISEME_SHAPES[viseme][2];
        key.controls.mouth_press = VISEME_SHAPES[viseme][3];
        key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
        if (step == 12 || step == 13) {
            key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
        }
    } else if (step < 21) {
        key.speech_phase = FACE_SPEECH_ENDING;
        key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
        key.controls.mouth_open = (uint8_t)(70 - (step - 18) * 22);
        key.audio_level = (uint8_t)(96 - (step - 18) * 28);
    } else {
        key = idle_key();
    }
    return key;
}

static uint8_t red8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 11U) & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t green8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 5U) & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static uint8_t blue8(uint16_t color)
{
    const uint8_t value = (uint8_t)(color & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint16_t pack565(uint32_t red, uint32_t green, uint32_t blue)
{
    return (uint16_t)(
        (((uint16_t)red & 0xf8U) << 8U) |
        (((uint16_t)green & 0xfcU) << 3U) |
        ((uint16_t)blue >> 3U));
}

static void downsample_exact40(
    const uint16_t *source, uint16_t *destination)
{
    for (int y = 0; y < DUMP_EXACT_HEIGHT; ++y) {
        for (int x = 0; x < DUMP_EXACT_WIDTH; ++x) {
            uint32_t red = 0U;
            uint32_t green = 0U;
            uint32_t blue = 0U;
            for (int yy = 0; yy < 4; ++yy) {
                for (int xx = 0; xx < 4; ++xx) {
                    const uint16_t pixel =
                        source[(y * 4 + yy) * FAV_FRAME_WIDTH +
                               (x * 4 + xx)];
                    red += red8(pixel);
                    green += green8(pixel);
                    blue += blue8(pixel);
                }
            }
            destination[y * DUMP_EXACT_WIDTH + x] =
                pack565((red + 8U) / 16U, (green + 8U) / 16U,
                        (blue + 8U) / 16U);
        }
    }
}

static uint32_t frame_hash(const uint16_t *pixels, size_t count)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < count; ++index) {
        hash ^= pixels[index] & 0xffU;
        hash *= 16777619U;
        hash ^= pixels[index] >> 8U;
        hash *= 16777619U;
    }
    return hash;
}

static int write_ppm(
    const char *path, const uint16_t *pixels, int width, int height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fprintf(file, "P6\n%d %d\n255\n", width, height) < 0) {
        fclose(file);
        return 1;
    }
    for (int index = 0; index < width * height; ++index) {
        const uint8_t rgb[3] = {
            red8(pixels[index]), green8(pixels[index]), blue8(pixels[index]),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

static void blit(
    uint16_t *sheet, int sheet_width,
    const uint16_t *source, int source_width, int source_height,
    int destination_x, int destination_y)
{
    for (int y = 0; y < source_height; ++y) {
        memcpy(
            sheet + (destination_y + y) * sheet_width + destination_x,
            source + y * source_width,
            (size_t)source_width * sizeof(*source));
    }
}

typedef face_render_key_t (*key_factory_t)(int case_index);

static int dump_sheet_pair(
    const char *outdir, const char *stem, int columns,
    key_factory_t key_factory, uint32_t clock_step)
{
    const int native_width = columns * FAV_FRAME_WIDTH;
    const int native_height = (int)fav_profile_count() * FAV_FRAME_HEIGHT;
    const int exact_width = columns * DUMP_EXACT_WIDTH;
    const int exact_height = (int)fav_profile_count() * DUMP_EXACT_HEIGHT;
    uint16_t *native = calloc(
        (size_t)native_width * (size_t)native_height, sizeof(*native));
    uint16_t *small = calloc(
        (size_t)exact_width * (size_t)exact_height, sizeof(*small));
    if (native == NULL || small == NULL) {
        free(native);
        free(small);
        fprintf(stderr, "out of memory building %s sheets\n", stem);
        return 1;
    }
    for (size_t profile = 0U; profile < fav_profile_count(); ++profile) {
        for (int column = 0; column < columns; ++column) {
            const face_render_key_t key = key_factory(column);
            const uint32_t clock =
                DUMP_EXPRESSION_CLOCK + (uint32_t)column * clock_step;
            if (!fav_render_frame(
                    (fav_profile_t)profile, &key, clock,
                    frame, FAV_PIXEL_COUNT)) {
                free(native);
                free(small);
                return 1;
            }
            downsample_exact40(frame, exact);
            blit(
                native, native_width, frame,
                FAV_FRAME_WIDTH, FAV_FRAME_HEIGHT,
                column * FAV_FRAME_WIDTH,
                (int)profile * FAV_FRAME_HEIGHT);
            blit(
                small, exact_width, exact,
                DUMP_EXACT_WIDTH, DUMP_EXACT_HEIGHT,
                column * DUMP_EXACT_WIDTH,
                (int)profile * DUMP_EXACT_HEIGHT);
        }
    }
    char native_path[512];
    char exact_path[512];
    const int native_length = snprintf(
        native_path, sizeof(native_path), "%s/%s__native.ppm",
        outdir, stem);
    const int exact_length = snprintf(
        exact_path, sizeof(exact_path), "%s/%s__exact40.ppm",
        outdir, stem);
    int result = 0;
    if (native_length < 0 || (size_t)native_length >= sizeof(native_path) ||
        exact_length < 0 || (size_t)exact_length >= sizeof(exact_path)) {
        result = 1;
    } else {
        result |= write_ppm(
            native_path, native, native_width, native_height);
        result |= write_ppm(exact_path, small, exact_width, exact_height);
    }
    free(native);
    free(small);
    return result;
}

static int dump_sheets(const char *outdir)
{
    int result = 0;
    result |= dump_sheet_pair(
        outdir, "fav__9-profiles__11-emotions",
        FACE_EXPRESSION_COUNT, expression_key, 0U);
    result |= dump_sheet_pair(
        outdir, "fav__9-profiles__15-ovr-visemes",
        15, viseme_key, 13U);
    result |= dump_sheet_pair(
        outdir, "fav__9-profiles__speech-blink-temporal",
        DUMP_TEMPORAL_FRAMES, temporal_key, DUMP_SAMPLES_PER_FRAME);
    return result;
}

static int dump_hashes(void)
{
    for (size_t profile = 0U; profile < fav_profile_count(); ++profile) {
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key = expression_key(expression);
            if (!fav_render_frame(
                    (fav_profile_t)profile, &key, DUMP_EXPRESSION_CLOCK,
                    frame, FAV_PIXEL_COUNT)) {
                return 1;
            }
            printf(
                "%s expression %02d %08x\n",
                fav_profile_slug((fav_profile_t)profile), expression,
                frame_hash(frame, FAV_PIXEL_COUNT));
        }
        for (int viseme = 0; viseme < 15; ++viseme) {
            const face_render_key_t key = viseme_key(viseme);
            if (!fav_render_frame(
                    (fav_profile_t)profile, &key,
                    DUMP_EXPRESSION_CLOCK + (uint32_t)viseme * 13U,
                    frame, FAV_PIXEL_COUNT)) {
                return 1;
            }
            printf(
                "%s viseme %02d %08x\n",
                fav_profile_slug((fav_profile_t)profile), viseme,
                frame_hash(frame, FAV_PIXEL_COUNT));
        }
        for (int step = 0; step < DUMP_TEMPORAL_FRAMES; ++step) {
            const face_render_key_t key = temporal_key(step);
            if (!fav_render_frame(
                    (fav_profile_t)profile, &key,
                    (uint32_t)step * DUMP_SAMPLES_PER_FRAME,
                    frame, FAV_PIXEL_COUNT)) {
                return 1;
            }
            printf(
                "%s temporal %02d %08x\n",
                fav_profile_slug((fav_profile_t)profile), step,
                frame_hash(frame, FAV_PIXEL_COUNT));
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "hash") == 0) {
        return dump_hashes();
    }
    if (argc == 3 && strcmp(argv[1], "sheets") == 0) {
        return dump_sheets(argv[2]);
    }
    fprintf(
        stderr,
        "usage: %s hash | sheets OUTDIR\n",
        argc > 0 ? argv[0] : "dump_face_authored_variants");
    return 2;
}
