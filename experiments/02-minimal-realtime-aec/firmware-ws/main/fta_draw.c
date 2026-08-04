/*
 * fable_toon_acting — integer scanline compositor.
 *
 * Everything is drawn from parametric spans: horizontal spans for the
 * plate/eyes (per row), vertical spans for the mouth (per column between
 * two quadratic lip curves), and small capsule strokes for brows and
 * lashes. Q4 endpoints give one-pixel coverage ramps, so edges are
 * anti-aliased without any supersampling. Alpha is 0..32 (32 opaque).
 * No allocation, no floats, no retained state.
 */

#include "fta_internal.h"

/* ---- pixel level ------------------------------------------------------ */

uint16_t fta_blend565(uint16_t background, uint16_t foreground, uint8_t alpha32)
{
    if (alpha32 >= 32U) {
        return foreground;
    }
    if (alpha32 == 0U) {
        return background;
    }
    /* split 565 into two fields so all three channels blend in one op */
    const uint32_t fg =
        ((uint32_t)foreground | ((uint32_t)foreground << 16)) & 0x07e0f81fU;
    const uint32_t bg =
        ((uint32_t)background | ((uint32_t)background << 16)) & 0x07e0f81fU;
    const uint32_t mixed =
        (bg + (((fg - bg) * alpha32) >> 5)) & 0x07e0f81fU;
    return (uint16_t)(mixed | (mixed >> 16));
}

void fta_canvas_fill(fta_canvas_t *canvas, uint16_t color)
{
    for (int32_t index = 0; index < FTA_PIXEL_COUNT; ++index) {
        canvas->pixels[index] = color;
    }
}

void fta_fill_rect(
    fta_canvas_t *canvas,
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    uint16_t color)
{
    left = fta_max_i32(left, 0);
    top = fta_max_i32(top, 0);
    right = fta_min_i32(right, FTA_FRAME_WIDTH);
    bottom = fta_min_i32(bottom, FTA_FRAME_HEIGHT);
    for (int32_t y = top; y < bottom; ++y) {
        uint16_t *row = canvas->pixels + y * FTA_FRAME_WIDTH;
        for (int32_t x = left; x < right; ++x) {
            row[x] = color;
        }
    }
}

void fta_blend_rect(
    fta_canvas_t *canvas,
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    uint16_t color, uint8_t alpha32)
{
    left = fta_max_i32(left, 0);
    top = fta_max_i32(top, 0);
    right = fta_min_i32(right, FTA_FRAME_WIDTH);
    bottom = fta_min_i32(bottom, FTA_FRAME_HEIGHT);
    for (int32_t y = top; y < bottom; ++y) {
        uint16_t *row = canvas->pixels + y * FTA_FRAME_WIDTH;
        for (int32_t x = left; x < right; ++x) {
            row[x] = fta_blend565(row[x], color, alpha32);
        }
    }
}

static int32_t isqrt_u32(uint32_t value)
{
    uint32_t result = 0U;
    uint32_t bit = 1UL << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)result;
}

/*
 * Horizontal span with Q4 endpoints: interior pixels are written at
 * `alpha32`, the two edge pixels get fractional coverage. This is the
 * primitive every filled shape reduces to.
 */
static void hspan_aa(
    fta_canvas_t *canvas, int32_t y,
    int32_t left_q4, int32_t right_q4,
    uint16_t color, uint8_t alpha32)
{
    if (y < 0 || y >= FTA_FRAME_HEIGHT || right_q4 <= left_q4 ||
        alpha32 == 0U) {
        return;
    }
    left_q4 = fta_max_i32(left_q4, 0);
    right_q4 = fta_min_i32(right_q4, FTA_FRAME_WIDTH * FTA_Q4);
    if (right_q4 <= left_q4) {
        return;
    }
    uint16_t *row = canvas->pixels + y * FTA_FRAME_WIDTH;
    const int32_t first = left_q4 >> 4;
    const int32_t last = (right_q4 - 1) >> 4;
    if (first == last) {
        const int32_t coverage = right_q4 - left_q4;
        row[first] = fta_blend565(
            row[first], color, (uint8_t)((coverage * alpha32) >> 4));
        return;
    }
    const int32_t left_coverage = ((first + 1) << 4) - left_q4;
    if (left_coverage >= 16) {
        if (alpha32 >= 32U) {
            row[first] = color;
        } else {
            row[first] = fta_blend565(row[first], color, alpha32);
        }
    } else {
        row[first] = fta_blend565(
            row[first], color, (uint8_t)((left_coverage * alpha32) >> 4));
    }
    if (alpha32 >= 32U) {
        for (int32_t x = first + 1; x < last; ++x) {
            row[x] = color;
        }
    } else {
        for (int32_t x = first + 1; x < last; ++x) {
            row[x] = fta_blend565(row[x], color, alpha32);
        }
    }
    const int32_t right_coverage = right_q4 - (last << 4);
    if (right_coverage >= 16) {
        if (alpha32 >= 32U) {
            row[last] = color;
        } else {
            row[last] = fta_blend565(row[last], color, alpha32);
        }
    } else {
        row[last] = fta_blend565(
            row[last], color, (uint8_t)((right_coverage * alpha32) >> 4));
    }
}

/* vertical counterpart, used by the mouth columns */
static void vspan_aa(
    fta_canvas_t *canvas, int32_t x,
    int32_t top_q4, int32_t bottom_q4,
    uint16_t color, uint8_t alpha32)
{
    if (x < 0 || x >= FTA_FRAME_WIDTH || bottom_q4 <= top_q4 ||
        alpha32 == 0U) {
        return;
    }
    top_q4 = fta_max_i32(top_q4, 0);
    bottom_q4 = fta_min_i32(bottom_q4, FTA_FRAME_HEIGHT * FTA_Q4);
    if (bottom_q4 <= top_q4) {
        return;
    }
    uint16_t *pixel = canvas->pixels + x;
    const int32_t first = top_q4 >> 4;
    const int32_t last = (bottom_q4 - 1) >> 4;
    if (first == last) {
        const int32_t coverage = bottom_q4 - top_q4;
        pixel[first * FTA_FRAME_WIDTH] = fta_blend565(
            pixel[first * FTA_FRAME_WIDTH], color,
            (uint8_t)((coverage * alpha32) >> 4));
        return;
    }
    const int32_t top_coverage = ((first + 1) << 4) - top_q4;
    pixel[first * FTA_FRAME_WIDTH] = fta_blend565(
        pixel[first * FTA_FRAME_WIDTH], color,
        (uint8_t)((fta_min_i32(top_coverage, 16) * alpha32) >> 4));
    for (int32_t y = first + 1; y < last; ++y) {
        if (alpha32 >= 32U) {
            pixel[y * FTA_FRAME_WIDTH] = color;
        } else {
            pixel[y * FTA_FRAME_WIDTH] =
                fta_blend565(pixel[y * FTA_FRAME_WIDTH], color, alpha32);
        }
    }
    const int32_t bottom_coverage = bottom_q4 - (last << 4);
    pixel[last * FTA_FRAME_WIDTH] = fta_blend565(
        pixel[last * FTA_FRAME_WIDTH], color,
        (uint8_t)((fta_min_i32(bottom_coverage, 16) * alpha32) >> 4));
}

/* ---- shapes ----------------------------------------------------------- */

void fta_fill_ellipse_q4(
    fta_canvas_t *canvas,
    int32_t center_x_q4, int32_t center_y_q4,
    int32_t radius_x_q4, int32_t radius_y_q4,
    uint16_t color, uint8_t alpha32)
{
    if (radius_x_q4 <= 0 || radius_y_q4 <= 0) {
        return;
    }
    const int32_t top = (center_y_q4 - radius_y_q4) >> 4;
    const int32_t bottom = ((center_y_q4 + radius_y_q4) >> 4) + 1;
    for (int32_t y = fta_max_i32(top, 0);
         y <= fta_min_i32(bottom, FTA_FRAME_HEIGHT - 1); ++y) {
        const int32_t dy = (y << 4) + 8 - center_y_q4;
        const int32_t ny_q8 = (dy * 256) / radius_y_q4;
        const int32_t inside_q16 = 65536 - ny_q8 * ny_q8;
        if (inside_q16 <= 0) {
            continue;
        }
        const int32_t wx_q8 = isqrt_u32((uint32_t)inside_q16);
        const int32_t half_q4 = (radius_x_q4 * wx_q8) >> 8;
        hspan_aa(
            canvas, y, center_x_q4 - half_q4, center_x_q4 + half_q4,
            color, alpha32);
    }
}

void fta_ring_ellipse_q4(
    fta_canvas_t *canvas,
    int32_t center_x_q4, int32_t center_y_q4,
    int32_t radius_x_q4, int32_t radius_y_q4,
    int32_t stroke_q4, uint16_t color, uint8_t alpha32)
{
    if (radius_x_q4 <= 0 || radius_y_q4 <= 0 || stroke_q4 <= 0) {
        return;
    }
    const int32_t inner_x = radius_x_q4 - stroke_q4;
    const int32_t inner_y = radius_y_q4 - stroke_q4;
    const int32_t top = (center_y_q4 - radius_y_q4) >> 4;
    const int32_t bottom = ((center_y_q4 + radius_y_q4) >> 4) + 1;
    for (int32_t y = fta_max_i32(top, 0);
         y <= fta_min_i32(bottom, FTA_FRAME_HEIGHT - 1); ++y) {
        const int32_t dy = (y << 4) + 8 - center_y_q4;
        const int32_t ny_q8 = (dy * 256) / radius_y_q4;
        const int32_t outside_q16 = 65536 - ny_q8 * ny_q8;
        if (outside_q16 <= 0) {
            continue;
        }
        const int32_t outer_half =
            (radius_x_q4 * isqrt_u32((uint32_t)outside_q16)) >> 8;
        int32_t inner_half = 0;
        if (inner_x > 0 && inner_y > 0 && fta_abs_i32(dy) < inner_y) {
            const int32_t niy_q8 = (dy * 256) / inner_y;
            const int32_t inner_q16 = 65536 - niy_q8 * niy_q8;
            if (inner_q16 > 0) {
                inner_half =
                    (inner_x * isqrt_u32((uint32_t)inner_q16)) >> 8;
            }
        }
        if (inner_half <= 0) {
            hspan_aa(
                canvas, y, center_x_q4 - outer_half,
                center_x_q4 + outer_half, color, alpha32);
        } else {
            hspan_aa(
                canvas, y, center_x_q4 - outer_half,
                center_x_q4 - inner_half, color, alpha32);
            hspan_aa(
                canvas, y, center_x_q4 + inner_half,
                center_x_q4 + outer_half, color, alpha32);
        }
    }
}

/* per-row half width of a rounded rectangle, all Q4 */
static int32_t round_rect_half_width(
    int32_t dy_q4, int32_t half_w_q4, int32_t half_h_q4, int32_t radius_q4)
{
    const int32_t edge = fta_abs_i32(dy_q4) - (half_h_q4 - radius_q4);
    if (edge <= 0) {
        return half_w_q4;
    }
    if (edge >= radius_q4) {
        return 0;
    }
    const int32_t chord_q4 = isqrt_u32(
        (uint32_t)(radius_q4 * radius_q4 - edge * edge));
    return half_w_q4 - radius_q4 + chord_q4;
}

void fta_fill_round_rect_q4(
    fta_canvas_t *canvas,
    int32_t left_q4, int32_t top_q4, int32_t right_q4, int32_t bottom_q4,
    int32_t radius_q4, uint16_t color, uint8_t alpha32)
{
    const int32_t half_w = (right_q4 - left_q4) / 2;
    const int32_t half_h = (bottom_q4 - top_q4) / 2;
    if (half_w <= 0 || half_h <= 0) {
        return;
    }
    radius_q4 = fta_clamp_i32(radius_q4, 0, fta_min_i32(half_w, half_h));
    const int32_t cx = (left_q4 + right_q4) / 2;
    const int32_t cy = (top_q4 + bottom_q4) / 2;
    for (int32_t y = fta_max_i32(top_q4 >> 4, 0);
         y <= fta_min_i32((bottom_q4 >> 4) + 1, FTA_FRAME_HEIGHT - 1);
         ++y) {
        const int32_t dy = (y << 4) + 8 - cy;
        if (fta_abs_i32(dy) > half_h) {
            continue;
        }
        const int32_t half =
            round_rect_half_width(dy, half_w, half_h, radius_q4);
        if (half > 0) {
            hspan_aa(canvas, y, cx - half, cx + half, color, alpha32);
        }
    }
}

void fta_ring_round_rect_q4(
    fta_canvas_t *canvas,
    int32_t left_q4, int32_t top_q4, int32_t right_q4, int32_t bottom_q4,
    int32_t radius_q4, int32_t stroke_q4, uint16_t color, uint8_t alpha32)
{
    const int32_t half_w = (right_q4 - left_q4) / 2;
    const int32_t half_h = (bottom_q4 - top_q4) / 2;
    if (half_w <= 0 || half_h <= 0 || stroke_q4 <= 0) {
        return;
    }
    radius_q4 = fta_clamp_i32(radius_q4, 0, fta_min_i32(half_w, half_h));
    const int32_t cx = (left_q4 + right_q4) / 2;
    const int32_t cy = (top_q4 + bottom_q4) / 2;
    const int32_t inner_half_w = half_w - stroke_q4;
    const int32_t inner_half_h = half_h - stroke_q4;
    const int32_t inner_radius = fta_max_i32(radius_q4 - stroke_q4, 0);
    for (int32_t y = fta_max_i32(top_q4 >> 4, 0);
         y <= fta_min_i32((bottom_q4 >> 4) + 1, FTA_FRAME_HEIGHT - 1);
         ++y) {
        const int32_t dy = (y << 4) + 8 - cy;
        if (fta_abs_i32(dy) > half_h) {
            continue;
        }
        const int32_t outer =
            round_rect_half_width(dy, half_w, half_h, radius_q4);
        if (outer <= 0) {
            continue;
        }
        int32_t inner = 0;
        if (inner_half_w > 0 && inner_half_h > 0 &&
            fta_abs_i32(dy) <= inner_half_h) {
            inner = round_rect_half_width(
                dy, inner_half_w, inner_half_h, inner_radius);
        }
        if (inner <= 0) {
            hspan_aa(canvas, y, cx - outer, cx + outer, color, alpha32);
        } else {
            hspan_aa(canvas, y, cx - outer, cx - inner, color, alpha32);
            hspan_aa(canvas, y, cx + inner, cx + outer, color, alpha32);
        }
    }
}

void fta_fill_capsule_q4(
    fta_canvas_t *canvas,
    int32_t x0_q4, int32_t y0_q4, int32_t x1_q4, int32_t y1_q4,
    int32_t radius_q4, uint16_t color, uint8_t alpha32)
{
    if (radius_q4 <= 0) {
        return;
    }
    const int32_t left =
        (fta_min_i32(x0_q4, x1_q4) - radius_q4 - 8) >> 4;
    const int32_t right =
        ((fta_max_i32(x0_q4, x1_q4) + radius_q4 + 8) >> 4) + 1;
    const int32_t top =
        (fta_min_i32(y0_q4, y1_q4) - radius_q4 - 8) >> 4;
    const int32_t bottom =
        ((fta_max_i32(y0_q4, y1_q4) + radius_q4 + 8) >> 4) + 1;
    const int32_t dx = x1_q4 - x0_q4;
    const int32_t dy = y1_q4 - y0_q4;
    const int32_t length_sq = dx * dx + dy * dy;
    for (int32_t y = fta_max_i32(top, 0);
         y <= fta_min_i32(bottom, FTA_FRAME_HEIGHT - 1); ++y) {
        uint16_t *row = canvas->pixels + y * FTA_FRAME_WIDTH;
        for (int32_t x = fta_max_i32(left, 0);
             x <= fta_min_i32(right, FTA_FRAME_WIDTH - 1); ++x) {
            const int32_t px = (x << 4) + 8 - x0_q4;
            const int32_t py = (y << 4) + 8 - y0_q4;
            int32_t nearest_x = 0;
            int32_t nearest_y = 0;
            if (length_sq > 0) {
                int32_t t_q8 = (int32_t)(
                    ((int64_t)(px * dx + py * dy) * 256) / length_sq);
                t_q8 = fta_clamp_i32(t_q8, 0, 256);
                nearest_x = (int32_t)(((int64_t)dx * t_q8) >> 8);
                nearest_y = (int32_t)(((int64_t)dy * t_q8) >> 8);
            }
            const int32_t ox = px - nearest_x;
            const int32_t oy = py - nearest_y;
            const int32_t distance =
                isqrt_u32((uint32_t)(ox * ox + oy * oy));
            const int32_t coverage = radius_q4 + 8 - distance;
            if (coverage <= 0) {
                continue;
            }
            const int32_t edge = fta_min_i32(coverage, 16);
            row[x] = fta_blend565(
                row[x], color, (uint8_t)((edge * alpha32) >> 4));
        }
    }
}

/* ---- eye composition --------------------------------------------------- */

/*
 * Intersect the sclera row span with the two lid half-planes. A lid edge
 * is the line y = base + slope * (x - cx); the visible aperture at row y
 * is the sub-span where the row sits below the upper lid line and above
 * the lower one.
 */
static bool lid_interval(
    int32_t row_y_q4, int32_t eye_cx_q4,
    int32_t base_q4, int32_t slope_q12, bool keep_below,
    int32_t *left_q4, int32_t *right_q4)
{
    const int32_t relative = row_y_q4 - base_q4;
    if (slope_q12 == 0) {
        return keep_below ? relative >= 0 : relative <= 0;
    }
    const int32_t boundary =
        eye_cx_q4 + (int32_t)(((int64_t)relative * 4096) / slope_q12);
    /* keep_below: y >= line. slope > 0 means the line rises to the
     * right, so the condition holds left of the boundary. */
    const bool keep_left = keep_below == (slope_q12 > 0);
    if (keep_left) {
        *right_q4 = fta_min_i32(*right_q4, boundary);
    } else {
        *left_q4 = fta_max_i32(*left_q4, boundary);
    }
    return *left_q4 < *right_q4;
}

static void draw_eye(
    fta_canvas_t *canvas,
    const fta_style_t *style,
    const fta_eye_t *eye)
{
    const fta_palette_t *palette = &style->palette;
    const bool ink = style->look == FTA_LOOK_INK;
    const int32_t cx = eye->center_x_q4;
    const int32_t cy = eye->center_y_q4;
    const int32_t half_w = eye->half_w_q4;
    const int32_t half_h = eye->half_h_q4;
    const int32_t corner = fta_min_i32(
        (half_w * 3) / 4, (half_h * 2) / 5);

    if (eye->openness_q8 < 16U) {
        /* cartoon closed eye: one confident lash stroke on the lid line */
        const int32_t left_y =
            eye->lid_top_q4 -
            (int32_t)((((int64_t)eye->lid_top_slope_q12) * half_w) >> 12);
        const int32_t right_y =
            eye->lid_top_q4 +
            (int32_t)((((int64_t)eye->lid_top_slope_q12) * half_w) >> 12);
        fta_fill_capsule_q4(
            canvas, cx - half_w + 8, left_y, cx + half_w - 8, right_y,
            14, palette->lash, 32U);
        return;
    }

    const int32_t top = fta_max_i32((cy - half_h) >> 4, 0);
    const int32_t bottom =
        fta_min_i32(((cy + half_h) >> 4) + 1, FTA_FRAME_HEIGHT - 1);
    for (int32_t y = top; y <= bottom; ++y) {
        const int32_t row_y = (y << 4) + 8;
        const int32_t dy = row_y - cy;
        if (fta_abs_i32(dy) > half_h) {
            continue;
        }
        const int32_t sclera_half =
            round_rect_half_width(dy, half_w, half_h, corner);
        if (sclera_half <= 0) {
            continue;
        }
        const int32_t sclera_left = cx - sclera_half;
        const int32_t sclera_right = cx + sclera_half;
        int32_t aperture_left = sclera_left;
        int32_t aperture_right = sclera_right;
        bool open = lid_interval(
            row_y, cx, eye->lid_top_q4, eye->lid_top_slope_q12, true,
            &aperture_left, &aperture_right);
        if (open) {
            open = lid_interval(
                row_y, cx, eye->lid_bottom_q4, eye->lid_bottom_slope_q12,
                false, &aperture_left, &aperture_right);
        }
        if (!open || aperture_left >= aperture_right) {
            /* the row is fully covered by lid */
            if (!ink) {
                hspan_aa(
                    canvas, y, sclera_left, sclera_right,
                    palette->lid, 32U);
            }
            continue;
        }
        /* lid-covered flanks */
        if (!ink) {
            if (aperture_left > sclera_left) {
                hspan_aa(
                    canvas, y, sclera_left, aperture_left,
                    palette->lid, 32U);
            }
            if (aperture_right < sclera_right) {
                hspan_aa(
                    canvas, y, aperture_right, sclera_right,
                    palette->lid, 32U);
            }
        }
        /* sclera */
        hspan_aa(
            canvas, y, aperture_left, aperture_right,
            palette->sclera, 32U);
        /* iris ring and pupil, clipped to the aperture sub-span */
        const int32_t iris_dy = row_y - eye->pupil_y_q4;
        if (fta_abs_i32(iris_dy) < eye->iris_r_q4) {
            const int32_t iris_half = isqrt_u32(
                (uint32_t)(eye->iris_r_q4 * eye->iris_r_q4 -
                           iris_dy * iris_dy));
            const int32_t left =
                fta_max_i32(eye->pupil_x_q4 - iris_half, aperture_left);
            const int32_t right =
                fta_min_i32(eye->pupil_x_q4 + iris_half, aperture_right);
            hspan_aa(canvas, y, left, right, palette->iris, 32U);
        }
        const int32_t pupil_dy = row_y - eye->pupil_y_q4;
        if (fta_abs_i32(pupil_dy) < eye->pupil_r_q4) {
            const int32_t pupil_half = isqrt_u32(
                (uint32_t)(eye->pupil_r_q4 * eye->pupil_r_q4 -
                           pupil_dy * pupil_dy));
            const int32_t left =
                fta_max_i32(eye->pupil_x_q4 - pupil_half, aperture_left);
            const int32_t right =
                fta_min_i32(eye->pupil_x_q4 + pupil_half, aperture_right);
            hspan_aa(canvas, y, left, right, palette->pupil, 32U);
        }
    }

    if (ink) {
        /* ink look: outline the open eye instead of filling lids */
        fta_ring_round_rect_q4(
            canvas, cx - half_w, fta_max_i32(eye->lid_top_q4, cy - half_h),
            cx + half_w,
            fta_min_i32(eye->lid_bottom_q4 + 8, cy + half_h),
            corner, 20, palette->lash, 32U);
    }

    /* lash line along the upper lid edge */
    {
        const int32_t reach = half_w - 6;
        const int32_t left_y =
            eye->lid_top_q4 -
            (int32_t)((((int64_t)eye->lid_top_slope_q12) * reach) >> 12);
        const int32_t right_y =
            eye->lid_top_q4 +
            (int32_t)((((int64_t)eye->lid_top_slope_q12) * reach) >> 12);
        fta_fill_capsule_q4(
            canvas, cx - reach, left_y, cx + reach, right_y,
            10, palette->lash, ink ? 32U : 26U);
    }

    /* glints: primary upper-left of the pupil, micro lower-right */
    const int32_t glint_r =
        fta_max_i32(eye->pupil_r_q4 / 3, 12);
    const int32_t glint_x = eye->pupil_x_q4 - eye->pupil_r_q4 * 2 / 5;
    const int32_t glint_y = eye->pupil_y_q4 - eye->pupil_r_q4 * 2 / 5;
    fta_fill_ellipse_q4(
        canvas, glint_x, glint_y, glint_r, glint_r, palette->glint, 30U);
    if (eye->sparkle > 24U) {
        const int32_t micro_r = fta_max_i32(glint_r / 2, 8);
        fta_fill_ellipse_q4(
            canvas,
            eye->pupil_x_q4 + eye->pupil_r_q4 * 2 / 5,
            eye->pupil_y_q4 + eye->pupil_r_q4 * 2 / 5,
            micro_r, micro_r, palette->glint, 22U);
    }
    if (eye->sparkle > 96U) {
        /* excited: the glint becomes a four-ray star */
        const int32_t ray = glint_r + (eye->sparkle * 28) / 255;
        fta_fill_capsule_q4(
            canvas, glint_x - ray, glint_y, glint_x + ray, glint_y,
            10, palette->glint, 30U);
        fta_fill_capsule_q4(
            canvas, glint_x, glint_y - ray, glint_x, glint_y + ray,
            10, palette->glint, 30U);
    }
}

/* ---- mouth ------------------------------------------------------------- */

static int32_t quad_bezier_q4(
    int32_t y0_q4, int32_t mid_q4, int32_t y1_q4, int32_t t_q8)
{
    const int32_t inverse = 256 - t_q8;
    return (int32_t)(
        ((int64_t)y0_q4 * inverse * inverse +
         (int64_t)mid_q4 * 2 * t_q8 * inverse +
         (int64_t)y1_q4 * t_q8 * t_q8) >> 16);
}

static void draw_mouth(
    fta_canvas_t *canvas,
    const fta_style_t *style,
    const fta_mouth_t *mouth)
{
    const fta_palette_t *palette = &style->palette;
    const int32_t cx = mouth->center_x_q4;
    const int32_t cy = mouth->center_y_q4;
    const int32_t half_w = mouth->half_w_q4;
    const int32_t lip = mouth->lip_q4;
    const int32_t left_y = cy + mouth->corner_left_q4;
    const int32_t right_y = cy + mouth->corner_right_q4;
    /*
     * JALI-style split: the jaw carries 60% of the opening downward.
     * Control offsets are doubled because a quadratic reaches only half
     * its control-point offset at the midpoint — this makes the center
     * aperture equal open_q4 exactly.
     */
    const int32_t upper_mid =
        cy + mouth->curve_q4 - (mouth->open_q4 * 4) / 5;
    const int32_t lower_mid =
        cy + mouth->curve_q4 + (mouth->open_q4 * 6) / 5 +
        ((int32_t)mouth->round_q8 * mouth->open_q4) / 256;
    const int32_t first = fta_max_i32((cx - half_w) >> 4, 0);
    const int32_t last =
        fta_min_i32(((cx + half_w) >> 4) + 1, FTA_FRAME_WIDTH - 1);
    if (last <= first) {
        return;
    }
    for (int32_t x = first; x <= last; ++x) {
        const int32_t column_q4 = (x << 4) + 8;
        if (column_q4 < cx - half_w || column_q4 > cx + half_w) {
            continue;
        }
        const int32_t t_q8 = (int32_t)fta_clamp_i32(
            ((column_q4 - (cx - half_w)) * 256) / (half_w * 2), 0, 256);
        const int32_t upper = quad_bezier_q4(left_y, upper_mid, right_y, t_q8);
        int32_t lower = quad_bezier_q4(left_y, lower_mid, right_y, t_q8);
        if (lower < upper) {
            lower = upper;
        }
        /* interior */
        if (lower - upper > 4) {
            vspan_aa(
                canvas, x, upper, lower, palette->mouth_interior, 32U);
            /* upper teeth band hangs from the upper lip */
            if (mouth->teeth_q4 > 4) {
                vspan_aa(
                    canvas, x, upper,
                    fta_min_i32(upper + mouth->teeth_q4, lower),
                    palette->teeth, 30U);
            }
            /* tongue hump rises from the floor, center 62% of the width */
            if (mouth->tongue_q4 > 4) {
                const int32_t middle = fta_abs_i32(t_q8 - 128);
                if (middle < 80) {
                    const int32_t profile =
                        (mouth->tongue_q4 * (80 - middle)) / 80;
                    vspan_aa(
                        canvas, x, fta_max_i32(lower - profile, upper),
                        lower, palette->tongue, 30U);
                }
            }
        }
        /* lips */
        vspan_aa(canvas, x, upper - lip, upper, palette->lip, 32U);
        vspan_aa(canvas, x, lower, lower + lip, palette->lip, 32U);
    }
}

/* ---- masked blush ------------------------------------------------------ */

/*
 * Ellipse fill that clips every row against both eye whites (the open
 * aperture between the lids, padded by one pixel). Guarantees blush can
 * never be drawn across sclera or pupils, whatever the pose does.
 */
static void blush_masked_ellipse(
    fta_canvas_t *canvas, const fta_rig_t *rig,
    int32_t center_x_q4, int32_t center_y_q4,
    int32_t radius_x_q4, int32_t radius_y_q4,
    uint16_t color, uint8_t alpha32)
{
    if (radius_x_q4 <= 0 || radius_y_q4 <= 0 || alpha32 == 0U) {
        return;
    }
    const int32_t top = (center_y_q4 - radius_y_q4) >> 4;
    const int32_t bottom = ((center_y_q4 + radius_y_q4) >> 4) + 1;
    for (int32_t y = fta_max_i32(top, 0);
         y <= fta_min_i32(bottom, FTA_FRAME_HEIGHT - 1); ++y) {
        const int32_t dy = (y << 4) + 8 - center_y_q4;
        const int32_t ny_q8 = (dy * 256) / radius_y_q4;
        const int32_t inside_q16 = 65536 - ny_q8 * ny_q8;
        if (inside_q16 <= 0) {
            continue;
        }
        const int32_t half_q4 =
            (radius_x_q4 * isqrt_u32((uint32_t)inside_q16)) >> 8;
        int32_t spans[3][2] = {
            {center_x_q4 - half_q4, center_x_q4 + half_q4},
            {0, 0},
            {0, 0},
        };
        int32_t span_count = 1;
        const int32_t row_y = (y << 4) + 8;
        for (int32_t side = 0; side < 2; ++side) {
            const fta_eye_t *eye = &rig->eye[side];
            if (row_y < eye->lid_top_q4 - FTA_Q4 ||
                row_y > eye->lid_bottom_q4 + FTA_Q4) {
                continue;
            }
            const int32_t hole_left =
                eye->center_x_q4 - eye->half_w_q4 - FTA_Q4;
            const int32_t hole_right =
                eye->center_x_q4 + eye->half_w_q4 + FTA_Q4;
            for (int32_t index = 0; index < span_count && span_count < 3;
                 ++index) {
                int32_t *span = spans[index];
                if (hole_left <= span[0] && hole_right >= span[1]) {
                    span[1] = span[0]; /* fully swallowed */
                } else if (hole_left > span[0] && hole_right < span[1]) {
                    spans[span_count][0] = hole_right;
                    spans[span_count][1] = span[1];
                    span[1] = hole_left;
                    ++span_count;
                } else if (hole_left <= span[0] && hole_right > span[0] &&
                           hole_right < span[1]) {
                    span[0] = hole_right;
                } else if (hole_left > span[0] && hole_left < span[1] &&
                           hole_right >= span[1]) {
                    span[1] = hole_left;
                }
            }
        }
        for (int32_t index = 0; index < span_count; ++index) {
            if (spans[index][1] > spans[index][0]) {
                hspan_aa(
                    canvas, y, spans[index][0], spans[index][1],
                    color, alpha32);
            }
        }
    }
}

/* ---- frame composition ------------------------------------------------- */

void fta_draw_rig(
    fta_canvas_t *canvas,
    const fta_style_t *style,
    const fta_rig_t *rig)
{
    const fta_palette_t *palette = &style->palette;
    const bool ink = style->look == FTA_LOOK_INK;

    fta_canvas_fill(canvas, palette->background);

    /* plate with per-row shear (the roll illusion) */
    const int32_t plate_cx =
        (rig->plate_left_q4 + rig->plate_right_q4) / 2;
    const int32_t plate_cy =
        (rig->plate_top_q4 + rig->plate_bottom_q4) / 2;
    const int32_t plate_half_w =
        (rig->plate_right_q4 - rig->plate_left_q4) / 2;
    const int32_t plate_half_h =
        (rig->plate_bottom_q4 - rig->plate_top_q4) / 2;
    const int32_t plate_radius = fta_clamp_i32(
        rig->plate_radius_q4, 0, fta_min_i32(plate_half_w, plate_half_h));
    const int32_t stroke = ink ? 26 : 20;
    for (int32_t y = fta_max_i32(rig->plate_top_q4 >> 4, 0);
         y <= fta_min_i32(
                  (rig->plate_bottom_q4 >> 4) + 1, FTA_FRAME_HEIGHT - 1);
         ++y) {
        const int32_t dy = (y << 4) + 8 - plate_cy;
        if (fta_abs_i32(dy) > plate_half_h) {
            continue;
        }
        const int32_t half = round_rect_half_width(
            dy, plate_half_w, plate_half_h, plate_radius);
        if (half <= 0) {
            continue;
        }
        const int32_t shear_offset =
            (int32_t)((((int64_t)rig->shear_q12) * dy) >> 12);
        const int32_t left = plate_cx + shear_offset - half;
        const int32_t right = plate_cx + shear_offset + half;
        if (!ink) {
            hspan_aa(canvas, y, left, right, palette->plate, 32U);
            /* one-pixel rim keeps the silhouette crisp on any bg */
            hspan_aa(
                canvas, y, left, left + stroke,
                palette->plate_outline, 18U);
            hspan_aa(
                canvas, y, right - stroke, right,
                palette->plate_outline, 18U);
        } else {
            hspan_aa(canvas, y, left, right, palette->plate, 32U);
            hspan_aa(
                canvas, y, left, left + stroke,
                palette->plate_outline, 32U);
            hspan_aa(
                canvas, y, right - stroke, right,
                palette->plate_outline, 32U);
        }
        /* top and bottom rims arrive from the row extremes */
        if (fta_abs_i32(dy) > plate_half_h - stroke) {
            hspan_aa(
                canvas, y, left, right, palette->plate_outline,
                ink ? 32U : 18U);
        }
    }

    /* blush pads and the embarrassed cheek band, hard-masked out of the
     * eye whites so no blush pixel can ever cross a pupil */
    for (int32_t side = 0; side < 2; ++side) {
        const fta_blush_t *blush = &rig->blush[side];
        if (blush->alpha > 0U) {
            blush_masked_ellipse(
                canvas, rig, blush->center_x_q4, blush->center_y_q4,
                blush->half_w_q4, blush->half_h_q4,
                palette->blush, blush->alpha);
        }
    }
    if (rig->blush_band_alpha > 0U) {
        blush_masked_ellipse(
            canvas, rig,
            (rig->blush[0].center_x_q4 + rig->blush[1].center_x_q4) / 2,
            rig->blush_band_y_q4,
            (rig->blush[1].center_x_q4 - rig->blush[0].center_x_q4) / 2 +
                rig->blush[0].half_w_q4,
            2 * FTA_Q4, palette->blush, rig->blush_band_alpha);
    }

    /* sweat droplet on the temple, sliding down */
    if (rig->sweat_alpha > 0U) {
        const int32_t drop_x = rig->plate_right_q4 - 9 * FTA_Q4;
        const int32_t slide = fta_min_i32(
            (int32_t)rig->sweat_y_q4,
            (rig->plate_bottom_q4 - rig->plate_top_q4) / 3);
        const int32_t drop_y = rig->plate_top_q4 + 10 * FTA_Q4 + slide;
        fta_fill_ellipse_q4(
            canvas, drop_x, drop_y, 24, 34, palette->sweat,
            rig->sweat_alpha);
        fta_fill_capsule_q4(
            canvas, drop_x, drop_y - 40, drop_x, drop_y - 16, 10,
            palette->sweat, (uint8_t)((rig->sweat_alpha * 3U) / 4U));
    }

    draw_mouth(canvas, style, &rig->mouth);

    draw_eye(canvas, style, &rig->eye[0]);
    draw_eye(canvas, style, &rig->eye[1]);

    /* brows last: they own the top of the face */
    for (int32_t side = 0; side < 2; ++side) {
        const fta_brow_t *brow = &rig->brow[side];
        fta_fill_capsule_q4(
            canvas, brow->inner_x_q4, brow->inner_y_q4,
            brow->outer_x_q4, brow->outer_y_q4,
            brow->thickness_q4, palette->brow, 32U);
    }
}
