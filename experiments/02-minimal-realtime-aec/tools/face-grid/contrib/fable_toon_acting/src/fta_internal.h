#pragma once

/*
 * fable_toon_acting — internal contract.
 *
 * Fixed-point conventions (matching the robot-eyes family):
 *   Q4  — screen geometry, 16 == one pixel
 *   Q8  — envelopes/scales, 256 == 1.0
 *   Q12 — slopes, x offset in Q4 per one Q4 of y
 * Alpha is 0..32 (32 == opaque) so blends are shifts, not /255.
 *
 * Everything below is pure: no statics are ever written, no allocation,
 * integer arithmetic only. The 16 kHz sample clock is the only source of
 * autonomous motion; all schedules hash the clock so any frame can be
 * rendered in isolation, in any order, on any host.
 */

#include "fta.h"

enum {
    FTA_Q4 = 16,
    FTA_Q8 = 256,
    FTA_SAMPLE_RATE = 16000,
    /* canonical 30 fps step: 16000 / 30 =~ 533 samples per frame */
    FTA_SAMPLES_PER_FRAME = 533,
};

/* ---- palette ---------------------------------------------------------- */

typedef struct {
    uint16_t background;
    uint16_t plate;
    uint16_t plate_outline;
    uint16_t sclera;
    uint16_t iris;
    uint16_t pupil;
    uint16_t glint;
    uint16_t lid;               /* lid fill (plate colour hides the cut) */
    uint16_t lash;              /* lid edge line */
    uint16_t brow;
    uint16_t mouth_interior;
    uint16_t lip;
    uint16_t teeth;
    uint16_t tongue;
    uint16_t blush;
    uint16_t sweat;
} fta_palette_t;

/* ---- style ------------------------------------------------------------ */

typedef enum {
    FTA_LOOK_FILLED = 0,    /* filled plate + features (toon_bean) */
    FTA_LOOK_INK = 1,       /* line art: outlines on paper (toon_ink) */
    FTA_LOOK_EMBER = 2,     /* emissive features on dark glass (toon_ember) */
} fta_look_t;

typedef struct {
    const char *slug;
    const char *name;
    uint32_t salt;              /* desyncs blinks/saccades between tiles */
    fta_look_t look;
    fta_palette_t palette;
    /* proportions, Q4 unless noted */
    int16_t plate_half_w_q4;
    int16_t plate_half_h_q4;
    int16_t plate_radius_q4;
    int16_t eye_offset_x_q4;    /* eye center from face center */
    int16_t eye_offset_y_q4;
    int16_t eye_half_w_q4;
    int16_t eye_half_h_q4;
    int16_t iris_r_q4;
    int16_t pupil_r_q4;
    int16_t brow_gap_q4;        /* brow rest height above eye top */
    int16_t brow_half_w_q4;
    int16_t brow_thickness_q4;
    int16_t mouth_offset_y_q4;  /* mouth anchor below face center */
    int16_t mouth_half_w_q4;
    int16_t mouth_max_open_q4;
    int16_t lip_thickness_q4;
    int16_t blush_offset_x_q4;
    int16_t blush_offset_y_q4;
    int16_t blush_half_w_q4;
    int16_t blush_half_h_q4;
    /* behavior character */
    uint8_t gaze_travel_q8;     /* pupil travel gain */
    uint8_t motion_gain_q8;     /* head/body transform gain */
    uint8_t accent_gain_q8;     /* expression accent gain */
    uint16_t ops_estimate;
} fta_style_t;

const fta_style_t *fta_style_for(fta_profile_t profile);

/* ---- per-expression accents ------------------------------------------ */

/*
 * What the generic stage channels cannot separate at 160x120, the accent
 * table can: each of the eleven authored emotions gets a hand-tuned set of
 * small integer offsets in the spirit of the visual-review guidance
 * ("emotion table = small integer offsets ... with per-emotion hand
 * authoring — no full-face warp").
 */
typedef struct {
    int8_t lid_top_q8;      /* + closes the upper lid, - widens beyond rest */
    int8_t lid_bottom_q8;   /* + raises the lower lid (smile squint) */
    int8_t lid_tilt_q8;     /* + outer-down (gentle), - inner-down (stern) */
    int8_t pupil_scale_q8;  /* signed pupil size bias */
    int8_t gaze_x_q4;       /* authored gaze bias, applied x3 */
    int8_t gaze_y_q4;
    int8_t brow_lift_q4;    /* whole-brow raise, applied x4 */
    int8_t mouth_curve_q4;  /* extra bow, applied x4: + deepens the smile */
    int8_t mouth_width_q8;  /* mouth width bias */
    int8_t jaw_q8;          /* mouth open bias, applied x2.5 */
    int8_t stretch_q8;      /* posture: + tall/alert, - settled/slumped */
    uint8_t blush;          /* 0..32 cheek alpha floor */
    uint8_t blush_band;     /* 0..32 nose-bridge band alpha */
    uint8_t sparkle;        /* 0..255 glint energy */
    uint8_t sweat;          /* 0..32 droplet alpha */
    uint8_t mouth_round_q8; /* rounding override (surprise O) */
    uint8_t breath_q8;      /* breathing depth gain */
    uint8_t bob_q8;         /* idle bounce gain */
} fta_accent_t;

const fta_accent_t *fta_accent_for(uint8_t stage_expression);

/* ---- math ------------------------------------------------------------- */

int32_t fta_clamp_i32(int32_t value, int32_t low, int32_t high);
int32_t fta_min_i32(int32_t a, int32_t b);
int32_t fta_max_i32(int32_t a, int32_t b);
int32_t fta_abs_i32(int32_t value);
uint32_t fta_hash_u32(uint32_t value);
/* deterministic per-slot noise: hash of (slot, salt, lane) -> 0..255 */
uint8_t fta_noise_u8(uint32_t slot, uint32_t salt, uint32_t lane);
/* smoothstep on 0..255 */
uint8_t fta_smooth_u8(uint8_t value);
/* C1-continuous unit wave from a phase in Q16 turns: -256..256 (Q8) */
int32_t fta_wave_q8(uint32_t phase_q16);
/*
 * Acting response curve: maps a cue weight 0..255 to a Q8 response with a
 * small anticipation dip, ~9% overshoot near w = 0.8, settling at 256.
 * Because stage cue attacks ramp the weight through this curve in time,
 * the face dips before it commits and settles after it lands with no
 * renderer state.
 */
int32_t fta_acting_q8(uint8_t weight);
/* monotone variant for magnitudes that must never go negative */
int32_t fta_acting_mag_q8(uint8_t weight);

/* ---- solver ----------------------------------------------------------- */

void fta_solve_rig(
    const fta_style_t *style,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fta_rig_t *rig);

/* ---- rasterizer ------------------------------------------------------- */

typedef struct {
    uint16_t *pixels;   /* 160x120, caller owned */
} fta_canvas_t;

uint16_t fta_blend565(uint16_t background, uint16_t foreground, uint8_t alpha32);
void fta_canvas_fill(fta_canvas_t *canvas, uint16_t color);
void fta_fill_rect(
    fta_canvas_t *canvas,
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    uint16_t color);
void fta_blend_rect(
    fta_canvas_t *canvas,
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    uint16_t color, uint8_t alpha32);
/* axis-aligned anti-aliased ellipse, Q4 center/radii, alpha 0..32 */
void fta_fill_ellipse_q4(
    fta_canvas_t *canvas,
    int32_t center_x_q4, int32_t center_y_q4,
    int32_t radius_x_q4, int32_t radius_y_q4,
    uint16_t color, uint8_t alpha32);
/* ellipse ring (outline) with Q4 stroke thickness */
void fta_ring_ellipse_q4(
    fta_canvas_t *canvas,
    int32_t center_x_q4, int32_t center_y_q4,
    int32_t radius_x_q4, int32_t radius_y_q4,
    int32_t stroke_q4, uint16_t color, uint8_t alpha32);
/* anti-aliased thick segment (capsule) in Q4 coordinates */
void fta_fill_capsule_q4(
    fta_canvas_t *canvas,
    int32_t x0_q4, int32_t y0_q4, int32_t x1_q4, int32_t y1_q4,
    int32_t radius_q4, uint16_t color, uint8_t alpha32);
/* anti-aliased rounded rectangle, Q4 edges/radius */
void fta_fill_round_rect_q4(
    fta_canvas_t *canvas,
    int32_t left_q4, int32_t top_q4, int32_t right_q4, int32_t bottom_q4,
    int32_t radius_q4, uint16_t color, uint8_t alpha32);
void fta_ring_round_rect_q4(
    fta_canvas_t *canvas,
    int32_t left_q4, int32_t top_q4, int32_t right_q4, int32_t bottom_q4,
    int32_t radius_q4, int32_t stroke_q4, uint16_t color, uint8_t alpha32);

/* full frame composition for one style */
void fta_draw_rig(
    fta_canvas_t *canvas,
    const fta_style_t *style,
    const fta_rig_t *rig);
