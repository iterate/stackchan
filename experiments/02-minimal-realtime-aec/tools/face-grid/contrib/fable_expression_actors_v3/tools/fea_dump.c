#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_stage.h"
#include "fea.h"

/*
 * Contact sheet / hash / cell dumper for fable_expression_actors_v3.
 *
 *   fea_dump hash               print FNV-1a32 hashes for the case table
 *   fea_dump sheets <dir>       labeled PPM contact sheets
 *   fea_dump cell <p> <e> <f>   one 160x120 cell as PPM
 *
 * Sheets are written at ORIGINAL cell size (160x120 per face, no
 * scaling) so review happens at device scale.
 */

enum {
    SAMPLE_RATE = 16000,
    LABEL_H = 10,
    LABEL_W = 46,
};

static const char *const EMOTION_NAMES[FACE_EXPRESSION_COUNT] = {
    "NEUTRAL", "WARM", "JOY", "CONCERN", "SURPRISE", "THOUGHT",
    "SKEPTIC", "DETERMIN", "SLEEPY", "EXCITED", "EMBARRAS",
};

static const char *const VISEME_NAMES[15] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH", "DD", "FF", "KK",
    "NN", "RR", "CH", "SIL",
};

/* ------------------------------------------------------- tiny font */

typedef struct {
    char ch;
    uint8_t rows[5];            /* 3-bit rows, MSB = left pixel */
} glyph_t;

static const glyph_t FONT[] = {
    { 'A', { 7, 5, 7, 5, 5 } }, { 'B', { 6, 5, 6, 5, 6 } },
    { 'C', { 3, 4, 4, 4, 3 } }, { 'D', { 6, 5, 5, 5, 6 } },
    { 'E', { 7, 4, 6, 4, 7 } }, { 'F', { 7, 4, 6, 4, 4 } },
    { 'G', { 3, 4, 5, 5, 3 } }, { 'H', { 5, 5, 7, 5, 5 } },
    { 'I', { 7, 2, 2, 2, 7 } }, { 'J', { 1, 1, 1, 5, 2 } },
    { 'K', { 5, 5, 6, 5, 5 } }, { 'L', { 4, 4, 4, 4, 7 } },
    { 'M', { 5, 7, 7, 5, 5 } }, { 'N', { 6, 5, 5, 5, 5 } },
    { 'O', { 7, 5, 5, 5, 7 } }, { 'P', { 6, 5, 6, 4, 4 } },
    { 'Q', { 7, 5, 5, 7, 1 } }, { 'R', { 6, 5, 6, 5, 5 } },
    { 'S', { 3, 4, 2, 1, 6 } }, { 'T', { 7, 2, 2, 2, 2 } },
    { 'U', { 5, 5, 5, 5, 7 } }, { 'V', { 5, 5, 5, 5, 2 } },
    { 'W', { 5, 5, 7, 7, 5 } }, { 'X', { 5, 5, 2, 5, 5 } },
    { 'Y', { 5, 5, 2, 2, 2 } }, { 'Z', { 7, 1, 2, 4, 7 } },
    { '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } },
    { '2', { 7, 1, 7, 4, 7 } }, { '3', { 7, 1, 3, 1, 7 } },
    { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } },
    { '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 2, 2, 2 } },
    { '8', { 7, 5, 7, 5, 7 } }, { '9', { 7, 5, 7, 1, 7 } },
    { '-', { 0, 0, 7, 0, 0 } }, { ' ', { 0, 0, 0, 0, 0 } },
};

static const glyph_t *find_glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    for (size_t index = 0; index < sizeof(FONT) / sizeof(FONT[0]);
         ++index) {
        if (FONT[index].ch == ch) {
            return &FONT[index];
        }
    }
    return NULL;
}

/* ------------------------------------------------------- sheet buffer */

typedef struct {
    uint8_t *rgb;               /* 3 bytes per pixel */
    int width;
    int height;
} sheet_t;

static sheet_t sheet_new(int width, int height)
{
    sheet_t sheet = { calloc((size_t)width * height, 3), width, height };
    if (sheet.rgb == NULL) {
        fprintf(stderr, "sheet alloc failed\n");
        exit(1);
    }
    /* dark slate background */
    for (int index = 0; index < width * height; ++index) {
        sheet.rgb[index * 3 + 0] = 18;
        sheet.rgb[index * 3 + 1] = 20;
        sheet.rgb[index * 3 + 2] = 24;
    }
    return sheet;
}

static void sheet_text(
    sheet_t *sheet, int x, int y, const char *text)
{
    for (; *text != '\0'; ++text, x += 4) {
        const glyph_t *glyph = find_glyph(*text);
        if (glyph == NULL) {
            continue;
        }
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if ((glyph->rows[row] & (4 >> col)) == 0) {
                    continue;
                }
                const int px = x + col;
                const int py = y + row;
                if (px < 0 || px >= sheet->width || py < 0 ||
                    py >= sheet->height) {
                    continue;
                }
                uint8_t *p = sheet->rgb +
                    ((size_t)py * sheet->width + px) * 3;
                p[0] = 220;
                p[1] = 224;
                p[2] = 230;
            }
        }
    }
}

/* nearest-neighbour 40x30 downsample of one frame; scale_up draws
 * each low-res pixel as a block so the exact pixels stay reviewable */
static void sheet_blit_nn40(
    sheet_t *sheet, int x0, int y0, const uint16_t *frame, int scale_up)
{
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 40; ++x) {
            const int sx = x * 4 + 2;
            const int sy = y * 4 + 2;
            const uint16_t pixel = frame[sy * FEA_FRAME_WIDTH + sx];
            const uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1f);
            const uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3f);
            const uint8_t b5 = (uint8_t)(pixel & 0x1f);
            const uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            const uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            const uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
            for (int by = 0; by < scale_up; ++by) {
                for (int bx = 0; bx < scale_up; ++bx) {
                    const int px = x0 + x * scale_up + bx;
                    const int py = y0 + y * scale_up + by;
                    if (px < 0 || px >= sheet->width || py < 0 ||
                        py >= sheet->height) {
                        continue;
                    }
                    uint8_t *out = sheet->rgb +
                        ((size_t)py * sheet->width + px) * 3;
                    out[0] = r;
                    out[1] = g;
                    out[2] = b;
                }
            }
        }
    }
}

static void sheet_blit(
    sheet_t *sheet, int x0, int y0, const uint16_t *frame)
{
    for (int y = 0; y < FEA_FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FEA_FRAME_WIDTH; ++x) {
            const uint16_t pixel = frame[y * FEA_FRAME_WIDTH + x];
            const int px = x0 + x;
            const int py = y0 + y;
            if (px < 0 || px >= sheet->width || py < 0 ||
                py >= sheet->height) {
                continue;
            }
            uint8_t *p = sheet->rgb +
                ((size_t)py * sheet->width + px) * 3;
            const uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1f);
            const uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3f);
            const uint8_t b5 = (uint8_t)(pixel & 0x1f);
            p[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            p[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            p[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
}

static void sheet_write(const sheet_t *sheet, const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", sheet->width, sheet->height);
    fwrite(sheet->rgb, 3, (size_t)sheet->width * sheet->height, file);
    fclose(file);
}

/* ------------------------------------------------------- key helpers */

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
    key.controls.expression = 3U;   /* speaking activity */
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U;                /* AA */
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_secondary = 1U;      /* E */
    key.viseme_blend = 38U;
    key.speech_phase = 2U;          /* ACTIVE */
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = 2U;
    return key;
}

static face_stage_cue_t emotion_cue(uint8_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100U + expression);
    cue.expression = expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static uint8_t triangle_u8(uint32_t frame, uint32_t period)
{
    const uint32_t phase = frame % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

/* mirror of the production quality-probe motion scenario */
static face_render_key_t motion_key(uint32_t frame)
{
    face_render_key_t key = base_key();
    const uint8_t jaw_wave = triangle_u8(frame + 7U, 42U);
    const uint8_t form_wave = triangle_u8(frame + 19U, 74U);
    const uint8_t gaze_wave = triangle_u8(frame + 11U, 120U);
    key.controls.mouth_open =
        (uint8_t)(24U + (uint16_t)jaw_wave * 204U / 255U);
    key.controls.mouth_width =
        (uint8_t)(104U + (uint16_t)form_wave * 104U / 255U);
    key.controls.mouth_round =
        (uint8_t)(218U - (uint16_t)form_wave * 174U / 255U);
    key.controls.mouth_teeth =
        (uint8_t)(24U + (uint16_t)jaw_wave * 104U / 255U);
    key.controls.look_x = (int8_t)((int32_t)gaze_wave / 3 - 42);
    key.controls.look_y =
        (int8_t)((int32_t)triangle_u8(frame + 37U, 156U) / 6 - 21);
    key.audio_level =
        (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_blend = form_wave;
    key.head_roll =
        (int8_t)((int32_t)triangle_u8(frame + 13U, 180U) / 12 - 10);
    return key;
}

static face_stage_cue_t motion_cue(void)
{
    face_stage_cue_t cue = emotion_cue(2U /* joy */);
    cue.start_sample = SAMPLE_RATE;
    cue.attack_samples = SAMPLE_RATE;
    cue.hold_samples = SAMPLE_RATE * 2U;
    cue.release_samples = SAMPLE_RATE;
    cue.flags = 0U;
    cue.gesture = FACE_GESTURE_NOD;
    cue.intensity = 238U;
    return cue;
}

/* 16f strip cue: LINEAR attack spanning 12 of the 16 frames so the
 * acting curve's anticipation dip occupies ~2 frames and the settle
 * lands in the final ~2 frames, all inside one sheet */
static face_stage_cue_t strip_cue(uint32_t first_frame)
{
    face_stage_cue_t cue = emotion_cue(2U /* joy */);
    cue.start_sample =
        (uint32_t)((uint64_t)(first_frame + 2U) * SAMPLE_RATE / 30U);
    cue.attack_samples = SAMPLE_RATE * 12U / 30U;
    cue.hold_samples = SAMPLE_RATE * 4U;
    cue.release_samples = SAMPLE_RATE;
    cue.easing = FACE_STAGE_EASE_LINEAR;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    cue.gesture = FACE_GESTURE_NONE;
    cue.intensity = 255U;
    return cue;
}

static void render_strip_frame(
    fea_profile_t profile, uint32_t first_frame, uint32_t frame,
    uint16_t *pixels)
{
    const uint32_t clock =
        (uint32_t)((uint64_t)frame * SAMPLE_RATE / 30U);
    face_render_key_t key = motion_key(frame);
    const face_stage_cue_t cue = strip_cue(first_frame);
    (void)face_stage_cue_apply(&cue, clock, &key);
    if (!fea_render_frame(profile, &key, clock, pixels,
                          FEA_PIXEL_COUNT)) {
        fprintf(stderr, "render failed\n");
        exit(1);
    }
}

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < (size_t)FEA_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static uint16_t frame_buffer[FEA_PIXEL_COUNT];

static void render_emotion(
    fea_profile_t profile, uint8_t emotion, uint32_t clock,
    uint16_t *pixels)
{
    face_render_key_t key = base_key();
    const face_stage_cue_t cue = emotion_cue(emotion);
    (void)face_stage_cue_apply(&cue, clock, &key);
    if (!fea_render_frame(profile, &key, clock, pixels,
                          FEA_PIXEL_COUNT)) {
        fprintf(stderr, "render failed\n");
        exit(1);
    }
}

static void render_motion(
    fea_profile_t profile, uint32_t frame, uint16_t *pixels)
{
    const uint32_t clock =
        (uint32_t)((uint64_t)frame * SAMPLE_RATE / 30U);
    face_render_key_t key = motion_key(frame);
    const face_stage_cue_t cue = motion_cue();
    (void)face_stage_cue_apply(&cue, clock, &key);
    if (!fea_render_frame(profile, &key, clock, pixels,
                          FEA_PIXEL_COUNT)) {
        fprintf(stderr, "render failed\n");
        exit(1);
    }
}

/* ------------------------------------------------------- commands */

static void cmd_hash(void)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        for (uint8_t emotion = 0; emotion < FACE_EXPRESSION_COUNT;
             ++emotion) {
            render_emotion(
                (fea_profile_t)profile, emotion, fixed_clock,
                frame_buffer);
            printf("%s expr%02u %08" PRIx32 "\n",
                   fea_profile_slug((fea_profile_t)profile), emotion,
                   frame_hash(frame_buffer));
        }
        for (uint32_t frame = 30U; frame < 130U; frame += 25U) {
            render_motion((fea_profile_t)profile, frame, frame_buffer);
            printf("%s frame%03u %08" PRIx32 "\n",
                   fea_profile_slug((fea_profile_t)profile), frame,
                   frame_hash(frame_buffer));
        }
        /* adversarial extremes */
        face_render_key_t key;
        memset(&key, 0x00, sizeof(key));
        (void)fea_render_frame(
            (fea_profile_t)profile, &key, 0U, frame_buffer,
            FEA_PIXEL_COUNT);
        printf("%s all00 %08" PRIx32 "\n",
               fea_profile_slug((fea_profile_t)profile),
               frame_hash(frame_buffer));
        memset(&key, 0xff, sizeof(key));
        (void)fea_render_frame(
            (fea_profile_t)profile, &key, 0xfffffff0U, frame_buffer,
            FEA_PIXEL_COUNT);
        printf("%s allff %08" PRIx32 "\n",
               fea_profile_slug((fea_profile_t)profile),
               frame_hash(frame_buffer));
    }
}

static void cmd_sheets(const char *dir)
{
    char path[1024];
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;

    /* 1. all-emotion sheet, native cell size */
    {
        const int width = LABEL_W + 11 * (FEA_FRAME_WIDTH + 2);
        const int height =
            LABEL_H + FEA_PROFILE_COUNT * (FEA_FRAME_HEIGHT + 2);
        sheet_t sheet = sheet_new(width, height);
        for (int emotion = 0; emotion < 11; ++emotion) {
            sheet_text(
                &sheet, LABEL_W + emotion * (FEA_FRAME_WIDTH + 2) + 2,
                2, EMOTION_NAMES[emotion]);
        }
        for (unsigned profile = 0; profile < FEA_PROFILE_COUNT;
             ++profile) {
            const int y0 =
                LABEL_H + (int)profile * (FEA_FRAME_HEIGHT + 2);
            sheet_text(
                &sheet, 2, y0 + 4,
                fea_profile_slug((fea_profile_t)profile) + 4);
            for (uint8_t emotion = 0; emotion < 11; ++emotion) {
                render_emotion(
                    (fea_profile_t)profile, emotion, fixed_clock,
                    frame_buffer);
                sheet_blit(
                    &sheet,
                    LABEL_W + emotion * (FEA_FRAME_WIDTH + 2), y0,
                    frame_buffer);
            }
        }
        snprintf(path, sizeof(path),
                 "%s/expression-actors-v4__11-emotions__native.ppm",
                 dir);
        sheet_write(&sheet, path);
        free(sheet.rgb);
    }

    /* 2. all-emotion sheet at exact 40x30 nearest-neighbour, plus a
     * x4 block-scaled copy of the same pixels for review */
    for (int scale = 1; scale <= 4; scale += 3) {
        const int cell_w = 40 * scale;
        const int cell_h = 30 * scale;
        const int width = LABEL_W + 11 * (cell_w + 2);
        const int height =
            LABEL_H + FEA_PROFILE_COUNT * (cell_h + 2);
        sheet_t sheet = sheet_new(width, height);
        for (int emotion = 0; emotion < 11; ++emotion) {
            sheet_text(
                &sheet, LABEL_W + emotion * (cell_w + 2) + 2, 2,
                EMOTION_NAMES[emotion]);
        }
        for (unsigned profile = 0; profile < FEA_PROFILE_COUNT;
             ++profile) {
            const int y0 = LABEL_H + (int)profile * (cell_h + 2);
            sheet_text(
                &sheet, 2, y0 + 4,
                fea_profile_slug((fea_profile_t)profile) + 4);
            for (uint8_t emotion = 0; emotion < 11; ++emotion) {
                render_emotion(
                    (fea_profile_t)profile, emotion, fixed_clock,
                    frame_buffer);
                sheet_blit_nn40(
                    &sheet, LABEL_W + emotion * (cell_w + 2), y0,
                    frame_buffer, scale);
            }
        }
        snprintf(path, sizeof(path),
                 scale == 1
                     ? "%s/expression-actors-v4__11-emotions__40x30-nn.ppm"
                     : "%s/expression-actors-v4__11-emotions__40x30-nn__x4.ppm",
                 dir);
        sheet_write(&sheet, path);
        free(sheet.rgb);
    }

    /* 3. temporal sheets: 16 frames at 30 fps per actor, native and
     * 40x30-nn x4; the strip cue puts anticipation in frames ~2-4 and
     * settle in the final two frames */
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        const uint32_t first_frame = 24U;
        for (int lowres = 0; lowres < 2; ++lowres) {
            const int cell_w = lowres != 0 ? 160 : FEA_FRAME_WIDTH;
            const int cell_h = lowres != 0 ? 120 : FEA_FRAME_HEIGHT;
            const int columns = 4;
            const int rows = 4;
            const int width = columns * (cell_w + 2) + 2;
            const int height = rows * (cell_h + LABEL_H + 2) + 2;
            sheet_t sheet = sheet_new(width, height);
            for (int cell = 0; cell < 16; ++cell) {
                const uint32_t frame = first_frame + (uint32_t)cell;
                const int cx = cell % columns;
                const int cy = cell / columns;
                const int x0 = 2 + cx * (cell_w + 2);
                const int y0 = 2 + cy * (cell_h + LABEL_H + 2);
                char label[40];
                const char *phase =
                    cell < 2 ? "PRE"
                    : cell < 5 ? "ANTIC"
                    : cell < 12 ? "ACTIVE"
                    : cell < 15 ? "OVER-SETTLE" : "HOLD";
                snprintf(label, sizeof(label), "F%02d %s", cell,
                         phase);
                sheet_text(&sheet, x0 + 2, y0 + 1, label);
                render_strip_frame(
                    (fea_profile_t)profile, first_frame, frame,
                    frame_buffer);
                if (lowres != 0) {
                    sheet_blit_nn40(
                        &sheet, x0, y0 + LABEL_H, frame_buffer, 4);
                } else {
                    sheet_blit(&sheet, x0, y0 + LABEL_H, frame_buffer);
                }
            }
            snprintf(path, sizeof(path),
                     lowres != 0
                         ? "%s/expression-actors-v4__temporal__%s__16f__40x30-nn__x4.ppm"
                         : "%s/expression-actors-v4__temporal__%s__16f__native.ppm",
                     dir, fea_profile_slug((fea_profile_t)profile) + 4);
            sheet_write(&sheet, path);
            free(sheet.rgb);
        }
    }

    /* 4. viseme sheets: native and 40x30-nn x4 */
    for (int lowres = 0; lowres < 2; ++lowres) {
        const int cell_w = lowres != 0 ? 160 : FEA_FRAME_WIDTH;
        const int cell_h = lowres != 0 ? 120 : FEA_FRAME_HEIGHT;
        const int width = LABEL_W + 15 * (cell_w + 2);
        const int height =
            LABEL_H + FEA_PROFILE_COUNT * (cell_h + 2);
        sheet_t sheet = sheet_new(width, height);
        for (int viseme = 0; viseme < 15; ++viseme) {
            sheet_text(
                &sheet, LABEL_W + viseme * (cell_w + 2) + 2, 2,
                VISEME_NAMES[viseme]);
        }
        for (unsigned profile = 0; profile < FEA_PROFILE_COUNT;
             ++profile) {
            const int y0 = LABEL_H + (int)profile * (cell_h + 2);
            sheet_text(
                &sheet, 2, y0 + 4,
                fea_profile_slug((fea_profile_t)profile) + 4);
            for (uint8_t viseme = 0; viseme < 15; ++viseme) {
                face_render_key_t key = base_key();
                key.viseme = viseme;
                key.viseme_weight = 255U;
                key.viseme_blend = 0U;
                if (!fea_render_frame(
                        (fea_profile_t)profile, &key, fixed_clock,
                        frame_buffer, FEA_PIXEL_COUNT)) {
                    exit(1);
                }
                if (lowres != 0) {
                    sheet_blit_nn40(
                        &sheet, LABEL_W + viseme * (cell_w + 2), y0,
                        frame_buffer, 4);
                } else {
                    sheet_blit(
                        &sheet, LABEL_W + viseme * (cell_w + 2), y0,
                        frame_buffer);
                }
            }
        }
        snprintf(path, sizeof(path),
                 lowres != 0
                     ? "%s/expression-actors-v4__15-visemes__40x30-nn__x4.ppm"
                     : "%s/expression-actors-v4__15-visemes__native.ppm",
                 dir);
        sheet_write(&sheet, path);
        free(sheet.rgb);
    }
    printf("v4 sheets written to %s\n", dir);
}

static void cmd_cell(
    const char *profile_arg, const char *emotion_arg, const char *path)
{
    const int profile = atoi(profile_arg);
    const int emotion = atoi(emotion_arg);
    if (profile < 0 || profile >= (int)FEA_PROFILE_COUNT ||
        emotion < 0 || emotion >= FACE_EXPRESSION_COUNT) {
        fprintf(stderr, "bad cell arguments\n");
        exit(1);
    }
    render_emotion(
        (fea_profile_t)profile, (uint8_t)emotion,
        SAMPLE_RATE * 7U + 211U, frame_buffer);
    sheet_t sheet = sheet_new(FEA_FRAME_WIDTH, FEA_FRAME_HEIGHT);
    sheet_blit(&sheet, 0, 0, frame_buffer);
    sheet_write(&sheet, path);
    free(sheet.rgb);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "hash") == 0) {
        cmd_hash();
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "sheets") == 0) {
        cmd_sheets(argv[2]);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "cell") == 0) {
        cmd_cell(argv[2], argv[3], argv[4]);
        return 0;
    }
    fprintf(stderr,
            "usage: fea_dump hash | sheets <dir> | cell <p> <e> <f>\n");
    return 2;
}
