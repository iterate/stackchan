#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Three deliberately unrelated cyber-face actors replacing the weakest
 * legacy shader entries.  Each actor is a pure integer function of the full
 * 40-byte render key and the 16 kHz sample clock.  There is no retained state,
 * heap allocation, floating point, or off-screen drawing requirement.
 */
enum {
    FACE_CYBER_WILDCARD_WIDTH = 160,
    FACE_CYBER_WILDCARD_HEIGHT = 120,
    FACE_CYBER_WILDCARD_PIXEL_COUNT =
        FACE_CYBER_WILDCARD_WIDTH * FACE_CYBER_WILDCARD_HEIGHT,
    FACE_CYBER_WILDCARD_FRAME_BYTES =
        FACE_CYBER_WILDCARD_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_CYBER_WILDCARD_CONTEXT_BYTES = 0,
};

typedef enum {
    FACE_CYBER_WILDCARD_CHLADNI = 0,
    FACE_CYBER_WILDCARD_TELETEXT,
    FACE_CYBER_WILDCARD_FERROFLUID,
    FACE_CYBER_WILDCARD_COUNT,
} face_cyber_wildcard_profile_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t estimated_ops_per_pixel;
} face_cyber_wildcard_info_t;

/*
 * Exposed bounded pose makes clipping, anchoring, full-IR retention, and
 * temporal behavior independently testable.
 */
typedef struct {
    face_render_key_t source;
    uint32_t input_signature;
    int16_t face_x;
    int16_t face_y;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t eye_y;
    int16_t eye_spacing;
    int16_t eye_width;
    int16_t eye_open_left;
    int16_t eye_open_right;
    int16_t brow_y_left;
    int16_t brow_y_right;
    int16_t brow_slope_left;
    int16_t brow_slope_right;
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t mouth_width;
    int16_t mouth_open;
    int16_t mouth_round;
    int16_t mouth_press;
    int16_t mouth_corner_left;
    int16_t mouth_corner_right;
    int16_t head_roll;
    int16_t body_lean_x;
    int16_t body_lean_y;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t attention;
    uint8_t arousal;
    int8_t valence;
    uint8_t stage_expression;
    uint8_t speech_phase;
    uint8_t activity;
    bool speaking;
} face_cyber_wildcard_pose_t;

size_t face_cyber_wildcard_count(void);
const char *face_cyber_wildcard_slug(
    face_cyber_wildcard_profile_t profile);
const char *face_cyber_wildcard_name(
    face_cyber_wildcard_profile_t profile);
bool face_cyber_wildcard_info(
    face_cyber_wildcard_profile_t profile,
    face_cyber_wildcard_info_t *info);

/* Exact intended replacement mapping: 33, 38, 39. */
bool face_cyber_wildcard_from_legacy_id(
    uint8_t legacy_profile_id,
    face_cyber_wildcard_profile_t *profile);

bool face_cyber_wildcard_resolve(
    face_cyber_wildcard_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_cyber_wildcard_pose_t *pose);

bool face_cyber_wildcard_render(
    face_cyber_wildcard_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_cyber_wildcard_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
