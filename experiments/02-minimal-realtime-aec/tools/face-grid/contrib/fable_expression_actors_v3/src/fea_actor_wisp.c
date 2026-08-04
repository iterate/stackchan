#include "fea_internal.h"

/*
 * will-o-wisp — emissive night spirit.
 *
 * Signature acting channels: the whole silhouette is an emotion
 * channel — flame crown flares when excited, the body flares wide on
 * surprise, compacts when determined, sags and drips when sleepy, and
 * its edge wavers with concern. Features are dark cuts in the glow;
 * the grin glows through the teeth.
 */

enum {
    WS_CX = 80 << 4,
    WS_CY = 60 << 4,
    WS_TOP = 16,
    WS_BOTTOM = 104,
};

static const uint16_t WS_BG = FEA_RGB(9, 11, 22);
static const uint16_t WS_STAR = FEA_RGB(150, 160, 190);
static const uint16_t WS_HALO = FEA_RGB(48, 110, 96);
static const uint16_t WS_BODY = FEA_RGB(120, 226, 192);
static const uint16_t WS_CORE = FEA_RGB(210, 255, 236);
static const uint16_t WS_FEATURE = FEA_RGB(12, 42, 40);
static const uint16_t WS_TEETH_GLOW = FEA_RGB(236, 255, 244);
static const uint16_t WS_TONGUE = FEA_RGB(70, 150, 128);
static const uint16_t WS_BLUSH = FEA_RGB(214, 150, 150);

/* teardrop profile half-widths in px at 12 stations top->bottom */
static const uint8_t WS_PROFILE[12] = {
    3, 7, 13, 21, 29, 35, 38, 37, 32, 24, 15, 6,
};

/* per-emotion silhouette: width Q8, crown extra px, sag px, wobble */
typedef struct {
    int16_t width_q8;
    int16_t crown;
    int16_t sag;
    int16_t wobble;
    int16_t flicker_q8;      /* crown flicker speed scale */
} ws_shape_t;

static const ws_shape_t WS_SHAPES[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = { 256, 0, 0, 0, 256 },
    [FACE_EXPRESSION_WARM] = { 264, 2, 0, 0, 200 },
    [FACE_EXPRESSION_JOY] = { 272, 5, 0, 0, 300 },
    [FACE_EXPRESSION_CONCERN] = { 236, -2, 1, 3, 340 },
    [FACE_EXPRESSION_SURPRISE] = { 306, 7, 0, 0, 190 },
    [FACE_EXPRESSION_THOUGHTFUL] = { 244, 3, 0, 0, 150 },
    [FACE_EXPRESSION_SKEPTICAL] = { 246, -3, 0, 0, 210 },
    [FACE_EXPRESSION_DETERMINED] = { 220, -4, 0, 0, 170 },
    [FACE_EXPRESSION_SLEEPY] = { 250, -7, 6, 0, 90 },
    [FACE_EXPRESSION_EXCITED] = { 268, 10, 0, 0, 420 },
    [FACE_EXPRESSION_EMBARRASSED] = { 232, -3, 2, 2, 260 },
};

typedef struct {
    int32_t width_q8;
    int32_t crown_q4;
    int32_t sag_q4;
    int32_t wobble;
    int32_t flicker_q8;
    int32_t speech_asym_q4;
    fea_pt_t eye[2];
    int32_t eye_rx_q4;
    int32_t eye_ry_q4[2];
    int32_t open_q8[2];
    fea_pt_t brow_in[2], brow_out[2];
    fea_lipmouth_t mouth;
    int32_t blush_alpha;
    int32_t body_alpha;
} ws_layout_t;

static void ws_layout(
    const fea_pose_t *pose, uint32_t clock, ws_layout_t *lay)
{
    (void)clock;
    const int32_t act = pose->act_q8;
    const ws_shape_t *shape = &WS_SHAPES[pose->emotion];

    lay->width_q8 = 256 + (((shape->width_q8 - 256) * act) >> 8);
    lay->crown_q4 = (shape->crown * 16 * act) >> 8;
    lay->sag_q4 = (shape->sag * 16 * act) >> 8;
    lay->wobble = (shape->wobble * act) >> 8;
    lay->flicker_q8 = 256 + (((shape->flicker_q8 - 256) * act) >> 8);
    lay->body_alpha = 32;

    /* asymmetric speech acting: brow/lid peaks alternate sides in
     * time with the audio envelope, so speech is facial, not just
     * silhouette */
    int32_t speech_asym_q4 = 0;
    if (pose->speech_phase == FACE_SPEECH_ACTIVE &&
        pose->speaking != 0U) {
        speech_asym_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U /
                                    5300U)) *
             ((pose->audio_q8 * 40) / 255)) >> 14;
    }
    lay->speech_asym_q4 = speech_asym_q4;
    lay->eye_rx_q4 = 7 << 4;
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        lay->eye[side] = fea_place(
            pose, WS_CX, WS_CY, sign * (14 << 4), -(8 << 4));
        lay->open_q8[side] = pose->eye_open_q8[side];
        const int32_t base_ry = 9 << 4;
        int32_t ry = (base_ry * pose->eye_open_q8[side]) >> 8;
        /* lower-lid squeeze arcs the eye */
        ry -= (base_ry * pose->lower_lid_q8[side]) >> 9;
        ry += sign < 0 ? speech_asym_q4 / 3 : -speech_asym_q4 / 3;
        /* >=3px at 40x30: an open eye never thins below 2.5 px */
        if (pose->eye_open_q8[side] > 60 && ry < 40) {
            ry = 40;
        }
        lay->eye_ry_q4[side] = ry < 0 ? 0 : ry;
    }

    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t raise_q4 =
            (pose->brow_raise_q8[side] * 5 * 16) >> 8;
        const int32_t tilt_q4 =
            (pose->brow_tilt_q8[side] * 5 * 16) >> 8;
        const int32_t knit_q4 = (pose->brow_knit_q8 * 4 * 16) >> 8;
        const int32_t asym =
            side == 0 ? lay->speech_asym_q4 : -lay->speech_asym_q4 / 2;
        lay->brow_in[side] = fea_place(
            pose, WS_CX, WS_CY, sign * ((8 << 4) - knit_q4 / 2),
            -(20 << 4) - raise_q4 - tilt_q4 / 2 + knit_q4 / 2 - asym);
        lay->brow_out[side] = fea_place(
            pose, WS_CX, WS_CY, sign * (21 << 4),
            -(22 << 4) - raise_q4 + tilt_q4 / 2 - asym);
    }

    /* glowing grin, parented corners */
    const fea_pt_t mouth_c = fea_place(pose, WS_CX, WS_CY, 0, 12 << 4);
    int32_t half_w_q4 = ((18 << 4) * pose->mouth_w_q8) >> 8;
    half_w_q4 -= (pose->round_q8 * 6 * 16) >> 8;
    half_w_q4 = fea_clamp_i32(half_w_q4, 6 << 4, 21 << 4);
    const int32_t corner_l_q4 = (pose->corner_q8[0] * 8 * 16) >> 8;
    const int32_t corner_r_q4 = (pose->corner_q8[1] * 8 * 16) >> 8;
    lay->mouth.left_x_q4 = (int16_t)(mouth_c.x_q4 - half_w_q4);
    lay->mouth.left_y_q4 = (int16_t)(mouth_c.y_q4 - corner_l_q4);
    lay->mouth.right_x_q4 = (int16_t)(mouth_c.x_q4 + half_w_q4);
    lay->mouth.right_y_q4 = (int16_t)(mouth_c.y_q4 - corner_r_q4);
    const int32_t jaw_q4 =
        ((pose->jaw_q8 * 16 * 16) >> 8) + ((pose->round_q8 * 72) >> 8);
    const int32_t curve_q4 = (pose->curve_q8 * 6 * 16) >> 8;
    lay->mouth.top_ctrl_y_q4 =
        (int16_t)(mouth_c.y_q4 - curve_q4 - jaw_q4 / 3);
    {
        int32_t bot = mouth_c.y_q4 - curve_q4 / 3 + jaw_q4;
        if (bot > (112 << 4)) {
            bot = 112 << 4;   /* mouth may never clip */
        }
        lay->mouth.bot_ctrl_y_q4 = (int16_t)bot;
    }
    lay->mouth.lip_q4 =
        (int16_t)(32 + ((pose->press_q8 * 22) >> 8));
    lay->mouth.lip_color = WS_FEATURE;
    lay->mouth.fill_color = WS_FEATURE;
    lay->mouth.teeth_color = WS_TEETH_GLOW;
    lay->mouth.tongue_color = WS_TONGUE;
    lay->mouth.teeth_q8 = pose->teeth_q8;
    lay->mouth.tongue_q8 = pose->tongue_q8;
    lay->mouth.alpha = 32U;

    lay->blush_alpha = (pose->cheek_q8 * 22) >> 8;
}

void fea_wisp_render(
    const fea_pose_t *pose, uint32_t clock, fea_canvas_t *canvas)
{
    ws_layout_t lay;
    ws_layout(pose, clock, &lay);

    fea_fill(canvas, WS_BG);
    /* fixed star field: hashed positions, constant brightness */
    for (uint32_t star = 0; star < 26U; ++star) {
        const uint32_t h = fea_hash2(star, 0x57A125U);
        const int32_t sx = (int32_t)(h % (FEA_FRAME_WIDTH - 16)) + 8;
        const int32_t sy = (int32_t)((h >> 9) % (FEA_FRAME_HEIGHT - 14)) +
            7;
        const uint32_t alpha = 6U + ((h >> 20) & 7U);
        uint16_t *p = canvas->pixels + sy * FEA_FRAME_WIDTH + sx;
        *p = fea_blend565(*p, WS_STAR, alpha);
    }

    /* emissive body: halo, body, hot core; silhouette per row */
    const int32_t top_q4 = ((WS_TOP << 4) - lay.crown_q4) + pose->oy_q4;
    const int32_t bottom_q4 = (WS_BOTTOM << 4) + lay.sag_q4 +
        pose->oy_q4;
    const int32_t span_q4 = bottom_q4 - top_q4;
    const int32_t breath_sway =
        (pose->breath_q8 * 12) >> 8;
    for (int32_t y = fea_clamp_i32(top_q4 >> 4, 0, FEA_FRAME_HEIGHT);
         y < fea_clamp_i32((bottom_q4 >> 4) + 1, 0, FEA_FRAME_HEIGHT);
         ++y) {
        const int32_t yc_q4 = (y << 4) + 8;
        if (yc_q4 < top_q4 || yc_q4 >= bottom_q4 || span_q4 <= 0) {
            continue;
        }
        const int32_t t_q8 = ((yc_q4 - top_q4) << 8) / span_q4;
        /* profile interpolation across 12 stations */
        const int32_t station_q8 = t_q8 * 11;
        const int32_t index = station_q8 >> 8;
        const int32_t frac = station_q8 & 255;
        const int32_t base_a = WS_PROFILE[index];
        const int32_t base_b =
            WS_PROFILE[index < 11 ? index + 1 : 11];
        int32_t half_q4 =
            base_a * 16 + (((base_b - base_a) * 16) * frac >> 8);
        half_q4 = (half_q4 * lay.width_q8) >> 8;
        half_q4 = (half_q4 * pose->scale_x_q8) >> 8;
        /* crown flicker on the upper quarter: two incommensurate sines
         * keyed to row so tongues of flame lick upward */
        if (t_q8 < 72) {
            const uint32_t speed =
                (uint32_t)(((uint64_t)clock * (uint32_t)lay.flicker_q8) >>
                           8);
            const int32_t f1 = fea_sin_q14(
                (uint32_t)(speed * 65536U / 21000U) + (uint32_t)(y * 900));
            const int32_t f2 = fea_sin_q14(
                (uint32_t)(speed * 65536U / 12600U) +
                (uint32_t)(y * 1500) + 21000U);
            const int32_t flick =
                ((f1 + f2 / 2) * (72 - t_q8)) / 72;
            half_q4 += (flick * 40) >> 14;
        }
        /* concern wavers the whole edge */
        if (lay.wobble > 0) {
            half_q4 += (fea_sin_q14(
                            (uint32_t)((uint64_t)clock * 65536U / 9000U) +
                            (uint32_t)(y * 2200)) *
                        lay.wobble * 10) >> 14;
        }
        if (half_q4 < 8) {
            half_q4 = 8;
        }
        /* face band: eyes/brows/mouth always sit on glow */
        {
            const int32_t band_top = WS_CY + pose->oy_q4 - (17 << 4);
            const int32_t band_bottom =
                WS_CY + pose->oy_q4 + (20 << 4);
            if (yc_q4 >= band_top && yc_q4 <= band_bottom &&
                half_q4 < (26 << 4)) {
                half_q4 = 26 << 4;
            }
        }
        /* hard clamp inside the safe area */
        const int32_t max_half =
            ((FEA_SAFE_RIGHT - 4) << 4) - WS_CX > 0
                ? ((FEA_SAFE_RIGHT - 4) << 4) - WS_CX : 0;
        if (half_q4 > max_half) {
            half_q4 = max_half;
        }
        const int32_t cx_row = WS_CX + pose->ox_q4 + breath_sway +
            (((yc_q4 - WS_CY) * pose->shear_q12) >> 12);
        fea_hspan_q4(
            canvas, y, cx_row - half_q4 - (7 << 4),
            cx_row + half_q4 + (7 << 4), WS_HALO, 7U);
        fea_hspan_q4(
            canvas, y, cx_row - half_q4, cx_row + half_q4, WS_BODY,
            (uint32_t)lay.body_alpha);
        const int32_t core_half = (half_q4 * 150) >> 8;
        fea_hspan_q4(
            canvas, y, cx_row - core_half, cx_row + core_half, WS_CORE,
            13U);
    }

    /* sleepy drips: two beads sliding below the body */
    if (pose->emotion == FACE_EXPRESSION_SLEEPY && pose->act_q8 > 64) {
        for (int drip = 0; drip < 2; ++drip) {
            const uint32_t period = drip == 0 ? 76000U : 99000U;
            const uint32_t phase =
                (uint32_t)(((uint64_t)(clock % period) << 8) / period);
            const int32_t fall = (int32_t)((phase * 6U) >> 8);
            const int32_t dx = drip == 0 ? -(11 << 4) : (13 << 4);
            const uint32_t fade =
                phase < 200U ? 20U : (20U * (256U - phase)) / 56U;
            int32_t drip_y = bottom_q4 + ((2 + fall) << 4);
            if (drip_y > (116 << 4)) {
                drip_y = 116 << 4;      /* drips never clip */
            }
            fea_ellipse_q4(
                canvas, WS_CX + pose->ox_q4 + dx, drip_y, 28, 40,
                WS_BODY, (fade * (uint32_t)pose->act_q8) >> 8);
        }
    }
    /* excited motes rising beside the body */
    if (pose->emotion == FACE_EXPRESSION_EXCITED && pose->act_q8 > 64) {
        for (int mote = 0; mote < 3; ++mote) {
            const uint32_t period = 52000U + (uint32_t)mote * 17000U;
            const uint32_t phase =
                (uint32_t)(((uint64_t)(clock % period) << 8) / period);
            const uint32_t h = fea_hash2((uint32_t)mote, 0x307E5U);
            const int32_t mx = WS_CX + pose->ox_q4 +
                ((mote & 1) != 0 ? 1 : -1) *
                    ((44 << 4) + (int32_t)((h >> 4) & 63U));
            const int32_t my = (96 << 4) - (int32_t)(phase * 300U >> 8);
            const uint32_t fade = phase < 128U
                ? (16U * phase) >> 7
                : (16U * (256U - phase)) >> 7;
            fea_glow_q4(
                canvas, mx, my, 12, 40, WS_CORE,
                (fade * (uint32_t)pose->act_q8) >> 8);
        }
    }

    if (lay.blush_alpha > 2) {
        for (int side = 0; side < 2; ++side) {
            const int32_t sign = side == 0 ? -1 : 1;
            const fea_pt_t pad = fea_place(
                pose, WS_CX, WS_CY, sign * (24 << 4), 4 << 4);
            fea_ellipse_q4(
                canvas, pad.x_q4, pad.y_q4, 7 << 4, 4 << 4, WS_BLUSH,
                (uint32_t)lay.blush_alpha);
        }
    }

    /* dark features cut into the glow */
    for (int side = 0; side < 2; ++side) {
        if (lay.eye_ry_q4[side] > 16) {
            fea_ellipse_q4(
                canvas, lay.eye[side].x_q4, lay.eye[side].y_q4,
                lay.eye_rx_q4, lay.eye_ry_q4[side], WS_FEATURE, 30U);
            /* bright pupil spark inside the dark eye */
            fea_ellipse_q4(
                canvas,
                lay.eye[side].x_q4 + ((pose->gaze_x_q8 * 40) >> 8) +
                    lay.speech_asym_q4 / 2,
                lay.eye[side].y_q4 + ((pose->gaze_y_q8 * 30) >> 8),
                22, 24, WS_TEETH_GLOW, 28U);
        } else {
            /* heavy-lidded: two thick angled seams keep >=2px mass */
            fea_stroke_q4(
                canvas, lay.eye[side].x_q4 - lay.eye_rx_q4 + 8,
                lay.eye[side].y_q4 + 12, lay.eye[side].x_q4,
                lay.eye[side].y_q4 - 6, 44, WS_FEATURE, 32U);
            fea_stroke_q4(
                canvas, lay.eye[side].x_q4,
                lay.eye[side].y_q4 - 6,
                lay.eye[side].x_q4 + lay.eye_rx_q4 - 8,
                lay.eye[side].y_q4 + 12, 44, WS_FEATURE, 32U);
        }
    }
    for (int side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, lay.brow_in[side].x_q4, lay.brow_in[side].y_q4,
            lay.brow_out[side].x_q4, lay.brow_out[side].y_q4, 34,
            WS_FEATURE, 26U);
    }
    fea_lipmouth_draw(canvas, &lay.mouth);
}

void fea_wisp_probe(
    const fea_pose_t *pose, uint32_t clock, fea_probe_t *probe)
{
    ws_layout_t lay;
    ws_layout(pose, clock, &lay);
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 1U;
    for (int side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)lay.eye[side].x_q4;
        probe->eye_cy_q4[side] = (int16_t)lay.eye[side].y_q4;
        probe->eye_open_q8[side] = (int16_t)lay.open_q8[side];
        probe->pupil_x_q4[side] = (int16_t)
            (lay.eye[side].x_q4 + ((pose->gaze_x_q8 * 40) >> 8) +
             lay.speech_asym_q4 / 2);
        probe->pupil_y_q4[side] = (int16_t)
            (lay.eye[side].y_q4 + ((pose->gaze_y_q8 * 30) >> 8));
        probe->pupil_r_q4[side] = 20;
        probe->brow_y_q4[side] = (int16_t)
            ((lay.brow_in[side].y_q4 + lay.brow_out[side].y_q4) / 2);
        probe->brow_tilt_q8[side] = pose->brow_tilt_q8[side];
    }
    probe->mouth_cx_q4 = (int16_t)
        ((lay.mouth.left_x_q4 + lay.mouth.right_x_q4) / 2);
    probe->mouth_cy_q4 = (int16_t)
        ((lay.mouth.left_y_q4 + lay.mouth.right_y_q4) / 2);
    probe->corner_x_q4[0] = lay.mouth.left_x_q4;
    probe->corner_y_q4[0] = lay.mouth.left_y_q4;
    probe->corner_x_q4[1] = lay.mouth.right_x_q4;
    probe->corner_y_q4[1] = lay.mouth.right_y_q4;
    probe->jaw_q4 = (int16_t)
        (lay.mouth.bot_ctrl_y_q4 - lay.mouth.top_ctrl_y_q4);
    const int32_t max_half =
        ((38 << 4) * lay.width_q8 >> 8) + (7 << 4) + 60;
    probe->extent_left_q4 = (int16_t)(WS_CX + pose->ox_q4 - max_half);
    probe->extent_right_q4 = (int16_t)(WS_CX + pose->ox_q4 + max_half);
    probe->extent_top_q4 = (int16_t)
        (((WS_TOP << 4) - lay.crown_q4) + pose->oy_q4 - 40);
    {
        int32_t bottom =
            (WS_BOTTOM << 4) + lay.sag_q4 + pose->oy_q4;
        if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
            bottom += 11 << 4;          /* drip allowance */
            if (bottom > (119 << 4)) {
                bottom = 119 << 4;
            }
        }
        probe->extent_bottom_q4 = (int16_t)bottom;
    }
}
