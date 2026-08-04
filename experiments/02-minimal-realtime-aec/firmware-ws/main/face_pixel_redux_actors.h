#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Five original, allocation-free pixel-character rigs replacing the weakest
 * legacy adventure-game rows.  Artwork is procedural at an 80x60 logical
 * resolution and presented with exact 2x nearest-neighbour pixels.
 */
enum {
    FACE_PIXEL_REDUX_WIDTH = 160,
    FACE_PIXEL_REDUX_HEIGHT = 120,
    FACE_PIXEL_REDUX_PIXEL_COUNT =
        FACE_PIXEL_REDUX_WIDTH * FACE_PIXEL_REDUX_HEIGHT,
    FACE_PIXEL_REDUX_FRAME_BYTES =
        FACE_PIXEL_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_PIXEL_REDUX_LOGICAL_WIDTH = 80,
    FACE_PIXEL_REDUX_LOGICAL_HEIGHT = 60,
    FACE_PIXEL_REDUX_SCALE = 2,
};

typedef enum {
    FACE_PIXEL_REDUX_EGA_QUEST = 0,
    FACE_PIXEL_REDUX_VGA_ELDER,
    FACE_PIXEL_REDUX_TALKIE_CLOSEUP,
    FACE_PIXEL_REDUX_PIXEL_AUTOMATON,
    FACE_PIXEL_REDUX_POCKET_RPG,
    FACE_PIXEL_REDUX_ACTOR_COUNT,
} face_pixel_redux_actor_t;

typedef enum {
    FACE_PIXEL_REDUX_MOUTH_CELS = 0,
    FACE_PIXEL_REDUX_MOUTH_SHADED,
    FACE_PIXEL_REDUX_MOUTH_CINEMATIC,
    FACE_PIXEL_REDUX_MOUTH_LED,
    FACE_PIXEL_REDUX_MOUTH_CHIBI,
} face_pixel_redux_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    uint8_t logical_width;
    uint8_t logical_height;
    uint8_t palette_size;
    uint8_t estimated_ops_per_pixel;
} face_pixel_redux_actor_info_t;

/*
 * Resolved logical-pixel landmarks are exposed for sanitizer and topology
 * tests. `source` is retained byte-for-byte so captures can verify the exact
 * schema-v2 IR crossing the renderer boundary.
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
    int16_t brow_outer_y[2];
    int16_t brow_inner_y[2];
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t mouth_w;
    int16_t mouth_h;
    int16_t mouth_corner_y[2];
    int16_t body_lean_x;
    int16_t body_lean_y;
    int16_t head_yaw;
    int16_t head_pitch;
    int16_t head_roll;
    int16_t speech_pulse;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t attention;
    uint8_t stage_expression;
    uint8_t expression_weight;
    uint8_t activity;
    uint8_t speech_phase;
    uint8_t phoneme_shape;
    uint8_t emotion_icon;
    bool speaking;
} face_pixel_redux_pose_t;

size_t face_pixel_redux_actor_count(void);
const char *face_pixel_redux_actor_slug(
    face_pixel_redux_actor_t actor);
const char *face_pixel_redux_actor_name(
    face_pixel_redux_actor_t actor);
bool face_pixel_redux_actor_info(
    face_pixel_redux_actor_t actor,
    face_pixel_redux_actor_info_t *info);

/* Exact legacy dispatch mapping: 0, 1, 2, 3 and 5. */
bool face_pixel_redux_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_pixel_redux_actor_t *actor);

bool face_pixel_redux_actor_resolve(
    face_pixel_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_pixel_redux_pose_t *pose);

bool face_pixel_redux_actor_render(
    face_pixel_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_pixel_redux_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
