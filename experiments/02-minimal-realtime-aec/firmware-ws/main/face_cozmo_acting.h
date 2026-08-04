#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * face_cozmo_acting — a self-contained Cozmo/Vector-lineage acting renderer.
 *
 * The module is a fresh implementation (no code shared with the existing
 * face_robot_eyes contribution, no GPL-derived code) built around one idea:
 * the display is a stage. A stateless *director* turns the 16 kHz sample
 * clock into a deterministic performance (blinks, saccades, breath, idle
 * acts, speech emphasis); a *stager* folds the complete 40-byte
 * face_render_key_t on top (activity posture, the 11 authored stage
 * emotions, dense facial actions, gaze, head/body controls, visemes); a
 * *painter* rasterizes self-luminous eyes with an interior light field,
 * lid seams, and screen-space bloom, entirely in integer arithmetic.
 *
 * Contract, identical to the firmware's other renderer families:
 *   - one 160x120 RGB565 frame per call into a caller-owned buffer;
 *   - pure function of (profile, render key, sample clock); no heap, no
 *     retained state, no floating point, no I/O;
 *   - the deterministic clock alone drives autonomous motion, so the same
 *     inputs are pixel-identical on host, WASM, and device;
 *   - every drawn pixel stays inside the frame with a guaranteed dark
 *     margin: an analytic containment governor rescales the pose before
 *     rasterization, so no eyelid, glow, or mouth is ever clipped.
 */
enum {
    FACE_COZMO_ACTING_WIDTH = 160,
    FACE_COZMO_ACTING_HEIGHT = 120,
    FACE_COZMO_ACTING_PIXEL_COUNT =
        FACE_COZMO_ACTING_WIDTH * FACE_COZMO_ACTING_HEIGHT,
    FACE_COZMO_ACTING_FRAME_BYTES =
        FACE_COZMO_ACTING_PIXEL_COUNT * (int)sizeof(uint16_t),
    /* No pixel other than the background is ever painted inside this
     * many pixels of the frame edge. */
    FACE_COZMO_ACTING_SAFE_MARGIN = 2,
};

typedef enum {
    /* Flagship: large cyan stage eyes, hotspot light field, bloom. */
    FACE_COZMO_ACTING_STAGE = 0,
    /* Warm amber duo with heavier lids and calmer direction. */
    FACE_COZMO_ACTING_EMBER,
    /* Mint profile with a viseme-articulated mouth bar. */
    FACE_COZMO_ACTING_CHATTER,
    /* High-energy violet pair, quick saccades, playful acts. */
    FACE_COZMO_ACTING_NOVA,
    FACE_COZMO_ACTING_PROFILE_COUNT,
} face_cozmo_acting_profile_t;

/*
 * Fully resolved stage pose for one instant, exposed so tests can assert
 * on acting decisions without decoding pixels. Q8 uses 256 as unity;
 * screen-space values are Q4 (1/16 px). Eye index 0 is viewer-left.
 */
typedef struct {
    /* Final per-eye placement after the containment governor. */
    int32_t eye_cx_q4[2];
    int32_t eye_cy_q4[2];
    int32_t eye_hw_q4[2];
    int32_t eye_hh_q4[2];
    /* Corner radii, TL/TR/BL/BR in the eye's local (mirrored) frame. */
    int32_t corner_q4[2][4];
    /* Top-versus-bottom width taper, Q8 of half width, +narrows the top. */
    int32_t taper_q8[2];
    /* Whole-face rotation in milli-degrees (both eyes share it). */
    int32_t roll_mdeg;
    /* Aperture 0..256 after blinks, squints, authored lids. */
    int32_t aperture_q8[2];
    /* Upper/lower lid pose: drop/raise 0..256, slope/bend Q12. */
    int32_t upper_drop_q8[2];
    int32_t upper_slope_q12[2];
    int32_t upper_bend_q12[2];
    int32_t lower_raise_q8[2];
    int32_t lower_slope_q12[2];
    int32_t lower_bend_q12[2];
    /* Gaze state driving the interior light field, -256..256. */
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    /* Hotspot radius/intensity, Q8 relative to eye size / 0..256. */
    int32_t hotspot_size_q8;
    int32_t hotspot_gain_q8;
    /* Emissive gain 0..320 (256 = profile baseline) and warm/cool grade
     * -256..256 applied to the profile palette. */
    int32_t emissive_q8;
    int32_t grade_q8;
    /* Bloom radius in Q4 pixels beyond the silhouette. */
    int32_t bloom_q4;
    /* Mouth pose (FACE_COZMO_ACTING_CHATTER only): center, half size,
     * curve (+smile), openness, all Q4/Q8. */
    int32_t mouth_cx_q4;
    int32_t mouth_cy_q4;
    int32_t mouth_hw_q4;
    int32_t mouth_open_q4;
    int32_t mouth_curve_q8;
    int32_t mouth_round_q8;
    int32_t mouth_teeth_q8;
    /* Bookkeeping for tests. */
    uint8_t activity;
    uint8_t stage_expression;
    uint8_t blink_state;   /* 0 open, 1 closing, 2 closed, 3 reopening */
    uint8_t act_id;        /* FACE_COZMO_ACT_* */
    uint8_t saccade_active;
    uint8_t governor_engaged; /* containment clamp had to intervene */
} face_cozmo_acting_pose_t;

/* Idle acts the deterministic director can schedule (exposed for tests). */
enum {
    FACE_COZMO_ACT_NONE = 0,
    FACE_COZMO_ACT_GLANCE = 1,      /* quick look aside and back */
    FACE_COZMO_ACT_THINK_UP = 2,    /* long upward drift */
    FACE_COZMO_ACT_SLOW_BLINK = 3,  /* affectionate half-speed blink */
    FACE_COZMO_ACT_SQUINT_HOLD = 4, /* brief scrutinising squint */
    FACE_COZMO_ACT_WIGGLE = 5,      /* playful 2-step roll wiggle */
    FACE_COZMO_ACT_REFOCUS = 6,     /* drift out then snap back */
};

size_t face_cozmo_acting_profile_count(void);
const char *face_cozmo_acting_profile_slug(
    face_cozmo_acting_profile_t profile);
const char *face_cozmo_acting_profile_name(
    face_cozmo_acting_profile_t profile);

/*
 * Resolve the complete acting pose for one instant without rasterizing.
 * Consumes every semantic field of the 40-byte IR: activity, authored
 * stage emotion and weight, dense lid/brow/cheek/corner actions, affect,
 * attention, gaze, head yaw/pitch/roll, body lean, audio level, viseme
 * vocabulary/primary/secondary/blend, and speech phase. Returns false on
 * NULL arguments or an unknown profile.
 */
bool face_cozmo_acting_resolve(
    face_cozmo_acting_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_cozmo_acting_pose_t *pose);

/* Rasterize a previously resolved pose. */
bool face_cozmo_acting_render_resolved(
    face_cozmo_acting_profile_t profile,
    const face_cozmo_acting_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity);

/* Resolve and render one 160x120 RGB565 frame. */
bool face_cozmo_acting_render(
    face_cozmo_acting_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
