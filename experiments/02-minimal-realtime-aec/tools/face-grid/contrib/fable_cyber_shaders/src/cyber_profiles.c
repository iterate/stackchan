#include "cyber_internal.h"

/*
 * The eleven cyber-family renderers. Almost all of them follow the same
 * two-stage pipeline that makes a software shader plausible at 30 fps on
 * a 240 MHz ESP32-S3:
 *
 *   stage A ("field"): evaluate the face as blended signed-distance
 *   fields at 80x60 (or 40x30 for the LED matrix), writing one
 *   brightness/palette byte per cell into ctx->scratch;
 *
 *   stage B ("upscale"): expand to 160x120 while applying the cheap
 *   per-pixel cyberpunk effects — palette lookup, ordered dithering,
 *   scanlines, chromatic channel offsets, rolling bars, row jitter.
 *
 * The wireframe profile instead stamps additive glow sprites along
 * segments into a full-resolution accumulation plane. Distances are Q4
 * pixels, brightness is Q8, and there is no floating point anywhere.
 */

enum {
    FIELD_W = CYBER_FIELD_WIDTH,
    FIELD_H = CYBER_FIELD_HEIGHT,
    LED_W = CYBER_LED_WIDTH,
    LED_H = CYBER_LED_HEIGHT,
    OUT_W = CYBER_FACE_WIDTH,
    OUT_H = CYBER_FACE_HEIGHT,
    /* glow_neon and glow_core decay to zero inside these radii (Q4). */
    NEON_CUTOFF_Q4 = 512,
    CORE_CUTOFF_Q4 = 512,
};

/* ------------------------------------------------------------------ */
/* Shared face layout                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t eye_l_x, eye_r_x, eye_y;      /* Q4 centres            */
    int32_t eye_hw, eye_hh_l, eye_hh_r;   /* Q4 half extents       */
    int32_t mouth_x, mouth_y;             /* Q4 centre             */
    int32_t brow_l_ax, brow_l_ay, brow_l_bx, brow_l_by;
    int32_t brow_r_ax, brow_r_ay, brow_r_bx, brow_r_by;
    int32_t curve_k_q16;                  /* mouth bend coefficient */
} layout_t;

static void layout_face(const cyber_motion_t *m, layout_t *l)
{
    int32_t gaze_x = m->gaze_x_q4 / 2;
    int32_t gaze_y = m->gaze_y_q4 / 2;
    int32_t eye_y = CYBER_Q4(46) + m->breath_y_q4 + gaze_y;

    l->eye_hw = CYBER_Q4(11);
    int32_t hh_max = 216; /* 13.5 px */
    if (m->expression == CYBER_EXPRESSION_SURPRISED) {
        l->eye_hw += 24;
        hh_max += 40;
    }
    l->eye_hh_l =
        cyber_max32(12, (hh_max * (int32_t)(255u - m->lid_close_left)) >>
                            8);
    l->eye_hh_r =
        cyber_max32(12, (hh_max * (int32_t)(255u - m->lid_close_right)) >>
                            8);
    l->eye_l_x = CYBER_Q4(42) + gaze_x;
    l->eye_r_x = CYBER_Q4(118) + gaze_x;
    l->eye_y = eye_y;

    l->mouth_x = CYBER_Q4(80) + gaze_x / 3;
    l->mouth_y = CYBER_Q4(88) + m->breath_y_q4 + gaze_y / 4;
    int32_t hw = m->mouth_half_w_q4;
    /* bend(dx) = dx^2 * k >> 16 equals mouth_curve at |dx| == hw. */
    l->curve_k_q16 = (m->mouth_curve_q4 * 65536) / (hw * hw);

    /* Brows: short capsules above each eye; tilt drops the inner end. */
    int32_t brow_y = eye_y - hh_max - CYBER_Q4(6) + m->brow_drop_q4;
    int32_t half = CYBER_Q4(12);
    int32_t tilt = (m->brow_tilt_q8 * CYBER_Q4(5)) >> 8;
    l->brow_l_ax = l->eye_l_x - half;
    l->brow_l_ay = brow_y - tilt / 2;
    l->brow_l_bx = l->eye_l_x + half;
    l->brow_l_by = brow_y + tilt / 2; /* inner end */
    l->brow_r_ax = l->eye_r_x - half;
    l->brow_r_ay = brow_y + tilt / 2; /* inner end */
    l->brow_r_bx = l->eye_r_x + half;
    l->brow_r_by = brow_y - tilt / 2;
}

/* Mouth distance: horizontal capsule bent by a parabolic centreline. */
static int32_t mouth_distance(const layout_t *l, const cyber_motion_t *m,
                              int32_t x, int32_t y)
{
    int32_t dx = x - l->mouth_x;
    int32_t bend = (int32_t)(((int64_t)dx * dx * l->curve_k_q16) >> 16);
    int32_t yy = y - l->mouth_y + bend;
    int32_t px = cyber_clamp32(dx, -m->mouth_half_w_q4,
                               m->mouth_half_w_q4);
    return cyber_length_q4(dx - px, yy) - m->mouth_half_h_q4;
}

/* ------------------------------------------------------------------ */
/* Generic 2x upscale with per-profile effects                         */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint16_t *pal;
    uint32_t chroma_off;   /* field-space channel offset, 0..3     */
    uint32_t scanline;     /* 0 none, 1 half, 2 three-quarter      */
    int32_t bar_y;         /* rolling-bar top output row, INT32_MIN=off */
    int32_t bar_h;
    int32_t bar_boost;     /* palette index boost inside the bar   */
    const int8_t *row_off; /* per-output-row field x offset or NULL */
    uint32_t dither;       /* Bayer index dither on ramp palettes  */
} upscale_fx_t;

static uint16_t dim_half(uint16_t c)
{
    return (uint16_t)((c >> 1) & 0x7BEFu);
}

static uint16_t dim_threequarter(uint16_t c)
{
    return (uint16_t)(((c >> 1) & 0x7BEFu) + ((c >> 2) & 0x39E7u));
}

static void upscale_2x(cyber_face_ctx_t *ctx, const upscale_fx_t *fx,
                       uint16_t *out)
{
    const uint8_t *field = ctx->scratch;
    const uint16_t *pal = fx->pal;
    for (int y = 0; y < OUT_H; ++y) {
        const uint8_t *row = field + (y >> 1) * FIELD_W;
        uint16_t *dst = out + y * OUT_W;
        int32_t roff = fx->row_off != NULL ? fx->row_off[y] : 0;
        int32_t boost = 0;
        if (fx->bar_y != INT32_MIN && y >= fx->bar_y &&
            y < fx->bar_y + fx->bar_h) {
            boost = fx->bar_boost;
        }
        const uint8_t *bayer = cyber_bayer8 + (y & 7) * 8;
        int32_t c_off = (int32_t)fx->chroma_off;
        for (int x = 0; x < OUT_W; ++x) {
            int32_t fxi = (x >> 1) + roff;
            fxi = cyber_clamp32(fxi, 0, FIELD_W - 1);
            int32_t idx = row[fxi];
            if (fx->dither) {
                idx += (int32_t)(bayer[x & 7] >> 3) - 4;
            }
            idx = cyber_clamp32(idx + boost, 0, 255);
            uint16_t colour;
            if (c_off != 0) {
                int32_t ri = cyber_clamp32(fxi - c_off, 0, FIELD_W - 1);
                int32_t bi = cyber_clamp32(fxi + c_off, 0, FIELD_W - 1);
                int32_t idx_r =
                    cyber_clamp32((int32_t)row[ri] + boost, 0, 255);
                int32_t idx_b =
                    cyber_clamp32((int32_t)row[bi] + boost, 0, 255);
                colour = (uint16_t)((pal[idx_r] & 0xF800u) |
                                    (pal[idx] & 0x07E0u) |
                                    (pal[idx_b] & 0x001Fu));
            } else {
                colour = pal[idx];
            }
            if (fx->scanline != 0u && (y & 1) != 0) {
                colour = fx->scanline == 1u ? dim_half(colour)
                                            : dim_threequarter(colour);
            }
            dst[x] = colour;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Neon SDF face field (also feeds CRT and glitch)                     */
/* ------------------------------------------------------------------ */

typedef enum {
    EYE_SHAPE_ROUNDED_BOX = 0, /* cyan variant  */
    EYE_SHAPE_LIDDED_DISC = 1, /* magenta variant */
} eye_shape_t;

static void field_neon_face(cyber_face_ctx_t *ctx,
                            const cyber_motion_t *m,
                            const cyber_keyframe_t *kf,
                            eye_shape_t eye_shape, int use_vignette)
{
    layout_t l;
    layout_face(m, &l);
    const uint8_t *glow = ctx->glow_neon;
    const uint8_t *core = ctx->glow_core;
    uint32_t gain = m->glow_gain_q8;
    int32_t eye_r_l = cyber_min32(l.eye_hw, l.eye_hh_l) * 3 / 4;
    int32_t eye_r_r = cyber_min32(l.eye_hw, l.eye_hh_r) * 3 / 4;
    int32_t disc_r = (l.eye_hw + 32) / 1;
    uint32_t teeth = kf->mouth_teeth;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        for (int cx = 0; cx < FIELD_W; ++cx, ++f) {
            int32_t px = (cx << 5) + 16;
            uint32_t b = 0;

            /* eyes */
            for (int side = 0; side < 2; ++side) {
                int32_t ex = side == 0 ? l.eye_l_x : l.eye_r_x;
                int32_t hh = side == 0 ? l.eye_hh_l : l.eye_hh_r;
                int32_t adx = cyber_abs32(px - ex);
                int32_t ady = cyber_abs32(py - l.eye_y);
                if (adx - l.eye_hw > NEON_CUTOFF_Q4 ||
                    ady - hh > NEON_CUTOFF_Q4) {
                    continue;
                }
                int32_t d;
                if (eye_shape == EYE_SHAPE_ROUNDED_BOX) {
                    int32_t r = side == 0 ? eye_r_l : eye_r_r;
                    d = cyber_sd_round_box(px, py, ex, l.eye_y,
                                           l.eye_hw, hh, r);
                } else {
                    /*
                     * Disc with an upper lid plane sweeping from the
                     * top of the disc (open) to its bottom (closed).
                     */
                    d = cyber_sd_circle(px, py, ex, l.eye_y, disc_r);
                    uint32_t closed = side == 0 ? m->lid_close_left
                                                : m->lid_close_right;
                    int32_t lid_y =
                        l.eye_y - disc_r +
                        (int32_t)((2u * (uint32_t)disc_r * closed) /
                                  255u);
                    d = cyber_max32(d, lid_y - py);
                }
                b = cyber_sat_add(b, cyber_glow(glow, d));
            }

            /* mouth */
            {
                int32_t ady = cyber_abs32(py - l.mouth_y);
                if (ady < m->mouth_half_h_q4 + NEON_CUTOFF_Q4 + 96) {
                    int32_t d = mouth_distance(&l, m, px, py);
                    uint32_t mb = cyber_glow(glow, d);
                    if (teeth > 96u && d < -24) {
                        /* Tooth gaps: 4 px stripes inside the mouth. */
                        int32_t stripe =
                            ((px - l.mouth_x + 2048) >> 6) & 1;
                        if (stripe != 0) {
                            mb = (mb * (320u - teeth)) >> 8;
                        }
                    }
                    b = cyber_sat_add(b, mb);
                }
            }

            /* brows */
            {
                int32_t ady = cyber_abs32(py - l.brow_l_ay);
                if (ady < CORE_CUTOFF_Q4 / 2 + 96) {
                    int32_t d1 = cyber_sd_segment(px, py, l.brow_l_ax,
                                                  l.brow_l_ay, l.brow_l_bx,
                                                  l.brow_l_by, 14);
                    int32_t d2 = cyber_sd_segment(px, py, l.brow_r_ax,
                                                  l.brow_r_ay, l.brow_r_bx,
                                                  l.brow_r_by, 14);
                    uint32_t bb = cyber_glow(core, cyber_min32(d1, d2));
                    b = cyber_sat_add(b, (bb * 200u) >> 8);
                }
            }

            b = (b * gain) >> 8;
            if (b > 255u) {
                b = 255u;
            }
            if (use_vignette) {
                uint32_t v = ((uint32_t)ctx->vignette_x[cx] *
                              ctx->vignette_y[cy]) >>
                             8;
                b = (b * v) >> 8;
            }
            *f = (uint8_t)b;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Liquid smooth-min blobs                                             */
/* ------------------------------------------------------------------ */

static void field_liquid(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                         const cyber_keyframe_t *kf)
{
    (void)kf;
    layout_t l;
    layout_face(m, &l);
    uint32_t t = m->t_ms;

    /* Blob centres wobble on separate phases for the liquid idle. */
    int32_t wob_lx = (cyber_sin_q14(ctx, t * 21u) * 22) >> 14;
    int32_t wob_ly = (cyber_sin_q14(ctx, t * 17u + 9000u) * 18) >> 14;
    int32_t wob_rx = (cyber_sin_q14(ctx, t * 19u + 30000u) * 22) >> 14;
    int32_t wob_ry = (cyber_sin_q14(ctx, t * 23u + 47000u) * 18) >> 14;

    int32_t elx = l.eye_l_x + wob_lx;
    int32_t ely = l.eye_y + wob_ly;
    int32_t erx = l.eye_r_x + wob_rx;
    int32_t ery = l.eye_y + wob_ry;
    int32_t eye_r = CYBER_Q4(11);

    /*
     * A slow free droplet drifts across the face on an elliptical
     * path, merging with each eye and the mouth in turn — this is
     * where the smooth-min blend is actually visible.
     */
    int32_t drop_x =
        CYBER_Q4(80) + ((cyber_cos_q14(ctx, t * 7u) * CYBER_Q4(34)) >>
                        14);
    int32_t drop_y =
        CYBER_Q4(64) + ((cyber_sin_q14(ctx, t * 5u + 12000u) *
                         CYBER_Q4(20)) >>
                        14);
    int32_t drop_r = CYBER_Q4(5);

    /* Squash factor turns the discs into lens shapes as lids close. */
    int32_t sq_l = 256 + (int32_t)m->lid_close_left;
    int32_t sq_r = 256 + (int32_t)m->lid_close_right;

    /* Mouth blob: jaw drops and radius grows with mouth_open. */
    int32_t mrad = CYBER_Q4(4) + ((m->mouth_half_h_q4 * 3) / 4) +
                   (m->mouth_half_w_q4 / 6);
    int32_t mx = l.mouth_x;
    int32_t my = l.mouth_y + m->mouth_half_h_q4 / 2;

    int32_t k_eyes = CYBER_Q4(9) + ((cyber_sin_q14(ctx, t * 13u) * 30) >>
                                    14);
    int32_t k_mouth = m->speaking ? CYBER_Q4(11) : CYBER_Q4(6);
    int32_t k_drop = CYBER_Q4(12);

    const uint8_t *glow = ctx->glow_neon;
    const uint8_t *core = ctx->glow_core;
    uint32_t gain = m->glow_gain_q8;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        for (int cx = 0; cx < FIELD_W; ++cx, ++f) {
            int32_t px = (cx << 5) + 16;

            int32_t dyl = ((py - ely) * sq_l) >> 8;
            int32_t dl = cyber_length_q4(px - elx, dyl) - eye_r;
            int32_t dyr = ((py - ery) * sq_r) >> 8;
            int32_t dr = cyber_length_q4(px - erx, dyr) - eye_r;
            int32_t dm = cyber_length_q4(px - mx, py - my) - mrad;
            int32_t dd =
                cyber_length_q4(px - drop_x, py - drop_y) - drop_r;

            int32_t d = cyber_smin_q4(dl, dr, k_eyes);
            d = cyber_smin_q4(d, dm, k_mouth);
            d = cyber_smin_q4(d, dd, k_drop);

            uint32_t b = cyber_glow(glow, d);
            /* Surface-tension rim highlight on the zero contour. */
            b = cyber_sat_add(b, cyber_glow(core, cyber_abs32(d)) >> 1);
            b = (b * gain) >> 8;
            if (b > 255u) {
                b = 255u;
            }
            *f = (uint8_t)b;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Voice orb                                                           */
/* ------------------------------------------------------------------ */

static void field_voice_orb(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                            const cyber_keyframe_t *kf)
{
    uint32_t t = m->t_ms;
    int32_t cx0 = CYBER_Q4(80) + m->gaze_x_q4 / 4;
    int32_t cy0 = CYBER_Q4(60) + m->breath_y_q4 * 2 + m->gaze_y_q4 / 4;

    int32_t radius = CYBER_Q4(26) + ((cyber_sin_q14(ctx, t * 9u) * 24) >>
                                     14);
    if (m->speaking) {
        radius += ((int32_t)kf->mouth_open * CYBER_Q4(9)) >> 8;
    }
    int32_t orbit = radius + CYBER_Q4(8);
    /* Constant angular speed; speech adds bounded wobble, not rate. */
    uint32_t base_ang = t * 22u;
    int32_t wobble =
        m->speaking
            ? (cyber_sin_q14(ctx, t * 210u) * (int32_t)kf->mouth_open) >> 12
            : 0;

    int32_t sat_x[3];
    int32_t sat_y[3];
    for (int i = 0; i < 3; ++i) {
        uint32_t ang =
            base_ang + (uint32_t)i * (CYBER_TURN / 3u) + (uint32_t)wobble;
        sat_x[i] = cx0 + ((cyber_cos_q14(ctx, ang) * orbit) >> 14);
        sat_y[i] = cy0 + ((cyber_sin_q14(ctx, ang) * orbit) >> 14);
    }

    /* Inner eye dots: capsule-ish, close with the lids. */
    int32_t eye_dx = CYBER_Q4(10);
    int32_t eye_y = cy0 - CYBER_Q4(4);
    int32_t eye_hh_l =
        cyber_max32(8, (CYBER_Q4(3) * (int32_t)(255u - m->lid_close_left)) >>
                           8);
    int32_t eye_hh_r =
        cyber_max32(8, (CYBER_Q4(3) *
                        (int32_t)(255u - m->lid_close_right)) >>
                           8);

    const uint8_t *core = ctx->glow_core;
    const uint8_t *soft = ctx->glow_soft;
    uint32_t gain = m->glow_gain_q8;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        for (int cxi = 0; cxi < FIELD_W; ++cxi, ++f) {
            int32_t px = (cxi << 5) + 16;
            int32_t rlen = cyber_length_q4(px - cx0, py - cy0);

            /* ring + interior haze + centre halo */
            uint32_t b = cyber_glow(core, cyber_abs32(rlen - radius));
            b = cyber_sat_add(b,
                              cyber_glow(soft, cyber_max32(rlen - radius,
                                                           0)) >>
                                  2);
            b = cyber_sat_add(
                b, cyber_glow(soft, cyber_max32(rlen - CYBER_Q4(3), 0)) >>
                       1);

            /* satellites */
            for (int i = 0; i < 3; ++i) {
                int32_t adx = cyber_abs32(px - sat_x[i]);
                int32_t ady = cyber_abs32(py - sat_y[i]);
                if (adx > CORE_CUTOFF_Q4 || ady > CORE_CUTOFF_Q4) {
                    continue;
                }
                int32_t d = cyber_length_q4(adx, ady) - CYBER_Q4(2);
                b = cyber_sat_add(b, cyber_glow(core, d));
            }

            /* eyes */
            for (int side = 0; side < 2; ++side) {
                int32_t ex = side == 0 ? cx0 - eye_dx : cx0 + eye_dx;
                int32_t hh = side == 0 ? eye_hh_l : eye_hh_r;
                int32_t adx = cyber_abs32(px - ex);
                int32_t ady = cyber_abs32(py - eye_y);
                if (adx > CORE_CUTOFF_Q4 || ady > CORE_CUTOFF_Q4) {
                    continue;
                }
                int32_t d = cyber_sd_round_box(px, py, ex, eye_y,
                                               CYBER_Q4(3), hh,
                                               cyber_min32(CYBER_Q4(3),
                                                           hh));
                b = cyber_sat_add(b, cyber_glow(core, d));
            }

            b = (b * gain) >> 8;
            if (b > 255u) {
                b = 255u;
            }
            *f = (uint8_t)b;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Red optic (single lens)                                             */
/* ------------------------------------------------------------------ */

static void field_red_optic(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                            const cyber_keyframe_t *kf)
{
    int32_t cx0 = CYBER_Q4(80) + m->gaze_x_q4 / 2;
    int32_t cy0 = CYBER_Q4(60) + m->gaze_y_q4 / 2 + m->breath_y_q4;

    uint32_t open_q8 =
        (510u - m->lid_close_left - m->lid_close_right) / 2u;
    int32_t aperture = CYBER_Q4(2) + (int32_t)((open_q8 * CYBER_Q4(20)) >>
                                               8);

    int32_t hl_x = cx0 - CYBER_Q4(5) + m->gaze_x_q4 / 4;
    int32_t hl_y = cy0 - CYBER_Q4(5) + m->gaze_y_q4 / 4;

    uint32_t gain = m->glow_gain_q8;
    if (m->speaking) {
        gain += (uint32_t)kf->mouth_open >> 1;
    }

    const uint8_t *core = ctx->glow_core;
    const uint8_t *neon = ctx->glow_neon;
    const uint8_t *soft = ctx->glow_soft;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        for (int cxi = 0; cxi < FIELD_W; ++cxi, ++f) {
            int32_t px = (cxi << 5) + 16;
            int32_t rlen = cyber_length_q4(px - cx0, py - cy0);

            /* Hot core, dimmed by the lid shutter as the eye closes. */
            uint32_t bc =
                cyber_glow(neon, cyber_max32(rlen - CYBER_Q4(6), 0));
            int32_t lid_d = cyber_abs32(py - cy0) - aperture;
            if (lid_d > 0) {
                uint32_t shade = cyber_glow(core, lid_d);
                bc = (bc * shade) >> 8;
            }
            uint32_t b = (bc * 3u) >> 1;
            if (b > 255u) {
                b = 255u;
            }

            /* Iris rings. */
            b = cyber_sat_add(
                b, cyber_glow(core, cyber_abs32(rlen - CYBER_Q4(15))) >>
                       1);
            b = cyber_sat_add(
                b, cyber_glow(core, cyber_abs32(rlen - CYBER_Q4(26))) >>
                       2);

            /* Specular highlight dot. */
            {
                int32_t adx = cyber_abs32(px - hl_x);
                int32_t ady = cyber_abs32(py - hl_y);
                if (adx <= CORE_CUTOFF_Q4 && ady <= CORE_CUTOFF_Q4) {
                    int32_t d =
                        cyber_length_q4(adx, ady) - CYBER_Q4(1) - 8;
                    b = cyber_sat_add(b, cyber_glow(core, d));
                }
            }

            /* Ambient chassis glow. */
            b = cyber_sat_add(b, cyber_glow(soft, rlen) >> 3);

            b = (b * gain) >> 8;
            if (b > 255u) {
                b = 255u;
            }
            uint32_t v = ((uint32_t)ctx->vignette_x[cxi] *
                          ctx->vignette_y[cy]) >>
                         8;
            *f = (uint8_t)((b * v) >> 8);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Edge glow                                                           */
/* ------------------------------------------------------------------ */

static void field_edge_glow(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                            const cyber_keyframe_t *kf)
{
    layout_t l;
    layout_face(m, &l);
    uint32_t t = m->t_ms;
    uint32_t band_shift = t >> 9;

    uint32_t border_gain = 96u + ((m->glow_gain_q8 - 256u + 80u) >> 1);
    if (m->speaking) {
        border_gain += (uint32_t)kf->mouth_open >> 1;
    }
    if (border_gain > 320u) {
        border_gain = 320u;
    }

    const uint8_t *soft = ctx->glow_soft;
    const uint8_t *neon = ctx->glow_neon;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        for (int cx = 0; cx < FIELD_W; ++cx, ++f) {
            int32_t px = (cx << 5) + 16;
            int32_t e = cyber_min32(cyber_min32(px, CYBER_Q4(160) - px),
                                    cyber_min32(py, CYBER_Q4(120) - py));
            uint32_t bg = (cyber_glow(soft, e) * border_gain) >> 8;

            /* face: small eyes + mouth, tinted by the local band */
            uint32_t bf = 0;
            for (int side = 0; side < 2; ++side) {
                int32_t ex = side == 0 ? l.eye_l_x : l.eye_r_x;
                int32_t hh =
                    (side == 0 ? l.eye_hh_l : l.eye_hh_r) * 3 / 4;
                int32_t adx = cyber_abs32(px - ex);
                int32_t ady = cyber_abs32(py - l.eye_y);
                if (adx <= NEON_CUTOFF_Q4 && ady <= NEON_CUTOFF_Q4) {
                    int32_t d = cyber_sd_round_box(
                        px, py, ex, l.eye_y, l.eye_hw * 3 / 4, hh,
                        cyber_min32(l.eye_hw, hh) / 2);
                    bf = cyber_sat_add(bf, cyber_glow(neon, d));
                }
            }
            {
                int32_t d = mouth_distance(&l, m, px, py);
                bf = cyber_sat_add(bf, cyber_glow(neon, d));
            }
            bf = (bf * m->glow_gain_q8) >> 8;

            /* Border saturates below white; only the face reaches it. */
            uint32_t level5 = (bg * 28u) >> 8;
            if (level5 > 28u) {
                level5 = 28u;
            }
            uint32_t face5 = (bf * 31u) >> 8;
            if (face5 > 31u) {
                face5 = 31u;
            }
            if (face5 > level5) {
                level5 = face5;
            }
            uint32_t band =
                (band_shift + (uint32_t)((cx + cy) >> 5)) & 7u;
            *f = (uint8_t)((band << 5) | level5);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Palette plasma                                                      */
/* ------------------------------------------------------------------ */

static void field_plasma(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                         const cyber_keyframe_t *kf)
{
    layout_t l;
    layout_face(m, &l);
    uint32_t t = m->t_ms;
    uint32_t cyc = t >> 5;
    uint32_t boost = m->speaking ? ((uint32_t)kf->mouth_open >> 3) : 0u;

    const int16_t *sin_tab = ctx->sin_q14;
    uint8_t *f = ctx->scratch;

    for (int cy = 0; cy < FIELD_H; ++cy) {
        int32_t py = (cy << 5) + 16;
        uint32_t ay = (uint32_t)cy * 2185u + t * 23u;
        for (int cx = 0; cx < FIELD_W; ++cx, ++f) {
            int32_t px = (cx << 5) + 16;
            uint32_t a1 = (uint32_t)cx * 1638u + t * 37u;
            uint32_t a3 = (uint32_t)(cx + cy) * 1170u - t * 29u;
            int32_t rx = cx - 40;
            int32_t ry = cy - 30;
            uint32_t a4 = (uint32_t)(rx * rx + ry * ry) * 24u + t * 31u;

            int32_t sum =
                sin_tab[(a1 >> 6) & (CYBER_SIN_TABLE_SIZE - 1)] +
                sin_tab[(ay >> 6) & (CYBER_SIN_TABLE_SIZE - 1)] +
                sin_tab[(a3 >> 6) & (CYBER_SIN_TABLE_SIZE - 1)] +
                sin_tab[(a4 >> 6) & (CYBER_SIN_TABLE_SIZE - 1)];
            uint32_t v8 =
                (uint32_t)cyber_clamp32(128 + (sum >> 9), 0, 255);

            /* Face silhouette carved out of the plasma. */
            int32_t d;
            {
                int32_t dl = cyber_sd_round_box(
                    px, py, l.eye_l_x, l.eye_y, l.eye_hw, l.eye_hh_l,
                    cyber_min32(l.eye_hw, l.eye_hh_l) * 3 / 4);
                int32_t dr = cyber_sd_round_box(
                    px, py, l.eye_r_x, l.eye_y, l.eye_hw, l.eye_hh_r,
                    cyber_min32(l.eye_hw, l.eye_hh_r) * 3 / 4);
                int32_t dm = mouth_distance(&l, m, px, py);
                d = cyber_min32(cyber_min32(dl, dr), dm);
            }

            if (d < 0) {
                uint32_t idx = 128u + (v8 >> 1) + boost;
                if (idx > 255u) {
                    idx = 255u;
                }
                *f = (uint8_t)idx;
            } else {
                *f = (uint8_t)((v8 + cyc) & 127u);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* HUB75 LED matrix simulation                                         */
/* ------------------------------------------------------------------ */

static void field_hub75(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                        const cyber_keyframe_t *kf)
{
    layout_t l;
    layout_face(m, &l);
    (void)kf;
    const uint8_t *glow = ctx->glow_neon;
    uint32_t gain = m->glow_gain_q8;
    /* Temporal 2x2 dither pattern rotates every 32 ms. */
    uint32_t frame = m->t_ms >> 5;
    uint32_t fx_off = frame & 1u;
    uint32_t fy_off = (frame >> 1) & 1u;
    static const int32_t bayer2[4] = { -48, 16, 48, -16 };

    uint8_t *f = ctx->scratch;
    for (int cy = 0; cy < LED_H; ++cy) {
        int32_t py = cy * 64 + 32;
        for (int cx = 0; cx < LED_W; ++cx, ++f) {
            int32_t px = cx * 64 + 32;
            uint32_t b = 0;
            for (int side = 0; side < 2; ++side) {
                int32_t ex = side == 0 ? l.eye_l_x : l.eye_r_x;
                int32_t hh = side == 0 ? l.eye_hh_l : l.eye_hh_r;
                int32_t adx = cyber_abs32(px - ex);
                int32_t ady = cyber_abs32(py - l.eye_y);
                if (adx - l.eye_hw > NEON_CUTOFF_Q4 / 2 ||
                    ady - hh > NEON_CUTOFF_Q4 / 2) {
                    continue;
                }
                int32_t d = cyber_sd_round_box(
                    px, py, ex, l.eye_y, l.eye_hw, hh,
                    cyber_min32(l.eye_hw, hh) * 3 / 4);
                /* Doubled distance keeps the halo chunky and tight. */
                b = cyber_sat_add(b, cyber_glow(glow, d * 2));
            }
            {
                int32_t d = mouth_distance(&l, m, px, py);
                b = cyber_sat_add(b, cyber_glow(glow, d * 2));
            }
            b = (b * gain) >> 8;
            if (b > 255u) {
                b = 255u;
            }

            uint32_t di = ((cx + fx_off) & 1u) | (((cy + fy_off) & 1u)
                                                  << 1);
            int32_t bq = cyber_clamp32((int32_t)b + bayer2[di], 0, 255);
            uint32_t q3 = ((uint32_t)bq * 7u + 128u) >> 8;
            *f = (uint8_t)(q3 * 36u);
        }
    }
}

static void upscale_hub75(cyber_face_ctx_t *ctx, uint16_t *out)
{
    /* LED cell shading selectors: centre, inner, edge, gap. */
    static const uint8_t mask[16] = {
        3, 2, 2, 3,
        2, 0, 0, 2,
        2, 0, 0, 2,
        3, 2, 2, 3,
    };
    const uint8_t *field = ctx->scratch;
    for (int y = 0; y < OUT_H; ++y) {
        const uint8_t *row = field + (y >> 2) * LED_W;
        const uint8_t *mrow = mask + (y & 3) * 4;
        uint16_t *dst = out + y * OUT_W;
        for (int x = 0; x < OUT_W; ++x) {
            uint32_t sel = mrow[x & 3];
            dst[x] = ctx->led_palette[sel][row[x >> 2]];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Holographic wireframe (stamped additive glow lines)                 */
/* ------------------------------------------------------------------ */

static const uint8_t stamp5[25] = {
    2, 9, 15, 9, 2,
    9, 34, 62, 34, 9,
    15, 62, 118, 62, 15,
    9, 34, 62, 34, 9,
    2, 9, 15, 9, 2,
};

static const uint8_t stamp_node[25] = {
    6, 20, 32, 20, 6,
    20, 70, 110, 70, 20,
    32, 110, 190, 110, 32,
    20, 70, 110, 70, 20,
    6, 20, 32, 20, 6,
};

static void stamp_add(uint8_t *acc, int32_t x, int32_t y,
                      const uint8_t *kernel)
{
    for (int ky = 0; ky < 5; ++ky) {
        int32_t yy = y + ky - 2;
        if (yy < 0 || yy >= OUT_H) {
            continue;
        }
        uint8_t *row = acc + yy * OUT_W;
        const uint8_t *krow = kernel + ky * 5;
        for (int kx = 0; kx < 5; ++kx) {
            int32_t xx = x + kx - 2;
            if (xx < 0 || xx >= OUT_W) {
                continue;
            }
            uint32_t v = row[xx] + krow[kx];
            row[xx] = (uint8_t)(v > 255u ? 255u : v);
        }
    }
}

/* Stamp a glowing line between two Q4 endpoints, ~1.6 px spacing. */
static void stamp_line(uint8_t *acc, int32_t x0, int32_t y0, int32_t x1,
                       int32_t y1)
{
    int32_t len = cyber_length_q4(x1 - x0, y1 - y0);
    int32_t steps = cyber_max32(1, len / 26);
    if (steps > 160) {
        steps = 160;
    }
    for (int32_t i = 0; i <= steps; ++i) {
        int32_t x = x0 + ((x1 - x0) * i) / steps;
        int32_t y = y0 + ((y1 - y0) * i) / steps;
        stamp_add(acc, x >> 4, y >> 4, stamp5);
    }
}

static void render_wireframe(cyber_face_ctx_t *ctx,
                             const cyber_motion_t *m,
                             const cyber_keyframe_t *kf, uint16_t *out)
{
    layout_t l;
    layout_face(m, &l);
    uint8_t *acc = ctx->scratch;
    for (int i = 0; i < CYBER_FACE_PIXEL_COUNT; ++i) {
        acc[i] = 0;
    }

    /* Head silhouette: octagon with a slow breathing bob. */
    int32_t hx = CYBER_Q4(80);
    int32_t hy = CYBER_Q4(60) + m->breath_y_q4;
    static const int16_t oct[8][2] = {
        { 58, 0 }, { 41, -31 }, { 0, -44 }, { -41, -31 },
        { -58, 0 }, { -41, 31 }, { 0, 44 }, { 41, 31 },
    };
    int32_t vx[8];
    int32_t vy[8];
    for (int i = 0; i < 8; ++i) {
        vx[i] = hx + CYBER_Q4(oct[i][0]);
        vy[i] = hy + CYBER_Q4(oct[i][1]);
    }
    for (int i = 0; i < 8; ++i) {
        int j = (i + 1) & 7;
        stamp_line(acc, vx[i], vy[i], vx[j], vy[j]);
    }
    for (int i = 0; i < 8; ++i) {
        stamp_add(acc, vx[i] >> 4, vy[i] >> 4, stamp_node);
    }

    /* Eyes as diamonds that flatten when the lids close. */
    for (int side = 0; side < 2; ++side) {
        int32_t ex = side == 0 ? l.eye_l_x : l.eye_r_x;
        int32_t hh = side == 0 ? l.eye_hh_l : l.eye_hh_r;
        int32_t hw = l.eye_hw;
        int32_t px[4] = { ex - hw, ex, ex + hw, ex };
        int32_t py[4] = { l.eye_y, l.eye_y - hh, l.eye_y, l.eye_y + hh };
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) & 3;
            stamp_line(acc, px[i], py[i], px[j], py[j]);
        }
    }

    /* Brows. */
    stamp_line(acc, l.brow_l_ax, l.brow_l_ay, l.brow_l_bx, l.brow_l_by);
    stamp_line(acc, l.brow_r_ax, l.brow_r_ay, l.brow_r_bx, l.brow_r_by);

    /* Mouth: trapezoid when open, single line when closed. */
    {
        int32_t hw = m->mouth_half_w_q4;
        int32_t hh = m->mouth_half_h_q4;
        int32_t curve = m->mouth_curve_q4;
        int32_t lx = l.mouth_x - hw;
        int32_t rx = l.mouth_x + hw;
        int32_t ty = l.mouth_y - curve;
        if (hh > CYBER_Q4(2)) {
            int32_t bx0 = l.mouth_x - hw / 2;
            int32_t bx1 = l.mouth_x + hw / 2;
            int32_t by = l.mouth_y + hh;
            stamp_line(acc, lx, ty, rx, ty);
            stamp_line(acc, lx, ty, bx0, by);
            stamp_line(acc, bx0, by, bx1, by);
            stamp_line(acc, bx1, by, rx, ty);
            if (kf->mouth_teeth > 96u) {
                for (int i = -1; i <= 1; ++i) {
                    int32_t tx = l.mouth_x + i * (hw / 2);
                    stamp_line(acc, tx, ty, tx, ty + hh / 2);
                }
            }
        } else {
            stamp_line(acc, lx, l.mouth_y + curve, l.mouth_x,
                       l.mouth_y - curve);
            stamp_line(acc, l.mouth_x, l.mouth_y - curve, rx,
                       l.mouth_y + curve);
        }
    }

    /* Colorize with interlace, scan band, chroma fringe, flicker. */
    const uint16_t *pal = ctx->palette[CYBER_PROFILE_HOLO_WIREFRAME];
    uint32_t t = m->t_ms;
    uint32_t flick_h = cyber_hash32(t >> 5);
    uint32_t gain = (flick_h & 31u) == 0u ? 168u : 256u;
    gain = (gain * m->glow_gain_q8) >> 8;
    int32_t band_y = (int32_t)((t / 24u) % (OUT_H + 40u)) - 20;

    for (int y = 0; y < OUT_H; ++y) {
        const uint8_t *row = acc + y * OUT_W;
        uint16_t *dst = out + y * OUT_W;
        uint32_t row_gain = gain;
        if ((y & 1) != 0) {
            row_gain = (row_gain * 192u) >> 8;
        }
        int32_t band_d = y - band_y;
        if (band_d < 0) {
            band_d = -band_d;
        }
        if (band_d < 6) {
            row_gain += (uint32_t)(6 - band_d) * 14u;
        }
        for (int x = 0; x < OUT_W; ++x) {
            uint32_t g = row[x];
            uint32_t r =
                x >= 2 ? row[x - 2] : g;
            uint32_t b =
                x < OUT_W - 2 ? row[x + 2] : g;
            g = (g * row_gain) >> 8;
            r = (r * row_gain) >> 8;
            b = (b * row_gain) >> 8;
            if (g > 255u) {
                g = 255u;
            }
            if (r > 255u) {
                r = 255u;
            }
            if (b > 255u) {
                b = 255u;
            }
            dst[x] = (uint16_t)((pal[r] & 0xF800u) | (pal[g] & 0x07E0u) |
                                (pal[b] & 0x001Fu));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Glitch scheduling and block dropouts                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t active;
    uint32_t env_q8; /* attack/decay envelope 0..255 */
    uint32_t seed;
} glitch_state_t;

static void glitch_schedule(uint32_t t_ms, glitch_state_t *g)
{
    uint32_t bucket = t_ms / 2400u;
    uint32_t h = cyber_hash32(bucket * 0x51ED270Bu + 7u);
    uint32_t start = bucket * 2400u + (h % 1200u);
    uint32_t dur = 160u + ((h >> 8) % 220u);
    g->active = 0u;
    g->env_q8 = 0u;
    g->seed = h;
    if (t_ms >= start && t_ms < start + dur) {
        uint32_t dt = t_ms - start;
        uint32_t attack = dur / 4u;
        uint32_t env;
        if (dt < attack) {
            env = (dt * 255u) / cyber_max32((int32_t)attack, 1);
        } else {
            env = 255u - ((dt - attack) * 255u) /
                             cyber_max32((int32_t)(dur - attack), 1);
        }
        g->active = 1u;
        g->env_q8 = env;
    }
}

static void render_glitch(cyber_face_ctx_t *ctx, const cyber_motion_t *m,
                          const cyber_keyframe_t *kf, uint16_t *out)
{
    field_neon_face(ctx, m, kf, EYE_SHAPE_ROUNDED_BOX, 0);

    glitch_state_t g;
    glitch_schedule(m->t_ms, &g);

    /* Block dropouts, applied in field space. */
    if (g.active) {
        for (uint32_t rblock = 0; rblock < 3u; ++rblock) {
            uint32_t h = cyber_hash32(g.seed ^ (rblock * 0x9E3779B9u));
            uint32_t bx = h % FIELD_W;
            uint32_t by = (h >> 8) % FIELD_H;
            uint32_t bw = 6u + ((h >> 16) % 14u);
            uint32_t bh = 3u + ((h >> 20) % 6u);
            for (uint32_t y = by;
                 y < by + bh && y < (uint32_t)FIELD_H; ++y) {
                uint8_t *row = ctx->scratch + y * FIELD_W;
                for (uint32_t x = bx;
                     x < bx + bw && x < (uint32_t)FIELD_W; ++x) {
                    row[x] = (uint8_t)(row[x] >> 3);
                }
            }
        }
    }

    /* Per-band row offsets. */
    int8_t row_off[OUT_H];
    for (int y = 0; y < OUT_H; ++y) {
        row_off[y] = 0;
    }
    if (g.active) {
        uint32_t amp = 1u + ((g.env_q8 * 5u) >> 8);
        for (int band = 0; band < OUT_H / 8; ++band) {
            uint32_t h = cyber_hash32(g.seed ^ (uint32_t)(band * 197u));
            int32_t off =
                (int32_t)(h % (2u * amp + 1u)) - (int32_t)amp;
            for (int y = band * 8; y < band * 8 + 8; ++y) {
                row_off[y] = (int8_t)off;
            }
        }
    }

    upscale_fx_t fx;
    fx.pal = ctx->palette[CYBER_PROFILE_GLITCH_MASK];
    fx.chroma_off = 1u + ((g.env_q8 * 2u) >> 8);
    fx.scanline = 0u;
    fx.bar_y = INT32_MIN;
    fx.bar_h = 0;
    fx.bar_boost = 0;
    /* A one-row white flash while a glitch peaks. */
    if (g.active && g.env_q8 > 200u) {
        fx.bar_y = (int32_t)(g.seed % OUT_H);
        fx.bar_h = 1;
        fx.bar_boost = 140;
    }
    fx.row_off = row_off;
    fx.dither = 1u;
    upscale_2x(ctx, &fx, out);
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

void cyber_render_profile(cyber_face_ctx_t *ctx, cyber_profile_t profile,
                          const cyber_motion_t *m,
                          const cyber_keyframe_t *kf,
                          uint32_t sample_clock, uint16_t *out)
{
    (void)sample_clock;
    upscale_fx_t fx;
    fx.pal = ctx->palette[profile];
    fx.chroma_off = 0u;
    fx.scanline = 0u;
    fx.bar_y = INT32_MIN;
    fx.bar_h = 0;
    fx.bar_boost = 0;
    fx.row_off = NULL;
    fx.dither = 1u;

    switch (profile) {
    case CYBER_PROFILE_NEON_SDF_CYAN:
        field_neon_face(ctx, m, kf, EYE_SHAPE_ROUNDED_BOX, 0);
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_NEON_SDF_MAGENTA:
        field_neon_face(ctx, m, kf, EYE_SHAPE_LIDDED_DISC, 0);
        fx.chroma_off = 1u;
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_LIQUID_SMIN:
        field_liquid(ctx, m, kf);
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_CRT_CHROMATIC: {
        field_neon_face(ctx, m, kf, EYE_SHAPE_ROUNDED_BOX, 1);
        fx.chroma_off = 1u;
        fx.scanline = 2u;
        fx.bar_y =
            (int32_t)((m->t_ms / 14u) % (OUT_H + 40u)) - 30;
        fx.bar_h = 10;
        fx.bar_boost = 20;
        /* Horizontal hum jitter for ~240 ms every ~3.9 s. */
        int8_t row_off[OUT_H];
        int use_rows = 0;
        if ((m->t_ms % 3900u) < 240u) {
            use_rows = 1;
            uint32_t salt = m->t_ms >> 5;
            for (int y = 0; y < OUT_H; ++y) {
                uint32_t h =
                    cyber_hash32((uint32_t)y * 31u + salt * 977u);
                row_off[y] = (int8_t)((int32_t)(h % 3u) - 1);
            }
        }
        fx.row_off = use_rows ? row_off : NULL;
        upscale_2x(ctx, &fx, out);
        break;
    }
    case CYBER_PROFILE_HOLO_WIREFRAME:
        render_wireframe(ctx, m, kf, out);
        break;
    case CYBER_PROFILE_VOICE_ORB:
        field_voice_orb(ctx, m, kf);
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_RED_OPTIC:
        field_red_optic(ctx, m, kf);
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_HUB75_NEON:
        field_hub75(ctx, m, kf);
        upscale_hub75(ctx, out);
        break;
    case CYBER_PROFILE_EDGE_GLOW:
        field_edge_glow(ctx, m, kf);
        fx.dither = 0u; /* band-packed palette indices */
        upscale_2x(ctx, &fx, out);
        break;
    case CYBER_PROFILE_GLITCH_MASK:
        render_glitch(ctx, m, kf, out);
        break;
    case CYBER_PROFILE_PALETTE_PLASMA:
    default:
        field_plasma(ctx, m, kf);
        fx.dither = 0u; /* cyclic + banded palette indices */
        upscale_2x(ctx, &fx, out);
        break;
    }
}
