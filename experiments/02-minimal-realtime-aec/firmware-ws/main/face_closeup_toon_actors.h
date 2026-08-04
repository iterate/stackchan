#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Standalone close-up/toon character replacements for legacy IDs 42..51.
 *
 * Each actor owns a different silhouette and facial rig.  The render target
 * is the firmware/WASM 160x120 RGB565 surface; no allocation or floating
 * point is required.
 */
enum {
    FACE_CLOSEUP_TOON_WIDTH = 160,
    FACE_CLOSEUP_TOON_HEIGHT = 120,
    FACE_CLOSEUP_TOON_PIXEL_COUNT =
        FACE_CLOSEUP_TOON_WIDTH * FACE_CLOSEUP_TOON_HEIGHT,
    FACE_CLOSEUP_TOON_FRAME_BYTES =
        FACE_CLOSEUP_TOON_PIXEL_COUNT * (int)sizeof(uint16_t),
};

typedef enum {
    FACE_CLOSEUP_TOON_BROW_DIALOGUE_DIRECTOR = 0,
    FACE_CLOSEUP_TOON_SLEEP_WAKE_DREAMER,
    FACE_CLOSEUP_TOON_IRIS_PARALLAX_SCOUT,
    FACE_CLOSEUP_TOON_CAT_OPTICS_FAMILIAR,
    FACE_CLOSEUP_TOON_M5_MANGA_LEAD,
    FACE_CLOSEUP_TOON_VGA_STAR_NAVIGATOR,
    FACE_CLOSEUP_TOON_POCKET_RELAY_CREATURE,
    FACE_CLOSEUP_TOON_EGA_QUEST_SQUIRE,
    FACE_CLOSEUP_TOON_VGA_ELDER_STORYTELLER,
    FACE_CLOSEUP_TOON_TALKIE_MOON_MECHANIC,
    FACE_CLOSEUP_TOON_COUNT,
} face_closeup_toon_style_t;

typedef enum {
    FACE_CLOSEUP_TOON_MOUTH_CURVE = 0,
    FACE_CLOSEUP_TOON_MOUTH_CAVITY,
    FACE_CLOSEUP_TOON_MOUTH_MUZZLE,
    FACE_CLOSEUP_TOON_MOUTH_MANGA,
    FACE_CLOSEUP_TOON_MOUTH_BEARD,
} face_closeup_toon_mouth_kind_t;

/*
 * Rendering grammar is deliberately finer than anatomical mouth kind.
 * Every actor gets an identifiable topology rather than a recolored copy of
 * the same jaw-flap.
 */
typedef enum {
    FACE_CLOSEUP_TOON_GRAMMAR_SIGNAL_RIBBON = 0,
    FACE_CLOSEUP_TOON_GRAMMAR_DREAM_CUPID,
    FACE_CLOSEUP_TOON_GRAMMAR_SCOUT_GRIN,
    FACE_CLOSEUP_TOON_GRAMMAR_CAT_MUZZLE,
    FACE_CLOSEUP_TOON_GRAMMAR_MANGA_PETAL,
    FACE_CLOSEUP_TOON_GRAMMAR_NAV_CONSOLE,
    FACE_CLOSEUP_TOON_GRAMMAR_RELAY_ELASTIC,
    FACE_CLOSEUP_TOON_GRAMMAR_SQUIRE_FACET,
    FACE_CLOSEUP_TOON_GRAMMAR_ELDER_BEARD,
    FACE_CLOSEUP_TOON_GRAMMAR_MECHANIC_CROOK,
    FACE_CLOSEUP_TOON_GRAMMAR_COUNT,
} face_closeup_toon_mouth_grammar_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t mouth_kind;
    uint8_t mouth_grammar;
    uint8_t eye_kind;
    uint8_t estimated_ops_per_pixel;
} face_closeup_toon_info_t;

/*
 * Test-visible resolved anatomy.  Eye anchors describe sockets and remain
 * stable across speech.  Mouth corners are offsets parented to mouth_x/y, so
 * head motion cannot tear the mouth apart.  `source` proves the full schema-v2
 * 40-byte IR crossed the renderer boundary unchanged.
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
} face_closeup_toon_pose_t;

size_t face_closeup_toon_count(void);
const char *face_closeup_toon_slug(face_closeup_toon_style_t style);
const char *face_closeup_toon_name(face_closeup_toon_style_t style);
bool face_closeup_toon_info(
    face_closeup_toon_style_t style,
    face_closeup_toon_info_t *info);

/* Exact replacement mapping: legacy profile IDs 42 through 51. */
bool face_closeup_toon_from_legacy_id(
    uint8_t legacy_profile_id,
    face_closeup_toon_style_t *style);

bool face_closeup_toon_resolve(
    face_closeup_toon_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_closeup_toon_pose_t *pose);

bool face_closeup_toon_render(
    face_closeup_toon_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_closeup_toon_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
