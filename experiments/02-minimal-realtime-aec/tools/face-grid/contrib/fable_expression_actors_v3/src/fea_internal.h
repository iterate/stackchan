#pragma once

/*
 * fable_expression_actors_v3 — internal contracts.
 *
 * Fixed-point conventions:
 *   screen geometry  Q4   (16 == one pixel)
 *   envelopes/gains  Q8   (256 == 1.0)
 *   slopes           Q12  (x offset per Q4 y)
 *   trig             Q14  (16384 == 1.0), full turn == 65536
 * Alpha for blending is 0..32 (32 == opaque). All arithmetic is integer;
 * intermediates widen to 32/64 bits before multiplication so products
 * cannot overflow. No file in src/ may use float or double.
 */

#include <stdbool.h>
#include <stdint.h>

#include "face_keyframe.h"
#include "face_stage.h"
#include "fea.h"

/* ---------------------------------------------------------------- math */

uint32_t fea_hash32(uint32_t value);
uint32_t fea_hash2(uint32_t a, uint32_t b);
int32_t fea_isqrt64(int64_t value);
int16_t fea_sin_q14(uint32_t turn_u16);      /* sin(turn/65536 * 2pi) */
int32_t fea_smoothstep_q8(int32_t t_q8);     /* 0..256 -> 0..256 */
int32_t fea_clamp_i32(int32_t value, int32_t low, int32_t high);

/* Non-monotonic acting response: weight 0..255 -> Q8 (dip -18 .. peak
 * 276 .. settle 256). Drives anticipation-active-settle from stage cue
 * attack ramps. */
int16_t fea_act_curve_q8(uint8_t weight);

/* Blink closure 0..256 for a phase inside FEA_BLINK_TOTAL_SAMPLES.
 * VanderWerf kinematics: 94 ms ease-in close, 50 ms hold, 244 ms
 * cubic ease-out reopen. Phases outside the window return 0. */
enum {
    FEA_BLINK_CLOSE_SAMPLES = 1504,   /* 94 ms at 16 kHz */
    FEA_BLINK_HOLD_SAMPLES = 800,     /* 50 ms */
    FEA_BLINK_OPEN_SAMPLES = 3904,    /* 244 ms */
    FEA_BLINK_TOTAL_SAMPLES = FEA_BLINK_CLOSE_SAMPLES +
        FEA_BLINK_HOLD_SAMPLES + FEA_BLINK_OPEN_SAMPLES,
};
int32_t fea_blink_wave_q8(uint32_t phase_samples);

/* ---------------------------------------------------------------- pose */

/*
 * Resolved performance pose: everything the 40-byte IR + clock says,
 * reduced to renderer-agnostic channels. Solved once per frame and
 * consumed by all five actors.
 */
typedef struct {
    /* identity */
    uint8_t emotion;            /* clamped stage expression */
    int16_t act_q8;             /* acting-curve response of weight */
    uint8_t activity;           /* idle/listening/thinking/speaking */
    uint8_t speech_phase;       /* face_speech_phase_t, clamped */
    uint8_t speaking;           /* flags & SPEAKING */
    uint8_t audio_q8;           /* audio level 0..255 */
    int8_t valence;             /* affect */
    uint8_t arousal;
    uint8_t attention;
    /* eyes (0 == viewer left) */
    int16_t eye_open_q8[2];     /* aperture after squint+blink+emotion */
    int16_t lower_lid_q8[2];    /* smile squint from below, 0..256 */
    int16_t lid_tilt_q8[2];     /* signed upper lid tilt, + inner up */
    int16_t pupil_scale_q8;     /* 256 nominal */
    int16_t gaze_x_q8;          /* resolved gaze -256..256 */
    int16_t gaze_y_q8;
    uint8_t sparkle;            /* glint energy 0..255 */
    uint8_t blink_active;       /* 1 while the blink wave is nonzero */
    /* brows */
    int16_t brow_raise_q8[2];   /* signed, + up */
    int16_t brow_tilt_q8[2];    /* signed, + == outer end up */
    int16_t brow_knit_q8;       /* inner pull, 0..256 */
    /* mouth */
    int16_t jaw_q8;             /* opening 0..320 */
    int16_t mouth_w_q8;         /* width, 256 nominal, 128..320 */
    int16_t round_q8;           /* 0..256 */
    int16_t press_q8;           /* 0..256 */
    int16_t teeth_q8;           /* 0..256 */
    int16_t tongue_q8;          /* 0..256 */
    int16_t corner_q8[2];       /* signed, + raised */
    int16_t curve_q8;           /* mid-lip bow, + smile arch */
    /* whole-face pose */
    int16_t ox_q4;              /* origin offset from head pose/lean/bob */
    int16_t oy_q4;
    int16_t shear_q12;          /* roll shear */
    int16_t scale_y_q8;         /* squash & stretch; scale_x derived */
    int16_t scale_x_q8;
    int16_t breath_q8;          /* -256..256 breathing wave */
    int16_t cheek_q8;           /* blush 0..256 */
    uint8_t extended;           /* schema gate: extended bytes trusted */
} fea_pose_t;

void fea_solve(
    const face_render_key_t *key, uint32_t sample_clock, fea_pose_t *pose);

/* ---------------------------------------------------------------- draw */

typedef struct {
    uint16_t *pixels;           /* caller frame, FEA_PIXEL_COUNT */
} fea_canvas_t;

uint16_t fea_blend565(uint16_t bg, uint16_t fg, uint32_t alpha_0_32);
void fea_fill(fea_canvas_t *canvas, uint16_t color);
void fea_fill_rect(
    fea_canvas_t *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t color, uint32_t alpha);
/* AA horizontal span with Q4 endpoints (half-open [x0,x1)). */
void fea_hspan_q4(
    fea_canvas_t *canvas, int32_t y, int32_t x0_q4, int32_t x1_q4,
    uint16_t color, uint32_t alpha);
/* Filled axis-aligned ellipse, Q4 center and radii, AA edge. */
void fea_ellipse_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t rx_q4, int32_t ry_q4, uint16_t color, uint32_t alpha);
/* Rounded rectangle with one corner radius, Q4, AA edge. */
void fea_roundrect_q4(
    fea_canvas_t *canvas, int32_t left_q4, int32_t top_q4,
    int32_t right_q4, int32_t bottom_q4, int32_t radius_q4,
    uint16_t color, uint32_t alpha);
/* Thick capsule stroke from (x0,y0) to (x1,y1), Q4, AA. */
void fea_stroke_q4(
    fea_canvas_t *canvas, int32_t x0_q4, int32_t y0_q4,
    int32_t x1_q4, int32_t y1_q4, int32_t thickness_q4,
    uint16_t color, uint32_t alpha);
/* Filled triangle, Q4 vertices, AA on the two non-flat edges. */
void fea_triangle_q4(
    fea_canvas_t *canvas, int32_t x0_q4, int32_t y0_q4,
    int32_t x1_q4, int32_t y1_q4, int32_t x2_q4, int32_t y2_q4,
    uint16_t color, uint32_t alpha);
/* Radial glow: opaque core radius, halo fading to zero at halo radius. */
void fea_glow_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t core_q4, int32_t halo_q4, uint16_t color,
    uint32_t core_alpha);
/* Ring (annulus) centered stroke at radius with thickness, Q4, AA. */
void fea_ring_q4(
    fea_canvas_t *canvas, int32_t cx_q4, int32_t cy_q4,
    int32_t radius_q4, int32_t thickness_q4, uint16_t color,
    uint32_t alpha);

/*
 * Mouth built from two quadratic lip edges sharing corner endpoints.
 * Corners are the parents: every other mouth feature (interior, teeth,
 * tongue, lip strokes) derives from the same two curves, so corner
 * offsets move the whole assembly coherently.
 */
typedef struct {
    int16_t left_x_q4, left_y_q4;      /* corner endpoints (parents) */
    int16_t right_x_q4, right_y_q4;
    int16_t top_ctrl_y_q4;             /* control point y at center x */
    int16_t bot_ctrl_y_q4;
    int16_t lip_q4;                    /* lip stroke thickness, 0 none */
    uint16_t lip_color;
    uint16_t fill_color;               /* interior */
    uint16_t teeth_color;
    uint16_t tongue_color;
    int16_t teeth_q8;                  /* teeth band presence */
    int16_t tongue_q8;                 /* tongue hump presence */
    uint8_t alpha;                     /* 0..32 overall */
} fea_lipmouth_t;

void fea_lipmouth_draw(fea_canvas_t *canvas, const fea_lipmouth_t *mouth);

/* RGB565 helper */
#define FEA_RGB(r, g, b) \
    ((uint16_t)((((uint32_t)(r) >> 3) << 11) | \
                (((uint32_t)(g) >> 2) << 5) | ((uint32_t)(b) >> 3)))

/* Safe area: nothing may be drawn outside this pixel box (quality gates
 * measure a 4 px border; we keep a wider margin for pose headroom). */
enum {
    FEA_SAFE_LEFT = 6,
    FEA_SAFE_TOP = 5,
    FEA_SAFE_RIGHT = FEA_FRAME_WIDTH - 6,
    FEA_SAFE_BOTTOM = FEA_FRAME_HEIGHT - 5,
};

/* --------------------------------------------------------------- actors */

typedef void (*fea_actor_render_fn)(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
typedef void (*fea_actor_probe_fn)(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);

void fea_mochi_render(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
void fea_mochi_probe(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);
void fea_karakuri_render(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
void fea_karakuri_probe(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);
void fea_sticker_render(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
void fea_sticker_probe(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);
void fea_wisp_render(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
void fea_wisp_probe(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);
void fea_scope_render(
    const fea_pose_t *pose, uint32_t sample_clock, fea_canvas_t *canvas);
void fea_scope_probe(
    const fea_pose_t *pose, uint32_t sample_clock, fea_probe_t *probe);

/* ------------------------------------------------- placement helper */

typedef struct {
    int32_t x_q4;
    int32_t y_q4;
} fea_pt_t;

/*
 * Place a feature point offset (dx,dy) from the actor's face anchor
 * (cx,cy) through the whole-face pose: squash & stretch about the
 * anchor, then translation, then roll shear. Every actor routes all
 * feature geometry through this so head/body channels move the whole
 * face coherently.
 */
static inline fea_pt_t fea_place(
    const fea_pose_t *pose, int32_t cx_q4, int32_t cy_q4,
    int32_t dx_q4, int32_t dy_q4)
{
    fea_pt_t out;
    out.y_q4 = cy_q4 + ((dy_q4 * pose->scale_y_q8) >> 8) + pose->oy_q4;
    out.x_q4 = cx_q4 + ((dx_q4 * pose->scale_x_q8) >> 8) + pose->ox_q4 +
        (((out.y_q4 - cy_q4) * pose->shear_q12) >> 12);
    return out;
}
