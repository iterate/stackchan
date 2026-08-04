#include "fea_internal.h"

/*
 * mochi-cat — plush cat mascot.
 *
 * Signature acting channels: articulated ears (perk/droop/flatten/
 * asymmetric tilt per emotion, audio twitch while speaking), whisker
 * droop, bead eyes under fur lids, and a muzzle-parented lip mouth.
 */

enum {
    MC_CX = 80 << 4,
    MC_CY = 62 << 4,
};

static const uint16_t MC_BG = FEA_RGB(148, 178, 158);
static const uint16_t MC_BG_FLOOR = FEA_RGB(126, 156, 138);
static const uint16_t MC_FUR = FEA_RGB(246, 238, 218);
static const uint16_t MC_FUR_SHADOW = FEA_RGB(214, 198, 172);
static const uint16_t MC_EAR_INNER = FEA_RGB(232, 168, 162);
static const uint16_t MC_BEAD = FEA_RGB(56, 40, 36);
static const uint16_t MC_BEAD_WARM = FEA_RGB(128, 82, 50);
static const uint16_t MC_GLINT = FEA_RGB(255, 252, 246);
static const uint16_t MC_MUZZLE = FEA_RGB(252, 246, 232);
static const uint16_t MC_NOSE = FEA_RGB(198, 128, 122);
static const uint16_t MC_LIP = FEA_RGB(112, 74, 64);
static const uint16_t MC_MOUTH_FILL = FEA_RGB(96, 48, 52);
static const uint16_t MC_TEETH = FEA_RGB(250, 248, 240);
static const uint16_t MC_TONGUE = FEA_RGB(214, 120, 118);
static const uint16_t MC_BLUSH = FEA_RGB(240, 152, 148);
static const uint16_t MC_BROW = FEA_RGB(150, 122, 92);
static const uint16_t MC_WHISKER = FEA_RGB(206, 192, 162);

/* Per-emotion ear pose: lift (+perk/-droop), flare (tips outward),
 * asym (+ = viewer-left ear higher). Q8, scaled by the acting curve. */
typedef struct {
    int16_t lift;
    int16_t flare;
    int16_t asym;
} mc_ear_pose_t;

static const mc_ear_pose_t MC_EARS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = { 0, 0, 0 },
    [FACE_EXPRESSION_WARM] = { 60, 0, 0 },
    [FACE_EXPRESSION_JOY] = { 140, 20, 0 },
    [FACE_EXPRESSION_CONCERN] = { -120, 150, 0 },
    [FACE_EXPRESSION_SURPRISE] = { 230, -30, 0 },
    [FACE_EXPRESSION_THOUGHTFUL] = { 40, 0, 120 },
    [FACE_EXPRESSION_SKEPTICAL] = { -20, 40, -160 },
    [FACE_EXPRESSION_DETERMINED] = { 90, -60, 0 },
    [FACE_EXPRESSION_SLEEPY] = { -220, 90, 30 },
    [FACE_EXPRESSION_EXCITED] = { 255, 30, 0 },
    [FACE_EXPRESSION_EMBARRASSED] = { -170, 190, -95 },
};

typedef struct {
    fea_pt_t head_tl, head_br;
    fea_pt_t ear_bi[2], ear_bo[2], ear_tip[2];
    fea_pt_t eye[2];
    int32_t eye_rx_q4, eye_ry_q4;
    int32_t aperture_q8[2];
    int32_t lower_lid_q8[2];
    fea_pt_t brow_in[2], brow_out[2];
    fea_pt_t muzzle;
    fea_pt_t nose;
    fea_lipmouth_t mouth;
    int32_t whisker_droop_q4;
    int32_t blush_alpha;
} mc_layout_t;

static void mc_layout(
    const fea_pose_t *pose, uint32_t clock, mc_layout_t *lay)
{
    const int32_t act = pose->act_q8;
    const mc_ear_pose_t *ear = &MC_EARS[pose->emotion];
    /* embarrassed turns the whole head slightly away */
    fea_pose_t posed = *pose;
    if (pose->emotion == FACE_EXPRESSION_EMBARRASSED) {
        posed.ox_q4 = (int16_t)fea_clamp_i32(
            posed.ox_q4 + ((36 * act) >> 8), -96, 96);
        pose = &posed;
    }

    lay->head_tl = fea_place(pose, MC_CX, MC_CY, -(56 << 4), -(33 << 4));
    lay->head_br = fea_place(pose, MC_CX, MC_CY, 56 << 4, 42 << 4);

    /* ears ride the head top edge; lift/flare/asym act them */
    const int32_t lift = (ear->lift * act) >> 8;
    const int32_t flare = (ear->flare * act) >> 8;
    const int32_t asym = (ear->asym * act) >> 8;
    int32_t twitch_q4 = 0;
    if (pose->speech_phase == FACE_SPEECH_ACTIVE &&
        pose->speaking != 0U) {
        twitch_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U / 3900U)) *
             (pose->audio_q8 >> 3)) >> 14;
    }
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t side_lift = lift + (side == 0 ? asym : -asym);
        lay->ear_bi[side] = fea_place(
            pose, MC_CX, MC_CY, sign * (21 << 4), -(30 << 4));
        lay->ear_bo[side] = fea_place(
            pose, MC_CX, MC_CY, sign * (45 << 4), -(22 << 4));
        const int32_t tip_dx =
            sign * (40 + ((flare * 14) >> 8) - ((side_lift * 8) >> 8));
        const int32_t tip_dy =
            -47 - ((side_lift * 13) >> 8) + ((flare * 4) >> 8);
        fea_pt_t tip = fea_place(
            pose, MC_CX, MC_CY, tip_dx * 16 + sign * twitch_q4,
            tip_dy * 16);
        tip.y_q4 = fea_clamp_i32(
            tip.y_q4, (FEA_SAFE_TOP + 2) << 4,
            (FEA_FRAME_HEIGHT - 30) << 4);
        tip.x_q4 = fea_clamp_i32(
            tip.x_q4, (FEA_SAFE_LEFT + 2) << 4,
            (FEA_SAFE_RIGHT - 2) << 4);
        lay->ear_tip[side] = tip;
    }

    /* bead eyes; the bead itself shifts with gaze inside its socket */
    lay->eye_rx_q4 = 10 << 4;
    lay->eye_ry_q4 = 11 << 4;
    const int32_t gx = (pose->gaze_x_q8 * 4 * 16) >> 8;
    const int32_t gy = (pose->gaze_y_q8 * 3 * 16) >> 8;
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        lay->eye[side] = fea_place(
            pose, MC_CX, MC_CY, sign * (23 << 4) + gx, -(6 << 4) + gy);
        lay->aperture_q8[side] = pose->eye_open_q8[side];
        lay->lower_lid_q8[side] = pose->lower_lid_q8[side];
    }

    /* fur-tuft brows: inner ends pulled together and down by knit */
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t raise_q4 =
            (pose->brow_raise_q8[side] * 5 * 16) >> 8;
        const int32_t tilt_q4 =
            (pose->brow_tilt_q8[side] * 4 * 16) >> 8;
        const int32_t knit_q4 = (pose->brow_knit_q8 * 4 * 16) >> 8;
        lay->brow_in[side] = fea_place(
            pose, MC_CX, MC_CY, sign * ((14 << 4) - knit_q4 / 2),
            -(22 << 4) - raise_q4 - tilt_q4 / 2 + knit_q4 / 2);
        lay->brow_out[side] = fea_place(
            pose, MC_CX, MC_CY, sign * (31 << 4),
            -(25 << 4) - raise_q4 + tilt_q4 / 2);
    }

    lay->muzzle = fea_place(pose, MC_CX, MC_CY, 0, 22 << 4);
    lay->nose = fea_place(pose, MC_CX, MC_CY, 0, 13 << 4);

    /* muzzle-parented mouth: the two corners are the parents */
    const fea_pt_t mouth_c = fea_place(pose, MC_CX, MC_CY, 0, 27 << 4);
    int32_t half_w_q4 = ((18 << 4) * pose->mouth_w_q8) >> 8;
    half_w_q4 -= (pose->round_q8 * 6 * 16) >> 8;
    half_w_q4 = fea_clamp_i32(half_w_q4, 6 << 4, 21 << 4);
    const int32_t corner_l_q4 = (pose->corner_q8[0] * 9 * 16) >> 8;
    const int32_t corner_r_q4 = (pose->corner_q8[1] * 9 * 16) >> 8;
    const int32_t jaw_q4 =
        ((pose->jaw_q8 * 17 * 16) >> 8) + ((pose->round_q8 * 64) >> 8);
    const int32_t curve_q4 = (pose->curve_q8 * 7 * 16) >> 8;
    lay->mouth.left_x_q4 = (int16_t)(mouth_c.x_q4 - half_w_q4);
    lay->mouth.left_y_q4 = (int16_t)(mouth_c.y_q4 - corner_l_q4);
    lay->mouth.right_x_q4 = (int16_t)(mouth_c.x_q4 + half_w_q4);
    lay->mouth.right_y_q4 = (int16_t)(mouth_c.y_q4 - corner_r_q4);
    lay->mouth.top_ctrl_y_q4 =
        (int16_t)(mouth_c.y_q4 - curve_q4 - jaw_q4 / 3);
    {
        int32_t bot = mouth_c.y_q4 - curve_q4 / 3 + jaw_q4;
        if (bot > (115 << 4)) {
            bot = 115 << 4;   /* mouth may never clip */
        }
        lay->mouth.bot_ctrl_y_q4 = (int16_t)bot;
    }
    lay->mouth.lip_q4 =
        (int16_t)(20 + ((pose->press_q8 * 20) >> 8));
    lay->mouth.lip_color = MC_LIP;
    lay->mouth.fill_color = MC_MOUTH_FILL;
    lay->mouth.teeth_color = MC_TEETH;
    lay->mouth.tongue_color = MC_TONGUE;
    lay->mouth.teeth_q8 = pose->teeth_q8;
    lay->mouth.tongue_q8 = pose->tongue_q8;
    lay->mouth.alpha = 32U;

    lay->whisker_droop_q4 =
        ((-lift * 40) >> 8) + ((pose->breath_q8 * 3) >> 8);
    lay->blush_alpha = (pose->cheek_q8 * 24) >> 8;
}

static void mc_draw_eye(
    fea_canvas_t *canvas, const mc_layout_t *lay,
    const fea_pose_t *pose, int side)
{
    const fea_pt_t c = lay->eye[side];
    const int32_t rx = lay->eye_rx_q4;
    const int32_t ry = lay->eye_ry_q4;
    const int32_t open = lay->aperture_q8[side];
    if (lay->lower_lid_q8[side] > 140 && open > 40) {
        /* happy arc: three chained strokes forming an n-shape */
        const int32_t w = rx - 16;
        const int32_t lift = ry / 2;
        fea_stroke_q4(
            canvas, c.x_q4 - w, c.y_q4 + lift / 2, c.x_q4 - w / 3,
            c.y_q4 - lift / 2, 42, MC_BEAD, 32U);
        fea_stroke_q4(
            canvas, c.x_q4 - w / 3, c.y_q4 - lift / 2,
            c.x_q4 + w / 3, c.y_q4 - lift / 2, 42, MC_BEAD, 32U);
        fea_stroke_q4(
            canvas, c.x_q4 + w / 3, c.y_q4 - lift / 2, c.x_q4 + w,
            c.y_q4 + lift / 2, 42, MC_BEAD, 32U);
        return;
    }
    if (open <= 12) {
        /* closed: a thick fur seam that survives 40x30 */
        fea_stroke_q4(
            canvas, c.x_q4 - rx + 12, c.y_q4 + 8, c.x_q4,
            c.y_q4 - 8, 52, MC_FUR_SHADOW, 32U);
        fea_stroke_q4(
            canvas, c.x_q4, c.y_q4 - 8, c.x_q4 + rx - 12,
            c.y_q4 + 8, 52, MC_FUR_SHADOW, 32U);
        return;
    }
    /* bead */
    fea_ellipse_q4(canvas, c.x_q4, c.y_q4, rx, ry, MC_BEAD, 32U);
    /* warm caramel crescent at the bottom of the bead */
    fea_ellipse_q4(
        canvas, c.x_q4, c.y_q4 + (ry * 2) / 5, (rx * 3) / 4, ry / 3,
        MC_BEAD_WARM, 22U);
    /* pupil-side glints; sparkle grows them into a star */
    const int32_t glint_x = c.x_q4 + rx / 3 +
        ((pose->gaze_x_q8 * 16) >> 8);
    const int32_t glint_y = c.y_q4 - ry / 3 +
        ((pose->gaze_y_q8 * 12) >> 8);
    fea_ellipse_q4(canvas, glint_x, glint_y, 40, 44, MC_GLINT, 30U);
    fea_ellipse_q4(
        canvas, c.x_q4 - rx / 3, c.y_q4 + ry / 4, 22, 24, MC_GLINT,
        18U);
    if (pose->sparkle > 140U) {
        const int32_t ray = 40 + ((pose->sparkle - 140) * 48) / 115;
        fea_stroke_q4(
            canvas, glint_x - ray, glint_y, glint_x + ray, glint_y,
            18, MC_GLINT, 24U);
        fea_stroke_q4(
            canvas, glint_x, glint_y - ray, glint_x, glint_y + ray,
            18, MC_GLINT, 24U);
    }
    /* fur lids cut the bead from above and below */
    const int32_t covered_top =
        (2 * ry * (256 - open)) >> 8;
    if (covered_top > 0) {
        const int32_t lid_edge = c.y_q4 - ry + covered_top;
        const int32_t tilt =
            (pose->lid_tilt_q8[side] * 40) >> 8;
        /* filled fur block above the lid edge */
        for (int32_t y = (c.y_q4 - ry) >> 4;
             y <= (lid_edge + 63) >> 4; ++y) {
            if (y * 16 + 8 < lid_edge) {
                fea_hspan_q4(
                    canvas, y, c.x_q4 - rx - 8, c.x_q4 + rx + 8,
                    MC_FUR, 32U);
            }
        }
        /* lash seam, tilted: + tilt raises the inner end (oblique) */
        const int32_t inner = side == 0 ? c.x_q4 + rx : c.x_q4 - rx;
        const int32_t outer = side == 0 ? c.x_q4 - rx : c.x_q4 + rx;
        fea_stroke_q4(
            canvas, inner, lid_edge - tilt, outer, lid_edge + tilt,
            30, MC_FUR_SHADOW, 26U);
    }
    const int32_t lower = lay->lower_lid_q8[side];
    if (lower > 12) {
        const int32_t cut = (2 * ry * lower) >> 9;
        const int32_t lid_edge = c.y_q4 + ry - cut;
        for (int32_t y = lid_edge >> 4;
             y <= (c.y_q4 + ry + 15) >> 4; ++y) {
            const int32_t yc = y * 16 + 8;
            if (yc >= lid_edge) {
                fea_hspan_q4(
                    canvas, y, c.x_q4 - rx - 8, c.x_q4 + rx + 8,
                    MC_FUR, 32U);
            }
        }
        fea_stroke_q4(
            canvas, c.x_q4 - rx + 8, lid_edge, c.x_q4 + rx - 8,
            lid_edge, 26, MC_FUR_SHADOW, 22U);
    }
}

void fea_mochi_render(
    const fea_pose_t *pose, uint32_t clock, fea_canvas_t *canvas)
{
    mc_layout_t lay;
    mc_layout(pose, clock, &lay);

    fea_fill(canvas, MC_BG);
    fea_fill_rect(canvas, 0, 96, FEA_FRAME_WIDTH, FEA_FRAME_HEIGHT,
                  MC_BG_FLOOR, 32U);

    /* ears behind the head */
    for (int side = 0; side < 2; ++side) {
        fea_triangle_q4(
            canvas, lay.ear_bi[side].x_q4, lay.ear_bi[side].y_q4,
            lay.ear_bo[side].x_q4, lay.ear_bo[side].y_q4,
            lay.ear_tip[side].x_q4, lay.ear_tip[side].y_q4, MC_FUR,
            32U);
        /* inner ear: shrink toward the tip */
        const int32_t ix0 =
            (lay.ear_bi[side].x_q4 * 3 + lay.ear_tip[side].x_q4) / 4;
        const int32_t iy0 =
            (lay.ear_bi[side].y_q4 * 3 + lay.ear_tip[side].y_q4) / 4;
        const int32_t ix1 =
            (lay.ear_bo[side].x_q4 * 3 + lay.ear_tip[side].x_q4) / 4;
        const int32_t iy1 =
            (lay.ear_bo[side].y_q4 * 3 + lay.ear_tip[side].y_q4) / 4;
        fea_triangle_q4(
            canvas, ix0, iy0, ix1, iy1, lay.ear_tip[side].x_q4,
            lay.ear_tip[side].y_q4, MC_EAR_INNER, 26U);
    }

    /* head with soft under-shadow */
    fea_roundrect_q4(
        canvas, lay.head_tl.x_q4 + 24, lay.head_tl.y_q4 + 40,
        lay.head_br.x_q4 + 24, lay.head_br.y_q4 + 40, 33 << 4,
        MC_BG_FLOOR, 14U);
    fea_roundrect_q4(
        canvas, lay.head_tl.x_q4, lay.head_tl.y_q4, lay.head_br.x_q4,
        lay.head_br.y_q4, 33 << 4, MC_FUR, 32U);

    /* muzzle, then mouth (parented), then nose over the top lip */
    fea_ellipse_q4(
        canvas, lay.muzzle.x_q4, lay.muzzle.y_q4, 24 << 4, 16 << 4,
        MC_MUZZLE, 30U);
    fea_lipmouth_draw(canvas, &lay.mouth);
    fea_triangle_q4(
        canvas, lay.nose.x_q4 - 64, lay.nose.y_q4 - 40,
        lay.nose.x_q4 + 64, lay.nose.y_q4 - 40, lay.nose.x_q4,
        lay.nose.y_q4 + 56, MC_NOSE, 32U);

    /* whiskers: two solid strokes per side, drooping with mood */
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        for (int w = 0; w < 2; ++w) {
            const int32_t base_x =
                lay.muzzle.x_q4 + sign * (25 << 4);
            const int32_t base_y =
                lay.muzzle.y_q4 + (w * (6 << 4)) - (3 << 4);
            const int32_t tip_x = base_x + sign * (18 << 4);
            const int32_t tip_y = base_y + (w * (4 << 4)) -
                (2 << 4) + lay.whisker_droop_q4;
            fea_stroke_q4(
                canvas, base_x, base_y, tip_x, tip_y, 26, MC_WHISKER,
                32U);
        }
    }

    /* blush pads */
    if (lay.blush_alpha > 2) {
        for (int side = 0; side < 2; ++side) {
            const int32_t sign = side == 0 ? -1 : 1;
            const fea_pt_t pad = fea_place(
                pose, MC_CX, MC_CY, sign * (38 << 4), 14 << 4);
            fea_ellipse_q4(
                canvas, pad.x_q4, pad.y_q4, 9 << 4, 5 << 4, MC_BLUSH,
                (uint32_t)lay.blush_alpha);
        }
    }

    /* brows over everything facial */
    for (int side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, lay.brow_in[side].x_q4, lay.brow_in[side].y_q4,
            lay.brow_out[side].x_q4, lay.brow_out[side].y_q4, 46,
            MC_BROW, 32U);
    }

    /* eyes last so lids sit over the fur */
    for (int side = 0; side < 2; ++side) {
        mc_draw_eye(canvas, &lay, pose, side);
    }
}

void fea_mochi_probe(
    const fea_pose_t *pose, uint32_t clock, fea_probe_t *probe)
{
    mc_layout_t lay;
    mc_layout(pose, clock, &lay);
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 1U;
    for (int side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)lay.eye[side].x_q4;
        probe->eye_cy_q4[side] = (int16_t)lay.eye[side].y_q4;
        probe->eye_open_q8[side] = (int16_t)lay.aperture_q8[side];
        probe->pupil_x_q4[side] = (int16_t)lay.eye[side].x_q4;
        probe->pupil_y_q4[side] = (int16_t)lay.eye[side].y_q4;
        probe->pupil_r_q4[side] = (int16_t)lay.eye_rx_q4;
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
    /* extent: head + ears + shadow */
    int32_t left = lay.head_tl.x_q4;
    int32_t right = lay.head_br.x_q4 + 24;
    int32_t top = lay.head_tl.y_q4;
    int32_t bottom = lay.head_br.y_q4 + 40;
    if (lay.mouth.bot_ctrl_y_q4 + (7 << 4) > bottom) {
        bottom = lay.mouth.bot_ctrl_y_q4 + (7 << 4);
    }
    for (int side = 0; side < 2; ++side) {
        if (lay.ear_tip[side].x_q4 < left) {
            left = lay.ear_tip[side].x_q4;
        }
        if (lay.ear_tip[side].x_q4 > right) {
            right = lay.ear_tip[side].x_q4;
        }
        if (lay.ear_tip[side].y_q4 < top) {
            top = lay.ear_tip[side].y_q4;
        }
    }
    probe->extent_left_q4 = (int16_t)left;
    probe->extent_top_q4 = (int16_t)top;
    probe->extent_right_q4 = (int16_t)right;
    probe->extent_bottom_q4 = (int16_t)bottom;
}
