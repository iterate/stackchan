#pragma once

#include <stdint.h>

#include "face_pose.h"

/*
 * Stable continuous-control interchange. This is intentionally smaller than
 * face_pose_t and remains useful for renderers that only need blendshape-like
 * controls.
 *
 * The same record can be produced locally from PCM, streamed from a host, or
 * replayed from a deterministic test capture. All fields are byte-sized so
 * the wire representation is endian-independent.
 */
typedef struct {
    uint8_t mouth_open;
    uint8_t mouth_width;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t mouth_teeth;
    uint8_t eye_left_open;
    uint8_t eye_right_open;
    int8_t look_x;
    int8_t look_y;
    int8_t brow;
    uint8_t expression;
    uint8_t flags;
} face_keyframe_t;

enum {
    FACE_KEYFRAME_FLAG_SPEAKING = 1U << 0,
    FACE_KEYFRAME_FLAG_BLINKING = 1U << 1,
    FACE_KEYFRAME_BYTES = 12,
};

_Static_assert(
    sizeof(face_keyframe_t) == FACE_KEYFRAME_BYTES,
    "face keyframe wire format must remain exactly 12 bytes");

void face_keyframe_from_pose(
    const face_pose_t *pose, face_keyframe_t *keyframe);
void face_pose_apply_keyframe(
    face_pose_t *pose, const face_keyframe_t *keyframe);

/*
 * General-purpose renderer IR. The first twelve bytes remain the compact
 * continuous keyframe above. Four appended bytes preserve discrete mouth
 * identity for sprite sheets and viseme renderers:
 *
 * - `viseme` uses the canonical 15-shape FACE_VISEME_* vocabulary;
 * - `phoneme` optionally identifies the source classifier token;
 * - `viseme_weight` is classifier confidence / blend weight;
 * - `audio_level` is a compressed 0..255 energy uniform for abstract shaders.
 *
 * This deliberately supports both parameterized geometry (Live2D/JALI-like)
 * and discrete atlases (VRM vowels / Preston-Blair-like sprites) without
 * forcing either renderer family to reverse-engineer the other.
 */
typedef struct {
    face_keyframe_t controls;
    uint8_t viseme;
    uint8_t phoneme;
    uint8_t viseme_weight;
    uint8_t audio_level;
} face_render_key_t;

enum {
    FACE_RENDER_KEY_BYTES = 16,
};

_Static_assert(
    sizeof(face_render_key_t) == FACE_RENDER_KEY_BYTES,
    "face renderer IR must remain exactly 16 bytes");

void face_render_key_from_pose(
    const face_pose_t *pose, face_render_key_t *render_key);
void face_pose_apply_render_key(
    face_pose_t *pose, const face_render_key_t *render_key);
