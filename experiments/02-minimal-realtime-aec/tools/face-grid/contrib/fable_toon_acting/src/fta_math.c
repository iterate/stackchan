#include "fta_internal.h"

int32_t fta_clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

int32_t fta_min_i32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

int32_t fta_max_i32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

int32_t fta_abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

/* murmur3 finalizer: full avalanche, integer only */
uint32_t fta_hash_u32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

uint8_t fta_noise_u8(uint32_t slot, uint32_t salt, uint32_t lane)
{
    const uint32_t mixed =
        fta_hash_u32(slot * 0x9e3779b9U ^ salt ^ (lane * 0x85ebca6bU + 1U));
    return (uint8_t)(mixed >> 24);
}

uint8_t fta_smooth_u8(uint8_t value)
{
    const uint32_t x = value;
    return (uint8_t)((x * x * (765U - 2U * x) + 32512U) / 65025U);
}

/*
 * Smoothed triangle approximation of sine. Phase is Q16 turns; result is
 * Q8 in -256..256. y = t * (2m - |t|) / m^2 over the folded triangle t is
 * C1 continuous at the peaks and the zero crossings.
 */
int32_t fta_wave_q8(uint32_t phase_q16)
{
    const uint32_t turn = phase_q16 & 0xffffU;
    int32_t t;
    if (turn < 16384U) {
        t = (int32_t)turn;
    } else if (turn < 49152U) {
        t = 32768 - (int32_t)turn;
    } else {
        t = (int32_t)turn - 65536;
    }
    const int32_t magnitude = fta_abs_i32(t);
    return (t * (32768 - magnitude)) >> 20;
}

/*
 * Acting response curve (17-entry Q8 LUT, linearly interpolated).
 * Shape: anticipation dip of ~5% until w =~ 0.19, zero crossing near
 * w =~ 0.24, ~8% overshoot around w =~ 0.75, settled 1.0 at w = 1.
 * Stage cue attacks ramp the weight through this curve in time, so the
 * face dips before committing and settles after landing with no state.
 */
static const int16_t ACTING_LUT[17] = {
    0, -8, -14, -12, 6, 48, 104, 160, 206,
    238, 258, 270, 276, 274, 268, 261, 256,
};

int32_t fta_acting_q8(uint8_t weight)
{
    if (weight == 0U) {
        return 0;
    }
    if (weight == 255U) {
        return 256;
    }
    const uint32_t index = (uint32_t)weight >> 4;
    const int32_t fraction = (int32_t)(weight & 0x0fU);
    const int32_t base = ACTING_LUT[index];
    const int32_t next = ACTING_LUT[index + 1U];
    return base + ((next - base) * fraction) / 16;
}

int32_t fta_acting_mag_q8(uint8_t weight)
{
    const int32_t acted = fta_acting_q8(weight);
    return acted < 0 ? 0 : acted;
}
