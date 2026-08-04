#pragma once

#include "face_robot_eyes_core.h"

/*
 * Internal contract shared by the behavior solver, rasterizer, and profile
 * tables. Everything is integer fixed point:
 *   Q4  — screen geometry, 1/16 pixel;
 *   Q8  — normalized envelopes and scales, 256 == 1.0;
 *   Q12 — slopes and curvature per Q4 x;
 *   Q14 — rotation cos/sin.
 * All arithmetic is 32-bit with explicit widening; no float, no
 * implementation-defined behavior, so WASM output is byte-identical.
 */

#define FRE_Q4 16
#define FRE_Q8 256
#define FRE_Q14 16384

/* ---- deterministic hashing (splitmix32 finalizer) ------------------- */

static inline uint32_t fre_mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

/* Hash of (epoch index, stream salt, profile seed). Every stochastic
 * choice in the engine flows through this, so a given profile replays the
 * exact same life at the same sample clock on every platform. */
static inline uint32_t fre_hash3(uint32_t epoch, uint32_t salt, uint32_t seed)
{
    return fre_mix(epoch * 0x9E3779B9U + salt * 0x85EBCA77U + seed);
}

/* Salts: one per independent random stream. */
enum {
    FRE_SALT_BLINK_PHASE = 1,
    FRE_SALT_BLINK_DOUBLE = 2,
    FRE_SALT_BLINK_LEAD = 3,
    FRE_SALT_GAZE_RADIUS = 4,
    FRE_SALT_GAZE_ANGLE = 5,
    FRE_SALT_GAZE_PHASE = 6,
    FRE_SALT_GAZE_BLINK = 7,
    FRE_SALT_MICRO_PHASE = 8,
    FRE_SALT_MICRO_ANGLE = 9,
    FRE_SALT_ACT_KIND = 10,
    FRE_SALT_ACT_PHASE = 11,
    FRE_SALT_ACT_SIDE = 12,
    FRE_SALT_THINK_SIDE = 13,
    FRE_SALT_DOZE = 14,
    FRE_SALT_TWITCH = 15,
    FRE_SALT_SCAN = 16,
};

/* ---- small math helpers --------------------------------------------- */

static inline int32_t fre_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/*
 * Portable arithmetic right shift (rounds toward negative infinity) so the
 * module never relies on the implementation-defined behavior of `>>` on
 * negative operands. Compilers reduce these to a single shift.
 */
static inline int32_t fre_sar32(int32_t v, unsigned s)
{
    if (v >= 0) {
        return (int32_t)((uint32_t)v >> s);
    }
    return (int32_t)~((~(uint32_t)v) >> s);
}

static inline int64_t fre_sar64(int64_t v, unsigned s)
{
    if (v >= 0) {
        return (int64_t)((uint64_t)v >> s);
    }
    return (int64_t)~((~(uint64_t)v) >> s);
}

static inline int32_t fre_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t fre_max(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t fre_abs(int32_t v) { return v < 0 ? -v : v; }

/* Integer sqrt of a uint32, exact floor. */
static inline uint32_t fre_isqrt(uint32_t v)
{
    uint32_t r = 0;
    uint32_t bit = 1UL << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
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

/* sin of a 0..65535 turn angle, Q14 result. */
int32_t fre_sin_q14(uint32_t turn16);

/* Phase of a periodic oscillator as a 0..65535 turn, safe for any t:
 * reduces modulo the period before scaling so the product never wraps. */
static inline uint32_t fre_turn16(uint32_t t_ms, uint32_t period_ms)
{
    return ((t_ms % period_ms) * 65536U) / period_ms;
}

static inline int32_t fre_cos_q14(uint32_t turn16)
{
    return fre_sin_q14(turn16 + 16384U);
}

/* ---- easing on Q8 progress (0..256 in, 0..256 out) ------------------ */

static inline int32_t fre_smoothstep_q8(int32_t e)
{
    e = fre_clamp(e, 0, 256);
    /* e*e*(3*256 - 2*e) / 256^2 */
    return (int32_t)(((int64_t)e * e * (768 - 2 * e)) >> 16);
}

static inline int32_t fre_ease_in_q8(int32_t e)
{
    e = fre_clamp(e, 0, 256);
    return (e * e) >> 8;
}

static inline int32_t fre_ease_out_cubic_q8(int32_t e)
{
    e = fre_clamp(e, 0, 256);
    int32_t inv = 256 - e;
    return 256 - (int32_t)(((int64_t)inv * inv * inv) >> 16);
}

/* Linear interpolation between two Q values by Q8 progress. */
static inline int32_t fre_lerp_q8(int32_t a, int32_t b, int32_t e)
{
    return a + (int32_t)(((int64_t)(b - a) * e) >> 8);
}

/* ---- behavior tuning (per profile) ---------------------------------- */

typedef struct {
    /* Percent scales, 100 == the literature-derived baseline. */
    int16_t blink_cycle_pct;  /* spacing between spontaneous blinks */
    int16_t reopen_pct;       /* lid reopening duration */
    int16_t wander_pct;       /* idle gaze wander amplitude */
    int16_t micro_pct;        /* microsaccade + drift amplitude (0 = off) */
    int16_t overshoot_pct;    /* saccade follow-through overshoot */
    int16_t brow_gain_pct;    /* brow choreography gain */
    int16_t lid_lead_ms;      /* lid anticipation lookahead */
    uint16_t act_mask;        /* bitmask of permitted FRE_ACT_* ids */
    uint8_t drowsy;           /* run the doze/sleep engine when idle */
    uint8_t cat_slow_blink;   /* replace some blinks with slow half-blinks */
    uint8_t tilt_acts;        /* curious head-tilt acts */
    uint8_t asym_pct;         /* per-eye blink asymmetry strength */
} fre_tuning_t;

/* ---- pixel styles (per profile) ------------------------------------- */

enum {
    FRE_SHAPE_ROUNDRECT = 0, /* four independent corner radii */
    FRE_SHAPE_STADIUM = 1,   /* rounded rect with maximal radii */
    FRE_SHAPE_DISC = 2,
    FRE_SHAPE_WEDGE = 3,     /* EVE-like tilted teardrop */
};

enum {
    FRE_IRIS_NONE = 0,
    FRE_IRIS_PUPIL = 1,      /* pupil disc only */
    FRE_IRIS_FULL = 2,       /* iris ring + pupil + highlight */
    FRE_IRIS_SLIT = 3,       /* cat vertical slit */
    FRE_IRIS_GLINT = 4,      /* manga corner highlight only */
};

enum {
    FRE_MOOD_NEUTRAL = 0,
    FRE_MOOD_TIRED = 1,   /* upper lids angled down-outward */
    FRE_MOOD_ALERT = 2,   /* slight upper-lid lift, wide */
    FRE_MOOD_HAPPY = 3,   /* lower lids raised in an arc */
};

typedef struct {
    uint16_t bg_top;
    uint16_t bg_bottom;
    uint16_t eye_color;
    uint16_t eye_edge_color;   /* darker rim; equal to eye_color disables */
    uint16_t iris_color;
    uint16_t pupil_color;
    uint16_t highlight_color;
    uint16_t brow_color;
    uint16_t mouth_color;
    int16_t eye_cy;            /* eye row center, px */
    int16_t eye_gap;           /* center-to-center distance, px */
    int16_t eye_hw;            /* half width, px */
    int16_t eye_hh;            /* half height, px */
    int16_t corner_r[4];       /* TL, TR, BL, BR radii, px */
    int16_t travel_x;          /* max gaze travel, px */
    int16_t travel_y;
    int16_t iris_r;
    int16_t pupil_r;
    int16_t iris_travel_extra; /* extra iris travel for parallax, px */
    int16_t glow_range;        /* px of soft falloff outside the shape */
    uint8_t glow_alpha;        /* 0..32 peak glow */
    uint8_t shape;
    uint8_t iris_kind;
    uint8_t base_mood;
    uint8_t has_brow;
    int16_t brow_dy;           /* brow center above eye center, px */
    int16_t brow_len;          /* half length, px */
    int16_t brow_th;           /* half thickness, px */
    uint8_t mouth_kind;        /* FRE_MOUTH_* */
    int16_t mouth_cy;
    int16_t mouth_hw;
    int16_t mouth_hh;
    uint8_t closed_line;       /* draw a lid line when eye fully closed */
    uint8_t pixel_grid;        /* 0 = smooth; else LED cell size in px */
    uint8_t single_eye;        /* cyclops orb */
    uint16_t ops_estimate;
} fre_style_t;

/* Per-profile registration row. */
typedef struct {
    const char *slug;
    const char *name;
    uint8_t family;
    fre_tuning_t tuning;
    fre_style_t style;
} fre_profile_def_t;

const fre_profile_def_t *fre_profile_def(fre_profile_t profile);

/* ---- rasterizer ------------------------------------------------------ */

typedef struct {
    uint16_t *pixels; /* FRE_FRAME_WIDTH x FRE_FRAME_HEIGHT */
} fre_canvas_t;

/* One eye (or orb) draw request, fully resolved to Q4 screen space. */
typedef struct {
    int32_t cx_q4;
    int32_t cy_q4;
    int32_t hw_q4;
    int32_t hh_q4;
    int32_t r_q4[4];          /* TL, TR, BL, BR */
    int32_t rot_cos_q14;      /* rotation about (cx, cy) */
    int32_t rot_sin_q14;
    /* Upper/lower lid cut lines in the eye's local rotated frame:
     * y = base + (slope*x >> 12) + (bend*x*x >> 20), x from center. */
    int32_t ulid_base_q4;
    int32_t ulid_slope_q12;
    int32_t ulid_bend_q12;
    int32_t llid_base_q4;
    int32_t llid_slope_q12;
    int32_t llid_bend_q12;
    /* Iris block, in the local frame (already includes parallax). */
    int32_t iris_cx_q4;
    int32_t iris_cy_q4;
    int32_t iris_r_q4;
    int32_t pupil_r_q4;      /* slit half-width when iris_kind == SLIT */
    int32_t high_cx_q4;      /* specular highlight */
    int32_t high_cy_q4;
    int32_t high_r_q4;
    uint16_t color;
    uint16_t edge_color;
    uint16_t iris_color;
    uint16_t pupil_color;
    uint16_t highlight_color;
    uint8_t iris_kind;
    uint8_t glow_alpha;
    int32_t glow_range_q4;
} fre_eye_draw_t;

void fre_fill_gradient(fre_canvas_t *c, uint16_t top, uint16_t bottom);
void fre_draw_eye(fre_canvas_t *c, const fre_eye_draw_t *e);
/* Coverage 0..32 of the eye body (with lids) at one Q4 point; used by the
 * LED-matrix profile to sample shapes into cells. */
int32_t fre_eye_alpha_at(const fre_eye_draw_t *e, int32_t x_q4, int32_t y_q4);

/* Quadratic capsule stroke: y(x) = cy + slope*(x-cx)/4096 + arch*(x-cx)^2
 * /  2^20, drawn for x in [cx-half_len, cx+half_len] with rounded ends. */
void fre_draw_capsule(
    fre_canvas_t *c,
    int32_t cx_q4,
    int32_t cy_q4,
    int32_t half_len_q4,
    int32_t slope_q12,
    int32_t arch_q12,
    int32_t half_th_q4,
    uint16_t color);

void fre_draw_disc(
    fre_canvas_t *c,
    int32_t cx_q4,
    int32_t cy_q4,
    int32_t r_q4,
    uint16_t color,
    uint8_t alpha_max);

/* RGB565 helpers. */
static inline uint16_t fre_rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r & 0x1FU) << 11) | ((g & 0x3FU) << 5) | (b & 0x1FU));
}

static inline uint16_t fre_blend565(uint16_t dst, uint16_t src, uint32_t a32)
{
    /* a32 in 0..32 */
    uint32_t dr = (dst >> 11) & 0x1FU;
    uint32_t dg = (dst >> 5) & 0x3FU;
    uint32_t db = dst & 0x1FU;
    uint32_t sr = (src >> 11) & 0x1FU;
    uint32_t sg = (src >> 5) & 0x3FU;
    uint32_t sb = src & 0x1FU;
    uint32_t inv = 32U - a32;
    uint32_t r = (dr * inv + sr * a32 + 16U) >> 5;
    uint32_t g = (dg * inv + sg * a32 + 16U) >> 5;
    uint32_t b = (db * inv + sb * a32 + 16U) >> 5;
    return fre_rgb565(r, g, b);
}

/* Additive glow blend: saturating add of a scaled source color. */
static inline uint16_t fre_add565(uint16_t dst, uint16_t src, uint32_t a32)
{
    uint32_t r = ((dst >> 11) & 0x1FU) + ((((src >> 11) & 0x1FU) * a32) >> 5);
    uint32_t g = ((dst >> 5) & 0x3FU) + ((((src >> 5) & 0x3FU) * a32) >> 5);
    uint32_t b = (dst & 0x1FU) + (((src & 0x1FU) * a32) >> 5);
    if (r > 0x1FU) {
        r = 0x1FU;
    }
    if (g > 0x3FU) {
        g = 0x3FU;
    }
    if (b > 0x1FU) {
        b = 0x1FU;
    }
    return fre_rgb565(r, g, b);
}
