#pragma once

#include "fmg.h"

/*
 * Shared internals: fixed-point math, deterministic idle motion, the
 * normalized mouth model, the sprite viseme classifier, and clipped
 * software-rasterizer primitives. Everything is integer arithmetic and a
 * pure function of its inputs so frames are byte-identical across native
 * and WebAssembly builds.
 *
 * Q8 means 8 fractional bits unless noted; angles are uint16_t where a
 * full turn is 65536.
 */

#define FMG_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

static inline int32_t fmg_clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* u8 0..255 mapped to Q8 0..256 with exact endpoints. */
static inline int32_t fmg_u8_q8(uint8_t v)
{
    return ((int32_t)v * 256 + 127) / 255;
}

int32_t fmg_sin_q14(uint16_t angle);
int32_t fmg_cos_q14(uint16_t angle);
uint32_t fmg_isqrt(uint32_t v);
uint32_t fmg_hash(uint32_t x);
int32_t fmg_smooth_q8(int32_t t_q8);
uint16_t fmg_blend565(uint16_t dst, uint16_t src, int32_t alpha_q8);

/* ---- deterministic idle motion (pure function of keyframe + clock) ---- */

typedef struct {
    int32_t lid_l_q8;   /* 0..256 eyelid openness multiplier incl. blink */
    int32_t lid_r_q8;
    int32_t gaze_dx_q8; /* idle saccade offset, Q8 pixels */
    int32_t gaze_dy_q8;
    int32_t brow_l_q8;  /* idle brow offset, Q8 pixels, negative = raised */
    int32_t brow_r_q8;
    int32_t breath_q8;  /* whole-face vertical bob, Q8 pixels */
    int32_t sway_q8;    /* slow horizontal sway, Q8 pixels */
} fmg_idle_t;

void fmg_idle_compute(
    const fmg_keyframe_t *kf, uint32_t clock, fmg_idle_t *out);

/* ---- normalized mouth model + sprite viseme classes ---- */

typedef enum {
    FMG_VIS_REST = 0, /* silence, lips relaxed and closed */
    FMG_VIS_AA,       /* open jaw vowel (A/I in the Preston chart) */
    FMG_VIS_EE,       /* wide smiling vowel (E) */
    FMG_VIS_IH,       /* narrow wide shape, teeth showing (I/S/T ish) */
    FMG_VIS_OH,       /* open rounded vowel (O) */
    FMG_VIS_UU,       /* small tight pucker (U/W/Q) */
    FMG_VIS_MBP,      /* bilabial closure (M/B/P) */
    FMG_VIS_SS,       /* teeth together fricative (S/Z/CH/TH) */
    FMG_VIS_FV,       /* lower lip under upper teeth (F/V) */
    FMG_VIS_LN,       /* tongue up behind teeth (L/N/D) */
    FMG_VIS_COUNT,
} fmg_vis_t;

typedef struct {
    int32_t open_q8;  /* jaw openness 0..256 */
    int32_t width_q8; /* corner spread 0..256, ~110 neutral */
    int32_t round_q8; /* lip rounding 0..256 */
    int32_t press_q8; /* bilabial compression 0..256 */
    int32_t teeth_q8; /* teeth visibility 0..256 */
    int32_t jaw_q8;   /* JALI-style jaw axis: open gated by press */
    int32_t lip_q8;   /* JALI-style lip axis: articulation intensity */
    fmg_vis_t vis;    /* nearest discrete viseme for sprite systems */
    bool speaking;
} fmg_mouth_t;

void fmg_mouth_compute(const fmg_keyframe_t *kf, fmg_mouth_t *out);

/* ---- clipped RGB565 primitives ---- */

void fmg_fill(uint16_t *px, uint16_t color);
void fmg_pixel(uint16_t *px, int32_t x, int32_t y, uint16_t color);
void fmg_pixel_blend(
    uint16_t *px, int32_t x, int32_t y, uint16_t color, int32_t alpha_q8);
void fmg_hline(
    uint16_t *px, int32_t x0, int32_t x1, int32_t y, uint16_t color);
void fmg_hline_blend(
    uint16_t *px, int32_t x0, int32_t x1, int32_t y, uint16_t color,
    int32_t alpha_q8);
void fmg_fill_rect(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h,
    uint16_t color);
void fmg_fill_rect_blend(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h,
    uint16_t color, int32_t alpha_q8);
void fmg_fill_ellipse(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint16_t color);
void fmg_fill_ellipse_blend(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint16_t color, int32_t alpha_q8);
void fmg_fill_round_rect(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
    uint16_t color);
void fmg_line(
    uint16_t *px, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t color);

void fmg_vspan(
    uint16_t *px, int32_t x, int32_t y0, int32_t y1, uint16_t color);
void fmg_vspan_blend(
    uint16_t *px, int32_t x, int32_t y0, int32_t y1, uint16_t color,
    int32_t alpha_q8);

/*
 * Even-odd scanline fill of a polygon with Q4 subpixel vertex coordinates
 * (max 24 vertices). Deterministic integer edge interpolation.
 */
enum { FMG_POLY_MAX = 24 };
void fmg_poly_fill(
    uint16_t *px, const int32_t *xy_q4, int count, uint16_t color);

/* Quadratic bezier evaluation in Q4 coordinates, t in Q8 0..256. */
int32_t fmg_qbez_q4(int32_t p0, int32_t p1, int32_t p2, int32_t t_q8);

/* ---- ASCII sprite blitting ---- */

/*
 * Sprites are authored as arrays of equal-length ASCII rows; the blitter
 * maps characters to colors through a small palette table and scales by an
 * integer factor. '.' and ' ' are always transparent.
 */
typedef struct {
    char ch;
    uint16_t color;
} fmg_sprite_pal_t;

typedef struct {
    const char *const *rows;
    int32_t w;
    int32_t h;
} fmg_sprite_t;

void fmg_sprite_blit(
    uint16_t *px, const fmg_sprite_t *sprite, int32_t cx, int32_t cy,
    int32_t scale, const fmg_sprite_pal_t *pal, int pal_count);

/* ---- per-renderer entry points (each draws a complete frame) ---- */

void fmg_render_preston(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_talkie(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_manga(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_dotmatrix(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_jali(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_ribbon(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_origami(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_teeth(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_ledvu(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
void fmg_render_scope(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);
