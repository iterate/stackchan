#include "fea_favourite_variants_internal.h"

/*
 * Three descendants of Emote Sticker:
 *   pop-burst            — elastic die-cut starburst
 *   felt-patch-pal       — scalloped textile and button/yarn features
 *   speech-bubble-sprite — comic balloon, active tail and punctuation
 */

static const uint16_t PB_BG = FEA_RGB(242, 235, 222);
static const uint16_t PB_DOT = FEA_RGB(218, 201, 184);
static const uint16_t PB_SHADOW = FEA_RGB(166, 136, 148);
static const uint16_t PB_BORDER = FEA_RGB(255, 252, 244);
static const uint16_t PB_INK = FEA_RGB(47, 28, 52);
static const uint16_t PB_FACE = FEA_RGB(250, 88, 116);
static const uint16_t PB_FACE_HI = FEA_RGB(255, 188, 96);
static const uint16_t PB_SCLERA = FEA_RGB(255, 250, 232);
static const uint16_t PB_IRIS = FEA_RGB(68, 122, 154);
static const uint16_t PB_MOUTH = FEA_RGB(88, 28, 62);
static const uint16_t PB_TONGUE = FEA_RGB(244, 110, 136);
static const uint16_t PB_ACCENT = FEA_RGB(42, 184, 184);

static const uint16_t FP_BG = FEA_RGB(40, 58, 74);
static const uint16_t FP_GRID = FEA_RGB(58, 78, 92);
static const uint16_t FP_SHADOW = FEA_RGB(20, 32, 42);
static const uint16_t FP_EDGE = FEA_RGB(250, 216, 152);
static const uint16_t FP_PATCH = FEA_RGB(104, 176, 154);
static const uint16_t FP_PATCH_HI = FEA_RGB(150, 210, 180);
static const uint16_t FP_THREAD = FEA_RGB(252, 234, 190);
static const uint16_t FP_INK = FEA_RGB(42, 48, 54);
static const uint16_t FP_BUTTON = FEA_RGB(224, 108, 92);
static const uint16_t FP_BUTTON_HI = FEA_RGB(255, 192, 136);
static const uint16_t FP_MOUTH = FEA_RGB(112, 52, 72);
static const uint16_t FP_BLUSH = FEA_RGB(230, 130, 140);

static const uint16_t SB_BG = FEA_RGB(244, 242, 228);
static const uint16_t SB_DOT = FEA_RGB(194, 210, 216);
static const uint16_t SB_SHADOW = FEA_RGB(92, 118, 134);
static const uint16_t SB_BORDER = FEA_RGB(248, 250, 244);
static const uint16_t SB_INK = FEA_RGB(30, 40, 52);
static const uint16_t SB_FACE = FEA_RGB(126, 214, 224);
static const uint16_t SB_FACE_SHADE = FEA_RGB(76, 170, 190);
static const uint16_t SB_SCLERA = FEA_RGB(255, 253, 238);
static const uint16_t SB_IRIS = FEA_RGB(80, 92, 174);
static const uint16_t SB_MOUTH = FEA_RGB(110, 44, 84);
static const uint16_t SB_TONGUE = FEA_RGB(236, 112, 142);
static const uint16_t SB_ACCENT = FEA_RGB(246, 120, 72);

static int32_t sticker_act_px(
    const fea_pose_t *pose, int32_t pixels)
{
    return (pixels * 16 * pose->act_q8) >> 8;
}

static void sticker_draw_halftone(
    fea_canvas_t *canvas,
    uint16_t color,
    uint32_t seed)
{
    for (uint32_t dot = 0U; dot < 28U; ++dot) {
        const uint32_t hash = fea_hash2(dot, seed);
        const int32_t x_q4 =
            ((int32_t)(hash % 38U) * 4 + 4) << 4;
        const int32_t y_q4 =
            ((int32_t)((hash >> 8) % 28U) * 4 + 4) << 4;
        const int32_t radius_q4 =
            (int32_t)(12U + ((hash >> 19) & 15U));
        fea_ellipse_q4(
            canvas, x_q4, y_q4,
            radius_q4, radius_q4, color, 10U);
    }
}

static void sticker_draw_blush(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint16_t color)
{
    const uint32_t alpha =
        (uint32_t)((pose->cheek_q8 * 26) >> 8);
    if (alpha < 2U) {
        return;
    }
    for (int32_t side = 0; side < 2; ++side) {
        const int32_t sign = side == 0 ? -1 : 1;
        fea_ellipse_q4(
            canvas,
            layout->anchor.x_q4 + sign * (34 << 4),
            layout->anchor.y_q4 + (7 << 4),
            8 << 4, 4 << 4, color, alpha);
    }
}

static void pop_burst_accessories(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 31U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (50 << 4), cy_q4 - (19 << 4),
            4 << 4, PB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (49 << 4), cy_q4 - (20 << 4),
            4 << 4, PB_FACE_HI, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (50 << 4), cy_q4 - (25 << 4),
            4 << 4, 24, PB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (11 << 4),
            layout->eye[1].y_q4 + (11 << 4),
            4 << 4, FEA_RGB(82, 164, 226), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        for (int32_t ray = -2; ray <= 2; ++ray) {
            fea_stroke_q4(
                canvas, cx_q4 + ray * (12 << 4),
                cy_q4 - (47 << 4),
                cx_q4 + ray * (16 << 4),
                cy_q4 - (56 << 4),
                26, PB_INK, alpha);
        }
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (50 << 4), cy_q4 - (18 << 4),
            4 << 4, 28, PB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (42 << 4), cy_q4 - (29 << 4),
            cx_q4 + (54 << 4), cy_q4 - (32 << 4),
            30, PB_INK, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        for (int32_t line = 0; line < 3; ++line) {
            fea_stroke_q4(
                canvas, cx_q4 - (55 << 4),
                cy_q4 + (line * 7 - 7) * 16,
                cx_q4 - (42 << 4),
                cy_q4 + (line * 3 - 3) * 16,
                26, PB_ACCENT, alpha);
        }
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t float_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 60000U)) * 20) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (49 << 4),
            cy_q4 - (19 << 4) + float_q4,
            4 << 4, 24, PB_INK, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (52 << 4), cy_q4 - (27 << 4),
            5 << 4, 26, PB_FACE_HI, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (52 << 4), cy_q4 - (27 << 4),
            5 << 4, 26, PB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (50 << 4), cy_q4 + (20 << 4),
            4 << 4, PB_FACE_HI, alpha);
        break;
    default:
        break;
    }
}

static void pop_burst_tips(
    fea_canvas_t *canvas,
    int32_t cx_q4,
    int32_t cy_q4,
    int32_t inner_x_q4,
    int32_t inner_y_q4,
    int32_t point_q4,
    uint16_t color,
    uint32_t alpha)
{
    /* Four cardinal and four diagonal points form a die-cut burst. */
    fea_triangle_q4(
        canvas, cx_q4 - (8 << 4), cy_q4 - inner_y_q4,
        cx_q4 + (8 << 4), cy_q4 - inner_y_q4,
        cx_q4, cy_q4 - inner_y_q4 - point_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 - (8 << 4), cy_q4 + inner_y_q4,
        cx_q4 + (8 << 4), cy_q4 + inner_y_q4,
        cx_q4, cy_q4 + inner_y_q4 + point_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 - inner_x_q4, cy_q4 - (8 << 4),
        cx_q4 - inner_x_q4, cy_q4 + (8 << 4),
        cx_q4 - inner_x_q4 - point_q4, cy_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 + inner_x_q4, cy_q4 - (8 << 4),
        cx_q4 + inner_x_q4, cy_q4 + (8 << 4),
        cx_q4 + inner_x_q4 + point_q4, cy_q4, color, alpha);
    const int32_t diagonal_q4 = point_q4 * 3 / 4;
    fea_triangle_q4(
        canvas, cx_q4 - inner_x_q4 + (2 << 4),
        cy_q4 - inner_y_q4 + (9 << 4),
        cx_q4 - inner_x_q4 + (9 << 4),
        cy_q4 - inner_y_q4 + (2 << 4),
        cx_q4 - inner_x_q4 - diagonal_q4,
        cy_q4 - inner_y_q4 - diagonal_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 + inner_x_q4 - (2 << 4),
        cy_q4 - inner_y_q4 + (9 << 4),
        cx_q4 + inner_x_q4 - (9 << 4),
        cy_q4 - inner_y_q4 + (2 << 4),
        cx_q4 + inner_x_q4 + diagonal_q4,
        cy_q4 - inner_y_q4 - diagonal_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 - inner_x_q4 + (2 << 4),
        cy_q4 + inner_y_q4 - (9 << 4),
        cx_q4 - inner_x_q4 + (9 << 4),
        cy_q4 + inner_y_q4 - (2 << 4),
        cx_q4 - inner_x_q4 - diagonal_q4,
        cy_q4 + inner_y_q4 + diagonal_q4, color, alpha);
    fea_triangle_q4(
        canvas, cx_q4 + inner_x_q4 - (2 << 4),
        cy_q4 + inner_y_q4 - (9 << 4),
        cx_q4 + inner_x_q4 - (9 << 4),
        cy_q4 + inner_y_q4 - (2 << 4),
        cx_q4 + inner_x_q4 + diagonal_q4,
        cy_q4 + inner_y_q4 + diagonal_q4, color, alpha);
}

static void render_pop_burst(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_POP_BURST, pose, sample_clock, &layout);
    fea_fill(canvas, PB_BG);
    sticker_draw_halftone(canvas, PB_DOT, 0xB0B057U);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t rx_q4 = 43 << 4;
    int32_t ry_q4 = 39 << 4;
    int32_t point_q4 = 11 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        rx_q4 += sticker_act_px(pose, 4);
        ry_q4 += sticker_act_px(pose, 5);
        point_q4 += sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        rx_q4 += sticker_act_px(pose, 4);
        ry_q4 -= sticker_act_px(pose, 5);
        point_q4 -= sticker_act_px(pose, 3);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        ry_q4 -= sticker_act_px(pose, 7);
        point_q4 -= sticker_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        point_q4 += sticker_act_px(pose, 8);
        ry_q4 += sticker_act_px(pose, 3);
    } else if (pose->emotion == FACE_EXPRESSION_EMBARRASSED) {
        rx_q4 -= sticker_act_px(pose, 3);
    }
    rx_q4 = fea_clamp_i32(rx_q4, 37 << 4, 49 << 4);
    ry_q4 = fea_clamp_i32(ry_q4, 31 << 4, 47 << 4);
    point_q4 = fea_clamp_i32(point_q4, 7 << 4, 18 << 4);

    pop_burst_tips(
        canvas, cx_q4 + 40, cy_q4 + 44,
        rx_q4 + 22, ry_q4 + 22, point_q4 + 20,
        PB_SHADOW, 22U);
    fea_ellipse_q4(
        canvas, cx_q4 + 40, cy_q4 + 44,
        rx_q4 + 46, ry_q4 + 46, PB_SHADOW, 22U);
    pop_burst_tips(
        canvas, cx_q4, cy_q4,
        rx_q4 + 34, ry_q4 + 34, point_q4 + 34,
        PB_BORDER, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4,
        rx_q4 + 54, ry_q4 + 54, PB_BORDER, 32U);
    pop_burst_tips(
        canvas, cx_q4, cy_q4,
        rx_q4 - 4, ry_q4 - 4, point_q4,
        PB_FACE, 32U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4,
        rx_q4, ry_q4, PB_FACE, 32U);
    fea_ellipse_q4(
        canvas, cx_q4 - (10 << 4), cy_q4 - (12 << 4),
        rx_q4 * 2 / 3, ry_q4 / 2,
        PB_FACE_HI, 12U);

    fea_favourite_set_mouth_colors(
        &layout, PB_INK, PB_MOUTH,
        PB_SCLERA, PB_TONGUE, 4);
    sticker_draw_blush(canvas, pose, &layout, PB_FACE_HI);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_INK, PB_INK,
        PB_SCLERA, PB_IRIS, PB_INK,
        PB_BORDER, PB_INK, PB_FACE, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, PB_INK, 50, 32U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    pop_burst_accessories(
        canvas, pose, &layout, sample_clock);
}

static void felt_patch_accessories(
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
            canvas, cx_q4 + (50 << 4), cy_q4 + (18 << 4),
            4 << 4, FP_BLUSH, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (51 << 4), cy_q4 - (25 << 4),
            4 << 4, 24, FP_THREAD, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_drop(
            canvas, layout->eye[1].x_q4 + (10 << 4),
            layout->eye[1].y_q4 + (10 << 4),
            4 << 4, FEA_RGB(104, 174, 220), alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        for (int32_t ray = -1; ray <= 1; ++ray) {
            fea_stroke_q4(
                canvas, cx_q4 + ray * (15 << 4),
                cy_q4 - (47 << 4),
                cx_q4 + ray * (19 << 4),
                cy_q4 - (55 << 4),
                28, FP_THREAD, alpha);
        }
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (49 << 4), cy_q4 - (18 << 4),
            4 << 4, 26, FP_THREAD, alpha);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_stroke_q4(
            canvas, cx_q4 + (41 << 4), cy_q4 - (28 << 4),
            cx_q4 + (53 << 4), cy_q4 - (31 << 4),
            28, FP_THREAD, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        for (int32_t line = 0; line < 2; ++line) {
            fea_stroke_q4(
                canvas, cx_q4 - (53 << 4),
                cy_q4 + (line * 9 - 5) * 16,
                cx_q4 - (41 << 4),
                cy_q4 + (line * 4 - 2) * 16,
                28, FP_THREAD, alpha);
        }
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t float_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 64000U)) * 20) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (48 << 4),
            cy_q4 - (18 << 4) + float_q4,
            4 << 4, 25, FP_THREAD, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_favourite_draw_spark(
            canvas, cx_q4 - (50 << 4), cy_q4 - (27 << 4),
            5 << 4, 24, FP_BUTTON_HI, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (50 << 4), cy_q4 - (27 << 4),
            5 << 4, 24, FP_THREAD, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (49 << 4), cy_q4 + (18 << 4),
            4 << 4, FP_BLUSH, alpha);
        break;
    default:
        break;
    }
}

static void felt_draw_stitches(
    fea_canvas_t *canvas,
    int32_t left_q4,
    int32_t top_q4,
    int32_t right_q4,
    int32_t bottom_q4)
{
    for (int32_t x_q4 = left_q4 + (8 << 4);
         x_q4 < right_q4 - (5 << 4);
         x_q4 += 13 << 4) {
        fea_stroke_q4(
            canvas, x_q4, top_q4,
            x_q4 + (5 << 4), top_q4,
            22, FP_THREAD, 27U);
        fea_stroke_q4(
            canvas, x_q4, bottom_q4,
            x_q4 + (5 << 4), bottom_q4,
            22, FP_THREAD, 27U);
    }
    for (int32_t y_q4 = top_q4 + (8 << 4);
         y_q4 < bottom_q4 - (5 << 4);
         y_q4 += 13 << 4) {
        fea_stroke_q4(
            canvas, left_q4, y_q4,
            left_q4, y_q4 + (5 << 4),
            22, FP_THREAD, 27U);
        fea_stroke_q4(
            canvas, right_q4, y_q4,
            right_q4, y_q4 + (5 << 4),
            22, FP_THREAD, 27U);
    }
}

static void render_felt_patch_pal(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_FELT_PATCH_PAL, pose, sample_clock, &layout);
    fea_fill(canvas, FP_BG);
    for (int32_t line = 0; line < 7; ++line) {
        fea_stroke_q4(
            canvas, 8 << 4, (12 + line * 16) << 4,
            152 << 4, (12 + line * 16) << 4,
            14, FP_GRID, 14U);
    }

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t half_w_q4 = 48 << 4;
    int32_t half_h_q4 = 43 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        half_w_q4 += sticker_act_px(pose, 4);
        half_h_q4 += sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        half_w_q4 += sticker_act_px(pose, 4);
        half_h_q4 -= sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        half_h_q4 -= sticker_act_px(pose, 7);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        half_h_q4 += sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_EMBARRASSED) {
        half_w_q4 -= sticker_act_px(pose, 3);
    }
    half_w_q4 = fea_clamp_i32(half_w_q4, 42 << 4, 54 << 4);
    half_h_q4 = fea_clamp_i32(half_h_q4, 34 << 4, 49 << 4);
    const int32_t left_q4 = cx_q4 - half_w_q4;
    const int32_t right_q4 = cx_q4 + half_w_q4;
    const int32_t top_q4 = cy_q4 - half_h_q4;
    const int32_t bottom_q4 = cy_q4 + half_h_q4;

    fea_roundrect_q4(
        canvas, left_q4 + 40, top_q4 + 48,
        right_q4 + 40, bottom_q4 + 48,
        14 << 4, FP_SHADOW, 22U);

    /* Scalloped die-cut felt edge. */
    for (int32_t x_q4 = left_q4 + (8 << 4);
         x_q4 <= right_q4 - (8 << 4);
         x_q4 += 16 << 4) {
        fea_ellipse_q4(
            canvas, x_q4, top_q4,
            7 << 4, 7 << 4, FP_EDGE, 32U);
        fea_ellipse_q4(
            canvas, x_q4, bottom_q4,
            7 << 4, 7 << 4, FP_EDGE, 32U);
    }
    for (int32_t y_q4 = top_q4 + (8 << 4);
         y_q4 <= bottom_q4 - (8 << 4);
         y_q4 += 16 << 4) {
        fea_ellipse_q4(
            canvas, left_q4, y_q4,
            7 << 4, 7 << 4, FP_EDGE, 32U);
        fea_ellipse_q4(
            canvas, right_q4, y_q4,
            7 << 4, 7 << 4, FP_EDGE, 32U);
    }
    fea_roundrect_q4(
        canvas, left_q4, top_q4,
        right_q4, bottom_q4,
        13 << 4, FP_EDGE, 32U);
    fea_roundrect_q4(
        canvas, left_q4 + 42, top_q4 + 42,
        right_q4 - 42, bottom_q4 - 42,
        11 << 4, FP_PATCH, 32U);
    fea_ellipse_q4(
        canvas, cx_q4 - (12 << 4), cy_q4 - (16 << 4),
        28 << 4, 20 << 4, FP_PATCH_HI, 11U);
    felt_draw_stitches(
        canvas, left_q4 + 54, top_q4 + 54,
        right_q4 - 54, bottom_q4 - 54);

    /* Peeled corner and loose thread are material-specific acting. */
    int32_t peel_q4 = 7 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE ||
        pose->emotion == FACE_EXPRESSION_EXCITED) {
        peel_q4 += sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        peel_q4 -= sticker_act_px(pose, 3);
    }
    peel_q4 = fea_clamp_i32(peel_q4, 4 << 4, 13 << 4);
    fea_triangle_q4(
        canvas, right_q4 - peel_q4, top_q4 + 44,
        right_q4 - 44, top_q4 + peel_q4,
        right_q4 - 44, top_q4 + 44,
        FP_THREAD, 28U);
    fea_stroke_q4(
        canvas, left_q4 + (4 << 4), bottom_q4 - (3 << 4),
        left_q4 - (7 << 4), bottom_q4 + (5 << 4),
        24, FP_THREAD, 26U);
    fea_stroke_q4(
        canvas, left_q4 - (7 << 4), bottom_q4 + (5 << 4),
        left_q4 - (2 << 4), bottom_q4 + (9 << 4),
        20, FP_THREAD, 22U);

    fea_favourite_set_mouth_colors(
        &layout, FP_THREAD, FP_MOUTH,
        FP_THREAD, FP_BLUSH, 5);
    sticker_draw_blush(canvas, pose, &layout, FP_BLUSH);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_BUTTON, FP_THREAD,
        FP_PATCH_HI, FP_BUTTON, FP_INK,
        FP_BUTTON_HI, FP_THREAD, FP_PATCH, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, FP_THREAD, 40, 30U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    felt_patch_accessories(
        canvas, pose, &layout, sample_clock);
}

static void bubble_punctuation(
    fea_canvas_t *canvas,
    const fea_pose_t *pose,
    const fea_favourite_layout_t *layout,
    uint32_t sample_clock)
{
    const uint32_t alpha = fea_favourite_act_alpha(pose, 31U);
    const int32_t cx_q4 = layout->anchor.x_q4;
    const int32_t cy_q4 = layout->anchor.y_q4;
    switch (pose->emotion) {
    case FACE_EXPRESSION_WARM:
        fea_favourite_draw_heart(
            canvas, cx_q4 + (47 << 4), cy_q4 - (20 << 4),
            4 << 4, SB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_JOY:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (47 << 4), cy_q4 - (20 << 4),
            4 << 4, SB_ACCENT, alpha);
        fea_favourite_draw_spark(
            canvas, cx_q4 + (47 << 4), cy_q4 - (25 << 4),
            4 << 4, 23, SB_INK, alpha);
        break;
    case FACE_EXPRESSION_CONCERN:
        fea_favourite_draw_question(
            canvas, cx_q4 + (48 << 4), cy_q4 - (18 << 4),
            4 << 4, 28, SB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_SURPRISE:
        fea_stroke_q4(
            canvas, cx_q4, cy_q4 - (47 << 4),
            cx_q4, cy_q4 - (56 << 4),
            34, SB_ACCENT, alpha);
        fea_ellipse_q4(
            canvas, cx_q4, cy_q4 - (43 << 4),
            22, 22, SB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        for (int32_t dot = 0; dot < 3; ++dot) {
            fea_ellipse_q4(
                canvas, cx_q4 - (45 << 4) - dot * (5 << 4),
                cy_q4 + (19 << 4) + dot * (4 << 4),
                (3 - dot) << 3, (3 - dot) << 3,
                SB_INK, alpha);
        }
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        fea_favourite_draw_question(
            canvas, cx_q4 + (48 << 4), cy_q4 - (19 << 4),
            4 << 4, 28, SB_INK, alpha);
        break;
    case FACE_EXPRESSION_DETERMINED:
        for (int32_t line = 0; line < 3; ++line) {
            fea_stroke_q4(
                canvas, cx_q4 - (53 << 4),
                cy_q4 + (line * 7 - 7) * 16,
                cx_q4 - (41 << 4),
                cy_q4 + (line * 3 - 3) * 16,
                26, SB_ACCENT, alpha);
        }
        break;
    case FACE_EXPRESSION_SLEEPY: {
        const int32_t float_q4 =
            (fea_sin_q14((uint32_t)((uint64_t)sample_clock *
                                    65536U / 60000U)) * 20) >> 14;
        fea_favourite_draw_z(
            canvas, cx_q4 + (47 << 4),
            cy_q4 - (18 << 4) + float_q4,
            4 << 4, 25, SB_INK, alpha);
        break;
    }
    case FACE_EXPRESSION_EXCITED:
        fea_stroke_q4(
            canvas, cx_q4 - (47 << 4), cy_q4 - (24 << 4),
            cx_q4 - (55 << 4), cy_q4 - (31 << 4),
            28, SB_ACCENT, alpha);
        fea_stroke_q4(
            canvas, cx_q4 + (47 << 4), cy_q4 - (24 << 4),
            cx_q4 + (55 << 4), cy_q4 - (31 << 4),
            28, SB_ACCENT, alpha);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        fea_favourite_draw_heart(
            canvas, cx_q4 - (47 << 4), cy_q4 + (19 << 4),
            4 << 4, SB_ACCENT, alpha);
        break;
    default:
        break;
    }

    /* Speech itself adds tiny radiating marks beside the balloon tail. */
    if (pose->speaking != 0U &&
        pose->speech_phase == FACE_SPEECH_ACTIVE &&
        pose->audio_q8 > 40U) {
        const uint32_t speech_alpha =
            9U + (uint32_t)pose->audio_q8 * 18U / 255U;
        fea_stroke_q4(
            canvas, cx_q4 + (43 << 4), cy_q4 + (35 << 4),
            cx_q4 + (50 << 4), cy_q4 + (39 << 4),
            24, SB_ACCENT, speech_alpha);
        fea_stroke_q4(
            canvas, cx_q4 + (44 << 4), cy_q4 + (28 << 4),
            cx_q4 + (53 << 4), cy_q4 + (29 << 4),
            22, SB_ACCENT, speech_alpha);
    }
}

static void render_speech_bubble_sprite(
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    fea_favourite_layout_t layout;
    fea_favourite_layout_build(
        FEA_FAVOURITE_SPEECH_BUBBLE_SPRITE,
        pose, sample_clock, &layout);
    fea_fill(canvas, SB_BG);
    sticker_draw_halftone(canvas, SB_DOT, 0x5B0BB1EU);

    const int32_t cx_q4 = layout.anchor.x_q4;
    const int32_t cy_q4 = layout.anchor.y_q4;
    int32_t half_w_q4 = 51 << 4;
    int32_t half_h_q4 = 41 << 4;
    if (pose->emotion == FACE_EXPRESSION_SURPRISE) {
        half_w_q4 += sticker_act_px(pose, 4);
        half_h_q4 += sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_DETERMINED) {
        half_w_q4 += sticker_act_px(pose, 5);
        half_h_q4 -= sticker_act_px(pose, 5);
    } else if (pose->emotion == FACE_EXPRESSION_SLEEPY) {
        half_w_q4 -= sticker_act_px(pose, 3);
        half_h_q4 -= sticker_act_px(pose, 6);
    } else if (pose->emotion == FACE_EXPRESSION_EXCITED) {
        half_w_q4 += sticker_act_px(pose, 3);
        half_h_q4 += sticker_act_px(pose, 4);
    } else if (pose->emotion == FACE_EXPRESSION_EMBARRASSED) {
        half_w_q4 -= sticker_act_px(pose, 4);
    }
    half_w_q4 = fea_clamp_i32(half_w_q4, 44 << 4, 56 << 4);
    half_h_q4 = fea_clamp_i32(half_h_q4, 33 << 4, 47 << 4);
    const int32_t left_q4 = cx_q4 - half_w_q4;
    const int32_t right_q4 = cx_q4 + half_w_q4;
    const int32_t top_q4 = cy_q4 - half_h_q4;
    const int32_t bottom_q4 = cy_q4 + half_h_q4;

    int32_t tail_x_q4 =
        cx_q4 + ((pose->gaze_x_q8 * (23 << 4)) >> 8);
    if (pose->emotion == FACE_EXPRESSION_THOUGHTFUL) {
        tail_x_q4 -= sticker_act_px(pose, 13);
    } else if (pose->emotion == FACE_EXPRESSION_SKEPTICAL) {
        tail_x_q4 += sticker_act_px(pose, 12);
    } else if (pose->emotion == FACE_EXPRESSION_EMBARRASSED) {
        tail_x_q4 -= sticker_act_px(pose, 9);
    }
    tail_x_q4 = fea_clamp_i32(
        tail_x_q4, cx_q4 - (29 << 4), cx_q4 + (29 << 4));
    const int32_t tail_tip_y_q4 = fea_clamp_i32(
        bottom_q4 + (13 << 4), 0, 112 << 4);

    fea_roundrect_q4(
        canvas, left_q4 + 44, top_q4 + 50,
        right_q4 + 44, bottom_q4 + 50,
        16 << 4, SB_SHADOW, 21U);
    fea_triangle_q4(
        canvas, tail_x_q4 - (9 << 4) + 44, bottom_q4 + 22,
        tail_x_q4 + (9 << 4) + 44, bottom_q4 + 22,
        tail_x_q4 + (7 << 4) + 44, tail_tip_y_q4 + 44,
        SB_SHADOW, 21U);

    fea_triangle_q4(
        canvas, tail_x_q4 - (10 << 4), bottom_q4 - 20,
        tail_x_q4 + (10 << 4), bottom_q4 - 20,
        tail_x_q4 + (7 << 4), tail_tip_y_q4,
        SB_BORDER, 32U);
    fea_roundrect_q4(
        canvas, left_q4 - 48, top_q4 - 48,
        right_q4 + 48, bottom_q4 + 48,
        17 << 4, SB_BORDER, 32U);
    fea_triangle_q4(
        canvas, tail_x_q4 - (8 << 4), bottom_q4 - 32,
        tail_x_q4 + (8 << 4), bottom_q4 - 32,
        tail_x_q4 + (6 << 4), tail_tip_y_q4 - 32,
        SB_FACE, 32U);
    fea_roundrect_q4(
        canvas, left_q4, top_q4,
        right_q4, bottom_q4,
        14 << 4, SB_INK, 32U);
    fea_roundrect_q4(
        canvas, left_q4 + 34, top_q4 + 34,
        right_q4 - 34, bottom_q4 - 34,
        12 << 4, SB_FACE, 32U);
    fea_ellipse_q4(
        canvas, cx_q4 - (12 << 4), cy_q4 - (13 << 4),
        30 << 4, 20 << 4, SB_BORDER, 10U);
    fea_ellipse_q4(
        canvas, cx_q4, cy_q4 + (24 << 4),
        36 << 4, 14 << 4, SB_FACE_SHADE, 10U);

    fea_favourite_set_mouth_colors(
        &layout, SB_INK, SB_MOUTH,
        SB_SCLERA, SB_TONGUE, 4);
    sticker_draw_blush(canvas, pose, &layout, SB_ACCENT);
    const fea_favourite_eye_style_t eye_style = {
        FEA_FAVOURITE_EYE_INK, SB_INK,
        SB_SCLERA, SB_IRIS, SB_INK,
        SB_BORDER, SB_INK, SB_FACE, 1U, 1U,
    };
    fea_favourite_draw_eyes(canvas, pose, &layout, &eye_style);
    fea_favourite_draw_brows(canvas, &layout, SB_INK, 48, 32U);
    fea_lipmouth_draw(canvas, &layout.mouth);
    bubble_punctuation(canvas, pose, &layout, sample_clock);
}

void fea_favourite_sticker_render(
    fea_favourite_profile_t profile,
    const fea_pose_t *pose,
    uint32_t sample_clock,
    fea_canvas_t *canvas)
{
    switch (profile) {
    case FEA_FAVOURITE_POP_BURST:
        render_pop_burst(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_FELT_PATCH_PAL:
        render_felt_patch_pal(pose, sample_clock, canvas);
        break;
    case FEA_FAVOURITE_SPEECH_BUBBLE_SPRITE:
        render_speech_bubble_sprite(pose, sample_clock, canvas);
        break;
    default:
        fea_fill(canvas, PB_BG);
        break;
    }
}
