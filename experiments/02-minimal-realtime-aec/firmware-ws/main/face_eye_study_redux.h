#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Standalone replacement renderer for the legacy eye-study range 15..22.
 *
 * The numeric enum values intentionally match face_render_profile_t.  The
 * module does not include face_render.h and can therefore be reviewed,
 * tested, or integrated without creating a dependency on legacy dispatch.
 *
 * Rendering is deterministic from the complete 40-byte face_render_key_t and
 * the 16 kHz sample clock.  It is integer-only, stateless, heap-free, and
 * always writes one clipped 160x120 RGB565 frame.
 */
enum {
    FACE_EYE_STUDY_REDUX_WIDTH = 160,
    FACE_EYE_STUDY_REDUX_HEIGHT = 120,
    FACE_EYE_STUDY_REDUX_PIXEL_COUNT =
        FACE_EYE_STUDY_REDUX_WIDTH * FACE_EYE_STUDY_REDUX_HEIGHT,
    FACE_EYE_STUDY_REDUX_FRAME_BYTES =
        FACE_EYE_STUDY_REDUX_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_EYE_STUDY_REDUX_CONTEXT_BYTES = 0,
    FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID = 15,
    FACE_EYE_STUDY_REDUX_LAST_LEGACY_ID = 22,
    FACE_EYE_STUDY_REDUX_PROFILE_COUNT = 8,
};

typedef enum {
    FACE_EYE_STUDY_REDUX_SACCADE_LAB = 15,
    FACE_EYE_STUDY_REDUX_BROW_DIALOGUE = 16,
    FACE_EYE_STUDY_REDUX_LID_ANTICIPATION = 17,
    FACE_EYE_STUDY_REDUX_IRIS_PARALLAX = 18,
    FACE_EYE_STUDY_REDUX_SLEEP_WAKE = 19,
    FACE_EYE_STUDY_REDUX_CURIOUS_TILT = 20,
    FACE_EYE_STUDY_REDUX_DOT_MATRIX_EYES = 21,
    FACE_EYE_STUDY_REDUX_CAT_OPTICS = 22,
} face_eye_study_redux_profile_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} face_eye_study_redux_bounds_t;

typedef struct {
    face_eye_study_redux_bounds_t face;
    face_eye_study_redux_bounds_t left_eye;
    face_eye_study_redux_bounds_t right_eye;
} face_eye_study_redux_landmarks_t;

/*
 * Resolved facial performance.  Centers are Q8 screen pixels; all other Q8
 * controls use 256 as unity/full range.  Eye anchors remain fixed by profile:
 * gaze and head cues move only the iris/parallax layer and authored lids.
 */
typedef struct {
    face_render_key_t source;
    uint32_t input_signature;
    int32_t left_center_x_q8;
    int32_t right_center_x_q8;
    int32_t center_y_q8;
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    int32_t openness_q8[2];
    int32_t width_scale_q8[2];
    int32_t height_scale_q8[2];
    int32_t eye_tilt_q8[2];
    int32_t upper_lid_q8[2];
    int32_t lower_lid_q8[2];
    int32_t brow_raise_q8[2];
    int32_t brow_tilt_q8[2];
    int32_t brow_arch_q8[2];
    int32_t pupil_scale_q8;
    int32_t parallax_q8;
    int32_t blink_q8;
    int32_t speech_energy_q8;
    uint8_t expression;
    uint8_t expression_weight;
    uint8_t attention;
    uint8_t saccade_active;
} face_eye_study_redux_pose_t;

size_t face_eye_study_redux_profile_count(void);
bool face_eye_study_redux_profile_from_index(
    size_t index, face_eye_study_redux_profile_t *profile);
bool face_eye_study_redux_profile_from_legacy_id(
    uint8_t legacy_id, face_eye_study_redux_profile_t *profile);
const char *face_eye_study_redux_profile_slug(
    face_eye_study_redux_profile_t profile);
const char *face_eye_study_redux_profile_name(
    face_eye_study_redux_profile_t profile);

bool face_eye_study_redux_resolve(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_eye_study_redux_pose_t *pose);

bool face_eye_study_redux_render_resolved(
    face_eye_study_redux_profile_t profile,
    const face_eye_study_redux_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_eye_study_redux_landmarks_t *landmarks);

bool face_eye_study_redux_render_checked(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_eye_study_redux_landmarks_t *landmarks);

bool face_eye_study_redux_render(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
