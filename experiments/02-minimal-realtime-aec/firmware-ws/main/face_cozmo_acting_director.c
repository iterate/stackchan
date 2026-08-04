#include "face_cozmo_acting_internal.h"

/*
 * The director: a stateless performance generator.
 *
 * Time is divided per channel into fixed epochs; every stochastic choice
 * inside an epoch (does a blink happen, where does the gaze wander, which
 * idle act plays) is drawn from fca_hash of the epoch index, so the whole
 * performance is a pure function of the sample clock. Every envelope is
 * built to start and end at rest strictly inside its epoch, which makes
 * epoch boundaries invisible and 30 fps sampling free of steps.
 *
 * Constants follow the physiology and social-gaze literature also cited by
 * the sibling robot-eyes contribution (blink down-phase near 75 ms with a
 * slower reopening; saccade main sequence near 25 ms + 2.2 ms/deg; ~90% of
 * saccades under 15 deg; upward cognitive aversions, sideways speaking
 * aversions; microsaccades 1-2 per second under 0.4 deg), retuned for a
 * character whose eyes are the entire body.
 */

/*
 * Blink and dart timing runs deliberately slower than the human
 * literature values (75 ms down-phase, ~2.4 ms/deg saccades): these
 * eyes fill a quarter of the display, so at a 30 fps sample cadence a
 * literature-speed sweep moves too much luminous area in one frame and
 * reads as a cut. The stretched envelopes keep the asymmetry (fast-ish
 * close, slow reopen) that makes blinks look organic.
 */
enum {
    FCA_BLINK_CLOSE_MS = 122,
    FCA_BLINK_HOLD_MS = 30,
    FCA_BLINK_REOPEN_MS = 225,
    FCA_BLINK_SETTLE_MS = 105, /* overshoot decay after reopen */
    FCA_DOUBLE_GAP_MS = 300,
    FCA_LID_LEAD_MS = 60,
    FCA_GAZE_FULL_DEG = 24, /* 256 Q8 gaze units == this many degrees */
};

/* Total footprint of one blink event including settle. */
static int32_t fca_blink_span_ms(int32_t reopen_scale_q8)
{
    return FCA_BLINK_CLOSE_MS + FCA_BLINK_HOLD_MS +
           ((FCA_BLINK_REOPEN_MS * reopen_scale_q8) >> 8) +
           FCA_BLINK_SETTLE_MS;
}

/*
 * Aperture multiplier of one blink event at time u after onset.
 * 256 fully open, 0 closed; reopening may overshoot above 256.
 */
static int32_t fca_blink_wave(
    int32_t u_ms, int32_t reopen_scale_q8, int32_t overshoot_q8)
{
    if (u_ms <= 0) {
        return 256;
    }
    const int32_t reopen_ms =
        (FCA_BLINK_REOPEN_MS * reopen_scale_q8) >> 8;
    if (u_ms < FCA_BLINK_CLOSE_MS) {
        /* Eased fall: quick through the middle, soft at both ends. */
        const int32_t e = (u_ms * 256) / FCA_BLINK_CLOSE_MS;
        return 256 - fca_smooth_q8(e);
    }
    if (u_ms < FCA_BLINK_CLOSE_MS + FCA_BLINK_HOLD_MS) {
        return 0;
    }
    const int32_t v = u_ms - FCA_BLINK_CLOSE_MS - FCA_BLINK_HOLD_MS;
    if (v < reopen_ms) {
        /* Decelerating rise into a slight overshoot. */
        const int32_t e = (v * 256) / fca_max(reopen_ms, 1);
        return (fca_ease_out_q8(e) * (256 + overshoot_q8)) >> 8;
    }
    const int32_t w = v - reopen_ms;
    if (w < FCA_BLINK_SETTLE_MS) {
        /* Overshoot settles back to unity. */
        const int32_t e = (w * 256) / FCA_BLINK_SETTLE_MS;
        return 256 + overshoot_q8 -
               ((overshoot_q8 * fca_smooth_q8(e)) >> 8);
    }
    return 256;
}

typedef struct {
    int32_t aperture_q8[2];
    uint8_t state; /* 0 open, 1 closing, 2 closed, 3 reopening */
} fca_blink_out_t;

static void fca_blink_eval_event(
    uint32_t t_ms,
    uint32_t onset_ms,
    int32_t reopen_scale_q8,
    int32_t overshoot_q8,
    int32_t asym_ms,
    fca_blink_out_t *out)
{
    const int32_t u = (int32_t)(t_ms - onset_ms);
    if (u <= -asym_ms || u >= fca_blink_span_ms(reopen_scale_q8)) {
        return;
    }
    const int32_t left = fca_blink_wave(u, reopen_scale_q8, overshoot_q8);
    const int32_t right =
        fca_blink_wave(u - asym_ms, reopen_scale_q8, overshoot_q8);
    out->aperture_q8[0] = fca_min(out->aperture_q8[0], left);
    out->aperture_q8[1] = fca_min(out->aperture_q8[1], right);
    const int32_t lead = fca_min(left, right);
    if (u < FCA_BLINK_CLOSE_MS) {
        out->state = 1;
    } else if (lead <= 8) {
        out->state = 2;
    } else if (out->state == 0) {
        out->state = 3;
    }
}

/*
 * Spontaneous blinks. One epoch per blink period; the onset sits in the
 * first 55% of the epoch so a double blink plus settle always finishes
 * before the epoch closes.
 */
static void fca_blink_channel(
    const fca_direction_t *dir,
    uint8_t activity,
    uint32_t t_ms,
    uint32_t seed,
    int32_t slow_blink_q8,
    fca_blink_out_t *out)
{
    out->aperture_q8[0] = 256;
    out->aperture_q8[1] = 256;
    out->state = 0;

    const uint32_t period = dir->blink_period_ms[activity & 3U];
    const int32_t overshoot = dir->reopen_overshoot_q8;
    const int32_t asym = dir->blink_asym_ms;

    /* The previous epoch's event can spill into this one only if the
     * period were shorter than a blink; periods are >= 1500 ms and spans
     * are <= 620 ms, so evaluating the current epoch alone is exact. */
    const uint32_t epoch = t_ms / period;
    const uint32_t h = fca_hash(epoch, FCA_TAG_BLINK, seed);
    /* 6% of epochs skip their blink entirely; life is irregular. */
    if ((h % 100U) < 6U) {
        return;
    }
    const uint32_t hk = fca_hash(epoch, FCA_TAG_BLINK_KIND, seed);
    const bool is_double = (hk % 100U) < 12U;
    int32_t reopen_scale = 256;
    if (slow_blink_q8 > 0) {
        /* An affectionate slow blink stretches the reopen phase. */
        reopen_scale = 256 + ((slow_blink_q8 * 640) >> 8);
    }
    const uint32_t span =
        (uint32_t)fca_blink_span_ms(reopen_scale) +
        (is_double ? (uint32_t)FCA_DOUBLE_GAP_MS : 0U);
    uint32_t window = (period * 55U) / 100U;
    if (span + 40U < window) {
        window -= span;
    } else {
        window = 1U;
    }
    const uint32_t onset =
        epoch * period + 30U +
        (fca_hash(epoch, FCA_TAG_BLINK_OFFSET, seed) % window);
    fca_blink_eval_event(
        t_ms, onset, reopen_scale, overshoot, asym, out);
    if (is_double) {
        fca_blink_eval_event(
            t_ms, onset + (uint32_t)FCA_DOUBLE_GAP_MS,
            reopen_scale, overshoot / 2, asym, out);
    }
}

/* ---- macro gaze ------------------------------------------------------ */

/*
 * Inverse CDF of an exponential aversion magnitude (mean ~7 deg within a
 * 24 deg range), sampled at 16 equiprobable points, Q8 of full travel.
 */
static const uint8_t FCA_MAG_Q8[16] = {
    236, 176, 143, 120, 102, 88, 75, 64, 55, 46, 38, 31, 24, 18, 12, 6,
};

/*
 * Cumulative direction tables (R, UR, U, UL, L, DL, D, DR out of 255):
 * index 0 neutral roaming, 1 cognitive up-bias, 2 social side-bias,
 * 3 bashful down-bias.
 */
static const uint8_t FCA_DIR_CDF[4][8] = {
    { 42, 60, 100, 122, 166, 186, 232, 255 },
    { 22, 62, 138, 178, 202, 216, 238, 255 },
    { 64, 76, 94, 106, 170, 192, 232, 255 },
    { 34, 44, 56, 66, 100, 150, 216, 255 },
};

static const int16_t FCA_DIR_X[8] = {
    256, 181, 0, -181, -256, -181, 0, 181,
};
static const int16_t FCA_DIR_Y[8] = {
    0, -181, -256, -181, 0, 181, 256, 181,
};

/* Stretched main-sequence duration for a Q8 amplitude (see the
 * timing note at the top of this file). */
static int32_t fca_saccade_ms(int32_t amp_q8)
{
    const int32_t deg = (amp_q8 * FCA_GAZE_FULL_DEG) >> 8;
    return 30 + (deg * 36) / 10;
}

/* Saccade progress with a 5% overshoot and 70 ms settle. */
static int32_t fca_saccade_q8(int32_t u_ms, int32_t dur_ms)
{
    if (u_ms <= 0) {
        return 0;
    }
    const int32_t peak = 269; /* 256 * 1.05 */
    if (u_ms < dur_ms) {
        return (fca_smooth_q8((u_ms * 256) / dur_ms) * peak) >> 8;
    }
    if (u_ms < dur_ms + 70) {
        const int32_t e = ((u_ms - dur_ms) * 256) / 70;
        return peak - ((13 * fca_smooth_q8(e)) >> 8);
    }
    return 256;
}

typedef struct {
    int32_t x_q8;
    int32_t y_q8;
    bool in_flight;
    int32_t flight_vx_q8;   /* direction of the current dart */
    int32_t flight_vy_q8;
    int32_t flight_e_q8;    /* 0..256 progress of the dart */
    /* Onset and amplitude of the outbound dart, for evoked blinks. */
    uint32_t out_onset_ms;
    int32_t out_amp_q8;
    bool has_aversion;
} fca_avert_out_t;

typedef struct {
    uint32_t period;
    uint32_t start_off;
    uint32_t hold_len;
    int32_t tx_q8;
    int32_t ty_q8;
    bool has;
} fca_avert_epoch_t;

static void fca_avert_epoch(
    const fca_direction_t *dir,
    uint8_t activity,
    uint8_t dir_table,
    int32_t wander_scale_q8,
    uint32_t epoch,
    uint32_t seed,
    fca_avert_epoch_t *ep)
{
    const uint32_t period = dir->avert_period_ms[activity & 3U];
    ep->period = period;
    const uint32_t h = fca_hash(epoch, FCA_TAG_AVERT, seed);
    ep->has = (h % 100U) < dir->avert_prob_pct[activity & 3U];
    /* Hold between 26% and 52% of the period: outbound and return darts
     * plus settles always fit with margins on both sides. */
    const uint32_t hold_span = (period * 26U) / 100U;
    ep->hold_len =
        (period * 26U) / 100U +
        (fca_hash(epoch, FCA_TAG_AVERT_LEN, seed) % (hold_span + 1U));
    const uint32_t margin = 260U;
    const uint32_t latest =
        period > ep->hold_len + 2U * margin
            ? period - ep->hold_len - 2U * margin
            : 1U;
    ep->start_off = margin + ((h >> 10) % latest);

    const uint32_t hd = fca_hash(epoch, FCA_TAG_AVERT_DIR, seed);
    const uint8_t *cdf = FCA_DIR_CDF[dir_table & 3U];
    const uint32_t pick = hd & 0xFFU;
    uint32_t bin = 0U;
    while (bin < 7U && pick > cdf[bin]) {
        ++bin;
    }
    int32_t mag = FCA_MAG_Q8[(hd >> 8) & 15U];
    mag = fca_mul_q8(mag, dir->wander_q8[activity & 3U]);
    mag = fca_mul_q8(mag, wander_scale_q8);
    /* +-12% jitter so a repeated bin is never the identical pose. */
    const int32_t jx = (int32_t)((hd >> 12) & 63U) - 32;
    const int32_t jy = (int32_t)((hd >> 18) & 63U) - 32;
    ep->tx_q8 = fca_clamp(
        (int32_t)fca_sar64(
            (int64_t)(FCA_DIR_X[bin] + jx) * mag, 8), -256, 256);
    ep->ty_q8 = fca_clamp(
        (int32_t)fca_sar64(
            (int64_t)(FCA_DIR_Y[bin] + jy) * mag, 8), -256, 256);
    /* Cognitive aversions ride above the horizon. */
    if (dir_table == 1U && ep->ty_q8 > -24) {
        ep->ty_q8 -= 46;
    }
}

static void fca_avert_channel(
    const fca_direction_t *dir,
    uint8_t activity,
    uint8_t dir_table,
    int32_t wander_scale_q8,
    uint32_t t_ms,
    uint32_t seed,
    fca_avert_out_t *out)
{
    const uint32_t period = dir->avert_period_ms[activity & 3U];
    const uint32_t epoch = t_ms / period;
    fca_avert_epoch_t ep;
    fca_avert_epoch(
        dir, activity, dir_table, wander_scale_q8, epoch, seed, &ep);

    out->x_q8 = 0;
    out->y_q8 = 0;
    out->in_flight = false;
    out->flight_vx_q8 = 0;
    out->flight_vy_q8 = 0;
    out->flight_e_q8 = 0;
    out->out_onset_ms = 0U;
    out->out_amp_q8 = 0;
    out->has_aversion = ep.has;
    if (!ep.has) {
        return;
    }

    const uint32_t local = t_ms - epoch * period;
    const int32_t amp_q8 = (int32_t)fca_isqrt(
        (uint32_t)(ep.tx_q8 * ep.tx_q8 + ep.ty_q8 * ep.ty_q8));
    const int32_t dur_out = fca_saccade_ms(amp_q8);
    const int32_t dur_back = fca_saccade_ms(amp_q8);
    out->out_onset_ms = epoch * period + ep.start_off;
    out->out_amp_q8 = amp_q8;

    if (local < ep.start_off) {
        return;
    }
    const uint32_t u = local - ep.start_off;
    if (u < (uint32_t)(dur_out + 70)) {
        const int32_t e = fca_saccade_q8((int32_t)u, dur_out);
        out->x_q8 = (int32_t)fca_sar64((int64_t)ep.tx_q8 * e, 8);
        out->y_q8 = (int32_t)fca_sar64((int64_t)ep.ty_q8 * e, 8);
        out->in_flight = u < (uint32_t)dur_out;
        out->flight_vx_q8 = ep.tx_q8;
        out->flight_vy_q8 = ep.ty_q8;
        out->flight_e_q8 = fca_clamp(
            ((int32_t)u * 256) / fca_max(dur_out, 1), 0, 256);
        return;
    }
    if (u < ep.hold_len) {
        out->x_q8 = ep.tx_q8;
        out->y_q8 = ep.ty_q8;
        return;
    }
    const uint32_t v = u - ep.hold_len;
    if (v < (uint32_t)(dur_back + 70)) {
        const int32_t e = fca_saccade_q8((int32_t)v, dur_back);
        out->x_q8 = ep.tx_q8 -
            (int32_t)fca_sar64((int64_t)ep.tx_q8 * e, 8);
        out->y_q8 = ep.ty_q8 -
            (int32_t)fca_sar64((int64_t)ep.ty_q8 * e, 8);
        out->in_flight = v < (uint32_t)dur_back;
        out->flight_vx_q8 = -ep.tx_q8;
        out->flight_vy_q8 = -ep.ty_q8;
        out->flight_e_q8 = fca_clamp(
            ((int32_t)v * 256) / fca_max(dur_back, 1), 0, 256);
        return;
    }
    /* Settled back on the shared attention point. */
}

/* ---- micro fixational motion ----------------------------------------- */

static void fca_micro_channel(
    const fca_direction_t *dir,
    uint32_t t_ms,
    uint32_t seed,
    int32_t *dx_q8,
    int32_t *dy_q8)
{
    *dx_q8 = 0;
    *dy_q8 = 0;
    if (dir->micro_gain_q8 == 0U) {
        return;
    }
    /* Slow ocular drift: two incommensurate sines per axis. */
    const int32_t g = dir->micro_gain_q8;
    const int32_t drift_x =
        (fca_sin_q14(fca_turn16(t_ms, 2731U)) +
         fca_sin_q14(fca_turn16(t_ms + 911U, 4177U))) / 2;
    const int32_t drift_y =
        (fca_sin_q14(fca_turn16(t_ms + 501U, 3391U)) +
         fca_sin_q14(fca_turn16(t_ms + 137U, 5279U))) / 2;
    *dx_q8 = (int32_t)fca_sar64((int64_t)drift_x * g, 14 + 5);
    *dy_q8 = (int32_t)fca_sar64((int64_t)drift_y * g, 14 + 6);

    /* Microsaccade: a tiny out-and-back pulse inside a 700 ms epoch. */
    const uint32_t epoch = t_ms / 700U;
    const uint32_t h = fca_hash(epoch, FCA_TAG_MICRO, seed);
    if ((h % 100U) >= 62U) {
        return;
    }
    const uint32_t onset = (h >> 8) % 560U;
    const uint32_t local = t_ms - epoch * 700U;
    if (local < onset || local >= onset + 90U) {
        return;
    }
    const int32_t e = fca_pulse_q8(
        (int32_t)((local - onset) * 256U / 90U));
    const uint32_t hd = fca_hash(epoch, FCA_TAG_MICRO_DIR, seed);
    const int32_t ax = (int32_t)(hd & 15U) - 8;
    const int32_t ay = (int32_t)((hd >> 4) & 15U) - 8;
    *dx_q8 += (ax * e * g) >> (8 + 7);
    *dy_q8 += (ay * e * g) >> (8 + 8);
}

/* ---- idle acts -------------------------------------------------------- */

typedef struct {
    uint8_t id;
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    int32_t squint_q8;
    int32_t roll_mdeg;
    int32_t slow_blink_q8;
} fca_act_out_t;

/* Chance (percent) that an act epoch actually performs, per activity. */
static const uint8_t FCA_ACT_GATE_PCT[4] = { 58, 22, 46, 12 };

static void fca_act_channel(
    const fca_direction_t *dir,
    uint8_t activity,
    int32_t arousal_q8,
    uint32_t t_ms,
    uint32_t seed,
    fca_act_out_t *out)
{
    out->id = FACE_COZMO_ACT_NONE;
    out->gaze_x_q8 = 0;
    out->gaze_y_q8 = 0;
    out->squint_q8 = 0;
    out->roll_mdeg = 0;
    out->slow_blink_q8 = 0;

    const uint32_t period = (uint32_t)dir->act_every_s * 1000U;
    if (period == 0U) {
        return;
    }
    const uint32_t epoch = t_ms / period;
    const uint32_t h = fca_hash(epoch, FCA_TAG_ACT, seed);
    if ((h % 100U) >= FCA_ACT_GATE_PCT[activity & 3U]) {
        return;
    }
    /* Very sleepy characters stop performing. */
    if (arousal_q8 < 40) {
        return;
    }
    const uint32_t hk = fca_hash(epoch, FCA_TAG_ACT_KIND, seed);
    uint8_t kind = (uint8_t)(1U + (hk % 6U));
    if (activity == 2U && (hk & 0x80U) != 0U) {
        kind = FACE_COZMO_ACT_THINK_UP; /* thinking prefers to look up */
    }
    const uint32_t dur =
        kind == FACE_COZMO_ACT_THINK_UP ? 2600U :
        kind == FACE_COZMO_ACT_REFOCUS ? 1900U :
        kind == FACE_COZMO_ACT_SLOW_BLINK ? 900U : 1300U;
    const uint32_t margin = 150U;
    const uint32_t latest =
        period > dur + 2U * margin ? period - dur - 2U * margin : 1U;
    const uint32_t onset = margin + ((h >> 9) % latest);
    const uint32_t local = t_ms - epoch * period;
    if (local < onset || local >= onset + dur) {
        return;
    }
    const int32_t e =
        fca_pulse_q8((int32_t)((local - onset) * 256U / dur));
    const int32_t side =
        (fca_hash(epoch, FCA_TAG_ACT_SIDE, seed) & 1U) != 0U ? 1 : -1;

    out->id = kind;
    switch (kind) {
    case FACE_COZMO_ACT_GLANCE:
        out->gaze_x_q8 = side * ((e * 150) >> 8);
        out->gaze_y_q8 = -((e * 24) >> 8);
        break;
    case FACE_COZMO_ACT_THINK_UP:
        out->gaze_x_q8 = side * ((e * 62) >> 8);
        out->gaze_y_q8 = -((e * 148) >> 8);
        out->roll_mdeg = side * ((e * 1400) >> 8);
        break;
    case FACE_COZMO_ACT_SLOW_BLINK:
        out->slow_blink_q8 = e;
        break;
    case FACE_COZMO_ACT_SQUINT_HOLD:
        out->squint_q8 = (e * 118) >> 8;
        out->gaze_y_q8 = (e * 16) >> 8;
        break;
    case FACE_COZMO_ACT_WIGGLE: {
        const int32_t wob = fca_sin_q14(
            fca_turn16(local - onset, fca_max((int32_t)dur / 2, 1)));
        out->roll_mdeg =
            side * (int32_t)fca_sar64((int64_t)wob * e * 2600, 14 + 8);
        break;
    }
    case FACE_COZMO_ACT_REFOCUS:
        out->gaze_x_q8 = side * ((e * 96) >> 8);
        out->gaze_y_q8 = (e * 58) >> 8;
        out->squint_q8 = (e * 46) >> 8;
        break;
    default:
        break;
    }
}

/* ---- top-level direction ---------------------------------------------- */

void fca_direct(
    const fca_profile_def_t *def,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fca_score_t *score)
{
    const fca_direction_t *dir = &def->direction;
    const uint32_t t_ms = fca_ms(sample_clock);
    const uint8_t activity = (uint8_t)(key->controls.expression & 3U);

    /* Direction-table selection: thinking looks up, conversation looks
     * sideways, embarrassment (folded in by the stager) looks down. */
    const uint8_t dir_table =
        activity == 2U ? 1U : (activity == 0U ? 0U : 2U);

    /* Attention narrows wandering; a rapt character holds the gaze. */
    const int32_t attention = key->attention;
    const int32_t wander_scale_q8 =
        fca_clamp(384 - attention, 102, 384);

    fca_avert_out_t avert;
    fca_avert_channel(
        dir, activity, dir_table, wander_scale_q8,
        t_ms, def->seed, &avert);

    /* Lids read the vertical gaze ahead of the eyes. */
    fca_avert_out_t avert_ahead;
    fca_avert_channel(
        dir, activity, dir_table, wander_scale_q8,
        t_ms + FCA_LID_LEAD_MS, def->seed, &avert_ahead);

    int32_t micro_x = 0;
    int32_t micro_y = 0;
    fca_micro_channel(dir, t_ms, def->seed, &micro_x, &micro_y);

    const int32_t arousal_q8 = key->affect_arousal;
    fca_act_out_t act;
    fca_act_channel(dir, activity, arousal_q8, t_ms, def->seed, &act);

    fca_blink_out_t blink;
    fca_blink_channel(
        dir, activity, t_ms, def->seed, act.slow_blink_q8, &blink);

    /* Gaze-evoked blink: large darts trigger a lid sweep at onset. */
    if (avert.has_aversion && avert.out_amp_q8 > 118) {
        const uint32_t hb = fca_hash(
            avert.out_onset_ms, FCA_TAG_BLINK_KIND, def->seed ^ 0xE7U);
        if ((hb % 100U) < 55U) {
            fca_blink_eval_event(
                t_ms,
                avert.out_onset_ms > 30U ? avert.out_onset_ms - 30U : 0U,
                256, dir->reopen_overshoot_q8 / 2,
                dir->blink_asym_ms, &blink);
        }
    }

    score->gaze_x_q8 = fca_clamp(
        avert.x_q8 + micro_x + act.gaze_x_q8, -256, 256);
    score->gaze_y_q8 = fca_clamp(
        avert.y_q8 + micro_y + act.gaze_y_q8, -256, 256);
    score->lid_gaze_y_q8 = fca_clamp(
        avert_ahead.y_q8 + micro_y + act.gaze_y_q8, -256, 256);
    score->blink_q8[0] = blink.aperture_q8[0];
    score->blink_q8[1] = blink.aperture_q8[1];
    score->blink_state = blink.state;
    score->act_id = act.id;
    score->act_gaze_x_q8 = act.gaze_x_q8;
    score->act_gaze_y_q8 = act.gaze_y_q8;
    score->act_squint_q8 = act.squint_q8;
    score->act_roll_mdeg = act.roll_mdeg;

    /* Breathing bob fades with arousal and while speech is active. */
    const bool speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        activity == 3U ||
        key->speech_phase == FACE_SPEECH_ACTIVE;
    int32_t breath_amp_q4 = dir->breath_amp_q4;
    breath_amp_q4 = (breath_amp_q4 * (352 - arousal_q8)) / 352;
    if (speaking) {
        breath_amp_q4 = breath_amp_q4 / 2;
    }
    score->breath_y_q4 = (int32_t)fca_sar64(
        (int64_t)fca_sin_q14(fca_turn16(t_ms, dir->breath_period_ms)) *
            breath_amp_q4,
        14);

    /* Saccade squash: stretch along the dart, ease out mid-flight. */
    score->saccade_active = avert.in_flight ? 1U : 0U;
    score->dart_stretch_q8 = 256;
    if (avert.in_flight) {
        const int32_t kick = fca_pulse_q8(avert.flight_e_q8);
        const int32_t axis = fca_abs(avert.flight_vx_q8) >
                                     fca_abs(avert.flight_vy_q8)
                                 ? 1
                                 : -1;
        const int32_t gain =
            (kick * dir->saccade_verve_q8) >> 8;
        /* Horizontal darts stretch x; vertical darts squash x. */
        score->dart_stretch_q8 = 256 + axis * (gain / 5);
    }

    /*
     * Speech emphasis: instantaneous energy of the articulation bytes.
     * The stager keeps its influence small (brightness, one or two
     * pixels of scale) so the 30 fps stream cannot strobe.
     */
    int32_t emphasis = 0;
    if (speaking || key->speech_phase == FACE_SPEECH_STARTING) {
        emphasis =
            ((int32_t)key->audio_level * 3 +
             (int32_t)key->controls.mouth_open * 2) / 5;
        emphasis = (emphasis * (192 + ((int32_t)key->viseme_weight >> 2))) /
                   256;
    }
    score->emphasis_q8 = fca_clamp(emphasis, 0, 256);
}
