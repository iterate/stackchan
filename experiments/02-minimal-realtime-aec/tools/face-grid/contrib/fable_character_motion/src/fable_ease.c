#include "fable_ease.h"

/*
 * Finalizer constants are Chris Wellons' "lowbias32" (hash-prospector,
 * public domain / CC0). The structure is the standard xorshift-multiply
 * avalanche; only the constants come from that search.
 */
uint32_t fable_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t fable_hash2(uint32_t seed, uint32_t index)
{
    return fable_hash(seed ^ (index * 0x9e3779b9U + 0x85ebca6bU));
}

uint32_t fable_isqrt(uint32_t v)
{
    uint32_t result = 0;
    uint32_t bit = 1UL << 30;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= result + bit) {
            v -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* cos(i * pi / 128) * 1024, i = 0..64: one quadrant of FABLE_TURN. */
static const int16_t COS_QUARTER[65] = {
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009,
    1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865,
    851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590,
    569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224,
    200, 175, 150, 125, 100, 75, 50, 25,
    0,
};

int32_t fable_cos_turn(uint32_t angle)
{
    const uint32_t a = angle & (uint32_t)(FABLE_TURN - 1);
    const uint32_t quadrant = a >> 10; /* 0..3 */
    const uint32_t within = a & 1023U;
    /* 1024 steps per quadrant, 64 LUT intervals: 16 steps per interval. */
    const uint32_t idx = within >> 4;
    const uint32_t frac = within & 15U;
    uint32_t i0;
    int32_t sign;

    if (quadrant == 0U || quadrant == 2U) {
        i0 = idx;
        sign = (quadrant == 0U) ? 1 : -1;
        const int32_t a0 = COS_QUARTER[i0];
        const int32_t a1 = COS_QUARTER[i0 + 1U];
        const int32_t value = a0 + (((a1 - a0) * (int32_t)frac) >> 4);
        return sign * value;
    }
    /* Quadrants 1 and 3 walk the LUT backwards. */
    i0 = 64U - idx;
    sign = (quadrant == 1U) ? -1 : 1;
    const int32_t b0 = COS_QUARTER[i0];
    const int32_t b1 = COS_QUARTER[i0 - 1U];
    const int32_t value = b0 + (((b1 - b0) * (int32_t)frac) >> 4);
    return sign * value;
}

int32_t fable_sin_turn(uint32_t angle)
{
    return fable_cos_turn(angle + (uint32_t)(FABLE_TURN * 3 / 4));
}

static int32_t clamp_phase(int32_t t)
{
    return fable_clamp_i32(t, 0, FABLE_ONE);
}

int32_t fable_ease_linear(int32_t t)
{
    return clamp_phase(t);
}

int32_t fable_ease_in_quad(int32_t t)
{
    t = clamp_phase(t);
    return (t * t) >> 10;
}

int32_t fable_ease_out_quad(int32_t t)
{
    t = clamp_phase(t);
    const int32_t u = FABLE_ONE - t;
    return FABLE_ONE - ((u * u) >> 10);
}

int32_t fable_ease_in_cubic(int32_t t)
{
    t = clamp_phase(t);
    return (int32_t)(((int64_t)t * t * t) >> 20);
}

int32_t fable_ease_out_cubic(int32_t t)
{
    t = clamp_phase(t);
    const int64_t u = FABLE_ONE - t;
    return FABLE_ONE - (int32_t)((u * u * u) >> 20);
}

int32_t fable_ease_smooth(int32_t t)
{
    t = clamp_phase(t);
    const int64_t t2 = (int64_t)t * t; /* Q20 */
    const int64_t t3 = t2 * t;         /* Q30 */
    return (int32_t)(((3 * (t2 << 10)) - 2 * t3) >> 20);
}

int32_t fable_ease_smoother(int32_t t)
{
    t = clamp_phase(t);
    /* Exact Q50 evaluation with one final truncation: the coefficients
       cancel heavily, so intermediate rounding must not compound. */
    const int64_t t2 = (int64_t)t * t;  /* Q20 */
    const int64_t t3 = t2 * t;          /* Q30 */
    const int64_t t4 = t3 * t;          /* Q40 */
    const int64_t t5 = t4 * t;          /* Q50 */
    return (int32_t)((6 * t5 - 15 * (t4 << 10) + 10 * (t3 << 20)) >> 40);
}

/* Penner overshoot constant 1.70158 in Q10. */
enum { BACK_K = 1742 };

int32_t fable_ease_out_back(int32_t t)
{
    t = clamp_phase(t);
    const int64_t u = t - FABLE_ONE; /* [-1024, 0] */
    const int64_t u2 = u * u;        /* Q20 */
    const int64_t u3 = u2 * u;       /* Q30 */
    const int64_t term =
        (((int64_t)BACK_K + FABLE_ONE) * u3 >> 30) + (BACK_K * u2 >> 20);
    return (int32_t)(FABLE_ONE + term);
}

int32_t fable_ease_in_back(int32_t t)
{
    t = clamp_phase(t);
    return FABLE_ONE - fable_ease_out_back(FABLE_ONE - t);
}

int32_t fable_settle(int32_t t, int32_t cycles_q2)
{
    t = clamp_phase(t);
    if (t >= FABLE_ONE) {
        return FABLE_ONE;
    }
    /* Quintic decay envelope: (1-t)^5 approximates an exponential and
       keeps the first bounce near the classic ~13% overshoot. */
    const int64_t u = FABLE_ONE - t;
    const int64_t u2 = u * u;
    const int32_t decay = (int32_t)((u2 * u2 * u) >> 40);
    const uint32_t angle =
        ((uint32_t)(cycles_q2 < 0 ? 0 : cycles_q2) * 1024U *
         (uint32_t)t) >> 10;
    const int32_t osc = fable_cos_turn(angle);
    return FABLE_ONE - ((decay * osc) >> 10);
}

int32_t fable_mix(int32_t a, int32_t b, int32_t mix)
{
    mix = clamp_phase(mix);
    return a + (int32_t)(((int64_t)(b - a) * mix) >> 10);
}

int32_t fable_arc(int32_t t)
{
    t = clamp_phase(t);
    return (int32_t)(((int64_t)4 * t * (FABLE_ONE - t)) >> 10);
}

static int32_t lattice_value(uint32_t seed, uint32_t k)
{
    /* Map the hash into [-FABLE_ONE, FABLE_ONE). */
    return (int32_t)(fable_hash2(seed, k) & 2047U) - FABLE_ONE;
}

int32_t fable_vnoise(uint32_t clock, uint32_t period, uint32_t seed)
{
    if (period == 0U) {
        return 0;
    }
    const uint32_t k = clock / period;
    const uint32_t within = clock % period;
    const int32_t f = (int32_t)(((uint64_t)within << 10) / period);
    const int32_t a = lattice_value(seed, k);
    const int32_t b = lattice_value(seed, k + 1U);
    const int32_t s = fable_ease_smooth(f);
    return a + (int32_t)(((int64_t)(b - a) * s) >> 10);
}

void fable_schedule(uint32_t clock, uint32_t slot_len, uint32_t duration,
                    uint32_t seed, fable_event_t *out)
{
    if (slot_len == 0U) {
        slot_len = 1U;
    }
    if (duration == 0U) {
        duration = 1U;
    }
    if (duration > slot_len) {
        duration = slot_len;
    }

    const uint32_t slot = clock / slot_len;
    const uint32_t rand = fable_hash2(seed, slot);
    const uint32_t jitter_room = slot_len - duration;
    const uint32_t start =
        slot * slot_len + (jitter_room != 0U ? rand % jitter_room : 0U);
    const uint32_t end = start + duration;

    out->slot = slot;
    out->rand = rand;
    out->start = start;
    out->duration = duration;

    if (clock < start) {
        out->phase = -1;
        out->since_end = 0U;
    } else if (clock < end) {
        out->phase =
            (int32_t)(((uint64_t)(clock - start) << 10) / duration);
        out->since_end = 0U;
    } else {
        out->phase = FABLE_ONE;
        out->since_end = clock - end;
    }

    const uint32_t next_slot = slot + 1U;
    const uint32_t next_rand = fable_hash2(seed, next_slot);
    out->next_rand = next_rand;
    out->next_start =
        next_slot * slot_len +
        (jitter_room != 0U ? next_rand % jitter_room : 0U);
    out->prev_rand = fable_hash2(seed, slot - 1U);
}
