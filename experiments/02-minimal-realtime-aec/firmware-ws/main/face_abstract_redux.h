#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Five deliberately different replacements for the old shader family.
 *
 * The legacy implementations all put the same black oval face over a
 * different procedural background.  These actors instead have independent
 * silhouettes, eye systems, and speech mechanisms while retaining the
 * original profile ids and the same allocation-free 160x120 RGB565 ABI.
 */
enum {
    FACE_ABSTRACT_REDUX_WIDTH = 160,
    FACE_ABSTRACT_REDUX_HEIGHT = 120,
    FACE_ABSTRACT_REDUX_PIXEL_COUNT =
        FACE_ABSTRACT_REDUX_WIDTH * FACE_ABSTRACT_REDUX_HEIGHT,
};

typedef enum {
    FACE_ABSTRACT_REDUX_NEON_RIBBON = 0,
    FACE_ABSTRACT_REDUX_LIQUID_FAMILIAR,
    FACE_ABSTRACT_REDUX_CRT_PUPPET,
    FACE_ABSTRACT_REDUX_VOICE_ORBIT,
    FACE_ABSTRACT_REDUX_EDGE_SENTINEL,
    FACE_ABSTRACT_REDUX_COUNT,
} face_abstract_redux_style_t;

typedef enum {
    FACE_ABSTRACT_REDUX_MOUTH_NONE = 0,
    FACE_ABSTRACT_REDUX_MOUTH_RIBBON,
    FACE_ABSTRACT_REDUX_MOUTH_LIQUID,
    FACE_ABSTRACT_REDUX_MOUTH_MATRIX,
    FACE_ABSTRACT_REDUX_MOUTH_LINE,
} face_abstract_redux_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    uint8_t estimated_ops_per_pixel;
} face_abstract_redux_info_t;

typedef struct {
    face_render_key_t source;
    int16_t face_center_x;
    int16_t face_center_y;
    int16_t eye_center_x[2];
    int16_t eye_center_y[2];
    int16_t mouth_center_x;
    int16_t mouth_center_y;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t eye_open[2];
    int16_t brow_y[2];
    int16_t brow_slope[2];
    int16_t mouth_width;
    int16_t mouth_height;
    int16_t mouth_corner[2];
    int16_t lean_x;
    int16_t lean_y;
    int16_t speech_pulse;
    uint32_t input_signature;
    uint8_t stage_expression;
    uint8_t expression_weight;
    uint8_t speech_open;
    uint8_t speech_width;
    uint8_t speech_round;
    uint8_t speech_press;
    uint8_t speech_drive_q8;
    uint8_t anticipation_q8;
    uint8_t settle_q8;
    uint8_t speech_envelope_q8;
    uint8_t speech_phase;
    bool speaking;
} face_abstract_redux_pose_t;

size_t face_abstract_redux_count(void);
const char *face_abstract_redux_slug(
    face_abstract_redux_style_t style);
const char *face_abstract_redux_name(
    face_abstract_redux_style_t style);
bool face_abstract_redux_info(
    face_abstract_redux_style_t style,
    face_abstract_redux_info_t *info);
bool face_abstract_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_abstract_redux_style_t *style);
bool face_abstract_redux_resolve(
    face_abstract_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_abstract_redux_pose_t *pose);
bool face_abstract_redux_render(
    face_abstract_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
bool face_abstract_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
