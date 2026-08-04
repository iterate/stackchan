#include "fea_internal.h"

/*
 * emote-sticker — bold sticker-sheet face.
 *
 * Signature acting channels: emoji grammar. Big outlined features on a
 * golden badge, arc "joy eyes" when the lower lids squeeze, plus
 * per-emotion glyph accessories: sweat drop, sparkles, Zzz, shock
 * rays, thought dots, blush. High-contrast teeth/tongue mouth.
 */

enum {
    ST_CX = 80 << 4,
    ST_CY = 60 << 4,
};

static const uint16_t ST_BG = FEA_RGB(236, 233, 226);
static const uint16_t ST_SHADOW = FEA_RGB(204, 199, 188);
static const uint16_t ST_BORDER = FEA_RGB(252, 252, 250);
static const uint16_t ST_INK = FEA_RGB(46, 40, 38);
static const uint16_t ST_FACE = FEA_RGB(248, 198, 62);
static const uint16_t ST_FACE_SHADE = FEA_RGB(232, 172, 44);
static const uint16_t ST_SCLERA = FEA_RGB(252, 250, 244);
static const uint16_t ST_IRIS = FEA_RGB(122, 82, 46);
static const uint16_t ST_GLINT = FEA_RGB(255, 255, 252);
static const uint16_t ST_MOUTH_FILL = FEA_RGB(142, 44, 46);
static const uint16_t ST_TEETH = FEA_RGB(252, 250, 242);
static const uint16_t ST_TONGUE = FEA_RGB(226, 118, 116);
static const uint16_t ST_BLUSH = FEA_RGB(244, 138, 122);
static const uint16_t ST_SWEAT = FEA_RGB(118, 178, 240);
static const uint16_t ST_SPARK = FEA_RGB(255, 244, 180);

typedef struct {
    fea_pt_t badge;
    int32_t badge_r_q4;
    fea_pt_t eye[2];
    int32_t eye_rx_q4, eye_ry_q4;
    fea_pt_t iris[2];
    int32_t iris_r_q4;
    int32_t open_q8[2];
    int32_t arc_eye[2];            /* 1 == closed-happy arc eye */
    fea_pt_t brow_in[2], brow_out[2];
    fea_lipmouth_t mouth;
    int32_t blush_alpha;
} st_layout_t;

static void st_layout(
    const fea_pose_t *pose, uint32_t clock, st_layout_t *lay)
{
    (void)clock;
    lay->badge = fea_place(pose, ST_CX, ST_CY, 0, 0);
    {
        int32_t scale = pose->scale_y_q8;
        if (scale > 260) {
            scale = 260;    /* badge + shadow stay on-frame */
        }
        lay->badge_r_q4 = ((50 << 4) * scale) >> 8;
    }

    lay->eye_rx_q4 = 9 << 4;
    lay->eye_ry_q4 = 10 << 4;
    lay->iris_r_q4 = (72 * pose->pupil_scale_q8) >> 8;
    const int32_t gx = (pose->gaze_x_q8 * 4 * 16) >> 8;
    const int32_t gy = (pose->gaze_y_q8 * 4 * 16) >> 8;
    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        lay->eye[side] = fea_place(
            pose, ST_CX, ST_CY, sign * (20 << 4), -(9 << 4));
        const int32_t max_shift = lay->eye_rx_q4 - lay->iris_r_q4 - 8;
        lay->iris[side].x_q4 = lay->eye[side].x_q4 +
            fea_clamp_i32(gx, -max_shift, max_shift);
        lay->iris[side].y_q4 = lay->eye[side].y_q4 +
            fea_clamp_i32(gy, -max_shift, max_shift);
        lay->open_q8[side] = pose->eye_open_q8[side];
        lay->arc_eye[side] =
            pose->lower_lid_q8[side] > 132 && pose->eye_open_q8[side] > 40
                ? 1 : 0;
    }

    for (int side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t raise_q4 =
            (pose->brow_raise_q8[side] * 6 * 16) >> 8;
        const int32_t tilt_q4 =
            (pose->brow_tilt_q8[side] * 6 * 16) >> 8;
        const int32_t knit_q4 = (pose->brow_knit_q8 * 5 * 16) >> 8;
        lay->brow_in[side] = fea_place(
            pose, ST_CX, ST_CY, sign * ((11 << 4) - knit_q4 / 2),
            -(24 << 4) - raise_q4 - tilt_q4 / 2 + knit_q4 / 2);
        lay->brow_out[side] = fea_place(
            pose, ST_CX, ST_CY, sign * (29 << 4),
            -(26 << 4) - raise_q4 + tilt_q4 / 2);
    }

    /* badge-parented giant mouth */
    const fea_pt_t mouth_c = fea_place(pose, ST_CX, ST_CY, 0, 23 << 4);
    int32_t half_w_q4 = ((24 << 4) * pose->mouth_w_q8) >> 8;
    half_w_q4 -= (pose->round_q8 * 9 * 16) >> 8;
    half_w_q4 = fea_clamp_i32(half_w_q4, 7 << 4, 27 << 4);
    const int32_t corner_l_q4 = (pose->corner_q8[0] * 11 * 16) >> 8;
    const int32_t corner_r_q4 = (pose->corner_q8[1] * 11 * 16) >> 8;
    lay->mouth.left_x_q4 = (int16_t)(mouth_c.x_q4 - half_w_q4);
    lay->mouth.left_y_q4 = (int16_t)(mouth_c.y_q4 - corner_l_q4);
    lay->mouth.right_x_q4 = (int16_t)(mouth_c.x_q4 + half_w_q4);
    lay->mouth.right_y_q4 = (int16_t)(mouth_c.y_q4 - corner_r_q4);
    const int32_t jaw_q4 =
        ((pose->jaw_q8 * 20 * 16) >> 8) + ((pose->round_q8 * 96) >> 8);
    const int32_t curve_q4 = (pose->curve_q8 * 9 * 16) >> 8;
    lay->mouth.top_ctrl_y_q4 =
        (int16_t)(mouth_c.y_q4 - curve_q4 - jaw_q4 / 3);
    {
        int32_t bot = mouth_c.y_q4 - curve_q4 / 3 + jaw_q4;
        if (bot > (113 << 4)) {
            bot = 113 << 4;   /* mouth may never clip */
        }
        lay->mouth.bot_ctrl_y_q4 = (int16_t)bot;
    }
    lay->mouth.lip_q4 =
        (int16_t)(40 + ((pose->press_q8 * 24) >> 8));
    lay->mouth.lip_color = ST_INK;
    lay->mouth.fill_color = ST_MOUTH_FILL;
    lay->mouth.teeth_color = ST_TEETH;
    lay->mouth.tongue_color = ST_TONGUE;
    lay->mouth.teeth_q8 = pose->teeth_q8;
    lay->mouth.tongue_q8 = pose->tongue_q8;
    lay->mouth.alpha = 32U;

    lay->blush_alpha = (pose->cheek_q8 * 26) >> 8;
}

/* small Z glyph from three strokes */
static void st_glyph_z(
    fea_canvas_t *canvas, int32_t x_q4, int32_t y_q4, int32_t size_q4,
    uint32_t alpha)
{
    fea_stroke_q4(
        canvas, x_q4 - size_q4, y_q4 - size_q4, x_q4 + size_q4,
        y_q4 - size_q4, 26, ST_INK, alpha);
    fea_stroke_q4(
        canvas, x_q4 + size_q4, y_q4 - size_q4, x_q4 - size_q4,
        y_q4 + size_q4, 26, ST_INK, alpha);
    fea_stroke_q4(
        canvas, x_q4 - size_q4, y_q4 + size_q4, x_q4 + size_q4,
        y_q4 + size_q4, 26, ST_INK, alpha);
}

static void st_sparkle(
    fea_canvas_t *canvas, int32_t x_q4, int32_t y_q4, int32_t ray_q4,
    uint32_t alpha)
{
    fea_stroke_q4(
        canvas, x_q4 - ray_q4, y_q4, x_q4 + ray_q4, y_q4, 24, ST_SPARK,
        alpha);
    fea_stroke_q4(
        canvas, x_q4, y_q4 - ray_q4, x_q4, y_q4 + ray_q4, 24, ST_SPARK,
        alpha);
    fea_ellipse_q4(canvas, x_q4, y_q4, 20, 20, ST_GLINT, alpha);
}

static void st_draw_eye(
    fea_canvas_t *canvas, const st_layout_t *lay,
    const fea_pose_t *pose, int side)
{
    const fea_pt_t c = lay->eye[side];
    const int32_t rx = lay->eye_rx_q4;
    const int32_t ry = lay->eye_ry_q4;
    const int32_t open = lay->open_q8[side];
    if (lay->arc_eye[side] != 0) {
        /* closed-happy arc: three chained strokes forming a flat U */
        const int32_t w = rx;
        const int32_t lift = ry / 2;
        fea_stroke_q4(
            canvas, c.x_q4 - w, c.y_q4 - lift / 2, c.x_q4 - w / 3,
            c.y_q4 + lift / 2, 40, ST_INK, 32U);
        fea_stroke_q4(
            canvas, c.x_q4 - w / 3, c.y_q4 + lift / 2, c.x_q4 + w / 3,
            c.y_q4 + lift / 2, 40, ST_INK, 32U);
        fea_stroke_q4(
            canvas, c.x_q4 + w / 3, c.y_q4 + lift / 2, c.x_q4 + w,
            c.y_q4 - lift / 2, 40, ST_INK, 32U);
        return;
    }
    if (open <= 12) {
        /* closed: single ink line */
        fea_stroke_q4(
            canvas, c.x_q4 - rx + 8, c.y_q4, c.x_q4 + rx - 8, c.y_q4,
            36, ST_INK, 32U);
        return;
    }
    /* outlined sclera */
    fea_ellipse_q4(
        canvas, c.x_q4, c.y_q4, rx + 22, ry + 22, ST_INK, 32U);
    fea_ellipse_q4(canvas, c.x_q4, c.y_q4, rx, ry, ST_SCLERA, 32U);
    /* iris/pupil/glint */
    const fea_pt_t iris = lay->iris[side];
    fea_ellipse_q4(
        canvas, iris.x_q4, iris.y_q4, lay->iris_r_q4, lay->iris_r_q4,
        ST_IRIS, 32U);
    fea_ellipse_q4(
        canvas, iris.x_q4, iris.y_q4, lay->iris_r_q4 / 2,
        lay->iris_r_q4 / 2, ST_INK, 32U);
    fea_ellipse_q4(
        canvas, iris.x_q4 + lay->iris_r_q4 / 3,
        iris.y_q4 - lay->iris_r_q4 / 3, 22, 24, ST_GLINT, 30U);
    /* lids cut from above with a lash line */
    const int32_t covered = (2 * ry * (256 - open)) >> 8;
    if (covered > 8) {
        const int32_t lid_edge = c.y_q4 - ry + covered;
        for (int32_t y = (c.y_q4 - ry - 24) >> 4;
             y <= (lid_edge + 15) >> 4; ++y) {
            const int32_t yc = y * 16 + 8;
            if (yc < lid_edge) {
                fea_hspan_q4(
                    canvas, y, c.x_q4 - rx - 26, c.x_q4 + rx + 26,
                    ST_FACE, 32U);
            }
        }
        const int32_t tilt = (pose->lid_tilt_q8[side] * 36) >> 8;
        const int32_t inner = side == 0 ? c.x_q4 + rx : c.x_q4 - rx;
        const int32_t outer = side == 0 ? c.x_q4 - rx : c.x_q4 + rx;
        fea_stroke_q4(
            canvas, inner, lid_edge - tilt, outer, lid_edge + tilt, 30,
            ST_INK, 32U);
    }
    /* lower lid raise: face-colored cut with a soft crease */
    const int32_t lower = pose->lower_lid_q8[side];
    if (lower > 16) {
        const int32_t cut = (2 * ry * lower) >> 9;
        const int32_t lid_edge = c.y_q4 + ry - cut;
        for (int32_t y = lid_edge >> 4;
             y <= (c.y_q4 + ry + 24) >> 4; ++y) {
            const int32_t yc = y * 16 + 8;
            if (yc >= lid_edge) {
                fea_hspan_q4(
                    canvas, y, c.x_q4 - rx - 26, c.x_q4 + rx + 26,
                    ST_FACE, 32U);
            }
        }
        fea_stroke_q4(
            canvas, c.x_q4 - rx + 16, lid_edge, c.x_q4 + rx - 16,
            lid_edge, 22, ST_FACE_SHADE, 30U);
    }
}

void fea_sticker_render(
    const fea_pose_t *pose, uint32_t clock, fea_canvas_t *canvas)
{
    st_layout_t lay;
    st_layout(pose, clock, &lay);

    fea_fill(canvas, ST_BG);
    /* sticker drop shadow, border, face */
    fea_ellipse_q4(
        canvas, lay.badge.x_q4 + 40, lay.badge.y_q4 + 36,
        lay.badge_r_q4 + 24, lay.badge_r_q4, ST_SHADOW, 22U);
    fea_ellipse_q4(
        canvas, lay.badge.x_q4, lay.badge.y_q4, lay.badge_r_q4 + 56,
        lay.badge_r_q4 + 48, ST_BORDER, 32U);
    fea_ellipse_q4(
        canvas, lay.badge.x_q4, lay.badge.y_q4, lay.badge_r_q4 + 12,
        lay.badge_r_q4 + 8, ST_INK, 32U);
    fea_ellipse_q4(
        canvas, lay.badge.x_q4, lay.badge.y_q4, lay.badge_r_q4,
        lay.badge_r_q4 - 4, ST_FACE, 32U);
    /* lower face shading */
    fea_ellipse_q4(
        canvas, lay.badge.x_q4, lay.badge.y_q4 + lay.badge_r_q4 / 2,
        (lay.badge_r_q4 * 3) / 4, lay.badge_r_q4 / 3, ST_FACE_SHADE,
        10U);

    /* mouth first (under blush/eyes), corners are parents */
    fea_lipmouth_draw(canvas, &lay.mouth);

    if (lay.blush_alpha > 2) {
        for (int side = 0; side < 2; ++side) {
            const int32_t sign = side == 0 ? -1 : 1;
            const fea_pt_t pad = fea_place(
                pose, ST_CX, ST_CY, sign * (33 << 4), 6 << 4);
            fea_ellipse_q4(
                canvas, pad.x_q4, pad.y_q4, 8 << 4, 4 << 4, ST_BLUSH,
                (uint32_t)lay.blush_alpha);
        }
    }

    for (int side = 0; side < 2; ++side) {
        st_draw_eye(canvas, &lay, pose, side);
    }
    for (int side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, lay.brow_in[side].x_q4, lay.brow_in[side].y_q4,
            lay.brow_out[side].x_q4, lay.brow_out[side].y_q4, 48,
            ST_INK, 32U);
    }

    /* emoji accessories, scaled by the acting curve */
    const int32_t act = pose->act_q8 > 0 ? pose->act_q8 : 0;
    const uint32_t a20 = (uint32_t)((20 * act) >> 8);
    const uint32_t a26 = (uint32_t)((26 * act) >> 8);
    switch (pose->emotion) {
    case FACE_EXPRESSION_CONCERN:
    case FACE_EXPRESSION_EMBARRASSED: {
        /* sweat drop sliding slowly down the temple */
        const int32_t slide =
            (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U /
                                    40000U)) * 24) >> 14;
        const fea_pt_t drop = fea_place(
            pose, ST_CX, ST_CY, 40 << 4, -(30 << 4));
        const int32_t dy = drop.y_q4 + slide;
        fea_triangle_q4(
            canvas, drop.x_q4, dy - (6 << 4), drop.x_q4 - (3 << 4),
            dy, drop.x_q4 + (3 << 4), dy, ST_SWEAT, a26);
        fea_ellipse_q4(
            canvas, drop.x_q4, dy + 20, 3 << 4, 4 << 4, ST_SWEAT, a26);
        break;
    }
    case FACE_EXPRESSION_EXCITED: {
        const fea_pt_t s1 = fea_place(
            pose, ST_CX, ST_CY, -(44 << 4), -(34 << 4));
        const fea_pt_t s2 = fea_place(
            pose, ST_CX, ST_CY, 45 << 4, -(28 << 4));
        st_sparkle(canvas, s1.x_q4, s1.y_q4, 5 << 4, a26);
        st_sparkle(canvas, s2.x_q4, s2.y_q4, 4 << 4, a20);
        break;
    }
    case FACE_EXPRESSION_SLEEPY: {
        const fea_pt_t z = fea_place(
            pose, ST_CX, ST_CY, 42 << 4, -(26 << 4));
        st_glyph_z(canvas, z.x_q4, z.y_q4, 3 << 4, 32U);
        st_glyph_z(
            canvas, z.x_q4 + (7 << 4), z.y_q4 - (8 << 4), 2 << 4, a26);
        break;
    }
    case FACE_EXPRESSION_SURPRISE: {
        /* shock rays over the crown */
        for (int ray = -2; ray <= 2; ++ray) {
            const fea_pt_t base = fea_place(
                pose, ST_CX, ST_CY, ray * (14 << 4), -(48 << 4));
            fea_stroke_q4(
                canvas, base.x_q4, base.y_q4, base.x_q4 + ray * 28,
                base.y_q4 - (5 << 4), 22, ST_INK, a20);
        }
        break;
    }
    case FACE_EXPRESSION_THOUGHTFUL: {
        for (int dot = 0; dot < 3; ++dot) {
            const fea_pt_t p = fea_place(
                pose, ST_CX, ST_CY, -(46 << 4) - dot * (5 << 4),
                (30 << 4) + dot * (4 << 4));
            fea_ellipse_q4(
                canvas, p.x_q4, p.y_q4, (3 - dot) << 3, (3 - dot) << 3,
                ST_INK, a20);
        }
        break;
    }
    default:
        break;
    }
}

void fea_sticker_probe(
    const fea_pose_t *pose, uint32_t clock, fea_probe_t *probe)
{
    st_layout_t lay;
    st_layout(pose, clock, &lay);
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 1U;
    for (int side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)lay.eye[side].x_q4;
        probe->eye_cy_q4[side] = (int16_t)lay.eye[side].y_q4;
        probe->eye_open_q8[side] = (int16_t)lay.open_q8[side];
        probe->pupil_x_q4[side] = (int16_t)lay.iris[side].x_q4;
        probe->pupil_y_q4[side] = (int16_t)lay.iris[side].y_q4;
        probe->pupil_r_q4[side] = (int16_t)lay.iris_r_q4;
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
    probe->extent_left_q4 =
        (int16_t)(lay.badge.x_q4 - lay.badge_r_q4 - 56);
    probe->extent_top_q4 =
        (int16_t)(lay.badge.y_q4 - lay.badge_r_q4 - 48);
    probe->extent_right_q4 =
        (int16_t)(lay.badge.x_q4 + lay.badge_r_q4 + 56);
    probe->extent_bottom_q4 =
        (int16_t)(lay.badge.y_q4 + lay.badge_r_q4 + 52);
}
