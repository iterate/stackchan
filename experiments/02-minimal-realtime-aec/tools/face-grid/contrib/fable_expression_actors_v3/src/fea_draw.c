#include "fea_internal.h"

/*
 * Integer scanline/bbox compositor. All primitives clamp to the frame,
 * blend in RGB565 with 0..32 alpha, and anti-alias edges from Q4
 * fractional coverage. No divides in per-pixel loops except where a
 * reciprocal is hoisted per primitive.
 */

uint16_t fea_blend565(uint16_t bg, uint16_t fg, uint32_t alpha_0_32)
{
    if (alpha_0_32 >= 32U) {
        return fg;
    }
    if (alpha_0_32 == 0U) {
        return bg;
    }
    const uint32_t inv = 32U - alpha_0_32;
    const uint32_t bg_r = (bg >> 11) & 0x1fU;
    const uint32_t bg_g = (bg >> 5) & 0x3fU;
    const uint32_t bg_b = bg & 0x1fU;
    const uint32_t fg_r = (fg >> 11) & 0x1fU;
    const uint32_t fg_g = (fg >> 5) & 0x3fU;
    const uint32_t fg_b = fg & 0x1fU;
    const uint32_t r = (fg_r * alpha_0_32 + bg_r * inv + 16U) >> 5;
    const uint32_t g = (fg_g * alpha_0_32 + bg_g * inv + 16U) >> 5;
    const uint32_t b = (fg_b * alpha_0_32 + bg_b * inv + 16U) >> 5;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void fea_fill(fea_canvas_t *canvas, uint16_t color)
{
    for (int32_t index = 0; index < FEA_PIXEL_COUNT; ++index) {
        canvas->pixels[index] = color;
    }
}

void fea_fill_rect(
    fea_canvas_t *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t color, uint32_t alpha)
{
    x0 = fea_clamp_i32(x0, 0, FEA_FRAME_WIDTH);
    x1 = fea_clamp_i32(x1, 0, FEA_FRAME_WIDTH);
    y0 = fea_clamp_i32(y0, 0, FEA_FRAME_HEIGHT);
    y1 = fea_clamp_i32(y1, 0, FEA_FRAME_HEIGHT);
    for (int32_t y = y0; y < y1; ++y) {
        uint16_t *row = canvas->pixels + y * FEA_FRAME_WIDTH;
        for (int32_t x = x0; x < x1; ++x) {
            row[x] = fea_blend565(row[x], color, alpha);
        }
    }
}

void fea_hspan_q4(
    fea_canvas_t *canvas, int32_t y, int32_t x0_q4, int32_t x1_q4,
    uint16_t color, uint32_t alpha)
{
    if (y < 0 || y >= FEA_FRAME_HEIGHT || x1_q4 <= x0_q4 ||
        alpha == 0U) {
        return;
    }
    x0_q4 = fea_clamp_i32(x0_q4, 0, FEA_FRAME_WIDTH << 4);
    x1_q4 = fea_clamp_i32(x1_q4, 0, FEA_FRAME_WIDTH << 4);
    uint16_t *row = canvas->pixels + y * FEA_FRAME_WIDTH;
    const int32_t first = x0_q4 >> 4;
    const int32_t last = (x1_q4 - 1) >> 4;
    if (first == last) {
        const uint32_t cover = (uint32_t)(x1_q4 - x0_q4) * 2U;
        row[first] = fea_blend565(
            row[first], color, (cover * alpha) >> 5);
        return;
    }
    const uint32_t head = (uint32_t)(16 - (x0_q4 & 15)) * 2U;
    row[first] = fea_blend565(row[first], color, (head * alpha) >> 5);
    for (int32_t x = first + 1; x < last; ++x) {
        row[x] = fea_blend565(row[x], color, alpha);
    }
    const uint32_t tail = (uint32_t)(((x1_q4 - 1) & 15) + 1) * 2U;
    row[last] = fea_blend565(row[last], color, (tail * alpha) >> 5);
}

static void fea_vspan_q4(
    fea_canvas_t *canvas, int32_t x, int32_t y0_q4, int32_t y1_q4,
    uint16_t color, uint32_t alpha)
{
    if (x < 0 || x >= FEA_FRAME_WIDTH || y1_q4 <= y0_q4 || alpha == 0U) {
        return;
    }
    y0_q4 = fea_clamp_i32(y0_q4, 0, FEA_FRAME_HEIGHT << 4);
    y1_q4 = fea_clamp_i32(y1_q4, 0, FEA_FRAME_HEIGHT << 4);
    if (y1_q4 <= y0_q4) {
        return;
    }
    uint16_t *pixels = canvas->pixels;
    const int32_t first = y0_q4 >> 4;
    const int32_t last = (y1_q4 - 1) >> 4;
    if (first == last) {
        const uint32_t cover = (uint32_t)(y1_q4 - y0_q4) * 2U;
        uint16_t *p = pixels + first * FEA_FRAME_WIDTH + x;
        *p = fea_blend565(*p, color, (cover * alpha) >> 5);
        return;
    }
    uint16_t *p = pixels + first * FEA_FRAME_WIDTH + x;
    const uint32_t head = (uint32_t)(16 - (y0_q4 & 15)) * 2U;
    *p = fea_blend565(*p, color, (head * alpha) >> 5);
    for (int32_t y = first + 1; y < last; ++y) {
        p = pixels + y * FEA_FRAME_WIDTH + x;
        *p = fea_blend565(*p, color, alpha);
    }
    p = pixels + last * FEA_FRAME_WIDTH + x;
    const uint32_t tail = (uint32_t)(((y1_q4 - 1) & 15) + 1) * 2U;
    *p = fea_blend565(*p, color, (tail * alpha) >> 5);
}

/* Ellipse half-width at a Q4 vertical offset, in Q4. */
static int32_t ellipse_half_q4(
    int32_t dy_q4, int32_t rx_q4, int32_t ry_q4)
{
    const int64_t remain =
        (int64_t)ry_q4 * ry_q4 - (int64_t)dy_q4 * dy_q4;
    if (remain <= 0) {
        return -1;
    }
    return (int32_t)(
        ((int64_t)rx_q4 * fea_isqrt64(remain << 8)) / ((int64_t)ry_q4 << 4));
}

void fea_ellipse_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t rx_q4, int32_t ry_q4, uint16_t color, uint32_t alpha)
{
    if (rx_q4 <= 0 || ry_q4 <= 0) {
        return;
    }
    const int32_t y_top = (cy_q4 - ry_q4) >> 4;
    const int32_t y_bottom = (cy_q4 + ry_q4 + 15) >> 4;
    for (int32_t y = y_top; y <= y_bottom; ++y) {
        const int32_t dy_q4 = y * 16 + 8 - cy_q4;
        const int32_t half = ellipse_half_q4(dy_q4, rx_q4, ry_q4);
        if (half > 0) {
            fea_hspan_q4(
                canvas, y, cx_q4 - half, cx_q4 + half, color, alpha);
        }
    }
}

void fea_roundrect_q4(
    fea_canvas_t *canvas, int32_t left_q4, int32_t top_q4,
    int32_t right_q4, int32_t bottom_q4, int32_t radius_q4,
    uint16_t color, uint32_t alpha)
{
    if (right_q4 <= left_q4 || bottom_q4 <= top_q4) {
        return;
    }
    const int32_t max_radius =
        (right_q4 - left_q4 < bottom_q4 - top_q4
             ? right_q4 - left_q4 : bottom_q4 - top_q4) / 2;
    if (radius_q4 > max_radius) {
        radius_q4 = max_radius;
    }
    const int32_t y_top = top_q4 >> 4;
    const int32_t y_bottom = (bottom_q4 + 15) >> 4;
    for (int32_t y = y_top; y <= y_bottom; ++y) {
        const int32_t yc_q4 = y * 16 + 8;
        if (yc_q4 < top_q4 || yc_q4 >= bottom_q4) {
            continue;
        }
        int32_t inset = 0;
        if (radius_q4 > 0) {
            int32_t d = 0;
            if (yc_q4 < top_q4 + radius_q4) {
                d = top_q4 + radius_q4 - yc_q4;
            } else if (yc_q4 > bottom_q4 - radius_q4) {
                d = yc_q4 - (bottom_q4 - radius_q4);
            }
            if (d > 0) {
                const int64_t remain =
                    (int64_t)radius_q4 * radius_q4 - (int64_t)d * d;
                if (remain <= 0) {
                    continue;
                }
                inset = radius_q4 - (int32_t)fea_isqrt64(remain);
            }
        }
        fea_hspan_q4(
            canvas, y, left_q4 + inset, right_q4 - inset, color, alpha);
    }
}

void fea_stroke_q4(
    fea_canvas_t *canvas, int32_t x0_q4, int32_t y0_q4,
    int32_t x1_q4, int32_t y1_q4, int32_t thickness_q4,
    uint16_t color, uint32_t alpha)
{
    const int32_t r_q4 = thickness_q4 / 2;
    if (r_q4 <= 0 || alpha == 0U) {
        return;
    }
    const int32_t min_x =
        ((x0_q4 < x1_q4 ? x0_q4 : x1_q4) - r_q4 - 16) >> 4;
    const int32_t max_x =
        ((x0_q4 > x1_q4 ? x0_q4 : x1_q4) + r_q4 + 16) >> 4;
    const int32_t min_y =
        ((y0_q4 < y1_q4 ? y0_q4 : y1_q4) - r_q4 - 16) >> 4;
    const int32_t max_y =
        ((y0_q4 > y1_q4 ? y0_q4 : y1_q4) + r_q4 + 16) >> 4;
    const int64_t vx = x1_q4 - x0_q4;
    const int64_t vy = y1_q4 - y0_q4;
    const int64_t len2 = vx * vx + vy * vy;
    const int64_t r_in = (int64_t)(r_q4 > 8 ? r_q4 - 8 : 0);
    const int64_t r_out = (int64_t)r_q4 + 8;
    const int64_t r_in2 = r_in * r_in;
    const int64_t r_out2 = r_out * r_out;
    const int64_t denom = r_out2 - r_in2;
    const int64_t recip =
        denom > 0 ? (((int64_t)1 << 20) + denom - 1) / denom : 0;
    for (int32_t y = min_y < 0 ? 0 : min_y;
         y <= (max_y >= FEA_FRAME_HEIGHT ? FEA_FRAME_HEIGHT - 1 : max_y);
         ++y) {
        uint16_t *row = canvas->pixels + y * FEA_FRAME_WIDTH;
        const int64_t py = y * 16 + 8 - y0_q4;
        for (int32_t x = min_x < 0 ? 0 : min_x;
             x <= (max_x >= FEA_FRAME_WIDTH ? FEA_FRAME_WIDTH - 1 : max_x);
             ++x) {
            const int64_t px = x * 16 + 8 - x0_q4;
            int64_t t_num = px * vx + py * vy;
            if (t_num < 0) {
                t_num = 0;
            } else if (len2 > 0 && t_num > len2) {
                t_num = len2;
            }
            int64_t dx;
            int64_t dy;
            if (len2 > 0) {
                dx = px - (vx * t_num) / len2;
                dy = py - (vy * t_num) / len2;
            } else {
                dx = px;
                dy = py;
            }
            const int64_t d2 = dx * dx + dy * dy;
            if (d2 >= r_out2) {
                continue;
            }
            uint32_t a = alpha;
            if (d2 > r_in2) {
                const int64_t f_q8 =
                    (r_out2 - d2) * recip >> 12;   /* 0..256 */
                a = (uint32_t)((f_q8 * (int64_t)alpha) >> 8);
                if (a > alpha) {
                    a = alpha;
                }
            }
            row[x] = fea_blend565(row[x], color, a);
        }
    }
}

void fea_triangle_q4(
    fea_canvas_t *canvas, int32_t x0_q4, int32_t y0_q4,
    int32_t x1_q4, int32_t y1_q4, int32_t x2_q4, int32_t y2_q4,
    uint16_t color, uint32_t alpha)
{
    /* sort by y */
    if (y1_q4 < y0_q4) {
        int32_t t = y0_q4; y0_q4 = y1_q4; y1_q4 = t;
        t = x0_q4; x0_q4 = x1_q4; x1_q4 = t;
    }
    if (y2_q4 < y0_q4) {
        int32_t t = y0_q4; y0_q4 = y2_q4; y2_q4 = t;
        t = x0_q4; x0_q4 = x2_q4; x2_q4 = t;
    }
    if (y2_q4 < y1_q4) {
        int32_t t = y1_q4; y1_q4 = y2_q4; y2_q4 = t;
        t = x1_q4; x1_q4 = x2_q4; x2_q4 = t;
    }
    if (y2_q4 <= y0_q4) {
        return;
    }
    const int32_t y_top = y0_q4 >> 4;
    const int32_t y_bottom = (y2_q4 + 15) >> 4;
    for (int32_t y = y_top; y <= y_bottom; ++y) {
        const int32_t yc = y * 16 + 8;
        if (yc < y0_q4 || yc >= y2_q4) {
            continue;
        }
        /* long edge 0->2 */
        const int32_t xa = x0_q4 +
            (int32_t)((int64_t)(x2_q4 - x0_q4) * (yc - y0_q4) /
                      (y2_q4 - y0_q4));
        int32_t xb;
        if (yc < y1_q4 && y1_q4 > y0_q4) {
            xb = x0_q4 +
                (int32_t)((int64_t)(x1_q4 - x0_q4) * (yc - y0_q4) /
                          (y1_q4 - y0_q4));
        } else if (y2_q4 > y1_q4) {
            xb = x1_q4 +
                (int32_t)((int64_t)(x2_q4 - x1_q4) * (yc - y1_q4) /
                          (y2_q4 - y1_q4));
        } else {
            xb = x1_q4;
        }
        const int32_t left = xa < xb ? xa : xb;
        const int32_t right = xa < xb ? xb : xa;
        fea_hspan_q4(canvas, y, left, right, color, alpha);
    }
}

void fea_glow_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t core_q4, int32_t halo_q4, uint16_t color,
    uint32_t core_alpha)
{
    if (halo_q4 <= core_q4) {
        halo_q4 = core_q4 + 16;
    }
    const int64_t core2 = (int64_t)core_q4 * core_q4;
    const int64_t halo2 = (int64_t)halo_q4 * halo_q4;
    const int64_t denom = halo2 - core2;
    const int64_t recip = (((int64_t)1 << 20) + denom - 1) / denom;
    const int32_t min_x = (cx_q4 - halo_q4) >> 4;
    const int32_t max_x = (cx_q4 + halo_q4 + 15) >> 4;
    const int32_t min_y = (cy_q4 - halo_q4) >> 4;
    const int32_t max_y = (cy_q4 + halo_q4 + 15) >> 4;
    for (int32_t y = min_y < 0 ? 0 : min_y;
         y <= (max_y >= FEA_FRAME_HEIGHT ? FEA_FRAME_HEIGHT - 1 : max_y);
         ++y) {
        uint16_t *row = canvas->pixels + y * FEA_FRAME_WIDTH;
        const int64_t dy = y * 16 + 8 - cy_q4;
        const int64_t dy2 = dy * dy;
        if (dy2 >= halo2) {
            continue;
        }
        for (int32_t x = min_x < 0 ? 0 : min_x;
             x <= (max_x >= FEA_FRAME_WIDTH ? FEA_FRAME_WIDTH - 1 : max_x);
             ++x) {
            const int64_t dx = x * 16 + 8 - cx_q4;
            const int64_t d2 = dx * dx + dy2;
            if (d2 >= halo2) {
                continue;
            }
            uint32_t a = core_alpha;
            if (d2 > core2) {
                const int64_t f = (halo2 - d2) * recip >> 12;   /* Q8 */
                a = (uint32_t)((((f * f) >> 8) *
                                (int64_t)core_alpha) >> 8);
                if (a > core_alpha) {
                    a = core_alpha;
                }
            }
            if (a > 0U) {
                row[x] = fea_blend565(row[x], color, a);
            }
        }
    }
}

void fea_ring_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t radius_q4, int32_t thickness_q4, uint16_t color,
    uint32_t alpha)
{
    const int32_t half = thickness_q4 / 2;
    if (half <= 0 || radius_q4 <= 0) {
        return;
    }
    const int32_t inner = radius_q4 - half;
    const int32_t outer = radius_q4 + half;
    const int64_t inner2 = (int64_t)(inner > 0 ? inner : 0) *
        (inner > 0 ? inner : 0);
    const int64_t outer2 = (int64_t)outer * outer;
    const int32_t min_x = (cx_q4 - outer) >> 4;
    const int32_t max_x = (cx_q4 + outer + 15) >> 4;
    const int32_t min_y = (cy_q4 - outer) >> 4;
    const int32_t max_y = (cy_q4 + outer + 15) >> 4;
    const int64_t edge = 16 * 2 * (int64_t)radius_q4;  /* ~1px AA band */
    for (int32_t y = min_y < 0 ? 0 : min_y;
         y <= (max_y >= FEA_FRAME_HEIGHT ? FEA_FRAME_HEIGHT - 1 : max_y);
         ++y) {
        uint16_t *row = canvas->pixels + y * FEA_FRAME_WIDTH;
        const int64_t dy = y * 16 + 8 - cy_q4;
        const int64_t dy2 = dy * dy;
        if (dy2 >= outer2) {
            continue;
        }
        for (int32_t x = min_x < 0 ? 0 : min_x;
             x <= (max_x >= FEA_FRAME_WIDTH ? FEA_FRAME_WIDTH - 1 : max_x);
             ++x) {
            const int64_t dx = x * 16 + 8 - cx_q4;
            const int64_t d2 = dx * dx + dy2;
            if (d2 >= outer2 || d2 <= inner2) {
                continue;
            }
            uint32_t a = alpha;
            const int64_t to_outer = outer2 - d2;
            const int64_t to_inner = d2 - inner2;
            if (to_outer < edge) {
                a = (uint32_t)((int64_t)a * to_outer / edge);
            } else if (to_inner < edge) {
                a = (uint32_t)((int64_t)a * to_inner / edge);
            }
            if (a > 0U) {
                row[x] = fea_blend565(row[x], color, a);
            }
        }
    }
}

/* ----------------------------------------------------------- lip mouth */

void fea_lipmouth_draw(fea_canvas_t *canvas, const fea_lipmouth_t *mouth)
{
    const int32_t lx = mouth->left_x_q4;
    const int32_t rx = mouth->right_x_q4;
    if (rx - lx < 32) {
        return;
    }
    const int32_t first_col = (lx + 8) >> 4;
    const int32_t last_col = (rx - 8) >> 4;
    const int32_t span = rx - lx;
    for (int32_t col = first_col; col <= last_col; ++col) {
        const int32_t xc_q4 = col * 16 + 8;
        int32_t t_q8 = ((xc_q4 - lx) << 8) / span;
        t_q8 = fea_clamp_i32(t_q8, 0, 256);
        const int32_t u_q8 = 256 - t_q8;
        /* quadratic bezier vertical positions */
        const int32_t w0 = (u_q8 * u_q8) >> 8;      /* (1-t)^2 */
        const int32_t w1 = (2 * u_q8 * t_q8) >> 8;  /* 2(1-t)t */
        const int32_t w2 = (t_q8 * t_q8) >> 8;      /* t^2 */
        const int32_t y_top =
            (w0 * mouth->left_y_q4 + w1 * mouth->top_ctrl_y_q4 +
             w2 * mouth->right_y_q4) >> 8;
        const int32_t y_bot =
            (w0 * mouth->left_y_q4 + w1 * mouth->bot_ctrl_y_q4 +
             w2 * mouth->right_y_q4) >> 8;
        const int32_t gap = y_bot - y_top;
        if (gap > 4) {
            /* interior */
            fea_vspan_q4(
                canvas, col, y_top, y_bot, mouth->fill_color,
                mouth->alpha);
            /* teeth: band hanging from the upper lip */
            if (mouth->teeth_q8 > 24) {
                const int32_t band =
                    (gap * ((mouth->teeth_q8 * 120) >> 8)) >> 8;
                if (band > 4) {
                    fea_vspan_q4(
                        canvas, col, y_top, y_top + band,
                        mouth->teeth_color, mouth->alpha);
                }
            }
            /* tongue: center-weighted hump from the floor */
            if (mouth->tongue_q8 > 24) {
                const int32_t hump_scale = (4 * t_q8 * u_q8) >> 8;
                const int32_t hump =
                    (gap * ((mouth->tongue_q8 * 130) >> 8) >> 8) *
                    hump_scale >> 8;
                if (hump > 4) {
                    fea_vspan_q4(
                        canvas, col, y_bot - hump, y_bot,
                        mouth->tongue_color, mouth->alpha);
                }
            }
        }
        /* lip strokes riding the same curves (parented) */
        if (mouth->lip_q4 > 0) {
            const int32_t half = mouth->lip_q4 / 2;
            fea_vspan_q4(
                canvas, col, y_top - half, y_top + half,
                mouth->lip_color, mouth->alpha);
            if (gap > 4) {
                fea_vspan_q4(
                    canvas, col, y_bot - half, y_bot + half,
                    mouth->lip_color, mouth->alpha);
            }
        }
    }
}
