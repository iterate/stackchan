#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Six visually independent, allocation-free replacements for legacy rows
 * 4, 6, 29, 35, 36 and 52.  The pack deliberately remains standalone until
 * its native/contact sheets have passed visual review.
 */
enum {
    FACE_SALVAGE_REDUX_WIDTH = 160,
    FACE_SALVAGE_REDUX_HEIGHT = 120,
    FACE_SALVAGE_REDUX_PIXEL_COUNT =
        FACE_SALVAGE_REDUX_WIDTH * FACE_SALVAGE_REDUX_HEIGHT,
    FACE_SALVAGE_REDUX_FRAME_BYTES =
        FACE_SALVAGE_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
};

typedef enum {
    /* Clean cinematic shape language; not based on a specific character. */
    FACE_SALVAGE_REDUX_STORY_SCOUT = 0,
    /* Handheld-game sprite silhouette with vector-quality face acting. */
    FACE_SALVAGE_REDUX_POCKET_COURIER,
    /* Minimal mouthless EVE/Anki-style luminous eye performance. */
    FACE_SALVAGE_REDUX_VELA_EYES,
    /* Folded-paper mask whose mouth is an attached ribbon fold. */
    FACE_SALVAGE_REDUX_KITE_ORACLE,
    /* Circular garden automaton with petal irises and a segmented voice arc. */
    FACE_SALVAGE_REDUX_ORBIT_GARDENER,
    /* Bold felt-theatre familiar with stitched, elastic features. */
    FACE_SALVAGE_REDUX_FELT_FAMILIAR,
    FACE_SALVAGE_REDUX_COUNT,
} face_salvage_redux_style_t;

typedef enum {
    FACE_SALVAGE_REDUX_GRAMMAR_SOFT_CINEMATIC = 0,
    FACE_SALVAGE_REDUX_GRAMMAR_HANDHELD_HYBRID,
    FACE_SALVAGE_REDUX_GRAMMAR_LUMINOUS_EYES_ONLY,
    FACE_SALVAGE_REDUX_GRAMMAR_FOLDED_RIBBON,
    FACE_SALVAGE_REDUX_GRAMMAR_SEGMENTED_ORBIT,
    FACE_SALVAGE_REDUX_GRAMMAR_STITCHED_ELASTIC,
    FACE_SALVAGE_REDUX_GRAMMAR_COUNT,
} face_salvage_redux_grammar_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_profile_id;
    uint8_t grammar;
    bool mouthless;
    bool pixel_hybrid;
    uint8_t estimated_ops_per_pixel;
} face_salvage_redux_info_t;

/*
 * Resolved semantic geometry is exposed to native tests.  Facial anchors are
 * invariant; articulation changes apertures and attached contours only.
 * `source` proves that the complete schema-v2 40-byte IR reached the actor.
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
    int16_t mouth_asymmetry;
    int16_t body_lean_x;
    int16_t body_lean_y;
    int16_t speech_wave;
    uint8_t speech_open;
    uint8_t speech_width;
    uint8_t speech_round;
    uint8_t speech_press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t cheek;
    uint8_t consonant;
    uint8_t detail_phase;
    uint8_t stage_expression;
    uint8_t expression_weight;
    uint8_t activity;
    uint8_t speech_phase;
    uint8_t attention;
    bool speaking;
    bool mouthless;
} face_salvage_redux_pose_t;

size_t face_salvage_redux_count(void);
const char *face_salvage_redux_slug(face_salvage_redux_style_t style);
const char *face_salvage_redux_name(face_salvage_redux_style_t style);
bool face_salvage_redux_info(
    face_salvage_redux_style_t style,
    face_salvage_redux_info_t *info);

bool face_salvage_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_salvage_redux_style_t *style);

bool face_salvage_redux_resolve(
    face_salvage_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_salvage_redux_pose_t *pose);

bool face_salvage_redux_render(
    face_salvage_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_salvage_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
