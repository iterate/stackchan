#pragma once

#include "cyber_face.h"

/*
 * Internal fixed-point toolkit shared by the profile renderers.
 *
 * Conventions:
 *  - Screen space is full-resolution pixels in Q4 (1.0 px == 16 units),
 *    stored in int32_t. The 160x120 frame spans [0, 2560) x [0, 1920).
 *  - Signed distances are Q4 pixels. Negative means inside.
 *  - Angles are uint32_t turns where one full turn == 65536; the sine
 *    table quantizes to CYBER_SIN_TABLE_SIZE steps of Q14 output.
 *  - Brightness accumulates in uint32_t Q8 (0..255 usable range) before
 *    palette lookup.
 *
 * Everything here must stay integer-only and free of implementation-
 * defined traps: no signed overflow, shifts only on non-negative or
 * unsigned values wherever the shifted amount could matter, fixed-width
 * types throughout. That is what makes host, WASM, and Xtensa builds
 * byte-identical.
 */

#define CYBER_Q4(px) ((int32_t)((px) * 16))
#define CYBER_TURN 65536u
#define CYBER_CTX_MAGIC 0xCB3AF00Du

static inline int32_t cyber_min32(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static inline int32_t cyber_max32(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static inline int32_t cyber_clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int32_t cyber_abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

/*
 * Integer square root of a non-negative 32-bit value via the classic
 * shift-and-subtract method: 16 iterations, no division, exact floor.
 */
static inline uint32_t cyber_isqrt32(uint32_t v)
{
    uint32_t result = 0;
    uint32_t bit = 1u << 30;
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

/*
 * Exact Euclidean length of a Q4 vector, in Q4. Components must stay
 * below 8192 (512 px) so the squared sum fits in uint32_t; every caller
 * works in screen space where the diagonal is exactly 200 px.
 */
static inline int32_t cyber_length_q4(int32_t dx, int32_t dy)
{
    uint32_t x = (uint32_t)cyber_abs32(dx);
    uint32_t y = (uint32_t)cyber_abs32(dy);
    return (int32_t)cyber_isqrt32(x * x + y * y);
}

/*
 * Alpha-max-plus-beta-min octagonal magnitude approximation with
 * alpha = 15/16, beta = 15/32 (largest error about 1.6 percent). Used
 * where a slightly lumpy but very cheap norm is fine, e.g. wide glows.
 */
static inline int32_t cyber_length_fast_q4(int32_t dx, int32_t dy)
{
    int32_t x = cyber_abs32(dx);
    int32_t y = cyber_abs32(dy);
    int32_t hi = cyber_max32(x, y);
    int32_t lo = cyber_min32(x, y);
    return (hi * 30 + lo * 15) >> 5;
}

/* Deterministic avalanche hash (SplitMix-style mixing constants). */
static inline uint32_t cyber_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Sine lookup: angle in 1/65536 turns, result Q14 in [-16384, 16384]. */
static inline int32_t cyber_sin_q14(const cyber_face_ctx_t *ctx,
                                    uint32_t turns)
{
    return ctx->sin_q14[(turns >> 6) & (CYBER_SIN_TABLE_SIZE - 1u)];
}

static inline int32_t cyber_cos_q14(const cyber_face_ctx_t *ctx,
                                    uint32_t turns)
{
    return cyber_sin_q14(ctx, turns + (CYBER_TURN / 4u));
}

/* Glow lookup on a clamped Q4 distance (index saturates at table end). */
static inline uint32_t cyber_glow(const uint8_t *table, int32_t dist_q4)
{
    uint32_t idx = (uint32_t)cyber_max32(dist_q4, 0);
    if (idx >= CYBER_GLOW_TABLE_SIZE) {
        idx = CYBER_GLOW_TABLE_SIZE - 1u;
    }
    return table[idx];
}

/* Saturating Q8 brightness add. */
static inline uint32_t cyber_sat_add(uint32_t a, uint32_t b)
{
    uint32_t sum = a + b;
    return sum > 255u ? 255u : sum;
}

/*
 * Quadratic polynomial smooth minimum of two Q4 distances with blend
 * radius k (Q4): h = max(k - |a - b|, 0); smin = min(a, b) - h*h/(4k).
 */
static inline int32_t cyber_smin_q4(int32_t a, int32_t b, int32_t k)
{
    int32_t h = k - cyber_abs32(a - b);
    if (h < 0) {
        h = 0;
    }
    return cyber_min32(a, b) - (h * h) / (4 * k);
}

/* Signed distance to a circle at (cx, cy) with radius r; all Q4. */
static inline int32_t cyber_sd_circle(int32_t x, int32_t y, int32_t cx,
                                      int32_t cy, int32_t r)
{
    return cyber_length_q4(x - cx, y - cy) - r;
}

/*
 * Signed distance to an axis-aligned rounded box centred at (cx, cy)
 * with half extents (hx, hy) and corner radius r; all Q4.
 */
static inline int32_t cyber_sd_round_box(int32_t x, int32_t y, int32_t cx,
                                         int32_t cy, int32_t hx, int32_t hy,
                                         int32_t r)
{
    int32_t qx = cyber_abs32(x - cx) - (hx - r);
    int32_t qy = cyber_abs32(y - cy) - (hy - r);
    int32_t outside =
        cyber_length_q4(cyber_max32(qx, 0), cyber_max32(qy, 0));
    int32_t inside = cyber_min32(cyber_max32(qx, qy), 0);
    return outside + inside - r;
}

/*
 * Signed distance to a horizontal capsule (segment with radius): the
 * segment runs from (cx - hw, cy) to (cx + hw, cy); all Q4.
 */
static inline int32_t cyber_sd_hcapsule(int32_t x, int32_t y, int32_t cx,
                                        int32_t cy, int32_t hw, int32_t r)
{
    int32_t px = cyber_clamp32(x - cx, -hw, hw);
    return cyber_length_q4(x - cx - px, y - cy) - r;
}

/* Signed distance to an arbitrary segment (a..b) with radius r; all Q4. */
int32_t cyber_sd_segment(int32_t x, int32_t y, int32_t ax, int32_t ay,
                         int32_t bx, int32_t by, int32_t r);

/*
 * Deterministic idle-motion state derived purely from the sample clock
 * and the keyframe. Distilled once per frame, consumed by every profile.
 */
typedef struct {
    /* Milliseconds since clock zero (wraps with uint32 arithmetic). */
    uint32_t t_ms;
    /* 0 open .. 255 closed, per eye, blink and keyframe combined. */
    uint32_t lid_close_left;
    uint32_t lid_close_right;
    /* Saccade + gaze offset in Q4 pixels, already blended and clamped. */
    int32_t gaze_x_q4;
    int32_t gaze_y_q4;
    /* Breathing bob in Q4 pixels (vertical) and Q8 glow gain 192..320. */
    int32_t breath_y_q4;
    uint32_t glow_gain_q8;
    /* Mouth geometry in Q4: half width, half height, corner curve. */
    int32_t mouth_half_w_q4;
    int32_t mouth_half_h_q4;
    int32_t mouth_curve_q4;   /* positive curls up (smile) */
    int32_t mouth_round_q8;   /* 0 wide .. 255 round O   */
    /* Brow vertical offset in Q4 (positive lowers brows) and tilt Q8. */
    int32_t brow_drop_q4;
    int32_t brow_tilt_q8;
    /* Convenience flags. */
    uint32_t speaking;
    uint32_t expression;
} cyber_motion_t;

void cyber_motion_compute(const cyber_face_ctx_t *ctx,
                          const cyber_keyframe_t *keyframe,
                          uint32_t sample_clock, cyber_motion_t *motion);

/* Shared 8x8 Bayer threshold matrix, values 0..63 (row-major). */
extern const uint8_t cyber_bayer8[64];

/* Per-profile field renderers implemented in cyber_profiles.c. */
void cyber_render_profile(cyber_face_ctx_t *ctx, cyber_profile_t profile,
                          const cyber_motion_t *motion,
                          const cyber_keyframe_t *keyframe,
                          uint32_t sample_clock, uint16_t *rgb565);
