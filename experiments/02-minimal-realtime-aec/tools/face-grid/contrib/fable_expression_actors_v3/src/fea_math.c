#include "fea_internal.h"

/* splitmix32 finalizer — cheap avalanche for clock-epoch scheduling */
uint32_t fea_hash32(uint32_t value)
{
    uint32_t x = value + 0x9e3779b9U;
    x ^= x >> 16;
    x *= 0x21f0aaadU;
    x ^= x >> 15;
    x *= 0x735a2d97U;
    x ^= x >> 15;
    return x;
}

uint32_t fea_hash2(uint32_t a, uint32_t b)
{
    return fea_hash32(a ^ (b * 0x9e3779b9U) ^ 0x5bd1e995U);
}

int32_t fea_clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

/* Integer sqrt of a non-negative 64-bit value (floor). */
int32_t fea_isqrt64(int64_t value)
{
    if (value <= 0) {
        return 0;
    }
    uint64_t v = (uint64_t)value;
    uint64_t result = 0U;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (v >= result + bit) {
            v -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)result;
}

/* Quarter-wave sine table, Q14: sin(i/64 * pi/2). */
static const int16_t SIN_Q14[65] = {
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

int16_t fea_sin_q14(uint32_t turn_u16)
{
    const uint32_t turn = turn_u16 & 0xffffU;
    const uint32_t quadrant = turn >> 14;          /* 0..3 */
    uint32_t phase = turn & 0x3fffU;               /* Q14 within quadrant */
    if ((quadrant & 1U) != 0U) {
        phase = 0x4000U - phase;
    }
    const uint32_t index = phase >> 8;             /* 0..64 */
    const uint32_t frac = phase & 0xffU;
    const int32_t a = SIN_Q14[index];
    const int32_t b = SIN_Q14[index < 64U ? index + 1U : 64U];
    const int32_t value = a + (((b - a) * (int32_t)frac) >> 8);
    return (int16_t)(quadrant >= 2U ? -value : value);
}

int32_t fea_smoothstep_q8(int32_t t_q8)
{
    const int32_t t = fea_clamp_i32(t_q8, 0, 256);
    /* t^2 (3 - 2t) in Q8 */
    return (t * t * (768 - 2 * t)) >> 16;
}

/*
 * Acting response curve: anticipation dip (~-7 %), smooth rise,
 * ~108 % overshoot, exact settle at 256. 33 entries at weight steps
 * of 8; linear interpolation between entries.
 */
static const int16_t ACT_Q8[33] = {
    0, -13, -18, -11, 0, 3, 10, 20,
    33, 49, 67, 86, 106, 127, 147, 168,
    187, 205, 221, 234, 245, 252, 256, 262,
    269, 274, 276, 272, 262, 250, 246, 251,
    256,
};

int16_t fea_act_curve_q8(uint8_t weight)
{
    if (weight == 255U) {
        return 256;                     /* exact settle endpoint */
    }
    const uint32_t index = (uint32_t)weight >> 3;    /* 0..31 */
    const uint32_t frac = (uint32_t)weight & 7U;
    const int32_t a = ACT_Q8[index];
    const int32_t b = ACT_Q8[index + 1U];
    return (int16_t)(a + (((b - a) * (int32_t)frac) >> 3));
}

/*
 * Blink closure envelope. Close is quadratic ease-in (fast finish),
 * reopen is cubic ease-out (slow settle), per measured lid kinematics:
 * the reopen takes ~2.6x the close.
 */
int32_t fea_blink_wave_q8(uint32_t phase_samples)
{
    if (phase_samples >= (uint32_t)FEA_BLINK_TOTAL_SAMPLES) {
        return 0;
    }
    if (phase_samples < (uint32_t)FEA_BLINK_CLOSE_SAMPLES) {
        const int32_t t =
            (int32_t)((phase_samples << 8) / FEA_BLINK_CLOSE_SAMPLES);
        return (t * t) >> 8;                        /* ease-in */
    }
    const uint32_t after_close =
        phase_samples - (uint32_t)FEA_BLINK_CLOSE_SAMPLES;
    if (after_close < (uint32_t)FEA_BLINK_HOLD_SAMPLES) {
        return 256;
    }
    const uint32_t reopen =
        after_close - (uint32_t)FEA_BLINK_HOLD_SAMPLES;
    const int32_t t = (int32_t)((reopen << 8) / FEA_BLINK_OPEN_SAMPLES);
    const int32_t inv = 256 - t;
    return (((inv * inv) >> 8) * inv) >> 8;         /* cubic ease-out */
}
