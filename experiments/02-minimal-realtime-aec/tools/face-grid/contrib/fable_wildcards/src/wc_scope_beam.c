#include "wc_common.h"

/*
 * Oscillofluor: an XY vector-CRT face in the oscilloscope-music tradition.
 *
 * The whole face is one continuous beam path retraced at ~83 Hz. The mouth
 * segment is a synthesized "voice waveform" whose harmonic mix comes from the
 * semantic mouth bytes, so the face literally speaks its own oscillogram.
 * Persistence is simulated statelessly with a two-layer P7 phosphor model:
 * every frame redraws the last ~120 ms of beam history from the sample clock,
 * new light is a blue-white flash, old light decays into green afterglow.
 * The framebuffer doubles as the accumulator (hi byte flash, lo byte glow),
 * then a final pass maps both layers to RGB565.
 */

enum {
    WC_TRACE_PERIOD = 192,  /* 12 ms per retrace, ~83 Hz */
    WC_TRACE_COUNT = 10,    /* ~120 ms of visible history */
    WC_TRACE_STEPS = 512,
    WC_FLASH_FADE = 480,    /* samples until the blue flash is gone */
    WC_GLOW_FADE = WC_TRACE_PERIOD * WC_TRACE_COUNT,
};

/* segment boundaries of the trace, Q16 phase */
enum {
    WC_SEG_EYE_L_END = 11796,
    WC_SEG_HOP1_END = 13107,
    WC_SEG_EYE_R_END = 24903,
    WC_SEG_HOP2_END = 26214,
    WC_SEG_BROW_L_END = 29491,
    WC_SEG_HOP3_END = 30146,
    WC_SEG_BROW_R_END = 33423,
    WC_SEG_HOP4_END = 35389,
    WC_SEG_MOUTH_END = 62914,
};

typedef struct {
    int32_t eye_cx_q8[2];
    int32_t eye_cy_q8[2];
    int32_t eye_a_q8;
    int32_t eye_b_q8[2];
    int32_t brow_y_q8[2];
    int32_t brow_tilt_q8[2];
    int32_t mouth_x0_q8;
    int32_t mouth_w_q8; /* full width */
    int32_t mouth_y_q8;
    int32_t amp_q8;
    int32_t a1, a2, a3;      /* harmonic weights */
    int32_t recip_q30;       /* 1 / (a1+a2+a3) */
    uint32_t h1, h2, h3;     /* harmonic counts across the mouth */
    uint32_t ph1, ph2, ph3;  /* scroll phases at this trace's time */
    uint32_t wob;
} wc_scope_pose_t;

static void wc_scope_pose(
    const wc_keyframe_t *kf, uint32_t t, wc_scope_pose_t *p) {
    wc_rig_t rig;
    wc_rig_derive(kf, t, &rig);

    for (int e = 0; e < 2; ++e) {
        int32_t sx = e == 0 ? -WC_EYE_DX : WC_EYE_DX;
        p->eye_cx_q8[e] = ((WC_CENTER_X + sx) << 8) + rig.head_x_q8 +
                          rig.gaze_x * 6;
        p->eye_cy_q8[e] = (WC_EYE_Y << 8) + rig.head_y_q8 + rig.gaze_y * 4;
        int32_t lid = e == 0 ? rig.lid_l : rig.lid_r;
        p->eye_b_q8[e] = wc_max32(96, (9 << 8) * lid / 255);
        p->brow_y_q8[e] = p->eye_cy_q8[e] - (17 << 8) - rig.brow_q8 * 5;
        p->brow_tilt_q8[e] = (e == 0 ? -1 : 1) * rig.brow_q8;
    }
    p->eye_a_q8 = 10 << 8;

    int32_t w_half = 15 + ((int32_t)rig.mouth_width * 20 >> 8);
    p->mouth_w_q8 = (w_half * 2) << 8;
    p->mouth_x0_q8 = ((WC_CENTER_X - w_half) << 8) + rig.head_x_q8;
    p->mouth_y_q8 = (WC_MOUTH_Y << 8) + rig.head_y_q8;

    int32_t amp = (2 << 8) + ((int32_t)rig.mouth_open * (22 << 8) >> 8);
    amp = amp * (256 - (int32_t)rig.mouth_press / 2) >> 8;
    if (!rig.speaking) {
        amp += rig.breath_q14 * 3 >> 5; /* idle breathing ripple, ~+-1.5 px */
        if (amp < (1 << 8)) {
            amp = 1 << 8;
        }
    }
    p->amp_q8 = amp;

    p->a1 = 10000 + (int32_t)rig.mouth_round * 20;
    p->a2 = 6200 - (int32_t)rig.mouth_round * 14;
    p->a3 = 800 + (int32_t)rig.mouth_teeth * 36;
    p->recip_q30 = (int32_t)((1 << 30) / (uint32_t)(p->a1 + p->a2 + p->a3));
    p->h1 = 1u + ((uint32_t)rig.mouth_width * 4u >> 8);
    p->h2 = p->h1 * 2u;
    p->h3 = 7u;
    p->ph1 = t * 11u;
    p->ph2 = (uint32_t)(-(int32_t)(t * 7u));
    p->ph3 = t * 23u;
    p->wob = t * 3u;
}

/* beam position for phase s (Q16 within the trace); returns 0 if blanked */
static int32_t wc_scope_beam_pos(
    const wc_scope_pose_t *p, uint32_t s, int32_t *x_q8, int32_t *y_q8) {
    if (s < WC_SEG_EYE_L_END || (s >= WC_SEG_HOP1_END && s < WC_SEG_EYE_R_END)) {
        int e = s < WC_SEG_EYE_L_END ? 0 : 1;
        uint32_t s0 = e == 0 ? 0u : WC_SEG_HOP1_END;
        uint32_t len = e == 0 ? WC_SEG_EYE_L_END
                              : WC_SEG_EYE_R_END - WC_SEG_HOP1_END;
        uint32_t v = (uint32_t)(((uint64_t)(s - s0) << 16) / len);
        int32_t wobble = 16384 + (wc_sin_q14(v * 2u + p->wob) * 5 >> 5);
        int32_t a = p->eye_a_q8 * wobble >> 14;
        int32_t b = p->eye_b_q8[e] * wobble >> 14;
        *x_q8 = p->eye_cx_q8[e] + (a * wc_cos_q14(v) >> 14);
        *y_q8 = p->eye_cy_q8[e] + (b * wc_sin_q14(v) >> 14);
        return 1;
    }
    if ((s >= WC_SEG_HOP2_END && s < WC_SEG_BROW_L_END) ||
        (s >= WC_SEG_HOP3_END && s < WC_SEG_BROW_R_END)) {
        int e = s < WC_SEG_BROW_L_END ? 0 : 1;
        uint32_t s0 = e == 0 ? WC_SEG_HOP2_END : WC_SEG_HOP3_END;
        uint32_t len = e == 0 ? WC_SEG_BROW_L_END - WC_SEG_HOP2_END
                              : WC_SEG_BROW_R_END - WC_SEG_HOP3_END;
        int32_t v = (int32_t)(((uint64_t)(s - s0) << 16) / len); /* Q16 */
        int32_t half = 10 << 8;
        int32_t dx = -half + (int32_t)((int64_t)(2 * half) * v >> 16);
        *x_q8 = p->eye_cx_q8[e] + dx;
        *y_q8 = p->brow_y_q8[e] + (dx * p->brow_tilt_q8[e] >> 10);
        return 1;
    }
    if (s >= WC_SEG_HOP4_END && s < WC_SEG_MOUTH_END) {
        uint32_t v = (uint32_t)(((uint64_t)(s - WC_SEG_HOP4_END) << 16) /
                                (WC_SEG_MOUTH_END - WC_SEG_HOP4_END));
        *x_q8 = p->mouth_x0_q8 + (int32_t)((uint64_t)p->mouth_w_q8 * v >> 16);
        int32_t env = (16384 - wc_cos_q14(v)) >> 1; /* raised cosine, Q14 */
        int32_t wave = (p->a1 * wc_sin_q14(v * p->h1 + p->ph1) +
                        p->a2 * wc_sin_q14(v * p->h2 + p->ph2) +
                        p->a3 * wc_sin_q14(v * p->h3 + p->ph3)) >> 14;
        wave = (int32_t)((int64_t)wave * p->recip_q30 >> 16); /* back to Q14 */
        *y_q8 = p->mouth_y_q8 + (((p->amp_q8 * (env * wave >> 14)) >> 14));
        return 1;
    }
    /* hop segments: return the linear travel so retrace lines can be hinted */
    uint32_t h0, h1p;
    int32_t ax = 0, ay = 0, bx = 0, by = 0;
    if (s < WC_SEG_HOP1_END) {
        h0 = WC_SEG_EYE_L_END;
        h1p = WC_SEG_HOP1_END;
        ax = p->eye_cx_q8[0] + p->eye_a_q8;
        ay = p->eye_cy_q8[0];
        bx = p->eye_cx_q8[1] + p->eye_a_q8;
        by = p->eye_cy_q8[1];
    } else if (s < WC_SEG_HOP2_END) {
        h0 = WC_SEG_EYE_R_END;
        h1p = WC_SEG_HOP2_END;
        ax = p->eye_cx_q8[1] + p->eye_a_q8;
        ay = p->eye_cy_q8[1];
        bx = p->eye_cx_q8[0] - (10 << 8);
        by = p->brow_y_q8[0];
    } else if (s < WC_SEG_HOP3_END) {
        h0 = WC_SEG_BROW_L_END;
        h1p = WC_SEG_HOP3_END;
        ax = p->eye_cx_q8[0] + (10 << 8);
        ay = p->brow_y_q8[0];
        bx = p->eye_cx_q8[1] - (10 << 8);
        by = p->brow_y_q8[1];
    } else if (s < WC_SEG_HOP4_END) {
        h0 = WC_SEG_BROW_R_END;
        h1p = WC_SEG_HOP4_END;
        ax = p->eye_cx_q8[1] + (10 << 8);
        ay = p->brow_y_q8[1];
        bx = p->mouth_x0_q8;
        by = p->mouth_y_q8;
    } else {
        h0 = WC_SEG_MOUTH_END;
        h1p = 65536;
        ax = p->mouth_x0_q8 + p->mouth_w_q8;
        ay = p->mouth_y_q8;
        bx = p->eye_cx_q8[0] + p->eye_a_q8;
        by = p->eye_cy_q8[0];
    }
    int32_t v = (int32_t)(((uint64_t)(s - h0) << 16) / (h1p - h0));
    *x_q8 = ax + (int32_t)((int64_t)(bx - ax) * v >> 16);
    *y_q8 = ay + (int32_t)((int64_t)(by - ay) * v >> 16);
    return 0;
}

static void wc_scope_splat(
    uint16_t *fb, int32_t x_q8, int32_t y_q8, uint32_t flash, uint32_t glow) {
    int32_t ix = x_q8 >> 8;
    int32_t iy = y_q8 >> 8;
    if (ix < 2 || ix >= WC_FACE_WIDTH - 3 || iy < 2 || iy >= WC_FACE_HEIGHT - 3) {
        return; /* 2 px margin also keeps the halo writes in bounds */
    }
    uint32_t fx = (uint32_t)x_q8 & 255u;
    uint32_t fy = (uint32_t)y_q8 & 255u;
    uint32_t w00 = (256u - fx) * (256u - fy) >> 8;
    uint32_t w10 = fx * (256u - fy) >> 8;
    uint32_t w01 = (256u - fx) * fy >> 8;
    uint32_t w11 = fx * fy >> 8;
    uint16_t *row = fb + (size_t)iy * WC_FACE_WIDTH + (size_t)ix;
    const uint32_t w[4] = { w00, w10, w01, w11 };
    uint16_t *px[4] = { row, row + 1, row + WC_FACE_WIDTH, row + WC_FACE_WIDTH + 1 };
    for (int i = 0; i < 4; ++i) {
        uint32_t p = *px[i];
        uint32_t hi = (p >> 8) + (flash * w[i] >> 8);
        uint32_t lo = (p & 255u) + (glow * w[i] >> 8);
        if (hi > 255u) hi = 255u;
        if (lo > 255u) lo = 255u;
        *px[i] = (uint16_t)((hi << 8) | lo);
    }
    /* soft halo on the glow layer */
    uint32_t halo = glow >> 3;
    if (halo) {
        static const int32_t off[4][2] = { {-2, 0}, {2, 0}, {0, -2}, {0, 2} };
        for (int i = 0; i < 4; ++i) {
            uint16_t *q = fb + (size_t)(iy + off[i][1]) * WC_FACE_WIDTH +
                          (size_t)(ix + off[i][0]);
            uint32_t p = *q;
            uint32_t lo = (p & 255u) + halo;
            if (lo > 255u) lo = 255u;
            *q = (uint16_t)((p & 0xFF00u) | lo);
        }
    }
}

void wc_render_scope_beam(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)rig;
    /* clear + graticule seeds in the glow layer */
    for (int32_t y = 0; y < WC_FACE_HEIGHT; ++y) {
        uint16_t *row = fb + (size_t)y * WC_FACE_WIDTH;
        uint32_t gy = ((uint32_t)y % 30u) == 15u;
        for (int32_t x = 0; x < WC_FACE_WIDTH; ++x) {
            uint32_t g = (gy || ((uint32_t)x & 31u) == 16u) ? 5u : 0u;
            row[x] = (uint16_t)g;
        }
    }

    uint32_t cur_off = clock % WC_TRACE_PERIOD;
    uint32_t cur_base = clock - cur_off;
    int32_t last_x = 0, last_y = 0, have_last = 0;

    for (int32_t k = WC_TRACE_COUNT - 1; k >= 0; --k) {
        uint32_t base = cur_base - (uint32_t)k * WC_TRACE_PERIOD;
        if (base > clock) {
            continue; /* before clock zero */
        }
        wc_scope_pose_t pose;
        wc_scope_pose(kf, base, &pose);
        uint32_t max_step = WC_TRACE_STEPS;
        if (k == 0) {
            max_step = cur_off * WC_TRACE_STEPS / WC_TRACE_PERIOD;
        }
        for (uint32_t step = 0; step < max_step; ++step) {
            uint32_t s = step << 7; /* Q16 phase, 512 steps */
            uint32_t t = base + s * WC_TRACE_PERIOD / 65536u;
            uint32_t age = clock - t;
            uint32_t flash =
                age < WC_FLASH_FADE ? (WC_FLASH_FADE - age) * 255u / WC_FLASH_FADE : 0u;
            uint32_t glow =
                age < WC_GLOW_FADE ? (WC_GLOW_FADE - age) * 200u / WC_GLOW_FADE : 0u;
            int32_t x_q8, y_q8;
            int32_t lit = wc_scope_beam_pos(&pose, s, &x_q8, &y_q8);
            if (!lit) {
                /* faint retrace hint, newest trace only */
                if (k == 0 && (step & 3u) == 0u) {
                    wc_scope_splat(fb, x_q8, y_q8, 0, glow >> 4);
                }
                continue;
            }
            wc_scope_splat(fb, x_q8, y_q8, flash, glow);
            if (k == 0) {
                last_x = x_q8;
                last_y = y_q8;
                have_last = 1;
            }
        }
    }

    /* the writing dot: a hot spark where the beam currently sits */
    if (have_last) {
        wc_scope_splat(fb, last_x, last_y, 255, 200);
        wc_scope_splat(fb, last_x + 128, last_y, 120, 90);
        wc_scope_splat(fb, last_x - 128, last_y, 120, 90);
        wc_scope_splat(fb, last_x, last_y + 128, 120, 90);
        wc_scope_splat(fb, last_x, last_y - 128, 120, 90);
    }

    /* colorize: flash is blue-white, glow is P7 green */
    for (size_t i = 0; i < WC_FACE_PIXEL_COUNT; ++i) {
        uint32_t p = fb[i];
        uint32_t f = p >> 8;
        uint32_t g = p & 255u;
        uint32_t r = (f * 200u + g * 30u) >> 8;
        uint32_t gr = (f * 220u + g * 252u) >> 8;
        uint32_t b = (f * 255u + g * 96u) >> 8;
        if (r > 255u) r = 255u;
        if (gr > 255u) gr = 255u;
        if (b > 255u) b = 255u;
        fb[i] = wc_rgb565(r, gr, b);
    }
}
