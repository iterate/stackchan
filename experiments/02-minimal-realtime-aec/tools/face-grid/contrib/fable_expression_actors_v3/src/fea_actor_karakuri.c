#include "fea_internal.h"

/*
 * karakuri-brass — segmented mechanical puppet mask.
 *
 * Signature acting channels: sliding plates (forehead visor, cheeks,
 * chin), mechanical shutter irises with amber sensor pupils, copper
 * brow slats, a hinged jaw slot with corner flaps, and cheek indicator
 * lamps. Blinks are shutter snaps, not lid falls.
 */

enum {
    KB_CX = 80 << 4,
    KB_CY = 60 << 4,
};

static const uint16_t KB_BG = FEA_RGB(22, 32, 38);
static const uint16_t KB_BG_EDGE = FEA_RGB(14, 22, 27);
static const uint16_t KB_BRASS = FEA_RGB(176, 140, 88);
static const uint16_t KB_BRASS_LIGHT = FEA_RGB(196, 158, 102);
static const uint16_t KB_BRASS_DARK = FEA_RGB(148, 114, 68);
static const uint16_t KB_SEAM = FEA_RGB(58, 44, 30);
static const uint16_t KB_RIVET = FEA_RGB(228, 198, 140);
static const uint16_t KB_STEEL = FEA_RGB(122, 120, 118);
static const uint16_t KB_LENS = FEA_RGB(20, 22, 26);
static const uint16_t KB_SENSOR = FEA_RGB(255, 190, 70);
static const uint16_t KB_SENSOR_HOT = FEA_RGB(255, 236, 170);
static const uint16_t KB_COPPER = FEA_RGB(190, 108, 60);
static const uint16_t KB_SLOT = FEA_RGB(26, 20, 16);
static const uint16_t KB_GRILL = FEA_RGB(168, 162, 150);
static const uint16_t KB_TONGUE_GLOW = FEA_RGB(232, 150, 66);
static const uint16_t KB_LAMP = FEA_RGB(255, 158, 92);

typedef struct {
    fea_pt_t forehead_tl, forehead_br;
    int32_t visor_drop_q4;         /* determined lowers the visor */
    fea_pt_t cheek_tl[2], cheek_br[2];
    fea_pt_t chin_tl, chin_br;
    fea_pt_t eye[2];
    int32_t lens_r_q4;
    int32_t sensor_r_q4[2];
    int32_t shutter_angle[2];      /* leaf rotation with aperture */
    fea_pt_t pupil[2];
    fea_pt_t brow_in[2], brow_out[2];
    fea_lipmouth_t mouth;
    int32_t lamp_alpha;
    int32_t sensor_alpha;
    int32_t open_q8[2];
} kb_layout_t;

static void kb_layout(
    const fea_pose_t *pose, uint32_t clock, kb_layout_t *lay)
{
    (void)clock;
    const int32_t act = pose->act_q8;

    /* visor: determined lowers the forehead plate over the eyes;
     * surprise flings it up */
    int32_t visor = 0;
    if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        visor = (5 * 16 * act) >> 8;
    } else if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        visor = -((3 * 16 * act) >> 8);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        visor = (7 * 16 * act) >> 8;
    }
    lay->visor_drop_q4 = visor;

    const int32_t brow_avg =
        (pose->brow_raise_q8[0] + pose->brow_raise_q8[1]) / 2;
    lay->forehead_tl = fea_place(
        pose, KB_CX, KB_CY, -(46 << 4),
        -(38 << 4) - ((brow_avg * 32) >> 8) + visor);
    lay->forehead_br = fea_place(
        pose, KB_CX, KB_CY, 46 << 4, -(16 << 4) + visor);

    /* cheek plates track the mouth corners a little */
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t lift = (pose->corner_q8[side] * 24) >> 8;
        lay->cheek_tl[side] = fea_place(
            pose, KB_CX, KB_CY,
            side == 0 ? -(46 << 4) : (2 << 4), -(14 << 4) - lift);
        lay->cheek_br[side] = fea_place(
            pose, KB_CX, KB_CY,
            side == 0 ? -(2 << 4) : (46 << 4), (18 << 4) - lift / 2);
        (void)sign;
    }

    /* chin plate drops with the jaw */
    const int32_t jaw_q4 = (pose->jaw_q8 * 14 * 16) >> 8;
    lay->chin_tl = fea_place(pose, KB_CX, KB_CY, -(34 << 4), 20 << 4);
    {
        int32_t drop = jaw_q4 / 2;
        if (drop > (6 << 4)) {
            drop = 6 << 4;      /* hinge travel is mechanical */
        }
        lay->chin_br = fea_place(
            pose, KB_CX, KB_CY, 34 << 4, (42 << 4) + drop);
    }

    /* shutter eyes */
    lay->lens_r_q4 = 14 << 4;
    const int32_t gx = (pose->gaze_x_q8 * 7 * 16) >> 8;
    const int32_t gy = (pose->gaze_y_q8 * 5 * 16) >> 8;
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        lay->eye[side] = fea_place(
            pose, KB_CX, KB_CY, sign * (22 << 4), -(4 << 4));
        const int32_t open = pose->eye_open_q8[side];
        lay->open_q8[side] = open;
        lay->sensor_r_q4[side] =
            (((3 << 4) + ((7 << 4) * open >> 8)) *
             pose->pupil_scale_q8) >> 8;
        if (lay->sensor_r_q4[side] > lay->lens_r_q4 - 56) {
            lay->sensor_r_q4[side] = lay->lens_r_q4 - 56;
        }
        lay->shutter_angle[side] = (256 - open) * 96;
        const int32_t max_shift = lay->lens_r_q4 - 24 -
            lay->sensor_r_q4[side];
        lay->pupil[side].x_q4 = lay->eye[side].x_q4 +
            fea_clamp_i32(gx, -max_shift, max_shift);
        lay->pupil[side].y_q4 = lay->eye[side].y_q4 +
            fea_clamp_i32(gy, -max_shift, max_shift);
    }

    /* copper brow slats above the eyes, sliding on the forehead */
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t raise_q4 =
            (pose->brow_raise_q8[side] * 6 * 16) >> 8;
        const int32_t tilt_q4 =
            (pose->brow_tilt_q8[side] * 5 * 16) >> 8;
        const int32_t knit_q4 = (pose->brow_knit_q8 * 5 * 16) >> 8;
        lay->brow_in[side] = fea_place(
            pose, KB_CX, KB_CY, sign * ((10 << 4) - knit_q4 / 2),
            -(24 << 4) - raise_q4 - tilt_q4 / 2 + knit_q4 / 3 + visor);
        lay->brow_out[side] = fea_place(
            pose, KB_CX, KB_CY, sign * (32 << 4),
            -(26 << 4) - raise_q4 + tilt_q4 / 2 + visor);
    }

    /* hinged jaw slot with corner flaps (parents) */
    const fea_pt_t mouth_c = fea_place(pose, KB_CX, KB_CY, 0, 29 << 4);
    int32_t half_w_q4 = ((23 << 4) * pose->mouth_w_q8) >> 8;
    half_w_q4 -= (pose->round_q8 * 7 * 16) >> 8;
    half_w_q4 = fea_clamp_i32(half_w_q4, 8 << 4, 26 << 4);
    const int32_t corner_l_q4 = (pose->corner_q8[0] * 8 * 16) >> 8;
    const int32_t corner_r_q4 = (pose->corner_q8[1] * 8 * 16) >> 8;
    lay->mouth.left_x_q4 = (int16_t)(mouth_c.x_q4 - half_w_q4);
    lay->mouth.left_y_q4 = (int16_t)(mouth_c.y_q4 - corner_l_q4);
    lay->mouth.right_x_q4 = (int16_t)(mouth_c.x_q4 + half_w_q4);
    lay->mouth.right_y_q4 = (int16_t)(mouth_c.y_q4 - corner_r_q4);
    const int32_t gape_q4 =
        ((pose->jaw_q8 * 24 * 16) >> 8) + ((pose->round_q8 * 96) >> 8);
    const int32_t curve_q4 = (pose->curve_q8 * 5 * 16) >> 8;
    lay->mouth.top_ctrl_y_q4 =
        (int16_t)(mouth_c.y_q4 - curve_q4 - gape_q4 / 4);
    {
        int32_t bot = mouth_c.y_q4 - curve_q4 / 2 + gape_q4;
        if (bot > (110 << 4)) {
            bot = 110 << 4;   /* mouth may never clip */
        }
        lay->mouth.bot_ctrl_y_q4 = (int16_t)bot;
    }
    lay->mouth.lip_q4 =
        (int16_t)(26 + ((pose->press_q8 * 22) >> 8));
    lay->mouth.lip_color = KB_BRASS_DARK;
    lay->mouth.fill_color = KB_SLOT;
    lay->mouth.teeth_color = KB_GRILL;
    lay->mouth.tongue_color = KB_TONGUE_GLOW;
    lay->mouth.teeth_q8 = pose->teeth_q8;
    lay->mouth.tongue_q8 = pose->tongue_q8;
    lay->mouth.alpha = 32U;

    lay->lamp_alpha = (pose->cheek_q8 * 26) >> 8;
    lay->sensor_alpha = 24 + ((int32_t)pose->sparkle * 8) / 255;
    if (pose->emotion == FACE_EXPRESSION_THOUGHTFUL ||
        pose->emotion == FACE_EXPRESSION_SLEEPY) {
        lay->sensor_alpha -= (6 * act) >> 8;
    }
}

static void kb_plate(
    fea_canvas_t *canvas, const fea_pt_t *tl, const fea_pt_t *br,
    uint16_t color)
{
    fea_roundrect_q4(
        canvas, tl->x_q4, tl->y_q4, br->x_q4, br->y_q4, 3 << 4, color,
        32U);
    /* seam shadow along the bottom and right edges */
    fea_stroke_q4(
        canvas, tl->x_q4 + 16, br->y_q4 - 8, br->x_q4 - 16,
        br->y_q4 - 8, 18, KB_SEAM, 20U);
    /* rivets in the two lower corners */
    fea_ellipse_q4(
        canvas, tl->x_q4 + 40, br->y_q4 - 40, 14, 14, KB_RIVET, 22U);
    fea_ellipse_q4(
        canvas, br->x_q4 - 40, br->y_q4 - 40, 14, 14, KB_RIVET, 22U);
}

static void kb_draw_eye(
    fea_canvas_t *canvas, const kb_layout_t *lay,
    const fea_pose_t *pose, int side)
{
    const fea_pt_t c = lay->eye[side];
    const int32_t lens_r = lay->lens_r_q4;
    /* steel surround + dark lens */
    fea_ring_q4(canvas, c.x_q4, c.y_q4, lens_r + 20, 44, KB_STEEL, 32U);
    fea_ellipse_q4(
        canvas, c.x_q4, c.y_q4, lens_r, lens_r, KB_LENS, 32U);
    const int32_t open = lay->open_q8[side];
    if (open <= 10) {
        /* shutter closed: brass cap with a slit line */
        fea_ellipse_q4(
            canvas, c.x_q4, c.y_q4, lens_r - 8, lens_r - 8,
            KB_BRASS_DARK, 30U);
        fea_stroke_q4(
            canvas, c.x_q4 - lens_r + 24, c.y_q4,
            c.x_q4 + lens_r - 24, c.y_q4, 22, KB_SEAM, 32U);
        return;
    }
    /* partial shutter: brass leaves close inside the lens circle.
     * The lower leaf also rises with the lower-lid channel so warm
     * and joy read as a mechanical squint. */
    const int32_t lolid_cover =
        (pose->lower_lid_q8[side] * (lens_r - 8)) >> 9;
    if (open < 236 || lolid_cover > 8) {
        const int32_t cover = open < 236
            ? ((256 - open) * (lens_r - 12)) >> 8 : 0;
        const int32_t tilt = (pose->lid_tilt_q8[side] * 48) >> 8;
        const int32_t top_edge = c.y_q4 - lens_r + cover + tilt;
        const int32_t bot_edge =
            c.y_q4 + lens_r - cover / 2 - lolid_cover;
        for (int32_t y = (c.y_q4 - lens_r) >> 4;
             y <= ((c.y_q4 + lens_r) >> 4) + 1; ++y) {
            const int32_t yc = y * 16 + 8;
            if (yc >= top_edge && yc <= bot_edge) {
                continue;
            }
            const int64_t dy = yc - c.y_q4;
            const int64_t remain =
                (int64_t)lens_r * lens_r - dy * dy;
            if (remain <= 0) {
                continue;
            }
            const int32_t half = fea_isqrt64(remain);
            fea_hspan_q4(
                canvas, y, c.x_q4 - half, c.x_q4 + half,
                KB_BRASS_DARK, yc < top_edge ? 30U : 26U);
        }
        if (bot_edge - top_edge < 64) {
            /* nearly met: draw the seam so "closed" reads */
            fea_stroke_q4(
                canvas, c.x_q4 - lens_r + 32,
                (top_edge + bot_edge) / 2, c.x_q4 + lens_r - 32,
                (top_edge + bot_edge) / 2, 20, KB_SEAM, 30U);
        }
    }
    /* amber sensor pupil with hot core and glint */
    const fea_pt_t p = lay->pupil[side];
    const int32_t sr = lay->sensor_r_q4[side];
    fea_glow_q4(
        canvas, p.x_q4, p.y_q4, sr, sr + (5 << 4), KB_SENSOR,
        (uint32_t)lay->sensor_alpha);
    fea_ellipse_q4(
        canvas, p.x_q4, p.y_q4, sr / 2, sr / 2, KB_SENSOR_HOT, 26U);
    fea_ellipse_q4(
        canvas, p.x_q4 + sr / 3, p.y_q4 - sr / 3, 20, 20, KB_SENSOR_HOT,
        30U);
    if (pose->sparkle > 150U) {
        const int32_t ray = sr + 40;
        fea_stroke_q4(
            canvas, p.x_q4 - ray, p.y_q4, p.x_q4 + ray, p.y_q4, 14,
            KB_SENSOR_HOT, 18U);
        fea_stroke_q4(
            canvas, p.x_q4, p.y_q4 - ray, p.x_q4, p.y_q4 + ray, 14,
            KB_SENSOR_HOT, 18U);
    }
    /* shutter leaf chords hint the mechanism */
    const int32_t chord = (lay->shutter_angle[side] >> 8) + 8;
    if (chord > 12 && open > 60) {
        fea_stroke_q4(
            canvas, c.x_q4 - lens_r + 40, c.y_q4 - chord,
            c.x_q4 + lens_r - 56, c.y_q4 - chord - 24, 12,
            KB_BRASS_DARK, 14U);
        fea_stroke_q4(
            canvas, c.x_q4 - lens_r + 56, c.y_q4 + chord + 24,
            c.x_q4 + lens_r - 40, c.y_q4 + chord, 12, KB_BRASS_DARK,
            14U);
    }
}

void fea_karakuri_render(
    const fea_pose_t *pose, uint32_t clock, fea_canvas_t *canvas)
{
    kb_layout_t lay;
    kb_layout(pose, clock, &lay);

    fea_fill(canvas, KB_BG);
    /* static vignette frame */
    fea_fill_rect(canvas, 0, 0, FEA_FRAME_WIDTH, 6, KB_BG_EDGE, 32U);
    fea_fill_rect(
        canvas, 0, FEA_FRAME_HEIGHT - 6, FEA_FRAME_WIDTH,
        FEA_FRAME_HEIGHT, KB_BG_EDGE, 32U);
    fea_fill_rect(canvas, 0, 0, 8, FEA_FRAME_HEIGHT, KB_BG_EDGE, 32U);
    fea_fill_rect(
        canvas, FEA_FRAME_WIDTH - 8, 0, FEA_FRAME_WIDTH,
        FEA_FRAME_HEIGHT, KB_BG_EDGE, 32U);

    /* plates: chin, cheeks, forehead (painter order back to front) */
    kb_plate(canvas, &lay.chin_tl, &lay.chin_br, KB_BRASS);
    for (int side = 0; side < 2; ++side) {
        kb_plate(
            canvas, &lay.cheek_tl[side], &lay.cheek_br[side], KB_BRASS);
    }
    kb_plate(canvas, &lay.forehead_tl, &lay.forehead_br, KB_BRASS_LIGHT);

    /* integrated jaw: seam lines run from the cavity corners to the
     * chin-plate edges, the cavity deforms with the lips, and a
     * hinged lower-jaw plate rides the cavity floor */
    {
        const int32_t cav_top =
            (lay.mouth.top_ctrl_y_q4 < lay.mouth.left_y_q4
                 ? lay.mouth.top_ctrl_y_q4 : lay.mouth.left_y_q4) -
            (2 << 4);
        const int32_t cav_bot = lay.mouth.bot_ctrl_y_q4 + (2 << 4);
        fea_stroke_q4(
            canvas, lay.chin_tl.x_q4 + 16, lay.mouth.left_y_q4,
            lay.mouth.left_x_q4, lay.mouth.left_y_q4, 18, KB_SEAM,
            22U);
        fea_stroke_q4(
            canvas, lay.mouth.right_x_q4, lay.mouth.right_y_q4,
            lay.chin_br.x_q4 - 16, lay.mouth.right_y_q4, 18,
            KB_SEAM, 22U);
        fea_roundrect_q4(
            canvas, lay.mouth.left_x_q4 - (2 << 4), cav_top,
            lay.mouth.right_x_q4 + (2 << 4), cav_bot, 3 << 4,
            KB_SLOT, 20U);
        /* hinged lower-jaw plate below the cavity */
        fea_roundrect_q4(
            canvas, lay.mouth.left_x_q4 + (1 << 4), cav_bot - 8,
            lay.mouth.right_x_q4 - (1 << 4), cav_bot + (6 << 4),
            2 << 4, KB_BRASS_DARK, 32U);
        fea_ellipse_q4(
            canvas, lay.mouth.left_x_q4 + (3 << 4), cav_bot + (3 << 4),
            14, 14, KB_RIVET, 24U);
        fea_ellipse_q4(
            canvas, lay.mouth.right_x_q4 - (3 << 4),
            cav_bot + (3 << 4), 14, 14, KB_RIVET, 24U);
    }
    fea_lipmouth_draw(canvas, &lay.mouth);

    /* cheek indicator lamps */
    if (lay.lamp_alpha > 2) {
        for (int side = 0; side < 2; ++side) {
            const int32_t sign = side == 0 ? -1 : 1;
            const fea_pt_t lamp = fea_place(
                pose, KB_CX, KB_CY, sign * (36 << 4), 12 << 4);
            fea_glow_q4(
                canvas, lamp.x_q4, lamp.y_q4, 2 << 4, 6 << 4, KB_LAMP,
                (uint32_t)lay.lamp_alpha);
        }
    }

    /* eyes over the plates */
    for (int side = 0; side < 2; ++side) {
        kb_draw_eye(canvas, &lay, pose, side);
    }

    /* copper brow slats on top */
    for (int side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, lay.brow_in[side].x_q4, lay.brow_in[side].y_q4,
            lay.brow_out[side].x_q4, lay.brow_out[side].y_q4, 52,
            KB_COPPER, 32U);
        /* slat highlight */
        fea_stroke_q4(
            canvas, lay.brow_in[side].x_q4, lay.brow_in[side].y_q4 - 12,
            lay.brow_out[side].x_q4, lay.brow_out[side].y_q4 - 12, 14,
            KB_RIVET, 18U);
    }
}

void fea_karakuri_probe(
    const fea_pose_t *pose, uint32_t clock, fea_probe_t *probe)
{
    kb_layout_t lay;
    kb_layout(pose, clock, &lay);
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 1U;
    for (int side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)lay.eye[side].x_q4;
        probe->eye_cy_q4[side] = (int16_t)lay.eye[side].y_q4;
        probe->eye_open_q8[side] = (int16_t)lay.open_q8[side];
        probe->pupil_x_q4[side] = (int16_t)lay.pupil[side].x_q4;
        probe->pupil_y_q4[side] = (int16_t)lay.pupil[side].y_q4;
        probe->pupil_r_q4[side] = (int16_t)lay.sensor_r_q4[side];
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
    int32_t left = lay.forehead_tl.x_q4;
    int32_t right = lay.forehead_br.x_q4;
    int32_t top = lay.forehead_tl.y_q4;
    int32_t bottom = lay.chin_br.y_q4;
    if (lay.mouth.bot_ctrl_y_q4 + (7 << 4) > bottom) {
        bottom = lay.mouth.bot_ctrl_y_q4 + (7 << 4);
    }
    for (int side = 0; side < 2; ++side) {
        const int32_t lens_left =
            lay.eye[side].x_q4 - lay.lens_r_q4 - 40;
        const int32_t lens_right =
            lay.eye[side].x_q4 + lay.lens_r_q4 + 40;
        if (lens_left < left) {
            left = lens_left;
        }
        if (lens_right > right) {
            right = lens_right;
        }
        if (lay.brow_out[side].y_q4 - 32 < top) {
            top = lay.brow_out[side].y_q4 - 32;
        }
    }
    probe->extent_left_q4 = (int16_t)left;
    probe->extent_top_q4 = (int16_t)top;
    probe->extent_right_q4 = (int16_t)right;
    probe->extent_bottom_q4 = (int16_t)bottom;
}
