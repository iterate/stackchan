#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Six allocation-free replacements for weak legacy rows.  This module is
 * deliberately standalone: face_render.c can adopt it later without making
 * the visual review harness depend on the production dispatch.
 */
enum {
    FACE_SALVAGE_ACTOR_WIDTH = 160,
    FACE_SALVAGE_ACTOR_HEIGHT = 120,
    FACE_SALVAGE_ACTOR_PIXEL_COUNT =
        FACE_SALVAGE_ACTOR_WIDTH * FACE_SALVAGE_ACTOR_HEIGHT,
    FACE_SALVAGE_ACTOR_FRAME_BYTES =
        FACE_SALVAGE_ACTOR_PIXEL_COUNT * (int)sizeof(uint16_t),
};

typedef enum {
    FACE_SALVAGE_ACTOR_AMBER_TERMINAL = 0,
    FACE_SALVAGE_ACTOR_FILM_NOIR_ROGUE,
    FACE_SALVAGE_ACTOR_NEON_MASK,
    FACE_SALVAGE_ACTOR_RED_OPTIC,
    FACE_SALVAGE_ACTOR_HUB75_MASCOT,
    FACE_SALVAGE_ACTOR_ZINE_ROGUE,
    FACE_SALVAGE_ACTOR_COUNT,
} face_salvage_actor_style_t;

typedef enum {
    FACE_SALVAGE_MOUTH_NONE = 0,
    FACE_SALVAGE_MOUTH_GLYPH,
    FACE_SALVAGE_MOUTH_LINE,
    FACE_SALVAGE_MOUTH_CAVITY,
    FACE_SALVAGE_MOUTH_BLOCK,
} face_salvage_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    bool deliberate_monocular;
    uint8_t estimated_ops_per_pixel;
} face_salvage_actor_info_t;

/*
 * Resolved values expose the fixed topology and semantic acting controls to
 * native tests.  `source` is retained byte-for-byte so captures can prove that
 * the complete schema-v2 40-byte IR reached the renderer.
 */
typedef struct {
    face_render_key_t source;
    int16_t eye_x[2];
    int16_t eye_y[2];
    int16_t eye_w[2];
    int16_t eye_h[2];
    int16_t eye_aperture[2];
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
    int16_t shoulder_lean_x;
    int16_t shoulder_lean_y;
    int16_t speech_pulse;
    uint8_t speech_open;
    uint8_t speech_width;
    uint8_t speech_round;
    uint8_t speech_press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t stage_expression;
    uint8_t expression_weight;
    uint8_t activity;
    uint8_t speech_phase;
    uint8_t attention;
    bool speaking;
    bool deliberate_monocular;
} face_salvage_actor_pose_t;

size_t face_salvage_actor_count(void);
const char *face_salvage_actor_slug(face_salvage_actor_style_t style);
const char *face_salvage_actor_name(face_salvage_actor_style_t style);
bool face_salvage_actor_info(
    face_salvage_actor_style_t style,
    face_salvage_actor_info_t *info);

/*
 * Preserve the existing IDs so integration is one explicit dispatch branch:
 * 4, 6, 29, 35, 36 and 52.
 */
bool face_salvage_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_salvage_actor_style_t *style);

bool face_salvage_actor_resolve(
    face_salvage_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_salvage_actor_pose_t *pose);

bool face_salvage_actor_render(
    face_salvage_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_salvage_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
