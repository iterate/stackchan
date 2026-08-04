#include "fea_favourite_variants_internal.h"

/*
 * Shared facial layout only.  The silhouettes, materials and secondary
 * acting remain family-specific.  Values are deliberately chunky enough
 * to survive the production 4:1 nearest-neighbour reduction.
 */

typedef struct {
    int16_t anchor_x;
    int16_t anchor_y;
    int16_t eye_dx;
    int16_t eye_y;
    int16_t eye_rx;
    int16_t eye_ry;
    int16_t pupil_r;
    int16_t brow_in_x;
    int16_t brow_out_x;
    int16_t brow_y;
    int16_t brow_raise;
    int16_t brow_tilt;
    int16_t mouth_y;
    int16_t mouth_half_w;
    int16_t mouth_jaw;
    int16_t mouth_round_narrow;
    int16_t mouth_corner;
    int16_t mouth_curve;
    int16_t lip_q4;
} fv_geometry_t;

static const fv_geometry_t FV_GEOMETRY[FEA_FAVOURITE_PROFILE_COUNT] = {
    [FEA_FAVOURITE_LANTERN_BLOOM] = {
        80, 60, 15, -7, 7, 9, 2, 8, 23, -22, 6, 6,
        14, 20, 18, 7, 8, 7, 32,
    },
    [FEA_FAVOURITE_COMET_RASCAL] = {
        68, 61, 15, -8, 8, 9, 2, 8, 24, -23, 6, 6,
        14, 20, 18, 7, 9, 7, 34,
    },
    [FEA_FAVOURITE_MOON_MOTH_ORACLE] = {
        80, 59, 16, -7, 8, 8, 2, 8, 25, -22, 7, 7,
        15, 17, 17, 6, 8, 7, 30,
    },
    [FEA_FAVOURITE_GILDED_NOH] = {
        80, 61, 20, -8, 12, 8, 3, 10, 34, -24, 6, 6,
        22, 23, 18, 8, 8, 6, 36,
    },
    [FEA_FAVOURITE_BEETLE_AUTOMATON] = {
        80, 61, 21, -8, 12, 11, 4, 10, 35, -26, 7, 7,
        23, 22, 18, 7, 8, 5, 30,
    },
    [FEA_FAVOURITE_KINTSUGI_MARIONETTE] = {
        80, 60, 18, -9, 10, 11, 3, 9, 31, -26, 7, 7,
        22, 22, 19, 7, 9, 7, 34,
    },
    [FEA_FAVOURITE_POP_BURST] = {
        80, 60, 20, -10, 10, 11, 3, 10, 32, -27, 7, 7,
        22, 25, 22, 9, 11, 9, 42,
    },
    [FEA_FAVOURITE_FELT_PATCH_PAL] = {
        80, 61, 20, -10, 10, 10, 4, 10, 32, -27, 7, 7,
        22, 23, 20, 8, 10, 8, 44,
    },
    [FEA_FAVOURITE_SPEECH_BUBBLE_SPRITE] = {
        79, 58, 20, -8, 11, 10, 3, 10, 33, -25, 7, 7,
        22, 25, 22, 9, 11, 9, 44,
    },
};

static int32_t fv_q4(int32_t pixels)
{
    return pixels * 16;
}

static int32_t fv_speech_asym(
    const fea_pose_t *pose, uint32_t sample_clock)
{
    if (pose->speaking == 0U ||
        pose->speech_phase != FACE_SPEECH_ACTIVE) {
        return 0;
    }
    return (fea_sin_q14(
                (uint32_t)((uint64_t)sample_clock * 65536U / 5700U)) *
            ((int32_t)pose->audio_q8 * 36 / 255)) >> 14;
}

void fea_favourite_layout_build(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_favourite_layout_t *layout)
{
    const fv_geometry_t *geometry = &FV_GEOMETRY[profile];
    const int32_t cx_q4 = fv_q4(geometry->anchor_x);
    const int32_t cy_q4 = fv_q4(geometry->anchor_y);
    const int32_t asym_q4 = fv_speech_asym(pose, sample_clock);

    layout->anchor = fea_place(pose, cx_q4, cy_q4, 0, 0);
    layout->eye_rx_q4 = fv_q4(geometry->eye_rx);
    layout->pupil_r_q4 =
        (fv_q4(geometry->pupil_r) * pose->pupil_scale_q8) >> 8;
    layout->pupil_r_q4 =
        fea_clamp_i32(layout->pupil_r_q4, 20, fv_q4(geometry->pupil_r + 2));

    const int32_t gaze_x_q4 = (pose->gaze_x_q8 *
        fv_q4(geometry->eye_rx - 3)) >> 8;
    const int32_t gaze_y_q4 = (pose->gaze_y_q8 *
        fv_q4(geometry->eye_ry - 3)) >> 8;

    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t side_asym = side == 0 ? asym_q4 : -asym_q4;
        layout->eye[side] = fea_place(
            pose, cx_q4, cy_q4, sign * fv_q4(geometry->eye_dx),
            fv_q4(geometry->eye_y) + side_asym / 4);
        layout->open_q8[side] = pose->eye_open_q8[side];

        const int32_t base_ry_q4 = fv_q4(geometry->eye_ry);
        int32_t eye_ry_q4 =
            (base_ry_q4 * pose->eye_open_q8[side]) >> 8;
        eye_ry_q4 -=
            (base_ry_q4 * pose->lower_lid_q8[side]) >> 9;
        if (pose->eye_open_q8[side] > 52 && eye_ry_q4 < 44) {
            eye_ry_q4 = 44;
        }
        layout->eye_ry_q4[side] =
            fea_clamp_i32(eye_ry_q4, 0, base_ry_q4 + 32);

        const int32_t max_x_q4 = layout->eye_rx_q4 -
            layout->pupil_r_q4 - 18;
        const int32_t max_y_q4 = base_ry_q4 -
            layout->pupil_r_q4 - 18;
        layout->pupil[side].x_q4 = layout->eye[side].x_q4 +
            fea_clamp_i32(gaze_x_q4, -max_x_q4, max_x_q4);
        layout->pupil[side].y_q4 = layout->eye[side].y_q4 +
            fea_clamp_i32(gaze_y_q4, -max_y_q4, max_y_q4);

        const int32_t raise_q4 =
            (pose->brow_raise_q8[side] *
             fv_q4(geometry->brow_raise)) >> 8;
        const int32_t tilt_q4 =
            (pose->brow_tilt_q8[side] *
             fv_q4(geometry->brow_tilt)) >> 8;
        const int32_t knit_q4 =
            (pose->brow_knit_q8 * fv_q4(5)) >> 8;
        layout->brow_in[side] = fea_place(
            pose, cx_q4, cy_q4,
            sign * (fv_q4(geometry->brow_in_x) - knit_q4 / 2),
            fv_q4(geometry->brow_y) - raise_q4 -
                tilt_q4 / 2 + knit_q4 / 3 - side_asym / 2);
        layout->brow_out[side] = fea_place(
            pose, cx_q4, cy_q4,
            sign * fv_q4(geometry->brow_out_x),
            fv_q4(geometry->brow_y - 2) - raise_q4 +
                tilt_q4 / 2 - side_asym / 2);
    }

    const fea_pt_t mouth_center = fea_place(
        pose, cx_q4, cy_q4, 0, fv_q4(geometry->mouth_y));
    int32_t half_w_q4 =
        (fv_q4(geometry->mouth_half_w) * pose->mouth_w_q8) >> 8;
    half_w_q4 -=
        (pose->round_q8 * fv_q4(geometry->mouth_round_narrow)) >> 8;
    half_w_q4 = fea_clamp_i32(
        half_w_q4, fv_q4(6), fv_q4(geometry->mouth_half_w + 4));
    const int32_t corner_l_q4 =
        (pose->corner_q8[0] * fv_q4(geometry->mouth_corner)) >> 8;
    const int32_t corner_r_q4 =
        (pose->corner_q8[1] * fv_q4(geometry->mouth_corner)) >> 8;
    layout->mouth.left_x_q4 =
        (int16_t)(mouth_center.x_q4 - half_w_q4);
    layout->mouth.left_y_q4 =
        (int16_t)(mouth_center.y_q4 - corner_l_q4);
    layout->mouth.right_x_q4 =
        (int16_t)(mouth_center.x_q4 + half_w_q4);
    layout->mouth.right_y_q4 =
        (int16_t)(mouth_center.y_q4 - corner_r_q4);

    const int32_t jaw_q4 =
        (pose->jaw_q8 * fv_q4(geometry->mouth_jaw)) >> 8;
    const int32_t round_gape_q4 =
        (pose->round_q8 * fv_q4(5)) >> 8;
    const int32_t curve_q4 =
        (pose->curve_q8 * fv_q4(geometry->mouth_curve)) >> 8;
    layout->mouth.top_ctrl_y_q4 = (int16_t)(
        mouth_center.y_q4 - curve_q4 - jaw_q4 / 4);
    layout->mouth.bot_ctrl_y_q4 = (int16_t)fea_clamp_i32(
        mouth_center.y_q4 - curve_q4 / 3 + jaw_q4 + round_gape_q4,
        mouth_center.y_q4 - fv_q4(2), fv_q4(112));
    layout->mouth.lip_q4 = (int16_t)(
        geometry->lip_q4 + ((pose->press_q8 * 26) >> 8));
    layout->mouth.lip_color = FEA_RGB(24, 22, 28);
    layout->mouth.fill_color = FEA_RGB(30, 20, 30);
    layout->mouth.teeth_color = FEA_RGB(248, 245, 232);
    layout->mouth.tongue_color = FEA_RGB(214, 96, 112);
    layout->mouth.teeth_q8 = pose->teeth_q8;
    layout->mouth.tongue_q8 = pose->tongue_q8;
    layout->mouth.alpha = 32U;
}

void fea_favourite_layout_probe(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    fea_probe_t *probe)
{
    (void)profile;
    probe->emotion = pose->emotion;
    probe->act_q8 = pose->act_q8;
    probe->has_mouth = 1U;
    for (int32_t side = 0; side < 2; ++side) {
        probe->eye_cx_q4[side] = (int16_t)layout->eye[side].x_q4;
        probe->eye_cy_q4[side] = (int16_t)layout->eye[side].y_q4;
        probe->eye_open_q8[side] = (int16_t)layout->open_q8[side];
        probe->pupil_x_q4[side] = (int16_t)layout->pupil[side].x_q4;
        probe->pupil_y_q4[side] = (int16_t)layout->pupil[side].y_q4;
        probe->pupil_r_q4[side] = (int16_t)layout->pupil_r_q4;
        probe->brow_y_q4[side] = (int16_t)(
            (layout->brow_in[side].y_q4 +
             layout->brow_out[side].y_q4) / 2);
        probe->brow_tilt_q8[side] = pose->brow_tilt_q8[side];
    }
    probe->mouth_cx_q4 = (int16_t)(
        (layout->mouth.left_x_q4 + layout->mouth.right_x_q4) / 2);
    probe->mouth_cy_q4 = (int16_t)(
        (layout->mouth.left_y_q4 + layout->mouth.right_y_q4) / 2);
    probe->corner_x_q4[0] = layout->mouth.left_x_q4;
    probe->corner_y_q4[0] = layout->mouth.left_y_q4;
    probe->corner_x_q4[1] = layout->mouth.right_x_q4;
    probe->corner_y_q4[1] = layout->mouth.right_y_q4;
    probe->jaw_q4 = (int16_t)(
        layout->mouth.bot_ctrl_y_q4 -
        layout->mouth.top_ctrl_y_q4);

    /* Every authored silhouette and accessory stays inside this box,
     * including full schema-v2 head offsets and acting overshoot. */
    probe->extent_left_q4 = FEA_SAFE_LEFT << 4;
    probe->extent_top_q4 = FEA_SAFE_TOP << 4;
    probe->extent_right_q4 = FEA_SAFE_RIGHT << 4;
    probe->extent_bottom_q4 = FEA_SAFE_BOTTOM << 4;
}

void fea_favourite_set_mouth_colors(
    fea_favourite_layout_t *layout,
    uint16_t lip,
    uint16_t fill,
    uint16_t teeth,
    uint16_t tongue,
    int32_t lip_extra_q4)
{
    layout->mouth.lip_color = lip;
    layout->mouth.fill_color = fill;
    layout->mouth.teeth_color = teeth;
    layout->mouth.tongue_color = tongue;
    layout->mouth.lip_q4 = (int16_t)fea_clamp_i32(
        layout->mouth.lip_q4 + lip_extra_q4, 18, 88);
}

static void fv_draw_happy_arc(
    fea_canvas_t *canvas,
    const fea_pt_t *center,
    int32_t rx_q4,
    int32_t ry_q4,
    uint16_t color,
    int32_t thickness_q4)
{
    fea_stroke_q4(
        canvas, center->x_q4 - rx_q4, center->y_q4 - ry_q4 / 4,
        center->x_q4, center->y_q4 + ry_q4 / 2,
        thickness_q4, color, 32U);
    fea_stroke_q4(
        canvas, center->x_q4, center->y_q4 + ry_q4 / 2,
        center->x_q4 + rx_q4, center->y_q4 - ry_q4 / 4,
        thickness_q4, color, 32U);
}

void fea_favourite_draw_eyes(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    const fea_favourite_eye_style_t *style)
{
    for (int32_t side = 0; side < 2; ++side) {
        const fea_pt_t center = layout->eye[side];
        const int32_t rx_q4 = layout->eye_rx_q4;
        const int32_t ry_q4 = layout->eye_ry_q4[side];
        const int32_t open_q8 = layout->open_q8[side];
        const bool happy_arc = style->happy_arcs != 0U &&
            pose->lower_lid_q8[side] > 118 && open_q8 > 36;

        if (happy_arc) {
            fv_draw_happy_arc(
                canvas, &center, rx_q4, fv_q4(7), style->lid,
                style->kind == FEA_FAVOURITE_EYE_INK ? 46 : 38);
            continue;
        }
        if (open_q8 <= 14 || ry_q4 <= 20) {
            fea_stroke_q4(
                canvas, center.x_q4 - rx_q4 + 12, center.y_q4,
                center.x_q4 + rx_q4 - 12, center.y_q4,
                style->kind == FEA_FAVOURITE_EYE_LENS ? 38 : 44,
                style->lid, 32U);
            continue;
        }

        if (style->kind == FEA_FAVOURITE_EYE_LENS) {
            fea_ellipse_q4(
                canvas, center.x_q4, center.y_q4, rx_q4 + 34,
                ry_q4 + 34, style->socket, 32U);
            fea_ellipse_q4(
                canvas, center.x_q4, center.y_q4, rx_q4,
                ry_q4, style->white, 32U);
            const fea_pt_t pupil = layout->pupil[side];
            fea_glow_q4(
                canvas, pupil.x_q4, pupil.y_q4,
                layout->pupil_r_q4,
                layout->pupil_r_q4 + fv_q4(4),
                style->iris, 28U);
            fea_ellipse_q4(
                canvas, pupil.x_q4, pupil.y_q4,
                layout->pupil_r_q4 / 2,
                layout->pupil_r_q4 / 2,
                style->glint, 30U);
            fea_stroke_q4(
                canvas, center.x_q4 - rx_q4 + 20,
                center.y_q4 - ry_q4 / 2,
                center.x_q4 + rx_q4 - 20,
                center.y_q4 - ry_q4 / 2 -
                    ((pose->lid_tilt_q8[side] * 28) >> 8),
                18, style->lid, 20U);
            continue;
        }

        if (style->kind == FEA_FAVOURITE_EYE_EMBER) {
            if (style->outlined != 0U) {
                fea_glow_q4(
                    canvas, center.x_q4, center.y_q4,
                    rx_q4, rx_q4 + fv_q4(3), style->socket, 10U);
            }
            fea_ellipse_q4(
                canvas, center.x_q4, center.y_q4,
                rx_q4, ry_q4, style->pupil, 31U);
            const fea_pt_t pupil = layout->pupil[side];
            fea_ellipse_q4(
                canvas, pupil.x_q4, pupil.y_q4,
                layout->pupil_r_q4, layout->pupil_r_q4,
                style->iris, 32U);
            fea_ellipse_q4(
                canvas, pupil.x_q4 + layout->pupil_r_q4 / 3,
                pupil.y_q4 - layout->pupil_r_q4 / 3,
                18, 20, style->glint, 30U);
            continue;
        }

        fea_ellipse_q4(
            canvas, center.x_q4, center.y_q4,
            rx_q4 + (style->outlined != 0U ? 34 : 18),
            ry_q4 + (style->outlined != 0U ? 34 : 18),
            style->socket, 32U);
        fea_ellipse_q4(
            canvas, center.x_q4, center.y_q4,
            rx_q4, ry_q4, style->white, 32U);
        const fea_pt_t pupil = layout->pupil[side];
        const int32_t iris_r_q4 = layout->pupil_r_q4 +
            (style->kind == FEA_FAVOURITE_EYE_BUTTON ? 34 : 18);
        fea_ellipse_q4(
            canvas, pupil.x_q4, pupil.y_q4,
            iris_r_q4, iris_r_q4, style->iris, 32U);
        if (style->kind == FEA_FAVOURITE_EYE_BUTTON) {
            const int32_t hole = iris_r_q4 / 4;
            fea_ellipse_q4(
                canvas, pupil.x_q4 - hole, pupil.y_q4,
                16, 16, style->pupil, 32U);
            fea_ellipse_q4(
                canvas, pupil.x_q4 + hole, pupil.y_q4,
                16, 16, style->pupil, 32U);
            fea_stroke_q4(
                canvas, pupil.x_q4 - hole, pupil.y_q4 - 16,
                pupil.x_q4 + hole, pupil.y_q4 + 16,
                13, style->glint, 22U);
        } else {
            fea_ellipse_q4(
                canvas, pupil.x_q4, pupil.y_q4,
                layout->pupil_r_q4 / 2,
                layout->pupil_r_q4 / 2, style->pupil, 32U);
            fea_ellipse_q4(
                canvas, pupil.x_q4 + layout->pupil_r_q4 / 3,
                pupil.y_q4 - layout->pupil_r_q4 / 3,
                18, 20, style->glint, 30U);
        }
    }
}

void fea_favourite_draw_brows(
    fea_canvas_t *canvas,
    const fea_favourite_layout_t *layout,
    uint16_t color,
    int32_t thickness_q4,
    uint32_t alpha)
{
    for (int32_t side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, layout->brow_in[side].x_q4,
            layout->brow_in[side].y_q4,
            layout->brow_out[side].x_q4,
            layout->brow_out[side].y_q4,
            thickness_q4, color, alpha);
        /* A shorter, thinner outer pass gives a tapered silhouette. */
        const int32_t mid_x_q4 =
            (layout->brow_in[side].x_q4 * 2 +
             layout->brow_out[side].x_q4) / 3;
        const int32_t mid_y_q4 =
            (layout->brow_in[side].y_q4 * 2 +
             layout->brow_out[side].y_q4) / 3;
        fea_stroke_q4(
            canvas, mid_x_q4, mid_y_q4,
            layout->brow_out[side].x_q4,
            layout->brow_out[side].y_q4,
            thickness_q4 * 2 / 3, color, alpha);
    }
}

void fea_favourite_draw_stars(
    fea_canvas_t *canvas,
    uint32_t seed,
    uint16_t color,
    uint32_t count,
    uint32_t max_alpha)
{
    for (uint32_t star = 0U; star < count; ++star) {
        const uint32_t hash = fea_hash2(star, seed);
        const int32_t x = 7 + (int32_t)(hash % 146U);
        const int32_t y = 7 + (int32_t)((hash >> 9) % 106U);
        const uint32_t alpha = 4U +
            ((hash >> 21) % (max_alpha > 4U ? max_alpha - 3U : 1U));
        fea_ellipse_q4(
            canvas, fv_q4(x), fv_q4(y), 10, 10, color, alpha);
    }
}

void fea_favourite_draw_spark(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t radius_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha)
{
    fea_stroke_q4(
        canvas, x_q4 - radius_q4, y_q4,
        x_q4 + radius_q4, y_q4,
        thickness_q4, color, alpha);
    fea_stroke_q4(
        canvas, x_q4, y_q4 - radius_q4,
        x_q4, y_q4 + radius_q4,
        thickness_q4, color, alpha);
    fea_ellipse_q4(canvas, x_q4, y_q4, 16, 16, color, alpha);
}

void fea_favourite_draw_heart(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    uint16_t color,
    uint32_t alpha)
{
    const int32_t lobe_q4 = size_q4 * 5 / 9;
    fea_ellipse_q4(
        canvas, x_q4 - lobe_q4 / 2, y_q4 - lobe_q4 / 3,
        lobe_q4, lobe_q4, color, alpha);
    fea_ellipse_q4(
        canvas, x_q4 + lobe_q4 / 2, y_q4 - lobe_q4 / 3,
        lobe_q4, lobe_q4, color, alpha);
    fea_triangle_q4(
        canvas, x_q4 - size_q4, y_q4,
        x_q4 + size_q4, y_q4,
        x_q4, y_q4 + size_q4 + lobe_q4 / 2,
        color, alpha);
}

void fea_favourite_draw_drop(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    uint16_t color,
    uint32_t alpha)
{
    fea_triangle_q4(
        canvas, x_q4, y_q4 - size_q4,
        x_q4 - size_q4 * 2 / 3, y_q4,
        x_q4 + size_q4 * 2 / 3, y_q4,
        color, alpha);
    fea_ellipse_q4(
        canvas, x_q4, y_q4 + size_q4 / 4,
        size_q4 * 2 / 3, size_q4 * 3 / 4,
        color, alpha);
}

void fea_favourite_draw_z(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha)
{
    fea_stroke_q4(
        canvas, x_q4 - size_q4, y_q4 - size_q4,
        x_q4 + size_q4, y_q4 - size_q4,
        thickness_q4, color, alpha);
    fea_stroke_q4(
        canvas, x_q4 + size_q4, y_q4 - size_q4,
        x_q4 - size_q4, y_q4 + size_q4,
        thickness_q4, color, alpha);
    fea_stroke_q4(
        canvas, x_q4 - size_q4, y_q4 + size_q4,
        x_q4 + size_q4, y_q4 + size_q4,
        thickness_q4, color, alpha);
}

void fea_favourite_draw_question(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    int32_t size_q4,
    int32_t thickness_q4,
    uint16_t color,
    uint32_t alpha)
{
    fea_stroke_q4(
        canvas, x_q4 - size_q4, y_q4 - size_q4,
        x_q4, y_q4 - size_q4 * 3 / 2,
        thickness_q4, color, alpha);
    fea_stroke_q4(
        canvas, x_q4, y_q4 - size_q4 * 3 / 2,
        x_q4 + size_q4, y_q4 - size_q4 / 2,
        thickness_q4, color, alpha);
    fea_stroke_q4(
        canvas, x_q4 + size_q4, y_q4 - size_q4 / 2,
        x_q4, y_q4 + size_q4 / 2,
        thickness_q4, color, alpha);
    fea_ellipse_q4(
        canvas, x_q4, y_q4 + size_q4 * 3 / 2,
        thickness_q4 / 2, thickness_q4 / 2,
        color, alpha);
}

uint32_t fea_favourite_act_alpha(
    const fea_pose_t *pose,
    uint32_t maximum)
{
    const int32_t act_q8 = pose->act_q8 > 0 ? pose->act_q8 : 0;
    return (uint32_t)fea_clamp_i32(
        ((int32_t)maximum * act_q8) >> 8,
        0, (int32_t)maximum);
}
