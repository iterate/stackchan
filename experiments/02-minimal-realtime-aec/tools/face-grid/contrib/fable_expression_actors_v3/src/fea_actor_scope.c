#include "fea_internal.h"

/*
 * mono-scope — eye-only cyclops robot (v4).
 *
 * One large lens IS the face. A stable bright eye-light mass carries
 * all acting: upper/lower shutter lids (per-eye IR channels tilt
 * them), a smile-cut lower edge parented to the mouth-corner/curve
 * channels, a dark pupil with wide horizontal saccade travel, and a
 * brow bar plus attention halo. There is deliberately no mouth panel:
 * speech lives in the light — jaw stretches it, width/round/press
 * reshape it, teeth adds a bright scanline, tongue a warm under-core,
 * and the audio envelope pulses the aperture.
 */

enum {
    MS_CX = 80 << 4,
    MS_CY = 62 << 4,
};

static const uint16_t MS_BG = FEA_RGB(30, 32, 38);
static const uint16_t MS_PANEL = FEA_RGB(44, 47, 54);
static const uint16_t MS_PANEL_EDGE = FEA_RGB(58, 62, 72);
static const uint16_t MS_RING = FEA_RGB(112, 118, 128);
static const uint16_t MS_GLASS = FEA_RGB(13, 15, 21);
static const uint16_t MS_IRIS = FEA_RGB(64, 214, 200);
static const uint16_t MS_IRIS_HOT = FEA_RGB(180, 255, 244);
static const uint16_t MS_PUPIL = FEA_RGB(8, 28, 28);
static const uint16_t MS_LID = FEA_RGB(38, 40, 46);
static const uint16_t MS_LID_LIGHT = FEA_RGB(255, 176, 84);
static const uint16_t MS_BROW = FEA_RGB(255, 186, 92);
static const uint16_t MS_TONGUE_CORE = FEA_RGB(255, 196, 120);
static const uint16_t MS_BLUSH = FEA_RGB(226, 120, 110);

/* per-emotion eye-light aspect: width/height Q8 deltas, light offset,
 * extra top-lid cover, smile-cut gain */
typedef struct {
    int16_t dw_q8;
    int16_t dh_q8;
    int16_t dx_q4;
    int16_t dy_q4;
    int16_t top_lid_q8;      /* extra upper shutter cover */
    int16_t smile_q8;        /* extra lower smile-cut gain */
    int16_t dim;             /* light alpha delta */
} ms_aspect_t;

static const ms_aspect_t MS_ASPECTS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = { 0, 0, 0, 0, 0, 0, 0 },
    [FACE_EXPRESSION_WARM] = { 0, -20, 0, 0, 0, 55, 0 },
    [FACE_EXPRESSION_JOY] = { 20, 0, 0, 0, 0, 105, 0 },
    [FACE_EXPRESSION_CONCERN] = { -30, -45, 0, 12, 60, 0, -2 },
    [FACE_EXPRESSION_SURPRISE] = { -60, 40, 0, -8, 0, 0, 2 },
    [FACE_EXPRESSION_THOUGHTFUL] = { -25, -30, -36, -20, 70, 0, -4 },
    [FACE_EXPRESSION_SKEPTICAL] = { -15, -50, 12, 0, 90, 0, -1 },
    [FACE_EXPRESSION_DETERMINED] = { 80, -110, 0, 0, 60, 0, 2 },
    [FACE_EXPRESSION_SLEEPY] = { 10, -60, 0, 16, 40, 0, -5 },
    [FACE_EXPRESSION_EXCITED] = { 40, 30, 0, -4, 0, 60, 3 },
    [FACE_EXPRESSION_EMBARRASSED] = { -35, -50, 20, 22, 50, 30, -2 },
};

typedef struct {
    fea_pt_t lens;
    int32_t lens_r_q4;
    int32_t open_q8;
    int32_t lid_tilt_q4;
    fea_pt_t light;             /* eye-light center after gaze */
    int32_t half_w_q4;
    int32_t half_h_q4;
    int32_t radius_q4;          /* light corner radius */
    fea_pt_t pupil;
    int32_t pupil_half_w_q4;
    int32_t pupil_half_h_q4;
    int32_t smile_cut_q4;       /* lower-edge lift (parented corners) */
    int32_t smile_tilt_q4;      /* corner asymmetry */
    int32_t light_alpha;
    int32_t lower_lid_q4;
    fea_pt_t brow_l, brow_r;
    int32_t ring_alpha;
    int32_t teeth_band;         /* bright scanline strength */
    int32_t tongue_core;        /* warm under-core strength */
    int32_t blush_alpha;
} ms_layout_t;

static void ms_layout(
    const fea_pose_t *pose, uint32_t clock, ms_layout_t *lay)
{
    const int32_t act = pose->act_q8;
    const ms_aspect_t *aspect = &MS_ASPECTS[pose->emotion];

    lay->lens = fea_place(pose, MS_CX, MS_CY, 0, 0);
    lay->lens_r_q4 = 32 << 4;

    /* both per-eye channels drive the one lens: min is the aperture,
     * the difference tilts the shutter line (wink -> quizzical tilt) */
    const int32_t open_l = pose->eye_open_q8[0];
    const int32_t open_r = pose->eye_open_q8[1];
    lay->open_q8 = open_l < open_r ? open_l : open_r;
    lay->lid_tilt_q4 = ((open_l - open_r) * 48) >> 8;
    lay->lid_tilt_q4 +=
        ((pose->lid_tilt_q8[0] + pose->lid_tilt_q8[1]) * 28) >> 8;

    /* stable bright eye-light mass; mouth channels reshape it */
    int32_t half_w = ((20 << 4) * pose->pupil_scale_q8) >> 8;
    int32_t half_h = ((15 << 4) * pose->pupil_scale_q8) >> 8;
    half_w = (half_w * pose->mouth_w_q8) >> 8;
    half_w -= (pose->round_q8 * 4 * 16) >> 8;
    half_h += (pose->jaw_q8 * 5 * 16) >> 8;
    half_h -= (pose->press_q8 * 5 * 16) >> 8;
    if (pose->speech_phase == FACE_SPEECH_ACTIVE &&
        pose->speaking != 0U) {
        half_h += ((int32_t)pose->audio_q8 * 3 * 16) / 255;
    }
    half_w += (aspect->dw_q8 * ((20 << 4) / 4) * act >> 8) >> 6;
    half_h += (aspect->dh_q8 * ((15 << 4) / 4) * act >> 8) >> 6;
    half_w = fea_clamp_i32(half_w, 9 << 4, 25 << 4);
    half_h = fea_clamp_i32(half_h, 4 << 4, 21 << 4);
    lay->half_w_q4 = half_w;
    lay->half_h_q4 = half_h;
    lay->radius_q4 = half_h / 2 + ((pose->round_q8 * half_h / 2) >> 8);

    /* gaze: light shifts a little, pupil a lot (visible saccades) */
    const int32_t gx = (pose->gaze_x_q8 * 5 * 16) >> 8;
    const int32_t gy = (pose->gaze_y_q8 * 4 * 16) >> 8;
    const int32_t light_max_x = lay->lens_r_q4 - half_w - 32;
    const int32_t light_max_y = lay->lens_r_q4 - half_h - 32;
    lay->light.x_q4 = lay->lens.x_q4 +
        fea_clamp_i32(gx + ((aspect->dx_q4 * act) >> 8),
                      -light_max_x, light_max_x);
    lay->light.y_q4 = lay->lens.y_q4 +
        fea_clamp_i32(gy + ((aspect->dy_q4 * act) >> 8),
                      -light_max_y, light_max_y);
    lay->pupil_half_w_q4 = half_w * 5 / 14;
    lay->pupil_half_h_q4 = half_h * 5 / 14;
    const int32_t pupil_max_x = half_w - lay->pupil_half_w_q4 - 12;
    const int32_t pupil_max_y = half_h - lay->pupil_half_h_q4 - 8;
    lay->pupil.x_q4 = lay->light.x_q4 +
        fea_clamp_i32((pose->gaze_x_q8 * 9 * 16) >> 8,
                      -pupil_max_x, pupil_max_x);
    lay->pupil.y_q4 = lay->light.y_q4 +
        fea_clamp_i32((pose->gaze_y_q8 * 6 * 16) >> 8,
                      -pupil_max_y, pupil_max_y);
    if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        lay->pupil.y_q4 +=
            (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U /
                                    52000U)) * 3 * act) >> 16;
    }

    /* smile cut: the light's lower edge is parented to the mouth
     * corners and curve — joy arcs it up, concern sags it */
    const int32_t corner_avg =
        (pose->corner_q8[0] + pose->corner_q8[1]) / 2;
    int32_t smile = (corner_avg + pose->curve_q8 / 2) +
        ((aspect->smile_q8 * act) >> 8);
    lay->smile_cut_q4 =
        smile > 0 ? (smile * half_h / 2) >> 8
                  : (smile * half_h / 5) >> 8;
    if (lay->smile_cut_q4 > (half_h * 3) / 8) {
        lay->smile_cut_q4 = (half_h * 3) / 8;   /* keep the mass */
    }
    lay->smile_tilt_q4 =
        ((pose->corner_q8[0] - pose->corner_q8[1]) * half_w / 4) >> 8;

    lay->light_alpha = fea_clamp_i32(
        26 + ((int32_t)pose->sparkle * 6) / 255 +
            ((aspect->dim * act) >> 8),
        14, 32);
    lay->lower_lid_q4 =
        ((pose->lower_lid_q8[0] + pose->lower_lid_q8[1]) *
         (half_h / 2)) >> 9;

    /* brow bar */
    const int32_t brow_cy =
        -(44 << 4) -
        ((pose->brow_raise_q8[0] + pose->brow_raise_q8[1]) * 3 * 16 /
         2 >> 8);
    const int32_t tilt =
        ((pose->brow_tilt_q8[0] - pose->brow_tilt_q8[1]) * 3 * 16 /
         2 >> 8) + lay->lid_tilt_q4 / 2;
    const int32_t knit_q4 = (pose->brow_knit_q8 * 6 * 16) >> 8;
    lay->brow_l = fea_place(
        pose, MS_CX, MS_CY, -(26 << 4) + knit_q4 / 2, brow_cy - tilt);
    lay->brow_r = fea_place(
        pose, MS_CX, MS_CY, (26 << 4) - knit_q4 / 2, brow_cy + tilt);

    lay->ring_alpha = 4 + (((int32_t)pose->attention * 16) >> 8);
    if (pose->sparkle > 150U) {
        lay->ring_alpha +=
            (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U / 8000U)) *
             6) >> 14;
    }
    lay->teeth_band = pose->teeth_q8 > 80 ? pose->teeth_q8 : 0;
    lay->tongue_core = pose->tongue_q8 > 60 ? pose->tongue_q8 : 0;
    lay->blush_alpha = (pose->cheek_q8 * 22) >> 8;
}

void fea_scope_render(
    const fea_pose_t *pose, uint32_t clock, fea_canvas_t *canvas)
{
    ms_layout_t lay;
    ms_layout(pose, clock, &lay);

    fea_fill(canvas, MS_BG);
    fea_roundrect_q4(
        canvas, 14 << 4, 9 << 4, 146 << 4, 111 << 4, 10 << 4, MS_PANEL,
        32U);
    fea_roundrect_q4(
        canvas, 17 << 4, 12 << 4, 143 << 4, 108 << 4, 8 << 4,
        MS_PANEL_EDGE, 10U);

    /* halo, steel ring, glass */
    fea_ring_q4(
        canvas, lay.lens.x_q4, lay.lens.y_q4, lay.lens_r_q4 + (5 << 4),
        30, MS_IRIS,
        (uint32_t)fea_clamp_i32(lay.ring_alpha, 0, 20));
    fea_ring_q4(
        canvas, lay.lens.x_q4, lay.lens.y_q4, lay.lens_r_q4 + 28, 56,
        MS_RING, 32U);
    fea_ellipse_q4(
        canvas, lay.lens.x_q4, lay.lens.y_q4, lay.lens_r_q4,
        lay.lens_r_q4, MS_GLASS, 32U);

    /* eye-light: halo, mass, hot core */
    fea_glow_q4(
        canvas, lay.light.x_q4, lay.light.y_q4,
        lay.half_h_q4 < lay.half_w_q4 ? lay.half_h_q4 : lay.half_w_q4,
        (lay.half_w_q4 > lay.half_h_q4 ? lay.half_w_q4
                                       : lay.half_h_q4) + (8 << 4),
        MS_IRIS, 9U);
    fea_roundrect_q4(
        canvas, lay.light.x_q4 - lay.half_w_q4,
        lay.light.y_q4 - lay.half_h_q4,
        lay.light.x_q4 + lay.half_w_q4,
        lay.light.y_q4 + lay.half_h_q4, lay.radius_q4, MS_IRIS,
        (uint32_t)lay.light_alpha);
    fea_roundrect_q4(
        canvas, lay.light.x_q4 - (lay.half_w_q4 * 5) / 8,
        lay.light.y_q4 - (lay.half_h_q4 * 5) / 8,
        lay.light.x_q4 + (lay.half_w_q4 * 5) / 8,
        lay.light.y_q4 + (lay.half_h_q4 * 5) / 8,
        lay.radius_q4 / 2 + 12, MS_IRIS_HOT, 15U);

    /* tongue: warm under-core */
    if (lay.tongue_core > 0) {
        fea_roundrect_q4(
            canvas, lay.light.x_q4 - lay.half_w_q4 / 2,
            lay.light.y_q4 + lay.half_h_q4 / 4,
            lay.light.x_q4 + lay.half_w_q4 / 2,
            lay.light.y_q4 + (lay.half_h_q4 * 7) / 8,
            lay.half_h_q4 / 4 + 8, MS_TONGUE_CORE,
            (uint32_t)((lay.tongue_core * 14) >> 8));
    }
    /* teeth: bright scanline across the light */
    if (lay.teeth_band > 0) {
        fea_stroke_q4(
            canvas, lay.light.x_q4 - lay.half_w_q4 + 16,
            lay.light.y_q4 - lay.half_h_q4 / 3,
            lay.light.x_q4 + lay.half_w_q4 - 16,
            lay.light.y_q4 - lay.half_h_q4 / 3, 22, MS_IRIS_HOT,
            (uint32_t)((lay.teeth_band * 18) >> 8));
    }

    /* pupil + glint */
    fea_roundrect_q4(
        canvas, lay.pupil.x_q4 - lay.pupil_half_w_q4,
        lay.pupil.y_q4 - lay.pupil_half_h_q4,
        lay.pupil.x_q4 + lay.pupil_half_w_q4,
        lay.pupil.y_q4 + lay.pupil_half_h_q4,
        lay.pupil_half_h_q4 / 2 + 8, MS_PUPIL, 26U);
    fea_ellipse_q4(
        canvas, lay.pupil.x_q4 + lay.pupil_half_w_q4,
        lay.pupil.y_q4 - lay.pupil_half_h_q4, 26, 28, MS_IRIS_HOT,
        32U);
    if (pose->sparkle > 150U) {
        const int32_t ray = lay.half_w_q4 + 40;
        fea_stroke_q4(
            canvas, lay.light.x_q4 - ray, lay.light.y_q4,
            lay.light.x_q4 + ray, lay.light.y_q4, 16, MS_IRIS_HOT,
            16U);
        fea_stroke_q4(
            canvas, lay.light.x_q4, lay.light.y_q4 - ray,
            lay.light.x_q4, lay.light.y_q4 + ray, 16, MS_IRIS_HOT,
            16U);
    }

    /* smile cut: glass-colored bite out of the light's lower edge,
     * parented to the mouth corners; negative smile sags the light */
    if (lay.smile_cut_q4 > 8) {
        fea_ellipse_q4(
            canvas, lay.light.x_q4 + lay.smile_tilt_q4,
            lay.light.y_q4 + lay.half_h_q4 + lay.half_h_q4 / 2 -
                lay.smile_cut_q4,
            lay.half_w_q4 + 24, (lay.half_h_q4 * 3) / 4, MS_GLASS,
            32U);
    } else if (lay.smile_cut_q4 < -8) {
        fea_ellipse_q4(
            canvas, lay.light.x_q4 + lay.smile_tilt_q4,
            lay.light.y_q4 - lay.half_h_q4 - lay.half_h_q4 / 2 -
                lay.smile_cut_q4 / 2,
            lay.half_w_q4 + 24, lay.half_h_q4, MS_GLASS, 30U);
    }

    /* scan highlight on the glass */
    fea_ring_q4(
        canvas, lay.lens.x_q4 + (7 << 4), lay.lens.y_q4 + (7 << 4),
        lay.lens_r_q4 - 40, 22, MS_IRIS_HOT, 5U);

    /* shutter lids close over everything on the glass */
    const int32_t cover =
        ((2 * lay.lens_r_q4) * (256 - lay.open_q8)) >> 8;
    const int32_t top_edge =
        lay.lens.y_q4 - lay.lens_r_q4 + (cover * 5) / 8;
    const int32_t bot_edge = lay.lens.y_q4 + lay.lens_r_q4 -
        (cover * 3) / 8 - lay.lower_lid_q4;
    if (cover > 8 || lay.lower_lid_q4 > 8) {
        for (int32_t y = (lay.lens.y_q4 - lay.lens_r_q4 - 16) >> 4;
             y <= (lay.lens.y_q4 + lay.lens_r_q4 + 31) >> 4; ++y) {
            const int32_t yc = y * 16 + 8;
            const int32_t tilt_here = lay.lid_tilt_q4;
            const int32_t local_top =
                top_edge + (yc > lay.lens.y_q4 ? 0 : tilt_here);
            if (yc >= local_top && yc <= bot_edge) {
                continue;
            }
            const int64_t dy = yc - lay.lens.y_q4;
            const int64_t remain =
                (int64_t)lay.lens_r_q4 * lay.lens_r_q4 - dy * dy;
            if (remain <= 0) {
                continue;
            }
            const int32_t half = fea_isqrt64(remain);
            fea_hspan_q4(
                canvas, y, lay.lens.x_q4 - half, lay.lens.x_q4 + half,
                MS_LID, 32U);
        }
        /* edge lights follow the lid lines */
        fea_stroke_q4(
            canvas, lay.lens.x_q4 - lay.lens_r_q4 + 40,
            top_edge - lay.lid_tilt_q4,
            lay.lens.x_q4 + lay.lens_r_q4 - 40,
            top_edge + lay.lid_tilt_q4, 20, MS_LID_LIGHT, 24U);
        if (lay.lower_lid_q4 > 8 || lay.open_q8 < 40) {
            fea_stroke_q4(
                canvas, lay.lens.x_q4 - lay.lens_r_q4 + 48, bot_edge,
                lay.lens.x_q4 + lay.lens_r_q4 - 48, bot_edge, 16,
                MS_LID_LIGHT, 18U);
        }
    }

    /* brow bar with soft under-glow */
    fea_stroke_q4(
        canvas, lay.brow_l.x_q4, lay.brow_l.y_q4, lay.brow_r.x_q4,
        lay.brow_r.y_q4, 64, MS_BROW, 32U);
    fea_stroke_q4(
        canvas, lay.brow_l.x_q4, lay.brow_l.y_q4 + 40, lay.brow_r.x_q4,
        lay.brow_r.y_q4 + 40, 20, MS_BROW, 8U);

    /* blush arcs on the lower lens */
    if (lay.blush_alpha > 2) {
        for (int side = 0; side < 2; ++side) {
            const int32_t sign = side == 0 ? -1 : 1;
            fea_ellipse_q4(
                canvas, lay.lens.x_q4 + sign * (24 << 4),
                lay.lens.y_q4 + (22 << 4), 6 << 4, 3 << 4, MS_BLUSH,
                (uint32_t)lay.blush_alpha);
        }
    }
}

void fea_scope_probe(
    const fea_pose_t *pose, uint32_t clock, fea_probe_t *probe)
{
    ms_layout_t lay;
    ms_layout(pose, clock, &lay);
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 0U;
    for (int side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)lay.lens.x_q4;
        probe->eye_cy_q4[side] = (int16_t)lay.lens.y_q4;
        probe->eye_open_q8[side] = (int16_t)lay.open_q8;
        probe->pupil_x_q4[side] = (int16_t)lay.pupil.x_q4;
        probe->pupil_y_q4[side] = (int16_t)lay.pupil.y_q4;
        probe->pupil_r_q4[side] = (int16_t)lay.pupil_half_w_q4;
        probe->brow_y_q4[side] = (int16_t)
            (side == 0 ? lay.brow_l.y_q4 : lay.brow_r.y_q4);
        probe->brow_tilt_q8[side] = pose->brow_tilt_q8[side];
    }
    /* eye-only: mouth probe fields describe the light's lower edge */
    probe->mouth_cx_q4 = (int16_t)lay.light.x_q4;
    probe->mouth_cy_q4 =
        (int16_t)(lay.light.y_q4 + lay.half_h_q4);
    probe->corner_x_q4[0] =
        (int16_t)(lay.light.x_q4 - lay.half_w_q4);
    probe->corner_y_q4[0] =
        (int16_t)(lay.light.y_q4 + lay.half_h_q4);
    probe->corner_x_q4[1] =
        (int16_t)(lay.light.x_q4 + lay.half_w_q4);
    probe->corner_y_q4[1] =
        (int16_t)(lay.light.y_q4 + lay.half_h_q4);
    probe->jaw_q4 = (int16_t)(2 * lay.half_h_q4);
    probe->extent_left_q4 = 14 << 4;
    probe->extent_top_q4 = 9 << 4;
    probe->extent_right_q4 = 146 << 4;
    probe->extent_bottom_q4 = 111 << 4;
}
