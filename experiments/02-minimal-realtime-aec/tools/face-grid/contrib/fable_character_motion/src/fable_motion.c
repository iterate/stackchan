#include "fable_motion.h"

#include <stddef.h>

/*
 * Sub-seeds decorrelate the independent schedules of one persona. The
 * constants are arbitrary odd values; they only need to differ.
 */
enum {
    SEED_GAZE = 0x67a3f1d5,
    SEED_BLINK = 0x2f6e2b1d,
    SEED_MICRO_X = 0x1e4f8a63,
    SEED_MICRO_Y = 0x51b3c98f,
    SEED_BREATH = 0x0d5f2c37,
    SEED_MSACC = 0x24c9d2e5,
    SEED_ACT = 0x37e15163,
    SEED_ACCENT = 0x3c6ef372,
    SEED_NOD = 0x5aa66d2b,
};

/*
 * Q10 multiply. Arithmetic right shift of a negative operand is
 * implementation-defined by ISO C but is arithmetic on every target this
 * project supports (GCC/xtensa, Clang/host, Clang/wasm32); the test suite
 * asserts the behavior at startup so an odd platform fails loudly instead
 * of drifting from the golden frames.
 */
static int32_t q10(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 10);
}

static int32_t abs_i32(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t max_i32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static int32_t min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

/* ------------------------------------------------------------------ */
/* Personas                                                            */
/* ------------------------------------------------------------------ */

const fable_persona_t FABLE_PERSONA_CALM = {
    .slug = "calm",
    .name = "Calm Ember",
    .seed = 0x1c0ffee1,
    .blink_slot_ms = 4000, .blink_close_ms = 90, .blink_hold_ms = 40,
    .blink_open_ms = 190, .double_blink_pct = 12, .blink_asym_ms = 12,
    .gaze_slot_ms = 2800, .gaze_range_x_px = 10, .gaze_range_y_px = 6,
    .saccade_base_ms = 30, .saccade_per_px_ms = 2,
    .micro_amp_q2 = 5, .micro_period_ms = 900,
    .head_follow_pct = 60, .head_lag_ms = 100, .head_dur_pct = 260,
    .body_follow_pct = 30, .body_lag_ms = 240, .body_dur_pct = 340,
    .overshoot_q10 = 60, .arc_dip_q2 = 6,
    .anticipation_ms = 110, .anticipation_pct = 14,
    .anticipation_min_px = 5,
    .breath_period_ms = 4300, .breath_amp = 110, .stretch_max = 8,
    .act_slot_ms = 15000, .act_none_pct = 35,
    .act_mask = FABLE_ACT_BIT(FABLE_ACT_GLANCE) |
                FABLE_ACT_BIT(FABLE_ACT_TILT) |
                FABLE_ACT_BIT(FABLE_ACT_SIGH),
    .lid_rest_q10 = 1000, .brow_rest = 0, .lid_tilt_rest = 0,
    .energy = 96,
};

const fable_persona_t FABLE_PERSONA_PERKY = {
    .slug = "perky",
    .name = "Perky Pip",
    .seed = 0x2bad5eed,
    .blink_slot_ms = 3200, .blink_close_ms = 70, .blink_hold_ms = 30,
    .blink_open_ms = 130, .double_blink_pct = 22, .blink_asym_ms = 8,
    .gaze_slot_ms = 1600, .gaze_range_x_px = 13, .gaze_range_y_px = 8,
    .saccade_base_ms = 24, .saccade_per_px_ms = 2,
    .micro_amp_q2 = 7, .micro_period_ms = 700,
    .head_follow_pct = 65, .head_lag_ms = 70, .head_dur_pct = 200,
    .body_follow_pct = 35, .body_lag_ms = 170, .body_dur_pct = 300,
    .overshoot_q10 = 200, .arc_dip_q2 = 8,
    .anticipation_ms = 90, .anticipation_pct = 18,
    .anticipation_min_px = 4,
    .breath_period_ms = 3300, .breath_amp = 130, .stretch_max = 12,
    .act_slot_ms = 9000, .act_none_pct = 25,
    .act_mask = FABLE_ACT_BIT(FABLE_ACT_GLANCE) |
                FABLE_ACT_BIT(FABLE_ACT_WIGGLE) |
                FABLE_ACT_BIT(FABLE_ACT_TILT) |
                FABLE_ACT_BIT(FABLE_ACT_SQUINT),
    .lid_rest_q10 = 1024, .brow_rest = 6, .lid_tilt_rest = 4,
    .energy = 190,
};

const fable_persona_t FABLE_PERSONA_SLEEPY = {
    .slug = "sleepy",
    .name = "Sleepy Moss",
    .seed = 0x3d0af00d,
    .blink_slot_ms = 5200, .blink_close_ms = 160, .blink_hold_ms = 90,
    .blink_open_ms = 340, .double_blink_pct = 6, .blink_asym_ms = 20,
    .gaze_slot_ms = 4200, .gaze_range_x_px = 6, .gaze_range_y_px = 4,
    .saccade_base_ms = 46, .saccade_per_px_ms = 3,
    .micro_amp_q2 = 6, .micro_period_ms = 1000,
    .head_follow_pct = 55, .head_lag_ms = 150, .head_dur_pct = 320,
    .body_follow_pct = 25, .body_lag_ms = 380, .body_dur_pct = 420,
    .overshoot_q10 = 20, .arc_dip_q2 = 4,
    .anticipation_ms = 140, .anticipation_pct = 8,
    .anticipation_min_px = 6,
    .breath_period_ms = 5300, .breath_amp = 150, .stretch_max = 10,
    .act_slot_ms = 12000, .act_none_pct = 30,
    .act_mask = FABLE_ACT_BIT(FABLE_ACT_YAWN) |
                FABLE_ACT_BIT(FABLE_ACT_SIGH) |
                FABLE_ACT_BIT(FABLE_ACT_TILT),
    .lid_rest_q10 = 640, .brow_rest = -4, .lid_tilt_rest = -8,
    .energy = 48,
};

const fable_persona_t FABLE_PERSONA_CURIOUS = {
    .slug = "curious",
    .name = "Curious Scout",
    .seed = 0x4face0ff,
    .blink_slot_ms = 3800, .blink_close_ms = 80, .blink_hold_ms = 35,
    .blink_open_ms = 170, .double_blink_pct = 15, .blink_asym_ms = 10,
    .gaze_slot_ms = 1900, .gaze_range_x_px = 16, .gaze_range_y_px = 10,
    .saccade_base_ms = 28, .saccade_per_px_ms = 2,
    .micro_amp_q2 = 6, .micro_period_ms = 800,
    .head_follow_pct = 70, .head_lag_ms = 90, .head_dur_pct = 240,
    .body_follow_pct = 40, .body_lag_ms = 220, .body_dur_pct = 330,
    .overshoot_q10 = 140, .arc_dip_q2 = 10,
    .anticipation_ms = 120, .anticipation_pct = 20,
    .anticipation_min_px = 4,
    .breath_period_ms = 3900, .breath_amp = 100, .stretch_max = 8,
    .act_slot_ms = 8000, .act_none_pct = 20,
    .act_mask = FABLE_ACT_BIT(FABLE_ACT_GLANCE) |
                FABLE_ACT_BIT(FABLE_ACT_TILT) |
                FABLE_ACT_BIT(FABLE_ACT_SQUINT) |
                FABLE_ACT_BIT(FABLE_ACT_WIGGLE),
    .lid_rest_q10 = 1024, .brow_rest = 10, .lid_tilt_rest = 6,
    .energy = 150,
};

const fable_persona_t FABLE_PERSONA_SAGE = {
    .slug = "sage",
    .name = "Attentive Sage",
    .seed = 0x5eafab1e,
    .blink_slot_ms = 4400, .blink_close_ms = 100, .blink_hold_ms = 45,
    .blink_open_ms = 210, .double_blink_pct = 8, .blink_asym_ms = 14,
    .gaze_slot_ms = 3400, .gaze_range_x_px = 7, .gaze_range_y_px = 4,
    .saccade_base_ms = 34, .saccade_per_px_ms = 2,
    .micro_amp_q2 = 6, .micro_period_ms = 850,
    .head_follow_pct = 50, .head_lag_ms = 120, .head_dur_pct = 300,
    .body_follow_pct = 22, .body_lag_ms = 300, .body_dur_pct = 380,
    .overshoot_q10 = 40, .arc_dip_q2 = 5,
    .anticipation_ms = 130, .anticipation_pct = 10,
    .anticipation_min_px = 6,
    .breath_period_ms = 4700, .breath_amp = 95, .stretch_max = 6,
    .act_slot_ms = 16000, .act_none_pct = 45,
    .act_mask = FABLE_ACT_BIT(FABLE_ACT_TILT) |
                FABLE_ACT_BIT(FABLE_ACT_SIGH) |
                FABLE_ACT_BIT(FABLE_ACT_GLANCE),
    .lid_rest_q10 = 980, .brow_rest = 2, .lid_tilt_rest = 0,
    .energy = 110,
};

static const fable_persona_t *const PERSONAS[FABLE_PERSONA_COUNT] = {
    &FABLE_PERSONA_CALM,
    &FABLE_PERSONA_PERKY,
    &FABLE_PERSONA_SLEEPY,
    &FABLE_PERSONA_CURIOUS,
    &FABLE_PERSONA_SAGE,
};

const fable_persona_t *fable_persona_at(uint32_t index)
{
    if (index >= (uint32_t)FABLE_PERSONA_COUNT) {
        index = 0U;
    }
    return PERSONAS[index];
}

/* ------------------------------------------------------------------ */
/* Resolved inputs                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t activity;
    bool speaking;
    int32_t look_x_q2; /* host attention point */
    int32_t look_y_q2;
    int32_t kf_lid_left_q10;
    int32_t kf_lid_right_q10;
    int32_t kf_brow;
    const fable_keyframe_t *kf;
} inputs_t;

static void resolve_inputs(const fable_keyframe_t *kf, inputs_t *in)
{
    in->kf = kf;
    if (kf == NULL) {
        in->activity = FABLE_ACTIVITY_IDLE;
        in->speaking = false;
        in->look_x_q2 = 0;
        in->look_y_q2 = 0;
        in->kf_lid_left_q10 = FABLE_ONE;
        in->kf_lid_right_q10 = FABLE_ONE;
        in->kf_brow = 0;
        return;
    }
    in->speaking = (kf->flags & FABLE_KEYFRAME_FLAG_SPEAKING) != 0U;
    if (kf->expression <= FABLE_ACTIVITY_SPEAKING) {
        in->activity = kf->expression;
    } else {
        in->activity =
            in->speaking ? FABLE_ACTIVITY_SPEAKING : FABLE_ACTIVITY_IDLE;
    }
    if (in->speaking && in->activity == FABLE_ACTIVITY_IDLE) {
        in->activity = FABLE_ACTIVITY_SPEAKING;
    }
    /* Host look full scale maps to +/-20 px => +/-80 q2. */
    in->look_x_q2 = ((int32_t)kf->look_x * 5) / 8;
    in->look_y_q2 = ((int32_t)kf->look_y * 5) / 8;
    in->kf_lid_left_q10 = ((int32_t)kf->eye_left_open * FABLE_ONE) / 255;
    in->kf_lid_right_q10 = ((int32_t)kf->eye_right_open * FABLE_ONE) / 255;
    in->kf_brow = ((int32_t)kf->brow) / 2;
}

/* ------------------------------------------------------------------ */
/* Gaze: fixation targets, saccades, follow-through chain              */
/* ------------------------------------------------------------------ */

/*
 * The plan covers fixation targets of slots k-2..k+1 and the three
 * transitions between them. Layer positions are evaluated as a
 * superposition: pos = t[0] + sum over transitions of delta * ease(phase),
 * which stays continuous even when a slow, lagged layer is still settling
 * out of one saccade as the next begins.
 *
 * Persona tables must keep lag + longest transition shorter than one gaze
 * slot so a transition that began in slot k-2 is finished by slot k;
 * every built-in persona satisfies that with a wide margin.
 */
enum { GAZE_TARGETS = 4, GAZE_TRANSITIONS = 3 };

typedef struct {
    uint32_t slot_len;
    uint32_t nominal_dur;   /* scheduling envelope for one saccade */
    uint32_t jitter_room;
    int32_t tx[GAZE_TARGETS];  /* q2 targets, slots k-2 .. k+1 */
    int32_t ty[GAZE_TARGETS];
    uint32_t start[GAZE_TARGETS]; /* saccade start clock per slot */
    uint32_t dur[GAZE_TARGETS];   /* actual saccade duration per slot */
} gaze_plan_t;

/*
 * Exploration gain per activity: while listening the face keeps near
 * mutual gaze (Argyle & Cook report roughly 75% eye contact while
 * listening vs 40% while speaking; Eyes Alive models listeners holding
 * gaze for seconds and glancing away only briefly), while thinking the
 * gaze drifts up and to a hash-chosen side - the folk "look up to think"
 * pose, staged deliberately.
 */
static int32_t activity_gain_q10(uint8_t activity)
{
    switch (activity) {
    case FABLE_ACTIVITY_LISTENING: return 460;
    case FABLE_ACTIVITY_THINKING: return 920;
    case FABLE_ACTIVITY_SPEAKING: return 768;
    default: return FABLE_ONE;
    }
}

static void gaze_target_for_slot(const fable_persona_t *p,
                                 const inputs_t *in,
                                 uint32_t slot,
                                 int32_t *tx_q2, int32_t *ty_q2)
{
    const uint32_t r = fable_hash2(p->seed ^ (uint32_t)SEED_GAZE, slot);
    const int32_t range_x = (int32_t)p->gaze_range_x_px * 4;
    const int32_t range_y = (int32_t)p->gaze_range_y_px * 4;
    /* Triangular distribution biases fixations toward the centre. */
    const int32_t ex =
        ((int32_t)(r % (uint32_t)(2 * range_x + 1)) +
         (int32_t)((r >> 10) % (uint32_t)(2 * range_x + 1))) / 2 -
        range_x;
    const int32_t ey =
        ((int32_t)((r >> 4) % (uint32_t)(2 * range_y + 1)) +
         (int32_t)((r >> 14) % (uint32_t)(2 * range_y + 1))) / 2 -
        range_y;
    const int32_t gain = activity_gain_q10(in->activity);
    int32_t x = in->look_x_q2 + q10(ex, gain);
    int32_t y = in->look_y_q2 + q10(ey, gain);

    if (in->activity == FABLE_ACTIVITY_THINKING) {
        /* Up and to one side; the side flips slowly with the slot hash. */
        const int32_t side = ((r >> 20) & 1U) != 0U ? 1 : -1;
        x += side * 20;
        y -= 24;
    } else if (in->activity == FABLE_ACTIVITY_SPEAKING &&
               (r >> 24) % 100U < 18U) {
        /* Occasional larger aversion mid-speech. */
        x = x * 2;
        y = y * 2;
    }
    *tx_q2 = fable_clamp_i32(x, -127, 127);
    *ty_q2 = fable_clamp_i32(y, -127, 127);
}

static uint32_t saccade_duration(const fable_persona_t *p,
                                 int32_t dx_q2, int32_t dy_q2)
{
    const int32_t amp_px = max_i32(abs_i32(dx_q2), abs_i32(dy_q2)) / 4;
    const uint32_t ms = (uint32_t)p->saccade_base_ms +
                        (uint32_t)p->saccade_per_px_ms * (uint32_t)amp_px;
    return fable_ms_to_samples(ms);
}

static void plan_gaze(const fable_persona_t *p, const inputs_t *in,
                      uint32_t clock, gaze_plan_t *g)
{
    g->slot_len = fable_ms_to_samples(p->gaze_slot_ms);
    const uint32_t max_amp_px = (uint32_t)max_i32(
        p->gaze_range_x_px * 2, p->gaze_range_y_px * 2) + 32U;
    g->nominal_dur = fable_ms_to_samples(
        (uint32_t)p->saccade_base_ms +
        (uint32_t)p->saccade_per_px_ms * max_amp_px);
    if (g->nominal_dur >= g->slot_len) {
        g->nominal_dur = g->slot_len - 1U;
    }
    g->jitter_room = g->slot_len - g->nominal_dur;

    const uint32_t base_slot = clock / g->slot_len; /* slot k */
    int32_t prev_tx = 0;
    int32_t prev_ty = 0;
    for (uint32_t i = 0; i < (uint32_t)GAZE_TARGETS; i++) {
        const uint32_t slot = base_slot - 2U + i;
        gaze_target_for_slot(p, in, slot, &g->tx[i], &g->ty[i]);
        const uint32_t r = fable_hash2(p->seed ^ (uint32_t)SEED_GAZE, slot);
        g->start[i] = slot * g->slot_len +
                      (g->jitter_room != 0U ? r % g->jitter_room : 0U);
        if (i == 0U) {
            g->dur[0] = 1U;
        } else {
            g->dur[i] =
                saccade_duration(p, g->tx[i] - prev_tx, g->ty[i] - prev_ty);
        }
        prev_tx = g->tx[i];
        prev_ty = g->ty[i];
    }
}

typedef struct {
    int32_t x_q2;
    int32_t y_q2;
    int32_t transit_q10; /* phase of the most recent active transition */
    int32_t transit_dx;  /* its scaled horizontal travel, q2 */
    int32_t arc_dip_q2;  /* accumulated arc dip for this layer */
} chain_pos_t;

/*
 * Evaluate one delayed, re-timed copy of the saccade chain. The eye layer
 * uses lag 0 / 100% duration / no overshoot; head and body layers lag
 * behind with longer, overshooting transitions - eyes lead, head follows,
 * body drags: follow-through by construction, arcs by the dip term.
 */
static void chain_position(const fable_persona_t *p, const gaze_plan_t *g,
                           uint32_t clock, uint32_t lag, uint32_t dur_pct,
                           int32_t overshoot_q10, int32_t follow_pct,
                           bool with_arc, chain_pos_t *out)
{
    int32_t x = g->tx[0];
    int32_t y = g->ty[0];
    int32_t transit = FABLE_ONE;
    int32_t transit_dx = 0;
    int32_t dip = 0;

    for (int i = 1; i < GAZE_TRANSITIONS + 1; i++) {
        const uint32_t s = g->start[i] + lag;
        if (clock < s) {
            break;
        }
        const int32_t dx = g->tx[i] - g->tx[i - 1];
        const int32_t dy = g->ty[i] - g->ty[i - 1];
        uint32_t dur = (g->dur[i] * dur_pct) / 100U;
        if (dur == 0U) {
            dur = 1U;
        }
        const uint32_t elapsed = clock - s;
        int32_t phase = FABLE_ONE;
        if (elapsed < dur) {
            phase = (int32_t)(((uint64_t)elapsed << 10) / dur);
        }
        const int32_t eased = fable_mix(fable_ease_smooth(phase),
                                        fable_ease_out_back(phase),
                                        overshoot_q10);
        x += q10(dx, eased);
        y += q10(dy, eased);
        if (phase < FABLE_ONE) {
            transit = phase;
            transit_dx = (dx * follow_pct) / 100;
            if (with_arc) {
                const int32_t travel_px =
                    min_i32(abs_i32(transit_dx) / 4, 16);
                dip += q10(((int32_t)p->arc_dip_q2 * travel_px) / 16,
                           fable_arc(phase));
            }
        }
    }
    out->x_q2 = (x * follow_pct) / 100;
    out->y_q2 = (y * follow_pct) / 100 + dip;
    out->transit_q10 = transit;
    out->transit_dx = transit_dx;
    out->arc_dip_q2 = dip;
}

/*
 * Anticipation: a counter-move ramping up over the window before each
 * saccade start and releasing over the head lag after it. Ramp and
 * release are continuous, so contributions from the transitions near
 * `clock` simply sum.
 */
static void anticipation_offset(const fable_persona_t *p,
                                const gaze_plan_t *g, uint32_t clock,
                                int32_t *out_x_q2, int32_t *out_y_q2)
{
    const uint32_t window = fable_ms_to_samples(p->anticipation_ms);
    const uint32_t release = fable_ms_to_samples(
        (uint32_t)max_i32(p->head_lag_ms, 40));
    const int32_t min_q2 = (int32_t)p->anticipation_min_px * 4;
    const int32_t share = ((int32_t)p->anticipation_pct << 10) / 100;
    int32_t ax = 0;
    int32_t ay = 0;

    for (int i = 1; i < GAZE_TRANSITIONS + 1; i++) {
        const int32_t dx = g->tx[i] - g->tx[i - 1];
        const int32_t dy = g->ty[i] - g->ty[i - 1];
        if (max_i32(abs_i32(dx), abs_i32(dy)) < min_q2) {
            continue;
        }
        const uint32_t s = g->start[i];
        int32_t shape = 0;
        if (clock < s && s - clock <= window) {
            const uint32_t into = window - (s - clock); /* 0..window */
            shape = fable_ease_smooth(
                (int32_t)(((uint64_t)into << 10) / window));
        } else if (clock >= s && clock - s < release) {
            shape = FABLE_ONE - fable_ease_smooth(
                (int32_t)(((uint64_t)(clock - s) << 10) / release));
        }
        if (shape != 0) {
            ax -= q10(q10(dx, share), shape);
            ay -= q10(q10(dy, share), shape);
        }
    }
    *out_x_q2 = ax;
    *out_y_q2 = ay;
}

/* ------------------------------------------------------------------ */
/* Blink phrasing                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t slot_len;
    uint32_t close;
    uint32_t hold;
    uint32_t open;
    uint32_t gap;      /* pause before a double-blink second beat */
    uint32_t nominal;  /* full envelope incl. possible second beat */
} blink_times_t;

static void blink_times(const fable_persona_t *p, blink_times_t *t)
{
    t->slot_len = fable_ms_to_samples(p->blink_slot_ms);
    t->close = fable_ms_to_samples(p->blink_close_ms);
    t->hold = fable_ms_to_samples(p->blink_hold_ms);
    t->open = fable_ms_to_samples(p->blink_open_ms);
    t->gap = fable_ms_to_samples(120);
    t->nominal = 2U * (t->close + t->hold + t->open) + t->gap;
    if (t->nominal >= t->slot_len) {
        t->nominal = t->slot_len - 1U;
    }
}

/*
 * Openness contribution of one blink beat that started `u` samples ago.
 * depth_q10 scales how far the lid drops (a double blink's second beat
 * is shallower). Returns FABLE_ONE once the beat has finished.
 */
static int32_t blink_beat(const blink_times_t *t, uint32_t u,
                          int32_t depth_q10)
{
    if (u < t->close) {
        const int32_t ph = (int32_t)(((uint64_t)u << 10) / t->close);
        return FABLE_ONE - q10(depth_q10, fable_ease_in_quad(ph));
    }
    u -= t->close;
    if (u < t->hold) {
        return FABLE_ONE - depth_q10;
    }
    u -= t->hold;
    if (u < t->open) {
        const int32_t ph = (int32_t)(((uint64_t)u << 10) / t->open);
        return FABLE_ONE - q10(depth_q10,
                               FABLE_ONE - fable_ease_out_cubic(ph));
    }
    return FABLE_ONE;
}

/*
 * Procedural lid openness at `clock` (Q10). Blinks close fast and reopen
 * slowly - the asymmetry every animation reference agrees on - and a slot
 * hash occasionally phrases a lighter second beat right after the first.
 */
static int32_t blink_openness(const fable_persona_t *p,
                              const inputs_t *in, uint32_t clock)
{
    blink_times_t t;
    blink_times(p, &t);

    fable_event_t ev;
    fable_schedule(clock, t.slot_len, t.nominal,
                   p->seed ^ (uint32_t)SEED_BLINK, &ev);

    /* Speakers hold blinks through phrases; skip some slots entirely. */
    if (in->activity == FABLE_ACTIVITY_SPEAKING &&
        (ev.rand >> 16) % 100U < 22U) {
        return FABLE_ONE;
    }

    if (clock < ev.start) {
        return FABLE_ONE;
    }
    const uint32_t u = clock - ev.start;
    int32_t open = blink_beat(&t, u, FABLE_ONE);

    if ((ev.rand % 100U) < p->double_blink_pct) {
        const uint32_t second = t.close + t.hold + t.open + t.gap;
        if (u >= second) {
            const int32_t beat2 = blink_beat(&t, u - second, 640);
            open = min_i32(open, beat2);
        }
    }
    return open;
}

/* ------------------------------------------------------------------ */
/* Breathing                                                           */
/* ------------------------------------------------------------------ */

/*
 * Breath cycle: inhale 35%, exhale 45%, rest 20%. The exhale runs longer
 * than the inhale and the cycle idles at the bottom, matching resting
 * respiration. Amplitude wanders +/-25% on a slow noise lattice so no two
 * minutes look alike, and speech suppresses the visible excursion.
 */
static int32_t breath_level_q10(const fable_persona_t *p,
                                const inputs_t *in, uint32_t clock,
                                int32_t boost_q10)
{
    const uint32_t period = fable_ms_to_samples(p->breath_period_ms);
    const uint32_t ph = clock % period;
    const uint32_t inhale = (period * 35U) / 100U;
    const uint32_t exhale = (period * 45U) / 100U;
    int32_t level;

    if (ph < inhale) {
        level = fable_ease_smooth(
            (int32_t)(((uint64_t)ph << 10) / inhale));
    } else if (ph < inhale + exhale) {
        level = FABLE_ONE - fable_ease_smooth(
            (int32_t)(((uint64_t)(ph - inhale) << 10) / exhale));
    } else {
        level = 0;
    }
    /* A living chest never goes perfectly still: a tiny always-on
       ripple keeps the rest segment breathing at sub-pixel scale. */
    const int32_t ripple =
        (fable_vnoise(clock, fable_ms_to_samples(700),
                      p->seed ^ (uint32_t)(SEED_BREATH + 1)) +
         FABLE_ONE) / 64;
    level = min_i32(level + ripple, FABLE_ONE);

    int32_t amp = ((int32_t)p->breath_amp << 10) / 255;
    const int32_t wander =
        fable_vnoise(clock, fable_ms_to_samples(11000),
                     p->seed ^ (uint32_t)SEED_BREATH);
    amp = amp + q10(amp, wander / 4);
    if (in->speaking) {
        amp /= 2;
    }
    amp = fable_clamp_i32(amp + boost_q10, 0, FABLE_ONE);
    return q10(level, amp);
}

/* ------------------------------------------------------------------ */
/* Idle acts                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t act;
    int32_t env;    /* trapezoid envelope, Q10 */
    int32_t phase;  /* 0..FABLE_ONE across the act */
} act_state_t;

static uint32_t act_duration_ms(uint8_t act)
{
    switch (act) {
    case FABLE_ACT_GLANCE: return 2600;
    case FABLE_ACT_TILT: return 2200;
    case FABLE_ACT_YAWN: return 3200;
    case FABLE_ACT_SIGH: return 3000;
    case FABLE_ACT_SQUINT: return 1600;
    case FABLE_ACT_WIGGLE: return 1400;
    default: return 0;
    }
}

static uint8_t pick_act(const fable_persona_t *p, uint32_t rand)
{
    if (p->act_mask == 0U || rand % 100U < p->act_none_pct) {
        return FABLE_ACT_NONE;
    }
    uint8_t allowed[FABLE_ACT_COUNT];
    uint8_t count = 0;
    for (uint8_t a = 1; a < (uint8_t)FABLE_ACT_COUNT; a++) {
        if ((p->act_mask & FABLE_ACT_BIT(a)) != 0U) {
            allowed[count] = a;
            count++;
        }
    }
    if (count == 0U) {
        return FABLE_ACT_NONE;
    }
    return allowed[(rand >> 8) % count];
}

/*
 * Staging: acts run only while the face is idle. Dialogue states keep the
 * stage clear for gaze, brows, and mouth so one idea reads at a time.
 */
static void plan_act(const fable_persona_t *p, const inputs_t *in,
                     uint32_t clock, act_state_t *a)
{
    a->act = FABLE_ACT_NONE;
    a->env = 0;
    a->phase = 0;
    if (in->activity != FABLE_ACTIVITY_IDLE) {
        return;
    }
    const uint32_t slot_len = fable_ms_to_samples(p->act_slot_ms);
    fable_event_t ev;
    /* Schedule with the longest act so every act fits the jitter room. */
    fable_schedule(clock, slot_len, fable_ms_to_samples(3200),
                   p->seed ^ (uint32_t)SEED_ACT, &ev);
    const uint8_t act = pick_act(p, ev.rand);
    if (act == FABLE_ACT_NONE || clock < ev.start) {
        return;
    }
    const uint32_t dur = fable_ms_to_samples(act_duration_ms(act));
    const uint32_t u = clock - ev.start;
    if (u >= dur) {
        return;
    }
    const int32_t phase = (int32_t)(((uint64_t)u << 10) / dur);
    /* Trapezoid: ease in over 20%, hold, ease out over the last 30%. */
    int32_t env;
    if (phase < 205) {
        env = fable_ease_smooth((phase << 10) / 205);
    } else if (phase < 717) {
        env = FABLE_ONE;
    } else {
        env = FABLE_ONE -
              fable_ease_smooth(((phase - 717) << 10) / 307);
    }
    a->act = act;
    a->env = env;
    a->phase = phase;
}

/* ------------------------------------------------------------------ */
/* Engine                                                              */
/* ------------------------------------------------------------------ */

void fable_motion_eval(const fable_persona_t *persona,
                       const fable_keyframe_t *keyframe,
                       uint32_t clock,
                       fable_motion_pose_t *out)
{
    const fable_persona_t *p =
        persona != NULL ? persona : &FABLE_PERSONA_CALM;
    inputs_t in;
    resolve_inputs(keyframe, &in);

    /* --- gaze chain: eyes lead, head follows, body drags ---------- */
    gaze_plan_t g;
    plan_gaze(p, &in, clock, &g);

    chain_pos_t eye;
    chain_position(p, &g, clock, 0U, 100U, 0, 100, false, &eye);

    chain_pos_t head;
    chain_position(p, &g, clock, fable_ms_to_samples(p->head_lag_ms),
                   p->head_dur_pct, (int32_t)p->overshoot_q10 * 4,
                   p->head_follow_pct, true, &head);

    chain_pos_t body;
    chain_position(p, &g, clock, fable_ms_to_samples(p->body_lag_ms),
                   p->body_dur_pct, 0, p->body_follow_pct, false, &body);

    int32_t eye_x = eye.x_q2;
    int32_t eye_y = eye.y_q2;
    int32_t head_x = head.x_q2;
    int32_t head_y = head.y_q2;

    /* Anticipation counter-move on the head layer. */
    int32_t antic_x;
    int32_t antic_y;
    anticipation_offset(p, &g, clock, &antic_x, &antic_y);
    head_x += antic_x;
    head_y += antic_y;

    /* Micro-drift keeps fixations alive (secondary action). */
    eye_x += q10((int32_t)p->micro_amp_q2,
                 fable_vnoise(clock,
                              fable_ms_to_samples(p->micro_period_ms),
                              p->seed ^ (uint32_t)SEED_MICRO_X));
    eye_y += q10((int32_t)p->micro_amp_q2,
                 fable_vnoise(clock,
                              fable_ms_to_samples(p->micro_period_ms),
                              p->seed ^ (uint32_t)SEED_MICRO_Y));

    /* Discrete micro-saccades: one ~1 px corrective flick per ~0.75 s
       (microsaccade rate is 1-2/s). Alternating sign guarantees the
       eye never sits bit-identical for long - a frozen face reads as
       dead long before it reads as calm. Eyes only; the head must not
       chase micro-motion. */
    {
        fable_event_t ms;
        fable_schedule(clock, fable_ms_to_samples(750),
                       fable_ms_to_samples(40),
                       p->seed ^ (uint32_t)SEED_MSACC, &ms);
        const int32_t sign_cur = (ms.slot & 1U) != 0U ? 1 : -1;
        const int32_t mag_cur = 2 + (int32_t)(ms.rand % 5U);
        const int32_t mag_prev = 2 + (int32_t)(ms.prev_rand % 5U);
        const int32_t off_cur = sign_cur * mag_cur;
        const int32_t off_prev = -sign_cur * mag_prev;
        if (ms.phase < 0) {
            eye_x += off_prev;
        } else {
            eye_x += off_prev +
                     q10(off_cur - off_prev,
                         fable_ease_out_quad(ms.phase));
        }
    }

    /* --- lids ------------------------------------------------------ */
    const uint32_t asym = fable_ms_to_samples(p->blink_asym_ms);
    int32_t lid_l = blink_openness(p, &in, clock);
    int32_t lid_r =
        blink_openness(p, &in, clock >= asym ? clock - asym : 0U);

    /* Saccade-coupled lid dip on large gaze shifts (gaze-evoked blink). */
    if (eye.transit_q10 < FABLE_ONE && abs_i32(eye.transit_dx) >= 24) {
        const int32_t dip =
            FABLE_ONE - q10(410, fable_arc(eye.transit_q10));
        lid_l = min_i32(lid_l, dip);
        lid_r = min_i32(lid_r, dip);
    }

    /* Lids track vertical gaze: down-gaze lowers, up-gaze widens. */
    const int32_t lid_gaze = -(eye_y * 16) / 4;
    const int32_t lid_cap = min_i32(
        (int32_t)p->lid_rest_q10 + max_i32(lid_gaze, 0), FABLE_ONE + 64);
    lid_l = min_i32(q10(lid_l, lid_cap + min_i32(lid_gaze, 0)),
                    in.kf_lid_left_q10);
    lid_r = min_i32(q10(lid_r, lid_cap + min_i32(lid_gaze, 0)),
                    in.kf_lid_right_q10);

    /* --- brows ----------------------------------------------------- */
    int32_t brow_l = p->brow_rest + in.kf_brow;
    int32_t brow_r = p->brow_rest + in.kf_brow;
    int32_t lid_tilt = p->lid_tilt_rest;

    if (in.activity == FABLE_ACTIVITY_LISTENING) {
        brow_l += 8;
        brow_r += 8;
    } else if (in.activity == FABLE_ACTIVITY_THINKING) {
        brow_l += 14;
        brow_r -= 6;
    }

    /* --- dialogue beats -------------------------------------------- */
    int32_t mouth_open =
        in.kf != NULL ? (int32_t)in.kf->mouth_open : 0;

    if (in.activity == FABLE_ACTIVITY_SPEAKING) {
        /* Head accents land on hash-scheduled beats, scaled by how loud
           the mouth is right now - loud beats nod, quiet beats pass. */
        fable_event_t acc;
        fable_schedule(clock, fable_ms_to_samples(1000),
                       fable_ms_to_samples(280),
                       p->seed ^ (uint32_t)SEED_ACCENT, &acc);
        if (acc.phase >= 0 && acc.phase < FABLE_ONE &&
            acc.rand % 100U < 65U) {
            const int32_t env = fable_arc(acc.phase);
            const int32_t amp = 6 + (mouth_open * 18) / 255;
            head_y += q10(amp, env);
            brow_l += q10(10, env);
            brow_r += q10(10, env);
        }
    } else if (in.activity == FABLE_ACTIVITY_LISTENING) {
        /* Soft affirmative nods while listening. */
        fable_event_t nod;
        fable_schedule(clock, fable_ms_to_samples(5200),
                       fable_ms_to_samples(900),
                       p->seed ^ (uint32_t)SEED_NOD, &nod);
        if (nod.phase >= 0 && nod.phase < FABLE_ONE &&
            nod.rand % 100U < 55U) {
            const int32_t env = fable_arc(nod.phase);
            head_y += q10(10, env);
            brow_l += q10(4, env);
            brow_r += q10(4, env);
        }
    }

    /* --- idle acts ------------------------------------------------- */
    act_state_t act;
    plan_act(p, &in, clock, &act);
    int32_t mouth_round =
        in.kf != NULL ? (int32_t)in.kf->mouth_round : 0;
    int32_t stretch_extra = 0;
    int32_t breath_boost = 0;

    switch (act.act) {
    case FABLE_ACT_GLANCE: {
        /* 1.5 sweeps: look one way, the other, and settle back. */
        const int32_t sweep =
            fable_sin_turn((uint32_t)((act.phase * 6144) >> 10));
        eye_x += q10(q10((int32_t)p->gaze_range_x_px * 4, sweep),
                     act.env);
        break;
    }
    case FABLE_ACT_TILT:
        lid_tilt += q10(20, act.env);
        brow_l += q10(12, act.env);
        brow_r -= q10(8, act.env);
        head_x += q10(8, act.env);
        break;
    case FABLE_ACT_YAWN:
        lid_l = min_i32(lid_l, FABLE_ONE - q10(720, act.env));
        lid_r = min_i32(lid_r, FABLE_ONE - q10(720, act.env));
        if (!in.speaking) {
            mouth_open = max_i32(mouth_open, q10(200, act.env));
            mouth_round = max_i32(mouth_round, q10(220, act.env));
        }
        stretch_extra += q10(20, act.env);
        head_y -= q10(6, act.env);
        break;
    case FABLE_ACT_SIGH:
        breath_boost = q10(560, act.env);
        if (act.phase > 512) {
            const int32_t droop = q10(300, act.env);
            lid_l = min_i32(lid_l, FABLE_ONE - droop);
            lid_r = min_i32(lid_r, FABLE_ONE - droop);
            head_y += q10(8, act.env);
        }
        break;
    case FABLE_ACT_SQUINT:
        lid_l = min_i32(lid_l, FABLE_ONE - q10(480, act.env));
        lid_r = min_i32(lid_r, FABLE_ONE - q10(480, act.env));
        brow_l -= q10(10, act.env);
        brow_r -= q10(10, act.env);
        break;
    case FABLE_ACT_WIGGLE: {
        const int32_t wob =
            fable_sin_turn((uint32_t)((act.phase * 12288) >> 10));
        head_x += q10(q10(10, wob), act.env);
        stretch_extra += q10(q10(8, wob), act.env);
        break;
    }
    default:
        break;
    }

    /* --- breathing (squash and stretch rides the breath) ----------- */
    const int32_t breath = breath_level_q10(p, &in, clock, breath_boost);
    /* Inhale stretches tall; the rest pose sits in a slight squash.
       Renderers compensate width to conserve area. */
    int32_t stretch =
        q10(breath - 320, (int32_t)p->stretch_max * 2) + stretch_extra;
    stretch = fable_clamp_i32(stretch, -32, 32);

    /* --- energy ---------------------------------------------------- */
    int32_t energy = p->energy;
    if (in.activity == FABLE_ACTIVITY_SPEAKING) {
        energy += 32;
    } else if (in.activity == FABLE_ACTIVITY_LISTENING) {
        energy += 16;
    } else if (in.activity == FABLE_ACTIVITY_THINKING) {
        energy -= 8;
    }

    /* --- publish --------------------------------------------------- */
    out->eye_x_q2 = (int16_t)fable_clamp_i32(eye_x, -160, 160);
    out->eye_y_q2 = (int16_t)fable_clamp_i32(eye_y, -120, 120);
    out->head_x_q2 = (int16_t)fable_clamp_i32(head_x, -160, 160);
    out->head_y_q2 = (int16_t)fable_clamp_i32(head_y, -120, 120);
    out->body_x_q2 = (int16_t)fable_clamp_i32(body.x_q2, -160, 160);
    out->body_y_q2 = (int16_t)fable_clamp_i32(body.y_q2, -120, 120);
    out->lid_left_q10 =
        (uint16_t)fable_clamp_i32(lid_l, 0, FABLE_ONE);
    out->lid_right_q10 =
        (uint16_t)fable_clamp_i32(lid_r, 0, FABLE_ONE);
    out->lid_tilt = (int8_t)fable_clamp_i32(lid_tilt, -32, 32);
    out->brow_left = (int8_t)fable_clamp_i32(brow_l, -64, 64);
    out->brow_right = (int8_t)fable_clamp_i32(brow_r, -64, 64);
    out->breath = (uint8_t)fable_clamp_i32((breath * 255) >> 10, 0, 255);
    out->stretch = (int8_t)stretch;
    out->mouth_open = (uint8_t)fable_clamp_i32(mouth_open, 0, 255);
    out->mouth_width =
        (uint8_t)(in.kf != NULL ? in.kf->mouth_width : 0U);
    out->mouth_round = (uint8_t)fable_clamp_i32(mouth_round, 0, 255);
    out->mouth_press =
        (uint8_t)(in.kf != NULL ? in.kf->mouth_press : 0U);
    out->mouth_teeth =
        (uint8_t)(in.kf != NULL ? in.kf->mouth_teeth : 0U);
    out->act = act.act;
    out->act_phase = (uint8_t)fable_clamp_i32(act.phase / 4, 0, 255);
    out->energy = (uint8_t)fable_clamp_i32(energy, 0, 255);
    out->reserved = 0;
}
