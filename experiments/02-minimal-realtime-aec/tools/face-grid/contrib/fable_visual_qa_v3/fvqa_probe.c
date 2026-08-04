/*
 * fvqa_probe: renders every registered production face renderer through
 * face_render_frame() + face_stage_cue_apply() and emits, per profile:
 *
 *   - an enhanced contact sheet (PPM): 11 stage emotions at half res with
 *     border-contact overlay, the same 11 emotions at contact scale (4x
 *     box downscale, shown 2x nearest), an 11-frame temporal storyboard
 *     of a 6 s motion sweep, and a per-transition delta sparkline with
 *     pop markers;
 *   - advisory metrics in acceptance.json: edge clipping, mask topology,
 *     dead-eye / silent-mouth region response, detached mouth corners,
 *     contact-scale separability, temporal pops, and nearest structural
 *     neighbor (clone suspects).
 *
 * Usage: fvqa_probe OUTPUT_DIR
 *
 * Numeric results are ADVISORY ONLY. They rank suspects for human review
 * of the sheets; they can never promote a renderer.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_render.h"
#include "face_stage.h"
#include "fvqa_metrics.h"

enum {
    SAMPLE_RATE = 16000,
    FRAMES_PER_SECOND = 30,
    EXPRESSION_CLOCK = SAMPLE_RATE * 7 + 211, /* matches the gate */
    MOTION_FRAME_COUNT = 180,
    MOTION_TRANSITIONS = MOTION_FRAME_COUNT - 1,
    STORYBOARD_CELLS = 11,
    /* sheet geometry */
    GUTTER = 16,
    LABEL_H = 8,
    CELL_W = 80,
    CELL_H = 60,
    SHEET_W = GUTTER + (int)FACE_EXPRESSION_COUNT * CELL_W,
    SPARK_H = 22,
    SHEET_H = LABEL_H + CELL_H + 2 + CELL_H + 2 + LABEL_H + CELL_H + 2 +
              SPARK_H,
    /* advisory thresholds; see REPORT.md for rationale */
    DEAD_EYE_CENTI = 120,
    SILENT_MOUTH_CENTI = 100,
    CLIP_RUN_LIMIT = 12,
    MASK_RELIABLE_FG_PERMILLE = 600,
    CLONE_SUSPECT_PERMILLE = 260,
    CONTACT_WEAK_PAIR_CENTI = 140,
};

/* face_render_profile_t identifiers in enum order; the C API exposes only
 * slugs, but the audit must cite exact legacy IDs. Checked against
 * FACE_RENDER_PROFILE_COUNT at compile time. */
static const char *const LEGACY_ENUM_IDS[] = {
    "FACE_RENDER_EGA_QUEST",
    "FACE_RENDER_VGA_ELDER",
    "FACE_RENDER_TALKIE_CLOSEUP",
    "FACE_RENDER_PIXEL_AUTOMATON",
    "FACE_RENDER_AMBER_TERMINAL",
    "FACE_RENDER_POCKET_RPG",
    "FACE_RENDER_DITHERED_ROGUE",
    "FACE_RENDER_VECTOR_ROUNDED",
    "FACE_RENDER_COZMO_CUBIC",
    "FACE_RENDER_ROBOEYES_ALERT",
    "FACE_RENDER_ROBOEYES_SOFT",
    "FACE_RENDER_M5_AVATAR_CLASSIC",
    "FACE_RENDER_M5_AVATAR_MANGA",
    "FACE_RENDER_EVE_MINIMAL",
    "FACE_RENDER_JIBO_ORB",
    "FACE_RENDER_SACCADE_LAB",
    "FACE_RENDER_BROW_DIALOGUE",
    "FACE_RENDER_LID_ANTICIPATION",
    "FACE_RENDER_IRIS_PARALLAX",
    "FACE_RENDER_SLEEP_WAKE",
    "FACE_RENDER_CURIOUS_TILT",
    "FACE_RENDER_DOT_MATRIX_EYES",
    "FACE_RENDER_CAT_OPTICS",
    "FACE_RENDER_PRESTON_SPRITES",
    "FACE_RENDER_POLYGON_JALI",
    "FACE_RENDER_BEZIER_RIBBON",
    "FACE_RENDER_TEETH_TONGUE",
    "FACE_RENDER_LED_VU_MOUTH",
    "FACE_RENDER_ORIGAMI_MASK",
    "FACE_RENDER_NEON_SDF_CYAN",
    "FACE_RENDER_NEON_SDF_MAGENTA",
    "FACE_RENDER_LIQUID_SMIN",
    "FACE_RENDER_CRT_CHROMATIC",
    "FACE_RENDER_HOLO_WIREFRAME",
    "FACE_RENDER_VOICE_ORB",
    "FACE_RENDER_RED_OPTIC",
    "FACE_RENDER_HUB75_NEON",
    "FACE_RENDER_EDGE_GLOW",
    "FACE_RENDER_GLITCH_MASK",
    "FACE_RENDER_PALETTE_PLASMA",
    "FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED",
    "FACE_RENDER_ROBOT_RIG_COZMO_CUBIC",
    "FACE_RENDER_ROBOT_RIG_BROW_DIALOGUE",
    "FACE_RENDER_ROBOT_RIG_SLEEP_WAKE",
    "FACE_RENDER_ROBOT_RIG_IRIS_PARALLAX",
    "FACE_RENDER_ROBOT_RIG_CAT_OPTICS",
    "FACE_RENDER_ROBOT_RIG_M5_MANGA",
    "FACE_RENDER_SPRITE_VGA_STAR_NAVIGATOR",
    "FACE_RENDER_SPRITE_POCKET_RELAY_CREATURE",
    "FACE_RENDER_PIXEL_PACK_EGA_QUEST",
    "FACE_RENDER_PIXEL_PACK_VGA_ELDER",
    "FACE_RENDER_PIXEL_PACK_TALKIE_CLOSEUP",
    "FACE_RENDER_PIXEL_PACK_DITHERED_ROGUE",
    "FACE_RENDER_TOON_BEAN",
    "FACE_RENDER_TOON_INK",
    "FACE_RENDER_TOON_EMBER",
    "FACE_RENDER_SPRITE_ACTOR_EGA_COURT_MAGE",
    "FACE_RENDER_SPRITE_ACTOR_VGA_STAR_CAPTAIN",
    "FACE_RENDER_SPRITE_ACTOR_TALKIE_MOON_MECHANIC",
    "FACE_RENDER_SPRITE_ACTOR_JRPG_STORM_FAMILIAR",
    "FACE_RENDER_SPRITE_ACTOR_HANDHELD_FOREST_PET",
    "FACE_RENDER_SPRITE_ACTOR_ARCADE_CHROME_PILOT",
};

_Static_assert(
    sizeof(LEGACY_ENUM_IDS) / sizeof(LEGACY_ENUM_IDS[0]) ==
        FACE_RENDER_PROFILE_COUNT,
    "legacy enum table must cover every registered profile");

/* ---------------------------------------------------------------- state */

static uint16_t frame_rgb565[FVQA_PIXELS];
static uint8_t emotion_luma[FACE_EXPRESSION_COUNT][FVQA_PIXELS];
static uint16_t emotion_rgb[FACE_EXPRESSION_COUNT][FVQA_PIXELS];
static uint8_t emotion_small[FACE_EXPRESSION_COUNT][FVQA_SMALL_PIXELS];
static uint8_t emotion_mask[FACE_EXPRESSION_COUNT][FVQA_PIXELS];
static fvqa_border_contact_t emotion_border[FACE_EXPRESSION_COUNT];
static int emotion_components[FACE_EXPRESSION_COUNT];
static fvqa_mouth_report_t emotion_mouth[FACE_EXPRESSION_COUNT];
static uint32_t emotion_fg_permille[FACE_EXPRESSION_COUNT];
static uint32_t eye_delta[FACE_EXPRESSION_COUNT];
static uint32_t mouth_delta[FACE_EXPRESSION_COUNT];

static uint16_t story_rgb[STORYBOARD_CELLS][FVQA_PIXELS];
static uint8_t prev_luma[FVQA_PIXELS];
static uint8_t cur_luma[FVQA_PIXELS];
static uint8_t motion_mask[FVQA_PIXELS];
static uint32_t motion_delta_centi[MOTION_TRANSITIONS];
static int motion_components[MOTION_FRAME_COUNT];

static fvqa_cc_scratch_t scratch;

typedef struct {
    uint32_t neutral[FVQA_SIG_WORDS];
    uint32_t joy[FVQA_SIG_WORDS];
    uint32_t concern[FVQA_SIG_WORDS];
    uint32_t surprise[FVQA_SIG_WORDS];
} profile_signature_t;

static profile_signature_t signatures[FACE_RENDER_PROFILE_COUNT];

static uint8_t sheet[SHEET_W * SHEET_H * 3];

/* ------------------------------------------------------------- fixture */

static face_render_key_t base_render_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 152;
    key.controls.mouth_width = 168;
    key.controls.mouth_round = 82;
    key.controls.mouth_press = 12;
    key.controls.mouth_teeth = 76;
    key.controls.eye_left_open = 238;
    key.controls.eye_right_open = 238;
    key.controls.expression = 0;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 10; /* FACE_VISEME_AA in the OVR15 vocabulary */
    key.viseme_weight = 220;
    key.audio_level = 154;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 11;
    key.viseme_blend = 38;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96;
    key.attention = 220;
    key.expression_weight = 0;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    return key;
}

static face_stage_cue_t expression_cue(uint8_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100 + expression);
    cue.expression = expression;
    cue.gesture = FACE_GESTURE_NONE;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.interrupt_mode = FACE_STAGE_INTERRUPT_BLEND;
    cue.intensity = 255;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

/* Symmetric triangle wave over `period` frames, 0..255. */
static uint8_t triangle_u8(uint32_t tick, uint32_t period)
{
    const uint32_t phase = tick % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)((phase * 255U) / half);
    }
    return (uint8_t)(255U - ((phase - half) * 255U) / (period - half));
}

static face_render_key_t motion_render_key(int frame)
{
    face_render_key_t key = base_render_key();
    const uint8_t jaw = triangle_u8((uint32_t)frame + 7U, 42U);
    const uint8_t form = triangle_u8((uint32_t)frame + 19U, 74U);
    key.controls.mouth_open = (uint8_t)(24U + ((uint32_t)jaw * 204U) / 255U);
    key.controls.mouth_teeth = (uint8_t)(24U + ((uint32_t)jaw * 104U) / 255U);
    key.audio_level = (uint8_t)(18U + ((uint32_t)jaw * 202U) / 255U);
    key.controls.mouth_width =
        (uint8_t)(104U + ((uint32_t)form * 104U) / 255U);
    key.controls.mouth_round =
        (uint8_t)(218U - ((uint32_t)form * 174U) / 255U);
    key.viseme_blend = form;
    key.controls.look_x =
        (int8_t)((int)triangle_u8((uint32_t)frame + 11U, 120U) / 3 - 42);
    key.controls.look_y =
        (int8_t)((int)triangle_u8((uint32_t)frame + 37U, 156U) / 6 - 21);
    key.head_roll =
        (int8_t)((int)triangle_u8((uint32_t)frame + 13U, 180U) / 12 - 10);
    return key;
}

static face_stage_cue_t motion_cue(void)
{
    face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_JOY);
    cue.gesture = FACE_GESTURE_NOD;
    cue.intensity = 238;
    cue.flags = 0;
    cue.start_sample = SAMPLE_RATE;          /* 1 s in */
    cue.attack_samples = SAMPLE_RATE;        /* 1 s rise */
    cue.hold_samples = 2U * SAMPLE_RATE;     /* 2 s hold */
    cue.release_samples = SAMPLE_RATE;       /* 1 s fall */
    return cue;
}

/* ------------------------------------------------------------- drawing */

static const uint16_t DIGIT_FONT[10] = {
    /* 3x5 glyphs, row-major, bit 14 = top-left */
    0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9,
    0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,
};

static void sheet_clear(void)
{
    memset(sheet, 8, sizeof(sheet));
}

static void sheet_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= SHEET_W || y < 0 || y >= SHEET_H) {
        return;
    }
    uint8_t *pixel = &sheet[(y * SHEET_W + x) * 3];
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
}

static void sheet_digit(int x, int y, int digit)
{
    const uint16_t glyph = DIGIT_FONT[digit % 10];
    for (int row = 0; row < 5; row++) {
        for (int column = 0; column < 3; column++) {
            if (glyph & (1U << (14 - (row * 3 + column)))) {
                sheet_pixel(x + column, y + row, 235, 240, 245);
            }
        }
    }
}

static void sheet_number(int x, int y, int value)
{
    sheet_digit(x, y, (value / 10) % 10);
    sheet_digit(x + 4, y, value % 10);
}

static void rgb565_to_rgb888(
    uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint32_t r5 = (pixel >> 11) & 0x1FU;
    const uint32_t g6 = (pixel >> 5) & 0x3FU;
    const uint32_t b5 = pixel & 0x1FU;
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

/* Draw an RGB565 frame at half resolution (2x2 average). */
static void sheet_half_frame(int x0, int y0, const uint16_t *frame)
{
    for (int y = 0; y < CELL_H; y++) {
        for (int x = 0; x < CELL_W; x++) {
            uint32_t sr = 0;
            uint32_t sg = 0;
            uint32_t sb = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    uint8_t r;
                    uint8_t g;
                    uint8_t b;
                    rgb565_to_rgb888(
                        frame[(y * 2 + dy) * FVQA_WIDTH + x * 2 + dx],
                        &r, &g, &b);
                    sr += r;
                    sg += g;
                    sb += b;
                }
            }
            sheet_pixel(
                x0 + x, y0 + y,
                (uint8_t)(sr / 4U), (uint8_t)(sg / 4U),
                (uint8_t)(sb / 4U));
        }
    }
}

/* Overlay red ticks where the full-res mask touches the frame border. */
static void sheet_border_overlay(int x0, int y0, const uint8_t *mask)
{
    for (int x = 0; x < FVQA_WIDTH; x++) {
        if (mask[x]) {
            sheet_pixel(x0 + x / 2, y0, 255, 40, 40);
        }
        if (mask[(FVQA_HEIGHT - 1) * FVQA_WIDTH + x]) {
            sheet_pixel(x0 + x / 2, y0 + CELL_H - 1, 255, 40, 40);
        }
    }
    for (int y = 0; y < FVQA_HEIGHT; y++) {
        if (mask[y * FVQA_WIDTH]) {
            sheet_pixel(x0, y0 + y / 2, 255, 40, 40);
        }
        if (mask[y * FVQA_WIDTH + FVQA_WIDTH - 1]) {
            sheet_pixel(x0 + CELL_W - 1, y0 + y / 2, 255, 40, 40);
        }
    }
}

/* Contact-scale cell: 40x30 grey view scaled 2x nearest to 80x60. */
static void sheet_contact_cell(int x0, int y0, const uint8_t *small)
{
    for (int y = 0; y < CELL_H; y++) {
        for (int x = 0; x < CELL_W; x++) {
            const uint8_t value =
                small[(y / 2) * FVQA_SMALL_WIDTH + x / 2];
            sheet_pixel(x0 + x, y0 + y, value, value, value);
        }
    }
}

static bool write_ppm(
    const char *path, const uint8_t *pixels, int width, int height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    const size_t expected = (size_t)width * (size_t)height * 3U;
    const bool ok = fwrite(pixels, 1, expected, file) == expected;
    fclose(file);
    return ok;
}

/* ------------------------------------------------------------ analysis */

static uint32_t foreground_permille(const uint8_t *mask)
{
    uint32_t lit = 0;
    for (int index = 0; index < FVQA_PIXELS; index++) {
        lit += mask[index];
    }
    return (lit * 1000U) / FVQA_PIXELS;
}

static void render_expressions(face_render_profile_t profile)
{
    for (uint8_t expression = 0; expression < FACE_EXPRESSION_COUNT;
         expression++) {
        face_render_key_t key = base_render_key();
        face_stage_cue_t cue = expression_cue(expression);
        face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
        if (!face_render_frame(
                profile, &key, EXPRESSION_CLOCK, frame_rgb565,
                FVQA_PIXELS)) {
            fprintf(stderr, "render failed: profile %d expression %d\n",
                    (int)profile, expression);
            exit(1);
        }
        memcpy(emotion_rgb[expression], frame_rgb565,
               sizeof(frame_rgb565));
        fvqa_luma_from_rgb565(
            frame_rgb565, emotion_luma[expression], FVQA_PIXELS);
        fvqa_box_downscale4(
            emotion_luma[expression], emotion_small[expression]);
        const uint8_t background =
            fvqa_background_mode(emotion_luma[expression]);
        fvqa_mask_build(
            emotion_luma[expression], background,
            emotion_mask[expression]);
        fvqa_border_contact(
            emotion_mask[expression], &emotion_border[expression]);
        emotion_components[expression] = fvqa_component_count(
            emotion_mask[expression], &scratch);
        fvqa_mouth_report(
            emotion_mask[expression], &scratch,
            &emotion_mouth[expression]);
        emotion_fg_permille[expression] =
            foreground_permille(emotion_mask[expression]);
    }
    for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
         expression++) {
        eye_delta[expression] = fvqa_band_delta_centi(
            emotion_luma[0], emotion_luma[expression],
            FVQA_EYE_BAND_TOP, FVQA_EYE_BAND_BOTTOM);
        mouth_delta[expression] = fvqa_band_delta_centi(
            emotion_luma[0], emotion_luma[expression],
            FVQA_MOUTH_BAND_TOP, FVQA_MOUTH_BAND_BOTTOM);
    }
    profile_signature_t *signature = &signatures[profile];
    fvqa_edge_signature(emotion_luma[0], signature->neutral);
    fvqa_edge_signature(
        emotion_luma[FACE_EXPRESSION_JOY], signature->joy);
    fvqa_edge_signature(
        emotion_luma[FACE_EXPRESSION_CONCERN], signature->concern);
    fvqa_edge_signature(
        emotion_luma[FACE_EXPRESSION_SURPRISE], signature->surprise);
}

static void render_motion(face_render_profile_t profile)
{
    const face_stage_cue_t cue = motion_cue();
    int story_slot = 0;
    for (int frame = 0; frame < MOTION_FRAME_COUNT; frame++) {
        const uint32_t clock =
            ((uint32_t)frame * SAMPLE_RATE) / FRAMES_PER_SECOND;
        face_render_key_t key = motion_render_key(frame);
        face_stage_cue_apply(&cue, clock, &key);
        if (!face_render_frame(
                profile, &key, clock, frame_rgb565, FVQA_PIXELS)) {
            fprintf(stderr, "motion render failed: profile %d frame %d\n",
                    (int)profile, frame);
            exit(1);
        }
        fvqa_luma_from_rgb565(frame_rgb565, cur_luma, FVQA_PIXELS);
        const uint8_t background = fvqa_background_mode(cur_luma);
        fvqa_mask_build(cur_luma, background, motion_mask);
        motion_components[frame] =
            fvqa_component_count(motion_mask, &scratch);
        if (frame > 0) {
            motion_delta_centi[frame - 1] = fvqa_band_delta_centi(
                prev_luma, cur_luma, 0, FVQA_HEIGHT);
        }
        memcpy(prev_luma, cur_luma, sizeof(prev_luma));
        if (frame % 17 == 0 && story_slot < STORYBOARD_CELLS) {
            memcpy(story_rgb[story_slot], frame_rgb565,
                   sizeof(frame_rgb565));
            story_slot++;
        }
    }
}

typedef struct {
    uint32_t median;
    uint32_t maximum;
    int maximum_index;
    uint32_t threshold;
    int pops;
    int pop_frames[16];
    int component_jumps;
    int component_jump_frames[16];
    int frozen;
} motion_summary_t;

static int compare_u32(const void *a, const void *b)
{
    const uint32_t left = *(const uint32_t *)a;
    const uint32_t right = *(const uint32_t *)b;
    return left < right ? -1 : left > right ? 1 : 0;
}

static void summarize_motion(motion_summary_t *out)
{
    static uint32_t sorted[MOTION_TRANSITIONS];
    memcpy(sorted, motion_delta_centi, sizeof(sorted));
    qsort(sorted, MOTION_TRANSITIONS, sizeof(sorted[0]), compare_u32);
    out->median = sorted[MOTION_TRANSITIONS / 2];
    out->maximum = 0;
    out->maximum_index = 0;
    out->frozen = 0;
    for (int index = 0; index < MOTION_TRANSITIONS; index++) {
        if (motion_delta_centi[index] > out->maximum) {
            out->maximum = motion_delta_centi[index];
            out->maximum_index = index;
        }
        if (motion_delta_centi[index] == 0U) {
            out->frozen++;
        }
    }
    /* Pop = a transition several times the typical one; mirrors the
     * spirit of the production gate but in the luminance domain. */
    uint32_t threshold = out->median * 7U;
    if (threshold < 350U) {
        threshold = 350U;
    }
    out->threshold = threshold;
    out->pops = 0;
    for (int index = 0; index < MOTION_TRANSITIONS; index++) {
        if (motion_delta_centi[index] > threshold) {
            if (out->pops < 16) {
                out->pop_frames[out->pops] = index + 1;
            }
            out->pops++;
        }
    }
    out->component_jumps = 0;
    for (int frame = 1; frame < MOTION_FRAME_COUNT; frame++) {
        const int step =
            motion_components[frame] - motion_components[frame - 1];
        if (step >= 2 || step <= -2) {
            if (out->component_jumps < 16) {
                out->component_jump_frames[out->component_jumps] =
                    frame;
            }
            out->component_jumps++;
        }
    }
}

/* Contact-scale separability: pairwise mean deltas at 40x30. */
static void contact_scale_pairs(
    uint32_t *mean_centi, int *weak_pairs)
{
    uint32_t total = 0;
    int weak = 0;
    int pairs = 0;
    for (int a = 0; a < FACE_EXPRESSION_COUNT; a++) {
        for (int b = a + 1; b < FACE_EXPRESSION_COUNT; b++) {
            uint32_t sum = 0;
            for (int index = 0; index < FVQA_SMALL_PIXELS; index++) {
                const int delta = (int)emotion_small[a][index] -
                                  (int)emotion_small[b][index];
                sum += (uint32_t)(delta < 0 ? -delta : delta);
            }
            const uint32_t centi = (sum * 100U) / FVQA_SMALL_PIXELS;
            total += centi;
            if (centi < CONTACT_WEAK_PAIR_CENTI) {
                weak++;
            }
            pairs++;
        }
    }
    *mean_centi = total / (uint32_t)pairs;
    *weak_pairs = weak;
}

/* --------------------------------------------------------------- sheet */

static void draw_sheet(int profile_index, const motion_summary_t *motion)
{
    sheet_clear();
    sheet_number(3, 1, profile_index);
    const int row_emotions = LABEL_H;
    const int row_contact = row_emotions + CELL_H + 2;
    const int row_story_label = row_contact + CELL_H + 2;
    const int row_story = row_story_label + LABEL_H;
    const int row_spark = row_story + CELL_H + 2;
    for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
         expression++) {
        const int x0 = GUTTER + expression * CELL_W;
        sheet_number(x0 + 2, 1, expression);
        sheet_half_frame(x0, row_emotions, emotion_rgb[expression]);
        sheet_border_overlay(
            x0, row_emotions, emotion_mask[expression]);
        sheet_contact_cell(
            x0, row_contact, emotion_small[expression]);
    }
    for (int slot = 0; slot < STORYBOARD_CELLS; slot++) {
        const int x0 = GUTTER + slot * CELL_W;
        sheet_number(x0 + 2, row_story_label + 1, slot * 17);
        sheet_half_frame(x0, row_story, story_rgb[slot]);
    }
    /* Sparkline: one 4px column per ~1 transition, red where popped. */
    const int spark_width = SHEET_W - GUTTER - 4;
    for (int transition = 0; transition < MOTION_TRANSITIONS;
         transition++) {
        const int x = GUTTER + (transition * spark_width) /
                                   MOTION_TRANSITIONS;
        uint32_t value = motion_delta_centi[transition];
        uint32_t scale = motion->threshold * 2U;
        if (scale < 100U) {
            scale = 100U;
        }
        int height = (int)((value * (uint32_t)(SPARK_H - 2)) / scale);
        if (height > SPARK_H - 2) {
            height = SPARK_H - 2;
        }
        const bool popped = value > motion->threshold;
        for (int dy = 0; dy <= height; dy++) {
            sheet_pixel(
                x, row_spark + SPARK_H - 2 - dy,
                popped ? 255 : 90,
                popped ? 60 : 140,
                popped ? 60 : 190);
        }
    }
}

/* ---------------------------------------------------------------- json */

static void json_expression_block(FILE *out, int expression)
{
    const fvqa_border_contact_t *border = &emotion_border[expression];
    const fvqa_mouth_report_t *mouth = &emotion_mouth[expression];
    fprintf(
        out,
        "        {\"border\": {\"left\": %u, \"right\": %u, "
        "\"top\": %u, \"bottom\": %u, \"max_run\": %u}, "
        "\"components\": %d, \"fg_permille\": %u, "
        "\"eye_delta_centi\": %u, \"mouth_delta_centi\": %u, "
        "\"mouth_band_components\": %u, "
        "\"mouth_corners_detached\": %s}",
        border->left, border->right, border->top, border->bottom,
        border->max_run, emotion_components[expression],
        emotion_fg_permille[expression], eye_delta[expression],
        mouth_delta[expression], mouth->band_components,
        mouth->corners_detached ? "true" : "false");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIR\n", argv[0]);
        return 2;
    }
    const char *output_dir = argv[1];
    const size_t count = face_render_profile_count();
    if (count != FACE_RENDER_PROFILE_COUNT) {
        fprintf(stderr, "profile count mismatch\n");
        return 2;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/acceptance.json", output_dir);
    FILE *json = fopen(path, "w");
    if (json == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    fprintf(json,
            "{\n  \"schema_version\": 1,\n"
            "  \"advisory\": \"Metrics flag suspects only; promotion "
            "requires human review of sheets and storyboards.\",\n"
            "  \"expression_clock\": %d,\n  \"profiles\": [\n",
            (int)EXPRESSION_CLOCK);

    static motion_summary_t motions[FACE_RENDER_PROFILE_COUNT];

    for (size_t profile = 0; profile < count; profile++) {
        render_expressions((face_render_profile_t)profile);
        render_motion((face_render_profile_t)profile);
        motion_summary_t *motion = &motions[profile];
        summarize_motion(motion);
        uint32_t contact_mean;
        int contact_weak;
        contact_scale_pairs(&contact_mean, &contact_weak);

        draw_sheet((int)profile, motion);
        snprintf(path, sizeof(path), "%s/sheets-ppm/%s.ppm",
                 output_dir,
                 face_render_profile_slug(
                     (face_render_profile_t)profile));
        if (!write_ppm(path, sheet, SHEET_W, SHEET_H)) {
            fprintf(stderr, "cannot write %s\n", path);
            return 2;
        }

        /* Advisory suspect flags. */
        bool dead_eyes = true;
        bool mouth_active = false;
        bool has_mouth_pixels = false;
        int clip_emotions = 0;
        int corner_detach_count = 0;
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             expression++) {
            if (expression > 0 &&
                eye_delta[expression] >= DEAD_EYE_CENTI) {
                dead_eyes = false;
            }
            if (expression > 0 &&
                mouth_delta[expression] >= SILENT_MOUTH_CENTI) {
                mouth_active = true;
            }
            if (emotion_mouth[expression].largest_area >=
                FVQA_MIN_COMPONENT_AREA) {
                has_mouth_pixels = true;
            }
            if (emotion_border[expression].max_run >= CLIP_RUN_LIMIT &&
                emotion_fg_permille[expression] <
                    MASK_RELIABLE_FG_PERMILLE) {
                clip_emotions++;
            }
            if (emotion_mouth[expression].corners_detached) {
                corner_detach_count++;
            }
        }

        fprintf(json,
                "    {\n      \"index\": %zu,\n"
                "      \"legacy_enum\": \"%s\",\n"
                "      \"slug\": \"%s\",\n      \"name\": \"%s\",\n"
                "      \"family\": \"%s\",\n",
                profile, LEGACY_ENUM_IDS[profile],
                face_render_profile_slug(
                    (face_render_profile_t)profile),
                face_render_profile_name(
                    (face_render_profile_t)profile),
                face_render_profile_family_name(
                    (face_render_profile_t)profile));
        fprintf(json, "      \"expressions\": [\n");
        for (int expression = 0; expression < FACE_EXPRESSION_COUNT;
             expression++) {
            json_expression_block(json, expression);
            fprintf(json, "%s\n",
                    expression < FACE_EXPRESSION_COUNT - 1 ? "," : "");
        }
        fprintf(json, "      ],\n");
        fprintf(json,
                "      \"flags\": {\"dead_eyes\": %s, "
                "\"silent_mouth\": %s, \"no_mouth\": %s, "
                "\"clip_suspect_emotions\": %d, "
                "\"corner_detach_emotions\": %d},\n",
                dead_eyes ? "true" : "false",
                (has_mouth_pixels && !mouth_active) ? "true" : "false",
                has_mouth_pixels ? "false" : "true",
                clip_emotions, corner_detach_count);
        fprintf(json,
                "      \"contact_scale\": {\"mean_pair_delta_centi\": "
                "%u, \"weak_pairs\": %d},\n",
                contact_mean, contact_weak);
        fprintf(json,
                "      \"motion\": {\"median_delta_centi\": %u, "
                "\"max_delta_centi\": %u, \"max_transition\": %d, "
                "\"pop_threshold_centi\": %u, \"pops\": %d, "
                "\"pop_frames\": [",
                motion->median, motion->maximum,
                motion->maximum_index + 1, motion->threshold,
                motion->pops);
        for (int index = 0; index < motion->pops && index < 16;
             index++) {
            fprintf(json, "%s%d", index > 0 ? ", " : "",
                    motion->pop_frames[index]);
        }
        fprintf(json,
                "], \"component_jumps\": %d, "
                "\"component_jump_frames\": [",
                motion->component_jumps);
        for (int index = 0;
             index < motion->component_jumps && index < 16; index++) {
            fprintf(json, "%s%d", index > 0 ? ", " : "",
                    motion->component_jump_frames[index]);
        }
        fprintf(json, "], \"frozen_transitions\": %d}\n", motion->frozen);
        fprintf(json, "    }%s\n",
                profile < count - 1 ? "," : "");
        fprintf(stderr, "profile %02zu done\n", profile);
    }

    /* Structural similarity across profiles: average signature distance
     * over four emotions. The closest pairs are always reported so clone
     * and palette-swap suspects are visible even above the threshold. */
    enum { CLOSEST_PAIRS = 20 };
    static uint32_t pair_distance[FACE_RENDER_PROFILE_COUNT]
                                 [FACE_RENDER_PROFILE_COUNT];
    for (size_t a = 0; a < count; a++) {
        for (size_t b = a + 1; b < count; b++) {
            pair_distance[a][b] =
                (fvqa_signature_distance_permille(
                     signatures[a].neutral, signatures[b].neutral) +
                 fvqa_signature_distance_permille(
                     signatures[a].joy, signatures[b].joy) +
                 fvqa_signature_distance_permille(
                     signatures[a].concern, signatures[b].concern) +
                 fvqa_signature_distance_permille(
                     signatures[a].surprise, signatures[b].surprise)) /
                4U;
        }
    }
    fprintf(json, "  ],\n  \"clone_pairs\": [\n");
    bool first_pair = true;
    for (size_t a = 0; a < count; a++) {
        for (size_t b = a + 1; b < count; b++) {
            if (pair_distance[a][b] < CLONE_SUSPECT_PERMILLE) {
                fprintf(json,
                        "%s    {\"a\": \"%s\", \"b\": \"%s\", "
                        "\"distance_permille\": %u}",
                        first_pair ? "" : ",\n",
                        face_render_profile_slug(
                            (face_render_profile_t)a),
                        face_render_profile_slug(
                            (face_render_profile_t)b),
                        pair_distance[a][b]);
                first_pair = false;
            }
        }
    }
    fprintf(json, "%s  ],\n  \"closest_pairs\": [\n",
            first_pair ? "" : "\n");
    bool used[FACE_RENDER_PROFILE_COUNT][FACE_RENDER_PROFILE_COUNT] = {
        {false}};
    for (int rank = 0; rank < CLOSEST_PAIRS; rank++) {
        uint32_t best = UINT32_MAX;
        size_t best_a = 0;
        size_t best_b = 0;
        for (size_t a = 0; a < count; a++) {
            for (size_t b = a + 1; b < count; b++) {
                if (!used[a][b] && pair_distance[a][b] < best) {
                    best = pair_distance[a][b];
                    best_a = a;
                    best_b = b;
                }
            }
        }
        used[best_a][best_b] = true;
        fprintf(json,
                "    {\"a\": \"%s\", \"b\": \"%s\", "
                "\"distance_permille\": %u}%s\n",
                face_render_profile_slug((face_render_profile_t)best_a),
                face_render_profile_slug((face_render_profile_t)best_b),
                best, rank < CLOSEST_PAIRS - 1 ? "," : "");
    }
    fprintf(json, "  ]\n}\n");
    fclose(json);
    printf("fvqa_probe: %zu profiles analyzed into %s\n", count,
           output_dir);
    return 0;
}
