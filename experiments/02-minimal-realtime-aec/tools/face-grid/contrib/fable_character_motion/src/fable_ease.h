#pragma once

#include <stdint.h>

/*
 * Integer-only easing, oscillator, hash, and scheduling toolkit.
 *
 * Everything operates in Q10 fixed point: FABLE_ONE (1024) represents 1.0.
 * Curves take a phase t in [0, FABLE_ONE] (inputs are clamped) and return a
 * Q10 value. Monotone eases map [0,1] -> [0,1]; the back/settle family may
 * exceed FABLE_ONE on purpose (overshoot) but stays within documented bounds.
 *
 * All functions are pure, allocation-free, and use no floating point, so the
 * same inputs produce byte-identical results on ESP32-S3, host, and wasm32.
 * Intermediate products use int64_t where a cubic in Q10 would overflow
 * 32 bits.
 */

enum {
    FABLE_ONE = 1024,          /* Q10 unit */
    FABLE_TURN = 4096,         /* full circle for fable_cos_turn/sin_turn */
    FABLE_SAMPLES_PER_MS = 16, /* 16 kHz sample clock */
};

static inline int32_t fable_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static inline uint32_t fable_ms_to_samples(uint32_t ms)
{
    return ms * (uint32_t)FABLE_SAMPLES_PER_MS;
}

/* Deterministic avalanche hash (lowbias32 constants, public domain). */
uint32_t fable_hash(uint32_t x);

/* Combine a seed and a lattice/slot index into one hash. */
uint32_t fable_hash2(uint32_t seed, uint32_t index);

/* Floor square root of a 32-bit value. */
uint32_t fable_isqrt(uint32_t v);

/*
 * Quarter-wave LUT cosine/sine. Angle wraps modulo FABLE_TURN; result is
 * Q10 in [-FABLE_ONE, FABLE_ONE].
 */
int32_t fable_cos_turn(uint32_t angle);
int32_t fable_sin_turn(uint32_t angle);

/* Monotone eases: [0,FABLE_ONE] -> [0,FABLE_ONE], exact at both endpoints. */
int32_t fable_ease_linear(int32_t t);
int32_t fable_ease_in_quad(int32_t t);
int32_t fable_ease_out_quad(int32_t t);
int32_t fable_ease_in_cubic(int32_t t);
int32_t fable_ease_out_cubic(int32_t t);
int32_t fable_ease_smooth(int32_t t);   /* 3t^2 - 2t^3 (smoothstep) */
int32_t fable_ease_smoother(int32_t t); /* 6t^5 - 15t^4 + 10t^3 (Perlin) */

/*
 * Overshoot family. fable_ease_out_back rises past FABLE_ONE (peak about
 * 1.10 * FABLE_ONE) before settling on FABLE_ONE; fable_ease_in_back dips
 * below zero (about -0.10 * FABLE_ONE) before rising - the classic
 * anticipation counter-move. Overshoot constant 1.70158 (Q10 1742) follows
 * Penner's easing equations.
 */
int32_t fable_ease_out_back(int32_t t);
int32_t fable_ease_in_back(int32_t t);

/*
 * Damped settle: 1 - decay(t) * cos(cycles_q2 quarter-turns * t). Starts at
 * 0, oscillates around FABLE_ONE with a cubic decay envelope, ends exactly
 * at FABLE_ONE. cycles_q2 = 6 gives 1.5 visible bounces. Bounded within
 * [-FABLE_ONE/4, FABLE_ONE*5/4].
 */
int32_t fable_settle(int32_t t, int32_t cycles_q2);

/*
 * Blend two ease outputs: result = a + (b - a) * mix / FABLE_ONE. Used to
 * dial personality-specific overshoot between smooth and back curves.
 */
int32_t fable_mix(int32_t a, int32_t b, int32_t mix);

/* Parabolic arc 4t(1-t): 0 at both ends, FABLE_ONE at the midpoint. */
int32_t fable_arc(int32_t t);

/*
 * 1-D value noise: hash lattice every `period` samples, smoothstep
 * interpolation between lattice values. Output Q10 in
 * [-FABLE_ONE, FABLE_ONE). Pure function of (clock, period, seed).
 */
int32_t fable_vnoise(uint32_t clock, uint32_t period, uint32_t seed);

/*
 * Deterministic aperiodic event scheduler. Time is cut into fixed slots of
 * `slot_len` samples; slot k hosts one event whose start is jittered inside
 * the slot by hash(seed, k) and whose duration is `duration` samples
 * (clamped to the slot). Because everything derives from the clock, any
 * frame can be evaluated in O(1) with no retained state.
 */
typedef struct {
    uint32_t slot;       /* slot index containing `clock` */
    uint32_t rand;       /* hash for this slot (event parameters) */
    uint32_t start;      /* event start clock */
    uint32_t duration;   /* event duration in samples */
    int32_t phase;       /* Q10 progress: <0 before, 0..FABLE_ONE during,
                            FABLE_ONE after the event */
    uint32_t since_end;  /* samples since event end (0 while pending) */
    uint32_t next_start; /* start of the following slot's event */
    uint32_t next_rand;  /* hash of the following slot */
    uint32_t prev_rand;  /* hash of the previous slot */
} fable_event_t;

void fable_schedule(uint32_t clock, uint32_t slot_len, uint32_t duration,
                    uint32_t seed, fable_event_t *out);
