#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Three original, allocation-free sprite-performance rigs replacing legacy
 * profile IDs 58, 59 and 60. Artwork and indexed facial cels are authored on
 * an 80x60 logical grid and enlarged with exact 2x nearest-neighbour pixels.
 */
enum {
    FACE_SPRITE_REDUX_WIDTH = 160,
    FACE_SPRITE_REDUX_HEIGHT = 120,
    FACE_SPRITE_REDUX_PIXEL_COUNT =
        FACE_SPRITE_REDUX_WIDTH * FACE_SPRITE_REDUX_HEIGHT,
    FACE_SPRITE_REDUX_FRAME_BYTES =
        FACE_SPRITE_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_SPRITE_REDUX_LOGICAL_WIDTH = 80,
    FACE_SPRITE_REDUX_LOGICAL_HEIGHT = 60,
    FACE_SPRITE_REDUX_SCALE = 2,
};

typedef enum {
    FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC = 0,
    FACE_SPRITE_REDUX_JRPG_STORM_FAMILIAR,
    FACE_SPRITE_REDUX_HANDHELD_FOREST_PET,
    FACE_SPRITE_REDUX_ACTOR_COUNT,
} face_sprite_redux_actor_t;

typedef enum {
    FACE_SPRITE_REDUX_MOUTH_EGA_CELS = 0,
    FACE_SPRITE_REDUX_MOUTH_VGA_SHADED,
    FACE_SPRITE_REDUX_MOUTH_ARCADE_CELS,
} face_sprite_redux_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    uint8_t logical_width;
    uint8_t logical_height;
    uint8_t palette_size;
    uint8_t estimated_ops_per_pixel;
} face_sprite_redux_actor_info_t;

/*
 * Resolved logical-pixel landmarks are exposed to topology and temporal
 * tests. `source` is retained byte-for-byte to audit the 40-byte IR boundary.
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
    int16_t silhouette_lift[2];
    int16_t silhouette_tilt[2];
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
    uint8_t mouth_cel;
    uint8_t viseme_index;
    uint8_t anticipation_q8;
    uint8_t settle_q8;
    bool speaking;
} face_sprite_redux_pose_t;

size_t face_sprite_redux_actor_count(void);
const char *face_sprite_redux_actor_slug(
    face_sprite_redux_actor_t actor);
const char *face_sprite_redux_actor_name(
    face_sprite_redux_actor_t actor);
bool face_sprite_redux_actor_info(
    face_sprite_redux_actor_t actor,
    face_sprite_redux_actor_info_t *info);

/* Exact production legacy dispatch mapping: 58, 59 and 60. */
bool face_sprite_redux_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_sprite_redux_actor_t *actor);

bool face_sprite_redux_actor_resolve(
    face_sprite_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_sprite_redux_pose_t *pose);

bool face_sprite_redux_actor_render(
    face_sprite_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_sprite_redux_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
