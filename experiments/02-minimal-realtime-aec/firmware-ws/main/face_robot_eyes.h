#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Embedded-safe Cozmo/Vector-style performance engine.
 *
 * This is the production adapter around the independently implemented
 * Fable robot-eyes behavior solver and fixed-point rasterizer. It consumes
 * the complete 40-byte renderer IR, keeping conversational activity,
 * authored emotion, PCM articulation, facial actions, and procedural motion
 * as separate layers.
 *
 * The implementation is stateless, integer-only, heap-free and deterministic
 * from (profile, render key, 16 kHz sample clock). The same functions compile
 * natively, under Emscripten, and in ESP-IDF.
 */
enum {
    FACE_ROBOT_EYES_WIDTH = 160,
    FACE_ROBOT_EYES_HEIGHT = 120,
    FACE_ROBOT_EYES_PIXEL_COUNT =
        FACE_ROBOT_EYES_WIDTH * FACE_ROBOT_EYES_HEIGHT,
    FACE_ROBOT_EYES_FRAME_BYTES =
        FACE_ROBOT_EYES_PIXEL_COUNT * (int)sizeof(uint16_t),
};

typedef enum {
    FACE_ROBOT_EYES_VECTOR_ROUNDED = 0,
    FACE_ROBOT_EYES_COZMO_CUBIC,
    FACE_ROBOT_EYES_BROW_DIALOGUE,
    FACE_ROBOT_EYES_SLEEP_WAKE,
    FACE_ROBOT_EYES_IRIS_PARALLAX,
    FACE_ROBOT_EYES_CAT_OPTICS,
    FACE_ROBOT_EYES_M5_MANGA,
    FACE_ROBOT_EYES_PROFILE_COUNT,
} face_robot_eyes_profile_t;

/*
 * Resolved performance pose. Q8 values use 256 as unity. The source IR is
 * retained byte-for-byte for downstream renderer chaining and diagnostics;
 * resolved_controls adds expression-safe mouth shaping without altering the
 * caller's PCM/viseme record.
 */
typedef struct {
    face_render_key_t source;
    face_keyframe_t resolved_controls;
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    int32_t lid_gaze_y_q8;
    int32_t openness_q8[2];
    int32_t brow_raise_q8[2];
    int32_t brow_tilt_q8[2];
    int32_t upper_lid_slope_q12[2];
    int32_t upper_lid_bend_q12[2];
    int32_t lower_lid_slope_q12[2];
    int32_t lower_lid_bend_q12[2];
    int32_t scale_x_q8;
    int32_t scale_y_q8;
    int32_t breath_y_q8;
    int32_t tilt_mdeg;
    int32_t pupil_q8;
    int32_t arousal_q8;
    uint8_t activity;
    uint8_t stage_expression;
    uint8_t act_id;
    uint8_t saccade_active;
} face_robot_eyes_pose_t;

size_t face_robot_eyes_profile_count(void);
const char *face_robot_eyes_profile_slug(face_robot_eyes_profile_t profile);
const char *face_robot_eyes_profile_name(face_robot_eyes_profile_t profile);

/*
 * Resolve autonomous gaze/blink behavior, all eleven authored expressions,
 * and the remaining facial/head/body action bytes without drawing.
 */
bool face_robot_eyes_resolve(
    face_robot_eyes_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_robot_eyes_pose_t *pose);

/*
 * Render a previously resolved pose. This is useful for acceptance tests and
 * for renderers that want to share one performance solve.
 */
bool face_robot_eyes_render_resolved(
    face_robot_eyes_profile_t profile,
    const face_robot_eyes_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity);

/* Resolve and render one 160x120 RGB565 frame. */
bool face_robot_eyes_render(
    face_robot_eyes_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
