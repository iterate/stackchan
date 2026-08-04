#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Standalone procedural-character replacements for legacy robot rows 9..14.
 *
 * The renderers are intentionally independent of face_render.c so native
 * review can reject or iterate an actor before production dispatch changes.
 * Every frame is the exact RGB565 160x120 surface used by firmware and WASM.
 */
enum {
    FACE_ROBOT_REDUX_WIDTH = 160,
    FACE_ROBOT_REDUX_HEIGHT = 120,
    FACE_ROBOT_REDUX_PIXEL_COUNT =
        FACE_ROBOT_REDUX_WIDTH * FACE_ROBOT_REDUX_HEIGHT,
    FACE_ROBOT_REDUX_FRAME_BYTES =
        FACE_ROBOT_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
};

typedef enum {
    FACE_ROBOT_REDUX_ROBOEYES_ALERT = 0,
    FACE_ROBOT_REDUX_ROBOEYES_SOFT,
    FACE_ROBOT_REDUX_M5_AVATAR_CLASSIC,
    FACE_ROBOT_REDUX_M5_AVATAR_MANGA,
    FACE_ROBOT_REDUX_EVE_MINIMAL,
    FACE_ROBOT_REDUX_JIBO_ORB,
    FACE_ROBOT_REDUX_COUNT,
} face_robot_redux_style_t;

typedef enum {
    FACE_ROBOT_REDUX_MOUTH_NONE = 0,
    FACE_ROBOT_REDUX_MOUTH_CAVITY,
    FACE_ROBOT_REDUX_MOUTH_MANGA,
} face_robot_redux_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    bool deliberate_mouthless;
    uint8_t estimated_ops_per_pixel;
} face_robot_redux_info_t;

/*
 * Test-visible resolved anatomy. Anchors remain fixed while gaze, lids,
 * articulation, authored emotion, and coherent head/body offsets move around
 * them. `source` proves that the schema-v2 IR crossed the API intact.
 */
typedef struct {
    face_render_key_t source;
    int16_t eye_x[2];
    int16_t eye_y[2];
    int16_t eye_w[2];
    int16_t eye_h[2];
    int16_t eye_open[2];
    int16_t pupil_x[2];
    int16_t pupil_y[2];
    int16_t pupil_radius[2];
    int16_t brow_y[2];
    int16_t brow_slope[2];
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t mouth_w;
    int16_t mouth_h;
    int16_t mouth_corner[2];
    int16_t face_shift_x;
    int16_t face_shift_y;
    int16_t body_lean_x;
    int16_t body_lean_y;
    int16_t head_roll;
    int16_t speech_bob;
    uint8_t speech_open;
    uint8_t speech_width;
    uint8_t speech_round;
    uint8_t speech_press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t consonant;
    uint8_t stage_expression;
    uint8_t expression_weight;
    uint8_t activity;
    uint8_t speech_phase;
    uint8_t attention;
    uint8_t detail_phase;
    bool speaking;
    bool mouthless;
} face_robot_redux_pose_t;

size_t face_robot_redux_count(void);
const char *face_robot_redux_slug(face_robot_redux_style_t style);
const char *face_robot_redux_name(face_robot_redux_style_t style);
bool face_robot_redux_info(
    face_robot_redux_style_t style,
    face_robot_redux_info_t *info);

/* Exact replacement mapping: legacy profile IDs 9, 10, 11, 12, 13, 14. */
bool face_robot_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_robot_redux_style_t *style);

bool face_robot_redux_resolve(
    face_robot_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_robot_redux_pose_t *pose);

bool face_robot_redux_render(
    face_robot_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_robot_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
