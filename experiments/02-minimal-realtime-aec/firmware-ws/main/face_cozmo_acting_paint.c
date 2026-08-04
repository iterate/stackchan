#include "face_cozmo_acting_internal.h"

/*
 * The painter. Everything is an analytic coverage evaluation in Q4 screen
 * space with a soft ramp — no framebuffer read-back other than the
 * destination blend, no allocation, no floating point, no LUT beyond one
 * 33-entry quarter-wave sine.
 *
 * What sells the "little stage light" look compared to a flat fill:
 *   - an interior luminance field: a vertical sheen plus a gaze-following
 *     hotspot that whitens toward the core;
 *   - a rim that darkens the last couple of pixels before the silhouette;
 *   - a shadow seam under the upper lid, so the lid reads as a thing that
 *     covers the eye instead of the eye merely shrinking;
 *   - elliptical additive bloom outside the silhouette, gated by the lid
 *     cuts so closed eyes never halo;
 *   - a faint static floor glow that keys the face onto the display.
 */

static const int16_t FCA_SIN_TABLE[33] = {
    0, 804, 1606, 2404, 3196, 3981, 4756, 5520,
    6270, 7005, 7723, 8423, 9102, 9760, 10394, 11003,
    11585, 12142, 12665, 13160, 13623, 14053, 14449, 14811,
    15137, 15426, 15679, 15893, 16069, 16207, 16305, 16364,
    16384,
};

int32_t fca_sin_q14(uint32_t turn16)
{
    const uint32_t a = turn16 & 0xFFFFU;
    const uint32_t quadrant = a >> 14;
    uint32_t t = a & 0x3FFFU;
    if (quadrant == 1U || quadrant == 3U) {
        t = 0x3FFFU - t;
    }
    const uint32_t idx = t >> 9;
    const uint32_t frac = t & 0x1FFU;
    const int32_t lo = FCA_SIN_TABLE[idx];
    const int32_t hi = FCA_SIN_TABLE[idx + 1U];
    const int32_t v = lo + (int32_t)(((hi - lo) * (int32_t)frac) >> 9);
    return quadrant >= 2U ? -v : v;
}

/* Coverage 0..64 from a Q4 signed distance, ~1.5 px ramp. */
static inline int32_t fca_cov(int32_t d_q4)
{
    return fca_clamp((10 - d_q4) * 3, 0, 64);
}

/* Scale every channel of a 565 color by gain_q8 (256 == identity). */
static uint16_t fca_shade(uint16_t c, int32_t gain_q8)
{
    gain_q8 = fca_clamp(gain_q8, 0, 512);
    uint32_t r = (uint32_t)((((c >> 11) & 0x1FU) * (uint32_t)gain_q8) >> 8);
    uint32_t g = (uint32_t)((((c >> 5) & 0x3FU) * (uint32_t)gain_q8) >> 8);
    uint32_t b = (uint32_t)(((c & 0x1FU) * (uint32_t)gain_q8) >> 8);
    r = r > 0x1FU ? 0x1FU : r;
    g = g > 0x3FU ? 0x3FU : g;
    b = b > 0x1FU ? 0x1FU : b;
    return fca_rgb565(r, g, b);
}

typedef struct {
    /* Placement. */
    int32_t cx_q4;
    int32_t cy_q4;
    int32_t hw_q4;
    int32_t hh_q4;
    int32_t r_q4[4]; /* top-inner, top-outer, bottom-inner, bottom-outer */
    int32_t taper_q8;
    int32_t mirror;  /* -1 for the viewer-left eye */
    int32_t cos_q14;
    int32_t sin_q14;
    /* Lid cut lines in the mirrored local frame (x outward, y down). */
    int32_t ulid_base_q4;
    int32_t ulid_slope_q12;
    int32_t ulid_bend_q12;
    int32_t llid_base_q4;
    int32_t llid_slope_q12;
    int32_t llid_bend_q12;
    /* Interior light field. */
    int32_t hot_x_q4;   /* mirrored-local hotspot center */
    int32_t hot_y_q4;
    int32_t hot_r_q4;
    int32_t hot_gain_q8;
    int32_t sheen_q8;
    /* Colors, already graded and emissive-scaled. */
    uint16_t body;
    uint16_t core;
    uint16_t rim;
    uint16_t seam;
    /* Bloom. */
    int32_t bloom_q4;
    int32_t bloom_alpha; /* 0..64 peak */
    /* Visible band height between the lid lines at the eye center,
     * used to fade the under-lid shadow out on narrow openings. */
    int32_t band_q4;
    /* Closed-eye seam. */
    bool closed;
} fca_eye_paint_t;

/* Signed distance to the tapered round-rect in the mirrored local frame. */
static int32_t fca_eye_dist(
    const fca_eye_paint_t *e, int32_t lx, int32_t ly)
{
    /* Positive taper narrows the top: width shrinks as ly goes up. */
    int32_t hw = e->hw_q4;
    if (e->taper_q8 != 0) {
        hw += (int32_t)fca_sar64(
            (int64_t)e->taper_q8 * ly * e->hw_q4,
            8) / fca_max(e->hh_q4, 1);
        hw = fca_max(hw, e->hw_q4 / 3);
    }
    const int32_t r =
        e->r_q4[(ly < 0 ? 0 : 2) + (lx < 0 ? 0 : 1)];
    const int32_t qx = fca_abs(lx) - (hw - r);
    const int32_t qy = fca_abs(ly) - (e->hh_q4 - r);
    if (qx <= 0 && qy <= 0) {
        return fca_max(qx, qy) - r;
    }
    const int32_t px = fca_max(qx, 0);
    const int32_t py = fca_max(qy, 0);
    if (px == 0) {
        return py - r;
    }
    if (py == 0) {
        return px - r;
    }
    return (int32_t)fca_isqrt(
               (uint32_t)((int64_t)px * px + (int64_t)py * py)) - r;
}

/* Signed distance above(+)/below(-) of a quadratic lid line. */
static int32_t fca_lid_line_d(
    int32_t base_q4, int32_t slope_q12, int32_t bend_q12,
    int32_t lx, int32_t ly)
{
    int64_t line = base_q4;
    line += fca_sar64((int64_t)slope_q12 * lx, 12);
    line += fca_sar64((int64_t)bend_q12 * lx * lx, 20);
    return (int32_t)((int64_t)ly - line);
}

/* Lid coverages: 64 keeps the pixel. `soft` widens the ramp. */
static inline int32_t fca_lid_cov(int32_t d_q4, bool upper, int32_t soft)
{
    int32_t d = upper ? d_q4 : -d_q4;
    return fca_clamp((d * 48) / fca_max(soft, 8) + 32, 0, 64);
}

static uint16_t fca_eye_interior(
    const fca_eye_paint_t *e,
    int32_t lx,
    int32_t ly,
    int32_t d_body,
    int32_t ulid_d)
{
    /* Vertical sheen: brighter toward the upper interior. */
    int32_t gain = 256;
    if (e->sheen_q8 > 0) {
        const int32_t t = fca_clamp(
            ((e->hh_q4 - ly) * 128) / fca_max(2 * e->hh_q4, 1), 0, 128);
        gain += (int32_t)fca_sar64(
            (int64_t)e->sheen_q8 * (t - 40), 7);
    }
    uint16_t color = fca_shade(e->body, gain);

    /* Gaze-following hotspot whitens toward the core. */
    if (e->hot_gain_q8 > 0 && e->hot_r_q4 > 0) {
        const int32_t dx = lx - e->hot_x_q4;
        const int32_t dy = ly - e->hot_y_q4;
        const int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
        const int64_t r2 = (int64_t)e->hot_r_q4 * e->hot_r_q4;
        if (d2 < r2) {
            int32_t w = (int32_t)(((r2 - d2) << 8) / r2); /* 0..256 */
            w = (w * w) >> 8;
            const int32_t a =
                (int32_t)fca_sar64((int64_t)w * e->hot_gain_q8, 10);
            color = fca_blend565(
                color, e->core, (uint32_t)fca_clamp(a, 0, 64));
        }
    }

    /* Rim darkening over the last ~2.5 px sells convexity. */
    if (d_body > -40) {
        const int32_t t = fca_clamp(((d_body + 40) * 34) / 40, 0, 34);
        color = fca_blend565(color, e->rim, (uint32_t)t);
    }

    /* Shadow seam under the upper lid: the lid is a thing, not a crop.
     * The shadow fades out as the opening narrows so that half-lidded
     * and sleepy poses stay luminous instead of turning to mud. */
    if (ulid_d >= 0 && ulid_d < 56) {
        int32_t t = ((56 - ulid_d) * 30) / 56;
        t = (t * fca_clamp(e->band_q4, 0, 160)) / 160;
        color = fca_blend565(color, e->seam, (uint32_t)t);
    }
    return color;
}

static void fca_paint_eye(uint16_t *pixels, const fca_eye_paint_t *e)
{
    const int32_t abs_sin = fca_abs(e->sin_q14);
    const int32_t abs_cos = fca_abs(e->cos_q14);
    const int32_t ext_x = (int32_t)fca_sar64(
        (int64_t)e->hw_q4 * abs_cos + (int64_t)e->hh_q4 * abs_sin, 14) +
        e->bloom_q4 + 24;
    const int32_t ext_y = (int32_t)fca_sar64(
        (int64_t)e->hw_q4 * abs_sin + (int64_t)e->hh_q4 * abs_cos, 14) +
        e->bloom_q4 + 24;
    const int32_t x0 = fca_max(fca_sar32(e->cx_q4 - ext_x, 4), 0);
    const int32_t x1 = fca_min(
        fca_sar32(e->cx_q4 + ext_x, 4) + 1, FACE_COZMO_ACTING_WIDTH - 1);
    const int32_t y0 = fca_max(fca_sar32(e->cy_q4 - ext_y, 4), 0);
    const int32_t y1 = fca_min(
        fca_sar32(e->cy_q4 + ext_y, 4) + 1, FACE_COZMO_ACTING_HEIGHT - 1);

    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = pixels + (size_t)y * FACE_COZMO_ACTING_WIDTH;
        const int32_t py = y * FCA_Q4 + 8 - e->cy_q4;
        for (int32_t x = x0; x <= x1; ++x) {
            const int32_t px = x * FCA_Q4 + 8 - e->cx_q4;
            /* Rotate into the eye frame, then mirror x outward. */
            const int32_t rx = (int32_t)fca_sar64(
                (int64_t)e->cos_q14 * px + (int64_t)e->sin_q14 * py, 14);
            const int32_t ry = (int32_t)fca_sar64(
                -(int64_t)e->sin_q14 * px + (int64_t)e->cos_q14 * py, 14);
            const int32_t lx = e->mirror < 0 ? -rx : rx;
            const int32_t ly = ry;

            const int32_t d = fca_eye_dist(e, lx, ly);
            if (d >= e->bloom_q4 + 10) {
                continue;
            }
            const int32_t ulid_d = fca_lid_line_d(
                e->ulid_base_q4, e->ulid_slope_q12, e->ulid_bend_q12,
                lx, ly);
            const int32_t llid_d = fca_lid_line_d(
                e->llid_base_q4, e->llid_slope_q12, e->llid_bend_q12,
                lx, ly);
            const int32_t a_u = fca_lid_cov(ulid_d, true, 10);
            const int32_t a_l = fca_lid_cov(llid_d, false, 10);
            const int32_t body_a = fca_cov(d);
            const int32_t a = fca_min(body_a, fca_min(a_u, a_l));
            if (a > 0) {
                const uint16_t color =
                    fca_eye_interior(e, lx, ly, d, ulid_d);
                row[x] = fca_blend565(row[x], color, (uint32_t)a);
                if (a < 40 && body_a > a) {
                    /* Soften the transition into the lid shell. */
                    const uint16_t shell = fca_shade(e->body, 104);
                    row[x] = fca_blend565(
                        row[x], shell,
                        (uint32_t)(((body_a - a) * 12) / 64));
                }
            } else if (body_a > 0) {
                /*
                 * Lid shell: the occluded part of the eye stays as a
                 * faintly lit surface, so a heavy lid reads as a lid
                 * over a complete eye instead of a shrunken shape.
                 */
                const uint16_t shell = fca_shade(e->body, 104);
                row[x] = fca_blend565(
                    row[x], shell, (uint32_t)((body_a * 12) / 64));
            } else if (e->bloom_alpha > 0 && d > 0) {
                /* Bloom outside the silhouette, gated by soft lids. */
                const int32_t g_u = fca_lid_cov(ulid_d, true, 40);
                const int32_t g_l = fca_lid_cov(llid_d, false, 40);
                const int32_t gate = fca_min(g_u, g_l);
                if (gate > 8) {
                    const int32_t fall =
                        ((e->bloom_q4 + 10 - d) << 8) /
                        fca_max(e->bloom_q4 + 10, 1);
                    int32_t ga = (int32_t)fca_sar64(
                        (int64_t)fall * fall * e->bloom_alpha, 16);
                    ga = (ga * gate) >> 6;
                    if (ga > 0) {
                        row[x] = fca_add565(
                            row[x], e->body, (uint32_t)ga);
                    }
                }
            }
        }
    }

    /* A closed eye keeps a bright luminous seam where the lids meet,
     * so a blink or an authored shut eye reads as a glowing lash line
     * rather than a hole in the face. */
    if (e->closed) {
        const int32_t seam_y = fca_clamp(
            (e->ulid_base_q4 + e->llid_base_q4) / 2,
            -e->hh_q4, e->hh_q4);
        const int32_t half_len = (e->hw_q4 * 224) >> 8;
        const int32_t slope = fca_clamp(
            (e->ulid_slope_q12 + e->llid_slope_q12) / 2, -700, 700);
        const uint16_t seam_color = fca_shade(e->body, 214);
        const int32_t sx0 = fca_max(
            fca_sar32(e->cx_q4 - half_len - 24, 4), 0);
        const int32_t sx1 = fca_min(
            fca_sar32(e->cx_q4 + half_len + 24, 4) + 1,
            FACE_COZMO_ACTING_WIDTH - 1);
        const int32_t sy0 = fca_max(
            fca_sar32(e->cy_q4 + seam_y - 48, 4), 0);
        const int32_t sy1 = fca_min(
            fca_sar32(e->cy_q4 + seam_y + 48, 4) + 1,
            FACE_COZMO_ACTING_HEIGHT - 1);
        for (int32_t y = sy0; y <= sy1; ++y) {
            uint16_t *row = pixels + (size_t)y * FACE_COZMO_ACTING_WIDTH;
            const int32_t py = y * FCA_Q4 + 8 - e->cy_q4;
            for (int32_t x = sx0; x <= sx1; ++x) {
                const int32_t px = x * FCA_Q4 + 8 - e->cx_q4;
                const int32_t rx = (int32_t)fca_sar64(
                    (int64_t)e->cos_q14 * px +
                        (int64_t)e->sin_q14 * py, 14);
                const int32_t ry = (int32_t)fca_sar64(
                    -(int64_t)e->sin_q14 * px +
                        (int64_t)e->cos_q14 * py, 14);
                const int32_t lx = e->mirror < 0 ? -rx : rx;
                const int32_t xc =
                    fca_clamp(lx, -half_len, half_len);
                const int32_t yc = seam_y +
                    (int32_t)fca_sar64((int64_t)slope * xc, 12);
                const int32_t ex = lx - xc;
                const int32_t ey = ry - yc;
                const int32_t dist = (int32_t)fca_isqrt(
                    (uint32_t)((int64_t)ex * ex + (int64_t)ey * ey));
                const int32_t a = fca_cov(dist - 14);
                if (a > 0) {
                    row[x] = fca_blend565(
                        row[x], seam_color, (uint32_t)a);
                } else if (dist - 14 < 40) {
                    const int32_t ga = ((40 - (dist - 14)) * 7) / 40;
                    if (ga > 0) {
                        row[x] = fca_add565(
                            row[x], seam_color, (uint32_t)ga);
                    }
                }
            }
        }
    }
}

/* ---- background -------------------------------------------------------- */

static void fca_paint_background(
    const fca_profile_def_t *def, uint16_t *pixels)
{
    for (int32_t i = 0; i < FACE_COZMO_ACTING_PIXEL_COUNT; ++i) {
        pixels[i] = 0x0000U;
    }
    if (def->palette.floor_glow == 0U) {
        return;
    }
    /* Static elliptical wash behind the face; never reaches the border. */
    const int32_t cx = FACE_COZMO_ACTING_WIDTH / 2;
    const int32_t cy = def->eye_cy + 8;
    const int32_t rx = 66;
    const int32_t ry = 40;
    const int32_t y0 = fca_max(cy - ry + 1, FACE_COZMO_ACTING_SAFE_MARGIN);
    const int32_t y1 = fca_min(
        cy + ry - 1,
        FACE_COZMO_ACTING_HEIGHT - 1 - FACE_COZMO_ACTING_SAFE_MARGIN);
    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = pixels + (size_t)y * FACE_COZMO_ACTING_WIDTH;
        const int32_t dy = ((y - cy) * 256) / ry;
        const int32_t x0 = fca_max(
            cx - rx + 1, FACE_COZMO_ACTING_SAFE_MARGIN);
        const int32_t x1 = fca_min(
            cx + rx - 1,
            FACE_COZMO_ACTING_WIDTH - 1 - FACE_COZMO_ACTING_SAFE_MARGIN);
        for (int32_t x = x0; x <= x1; ++x) {
            const int32_t dx = ((x - cx) * 256) / rx;
            const int32_t d2 = (dx * dx + dy * dy) >> 8; /* 0..~512 */
            if (d2 >= 256) {
                continue;
            }
            const int32_t w = 256 - d2;
            const int32_t a = (w * w) >> 13; /* 0..8 */
            if (a > 0) {
                row[x] = fca_add565(
                    row[x], def->palette.floor_glow, (uint32_t)a);
            }
        }
    }
}

/* ---- mouth -------------------------------------------------------------- */

static void fca_paint_mouth(
    const fca_profile_def_t *def,
    const face_cozmo_acting_pose_t *pose,
    uint16_t *pixels,
    uint16_t lip_color,
    uint16_t interior_color,
    uint16_t teeth_color)
{
    if (def->has_mouth == 0U || pose->mouth_hw_q4 <= 0) {
        return;
    }
    const int32_t hw = pose->mouth_hw_q4;
    /* Corner lift from the curve: positive curls the ends upward. */
    const int32_t arch_q12 = -(pose->mouth_curve_q8 * 5) / 2;
    const int32_t open_h = pose->mouth_open_q4;
    const int32_t lip_th = 26; /* Q4 half thickness of the lip stroke */

    const int32_t reach_y =
        open_h + (fca_abs(arch_q12) * ((hw >> 4) * (hw >> 4))) / 4096 +
        lip_th + 40;
    const int32_t x0 = fca_max(
        fca_sar32(pose->mouth_cx_q4 - hw - 32, 4), 0);
    const int32_t x1 = fca_min(
        fca_sar32(pose->mouth_cx_q4 + hw + 32, 4) + 1,
        FACE_COZMO_ACTING_WIDTH - 1);
    const int32_t y0 = fca_max(
        fca_sar32(pose->mouth_cy_q4 - reach_y, 4), 0);
    const int32_t y1 = fca_min(
        fca_sar32(pose->mouth_cy_q4 + reach_y, 4) + 1,
        FACE_COZMO_ACTING_HEIGHT - 1);

    for (int32_t y = y0; y <= y1; ++y) {
        uint16_t *row = pixels + (size_t)y * FACE_COZMO_ACTING_WIDTH;
        const int32_t dy = y * FCA_Q4 + 8 - pose->mouth_cy_q4;
        for (int32_t x = x0; x <= x1; ++x) {
            const int32_t dx = x * FCA_Q4 + 8 - pose->mouth_cx_q4;
            const int32_t cx = fca_clamp(dx, -hw, hw);
            /* Lip midline: arch plus rounded taper toward the corners. */
            const int32_t mid = (int32_t)fca_sar64(
                (int64_t)arch_q12 * cx * cx, 20);
            /* Normalized span 0..256 across the mouth. */
            const int32_t span =
                256 - fca_clamp(
                          (fca_abs(cx) * 256) / fca_max(hw, 1), 0, 256);
            /* Opening envelope: parabolic, widest at the center. */
            const int32_t env =
                (fca_smooth_q8(span) * open_h) >> 8;
            const int32_t upper = mid - env / 2;
            const int32_t lower = mid + env;
            /* Accent mouths stay a pure curve until a real gasp. */
            const int32_t open_gate =
                def->mouth_verve >= 200U ? 12 : 38;
            if (env > open_gate) {
                /* Interior: a lit cavity, brighter toward center. */
                if (dy > upper && dy < lower) {
                    uint16_t c = interior_color;
                    const int32_t depth =
                        ((dy - upper) * 256) /
                        fca_max(lower - upper, 1);
                    if (pose->mouth_teeth_q8 > 96 && depth < 104) {
                        c = fca_blend565(
                            c, teeth_color,
                            (uint32_t)fca_clamp(
                                (pose->mouth_teeth_q8 - 96) / 3, 0,
                                56));
                    } else if (depth > 168) {
                        /* Tongue-side warmth at the cavity floor. */
                        c = fca_blend565(c, lip_color, 14U);
                    }
                    row[x] = fca_blend565(row[x], c, 62U);
                    continue;
                }
                /* Lip strokes around the opening; the stroke thins
                 * with a small opening so a gasp stays a clean ring
                 * instead of two touching blobs. */
                const int32_t th_open =
                    fca_min(lip_th, 8 + env / 3);
                const int32_t du = fca_abs(dy - upper);
                const int32_t dl = fca_abs(dy - lower);
                const int32_t dd = fca_min(du, dl) - th_open +
                                   fca_max(0, fca_abs(dx) - hw);
                const int32_t a = fca_cov(dd);
                if (a > 0) {
                    row[x] = fca_blend565(
                        row[x], lip_color, (uint32_t)a);
                } else if (dd < 60) {
                    const int32_t ga = ((60 - dd) * 6) / 60;
                    row[x] = fca_add565(
                        row[x], lip_color, (uint32_t)fca_max(ga, 0));
                }
                continue;
            }
            /* Closed mouth: a single glowing curve. */
            const int32_t ex = dx - cx;
            const int32_t ey = dy - mid;
            const int32_t dist = (int32_t)fca_isqrt(
                (uint32_t)((int64_t)ex * ex + (int64_t)ey * ey));
            const int32_t a = fca_cov(dist - lip_th);
            if (a > 0) {
                row[x] = fca_blend565(row[x], lip_color, (uint32_t)a);
            } else if (dist - lip_th < 50) {
                const int32_t ga = ((50 - (dist - lip_th)) * 5) / 50;
                if (ga > 0) {
                    row[x] = fca_add565(
                        row[x], lip_color, (uint32_t)ga);
                }
            }
        }
    }
}

/* ---- entry -------------------------------------------------------------- */

void fca_paint(
    const fca_profile_def_t *def,
    const face_cozmo_acting_pose_t *pose,
    uint16_t *rgb565)
{
    fca_paint_background(def, rgb565);

    /* Grade the palette by valence, then scale by the emissive level.
     * The 3/2 gain makes the warm/cool shift clearly visible on a
     * 565 panel without ever leaving the profile's color family. */
    const int32_t grade = pose->grade_q8;
    uint16_t body = def->palette.body;
    if (grade > 0) {
        body = fca_mix565(
            body, def->palette.warm_body, (grade * 3) / 2);
    } else if (grade < 0) {
        body = fca_mix565(
            body, def->palette.cool_body, (-grade * 3) / 2);
    }
    const int32_t emissive = pose->emissive_q8;
    const uint16_t body_lit = fca_shade(body, emissive);
    const uint16_t core_lit = fca_shade(
        fca_mix565(def->palette.core, body, fca_abs(grade) / 3),
        fca_min(emissive + 24, 320));
    const uint16_t rim_lit = fca_shade(
        fca_mix565(def->palette.rim, body, fca_abs(grade) / 4),
        emissive);
    const uint16_t seam_lit = fca_shade(def->palette.seam, emissive);

    /* Both eyes share the face rotation. */
    const int32_t wrapped = pose->roll_mdeg % 360000;
    const uint32_t turn =
        (uint32_t)(((int64_t)(wrapped + 360000) << 16) / 360000);
    const int32_t cos_q14 = fca_cos_q14(turn);
    const int32_t sin_q14 = fca_sin_q14(turn);

    for (int eye = 0; eye < 2; ++eye) {
        fca_eye_paint_t e;
        e.cx_q4 = pose->eye_cx_q4[eye];
        e.cy_q4 = pose->eye_cy_q4[eye];
        e.hw_q4 = fca_max(pose->eye_hw_q4[eye], 3 * FCA_Q4);
        e.hh_q4 = fca_max(pose->eye_hh_q4[eye], 3 * FCA_Q4);
        for (int corner = 0; corner < 4; ++corner) {
            e.r_q4[corner] = pose->corner_q4[eye][corner];
        }
        e.taper_q8 = pose->taper_q8[eye];
        e.mirror = eye == 0 ? -1 : 1;
        e.cos_q14 = cos_q14;
        e.sin_q14 = sin_q14;

        /* Lid bases from drop/raise fractions of the full height. */
        e.ulid_base_q4 = -e.hh_q4 +
            (int32_t)fca_sar64(
                (int64_t)2 * e.hh_q4 *
                    fca_clamp(pose->upper_drop_q8[eye], 0, 300), 8);
        e.llid_base_q4 = e.hh_q4 -
            (int32_t)fca_sar64(
                (int64_t)2 * e.hh_q4 *
                    fca_clamp(pose->lower_raise_q8[eye], 0, 300), 8);
        e.ulid_slope_q12 = pose->upper_slope_q12[eye];
        e.ulid_bend_q12 = pose->upper_bend_q12[eye];
        e.llid_slope_q12 = pose->lower_slope_q12[eye];
        e.llid_bend_q12 = pose->lower_bend_q12[eye];

        /*
         * The hotspot is the pupil analog: it tracks gaze with real
         * travel (half the eye's width) and is clamped to stay inside
         * the lid aperture, so a drifting look never slides the focus
         * point under a lid or outside the silhouette.
         */
        e.hot_r_q4 = (int32_t)fca_sar64(
            (int64_t)fca_min(e.hw_q4, e.hh_q4) *
                pose->hotspot_size_q8, 8);
        const int32_t hot_dx_q4 =
            (pose->gaze_x_q8 * (e.hw_q4 / 2)) / 256;
        const int32_t hot_dy_q4 =
            (pose->gaze_y_q8 * (e.hh_q4 * 2 / 5)) / 256 - e.hh_q4 / 6;
        int32_t hot_x = e.mirror < 0 ? -hot_dx_q4 : hot_dx_q4;
        int32_t hot_y = hot_dy_q4;
        const int32_t keep_x = e.hw_q4 - e.hot_r_q4 / 3 - 8;
        hot_x = fca_clamp(hot_x, -keep_x, keep_x);
        const int32_t lid_top = e.ulid_base_q4 + e.hot_r_q4 / 3;
        const int32_t lid_bottom = e.llid_base_q4 - e.hot_r_q4 / 3;
        if (lid_top < lid_bottom) {
            hot_y = fca_clamp(hot_y, lid_top, lid_bottom);
        } else {
            hot_y = (e.ulid_base_q4 + e.llid_base_q4) / 2;
        }
        e.hot_x_q4 = hot_x;
        e.hot_y_q4 = hot_y;
        e.hot_gain_q8 = pose->hotspot_gain_q8;
        e.sheen_q8 = def->sheen_q8;

        e.body = body_lit;
        e.core = core_lit;
        e.rim = rim_lit;
        e.seam = seam_lit;

        e.bloom_q4 = pose->bloom_q4;
        e.bloom_alpha = fca_clamp(8 + emissive / 24, 0, 22);
        e.band_q4 = e.llid_base_q4 - e.ulid_base_q4;
        /* Closed when logically blinked out or geometrically crossed. */
        e.closed = pose->aperture_q8[eye] < 16 || e.band_q4 <= 12;

        fca_paint_eye(rgb565, &e);
    }

    if (def->has_mouth != 0U) {
        const uint16_t lip = fca_shade(
            grade > 0
                ? fca_mix565(def->palette.mouth,
                             def->palette.warm_body, grade / 2)
                : fca_mix565(def->palette.mouth,
                             def->palette.cool_body, -grade / 2),
            emissive);
        const uint16_t interior = fca_shade(lip, 148);
        fca_paint_mouth(
            def, pose, rgb565, lip, interior, core_lit);
    }
}
