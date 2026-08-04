#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Standalone procedural face actors for the concepts occupying legacy face
 * profile IDs 7..22 and robot-rig IDs 40..46. The module is intentionally not
 * wired into face_render.c: it can be evaluated independently and dropped into
 * that dispatch later.
 *
 * The renderer is a pure integer function of the complete 40-byte facial IR
 * and the 16 kHz sample clock. It owns no memory and writes one caller-owned
 * 160x120 RGB565 display plane.
 */
enum {
    FACE_EYE_ACTOR_WIDTH = 160,
    FACE_EYE_ACTOR_HEIGHT = 120,
    FACE_EYE_ACTOR_PIXEL_COUNT =
        FACE_EYE_ACTOR_WIDTH * FACE_EYE_ACTOR_HEIGHT,
    FACE_EYE_ACTOR_FRAME_BYTES =
        FACE_EYE_ACTOR_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_EYE_ACTOR_FIRST_LEGACY_ID = 7,
    FACE_EYE_ACTOR_LAST_LEGACY_ID = 22,
    FACE_EYE_ACTOR_FIRST_RIG_LEGACY_ID = 40,
    FACE_EYE_ACTOR_LAST_RIG_LEGACY_ID = 46,
};

typedef enum {
    FACE_EYE_ACTOR_VECTOR_FELT = 0,
    FACE_EYE_ACTOR_COZMO_TILES,
    FACE_EYE_ACTOR_ROBO_WEDGE,
    FACE_EYE_ACTOR_ROBO_PEBBLE,
    FACE_EYE_ACTOR_M5_INK,
    FACE_EYE_ACTOR_MANGA_SPARK,
    FACE_EYE_ACTOR_EVE_GLOW,
    FACE_EYE_ACTOR_JIBO_MONOCLE,
    FACE_EYE_ACTOR_SACCADE_SCOPE,
    FACE_EYE_ACTOR_BROW_PUPPET,
    FACE_EYE_ACTOR_LID_THEATRE,
    FACE_EYE_ACTOR_IRIS_DEPTH,
    FACE_EYE_ACTOR_DAWN_SLITS,
    FACE_EYE_ACTOR_CURIOUS_PAIR,
    FACE_EYE_ACTOR_DOT_MARQUEE,
    FACE_EYE_ACTOR_CAT_LANTERN,
    FACE_EYE_ACTOR_VECTOR_STAGE,
    FACE_EYE_ACTOR_COZMO_CONSOLE,
    FACE_EYE_ACTOR_BROW_CHORUS,
    FACE_EYE_ACTOR_MOON_SLEEP,
    FACE_EYE_ACTOR_IRIS_BINOCULAR,
    FACE_EYE_ACTOR_CAT_MECHA,
    FACE_EYE_ACTOR_MANGA_PANEL,
    FACE_EYE_ACTOR_COUNT,
} face_eye_actor_style_t;

typedef enum {
    FACE_EYE_ACTOR_MOUTH_NONE = 0,
    FACE_EYE_ACTOR_MOUTH_LINE,
    FACE_EYE_ACTOR_MOUTH_CAVITY,
    FACE_EYE_ACTOR_MOUTH_PIXEL,
} face_eye_actor_mouth_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    bool deliberate_monocular;
    uint8_t estimated_ops_per_pixel;
} face_eye_actor_info_t;

/*
 * Resolved geometry is exposed for native invariant tests and integration
 * previews. Coordinates are inclusive screen-space centres and bounded sizes.
 * `source` is retained byte-for-byte to make IR consumption auditable.
 */
typedef struct {
    face_render_key_t source;
    int16_t eye_x[2];
    int16_t eye_y[2];
    int16_t eye_w[2];
    int16_t eye_h[2];
    /* Animated opening inside the fixed socket; never below four pixels. */
    int16_t eye_aperture[2];
    /*
     * Eye-only actors translate speech into a bounded internal performance.
     * The outer socket geometry above remains fixed.
     */
    int16_t eye_speech_pulse;
    int16_t eye_speech_spacing;
    int16_t eye_speech_corner;
    /* Anki-style procedural rig values; scales use Q8 where 256 == 1.0. */
    int16_t eye_translate_x[2];
    int16_t eye_translate_y[2];
    int16_t eye_scale_x_q8[2];
    int16_t eye_scale_y_q8[2];
    int16_t eye_angle[2];
    /* Corner order: upper-outer, upper-inner, lower-inner, lower-outer. */
    int16_t eye_corner_radius[2][4];
    int16_t upper_lid_cover[2];
    int16_t upper_lid_angle[2];
    int16_t upper_lid_bend[2];
    int16_t lower_lid_cover[2];
    int16_t lower_lid_angle[2];
    int16_t lower_lid_bend[2];
    int16_t pupil_x[2];
    int16_t pupil_y[2];
    int16_t pupil_radius[2];
    int16_t brow_y[2];
    int16_t brow_slope[2];
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t mouth_w;
    int16_t mouth_h;
    int16_t mouth_mood;
    int16_t mouth_corner[2];
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
    bool speaking;
    bool deliberate_monocular;
} face_eye_actor_pose_t;

size_t face_eye_actor_count(void);
const char *face_eye_actor_slug(face_eye_actor_style_t style);
const char *face_eye_actor_name(face_eye_actor_style_t style);
bool face_eye_actor_info(
    face_eye_actor_style_t style, face_eye_actor_info_t *info);

/* Map existing global IDs 7..22 and robot-rig IDs 40..46. */
bool face_eye_actor_from_legacy_id(
    uint8_t legacy_profile_id, face_eye_actor_style_t *style);

bool face_eye_actor_resolve(
    face_eye_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_eye_actor_pose_t *pose);

bool face_eye_actor_render(
    face_eye_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_eye_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
