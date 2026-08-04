#pragma once

#include <stdint.h>

#include "wildcard_face.h"

/*
 * Shared fixed-point kernel for the wildcard renderers. Everything here is a
 * pure function of its arguments; there is no global state and no float.
 */

/* --- fixed-point trig ---------------------------------------------------- */

/* phase: 0..65535 covers one full turn. Result is Q14 in [-16384, 16384]. */
int32_t wc_sin_q14(uint32_t phase);
int32_t wc_cos_q14(uint32_t phase);

/* --- integer helpers ------------------------------------------------------ */

static inline int32_t wc_min32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t wc_max32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t wc_clamp32(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline int32_t wc_abs32(int32_t v) { return v < 0 ? -v : v; }

uint32_t wc_isqrt32(uint32_t v);

/* Branch-light integer atan2: full turn mapped to 0..65535. */
uint32_t wc_atan2_u16(int32_t y, int32_t x);

/* lowbias32-style avalanche mix (original constants are public domain
 * research by Chris Wellons; this is an independent re-typing). */
static inline uint32_t wc_hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t wc_hash2(uint32_t a, uint32_t b) {
    return wc_hash_u32(a * 0x9e3779b9U + wc_hash_u32(b));
}

/* Band-limited value noise: linear blend between lattice hashes. `t` advances
 * by `rate` per unit; returns Q14 in [0, 16384]. */
int32_t wc_noise_q14(uint32_t t, uint32_t rate, uint32_t salt);

/* --- RGB565 --------------------------------------------------------------- */

static inline uint16_t wc_rgb565(uint32_t r8, uint32_t g8, uint32_t b8) {
    return (uint16_t)(((r8 & 0xF8U) << 8) | ((g8 & 0xFCU) << 3) | (b8 >> 3));
}

/* Blend a over b with Q8 alpha (0..256). Operates on unpacked 8-bit lanes to
 * stay deterministic and cheap. */
uint16_t wc_mix565(uint16_t b, uint16_t a, uint32_t alpha_q8);

/* --- deterministic idle rig ----------------------------------------------- */

/*
 * The rig turns (keyframe, sample clock) into the animated pose every
 * renderer shares: blink episodes with asymmetric close/open and occasional
 * double blinks, saccade holds with anticipation widening and overshoot,
 * delayed head follow-through with a damped wobble, breathing, and a
 * band-limited flicker channel. All schedules derive from hashed episode
 * indices, so identical clocks always reproduce identical motion.
 */
typedef struct {
    uint8_t mouth_open;
    uint8_t mouth_width;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t mouth_teeth;
    uint8_t lid_l;        /* 0 closed .. 255 open, idle blinks folded in */
    uint8_t lid_r;
    int16_t gaze_x;       /* ~[-256, 256], keyframe look + idle saccades */
    int16_t gaze_y;
    int16_t head_x_q8;    /* head follow-through offset, pixels in Q8 */
    int16_t head_y_q8;
    int16_t brow_q8;      /* ~[-512, 512], up is positive */
    int16_t breath_q14;   /* slow breathing sine, Q14 */
    uint16_t flick_q14;   /* 0..16384 band-limited flicker */
    uint8_t energy;       /* voice energy proxy 0..255 */
    uint8_t speaking;
    uint8_t blink_closure; /* 0..255 how closed the idle blink alone is */
    uint8_t reserved;
} wc_rig_t;

void wc_rig_derive(const wc_keyframe_t *kf, uint32_t clock, wc_rig_t *rig);

/* --- shared face landmarks ------------------------------------------------ */

enum {
    WC_EYE_Y = 47,
    WC_EYE_DX = 33,        /* eye centers at 80 +- 33 */
    WC_MOUTH_Y = 88,
    WC_CENTER_X = 80,
};

/* Normalized ellipse field: q == 1024 on the rim, < 1024 inside.
 * `inv_a2`/`inv_b2` come from wc_inv_sq_q18. Callers must keep |dx| and |dy|
 * inside a few semi-axes (clip to a bounding box first) so the products stay
 * in int32 range. */
static inline int32_t wc_ellipse_q10(
    int32_t dx, int32_t dy, int32_t inv_a2, int32_t inv_b2) {
    return (dx * dx * inv_a2 + dy * dy * inv_b2) >> 8;
}

/* Soft coverage 0..256 from the Q10 field; divisions only happen on the rim
 * band, interiors and exteriors take the early branch. */
static inline uint32_t wc_cov_from_q10(int32_t q, int32_t edge_q10) {
    if (q <= 1024 - edge_q10) {
        return 256;
    }
    if (q >= 1024 + edge_q10) {
        return 0;
    }
    return (uint32_t)(((1024 + edge_q10 - q) << 7) / edge_q10);
}

static inline int32_t wc_inv_sq_q18(int32_t a) {
    int32_t a2 = a * a;
    return a2 > 0 ? (int32_t)((1 << 18) / a2) : (1 << 18);
}

/* internal per-profile entry points; kf is passed alongside the derived rig
 * because some renderers re-derive poses at earlier clocks (phosphor trails,
 * flip-dot refresh history). */
void wc_render_scope_beam(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_flipdot_cascade(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_chladni_sand(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_halftone_press(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_wayang_lamp(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_ferro_pool(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
void wc_render_teletext_sextant(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb);
