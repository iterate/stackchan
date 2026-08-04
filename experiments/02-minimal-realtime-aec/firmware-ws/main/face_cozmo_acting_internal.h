#pragma once

#include "face_cozmo_acting.h"

/*
 * Internal contract for the face_cozmo_acting module.
 *
 * Fixed-point conventions:
 *   Q4  — screen geometry, 1/16 pixel;
 *   Q8  — envelopes, scales, gaze units (256 == 1.0);
 *   Q12 — lid slope/bend per local Q4 x;
 *   Q14 — sine/cosine.
 * All arithmetic is 32-bit with explicit 64-bit widening for products.
 * Negative right shifts go through fca_sar32/fca_sar64 so no expression
 * relies on implementation-defined behavior.
 */

#define FCA_Q4 16
#define FCA_Q8 256
#define FCA_Q14 16384

/* Milliseconds from the 16 kHz PCM sample clock. */
static inline uint32_t fca_ms(uint32_t sample_clock)
{
    return sample_clock / 16U;
}

static inline int32_t fca_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int32_t fca_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t fca_max(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t fca_abs(int32_t v) { return v < 0 ? -v : v; }

static inline int32_t fca_sar32(int32_t v, unsigned s)
{
    if (v >= 0) {
        return (int32_t)((uint32_t)v >> s);
    }
    return (int32_t)~((~(uint32_t)v) >> s);
}

static inline int64_t fca_sar64(int64_t v, unsigned s)
{
    if (v >= 0) {
        return (int64_t)((uint64_t)v >> s);
    }
    return (int64_t)~((~(uint64_t)v) >> s);
}

/* (a * b) >> 8 with 64-bit widening. */
static inline int32_t fca_mul_q8(int32_t a, int32_t b)
{
    return (int32_t)fca_sar64((int64_t)a * b, 8);
}

/* Exact floor integer square root. */
static inline uint32_t fca_isqrt(uint32_t v)
{
    uint32_t r = 0U;
    uint32_t bit = 1UL << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0U) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/*
 * Deterministic stream hash: Murmur3-style finalizer over an epoch index,
 * a stream tag, and the profile seed. Every stochastic decision the
 * director takes flows through this, so the same clock always replays the
 * same performance on every platform.
 */
static inline uint32_t fca_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85EBCA6BU;
    x ^= x >> 13;
    x *= 0xC2B2AE35U;
    x ^= x >> 16;
    return x;
}

static inline uint32_t fca_hash(uint32_t epoch, uint32_t tag, uint32_t seed)
{
    return fca_mix32((epoch ^ (tag * 0x9E3779B1U)) + seed * 0x27D4EB2FU);
}

/* Independent random stream tags. */
enum {
    FCA_TAG_BLINK = 1,
    FCA_TAG_BLINK_KIND = 2,
    FCA_TAG_BLINK_OFFSET = 3,
    FCA_TAG_AVERT = 4,
    FCA_TAG_AVERT_DIR = 5,
    FCA_TAG_AVERT_LEN = 6,
    FCA_TAG_MICRO = 7,
    FCA_TAG_MICRO_DIR = 8,
    FCA_TAG_ACT = 9,
    FCA_TAG_ACT_KIND = 10,
    FCA_TAG_ACT_SIDE = 11,
    FCA_TAG_DRIFT = 12,
};

/* Quarter-turn sine, Q14; turn16 is a 0..65535 full revolution. */
int32_t fca_sin_q14(uint32_t turn16);

static inline int32_t fca_cos_q14(uint32_t turn16)
{
    return fca_sin_q14(turn16 + 16384U);
}

/* Phase of a period as 0..65535, safe for any t. */
static inline uint32_t fca_turn16(uint32_t t_ms, uint32_t period_ms)
{
    return (uint32_t)(((uint64_t)(t_ms % period_ms) << 16) / period_ms);
}

/* Smoothstep on Q8 progress (0..256 -> 0..256). */
static inline int32_t fca_smooth_q8(int32_t e)
{
    e = fca_clamp(e, 0, 256);
    return (int32_t)fca_sar64((int64_t)e * e * (768 - 2 * e), 16);
}

/* Cubic ease-out with a soft landing. */
static inline int32_t fca_ease_out_q8(int32_t e)
{
    e = fca_clamp(e, 0, 256);
    const int32_t inv = 256 - e;
    return 256 - (int32_t)fca_sar64((int64_t)inv * inv * inv, 16);
}

/* Quadratic ease-in. */
static inline int32_t fca_ease_in_q8(int32_t e)
{
    e = fca_clamp(e, 0, 256);
    return (e * e) >> 8;
}

static inline int32_t fca_lerp_q8(int32_t a, int32_t b, int32_t e)
{
    return a + (int32_t)fca_sar64((int64_t)(b - a) * fca_clamp(e, 0, 256), 8);
}

/*
 * A raised-cosine pulse: 0 at e=0 and e=256, 256 at e=128. Used for
 * envelopes that must start and end at rest so epoch boundaries can
 * never produce a visible step.
 */
static inline int32_t fca_pulse_q8(int32_t e)
{
    e = fca_clamp(e, 0, 256);
    const int32_t c = fca_cos_q14((uint32_t)(e << 8));
    return (int32_t)((((int64_t)(16384 - c)) * 256) >> 15);
}

/* ---- director output ------------------------------------------------- */

typedef struct {
    /* Autonomous gaze offset, Q8 of full travel (-256..256). */
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    /* Vertical gaze the lids believe in (leads gaze by the profile's
     * anticipation window so lids move first). */
    int32_t lid_gaze_y_q8;
    /* Blink aperture multiplier per eye, 0..256, with reopen overshoot
     * up to ~276. */
    int32_t blink_q8[2];
    uint8_t blink_state;
    /* Idle act channel. */
    uint8_t act_id;
    int32_t act_gaze_x_q8;
    int32_t act_gaze_y_q8;
    int32_t act_squint_q8;   /* extra lid closure 0..256 */
    int32_t act_roll_mdeg;
    /* Breathing bob in Q4 pixels (+down) and its exhale shade 0..64. */
    int32_t breath_y_q4;
    /* Saccade squash: transient horizontal stretch, Q8 around 256. */
    int32_t dart_stretch_q8;
    uint8_t saccade_active;
    /* Speech emphasis envelope 0..256 derived from level + jaw. */
    int32_t emphasis_q8;
} fca_score_t;

/* ---- profile definitions --------------------------------------------- */

typedef struct {
    uint16_t blink_period_ms[4];  /* per activity: idle/listen/think/speak */
    uint16_t avert_period_ms[4];
    uint8_t avert_prob_pct[4];
    uint8_t wander_q8[4];         /* aversion amplitude per activity */
    uint16_t breath_period_ms;
    uint8_t breath_amp_q4;
    uint8_t micro_gain_q8;        /* microsaccade amplitude, 0 disables */
    uint8_t act_every_s;          /* mean seconds between idle acts */
    uint8_t reopen_overshoot_q8;  /* extra aperture on reopen, 0..48 */
    uint8_t blink_asym_ms;        /* per-eye blink offset */
    uint8_t saccade_verve_q8;     /* dart stretch gain */
    uint8_t drowsy;               /* sleepy baseline lids when idle */
} fca_direction_t;

typedef struct {
    uint16_t body;        /* main emissive color */
    uint16_t core;        /* hotspot core (near white) */
    uint16_t rim;         /* darker rim */
    uint16_t seam;        /* lid seam shade */
    uint16_t floor_glow;  /* under-eye ambient, 0 disables */
    uint16_t mouth;       /* mouth color, CHATTER only */
    uint16_t warm_body;   /* graded variants for valence tinting */
    uint16_t cool_body;
} fca_palette_t;

typedef struct {
    const char *slug;
    const char *name;
    int16_t eye_cy;            /* pair center row, px */
    int16_t eye_gap;           /* center-to-center distance, px */
    int16_t eye_hw;            /* half width, px */
    int16_t eye_hh;            /* half height, px */
    int16_t corner_px[4];      /* TL, TR, BL, BR radii, px */
    int16_t travel_x;          /* max autonomous gaze travel, px */
    int16_t travel_y;
    int16_t bloom_px;          /* bloom reach beyond the silhouette */
    uint8_t hotspot_q8;        /* baseline hotspot size, Q8 of eye */
    uint8_t sheen_q8;          /* vertical interior sheen strength */
    uint8_t has_mouth;
    int16_t mouth_cy;
    int16_t mouth_hw;
    /* How far the mouth may articulate: 255 is a full viseme mouth,
     * lower values give a compact emotion-aware accent mouth. */
    uint8_t mouth_verve;
    fca_direction_t direction;
    fca_palette_t palette;
    uint32_t seed;
} fca_profile_def_t;

const fca_profile_def_t *fca_profile(face_cozmo_acting_profile_t profile);

/* Director: sample clock + activity/attention/speech -> autonomous score. */
void fca_direct(
    const fca_profile_def_t *def,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fca_score_t *score);

/* Painter entry: rasterize a resolved pose. */
void fca_paint(
    const fca_profile_def_t *def,
    const face_cozmo_acting_pose_t *pose,
    uint16_t *rgb565);

/* RGB565 helpers shared by the painter. */
static inline uint16_t fca_rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r & 0x1FU) << 11) | ((g & 0x3FU) << 5) | (b & 0x1FU));
}

/* Alpha blend with a 0..64 coverage. */
static inline uint16_t fca_blend565(uint16_t dst, uint16_t src, uint32_t a64)
{
    const uint32_t inv = 64U - a64;
    const uint32_t r =
        (((dst >> 11) & 0x1FU) * inv + ((src >> 11) & 0x1FU) * a64 + 32U) >> 6;
    const uint32_t g =
        (((dst >> 5) & 0x3FU) * inv + ((src >> 5) & 0x3FU) * a64 + 32U) >> 6;
    const uint32_t b = ((dst & 0x1FU) * inv + (src & 0x1FU) * a64 + 32U) >> 6;
    return fca_rgb565(r, g, b);
}

/* Saturating additive blend scaled by a 0..64 alpha. */
static inline uint16_t fca_add565(uint16_t dst, uint16_t src, uint32_t a64)
{
    uint32_t r = ((dst >> 11) & 0x1FU) + ((((src >> 11) & 0x1FU) * a64) >> 6);
    uint32_t g = ((dst >> 5) & 0x3FU) + ((((src >> 5) & 0x3FU) * a64) >> 6);
    uint32_t b = (dst & 0x1FU) + (((src & 0x1FU) * a64) >> 6);
    r = r > 0x1FU ? 0x1FU : r;
    g = g > 0x3FU ? 0x3FU : g;
    b = b > 0x1FU ? 0x1FU : b;
    return fca_rgb565(r, g, b);
}

/* Mix two RGB565 colors by Q8 weight toward `to`. */
static inline uint16_t fca_mix565(uint16_t from, uint16_t to, int32_t w_q8)
{
    const uint32_t w = (uint32_t)fca_clamp(w_q8, 0, 256) >> 2; /* 0..64 */
    return fca_blend565(from, to, w);
}
