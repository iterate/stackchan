#include "fmg_internal.h"

/*
 * Coarticulation stage: articulator-weighted smoothing in the spirit of
 * Cohen–Massaro dominance blending, reduced to integer one-pole filters.
 * Each channel models a different articulator mass: the jaw is heavy
 * (slow attack, slower release), lip corners travel at medium speed, and
 * lip compression is fast so plosives still snap shut. A bilabial closure
 * dominates jaw opening — while press is high the smoothed jaw target
 * collapses, which is what makes "m/b/p" read as consonants instead of a
 * dip in a vowel. Rounding dominates width by pulling the corners toward
 * a pucker.
 */

typedef struct {
    int32_t attack_ms;
    int32_t release_ms;
} fmg_coart_tau_t;

enum { FMG_COART_CH_COUNT = 5 };

static const fmg_coart_tau_t s_taus[FMG_COART_CH_COUNT] = {
    {45, 95},  /* open: jaw inertia */
    {70, 70},  /* width: corner travel */
    {55, 65},  /* round: orbicularis rounding */
    {22, 55},  /* press: plosive snap, softer release */
    {60, 60},  /* teeth: lip retraction */
};

void fmg_coart_reset(fmg_coart_t *state)
{
    state->open_q8 = 0;
    state->width_q8 = fmg_u8_q8(110);
    state->round_q8 = fmg_u8_q8(30);
    state->press_q8 = 0;
    state->teeth_q8 = 0;
    state->last_clock = 0;
    state->primed = false;
}

static int32_t fmg_coart_step_ch(
    int32_t value, int32_t target, int32_t dt_ms, const fmg_coart_tau_t *tau)
{
    int32_t t = target > value ? tau->attack_ms : tau->release_ms;
    /* one-pole: alpha = dt / (tau + dt), exact at dt=0 and dt>>tau */
    int32_t alpha = dt_ms * 256 / (t + dt_ms);
    return value + (((target - value) * alpha) >> 8);
}

void fmg_coart_apply(
    fmg_coart_t *state,
    const fmg_keyframe_t *in,
    uint32_t sample_clock,
    fmg_keyframe_t *out)
{
    int32_t targets[FMG_COART_CH_COUNT] = {
        fmg_u8_q8(in->mouth_open),
        fmg_u8_q8(in->mouth_width),
        fmg_u8_q8(in->mouth_round),
        fmg_u8_q8(in->mouth_press),
        fmg_u8_q8(in->mouth_teeth),
    };

    /* dominance: closure beats jaw, rounding pulls corners inward */
    targets[0] = (targets[0] * (256 - (targets[3] * 7 >> 3))) >> 8;
    targets[1] += ((fmg_u8_q8(96) - targets[1]) * (targets[2] * 3 / 4)) >> 8;

    int32_t dt_ms;
    if (!state->primed || sample_clock <= state->last_clock) {
        dt_ms = 33;
        state->primed = true;
    } else {
        uint32_t dt = sample_clock - state->last_clock;
        if (dt > 16000U) {
            dt = 16000U; /* >1 s gap: treat as a scene cut, converge fast */
        }
        dt_ms = (int32_t)(dt / 16U);
        if (dt_ms < 1) {
            dt_ms = 1;
        }
    }
    state->last_clock = sample_clock;

    int32_t *ch[FMG_COART_CH_COUNT] = {
        &state->open_q8, &state->width_q8, &state->round_q8,
        &state->press_q8, &state->teeth_q8,
    };
    for (int i = 0; i < FMG_COART_CH_COUNT; i++) {
        *ch[i] = fmg_coart_step_ch(*ch[i], targets[i], dt_ms, &s_taus[i]);
    }

    *out = *in;
    out->mouth_open = (uint8_t)fmg_clampi(state->open_q8 * 255 / 256, 0, 255);
    out->mouth_width =
        (uint8_t)fmg_clampi(state->width_q8 * 255 / 256, 0, 255);
    out->mouth_round =
        (uint8_t)fmg_clampi(state->round_q8 * 255 / 256, 0, 255);
    out->mouth_press =
        (uint8_t)fmg_clampi(state->press_q8 * 255 / 256, 0, 255);
    out->mouth_teeth =
        (uint8_t)fmg_clampi(state->teeth_q8 * 255 / 256, 0, 255);
}
