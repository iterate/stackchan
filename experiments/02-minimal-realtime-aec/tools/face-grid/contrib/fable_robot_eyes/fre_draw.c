#include "fre_internal.h"

/*
 * Integer rasterizer. Shapes are signed-distance evaluations in Q4 screen
 * space with a one-pixel analytic anti-aliasing ramp; there is no
 * framebuffer read-back beyond the destination blend, no allocation, and
 * no floating point.
 */

/* Quarter-wave sine, Q14, 65 entries covering 0..pi/2. */
static const int16_t FRE_SIN_TABLE[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801,
    3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765,
    9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395,
    13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
    16384,
};

int32_t fre_sin_q14(uint32_t turn16)
{
    uint32_t a = turn16 & 0xFFFFU;
    uint32_t quadrant = a >> 14;
    uint32_t t = a & 0x3FFFU;
    if (quadrant == 1U || quadrant == 3U) {
        t = 0x3FFFU - t;
    }
    uint32_t idx = t >> 8;
    uint32_t frac = t & 0xFFU;
    int32_t lo = FRE_SIN_TABLE[idx];
    int32_t hi = FRE_SIN_TABLE[idx + 1U];
    int32_t v = lo + (int32_t)(((hi - lo) * (int32_t)frac) >> 8);
    return (quadrant >= 2U) ? -v : v;
}

void fre_fill_gradient(fre_canvas_t *c, uint16_t top, uint16_t bottom)
{
    if (top == bottom) {
        for (int32_t i = 0; i < FRE_FRAME_PIXEL_COUNT; ++i) {
            c->pixels[i] = top;
        }
        return;
    }
    uint16_t *row = c->pixels;
    for (int32_t y = 0; y < FRE_FRAME_HEIGHT; ++y) {
        uint32_t e = (uint32_t)(y * 32) / (FRE_FRAME_HEIGHT - 1);
        uint16_t color = fre_blend565(top, bottom, e);
        for (int32_t x = 0; x < FRE_FRAME_WIDTH; ++x) {
            row[x] = color;
        }
        row += FRE_FRAME_WIDTH;
    }
}

/* Coverage ramp: full at d <= -8 (half pixel inside), zero at d >= +8. */
static inline int32_t fre_coverage(int32_t d_q4)
{
    return fre_clamp((8 - d_q4) * 2, 0, 32);
}

/* Signed distance to the rounded rectangle in the local frame. */
static int32_t fre_roundrect_dist(
    const fre_eye_draw_t *e, int32_t lx, int32_t ly)
{
    int32_t r = e->r_q4[(ly < 0 ? 0 : 2) + (lx < 0 ? 0 : 1)];
    int32_t qx = fre_abs(lx) - (e->hw_q4 - r);
    int32_t qy = fre_abs(ly) - (e->hh_q4 - r);
    if (qx <= 0 && qy <= 0) {
        return fre_max(qx, qy) - r;
    }
    int32_t px = fre_max(qx, 0);
    int32_t py = fre_max(qy, 0);
    if (px == 0) {
        return py - r;
    }
    if (py == 0) {
        return px - r;
    }
    uint32_t d2 = (uint32_t)(px * px) + (uint32_t)(py * py);
    return (int32_t)fre_isqrt(d2) - r;
}

/* Lid cut coverages in the local frame: 32 keeps the pixel. */
static inline int32_t fre_lid_cut(
    int32_t base_q4,
    int32_t slope_q12,
    int32_t bend_q12,
    int32_t lx,
    int32_t ly,
    bool upper)
{
    int64_t line = (int64_t)base_q4;
    line += fre_sar64((int64_t)slope_q12 * lx, 12);
    line += fre_sar64((int64_t)bend_q12 * lx * lx, 20);
    int32_t d = (int32_t)((int64_t)ly - line);
    if (!upper) {
        d = -d;
    }
    /* Visible strictly below an upper lid / above a lower lid. */
    return fre_clamp(d * 2 + 16, 0, 32);
}

int32_t fre_eye_alpha_at(const fre_eye_draw_t *e, int32_t x_q4, int32_t y_q4)
{
    int32_t dx = x_q4 - e->cx_q4;
    int32_t dy = y_q4 - e->cy_q4;
    int32_t lx = (int32_t)fre_sar64(
        (int64_t)e->rot_cos_q14 * dx + (int64_t)e->rot_sin_q14 * dy, 14);
    int32_t ly = (int32_t)fre_sar64(
        -(int64_t)e->rot_sin_q14 * dx + (int64_t)e->rot_cos_q14 * dy, 14);
    int32_t d = fre_roundrect_dist(e, lx, ly);
    int32_t a = fre_coverage(d);
    if (a == 0) {
        return 0;
    }
    a = fre_min(a, fre_lid_cut(
        e->ulid_base_q4, e->ulid_slope_q12, e->ulid_bend_q12, lx, ly, true));
    a = fre_min(a, fre_lid_cut(
        e->llid_base_q4, e->llid_slope_q12, e->llid_bend_q12, lx, ly, false));
    return a;
}

/* Interior shading: sclera/iris/pupil/highlight color for a local point. */
static uint16_t fre_eye_interior(
    const fre_eye_draw_t *e, int32_t lx, int32_t ly, int32_t d_body)
{
    uint16_t color = e->color;
    if (e->edge_color != e->color && d_body > -48) {
        /* Rim shading over the outer three pixels sells convexity. */
        int32_t t = fre_clamp(((d_body + 48) * 20) / 48, 0, 20);
        color = fre_blend565(color, e->edge_color, (uint32_t)t);
    }
    if (e->iris_kind == FRE_IRIS_NONE) {
        return color;
    }
    int32_t ix = lx - e->iris_cx_q4;
    int32_t iy = ly - e->iris_cy_q4;
    uint32_t ir2 = (uint32_t)(ix * ix) + (uint32_t)(iy * iy);
    int32_t ir = (int32_t)fre_isqrt(ir2);
    if (e->iris_kind == FRE_IRIS_PUPIL) {
        int32_t a = fre_coverage(ir - e->pupil_r_q4);
        if (a > 0) {
            color = fre_blend565(color, e->pupil_color, (uint32_t)a);
        }
    } else if (e->iris_kind == FRE_IRIS_FULL) {
        int32_t a_iris = fre_coverage(ir - e->iris_r_q4);
        if (a_iris > 0) {
            uint16_t iris = e->iris_color;
            /* Radial darkening toward the limbus adds depth. */
            int32_t shade = fre_clamp(
                (ir * 14) / fre_max(e->iris_r_q4, 1), 0, 14);
            iris = fre_blend565(iris, e->pupil_color, (uint32_t)shade);
            color = fre_blend565(color, iris, (uint32_t)a_iris);
            int32_t a_pupil = fre_coverage(ir - e->pupil_r_q4);
            if (a_pupil > 0) {
                color = fre_blend565(
                    color, e->pupil_color, (uint32_t)a_pupil);
            }
        }
    } else if (e->iris_kind == FRE_IRIS_SLIT) {
        int32_t a_iris = fre_coverage(ir - e->iris_r_q4);
        if (a_iris > 0) {
            uint16_t iris = e->iris_color;
            int32_t shade = fre_clamp(
                (ir * 12) / fre_max(e->iris_r_q4, 1), 0, 12);
            iris = fre_blend565(iris, e->pupil_color, (uint32_t)shade);
            color = fre_blend565(color, iris, (uint32_t)a_iris);
            /* Vertical lens-shaped slit: width tapers with |iy|. */
            int32_t ay = fre_abs(iy);
            if (ay < e->iris_r_q4) {
                int64_t r2 = (int64_t)e->iris_r_q4 * e->iris_r_q4;
                int64_t taper = r2 - (int64_t)ay * ay;
                int32_t w = (int32_t)(((int64_t)e->pupil_r_q4 * taper) / r2);
                int32_t a_slit = fre_coverage(fre_abs(ix) - w);
                if (a_slit > 0) {
                    color = fre_blend565(
                        color, e->pupil_color, (uint32_t)a_slit);
                }
            }
        }
    }
    if (e->high_r_q4 > 0) {
        int32_t hx = lx - e->high_cx_q4;
        int32_t hy = ly - e->high_cy_q4;
        uint32_t h2 = (uint32_t)(hx * hx) + (uint32_t)(hy * hy);
        int32_t hd = (int32_t)fre_isqrt(h2) - e->high_r_q4;
        int32_t a_high = fre_coverage(hd);
        if (a_high > 0) {
            /* Soft specular: cap below full opacity. */
            a_high = (a_high * 28) >> 5;
            color = fre_blend565(
                color, e->highlight_color, (uint32_t)a_high);
        }
    }
    return color;
}

void fre_draw_eye(fre_canvas_t *c, const fre_eye_draw_t *e)
{
    int32_t abs_sin = fre_abs(e->rot_sin_q14);
    int32_t margin = e->glow_range_q4 + 16;
    int32_t ext_x = e->hw_q4 +
        (int32_t)(((int64_t)e->hh_q4 * abs_sin) >> 14) + margin;
    int32_t ext_y = e->hh_q4 +
        (int32_t)(((int64_t)e->hw_q4 * abs_sin) >> 14) + margin;
    int32_t x0 = fre_max(fre_sar32(e->cx_q4 - ext_x, 4), 0);
    int32_t x1 = fre_min(fre_sar32(e->cx_q4 + ext_x, 4) + 1,
        FRE_FRAME_WIDTH - 1);
    int32_t y0 = fre_max(fre_sar32(e->cy_q4 - ext_y, 4), 0);
    int32_t y1 = fre_min(fre_sar32(e->cy_q4 + ext_y, 4) + 1,
        FRE_FRAME_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = c->pixels + (size_t)y * FRE_FRAME_WIDTH;
        int32_t py = y * FRE_Q4 + 8 - e->cy_q4;
        for (int32_t x = x0; x <= x1; ++x) {
            int32_t px = x * FRE_Q4 + 8 - e->cx_q4;
            int32_t lx = (int32_t)fre_sar64(
                (int64_t)e->rot_cos_q14 * px +
                (int64_t)e->rot_sin_q14 * py, 14);
            int32_t ly = (int32_t)fre_sar64(
                -(int64_t)e->rot_sin_q14 * px +
                (int64_t)e->rot_cos_q14 * py, 14);
            int32_t d = fre_roundrect_dist(e, lx, ly);
            if (d >= e->glow_range_q4 + 8) {
                continue;
            }
            int32_t a_u = fre_lid_cut(e->ulid_base_q4, e->ulid_slope_q12,
                e->ulid_bend_q12, lx, ly, true);
            int32_t a_l = fre_lid_cut(e->llid_base_q4, e->llid_slope_q12,
                e->llid_bend_q12, lx, ly, false);
            int32_t a = fre_min(fre_coverage(d), fre_min(a_u, a_l));
            if (a > 0) {
                uint16_t color = fre_eye_interior(e, lx, ly, d);
                row[x] = fre_blend565(row[x], color, (uint32_t)a);
            } else if (e->glow_alpha > 0 && d > 0 &&
                       a_u > 16 && a_l > 16) {
                int32_t ga = (int32_t)e->glow_alpha *
                    (e->glow_range_q4 - d) / fre_max(e->glow_range_q4, 1);
                if (ga > 0) {
                    row[x] = fre_add565(row[x], e->color, (uint32_t)ga);
                }
            }
        }
    }
}

void fre_draw_capsule(
    fre_canvas_t *c,
    int32_t cx_q4,
    int32_t cy_q4,
    int32_t half_len_q4,
    int32_t slope_q12,
    int32_t arch_q12,
    int32_t half_th_q4,
    uint16_t color)
{
    int32_t end_dy = (int32_t)fre_sar64((int64_t)slope_q12 * half_len_q4, 12);
    int32_t arch_dy = (int32_t)fre_sar64(
        (int64_t)arch_q12 * half_len_q4 * half_len_q4, 20);
    int32_t lo = fre_min(0, fre_min(-end_dy + arch_dy, end_dy + arch_dy));
    int32_t hi = fre_max(0, fre_max(-end_dy + arch_dy, end_dy + arch_dy));
    int32_t pad = half_th_q4 + 16;
    int32_t x0 = fre_max(fre_sar32(cx_q4 - half_len_q4 - pad, 4), 0);
    int32_t x1 = fre_min(fre_sar32(cx_q4 + half_len_q4 + pad, 4) + 1,
        FRE_FRAME_WIDTH - 1);
    int32_t y0 = fre_max(fre_sar32(cy_q4 + lo - pad, 4), 0);
    int32_t y1 = fre_min(fre_sar32(cy_q4 + hi + pad, 4) + 1,
        FRE_FRAME_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = c->pixels + (size_t)y * FRE_FRAME_WIDTH;
        int32_t dy = y * FRE_Q4 + 8 - cy_q4;
        for (int32_t x = x0; x <= x1; ++x) {
            int32_t dx = x * FRE_Q4 + 8 - cx_q4;
            int32_t xc = fre_clamp(dx, -half_len_q4, half_len_q4);
            int64_t yc = fre_sar64((int64_t)slope_q12 * xc, 12) +
                fre_sar64((int64_t)arch_q12 * xc * xc, 20);
            int32_t ey = (int32_t)((int64_t)dy - yc);
            int32_t ex = dx - xc;
            uint32_t d2 = (uint32_t)(ex * ex) + (uint32_t)(ey * ey);
            int32_t d = (int32_t)fre_isqrt(d2) - half_th_q4;
            int32_t a = fre_coverage(d);
            if (a > 0) {
                row[x] = fre_blend565(row[x], color, (uint32_t)a);
            }
        }
    }
}

void fre_draw_disc(
    fre_canvas_t *c,
    int32_t cx_q4,
    int32_t cy_q4,
    int32_t r_q4,
    uint16_t color,
    uint8_t alpha_max)
{
    int32_t pad = 16;
    int32_t x0 = fre_max(fre_sar32(cx_q4 - r_q4 - pad, 4), 0);
    int32_t x1 = fre_min(fre_sar32(cx_q4 + r_q4 + pad, 4) + 1,
        FRE_FRAME_WIDTH - 1);
    int32_t y0 = fre_max(fre_sar32(cy_q4 - r_q4 - pad, 4), 0);
    int32_t y1 = fre_min(fre_sar32(cy_q4 + r_q4 + pad, 4) + 1,
        FRE_FRAME_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = c->pixels + (size_t)y * FRE_FRAME_WIDTH;
        int32_t dy = y * FRE_Q4 + 8 - cy_q4;
        for (int32_t x = x0; x <= x1; ++x) {
            int32_t dx = x * FRE_Q4 + 8 - cx_q4;
            uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
            int32_t d = (int32_t)fre_isqrt(d2) - r_q4;
            int32_t a = fre_coverage(d);
            if (a > 0) {
                a = (a * alpha_max) >> 5;
                if (a > 0) {
                    row[x] = fre_blend565(row[x], color, (uint32_t)a);
                }
            }
        }
    }
}
