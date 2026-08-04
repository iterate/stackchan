#include "fea_favourite_variants_internal.h"

/*
 * Three descendants of Will-o-Wisp.  They share emissive materials but
 * deliberately do not share a silhouette:
 *   lantern-bloom  — radial flower/bell
 *   comet-rascal   — asymmetric swept projectile
 *   moon-moth      — bilateral winged oracle
 */

static const uint16_t WB_BG = FEA_RGB(9, 13, 29);
static const uint16_t WB_STAR = FEA_RGB(142, 162, 214);
static const uint16_t WB_HALO = FEA_RGB(44, 92, 112);
static const uint16_t WB_PETAL = FEA_RGB(116, 226, 204);
static const uint16_t WB_CORE = FEA_RGB(216, 255, 228);
static const uint16_t WB_INK = FEA_RGB(14, 42, 48);
static const uint16_t WB_EMBER = FEA_RGB(255, 218, 122);
static const uint16_t WB_BLUSH = FEA_RGB(242, 142, 166);

static const uint16_t CR_BG = FEA_RGB(18, 10, 34);
static const uint16_t CR_STAR = FEA_RGB(170, 140, 220);
static const uint16_t CR_TAIL_DARK = FEA_RGB(88, 50, 150);
static const uint16_t CR_TAIL = FEA_RGB(194, 104, 218);
static const uint16_t CR_BODY = FEA_RGB(246, 174, 174);
static const uint16_t CR_CORE = FEA_RGB(255, 238, 202);
static const uint16_t CR_INK = FEA_RGB(56, 24, 70);
static const uint16_t CR_SPARK = FEA_RGB(255, 230, 124);
static const uint16_t CR_TONGUE = FEA_RGB(194, 70, 116);

static const uint16_t MO_BG = FEA_RGB(7, 15, 28);
static const uint16_t MO_STAR = FEA_RGB(106, 136, 188);
static const uint16_t MO_WING = FEA_RGB(64, 102, 148);
static const uint16_t MO_WING_HI = FEA_RGB(128, 164, 198);
static const uint16_t MO_BODY = FEA_RGB(190, 214, 218);
static const uint16_t MO_CORE = FEA_RGB(238, 244, 224);
static const uint16_t MO_INK = FEA_RGB(20, 34, 54);
static const uint16_t MO_GOLD = FEA_RGB(244, 204, 116);
static const uint16_t MO_BLUSH = FEA_RGB(204, 132, 164);

static int32_t wisp_act_px(
    const fea_pose_t *pose, int32_t pixels)
{
    return (pixels * 16 * pose->act_q8) >> 8;
}

static void wisp_draw_blush(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint16_t color)
{
    const uint32_t alpha = (uint32_t)(
        (pose->cheek_q8 * 25) >> 8);
    if (alpha < 2U) {
        return;
    }
    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        fea_ellipse_q4(
            canvas,
            layout->anchor.x_q4 + sign * (25 << 4),
            layout->anchor.y_q4 + (5 << 4),
            6 << 4, 3 << 4, color, alpha);
    }
}

static void lantern_emotion_accessories(
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
            canvas, cx_q4 + (39 << 4), cy_q4 - (22 << 4),
            3 << 4, WB_BLUSH, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (39 << 4), cy_q4 - (25 << 4),
            4 << 4, 22, WB_EMBER, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (41 << 4), cy_q4 - (17 << 4),
            3 << 4, 20, WB_EMBER, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (8 << 4),
            layout->eye[1].y_q4 + (9 << 4),
            3 << 4, FEA_RGB(126, 190, 238), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        for (int32_t ray = -1; ray <= 1; ++ray) {
            fea_stroke_q4(
                canvas, cx_q4 + ray * (16 << 4), cy_q4 - (48 << 4),
                cx_q4 + ray * (20 << 4), cy_q4 - (55 << 4),
                24, WB_EMBER, alpha);
        }
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        for (int32_t dot = 0; dot < 3; ++dot) {
            fea_ellipse_q4(
                canvas, cx_q4 - (39 << 4) - dot * (5 << 4),
                cy_q4 + (17 << 4) + dot * (4 << 4),
                (3 - dot) << 3, (3 - dot) << 3,
                WB_EMBER, alpha);
        }
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (43 << 4), cy_q4 - (25 << 4),
            3 << 4, 24, WB_EMBER, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        fea_stroke_q4(
            canvas, cx_q4 - (18 << 4), cy_q4 - (44 << 4),
            cx_q4 + (18 << 4), cy_q4 - (44 << 4),
            30, WB_EMBER, alpha);
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t sway =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 52000U)) * 20) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (39 << 4), cy_q4 - (22 << 4) + sway,
            3 << 4, 22, WB_STAR, alpha);
        fea_favourite_draw_drop(
            canvas, cx_q4 - (10 << 4), cy_q4 + (47 << 4),
            2 << 4, WB_PETAL, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        for (int32_t mote = 0; mote < 3; ++mote) {
            const uint32_t phase =
                (sample_clock / (600U + (uint32_t)mote * 130U)) & 31U;
            fea_favourite_draw_spark(
                canvas,
                cx_q4 + (mote - 1) * (40 << 4),
                cy_q4 + (28 << 4) - (int32_t)phase * 18,
                2 << 4, 18, WB_EMBER, alpha);
        }
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (41 << 4), cy_q4 - (17 << 4),
            3 << 4, WB_BLUSH, alpha);
        break;
    default:
        break;
    }
}

static void render_lantern_bloom(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_LANTERN_BLOOM, pose, sample_clock, &layout);

    int32_t width_delta_q4 = 0;
    int32_t height_delta_q4 = 0;
    int32_t crown_delta_q4 = 0;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        width_delta_q4 = wisp_act_px(pose, 2);
        crown_delta_q4 = wisp_act_px(pose, 2);
        break;
    case FACE_EXPRESSION_JOY:
        width_delta_q4 = wisp_act_px(pose, 4);
        height_delta_q4 = wisp_act_px(pose, 3);
        crown_delta_q4 = wisp_act_px(pose, 5);
        break;
    case FACE_EXPRESSION_CONCERN:
        width_delta_q4 = -wisp_act_px(pose, 4);
        height_delta_q4 = wisp_act_px(pose, 2);
        break;
    case FACE_EXPRESSION_SURPRISE:
        width_delta_q4 = wisp_act_px(pose, 7);
        height_delta_q4 = wisp_act_px(pose, 5);
        crown_delta_q4 = wisp_act_px(pose, 7);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        width_delta_q4 = -wisp_act_px(pose, 2);
        crown_delta_q4 = wisp_act_px(pose, 3);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        width_delta_q4 = -wisp_act_px(pose, 3);
        crown_delta_q4 = -wisp_act_px(pose, 2);
        break;
    case FACE_EXPRESSION_DETERMINED:
        width_delta_q4 = -wisp_act_px(pose, 6);
        height_delta_q4 = wisp_act_px(pose, 3);
        break;
    case FACE_EXPRESSION_SLEEPY:
        width_delta_q4 = wisp_act_px(pose, 2);
        height_delta_q4 = -wisp_act_px(pose, 6);
        crown_delta_q4 = -wisp_act_px(pose, 5);
        break;
    case FACE_EXPRESSION_EXCITED:
        width_delta_q4 = wisp_act_px(pose, 5);
        height_delta_q4 = wisp_act_px(pose, 6);
        crown_delta_q4 = wisp_act_px(pose, 9);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        width_delta_q4 = -wisp_act_px(pose, 3);
        height_delta_q4 = -wisp_act_px(pose, 2);
        break;
    default:
        break;
    }

    fea_fill(canvas, WB_BG);
    fea_favourite_draw_stars(canvas, 0xB1006U, WB_STAR, 22U, 12U);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4 + (3 << 4);
    const int32_t rx_q4 = fea_clamp_i32(
        ((34 << 4) * pose->scale_x_q8 >> 8) + width_delta_q4,
        27 << 4, 43 << 4);
    const int32_t ry_q4 = fea_clamp_i32(
        ((38 << 4) * pose->scale_y_q8 >> 8) + height_delta_q4,
        30 << 4, 46 << 4);

    fea_glow_q4(
        canvas, cx_q4, cy_q4, rx_q4, rx_q4 + (9 << 4),
        WB_HALO, 12U);

    /* Five-petal flame crown, then the bell-shaped body. */
    const int32_t crown_y_q4 = fea_clamp_i32(
        cy_q4 - ry_q4 + (6 << 4) - crown_delta_q4,
        20 << 4, 43 << 4);
    fea_triangle_q4(
        canvas, cx_q4 - (12 << 4), crown_y_q4 + (11 << 4),
        cx_q4 - (6 << 4), crown_y_q4 - (8 << 4),
        cx_q4, crown_y_q4 + (10 << 4), WB_PETAL, 32U);
    fea_triangle_q4(
        canvas, cx_q4 - (5 << 4), crown_y_q4 + (10 << 4),
        cx_q4, crown_y_q4 - (13 << 4),
        cx_q4 + (6 << 4), crown_y_q4 + (10 << 4), WB_CORE, 32U);
    fea_triangle_q4(
        canvas, cx_q4, crown_y_q4 + (10 << 4),
        cx_q4 + (9 << 4), crown_y_q4 - (7 << 4),
        cx_q4 + (14 << 4), crown_y_q4 + (12 << 4), WB_PETAL, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, rx_q4, ry_q4, WB_PETAL, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4 + (3 << 4),
        rx_q4 * 3 / 4, ry_q4 * 4 / 5, WB_CORE, 19U);

    /* Scalloped lantern skirt is readable even at 40x30. */
    for (int32_t lobe = -1; lobe <= 1; ++lobe) {
        fea_ellipse_q4(
            canvas, cx_q4 + lobe * (15 << 4),
            cy_q4 + ry_q4 - (5 << 4),
            12 << 4, 6 << 4, WB_PETAL, 32U);
    }

    fea_favourite_set_mouth_colors(
        &layout, WB_INK, WB_INK, WB_CORE, FEA_RGB(76, 154, 132), 0);
    wisp_draw_blush(canvas, pose, &layout, WB_BLUSH);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_EMBER, WB_HALO, WB_CORE, WB_EMBER,
        WB_INK, WB_CORE, WB_INK, WB_PETAL, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, WB_INK, 34, 27U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    lantern_emotion_accessories(
        canvas, pose, &layout, sample_clock);
}

static int32_t comet_tail_bend_q4(const fea_pose_t *pose)
{
    int32_t bend_px = 0;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
    case FACE_EXPRESSION_JOY:
        bend_px = -8;
        break;
    case FACE_EXPRESSION_CONCERN:
    case FACE_EXPRESSION_EMBARRASSED:
        bend_px = 9;
        break;
    case FACE_EXPRESSION_SURPRISE:
        bend_px = -2;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        bend_px = -11;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        bend_px = 5;
        break;
    case FACE_EXPRESSION_DETERMINED:
        bend_px = 0;
        break;
    case FACE_EXPRESSION_SLEEPY:
        bend_px = 12;
        break;
    case FACE_EXPRESSION_EXCITED:
        bend_px = -13;
        break;
    default:
        break;
    }
    return wisp_act_px(pose, bend_px);
}

static void comet_emotion_accessories(
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
            canvas, cx_q4 - (38 << 4), cy_q4 - (26 << 4),
            3 << 4, CR_SPARK, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (38 << 4), cy_q4 - (31 << 4),
            4 << 4, 22, CR_SPARK, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[0].x_q4 - (8 << 4),
            layout->eye[0].y_q4 + (9 << 4),
            3 << 4, FEA_RGB(112, 174, 236), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (41 << 4), cy_q4 - (7 << 4),
            5 << 4, 22, CR_SPARK, alpha);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 - (40 << 4), cy_q4 - (22 << 4),
            3 << 4, 24, CR_SPARK, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 - (44 << 4), cy_q4 - (26 << 4),
            cx_q4 - (33 << 4), cy_q4 - (29 << 4),
            28, CR_SPARK, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        for (int32_t line = 0; line < 2; ++line) {
            fea_stroke_q4(
                canvas, cx_q4 - (46 << 4),
                cy_q4 + (line * 7 - 4) * 16,
                cx_q4 - (34 << 4),
                cy_q4 + (line * 4 - 3) * 16,
                24, CR_SPARK, alpha);
        }
        break;
    case FACE_EXPRESSION_SLEEPY:
        fea_favourite_draw_z(
            canvas, cx_q4 - (39 << 4), cy_q4 - (24 << 4),
            3 << 4, 22, CR_STAR, alpha);
        break;
    case FACE_EXPRESSION_EXCITED: {
        const int32_t pulse =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 7600U)) * 24) >> 14;
        fea_favourite_draw_spark(
            canvas, cx_q4 - (41 << 4), cy_q4 - (29 << 4) + pulse,
            4 << 4, 22, CR_SPARK, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 - (37 << 4), cy_q4 + (31 << 4) - pulse,
            3 << 4, 20, CR_SPARK, alpha);
        break;
    }
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (39 << 4), cy_q4 + (23 << 4),
            3 << 4, FEA_RGB(248, 128, 164), alpha);
        break;
    default:
        break;
    }
}

static void render_comet_rascal(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_COMET_RASCAL, pose, sample_clock, &layout);

    fea_fill(canvas, CR_BG);
    fea_favourite_draw_stars(canvas, 0xC0AE7U, CR_STAR, 18U, 11U);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    const int32_t bend_q4 = comet_tail_bend_q4(pose);
    int32_t fan_q4 = 21 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE ||
        pose->emotion == FACE_EXPRESSION_EXCITED) {
        fan_q4 += wisp_act_px(pose, 7);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        fan_q4 -= wisp_act_px(pose, 7);
    }
    fan_q4 = fea_clamp_i32(fan_q4, 13 << 4, 29 << 4);

    const int32_t tip_x_q4 = fea_clamp_i32(
        cx_q4 + (78 << 4), 0, 151 << 4);
    const int32_t tip_y_q4 = fea_clamp_i32(
        cy_q4 + bend_q4, 14 << 4, 106 << 4);
    fea_triangle_q4(
        canvas, cx_q4 + (21 << 4), cy_q4 - fan_q4,
        cx_q4 + (24 << 4), cy_q4 + fan_q4,
        tip_x_q4, tip_y_q4, CR_TAIL_DARK, 26U);
    fea_triangle_q4(
        canvas, cx_q4 + (24 << 4), cy_q4 - fan_q4 * 2 / 3,
        cx_q4 + (27 << 4), cy_q4 + fan_q4 * 2 / 3,
        tip_x_q4 - (5 << 4), tip_y_q4, CR_TAIL, 30U);
    fea_stroke_q4(
        canvas, cx_q4 + (25 << 4), cy_q4,
        tip_x_q4 - (6 << 4), tip_y_q4,
        34, CR_SPARK, 20U);
    if (pose->emotion == FACE_EXPRESSION_EXCITED &&
        pose->act_q8 > 40) {
        fea_stroke_q4(
            canvas, cx_q4 + (26 << 4), cy_q4 - (8 << 4),
            tip_x_q4 - (11 << 4), tip_y_q4 - (8 << 4),
            22, CR_SPARK,
            fea_favourite_act_alpha(pose, 25U));
        fea_stroke_q4(
            canvas, cx_q4 + (26 << 4), cy_q4 + (8 << 4),
            tip_x_q4 - (12 << 4), tip_y_q4 + (8 << 4),
            22, CR_SPARK,
            fea_favourite_act_alpha(pose, 22U));
    }

    int32_t rx_q4 = 36 << 4;
    int32_t ry_q4 = 34 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        rx_q4 += wisp_act_px(pose, 5);
        ry_q4 += wisp_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        rx_q4 -= wisp_act_px(pose, 5);
        ry_q4 -= wisp_act_px(pose, 2);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        ry_q4 -= wisp_act_px(pose, 6);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        ry_q4 += wisp_act_px(pose, 5);
    }
    rx_q4 = fea_clamp_i32(rx_q4, 29 << 4, 42 << 4);
    ry_q4 = fea_clamp_i32(ry_q4, 27 << 4, 42 << 4);
    fea_glow_q4(
        canvas, cx_q4, cy_q4, rx_q4, rx_q4 + (8 << 4),
        CR_TAIL_DARK, 11U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, rx_q4, ry_q4, CR_BODY, 32U);
    fea_ellipse_q4(
        canvas, cx_q4 - (8 << 4), cy_q4 - (7 << 4),
        rx_q4 * 2 / 3, ry_q4 * 2 / 3, CR_CORE, 18U);
    /* A pointed leading cap makes the head read as a comet, not a ball. */
    fea_triangle_q4(
        canvas, cx_q4 - rx_q4 - (5 << 4), cy_q4,
        cx_q4 - rx_q4 / 2, cy_q4 - ry_q4 * 3 / 4,
        cx_q4 - rx_q4 / 2, cy_q4 + ry_q4 * 3 / 4,
        CR_BODY, 32U);

    fea_favourite_set_mouth_colors(
        &layout, CR_INK, CR_INK, CR_CORE, CR_TONGUE, 2);
    wisp_draw_blush(
        canvas, pose, &layout, FEA_RGB(244, 118, 150));
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_EMBER, CR_TAIL, CR_CORE, CR_SPARK,
        CR_INK, CR_CORE, CR_INK, CR_BODY, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, CR_INK, 38, 30U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    comet_emotion_accessories(
        canvas, pose, &layout, sample_clock);
}

static int32_t moth_fold_q4(const fea_pose_t *pose)
{
    int32_t fold_px = 0;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fold_px = -3;
        break;
    case FACE_EXPRESSION_JOY:
        fold_px = -7;
        break;
    case FACE_EXPRESSION_CONCERN:
        fold_px = 7;
        break;
    case FACE_EXPRESSION_SURPRISE:
        fold_px = -12;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fold_px = 3;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fold_px = 5;
        break;
    case FACE_EXPRESSION_DETERMINED:
        fold_px = 9;
        break;
    case FACE_EXPRESSION_SLEEPY:
        fold_px = 13;
        break;
    case FACE_EXPRESSION_EXCITED:
        fold_px = -14;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fold_px = 8;
        break;
    default:
        break;
    }
    return wisp_act_px(pose, fold_px);
}

static void moth_emotion_accessories(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 29U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (42 << 4), cy_q4 + (18 << 4),
            3 << 4, MO_BLUSH, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (46 << 4), cy_q4 - (27 << 4),
            4 << 4, 20, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (8 << 4),
            layout->eye[1].y_q4 + (8 << 4),
            3 << 4, MO_WING_HI, alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        fea_ring_q4(
            canvas, cx_q4, cy_q4 - (46 << 4),
            7 << 4, 24, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (44 << 4), cy_q4 - (20 << 4),
            3 << 4, 22, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (38 << 4), cy_q4 - (25 << 4),
            cx_q4 + (49 << 4), cy_q4 - (28 << 4),
            26, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        fea_stroke_q4(
            canvas, cx_q4 - (16 << 4), cy_q4 - (42 << 4),
            cx_q4 + (16 << 4), cy_q4 - (42 << 4),
            30, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t float_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 60000U)) * 20) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (42 << 4),
            cy_q4 - (18 << 4) + float_q4,
            3 << 4, 22, MO_WING_HI, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (47 << 4), cy_q4 - (30 << 4),
            4 << 4, 22, MO_GOLD, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (47 << 4), cy_q4 - (27 << 4),
            4 << 4, 22, MO_GOLD, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (43 << 4), cy_q4 + (18 << 4),
            3 << 4, MO_BLUSH, alpha);
        break;
    default:
        break;
    }
}

static void render_moon_moth_oracle(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_MOON_MOTH_ORACLE, pose, sample_clock, &layout);
    fea_fill(canvas, MO_BG);
    fea_favourite_draw_stars(canvas, 0xA0074U, MO_STAR, 24U, 11U);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    const int32_t fold_q4 = moth_fold_q4(pose);
    const int32_t wing_rx_q4 = fea_clamp_i32(
        (28 << 4) - fold_q4 / 2, 20 << 4, 32 << 4);
    const int32_t wing_ry_q4 = fea_clamp_i32(
        (31 << 4) - fold_q4, 18 << 4, 43 << 4);
    const int32_t wing_y_q4 = cy_q4 + fold_q4 / 2;

    /* Crescent wings: outer oval plus a background cut, then veins. */
    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        const int32_t wing_x_q4 = cx_q4 + sign * (34 << 4);
        fea_ellipse_q4(
            canvas, wing_x_q4, wing_y_q4,
            wing_rx_q4, wing_ry_q4, MO_WING, 30U);
        fea_ellipse_q4(
            canvas, wing_x_q4 + sign * (10 << 4),
            wing_y_q4 - (2 << 4),
            wing_rx_q4 * 2 / 3, wing_ry_q4 * 3 / 4,
            MO_BG, 30U);
        fea_stroke_q4(
            canvas, cx_q4 + sign * (18 << 4), cy_q4 - (8 << 4),
            wing_x_q4 + sign * (wing_rx_q4 * 2 / 3),
            wing_y_q4 - wing_ry_q4 / 3,
            24, MO_WING_HI, 22U);
        fea_stroke_q4(
            canvas, cx_q4 + sign * (20 << 4), cy_q4 + (5 << 4),
            wing_x_q4 + sign * (wing_rx_q4 / 2),
            wing_y_q4 + wing_ry_q4 / 2,
            20, MO_WING_HI, 18U);
    }

    int32_t body_rx_q4 = 29 << 4;
    int32_t body_ry_q4 = 39 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        body_rx_q4 += wisp_act_px(pose, 4);
        body_ry_q4 += wisp_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        body_rx_q4 -= wisp_act_px(pose, 4);
        body_ry_q4 += wisp_act_px(pose, 2);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        body_ry_q4 -= wisp_act_px(pose, 5);
    }
    body_rx_q4 = fea_clamp_i32(body_rx_q4, 24 << 4, 35 << 4);
    body_ry_q4 = fea_clamp_i32(body_ry_q4, 32 << 4, 45 << 4);
    fea_glow_q4(
        canvas, cx_q4, cy_q4, body_rx_q4,
        body_rx_q4 + (7 << 4), MO_WING, 10U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4, body_rx_q4, body_ry_q4,
        MO_BODY, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4 + (4 << 4),
        body_rx_q4 * 2 / 3, body_ry_q4 * 4 / 5,
        MO_CORE, 18U);

    /* Crescent crown/antennae and a hanging oracle drop. */
    const int32_t crown_y_q4 = cy_q4 - body_ry_q4 + (5 << 4);
    fea_stroke_q4(
        canvas, cx_q4 - (4 << 4), crown_y_q4 + (3 << 4),
        cx_q4 - (13 << 4), crown_y_q4 - (8 << 4),
        24, MO_GOLD, 28U);
    fea_stroke_q4(
        canvas, cx_q4 + (4 << 4), crown_y_q4 + (3 << 4),
        cx_q4 + (13 << 4), crown_y_q4 - (8 << 4),
        24, MO_GOLD, 28U);
    fea_ellipse_q4(
        canvas, cx_q4 - (14 << 4), crown_y_q4 - (9 << 4),
        2 << 4, 2 << 4, MO_GOLD, 30U);
    fea_ellipse_q4(
        canvas, cx_q4 + (14 << 4), crown_y_q4 - (9 << 4),
        2 << 4, 2 << 4, MO_GOLD, 30U);
    const int32_t drop_y_q4 = fea_clamp_i32(
        cy_q4 + body_ry_q4 + (4 << 4), 0, 111 << 4);
    fea_favourite_draw_drop(
        canvas, cx_q4, drop_y_q4, 3 << 4, MO_GOLD, 28U);

    fea_favourite_set_mouth_colors(
        &layout, MO_INK, MO_INK, MO_CORE, MO_BLUSH, 0);
    wisp_draw_blush(canvas, pose, &layout, MO_BLUSH);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_EMBER, MO_WING_HI, MO_CORE, MO_GOLD,
        MO_INK, MO_CORE, MO_INK, MO_BODY, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, MO_INK, 34, 28U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    moth_emotion_accessories(
        canvas, pose, &layout, sample_clock);
}

void fea_favourite_wisp_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    switch (profile) {
    case FEA_FAVOURITE_LANTERN_BLOOM:
        render_lantern_bloom(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_COMET_RASCAL:
        render_comet_rascal(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_MOON_MOTH_ORACLE:
        render_moon_moth_oracle(pose, sample_clock, canvas);
        break;
    default:
        fea_fill(canvas, WB_BG);
        break;
    }
}
