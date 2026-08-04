#include "fre_internal.h"

/*
 * Stateless behavior solver.
 *
 * Every quantity is a pure function of the 16 kHz sample clock, the 12-byte
 * keyframe, and the profile's tuning row. Variable-interval events (blinks,
 * saccades, idle acts) use epoch hashing: time is cut into fixed macro
 * cycles, and each cycle's event parameters are drawn from fre_hash3 of the
 * cycle index. The same clock therefore replays the same life anywhere,
 * and the solver can be evaluated at a *future* instant (lids reading the
 * gaze 60 ms ahead) at no extra cost — which is how lid anticipation works.
 *
 * Sources for the constants (see RESEARCH.md for full citations):
 *  - blink rates: Bentivoglio et al. 1997 (rest 17/min, conversation
 *    26/min); Bailly et al. (speakers blink much more than listeners);
 *  - blink kinematics: VanderWerf et al. 2003 (down-phase ~75 ms,
 *    reopening 100-200 ms, asymptotic), Trutoiu et al. 2011 (full closure,
 *    asymmetric profile reads most natural);
 *  - gaze-evoked blinks: Evinger et al. 1994 (probability grows with gaze
 *    shift amplitude, near 1.0 above ~33 deg);
 *  - saccade main sequence: Bahill et al. 1975 / Eyes Alive
 *    (duration ~= 25 ms + 2.4 ms/deg, 90% of saccades < 15 deg);
 *  - saccade magnitude/direction/dwell statistics: Lee, Badler & Badler,
 *    "Eyes Alive", SIGGRAPH 2002 (exponential magnitudes, 8-bin direction
 *    table, mutual-gaze vs away dwell time per talking/listening mode);
 *  - conversational aversion bouts: Andrist et al., HRI 2014 (cognitive
 *    aversions ~3.5 s and upward, intimacy aversions ~1-2 s and sideways,
 *    listening aversions rarer and shorter than speaking ones);
 *  - microsaccades: Martinez-Conde et al. (1-2 per second, mostly under
 *    0.4 deg, clamped at 1 deg);
 *  - lid-gaze coupling: Becker & Fuchs 1988 (upper lid yoked to vertical
 *    eye position; downward lid motion is a passive fall).
 */

/* One gaze unit of 256 == FRE_GAZE_FULL_DEG degrees of eye rotation. */
#define FRE_GAZE_FULL_DEG 25

typedef struct {
    uint16_t blink_cycle_ms;
    uint16_t gaze_cycle_ms;
    uint16_t away_min_ms;
    uint16_t away_max_ms;
    uint8_t away_prob_pct;
    uint8_t wander_amp_q8;   /* aversion amplitude scale, 256 == full */
    uint8_t dir_table;       /* 0 neutral, 1 up-biased, 2 side-biased */
    uint16_t breath_period_ms;
    uint8_t arousal;
} fre_activity_params_t;

static const fre_activity_params_t FRE_ACTIVITY[4] = {
    /* IDLE: unattended robot roams the room. */
    { 3500, 3400, 800, 2300, 65, 185, 0, 4600, 110 },
    /* LISTENING: long mutual gaze, rare short sideways aversions
     * (Eyes Alive listening dwell ~7.9 s; Andrist listening bouts). */
    { 4700, 7600, 800, 1500, 55, 95, 2, 4200, 215 },
    /* THINKING: long upward cognitive aversions (Andrist ~3.5 s). */
    { 3300, 6200, 2600, 4400, 85, 200, 1, 4400, 165 },
    /* SPEAKING: frequent blinks, ~1 s sideways aversions
     * (Eyes Alive talking dwell ~3.1 s mutual / ~0.9 s away). */
    { 2100, 4100, 600, 1800, 70, 140, 2, 3400, 195 },
};

/*
 * Inverse CDF of the Eyes Alive saccade magnitude distribution
 * P(A) = 15.7 * exp(-A / 6.9), sampled at 16 equiprobable points and
 * expressed in Q8 of the 25-degree full gaze range.
 */
static const uint8_t FRE_SACCADE_MAG_Q8[16] = {
    248, 170, 134, 111, 93, 79, 67, 57, 48, 40, 33, 27, 21, 15, 10, 5,
};

/*
 * Eyes Alive Table 1 direction bins as cumulative 0..255 thresholds in
 * order R, UR, U, UL, L, DL, D, DR, plus an up-biased variant for
 * cognitive aversions and a side-biased variant for social aversions
 * (Andrist et al.: cognitive ~39% up; intimacy/floor ~50-58% sideways).
 */
static const uint8_t FRE_DIR_CDF[3][8] = {
    { 40, 56, 101, 120, 163, 183, 235, 255 },
    { 24, 60, 140, 176, 200, 214, 240, 255 },
    { 60, 72, 92, 104, 164, 190, 234, 255 },
};

/* Unit vectors for the eight bins, Q8, screen coordinates (y down). */
static const int16_t FRE_DIR_X[8] = { 256, 181, 0, -181, -256, -181, 0, 181 };
static const int16_t FRE_DIR_Y[8] = { 0, -181, -256, -181, 0, 181, 256, 181 };

/* ------------------------------------------------------------------ */
/* Saccade easing                                                      */
/* ------------------------------------------------------------------ */

/* Main-sequence duration for an amplitude in Q8 gaze units. */
static int32_t fre_saccade_duration_ms(int32_t amp_q8)
{
    int32_t deg = (amp_q8 * FRE_GAZE_FULL_DEG) >> 8;
    return 25 + ((deg * 24) + 5) / 10;
}

/*
 * Position of one saccade at time u after onset, Q8 progress of the full
 * displacement. overshoot_pct > 0 gives a cartoon overshoot with settle;
 * < 0 gives physiological undershoot plus a corrective saccade ~150 ms
 * later; 0 is a clean bell profile.
 */
static int32_t fre_saccade_progress_q8(
    int32_t u_ms, int32_t dur_ms, int32_t overshoot_pct)
{
    if (u_ms <= 0) {
        return 0;
    }
    if (overshoot_pct > 0) {
        int32_t settle = 60;
        int32_t peak = 256 + (overshoot_pct * 256) / 400;
        if (u_ms < dur_ms) {
            int32_t e = (u_ms * 256) / dur_ms;
            return (fre_smoothstep_q8(e) * peak) >> 8;
        }
        if (u_ms < dur_ms + settle) {
            int32_t e = ((u_ms - dur_ms) * 256) / settle;
            return peak - (((peak - 256) * fre_smoothstep_q8(e)) >> 8);
        }
        return 256;
    }
    if (overshoot_pct < 0) {
        int32_t short_of = (-overshoot_pct * 256) / 400;
        int32_t landing = 256 - short_of;
        int32_t corr_at = dur_ms + 150;
        int32_t corr_dur = 24;
        if (u_ms < dur_ms) {
            int32_t e = (u_ms * 256) / dur_ms;
            return (fre_smoothstep_q8(e) * landing) >> 8;
        }
        if (u_ms < corr_at) {
            return landing;
        }
        if (u_ms < corr_at + corr_dur) {
            int32_t e = ((u_ms - corr_at) * 256) / corr_dur;
            return landing + ((short_of * fre_smoothstep_q8(e)) >> 8);
        }
        return 256;
    }
    if (u_ms >= dur_ms) {
        return 256;
    }
    return fre_smoothstep_q8((u_ms * 256) / dur_ms);
}

/* ------------------------------------------------------------------ */
/* Macro gaze: mutual-gaze vs aversion segments                        */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t x_q8;
    int32_t y_q8;
    /* Most recent transition at or before t. */
    uint32_t sacc_start_ms;
    int32_t sacc_amp_q8;
    uint32_t sacc_id;
    int32_t sacc_dur_ms;
    bool in_flight;
    bool averted;
} fre_macro_gaze_t;

typedef struct {
    const fre_tuning_t *tu;
    const fre_activity_params_t *ap;
    uint32_t seed;
    const fre_keyframe_t *kf;
    fre_activity_t activity;
} fre_ctx_t;

/* Aversion parameters of gaze cycle gi. */
static void fre_cycle_aversion(
    const fre_ctx_t *c,
    uint32_t cycle_ms,
    uint32_t gi,
    bool *has,
    uint32_t *start_off,
    uint32_t *len,
    int32_t *tx,
    int32_t *ty)
{
    uint32_t h = fre_hash3(gi, FRE_SALT_GAZE_PHASE, c->seed);
    *has = (h % 100U) < c->ap->away_prob_pct;
    uint32_t span = (uint32_t)(c->ap->away_max_ms - c->ap->away_min_ms);
    uint32_t l = c->ap->away_min_ms + (fre_hash3(gi, FRE_SALT_GAZE_RADIUS,
        c->seed ^ 0x51EDU) % (span + 1U));
    uint32_t margin = 250U;
    if (l + 2U * margin >= cycle_ms) {
        l = cycle_ms - 2U * margin - 1U;
    }
    *len = l;
    *start_off = margin +
        (h >> 8) % (cycle_ms - l - 2U * margin);
    /* Aversion target: direction bin then exponential magnitude. */
    uint32_t hd = fre_hash3(gi, FRE_SALT_GAZE_ANGLE, c->seed);
    const uint8_t *cdf = FRE_DIR_CDF[c->ap->dir_table];
    uint32_t pick = hd & 0xFFU;
    uint32_t bin = 0;
    while (bin < 7U && pick > cdf[bin]) {
        ++bin;
    }
    int32_t mag = FRE_SACCADE_MAG_Q8[(hd >> 8) & 15U];
    mag = (mag * c->ap->wander_amp_q8) >> 8;
    mag = (mag * c->tu->wander_pct) / 100;
    /* +-15% component jitter so repeated bins are not identical. */
    int32_t jx = (int32_t)((hd >> 12) & 63U) - 32;
    int32_t jy = (int32_t)((hd >> 18) & 63U) - 32;
    *tx = fre_clamp((((int32_t)FRE_DIR_X[bin] + jx) * mag) >> 8, -256, 256);
    *ty = fre_clamp((((int32_t)FRE_DIR_Y[bin] + jy) * mag) >> 8, -256, 256);
    /* Thinking aversions land slightly above the horizon even for the
     * side bins: eyes-up-while-remembering (Glenberg et al. 1998). */
    if (c->ap->dir_table == 1 && *ty > -20) {
        *ty -= 40;
    }
}

static void fre_solve_macro_gaze(
    const fre_ctx_t *c, uint32_t t_ms, fre_macro_gaze_t *out)
{
    uint32_t cycle = c->ap->gaze_cycle_ms;
    uint32_t gi = t_ms / cycle;
    uint32_t phase = t_ms - gi * cycle;

    bool has_a = false, has_p = false;
    uint32_t off_a = 0, len_a = 0, off_p = 0, len_p = 0;
    int32_t ax = 0, ay = 0, px = 0, py = 0;
    fre_cycle_aversion(c, cycle, gi, &has_a, &off_a, &len_a, &ax, &ay);
    if (gi > 0U) {
        fre_cycle_aversion(
            c, cycle, gi - 1U, &has_p, &off_p, &len_p, &px, &py);
    }

    /* Transitions ordered in time: previous cycle's return, this cycle's
     * departure, this cycle's return. Pick the latest one at or before t
     * to define the segment; its predecessor defines the from-pose. */
    uint32_t start;
    int32_t from_x, from_y, to_x, to_y;
    uint32_t id;
    bool averted;
    if (has_a && phase >= off_a + len_a) {
        start = gi * cycle + off_a + len_a;
        from_x = ax;
        from_y = ay;
        to_x = 0;
        to_y = 0;
        id = gi * 2U + 1U;
        averted = false;
    } else if (has_a && phase >= off_a) {
        start = gi * cycle + off_a;
        from_x = 0;
        from_y = 0;
        to_x = ax;
        to_y = ay;
        id = gi * 2U;
        averted = true;
    } else if (has_p) {
        start = (gi - 1U) * cycle + off_p + len_p;
        from_x = px;
        from_y = py;
        to_x = 0;
        to_y = 0;
        id = (gi - 1U) * 2U + 1U;
        averted = false;
    } else {
        start = 0;
        from_x = 0;
        from_y = 0;
        to_x = 0;
        to_y = 0;
        id = 0;
        averted = false;
    }

    int32_t dx = to_x - from_x;
    int32_t dy = to_y - from_y;
    int32_t amp = (int32_t)fre_isqrt(
        (uint32_t)(dx * dx) + (uint32_t)(dy * dy));
    int32_t dur = fre_saccade_duration_ms(amp);
    int32_t u = (int32_t)(t_ms - start);
    int32_t e = fre_saccade_progress_q8(u, dur, c->tu->overshoot_pct);
    /* Curved dart path: the lagging axis starts a quarter-duration late
     * (Vector's keep-alive moves the minor axis at a fraction of the
     * dominant one, so darts arc instead of tracing straight lines). */
    int32_t e_lag = fre_saccade_progress_q8(
        u - dur / 4, dur, c->tu->overshoot_pct);
    int32_t ex = e, ey = e;
    if (fre_abs(dx) >= fre_abs(dy)) {
        ey = e_lag;
    } else {
        ex = e_lag;
    }
    out->x_q8 = from_x + (int32_t)fre_sar64((int64_t)dx * ex, 8);
    out->y_q8 = from_y + (int32_t)fre_sar64((int64_t)dy * ey, 8);
    out->sacc_start_ms = start;
    out->sacc_amp_q8 = amp;
    out->sacc_id = id;
    out->sacc_dur_ms = dur;
    out->in_flight = (start != 0U) && u >= 0 && u < dur;
    out->averted = averted;
}

/* ------------------------------------------------------------------ */
/* Refixation wobble and microsaccades                                 */
/* ------------------------------------------------------------------ */

/* Small target changes every ~700 ms while in mutual gaze: the triangular
 * face-scanning pattern a listener runs over eyes and mouth. Amplitude
 * follows the activity's wander scale, so attentive listening scans a
 * tighter patch than idle roaming. */
static void fre_solve_refixation(
    const fre_ctx_t *c, uint32_t t_ms, int32_t *rx, int32_t *ry)
{
    uint32_t cycle = 700U;
    uint32_t ri = t_ms / cycle;
    uint32_t u = t_ms - ri * cycle;
    uint32_t h_now = fre_hash3(ri, FRE_SALT_SCAN, c->seed);
    uint32_t h_prev = fre_hash3(ri - 1U, FRE_SALT_SCAN, c->seed);
    /* Targets on a small ring: mostly horizontal, a touch of vertical. */
    int32_t scale = (int32_t)c->ap->wander_amp_q8 + 64;
    int32_t nx = (((int32_t)(h_now & 63U) - 32) * scale) >> 8;
    int32_t ny = (((int32_t)((h_now >> 6) & 31U) - 16) * scale) >> 8;
    int32_t ox = (((int32_t)(h_prev & 63U) - 32) * scale) >> 8;
    int32_t oy = (((int32_t)((h_prev >> 6) & 31U) - 16) * scale) >> 8;
    int32_t e = fre_smoothstep_q8((int32_t)fre_min((int32_t)u, 40) * 256 / 40);
    *rx = ox + (((nx - ox) * e) >> 8);
    *ry = oy + (((ny - oy) * e) >> 8);
}

/* Fixational drift + 1-2 Hz microsaccades (Martinez-Conde). */
static void fre_solve_micro(
    const fre_ctx_t *c, uint32_t t_ms, int32_t *mx, int32_t *my)
{
    if (c->tu->micro_pct == 0) {
        *mx = 0;
        *my = 0;
        return;
    }
    /* Slow ocular drift: two incommensurate sines, +-4 Q8 (~0.4 deg). */
    int32_t dxq = fre_sin_q14(fre_turn16(t_ms, 5300U));
    int32_t dyq = fre_sin_q14(fre_turn16(t_ms, 3700U) + 21000U);
    int32_t x = (dxq * 4) / FRE_Q14;
    int32_t y = (dyq * 3) / FRE_Q14;
    /* Microsaccade epochs of 640 ms; ~70% contain one 20 ms dart. */
    uint32_t cycle = 640U;
    uint32_t mi = t_ms / cycle;
    uint32_t u = t_ms - mi * cycle;
    uint32_t h = fre_hash3(mi, FRE_SALT_MICRO_PHASE, c->seed);
    if ((h % 100U) < 70U) {
        uint32_t start = (h >> 8) % (cycle - 40U);
        uint32_t ha = fre_hash3(mi, FRE_SALT_MICRO_ANGLE, c->seed);
        int32_t ax = ((int32_t)(ha & 31U) - 16) / 2;
        int32_t ay = ((int32_t)((ha >> 5) & 15U) - 8) / 2;
        if (u >= start) {
            int32_t e = fre_smoothstep_q8(
                (int32_t)fre_min((int32_t)(u - start), 20) * 256 / 20);
            x += (ax * e) >> 8;
            y += (ay * e) >> 8;
        }
    }
    *mx = (x * c->tu->micro_pct) / 100;
    *my = (y * c->tu->micro_pct) / 100;
}

/* ------------------------------------------------------------------ */
/* Blinks                                                              */
/* ------------------------------------------------------------------ */

/*
 * Aperture contribution of a single blink event starting at u == 0:
 * ~75 ms active down-phase, short full-closure hold, then an asymptotic
 * reopening about twice as long as the closure (VanderWerf 2003), with a
 * small cartoon overshoot on the way up when `bounce` is set.
 * depth_q8: 256 closes fully. Returns aperture multiplier 0..~272.
 */
static int32_t fre_blink_wave(
    int32_t u_ms, int32_t reopen_ms, int32_t depth_q8, bool bounce)
{
    const int32_t close_ms = 75;
    const int32_t hold_ms = 20;
    if (u_ms < 0) {
        return 256;
    }
    if (u_ms < close_ms) {
        int32_t e = fre_ease_in_q8((u_ms * 256) / close_ms);
        return 256 - ((depth_q8 * e) >> 8);
    }
    if (u_ms < close_ms + hold_ms) {
        return 256 - depth_q8;
    }
    int32_t v = u_ms - close_ms - hold_ms;
    if (v < reopen_ms) {
        int32_t e = fre_ease_out_cubic_q8((v * 256) / reopen_ms);
        int32_t a = 256 - depth_q8 + ((depth_q8 * e) >> 8);
        if (bounce && e > 200) {
            /* Up to ~5% lid overshoot as the reopening settles. */
            a += ((e - 200) * 12) / 56;
        }
        return a;
    }
    if (bounce && v < reopen_ms + 90) {
        int32_t e = ((v - reopen_ms) * 256) / 90;
        return 256 + 12 - ((12 * e) >> 8);
    }
    return 256;
}

typedef struct {
    int32_t aperture_q8[2];
    int32_t min_aperture_q8;
    bool closing;
} fre_blink_state_t;

static void fre_solve_blinks(
    const fre_ctx_t *c,
    uint32_t t_ms,
    const fre_macro_gaze_t *mg,
    int32_t drowsy_rate_pct,
    fre_blink_state_t *out)
{
    uint32_t cycle = (uint32_t)(
        ((int32_t)c->ap->blink_cycle_ms * c->tu->blink_cycle_pct) / 100);
    if (drowsy_rate_pct != 100) {
        cycle = (uint32_t)((int32_t)cycle * 100 / fre_max(
            drowsy_rate_pct, 25));
    }
    if (cycle < 900U) {
        cycle = 900U;
    }
    int32_t reopen = (150 * c->tu->reopen_pct) / 100;
    int32_t a[2] = { 256, 256 };

    /* Spontaneous blinks: current and previous epoch (an event near the
     * end of the previous epoch may still be reopening). */
    for (uint32_t back = 0; back < 2U; ++back) {
        uint32_t bi = (t_ms / cycle);
        if (back > bi) {
            break;
        }
        bi -= back;
        uint32_t h = fre_hash3(bi, FRE_SALT_BLINK_PHASE, c->seed);
        uint32_t span = cycle > 700U ? cycle - 700U : 1U;
        uint32_t t0 = bi * cycle + (h % span);
        bool slow = c->tu->cat_slow_blink && ((h >> 16) % 100U) < 30U;
        /* Right eye trails by a hashed 0-10 ms; tiny amplitude asymmetry
         * keeps the two lids from feeling mechanically ganged. */
        int32_t skew = (int32_t)(
            fre_hash3(bi, FRE_SALT_BLINK_LEAD, c->seed) % 11U);
        int32_t asym = (256 - (int32_t)c->tu->asym_pct * 2);
        for (int eye = 0; eye < 2; ++eye) {
            int32_t u = (int32_t)(t_ms - t0) - (eye == 1 ? skew : 0);
            int32_t w;
            if (slow) {
                /* Feline affiliative slow blink: long, partial, no hold
                 * bounce. */
                int32_t su = u;
                if (su < 0) {
                    w = 256;
                } else if (su < 420) {
                    w = 256 - ((230 * fre_smoothstep_q8(
                        (su * 256) / 420)) >> 8);
                } else if (su < 720) {
                    w = 26;
                } else if (su < 1300) {
                    w = 26 + ((230 * fre_smoothstep_q8(
                        ((su - 720) * 256) / 580)) >> 8);
                } else {
                    w = 256;
                }
            } else {
                int32_t depth = eye == 1 ? asym : 256;
                w = fre_blink_wave(u, reopen, depth, true);
            }
            a[eye] = fre_min(a[eye], w);
        }
        /* Occasional doublet ~320 ms after the first blink. */
        if (!slow &&
            (fre_hash3(bi, FRE_SALT_BLINK_DOUBLE, c->seed) % 100U) < 12U) {
            for (int eye = 0; eye < 2; ++eye) {
                int32_t u = (int32_t)(t_ms - t0) - 320;
                a[eye] = fre_min(a[eye],
                    fre_blink_wave(u, reopen, 230, false));
            }
        }
    }

    /* Gaze-evoked blink: probability rises with saccade amplitude
     * (Evinger 1994: none below ~10 deg, certain above ~33 deg — our
     * range tops out at 25 deg so the ceiling maps to ~65%). */
    int32_t amp_deg = (mg->sacc_amp_q8 * FRE_GAZE_FULL_DEG) >> 8;
    if (mg->sacc_start_ms != 0U && amp_deg > 10) {
        uint32_t p = (uint32_t)((amp_deg - 10) * 100 / 23);
        if ((fre_hash3(mg->sacc_id, FRE_SALT_GAZE_BLINK, c->seed) % 100U)
            < p) {
            int32_t u = (int32_t)(t_ms - mg->sacc_start_ms) + 30;
            int32_t w = fre_blink_wave(u, reopen, 256, false);
            a[0] = fre_min(a[0], w);
            a[1] = fre_min(a[1], w);
        }
    }

    out->aperture_q8[0] = a[0];
    out->aperture_q8[1] = a[1];
    out->min_aperture_q8 = fre_min(a[0], a[1]);
    out->closing = false;
}

/* ------------------------------------------------------------------ */
/* Idle acts                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t id;
    int32_t gaze_dx_q8;
    int32_t gaze_dy_q8;
    int32_t aperture_scale_q8; /* 256 neutral */
    int32_t brow_add_q8[2];
    int32_t tilt_mdeg;
    int32_t shiver_dx_q4;
    int32_t wink_eye;          /* -1 none */
} fre_act_state_t;

#define FRE_ACT_EPOCH_MS 11000U

static uint16_t fre_act_allowed_mask(fre_activity_t activity)
{
    switch (activity) {
    case FRE_ACTIVITY_IDLE:
        return 0xFFFFU;
    case FRE_ACTIVITY_LISTENING:
        return (1U << FRE_ACT_BROW_FLASH) | (1U << FRE_ACT_TILT) |
               (1U << FRE_ACT_SLOW_BLINK);
    case FRE_ACTIVITY_THINKING:
        return (1U << FRE_ACT_SQUINT) | (1U << FRE_ACT_BROW_FLASH) |
               (1U << FRE_ACT_LOOK_UP_THINK);
    case FRE_ACTIVITY_SPEAKING:
    default:
        return (1U << FRE_ACT_BROW_FLASH);
    }
}

/* Envelope helper: ramp in, hold, ramp out; returns Q8. */
static int32_t fre_hold_env(
    int32_t u, int32_t ramp_in, int32_t hold, int32_t ramp_out)
{
    if (u < 0) {
        return 0;
    }
    if (u < ramp_in) {
        return fre_smoothstep_q8((u * 256) / ramp_in);
    }
    if (u < ramp_in + hold) {
        return 256;
    }
    if (u < ramp_in + hold + ramp_out) {
        return 256 - fre_smoothstep_q8(
            ((u - ramp_in - hold) * 256) / ramp_out);
    }
    return 0;
}

static void fre_solve_act(
    const fre_ctx_t *c, uint32_t t_ms, fre_act_state_t *out)
{
    out->id = FRE_ACT_NONE;
    out->gaze_dx_q8 = 0;
    out->gaze_dy_q8 = 0;
    out->aperture_scale_q8 = 256;
    out->brow_add_q8[0] = 0;
    out->brow_add_q8[1] = 0;
    out->tilt_mdeg = 0;
    out->shiver_dx_q4 = 0;
    out->wink_eye = -1;

    uint32_t ai = t_ms / FRE_ACT_EPOCH_MS;
    uint32_t u_epoch = t_ms - ai * FRE_ACT_EPOCH_MS;
    uint32_t h = fre_hash3(ai, FRE_SALT_ACT_KIND, c->seed);
    /* ~55% of epochs contain an act at all. */
    if ((h % 100U) >= 55U) {
        return;
    }
    static const uint8_t kinds[8] = {
        FRE_ACT_GLANCE_ASIDE, FRE_ACT_LOOK_UP_THINK, FRE_ACT_SQUINT,
        FRE_ACT_BROW_FLASH, FRE_ACT_WINK, FRE_ACT_DRIFT_REFOCUS,
        FRE_ACT_SHIVER, FRE_ACT_TILT,
    };
    uint8_t kind = kinds[(h >> 8) & 7U];
    if (kind == FRE_ACT_SHIVER && c->tu->cat_slow_blink) {
        kind = FRE_ACT_SLOW_BLINK;
    }
    if (kind == FRE_ACT_TILT && !c->tu->tilt_acts) {
        kind = FRE_ACT_GLANCE_ASIDE;
    }
    uint16_t allowed =
        fre_act_allowed_mask(c->activity) & c->tu->act_mask;
    if (((1U << kind) & allowed) == 0U) {
        return;
    }
    int32_t act_len;
    switch (kind) {
    case FRE_ACT_GLANCE_ASIDE:
        act_len = 1900;
        break;
    case FRE_ACT_LOOK_UP_THINK:
        act_len = 2300;
        break;
    case FRE_ACT_SQUINT:
        act_len = 1500;
        break;
    case FRE_ACT_BROW_FLASH:
        act_len = 600;
        break;
    case FRE_ACT_WINK:
        act_len = 700;
        break;
    case FRE_ACT_DRIFT_REFOCUS:
        act_len = 3200;
        break;
    case FRE_ACT_SHIVER:
        act_len = 500;
        break;
    case FRE_ACT_SLOW_BLINK:
        act_len = 1600;
        break;
    case FRE_ACT_TILT:
    default:
        act_len = 2800;
        break;
    }
    uint32_t start = (h >> 16) % (FRE_ACT_EPOCH_MS - (uint32_t)act_len);
    int32_t u = (int32_t)u_epoch - (int32_t)start;
    if (u < 0 || u >= act_len) {
        return;
    }
    out->id = kind;
    int32_t side =
        (fre_hash3(ai, FRE_SALT_ACT_SIDE, c->seed) & 1U) ? 1 : -1;
    switch (kind) {
    case FRE_ACT_GLANCE_ASIDE: {
        /* Saccade out, hold, saccade back with a small return blink is
         * produced naturally by the aperture dip below. */
        int32_t e = fre_hold_env(u, 70, act_len - 320, 250);
        out->gaze_dx_q8 = side * ((150 * e) >> 8);
        out->gaze_dy_q8 = (20 * e) >> 8;
        break;
    }
    case FRE_ACT_LOOK_UP_THINK: {
        int32_t e = fre_hold_env(u, 90, act_len - 490, 400);
        out->gaze_dx_q8 = side * ((70 * e) >> 8);
        out->gaze_dy_q8 = -((130 * e) >> 8);
        /* One brow slightly raised while consulting the ceiling. */
        out->brow_add_q8[side > 0 ? 1 : 0] = (70 * e) >> 8;
        break;
    }
    case FRE_ACT_SQUINT: {
        int32_t e = fre_hold_env(u, 260, act_len - 700, 440);
        out->aperture_scale_q8 = 256 - ((110 * e) >> 8);
        out->brow_add_q8[0] = -((50 * e) >> 8);
        out->brow_add_q8[1] = -((50 * e) >> 8);
        break;
    }
    case FRE_ACT_BROW_FLASH: {
        int32_t e = fre_hold_env(u, 120, 200, 280);
        out->brow_add_q8[0] = (110 * e) >> 8;
        out->brow_add_q8[1] = (110 * e) >> 8;
        break;
    }
    case FRE_ACT_WINK: {
        int32_t e = fre_hold_env(u, 90, 260, 240);
        out->wink_eye = side > 0 ? 1 : 0;
        out->aperture_scale_q8 = 256 - ((246 * e) >> 8);
        out->brow_add_q8[side > 0 ? 1 : 0] = -((40 * e) >> 8);
        break;
    }
    case FRE_ACT_DRIFT_REFOCUS: {
        /* Attention wanders off slowly, then a crisp saccade snaps it
         * back: slow-out then a fast return (anticipation, release). */
        int32_t drift_len = act_len - 420;
        if (u < drift_len) {
            int32_t e = fre_ease_in_q8((u * 256) / drift_len);
            out->gaze_dx_q8 = side * ((90 * e) >> 8);
            out->gaze_dy_q8 = (55 * e) >> 8;
            out->aperture_scale_q8 = 256 - ((40 * e) >> 8);
        } else {
            int32_t e = 256 - fre_smoothstep_q8(
                ((u - drift_len) * 256) / 90);
            if (e < 0) {
                e = 0;
            }
            out->gaze_dx_q8 = side * ((90 * e) >> 8);
            out->gaze_dy_q8 = (55 * e) >> 8;
            out->aperture_scale_q8 = 256;
        }
        break;
    }
    case FRE_ACT_SHIVER: {
        /* RoboEyes-style horizontal flicker with decay. */
        int32_t decay = 256 - (u * 256) / act_len;
        int32_t w = fre_sin_q14((uint32_t)u * 65536U / 90U);
        out->shiver_dx_q4 = (int32_t)fre_sar64(
            (int64_t)w * 3 * decay, 14 + 8);
        break;
    }
    case FRE_ACT_SLOW_BLINK: {
        int32_t e = fre_hold_env(u, 420, 400, 620);
        out->aperture_scale_q8 = 256 - ((225 * e) >> 8);
        break;
    }
    case FRE_ACT_TILT: {
        int32_t e = fre_hold_env(u, 420, act_len - 1000, 560);
        out->tilt_mdeg = side * ((6500 * e) >> 8);
        /* Curiosity widens the eyes a touch. */
        out->aperture_scale_q8 = 256 + ((26 * e) >> 8);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Drowsiness (sleep_wake)                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t aperture_cap_q8; /* 256 fully alert */
    int32_t gaze_sink_q8;    /* eyes roll down as lids fall */
    int32_t breath_scale_q8; /* 256 neutral, larger = deeper */
    int32_t rate_pct;        /* blink-cycle scale, 100 = neutral */
} fre_drowsy_t;

#define FRE_DOZE_CYCLE_MS 45000U

static void fre_solve_drowsy(
    const fre_ctx_t *c, uint32_t t_ms, fre_drowsy_t *out)
{
    out->aperture_cap_q8 = 256;
    out->gaze_sink_q8 = 0;
    out->breath_scale_q8 = 256;
    out->rate_pct = 100;
    if (!c->tu->drowsy || c->activity != FRE_ACTIVITY_IDLE) {
        return;
    }
    uint32_t u = t_ms % FRE_DOZE_CYCLE_MS;
    if (u < 14000U) {
        return;
    }
    if (u < 30000U) {
        /* Drooping: lids sag with slow oscillation, punctuated by
         * startle recoveries — the nod-off catch. */
        int32_t p = (int32_t)(u - 14000U);
        int32_t sag = 256 - (p * 186) / 16000;
        int32_t osc = (fre_sin_q14((uint32_t)p * 65536U / 2600U) * 30) /
            FRE_Q14;
        int32_t cap = sag + osc;
        uint32_t si = (u - 14000U) / 4000U;
        uint32_t hs = fre_hash3(si, FRE_SALT_DOZE, c->seed);
        if ((hs % 100U) < 40U) {
            int32_t s0 = (int32_t)((hs >> 8) % 3100U);
            int32_t su = (p - si * 4000U > 0)
                ? (int32_t)(p - (int32_t)(si * 4000U)) - s0 : -1;
            if (su >= 0 && su < 1000) {
                /* Quick reopen, slow sink back. */
                int32_t rec;
                if (su < 140) {
                    rec = fre_smoothstep_q8((su * 256) / 140);
                } else {
                    rec = 256 - fre_smoothstep_q8(((su - 140) * 256) / 860);
                }
                cap += ((250 - cap) * rec) >> 8;
            }
        }
        out->aperture_cap_q8 = fre_clamp(cap, 60, 256);
        out->gaze_sink_q8 = (256 - out->aperture_cap_q8) / 4;
        out->breath_scale_q8 = 256 + (256 - out->aperture_cap_q8) / 2;
        out->rate_pct = 140;
        return;
    }
    if (u < 42000U) {
        /* Asleep: lids shut, deep slow breathing, rare micro-twitches. */
        int32_t cap = 4;
        uint32_t ti = (u - 30000U) / 1500U;
        uint32_t ht = fre_hash3(ti, FRE_SALT_TWITCH, c->seed);
        if ((ht % 100U) < 18U) {
            int32_t s0 = (int32_t)((ht >> 8) % 1300U);
            int32_t su = (int32_t)(u - 30000U - ti * 1500U) - s0;
            if (su >= 0 && su < 110) {
                cap += 24;
            }
        }
        out->aperture_cap_q8 = cap;
        out->gaze_sink_q8 = 50;
        out->breath_scale_q8 = 460;
        out->rate_pct = 100;
        return;
    }
    /* Waking: lids climb with a flutter, posture straightens. */
    int32_t p = (int32_t)(u - 42000U);
    int32_t e = fre_smoothstep_q8((p * 256) / 3000);
    int32_t flutter = (fre_sin_q14((uint32_t)p * 65536U / 260U) * 30) /
        FRE_Q14;
    int32_t cap = 4 + ((252 * e) >> 8) + ((flutter * (256 - e)) >> 8);
    out->aperture_cap_q8 = fre_clamp(cap, 0, 256);
    out->gaze_sink_q8 = (50 * (256 - e)) >> 8;
    out->breath_scale_q8 = 460 - ((204 * e) >> 8);
    out->rate_pct = 60;
}

/* ------------------------------------------------------------------ */
/* Public solve                                                        */
/* ------------------------------------------------------------------ */

bool fre_behavior_solve(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    uint32_t sample_clock,
    fre_rig_t *rig)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    if (def == NULL || keyframe == NULL || rig == NULL) {
        return false;
    }
    fre_ctx_t c;
    c.tu = &def->tuning;
    uint8_t act_raw = keyframe->expression;
    c.activity = (act_raw <= FRE_ACTIVITY_SPEAKING)
        ? (fre_activity_t)act_raw : FRE_ACTIVITY_IDLE;
    if ((keyframe->flags & FRE_KEYFRAME_FLAG_SPEAKING) != 0U &&
        c.activity == FRE_ACTIVITY_IDLE) {
        c.activity = FRE_ACTIVITY_SPEAKING;
    }
    c.ap = &FRE_ACTIVITY[c.activity];
    c.seed = (uint32_t)profile * 0x9E37U + 0xA5A5U;
    c.kf = keyframe;

    uint32_t t_ms = sample_clock / (FRE_SAMPLE_RATE_HZ / 1000U);

    fre_drowsy_t dz;
    fre_solve_drowsy(&c, t_ms, &dz);

    fre_macro_gaze_t mg;
    fre_solve_macro_gaze(&c, t_ms, &mg);

    int32_t rx = 0, ry = 0;
    if (!mg.averted) {
        fre_solve_refixation(&c, t_ms, &rx, &ry);
    }
    int32_t mx, my;
    fre_solve_micro(&c, t_ms, &mx, &my);

    fre_act_state_t act;
    fre_solve_act(&c, t_ms, &act);

    fre_blink_state_t bl;
    fre_solve_blinks(&c, t_ms, &mg, dz.rate_pct, &bl);

    /* Host gaze is the social anchor; procedural streams layer on top. */
    int32_t host_x = (int32_t)keyframe->look_x * 2;
    int32_t host_y = (int32_t)keyframe->look_y * 2;
    int32_t gx = host_x + mg.x_q8 + rx + mx + act.gaze_dx_q8;
    int32_t gy = host_y + mg.y_q8 + ry + my + act.gaze_dy_q8 +
        dz.gaze_sink_q8;
    rig->gaze_x_q8 = fre_clamp(gx, -256, 256);
    rig->gaze_y_q8 = fre_clamp(gy, -256, 256);

    /*
     * Lid anticipation: the lids read the whole planned gaze a lookahead
     * into the future — macro saccades, refixations, micro drift, and
     * acts alike — so they begin to rise or fall before the eyes move.
     * Cheap because the solver is stateless: evaluating "later" costs
     * the same hashes as evaluating "now".
     */
    int32_t lead = c.tu->lid_lead_ms;
    if (lead > 0) {
        uint32_t t2 = t_ms + (uint32_t)lead;
        fre_macro_gaze_t mg2;
        fre_solve_macro_gaze(&c, t2, &mg2);
        int32_t rx2 = 0, ry2 = 0;
        if (!mg2.averted) {
            fre_solve_refixation(&c, t2, &rx2, &ry2);
        }
        int32_t mx2, my2;
        fre_solve_micro(&c, t2, &mx2, &my2);
        fre_act_state_t act2;
        fre_solve_act(&c, t2, &act2);
        int32_t gy2 = host_y + mg2.y_q8 + ry2 + my2 + act2.gaze_dy_q8 +
            dz.gaze_sink_q8;
        rig->lid_gaze_y_q8 = fre_clamp(gy2, -256, 256);
    } else {
        rig->lid_gaze_y_q8 = rig->gaze_y_q8;
    }

    /* Aperture: host valve, blink wave, act scaling, drowsiness cap,
     * and vertical lid-gaze coupling (Becker & Fuchs: the upper lid is
     * yoked to eye elevation — down-gaze narrows, up-gaze widens). */
    int32_t host_open[2] = {
        ((int32_t)keyframe->eye_left_open * 256) / 255,
        ((int32_t)keyframe->eye_right_open * 256) / 255,
    };
    if ((keyframe->flags & FRE_KEYFRAME_FLAG_BLINKING) != 0U) {
        host_open[0] = fre_min(host_open[0], 44);
        host_open[1] = fre_min(host_open[1], 44);
    }
    int32_t follow;
    if (rig->lid_gaze_y_q8 > 0) {
        follow = 256 - (rig->lid_gaze_y_q8 * 90) / 256;
    } else {
        follow = 256 - (rig->lid_gaze_y_q8 * 38) / 256;
    }
    for (int eye = 0; eye < 2; ++eye) {
        int32_t a = host_open[eye];
        a = (a * bl.aperture_q8[eye]) >> 8;
        int32_t scale = act.aperture_scale_q8;
        if (act.wink_eye >= 0 && act.wink_eye != eye) {
            scale = 256;
        }
        a = (a * scale) >> 8;
        a = (a * follow) >> 8;
        a = fre_min(a, dz.aperture_cap_q8);
        rig->openness_q8[eye] = fre_clamp(a, 0, 280);
    }

    /* Brows: host byte, arousal posture, act choreography, blink dip,
     * and a speech-emphasis beat on loud syllables. */
    int32_t arousal = c.ap->arousal;
    if (dz.aperture_cap_q8 < 256) {
        arousal = (arousal * dz.aperture_cap_q8) >> 8;
    }
    rig->arousal_q8 = arousal;
    int32_t brow_base = (int32_t)keyframe->brow * 2;
    int32_t brow_arousal = ((arousal - 128) * 60) / 128;
    int32_t brow_speech = 0;
    if (c.activity == FRE_ACTIVITY_SPEAKING &&
        keyframe->mouth_open > 150U) {
        brow_speech = (((int32_t)keyframe->mouth_open - 150) * 60) / 105;
    }
    int32_t blink_dip = (256 - fre_min(bl.min_aperture_q8, 256)) / 10;
    for (int eye = 0; eye < 2; ++eye) {
        int32_t b = brow_base + brow_arousal + brow_speech - blink_dip +
            act.brow_add_q8[eye];
        b = (b * c.tu->brow_gain_pct) / 100;
        rig->brow_raise_q8[eye] = fre_clamp(b, -256, 256);
    }
    /* Thinking knits the brows asymmetrically toward the averted side. */
    int32_t tilt_l = 0, tilt_r = 0;
    if (c.activity == FRE_ACTIVITY_THINKING) {
        uint32_t bout = t_ms / 6200U;
        int32_t tside =
            (fre_hash3(bout, FRE_SALT_THINK_SIDE, c.seed) & 1U) ? 1 : -1;
        tilt_l = tside > 0 ? -60 : 25;
        tilt_r = tside > 0 ? 25 : -60;
    }
    rig->brow_tilt_q8[0] = fre_clamp(
        (tilt_l * c.tu->brow_gain_pct) / 100, -256, 256);
    rig->brow_tilt_q8[1] = fre_clamp(
        (tilt_r * c.tu->brow_gain_pct) / 100, -256, 256);

    /* Squash and stretch: saccades stretch along the direction of travel
     * (windowed at mid-flight); deep blinks squeeze the eye a touch. */
    int32_t sx = 256, sy = 256;
    if (mg.in_flight && mg.sacc_dur_ms > 0) {
        int32_t u = (int32_t)(t_ms - mg.sacc_start_ms);
        int32_t w = fre_sin_q14(
            (uint32_t)((u * 32768) / mg.sacc_dur_ms));
        int32_t s = fre_min(20, mg.sacc_amp_q8 / 7);
        int32_t boost = (int32_t)fre_sar64((int64_t)w * s, 14);
        sx += boost;
        sy -= boost / 2;
    }
    int32_t squeeze = (256 - fre_min(bl.min_aperture_q8, 256)) / 18;
    sx += squeeze;
    rig->scale_x_q8 = sx;
    rig->scale_y_q8 = sy;
    rig->saccade_active = mg.in_flight;

    /* Breathing: slow sinusoidal settle of the whole face. */
    uint32_t period = c.ap->breath_period_ms;
    period = (uint32_t)((int32_t)period * 256 /
        fre_max(dz.breath_scale_q8 > 256 ? 160 : 256, 120));
    int32_t breath_amp = 300;
    breath_amp = (breath_amp * dz.breath_scale_q8) >> 8;
    rig->breath_y_q8 = (fre_sin_q14(fre_turn16(t_ms, period)) *
        breath_amp) / FRE_Q14;

    rig->tilt_mdeg = act.tilt_mdeg;
    rig->act_id = act.id;

    /* Pupil: arousal-coupled dilation plus slow hippus wobble. */
    int32_t pupil = 128 + ((arousal - 128) * 90) / 128;
    pupil += (fre_sin_q14(fre_turn16(t_ms, 6100U)) * 10) / FRE_Q14;
    pupil += (fre_sin_q14(fre_turn16(t_ms, 3700U) + 9000U) * 6) / FRE_Q14;
    rig->pupil_q8 = fre_clamp(pupil, 40, 230);

    return true;
}
