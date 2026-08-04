#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Six complete character faces built around inexpensive, visibly different
 * mouth-animation techniques.  The implementation is stateless, integer-only,
 * heap-free, clipped to 160x120, and deterministic from the complete 40-byte
 * face_render_key_t plus a 16 kHz sample clock.
 *
 * This module is deliberately independent of face_render.c so it can be
 * reviewed and integrated without changing the legacy profile dispatch.
 */
enum {
    FACE_MOUTH_ACTORS_WIDTH = 160,
    FACE_MOUTH_ACTORS_HEIGHT = 120,
    FACE_MOUTH_ACTORS_PIXEL_COUNT =
        FACE_MOUTH_ACTORS_WIDTH * FACE_MOUTH_ACTORS_HEIGHT,
    FACE_MOUTH_ACTORS_FRAME_BYTES =
        FACE_MOUTH_ACTORS_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_MOUTH_ACTORS_CONTEXT_BYTES = 0,
};

typedef enum {
    FACE_MOUTH_ACTOR_PRESTON = 0,
    FACE_MOUTH_ACTOR_JALI,
    FACE_MOUTH_ACTOR_RIBBON,
    FACE_MOUTH_ACTOR_TEETH_TONGUE,
    FACE_MOUTH_ACTOR_LED_VU,
    FACE_MOUTH_ACTOR_ORIGAMI,
    FACE_MOUTH_ACTOR_COUNT,
} face_mouth_actor_profile_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} face_mouth_actor_bounds_t;

typedef struct {
    face_mouth_actor_bounds_t face;
    face_mouth_actor_bounds_t left_eye;
    face_mouth_actor_bounds_t right_eye;
    face_mouth_actor_bounds_t mouth;
} face_mouth_actor_landmarks_t;

/*
 * Resolved performance pose.  `source` is retained byte-for-byte: adapters can
 * inspect or chain every field without losing the original renderer IR.
 * Remaining values are bounded pixel/facial-action controls.
 */
typedef struct {
    face_render_key_t source;
    uint32_t input_signature;
    int16_t head_x;
    int16_t head_y;
    int16_t head_roll;
    int16_t eye_y;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t brow_left;
    int16_t brow_right;
    int16_t brow_slant_left;
    int16_t brow_slant_right;
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t mouth_width;
    int16_t mouth_open;
    int16_t mouth_round;
    int16_t mouth_smile;
    int16_t mouth_corner_left;
    int16_t mouth_corner_right;
    uint8_t eye_left_open;
    uint8_t eye_right_open;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t attention;
    uint8_t arousal;
    uint8_t expression;
    uint8_t speech_phase;
    uint8_t viseme_class;
    uint8_t animation_phase;
} face_mouth_actor_pose_t;

size_t face_mouth_actors_profile_count(void);
const char *face_mouth_actors_profile_slug(
    face_mouth_actor_profile_t profile);
const char *face_mouth_actors_profile_name(
    face_mouth_actor_profile_t profile);

bool face_mouth_actors_resolve(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_mouth_actor_pose_t *pose);

bool face_mouth_actors_render_resolved(
    face_mouth_actor_profile_t profile,
    const face_mouth_actor_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_actor_landmarks_t *landmarks);

bool face_mouth_actors_render_checked(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_actor_landmarks_t *landmarks);

bool face_mouth_actors_render(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
