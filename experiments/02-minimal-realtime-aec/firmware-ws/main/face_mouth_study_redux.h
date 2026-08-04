#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Standalone, face-integrated replacements for legacy mouth studies 23..28.
 *
 * The renderer is deterministic from the complete 40-byte face_render_key_t
 * and 16 kHz sample clock. It is integer-only, stateless, heap-free, and
 * writes one clipped 160x120 RGB565 frame.
 */
enum {
    FACE_MOUTH_STUDY_REDUX_WIDTH = 160,
    FACE_MOUTH_STUDY_REDUX_HEIGHT = 120,
    FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT =
        FACE_MOUTH_STUDY_REDUX_WIDTH * FACE_MOUTH_STUDY_REDUX_HEIGHT,
    FACE_MOUTH_STUDY_REDUX_FRAME_BYTES =
        FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_MOUTH_STUDY_REDUX_CONTEXT_BYTES = 0,
    FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID = 23,
    FACE_MOUTH_STUDY_REDUX_LAST_LEGACY_ID = 28,
    FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT = 6,
};

typedef enum {
    FACE_MOUTH_STUDY_REDUX_PRESTON = 23,
    FACE_MOUTH_STUDY_REDUX_JALI = 24,
    FACE_MOUTH_STUDY_REDUX_RIBBON = 25,
    FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE = 26,
    FACE_MOUTH_STUDY_REDUX_LED_VU = 27,
    FACE_MOUTH_STUDY_REDUX_ORIGAMI = 28,
} face_mouth_study_redux_profile_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} face_mouth_study_redux_bounds_t;

typedef struct {
    face_mouth_study_redux_bounds_t face;
    face_mouth_study_redux_bounds_t left_eye;
    face_mouth_study_redux_bounds_t right_eye;
    face_mouth_study_redux_bounds_t mouth;
    face_mouth_study_redux_bounds_t jaw;
} face_mouth_study_redux_landmarks_t;

/*
 * One resolved skeleton drives the entire character. Mouth corners, cheeks,
 * jaw, eyes, and brows stay parented to these fixed face-space anchors.
 */
typedef struct {
    face_render_key_t source;
    uint32_t input_signature;
    int16_t face_center_x;
    int16_t face_center_y;
    int16_t left_eye_x;
    int16_t right_eye_x;
    int16_t eye_y;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t eye_open[2];
    int16_t brow_raise[2];
    int16_t brow_slant[2];
    int16_t mouth_center_x;
    int16_t mouth_center_y;
    int16_t mouth_width;
    int16_t mouth_open;
    int16_t mouth_round_q8;
    int16_t lip_press_q8;
    int16_t mouth_anchor_x[2];
    int16_t mouth_anchor_y[2];
    int16_t corner_y[2];
    int16_t cheek_x[2];
    int16_t cheek_y[2];
    int16_t cheek_lift;
    int16_t jaw_drop;
    int16_t jaw_skew;
    int16_t teeth_q8;
    int16_t tongue_q8;
    int16_t tongue_x;
    uint8_t expression;
    uint8_t expression_weight;
    uint8_t speech_phase;
    uint8_t viseme_class;
    uint8_t viseme_set;
    uint8_t attention;
    uint8_t blink_q8;
    uint8_t speech_energy;
    uint8_t speech_drive_q8;
    uint8_t anticipation_q8;
    uint8_t settle_q8;
    uint8_t led_envelope_q8;
} face_mouth_study_redux_pose_t;

size_t face_mouth_study_redux_profile_count(void);
bool face_mouth_study_redux_profile_from_index(
    size_t index,
    face_mouth_study_redux_profile_t *profile);
bool face_mouth_study_redux_profile_from_legacy_id(
    uint8_t legacy_id,
    face_mouth_study_redux_profile_t *profile);
const char *face_mouth_study_redux_profile_slug(
    face_mouth_study_redux_profile_t profile);
const char *face_mouth_study_redux_profile_name(
    face_mouth_study_redux_profile_t profile);

bool face_mouth_study_redux_resolve(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_mouth_study_redux_pose_t *pose);

bool face_mouth_study_redux_render_resolved(
    face_mouth_study_redux_profile_t profile,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_study_redux_landmarks_t *landmarks);

bool face_mouth_study_redux_render_checked(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_study_redux_landmarks_t *landmarks);

bool face_mouth_study_redux_render(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
