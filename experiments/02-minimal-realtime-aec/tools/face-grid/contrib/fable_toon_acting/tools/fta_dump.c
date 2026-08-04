/*
 * fta_dump — deterministic frame dumper for fable_toon_acting.
 *
 * Modes:
 *   fta_dump hash                     print FNV-1a32 per (profile, case)
 *   fta_dump sheets <outdir>          labeled PPM contact sheets
 *   fta_dump frames <slug> <script> <outdir> <count> <fps>
 *                                     numbered PPM frames for GIFs
 *
 * The expression sheet reproduces the firmware quality-probe scenario
 * exactly: the same mid-speech base key, the same instant full-intensity
 * REPLACE cue per expression (via the real face_stage_cue_apply), the
 * same frozen sample clock.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_stage.h"
#include "fta.h"

enum {
    SAMPLE_RATE = 16000,
    EXPRESSION_CLOCK = SAMPLE_RATE * 7 + 211,
    LABEL_GUTTER = 64,
    LABEL_HEADER = 12,
};

/* ---- tiny 3x5 font ----------------------------------------------------- */

static uint16_t glyph_bits(char c)
{
    /* 15 bits: 5 rows x 3 columns, MSB-first per row */
    switch (c) {
    case 'A': return 0x7b6dU; /* 111 101 101 101 101 -> see note */
    default: break;
    }
    return 0U;
}

/* rows encoded (r0<<12)|(r1<<9)|(r2<<6)|(r3<<3)|r4 */
static uint16_t glyph_rows(char c)
{
    switch (c) {
    case 'A': return (0x7U << 12) | (0x5U << 9) | (0x7U << 6) | (0x5U << 3) | 0x5U;
    case 'B': return (0x6U << 12) | (0x5U << 9) | (0x6U << 6) | (0x5U << 3) | 0x6U;
    case 'C': return (0x7U << 12) | (0x4U << 9) | (0x4U << 6) | (0x4U << 3) | 0x7U;
    case 'D': return (0x6U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x6U;
    case 'E': return (0x7U << 12) | (0x4U << 9) | (0x7U << 6) | (0x4U << 3) | 0x7U;
    case 'F': return (0x7U << 12) | (0x4U << 9) | (0x7U << 6) | (0x4U << 3) | 0x4U;
    case 'G': return (0x7U << 12) | (0x4U << 9) | (0x5U << 6) | (0x5U << 3) | 0x7U;
    case 'H': return (0x5U << 12) | (0x5U << 9) | (0x7U << 6) | (0x5U << 3) | 0x5U;
    case 'I': return (0x7U << 12) | (0x2U << 9) | (0x2U << 6) | (0x2U << 3) | 0x7U;
    case 'J': return (0x3U << 12) | (0x1U << 9) | (0x1U << 6) | (0x5U << 3) | 0x7U;
    case 'K': return (0x5U << 12) | (0x5U << 9) | (0x6U << 6) | (0x5U << 3) | 0x5U;
    case 'L': return (0x4U << 12) | (0x4U << 9) | (0x4U << 6) | (0x4U << 3) | 0x7U;
    case 'M': return (0x5U << 12) | (0x7U << 9) | (0x7U << 6) | (0x5U << 3) | 0x5U;
    case 'N': return (0x6U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x5U;
    case 'O': return (0x7U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x7U;
    case 'P': return (0x7U << 12) | (0x5U << 9) | (0x7U << 6) | (0x4U << 3) | 0x4U;
    case 'Q': return (0x7U << 12) | (0x5U << 9) | (0x5U << 6) | (0x7U << 3) | 0x1U;
    case 'R': return (0x7U << 12) | (0x5U << 9) | (0x6U << 6) | (0x5U << 3) | 0x5U;
    case 'S': return (0x7U << 12) | (0x4U << 9) | (0x7U << 6) | (0x1U << 3) | 0x7U;
    case 'T': return (0x7U << 12) | (0x2U << 9) | (0x2U << 6) | (0x2U << 3) | 0x2U;
    case 'U': return (0x5U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x7U;
    case 'V': return (0x5U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x2U;
    case 'W': return (0x5U << 12) | (0x5U << 9) | (0x7U << 6) | (0x7U << 3) | 0x5U;
    case 'X': return (0x5U << 12) | (0x5U << 9) | (0x2U << 6) | (0x5U << 3) | 0x5U;
    case 'Y': return (0x5U << 12) | (0x5U << 9) | (0x2U << 6) | (0x2U << 3) | 0x2U;
    case 'Z': return (0x7U << 12) | (0x1U << 9) | (0x2U << 6) | (0x4U << 3) | 0x7U;
    case '0': return (0x7U << 12) | (0x5U << 9) | (0x5U << 6) | (0x5U << 3) | 0x7U;
    case '1': return (0x2U << 12) | (0x6U << 9) | (0x2U << 6) | (0x2U << 3) | 0x7U;
    case '2': return (0x7U << 12) | (0x1U << 9) | (0x7U << 6) | (0x4U << 3) | 0x7U;
    case '3': return (0x7U << 12) | (0x1U << 9) | (0x3U << 6) | (0x1U << 3) | 0x7U;
    case '4': return (0x5U << 12) | (0x5U << 9) | (0x7U << 6) | (0x1U << 3) | 0x1U;
    case '5': return (0x7U << 12) | (0x4U << 9) | (0x7U << 6) | (0x1U << 3) | 0x7U;
    case '6': return (0x7U << 12) | (0x4U << 9) | (0x7U << 6) | (0x5U << 3) | 0x7U;
    case '7': return (0x7U << 12) | (0x1U << 9) | (0x2U << 6) | (0x2U << 3) | 0x2U;
    case '8': return (0x7U << 12) | (0x5U << 9) | (0x7U << 6) | (0x5U << 3) | 0x7U;
    case '9': return (0x7U << 12) | (0x5U << 9) | (0x7U << 6) | (0x1U << 3) | 0x7U;
    case '-': return (0x0U << 12) | (0x0U << 9) | (0x7U << 6) | (0x0U << 3) | 0x0U;
    default: return 0U;
    }
}

/* ---- RGB helpers ------------------------------------------------------- */

typedef struct {
    uint8_t *rgb;   /* 3 bytes per pixel */
    int width;
    int height;
} sheet_t;

static void sheet_clear(sheet_t *sheet, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < sheet->width * sheet->height; ++i) {
        sheet->rgb[i * 3 + 0] = r;
        sheet->rgb[i * 3 + 1] = g;
        sheet->rgb[i * 3 + 2] = b;
    }
}

static void sheet_blit_565(
    sheet_t *sheet, int x0, int y0, const uint16_t *frame)
{
    for (int y = 0; y < FTA_FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FTA_FRAME_WIDTH; ++x) {
            const uint16_t pixel = frame[y * FTA_FRAME_WIDTH + x];
            const int index =
                ((y0 + y) * sheet->width + (x0 + x)) * 3;
            const uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1fU);
            const uint8_t g6 = (uint8_t)((pixel >> 5) & 0x3fU);
            const uint8_t b5 = (uint8_t)(pixel & 0x1fU);
            sheet->rgb[index + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            sheet->rgb[index + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            sheet->rgb[index + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
}

static void sheet_text(
    sheet_t *sheet, int x0, int y0, const char *text, int scale)
{
    int cursor = x0;
    for (const char *c = text; *c != '\0'; ++c) {
        char upper = *c;
        if (upper >= 'a' && upper <= 'z') {
            upper = (char)(upper - 'a' + 'A');
        }
        const uint16_t rows = glyph_rows(upper);
        for (int row = 0; row < 5; ++row) {
            const uint16_t bits = (uint16_t)((rows >> ((4 - row) * 3)) & 0x7U);
            for (int col = 0; col < 3; ++col) {
                if ((bits & (0x4U >> col)) == 0U) {
                    continue;
                }
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        const int px = cursor + col * scale + sx;
                        const int py = y0 + row * scale + sy;
                        if (px < 0 || px >= sheet->width || py < 0 ||
                            py >= sheet->height) {
                            continue;
                        }
                        const int index = (py * sheet->width + px) * 3;
                        sheet->rgb[index + 0] = 235U;
                        sheet->rgb[index + 1] = 235U;
                        sheet->rgb[index + 2] = 235U;
                    }
                }
            }
        }
        cursor += 4 * scale;
    }
}

static int sheet_write_ppm(const sheet_t *sheet, const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fprintf(file, "P6\n%d %d\n255\n", sheet->width, sheet->height);
    fwrite(sheet->rgb, 1, (size_t)(sheet->width * sheet->height * 3), file);
    fclose(file);
    return 0;
}

/* ---- scenario keys ----------------------------------------------------- */

/* mirrors firmware-ws/tests/face_render_quality.c base_render_key() */
static face_render_key_t base_render_key(void)
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
    key.controls.look_x = 0;
    key.controls.look_y = 0;
    key.controls.brow = 0;
    key.controls.expression = 0U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U; /* AA */
    key.phoneme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 1U; /* E */
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_sample = 0U;
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gesture = FACE_GESTURE_NONE;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

/* neutral quiet key for idle scripts */
static face_render_key_t idle_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_width = 128U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.viseme = 14U; /* SIL */
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 255U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.affect_arousal = 72U;
    key.attention = 192U;
    key.expression_weight = 0U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

/*
 * Values transcribed from the host viseme shape table
 * (firmware-ws/main/face_viseme.c s_shapes): open/width/round/press/teeth.
 */
static const uint8_t VISEME_SHAPES[15][5] = {
    {236, 205, 24, 0, 18},  {155, 246, 0, 0, 128}, {102, 255, 0, 0, 155},
    {214, 112, 255, 0, 16}, {112, 82, 244, 0, 10}, {12, 164, 18, 255, 0},
    {66, 224, 0, 0, 210},   {88, 190, 0, 0, 235},  {82, 194, 0, 0, 164},
    {38, 198, 0, 176, 235}, {120, 181, 20, 0, 78}, {72, 202, 0, 0, 142},
    {104, 148, 94, 0, 72},  {86, 158, 46, 34, 184}, {0, 110, 30, 0, 0},
};

static const char *const VISEME_LABELS[15] = {
    "AA", "E", "I", "O", "U", "PP", "SS", "TH",
    "DD", "FF", "KK", "NN", "RR", "CH", "SIL",
};

static const char *const EXPRESSION_LABELS[FACE_EXPRESSION_COUNT] = {
    "NEUTRAL", "WARM", "JOY", "CONCERN", "SURPRISE", "THOUGHT",
    "SKEPTIC", "DETERMIN", "SLEEPY", "EXCITED", "EMBARRAS",
};

static face_render_key_t viseme_key(int viseme)
{
    face_render_key_t key = idle_key();
    key.controls.mouth_open = VISEME_SHAPES[viseme][0];
    key.controls.mouth_width = VISEME_SHAPES[viseme][1];
    key.controls.mouth_round = VISEME_SHAPES[viseme][2];
    key.controls.mouth_press = VISEME_SHAPES[viseme][3];
    key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = (uint8_t)viseme;
    key.viseme_weight = 255U;
    key.audio_level = 170U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 120U;
    return key;
}

/* ---- hash mode --------------------------------------------------------- */

static uint32_t fnv1a32(const uint16_t *frame)
{
    uint32_t hash = 2166136261U;
    for (int i = 0; i < FTA_PIXEL_COUNT; ++i) {
        hash ^= (uint32_t)(frame[i] & 0xffU);
        hash *= 16777619U;
        hash ^= (uint32_t)(frame[i] >> 8);
        hash *= 16777619U;
    }
    return hash;
}

static uint16_t framebuffer[FTA_PIXEL_COUNT];

static int run_hash(void)
{
    for (size_t profile = 0; profile < fta_profile_count(); ++profile) {
        /* expression cases at the probe clock */
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)expression);
            face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
            if (!fta_render_frame(
                    (fta_profile_t)profile, &key, EXPRESSION_CLOCK,
                    framebuffer, FTA_PIXEL_COUNT)) {
                return 1;
            }
            printf(
                "%s expr %02d %08x\n",
                fta_profile_slug((fta_profile_t)profile), expression,
                fnv1a32(framebuffer));
        }
        /* idle and viseme cases across clocks */
        for (int step = 0; step < 8; ++step) {
            const uint32_t clock = (uint32_t)step * 40033U + 7U;
            face_render_key_t key =
                (step & 1) != 0 ? viseme_key(step % 15) : idle_key();
            if (!fta_render_frame(
                    (fta_profile_t)profile, &key, clock, framebuffer,
                    FTA_PIXEL_COUNT)) {
                return 1;
            }
            printf(
                "%s clock %02d %08x\n",
                fta_profile_slug((fta_profile_t)profile), step,
                fnv1a32(framebuffer));
        }
    }
    return 0;
}

/* ---- sheets mode ------------------------------------------------------- */

static int write_expression_sheet(const char *outdir)
{
    const int columns = FACE_EXPRESSION_COUNT;
    const int rows = (int)fta_profile_count();
    sheet_t sheet;
    sheet.width = LABEL_GUTTER + columns * FTA_FRAME_WIDTH;
    sheet.height = LABEL_HEADER + rows * FTA_FRAME_HEIGHT;
    sheet.rgb = malloc((size_t)(sheet.width * sheet.height * 3));
    if (sheet.rgb == NULL) {
        return 1;
    }
    sheet_clear(&sheet, 12U, 12U, 16U);
    for (int column = 0; column < columns; ++column) {
        sheet_text(
            &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH + 4, 3,
            EXPRESSION_LABELS[column], 1);
    }
    for (int row = 0; row < rows; ++row) {
        sheet_text(
            &sheet, 4, LABEL_HEADER + row * FTA_FRAME_HEIGHT + 6,
            fta_profile_slug((fta_profile_t)row), 1);
        for (int column = 0; column < columns; ++column) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)column);
            face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
            if (!fta_render_frame(
                    (fta_profile_t)row, &key, EXPRESSION_CLOCK,
                    framebuffer, FTA_PIXEL_COUNT)) {
                free(sheet.rgb);
                return 1;
            }
            sheet_blit_565(
                &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH,
                LABEL_HEADER + row * FTA_FRAME_HEIGHT, framebuffer);
        }
    }
    char path[512];
    snprintf(
        path, sizeof(path),
        "%s/toon-acting__3-styles__11-stage-expressions__mid-speech.ppm",
        outdir);
    const int result = sheet_write_ppm(&sheet, path);
    free(sheet.rgb);
    return result;
}

static int write_viseme_sheet(const char *outdir)
{
    const int columns = 15;
    const int rows = (int)fta_profile_count();
    sheet_t sheet;
    sheet.width = LABEL_GUTTER + columns * FTA_FRAME_WIDTH;
    sheet.height = LABEL_HEADER + rows * FTA_FRAME_HEIGHT;
    sheet.rgb = malloc((size_t)(sheet.width * sheet.height * 3));
    if (sheet.rgb == NULL) {
        return 1;
    }
    sheet_clear(&sheet, 12U, 12U, 16U);
    for (int column = 0; column < columns; ++column) {
        sheet_text(
            &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH + 4, 3,
            VISEME_LABELS[column], 1);
    }
    for (int row = 0; row < rows; ++row) {
        sheet_text(
            &sheet, 4, LABEL_HEADER + row * FTA_FRAME_HEIGHT + 6,
            fta_profile_slug((fta_profile_t)row), 1);
        for (int column = 0; column < columns; ++column) {
            face_render_key_t key = viseme_key(column);
            if (!fta_render_frame(
                    (fta_profile_t)row, &key, 88011U, framebuffer,
                    FTA_PIXEL_COUNT)) {
                free(sheet.rgb);
                return 1;
            }
            sheet_blit_565(
                &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH,
                LABEL_HEADER + row * FTA_FRAME_HEIGHT, framebuffer);
        }
    }
    char path[512];
    snprintf(
        path, sizeof(path),
        "%s/toon-acting__3-styles__15-ovr-visemes.ppm", outdir);
    const int result = sheet_write_ppm(&sheet, path);
    free(sheet.rgb);
    return result;
}

/*
 * Motion strips: 10 frames at 33 ms steps around a blink, a nod cue,
 * and a speech jaw cycle for the flagship style.
 */
static int write_motion_strip(const char *outdir)
{
    const int columns = 10;
    const int rows = 3;
    static const char *const ROW_LABELS[3] = {
        "BLINK", "NOD", "SPEECH",
    };
    sheet_t sheet;
    sheet.width = LABEL_GUTTER + columns * FTA_FRAME_WIDTH;
    sheet.height = LABEL_HEADER + rows * FTA_FRAME_HEIGHT;
    sheet.rgb = malloc((size_t)(sheet.width * sheet.height * 3));
    if (sheet.rgb == NULL) {
        return 1;
    }
    sheet_clear(&sheet, 12U, 12U, 16U);
    for (int column = 0; column < columns; ++column) {
        char label[8];
        snprintf(label, sizeof(label), "F%d", column);
        sheet_text(
            &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH + 4, 3,
            label, 1);
    }
    /*
     * Locate a real blink with the solver: scan the deterministic
     * timeline until the aperture starts dropping, then center the
     * strip on the event (2 frames of lead so the pre-blink dip and the
     * slow reopen are both visible).
     */
    uint32_t blink_start = 0U;
    for (uint32_t clock = 0U; clock < 400000U; clock += 266U) {
        face_render_key_t key = idle_key();
        fta_rig_t rig;
        if (!fta_solve(FTA_PROFILE_TOON_BEAN, &key, clock, &rig)) {
            free(sheet.rgb);
            return 1;
        }
        if (rig.eye[0].openness_q8 < 150U) {
            blink_start = clock > 1066U ? clock - 1066U : 0U;
            break;
        }
    }
    if (blink_start == 0U) {
        fprintf(stderr, "motion strip: no blink found in scan window\n");
        free(sheet.rgb);
        return 1;
    }
    for (int row = 0; row < rows; ++row) {
        sheet_text(
            &sheet, 4, LABEL_HEADER + row * FTA_FRAME_HEIGHT + 6,
            ROW_LABELS[row], 1);
        for (int column = 0; column < columns; ++column) {
            face_render_key_t key = idle_key();
            uint32_t clock = 0U;
            if (row == 0) {
                clock = blink_start + (uint32_t)column * 533U;
            } else if (row == 1) {
                face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_WARM);
                cue.gesture = FACE_GESTURE_NOD;
                cue.start_sample = SAMPLE_RATE;
                cue.attack_samples = SAMPLE_RATE / 4U;
                cue.flags = FACE_STAGE_FLAG_HOLD_FINAL |
                            FACE_STAGE_FLAG_LOOP_GESTURE;
                clock = SAMPLE_RATE + (uint32_t)column * 640U;
                face_stage_cue_apply(&cue, clock, &key);
            } else {
                key = viseme_key((column * 3) % 15);
                clock = 30000U + (uint32_t)column * 533U;
            }
            if (!fta_render_frame(
                    FTA_PROFILE_TOON_BEAN, &key, clock, framebuffer,
                    FTA_PIXEL_COUNT)) {
                free(sheet.rgb);
                return 1;
            }
            sheet_blit_565(
                &sheet, LABEL_GUTTER + column * FTA_FRAME_WIDTH,
                LABEL_HEADER + row * FTA_FRAME_HEIGHT, framebuffer);
        }
    }
    char path[512];
    snprintf(
        path, sizeof(path),
        "%s/toon-acting__bean__motion-strips.ppm", outdir);
    const int result = sheet_write_ppm(&sheet, path);
    free(sheet.rgb);
    return result;
}

/* ---- frames mode (for GIFs) -------------------------------------------- */

static void script_key(
    const char *script, uint32_t clock, face_render_key_t *key)
{
    if (strcmp(script, "speech") == 0) {
        /* pseudo-phrase: cycle visemes with a wobbling level */
        const uint32_t beat = (clock / 2400U) % 15U;
        *key = viseme_key((int)beat);
        const uint32_t wobble = (clock / 300U) % 64U;
        key->audio_level = (uint8_t)(120U + wobble * 2U);
        key->controls.mouth_open = (uint8_t)(
            (key->controls.mouth_open * (128U + wobble)) / 192U);
        return;
    }
    if (strcmp(script, "emotions") == 0) {
        /* one expression per 1.5 s with a 0.35 s attack and release */
        const uint32_t slot = clock / 24000U;
        const uint32_t expression = slot % FACE_EXPRESSION_COUNT;
        *key = idle_key();
        face_stage_cue_t cue =
            expression_cue((face_expression_t)expression);
        cue.start_sample = slot * 24000U;
        cue.attack_samples = 5600U;
        cue.hold_samples = 12800U;
        cue.release_samples = 5600U;
        cue.flags = 0U;
        cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
        face_stage_cue_apply(&cue, clock, key);
        return;
    }
    if (strcmp(script, "gestures") == 0) {
        static const uint8_t GESTURES[5] = {
            FACE_GESTURE_NOD, FACE_GESTURE_SHAKE, FACE_GESTURE_TILT,
            FACE_GESTURE_LEAN_IN, FACE_GESTURE_BOUNCE,
        };
        const uint32_t slot = clock / 20000U;
        *key = idle_key();
        face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_WARM);
        cue.gesture = GESTURES[slot % 5U];
        cue.start_sample = slot * 20000U;
        cue.attack_samples = 2400U;
        cue.hold_samples = 9600U;
        cue.release_samples = 4800U;
        cue.flags = FACE_STAGE_FLAG_LOOP_GESTURE;
        cue.intensity = 220U;
        face_stage_cue_apply(&cue, clock, key);
        return;
    }
    /* idle */
    *key = idle_key();
    if ((clock / 60000U) % 3U == 1U) {
        key->controls.expression = 1U; /* listening beat */
    } else if ((clock / 60000U) % 3U == 2U) {
        key->controls.expression = 2U; /* thinking beat */
    }
}

static int run_frames(
    const char *slug, const char *script, const char *outdir,
    int count, int fps)
{
    fta_profile_t profile = FTA_PROFILE_COUNT;
    for (size_t candidate = 0; candidate < fta_profile_count();
         ++candidate) {
        if (strcmp(fta_profile_slug((fta_profile_t)candidate), slug) == 0) {
            profile = (fta_profile_t)candidate;
        }
    }
    if (profile == FTA_PROFILE_COUNT || count <= 0 || fps <= 0) {
        fprintf(stderr, "unknown profile %s or bad count/fps\n", slug);
        return 1;
    }
    sheet_t sheet;
    sheet.width = FTA_FRAME_WIDTH;
    sheet.height = FTA_FRAME_HEIGHT;
    sheet.rgb = malloc((size_t)(sheet.width * sheet.height * 3));
    if (sheet.rgb == NULL) {
        return 1;
    }
    for (int frame = 0; frame < count; ++frame) {
        const uint32_t clock = (uint32_t)(
            ((uint64_t)frame * SAMPLE_RATE) / (uint64_t)fps);
        face_render_key_t key;
        script_key(script, clock, &key);
        if (!fta_render_frame(
                profile, &key, clock, framebuffer, FTA_PIXEL_COUNT)) {
            free(sheet.rgb);
            return 1;
        }
        sheet_blit_565(&sheet, 0, 0, framebuffer);
        char path[512];
        snprintf(
            path, sizeof(path), "%s/frame_%04d.ppm", outdir, frame);
        if (sheet_write_ppm(&sheet, path) != 0) {
            free(sheet.rgb);
            return 1;
        }
    }
    free(sheet.rgb);
    return 0;
}

int main(int argc, char **argv)
{
    (void)glyph_bits;
    if (argc >= 2 && strcmp(argv[1], "hash") == 0) {
        return run_hash();
    }
    if (argc >= 3 && strcmp(argv[1], "sheets") == 0) {
        if (write_expression_sheet(argv[2]) != 0 ||
            write_viseme_sheet(argv[2]) != 0 ||
            write_motion_strip(argv[2]) != 0) {
            return 1;
        }
        printf("sheets written to %s\n", argv[2]);
        return 0;
    }
    if (argc >= 7 && strcmp(argv[1], "frames") == 0) {
        return run_frames(
            argv[2], argv[3], argv[4], atoi(argv[5]), atoi(argv[6]));
    }
    if (argc >= 5 && strcmp(argv[1], "cell") == 0) {
        /* fta_dump cell <profile-index> <expression> <out.ppm> */
        const int profile = atoi(argv[2]);
        const int expression = atoi(argv[3]);
        face_render_key_t key = base_render_key();
        const face_stage_cue_t cue =
            expression_cue((face_expression_t)expression);
        face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
        if (!fta_render_frame(
                (fta_profile_t)profile, &key, EXPRESSION_CLOCK,
                framebuffer, FTA_PIXEL_COUNT)) {
            return 1;
        }
        sheet_t sheet;
        sheet.width = FTA_FRAME_WIDTH;
        sheet.height = FTA_FRAME_HEIGHT;
        uint8_t rgb[FTA_PIXEL_COUNT * 3];
        sheet.rgb = rgb;
        sheet_blit_565(&sheet, 0, 0, framebuffer);
        return sheet_write_ppm(&sheet, argv[4]);
    }
    fprintf(
        stderr,
        "usage: fta_dump hash | sheets <outdir> | "
        "frames <slug> <script> <outdir> <count> <fps>\n");
    return 2;
}
