#pragma once

/*
 * fable_toon_acting — public API.
 *
 * Standalone toon-acting renderer pack for the StackChan face lab. Each
 * profile draws one 160x120 RGB565 frame as a pure function of the 40-byte
 * face_render_key_t (schema v2) plus the 16 kHz sample clock. No heap, no
 * floats, no retained state; byte-identical output across hosts and WASM.
 *
 * The pack is the first contribution built natively against the dense IR:
 * it consumes stage_expression/expression_weight (the authored emotion axis)
 * separately from controls.expression (the conversational activity axis),
 * honours speech_phase, coarticulates viseme_secondary/viseme_blend, and
 * turns head/body channels into a whole-face pose transform with
 * area-conserving squash and stretch.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FTA_FRAME_WIDTH = 160,
    FTA_FRAME_HEIGHT = 120,
    FTA_PIXEL_COUNT = FTA_FRAME_WIDTH * FTA_FRAME_HEIGHT,
    FTA_FRAME_BYTES = FTA_PIXEL_COUNT * (int)sizeof(uint16_t),
    FTA_CONTEXT_BYTES = 0,
};

typedef enum {
    FTA_PROFILE_TOON_BEAN = 0,
    FTA_PROFILE_TOON_INK = 1,
    FTA_PROFILE_TOON_EMBER = 2,
    FTA_PROFILE_COUNT = 3,
} fta_profile_t;

/*
 * Layout-identical to face_render_info_t in firmware-ws/main/face_render.h
 * (static-asserted at 16 bytes) so integration is a struct copy.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t work_width;
    uint16_t work_height;
    uint16_t framebuffer_bytes;
    uint8_t family;
    uint8_t mouth_kind;
    uint8_t flags;
    uint8_t reserved;
    uint16_t estimated_ops_per_pixel;
} fta_info_t;

/*
 * Resolved rig for one frame, exposed so behavior tests can assert acting
 * without rasterizing. Screen geometry is Q4 (16 == one pixel); envelopes
 * are Q8 (256 == 1.0); slopes are Q12 x-offset per Q4 y.
 */
typedef struct {
    int16_t center_x_q4;
    int16_t center_y_q4;
    int16_t half_w_q4;      /* sclera half extents before lid cuts */
    int16_t half_h_q4;
    int16_t lid_top_q4;     /* aperture edges at eye center x */
    int16_t lid_bottom_q4;
    int16_t lid_top_slope_q12;
    int16_t lid_bottom_slope_q12;
    int16_t pupil_x_q4;     /* hard-clamped inside the aperture */
    int16_t pupil_y_q4;
    int16_t pupil_r_q4;
    int16_t iris_r_q4;
    uint8_t openness_q8;    /* resolved aperture / full aperture */
    uint8_t sparkle;        /* 0..255 extra glint energy */
} fta_eye_t;

typedef struct {
    int16_t inner_x_q4;
    int16_t inner_y_q4;
    int16_t outer_x_q4;
    int16_t outer_y_q4;
    int16_t thickness_q4;
} fta_brow_t;

typedef struct {
    int16_t center_x_q4;
    int16_t center_y_q4;    /* jaw hinge anchor: upper lip rest line */
    int16_t half_w_q4;
    int16_t open_q4;        /* interior half height (jaw drop) */
    int16_t corner_left_q4; /* corner y offsets, negative == raised */
    int16_t corner_right_q4;
    int16_t curve_q4;       /* mid-lip bow, negative == smile arch */
    int16_t teeth_q4;       /* upper teeth band height */
    int16_t tongue_q4;      /* tongue hump height from the floor */
    int16_t lip_q4;         /* lip stroke thickness */
    uint8_t round_q8;       /* O-ness: narrows corners, ovals interior */
    uint8_t press_q8;       /* flattens interior toward a pressed line */
} fta_mouth_t;

typedef struct {
    int16_t center_x_q4;
    int16_t center_y_q4;
    int16_t half_w_q4;
    int16_t half_h_q4;
    uint8_t alpha;          /* 0..32 */
} fta_blush_t;

typedef struct {
    /* whole-face pose */
    int16_t origin_x_q4;    /* face center after yaw/lean/bob */
    int16_t origin_y_q4;
    int16_t scale_x_q8;     /* squash and stretch, area conserving */
    int16_t scale_y_q8;
    int16_t shear_q12;      /* roll: x offset per Q4 y from origin */
    /* plate (head silhouette), post transform */
    int16_t plate_left_q4;
    int16_t plate_top_q4;
    int16_t plate_right_q4;
    int16_t plate_bottom_q4;
    int16_t plate_radius_q4;
    fta_eye_t eye[2];       /* 0 == viewer left */
    fta_brow_t brow[2];
    fta_mouth_t mouth;
    fta_blush_t blush[2];
    uint8_t blush_band_alpha;   /* 0..32 cheek band, kept below the eyes */
    int16_t blush_band_y_q4;    /* band center, solver-clamped below lids */
    uint8_t sweat_alpha;        /* 0..32 concern/embarrassed droplet */
    int16_t sweat_y_q4;         /* droplet slide offset */
    uint8_t stage_expression;   /* echoed for tests/debug */
    uint8_t expression_weight;
} fta_rig_t;

size_t fta_profile_count(void);
const char *fta_profile_slug(fta_profile_t profile);
const char *fta_profile_name(fta_profile_t profile);
bool fta_profile_info(fta_profile_t profile, fta_info_t *info);

/*
 * Resolve the acting rig for one frame without drawing. Returns false on
 * NULL arguments or an invalid profile. Deterministic: the same
 * (profile, key, clock) triple always yields the same rig.
 */
bool fta_solve(
    fta_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fta_rig_t *rig);

/*
 * Render one full frame. Writes every pixel (no read-modify-write of the
 * caller buffer). Returns false on NULL arguments, invalid profile, or
 * pixel_capacity < FTA_PIXEL_COUNT.
 */
bool fta_render_frame(
    fta_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

#ifdef __cplusplus
}
#endif
