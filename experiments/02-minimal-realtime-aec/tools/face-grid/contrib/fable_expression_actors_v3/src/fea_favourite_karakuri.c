#include "fea_favourite_variants_internal.h"

/*
 * Three descendants of Karakuri Brass:
 *   gilded-noh          — ceremonial layered mask and lacquer jaw
 *   beetle-automaton    — domed carapace, antenna brows and mandibles
 *   kintsugi-marionette — porcelain plates, gold repairs and strings
 */

static const uint16_t GN_BG = FEA_RGB(34, 15, 20);
static const uint16_t GN_BG_EDGE = FEA_RGB(18, 8, 12);
static const uint16_t GN_GOLD = FEA_RGB(210, 162, 72);
static const uint16_t GN_GOLD_HI = FEA_RGB(250, 218, 142);
static const uint16_t GN_IVORY = FEA_RGB(232, 220, 190);
static const uint16_t GN_IVORY_SHADE = FEA_RGB(190, 168, 132);
static const uint16_t GN_INK = FEA_RGB(35, 24, 24);
static const uint16_t GN_LACQUER = FEA_RGB(154, 38, 46);
static const uint16_t GN_SENSOR = FEA_RGB(250, 188, 68);
static const uint16_t GN_BLUSH = FEA_RGB(190, 76, 82);

static const uint16_t BA_BG = FEA_RGB(8, 24, 23);
static const uint16_t BA_EDGE = FEA_RGB(5, 14, 14);
static const uint16_t BA_SHELL = FEA_RGB(78, 128, 88);
static const uint16_t BA_SHELL_HI = FEA_RGB(142, 174, 102);
static const uint16_t BA_BRASS = FEA_RGB(190, 146, 74);
static const uint16_t BA_DARK = FEA_RGB(22, 38, 30);
static const uint16_t BA_LENS = FEA_RGB(8, 23, 22);
static const uint16_t BA_SENSOR = FEA_RGB(122, 242, 156);
static const uint16_t BA_SENSOR_HI = FEA_RGB(222, 255, 202);
static const uint16_t BA_ALARM = FEA_RGB(244, 104, 74);

static const uint16_t KM_BG = FEA_RGB(21, 25, 48);
static const uint16_t KM_CURTAIN = FEA_RGB(37, 40, 70);
static const uint16_t KM_PORCELAIN = FEA_RGB(235, 226, 206);
static const uint16_t KM_SHADE = FEA_RGB(192, 192, 190);
static const uint16_t KM_GOLD = FEA_RGB(228, 180, 82);
static const uint16_t KM_INK = FEA_RGB(36, 30, 42);
static const uint16_t KM_WINE = FEA_RGB(132, 42, 72);
static const uint16_t KM_EYE = FEA_RGB(86, 120, 138);
static const uint16_t KM_BLUSH = FEA_RGB(210, 116, 132);

static int32_t mech_act_px(
    const fea_pose_t *pose, int32_t pixels)
{
    return (pixels * 16 * pose->act_q8) >> 8;
}

static void mech_draw_vignette(
    fea_canvas_t *canvas, uint16_t edge)
{
    fea_fill_rect(canvas, 0, 0, FEA_FRAME_WIDTH, 6, edge, 32U);
    fea_fill_rect(
        canvas, 0, FEA_FRAME_HEIGHT - 6,
        FEA_FRAME_WIDTH, FEA_FRAME_HEIGHT, edge, 32U);
    fea_fill_rect(canvas, 0, 0, 7, FEA_FRAME_HEIGHT, edge, 32U);
    fea_fill_rect(
        canvas, FEA_FRAME_WIDTH - 7, 0,
        FEA_FRAME_WIDTH, FEA_FRAME_HEIGHT, edge, 32U);
}

static void mech_draw_rivet(
    fea_canvas_t *canvas,
    int32_t x_q4,
    int32_t y_q4,
    uint16_t outer,
    uint16_t inner)
{
    fea_ellipse_q4(canvas, x_q4, y_q4, 30, 30, outer, 32U);
    fea_ellipse_q4(canvas, x_q4, y_q4, 14, 14, inner, 28U);
}

static void gilded_noh_accessories(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 30U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (47 << 4), cy_q4 + (20 << 4),
            3 << 4, GN_LACQUER, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (49 << 4), cy_q4 - (29 << 4),
            4 << 4, 22, GN_GOLD_HI, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (49 << 4), cy_q4 - (29 << 4),
            4 << 4, 22, GN_GOLD_HI, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (10 << 4),
            layout->eye[1].y_q4 + (10 << 4),
            3 << 4, FEA_RGB(116, 166, 214), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        for (int32_t ray = -2; ray <= 2; ++ray) {
            fea_stroke_q4(
                canvas, cx_q4 + ray * (10 << 4),
                cy_q4 - (48 << 4),
                cx_q4 + ray * (13 << 4),
                cy_q4 - (55 << 4),
                24, GN_GOLD_HI, alpha);
        }
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (48 << 4), cy_q4 - (20 << 4),
            3 << 4, 24, GN_GOLD_HI, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (39 << 4), cy_q4 - (31 << 4),
            cx_q4 + (51 << 4), cy_q4 - (34 << 4),
            28, GN_LACQUER, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        fea_triangle_q4(
            canvas, cx_q4 - (14 << 4), cy_q4 - (48 << 4),
            cx_q4 + (14 << 4), cy_q4 - (48 << 4),
            cx_q4, cy_q4 - (57 << 4),
            GN_LACQUER, alpha);
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t sway_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 61000U)) * 18) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (47 << 4),
            cy_q4 - (22 << 4) + sway_q4,
            3 << 4, 22, GN_GOLD_HI, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (50 << 4), cy_q4 - (31 << 4),
            5 << 4, 24, GN_SENSOR, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (50 << 4), cy_q4 - (31 << 4),
            5 << 4, 24, GN_SENSOR, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (47 << 4), cy_q4 + (20 << 4),
            3 << 4, GN_LACQUER, alpha);
        break;
    default:
        break;
    }
}

static void render_gilded_noh(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_GILDED_NOH, pose, sample_clock, &layout);
    fea_fill(canvas, GN_BG);
    mech_draw_vignette(canvas, GN_BG_EDGE);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t mask_rx_q4 = 43 << 4;
    int32_t mask_ry_q4 = 49 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        mask_rx_q4 += mech_act_px(pose, 4);
        mask_ry_q4 += mech_act_px(pose, 3);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        mask_rx_q4 -= mech_act_px(pose, 4);
        mask_ry_q4 += mech_act_px(pose, 2);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        mask_ry_q4 -= mech_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        mask_rx_q4 += mech_act_px(pose, 3);
        mask_ry_q4 += mech_act_px(pose, 4);
    }
    mask_rx_q4 = fea_clamp_i32(mask_rx_q4, 36 << 4, 49 << 4);
    mask_ry_q4 = fea_clamp_i32(mask_ry_q4, 42 << 4, 53 << 4);

    /* Gold backplate, ivory mask, painted chin and cheek fans. */
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, mask_rx_q4 + 42,
        mask_ry_q4 + 42, GN_GOLD, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, mask_rx_q4,
        mask_ry_q4, GN_IVORY, 32U);
    fea_triangle_q4(
        canvas, cx_q4 - mask_rx_q4, cy_q4 - (10 << 4),
        cx_q4 - mask_rx_q4 - (8 << 4), cy_q4 + (15 << 4),
        cx_q4 - (27 << 4), cy_q4 + (23 << 4),
        GN_GOLD, 30U);
    fea_triangle_q4(
        canvas, cx_q4 + mask_rx_q4, cy_q4 - (10 << 4),
        cx_q4 + mask_rx_q4 + (8 << 4), cy_q4 + (15 << 4),
        cx_q4 + (27 << 4), cy_q4 + (23 << 4),
        GN_GOLD, 30U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4 + (27 << 4),
        27 << 4, 17 << 4, GN_IVORY_SHADE, 11U);

    int32_t crest_lift_q4 = 0;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE ||
        pose->emotion == FACE_EXPRESSION_EXCITED) {
        crest_lift_q4 = -mech_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY ||
               pose->emotion == FACE_EXPRESSION_DETERMINED) {
        crest_lift_q4 = mech_act_px(pose, 4);
    }
    const int32_t crest_y_q4 = cy_q4 - (45 << 4) + crest_lift_q4;
    fea_triangle_q4(
        canvas, cx_q4 - (25 << 4), crest_y_q4 + (8 << 4),
        cx_q4, crest_y_q4 - (5 << 4),
        cx_q4 + (25 << 4), crest_y_q4 + (8 << 4),
        GN_GOLD, 32U);
    fea_triangle_q4(
        canvas, cx_q4 - (15 << 4), crest_y_q4 + (5 << 4),
        cx_q4, crest_y_q4,
        cx_q4 + (15 << 4), crest_y_q4 + (5 << 4),
        GN_LACQUER, 27U);

    /* Nose bridge and plate seams sell the mask construction. */
    fea_stroke_q4(
        canvas, cx_q4, cy_q4 - (17 << 4),
        cx_q4 - (3 << 4), cy_q4 + (9 << 4),
        24, GN_GOLD, 25U);
    fea_stroke_q4(
        canvas, cx_q4 - (3 << 4), cy_q4 + (9 << 4),
        cx_q4 + (5 << 4), cy_q4 + (12 << 4),
        20, GN_GOLD, 22U);
    mech_draw_rivet(
        canvas, cx_q4 - (35 << 4), cy_q4 + (20 << 4),
        GN_GOLD_HI, GN_LACQUER);
    mech_draw_rivet(
        canvas, cx_q4 + (35 << 4), cy_q4 + (20 << 4),
        GN_GOLD_HI, GN_LACQUER);

    fea_favourite_set_mouth_colors(
        &layout, GN_GOLD, GN_INK,
        GN_IVORY, GN_LACQUER, 4);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_LENS, GN_GOLD,
        GN_INK, GN_SENSOR, GN_INK,
        GN_GOLD_HI, GN_LACQUER, GN_IVORY, 1U, 0U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);

    /* Fan brows have a second slat rather than reading as drawn-on ink. */
    fea_favourite_draw_brows(canvas, &layout, GN_LACQUER, 42, 32U);
    for (int32_t side = 0; side < 2; ++side) {
        fea_stroke_q4(
            canvas, layout.brow_in[side].x_q4,
            layout.brow_in[side].y_q4 - 24,
            layout.brow_out[side].x_q4,
            layout.brow_out[side].y_q4 - 24,
            18, GN_GOLD_HI, 23U);
    }
    fea_lipmouth_draw(canvas, &layout.mouth);

    const uint32_t blush_alpha =
        (uint32_t)((pose->cheek_q8 * 22) >> 8);
    if (blush_alpha > 2U) {
        fea_ellipse_q4(
            canvas, cx_q4 - (33 << 4), cy_q4 + (10 << 4),
            7 << 4, 3 << 4, GN_BLUSH, blush_alpha);
        fea_ellipse_q4(
            canvas, cx_q4 + (33 << 4), cy_q4 + (10 << 4),
            7 << 4, 3 << 4, GN_BLUSH, blush_alpha);
    }
    gilded_noh_accessories(
        canvas, pose, &layout, sample_clock);
}

static int32_t beetle_antenna_bias_q4(const fea_pose_t *pose)
{
    int32_t pixels = 0;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        pixels = -3;
        break;
    case FACE_EXPRESSION_JOY:
        pixels = -8;
        break;
    case FACE_EXPRESSION_CONCERN:
        pixels = 5;
        break;
    case FACE_EXPRESSION_SURPRISE:
        pixels = -12;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        pixels = -5;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        pixels = 3;
        break;
    case FACE_EXPRESSION_DETERMINED:
        pixels = 8;
        break;
    case FACE_EXPRESSION_SLEEPY:
        pixels = 12;
        break;
    case FACE_EXPRESSION_EXCITED:
        pixels = -14;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        pixels = 6;
        break;
    default:
        break;
    }
    return mech_act_px(pose, pixels);
}

static void beetle_accessories(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 30U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (51 << 4), cy_q4 + (16 << 4),
            3 << 4, BA_ALARM, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (53 << 4), cy_q4 - (23 << 4),
            4 << 4, 22, BA_SENSOR_HI, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (11 << 4),
            layout->eye[1].y_q4 + (11 << 4),
            3 << 4, FEA_RGB(104, 174, 210), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        fea_ring_q4(
            canvas, cx_q4, cy_q4 - (48 << 4),
            6 << 4, 22, BA_SENSOR_HI, alpha);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (52 << 4), cy_q4 - (15 << 4),
            3 << 4, 22, BA_SENSOR_HI, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (43 << 4), cy_q4 - (25 << 4),
            cx_q4 + (54 << 4), cy_q4 - (28 << 4),
            26, BA_ALARM, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        for (int32_t line = 0; line < 3; ++line) {
            fea_stroke_q4(
                canvas, cx_q4 - (54 << 4),
                cy_q4 + (line * 6 - 6) * 16,
                cx_q4 - (43 << 4),
                cy_q4 + (line * 3 - 3) * 16,
                22, BA_ALARM, alpha);
        }
        break;
    case FACE_EXPRESSION_SLEEPY:
        fea_favourite_draw_z(
            canvas, cx_q4 + (50 << 4), cy_q4 - (18 << 4),
            3 << 4, 22, BA_SHELL_HI, alpha);
        break;
    case FACE_EXPRESSION_EXCITED: {
        const int32_t pulse_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 6900U)) * 26) >> 14;
        fea_favourite_draw_spark(
            canvas, cx_q4 - (52 << 4),
            cy_q4 - (27 << 4) + pulse_q4,
            4 << 4, 24, BA_SENSOR_HI, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (52 << 4),
            cy_q4 - (27 << 4) - pulse_q4,
            4 << 4, 24, BA_SENSOR_HI, alpha);
        break;
    }
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (51 << 4), cy_q4 + (17 << 4),
            3 << 4, BA_ALARM, alpha);
        break;
    default:
        break;
    }
}

static void render_beetle_automaton(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_BEETLE_AUTOMATON,
        pose, sample_clock, &layout);
    fea_fill(canvas, BA_BG);
    mech_draw_vignette(canvas, BA_EDGE);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t shell_rx_q4 = 49 << 4;
    int32_t shell_ry_q4 = 43 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        shell_rx_q4 += mech_act_px(pose, 5);
        shell_ry_q4 += mech_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        shell_rx_q4 += mech_act_px(pose, 3);
        shell_ry_q4 -= mech_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        shell_ry_q4 -= mech_act_px(pose, 7);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        shell_rx_q4 += mech_act_px(pose, 5);
        shell_ry_q4 += mech_act_px(pose, 3);
    }
    shell_rx_q4 = fea_clamp_i32(shell_rx_q4, 43 << 4, 55 << 4);
    shell_ry_q4 = fea_clamp_i32(shell_ry_q4, 34 << 4, 50 << 4);

    /* Rear elytra and central head shield. */
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, shell_rx_q4 + 36,
        shell_ry_q4 + 36, BA_BRASS, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, shell_rx_q4,
        shell_ry_q4, BA_SHELL, 32U);
    fea_ellipse_q4(
        canvas, cx_q4 - (18 << 4), cy_q4 - (4 << 4),
        27 << 4, shell_ry_q4 * 4 / 5, BA_SHELL_HI, 12U);
    fea_stroke_q4(
        canvas, cx_q4, cy_q4 - shell_ry_q4 + (5 << 4),
        cx_q4, cy_q4 + shell_ry_q4 - (5 << 4),
        34, BA_DARK, 24U);
    fea_roundrect_q4(
        canvas, cx_q4 - (25 << 4), cy_q4 - (33 << 4),
        cx_q4 + (25 << 4), cy_q4 - (13 << 4),
        8 << 4, BA_BRASS, 32U);

    /* Mandible plates parent the mouth corners visually. */
    fea_triangle_q4(
        canvas, layout.mouth.left_x_q4 - (3 << 4),
        layout.mouth.left_y_q4,
        cx_q4 - (42 << 4), cy_q4 + (18 << 4),
        cx_q4 - (29 << 4), cy_q4 + (34 << 4),
        BA_BRASS, 32U);
    fea_triangle_q4(
        canvas, layout.mouth.right_x_q4 + (3 << 4),
        layout.mouth.right_y_q4,
        cx_q4 + (42 << 4), cy_q4 + (18 << 4),
        cx_q4 + (29 << 4), cy_q4 + (34 << 4),
        BA_BRASS, 32U);
    mech_draw_rivet(
        canvas, cx_q4 - (34 << 4), cy_q4 + (25 << 4),
        BA_BRASS, BA_DARK);
    mech_draw_rivet(
        canvas, cx_q4 + (34 << 4), cy_q4 + (25 << 4),
        BA_BRASS, BA_DARK);

    const int32_t antenna_bias_q4 = beetle_antenna_bias_q4(pose);
    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t tip_x_q4 =
            cx_q4 + sign * (35 << 4);
        const int32_t tip_y_q4 = fea_clamp_i32(
            cy_q4 - (45 << 4) + antenna_bias_q4,
            8 << 4, 35 << 4);
        fea_stroke_q4(
            canvas, cx_q4 + sign * (16 << 4),
            cy_q4 - (31 << 4),
            layout.brow_out[side].x_q4,
            layout.brow_out[side].y_q4,
            36, BA_BRASS, 32U);
        fea_stroke_q4(
            canvas, layout.brow_out[side].x_q4,
            layout.brow_out[side].y_q4,
            tip_x_q4, tip_y_q4,
            30, BA_BRASS, 32U);
        fea_ellipse_q4(
            canvas, tip_x_q4, tip_y_q4,
            3 << 4, 3 << 4, BA_SENSOR, 32U);
    }

    fea_favourite_set_mouth_colors(
        &layout, BA_BRASS, BA_DARK,
        BA_SENSOR_HI, BA_ALARM, 0);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_LENS, BA_BRASS,
        BA_LENS, BA_SENSOR, BA_DARK,
        BA_SENSOR_HI, BA_BRASS, BA_SHELL, 1U, 0U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    /* Short brow guards remain independent from the antenna tips. */
    fea_favourite_draw_brows(canvas, &layout, BA_DARK, 30, 24U);
    fea_lipmouth_draw(canvas, &layout.mouth);

    const uint32_t cheek_alpha =
        (uint32_t)((pose->cheek_q8 * 24) >> 8);
    if (cheek_alpha > 2U) {
        fea_glow_q4(
            canvas, cx_q4 - (37 << 4), cy_q4 + (8 << 4),
            2 << 4, 5 << 4, BA_ALARM, cheek_alpha);
        fea_glow_q4(
            canvas, cx_q4 + (37 << 4), cy_q4 + (8 << 4),
            2 << 4, 5 << 4, BA_ALARM, cheek_alpha);
    }
    beetle_accessories(canvas, pose, &layout, sample_clock);
}

static void marionette_accessories(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 30U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (48 << 4), cy_q4 + (17 << 4),
            3 << 4, KM_BLUSH, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (49 << 4), cy_q4 - (27 << 4),
            4 << 4, 22, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (9 << 4),
            layout->eye[1].y_q4 + (10 << 4),
            3 << 4, FEA_RGB(116, 162, 208), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        fea_ring_q4(
            canvas, cx_q4, cy_q4 - (49 << 4),
            6 << 4, 22, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (48 << 4), cy_q4 - (18 << 4),
            3 << 4, 22, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (40 << 4), cy_q4 - (27 << 4),
            cx_q4 + (51 << 4), cy_q4 - (30 << 4),
            26, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        fea_stroke_q4(
            canvas, cx_q4 - (17 << 4), cy_q4 - (44 << 4),
            cx_q4 + (17 << 4), cy_q4 - (44 << 4),
            28, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t sway_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 58000U)) * 18) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (47 << 4),
            cy_q4 - (20 << 4) + sway_q4,
            3 << 4, 22, KM_SHADE, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (49 << 4), cy_q4 - (28 << 4),
            5 << 4, 23, KM_GOLD, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (49 << 4), cy_q4 - (28 << 4),
            5 << 4, 23, KM_GOLD, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (48 << 4), cy_q4 + (18 << 4),
            3 << 4, KM_BLUSH, alpha);
        break;
    default:
        break;
    }
}

static void render_kintsugi_marionette(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_KINTSUGI_MARIONETTE,
        pose, sample_clock, &layout);
    fea_fill(canvas, KM_BG);
    mech_draw_vignette(canvas, KM_CURTAIN);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t face_rx_q4 = 41 << 4;
    int32_t face_ry_q4 = 48 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        face_rx_q4 += mech_act_px(pose, 4);
        face_ry_q4 += mech_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        face_rx_q4 -= mech_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        face_ry_q4 -= mech_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        face_ry_q4 += mech_act_px(pose, 4);
    }
    face_rx_q4 = fea_clamp_i32(face_rx_q4, 35 << 4, 47 << 4);
    face_ry_q4 = fea_clamp_i32(face_ry_q4, 41 << 4, 53 << 4);

    /* Puppet crossbar and taut/slack strings. */
    int32_t slack_q4 = 0;
    if (pose->emotion == FACE_EXPRESSION_SLEEPY ||
        pose->emotion == FACE_EXPRESSION_CONCERN) {
        slack_q4 = mech_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED ||
               pose->emotion == FACE_EXPRESSION_SURPRISE) {
        slack_q4 = -mech_act_px(pose, 3);
    }
    fea_stroke_q4(
        canvas, 49 << 4, 8 << 4, 111 << 4, 8 << 4,
        28, KM_GOLD, 28U);
    fea_stroke_q4(
        canvas, 57 << 4, 8 << 4,
        cx_q4 - (24 << 4), cy_q4 - face_ry_q4 + slack_q4,
        18, KM_GOLD, 22U);
    fea_stroke_q4(
        canvas, 103 << 4, 8 << 4,
        cx_q4 + (24 << 4), cy_q4 - face_ry_q4 + slack_q4,
        18, KM_GOLD, 22U);

    fea_ellipse_q4(
        canvas, cx_q4, cy_q4,
        face_rx_q4 + 34, face_ry_q4 + 34,
        KM_GOLD, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4,
        face_rx_q4, face_ry_q4,
        KM_PORCELAIN, 32U);

    /* Separate cheek plates float on visible pin joints. */
    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t lift_q4 =
            (pose->corner_q8[side] * 28) >> 8;
        const int32_t plate_x_q4 =
            cx_q4 + sign * (34 << 4);
        const int32_t plate_y_q4 =
            cy_q4 + (16 << 4) - lift_q4;
        fea_ellipse_q4(
            canvas, plate_x_q4, plate_y_q4,
            13 << 4, 15 << 4, KM_SHADE, 31U);
        mech_draw_rivet(
            canvas, cx_q4 + sign * (27 << 4),
            cy_q4 + (8 << 4), KM_GOLD, KM_INK);
        fea_stroke_q4(
            canvas, cx_q4 + sign * (28 << 4),
            cy_q4 + (9 << 4),
            plate_x_q4, plate_y_q4,
            20, KM_GOLD, 24U);
    }

    /* Kintsugi repair seams are bold, sparse, and asymmetrical. */
    fea_stroke_q4(
        canvas, cx_q4 - (11 << 4), cy_q4 - face_ry_q4 + (4 << 4),
        cx_q4 - (5 << 4), cy_q4 - (17 << 4),
        26, KM_GOLD, 30U);
    fea_stroke_q4(
        canvas, cx_q4 - (5 << 4), cy_q4 - (17 << 4),
        cx_q4 - (12 << 4), cy_q4 - (3 << 4),
        24, KM_GOLD, 30U);
    fea_stroke_q4(
        canvas, cx_q4 - (12 << 4), cy_q4 - (3 << 4),
        cx_q4 - (5 << 4), cy_q4 + (9 << 4),
        22, KM_GOLD, 28U);
    fea_stroke_q4(
        canvas, cx_q4 + (25 << 4), cy_q4 + (7 << 4),
        cx_q4 + (15 << 4), cy_q4 + (23 << 4),
        22, KM_GOLD, 27U);
    fea_stroke_q4(
        canvas, cx_q4 + (15 << 4), cy_q4 + (23 << 4),
        cx_q4 + (20 << 4), cy_q4 + (39 << 4),
        20, KM_GOLD, 26U);

    fea_favourite_set_mouth_colors(
        &layout, KM_GOLD, KM_INK,
        KM_PORCELAIN, KM_WINE, 4);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_INK, KM_INK,
        FEA_RGB(250, 246, 232), KM_EYE, KM_INK,
        FEA_RGB(255, 255, 244), KM_INK, KM_PORCELAIN, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    /* Gold wire brows visually connect to the puppet construction. */
    fea_favourite_draw_brows(canvas, &layout, KM_GOLD, 34, 30U);
    fea_lipmouth_draw(canvas, &layout.mouth);

    const uint32_t cheek_alpha =
        (uint32_t)((pose->cheek_q8 * 24) >> 8);
    if (cheek_alpha > 2U) {
        fea_ellipse_q4(
            canvas, cx_q4 - (31 << 4), cy_q4 + (10 << 4),
            6 << 4, 3 << 4, KM_BLUSH, cheek_alpha);
        fea_ellipse_q4(
            canvas, cx_q4 + (31 << 4), cy_q4 + (10 << 4),
            6 << 4, 3 << 4, KM_BLUSH, cheek_alpha);
    }
    marionette_accessories(
        canvas, pose, &layout, sample_clock);
}

void fea_favourite_karakuri_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    switch (profile) {
    case FEA_FAVOURITE_GILDED_NOH:
        render_gilded_noh(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_BEETLE_AUTOMATON:
        render_beetle_automaton(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_KINTSUGI_MARIONETTE:
        render_kintsugi_marionette(pose, sample_clock, canvas);
        break;
    default:
        fea_fill(canvas, GN_BG);
        break;
    }
}
