#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * Four deliberately different, stateless pixel-character portraits selected
 * from the Fable renderer study. The caller supplies the production 40-byte
 * performance key and a 160x120 RGB565 framebuffer. No context, heap, PCM
 * history, floating point, or retained renderer state is required.
 */
enum {
    FACE_PIXEL_PACK_WIDTH = 160,
    FACE_PIXEL_PACK_HEIGHT = 120,
    FACE_PIXEL_PACK_PIXEL_COUNT =
        FACE_PIXEL_PACK_WIDTH * FACE_PIXEL_PACK_HEIGHT,
    FACE_PIXEL_PACK_FRAME_BYTES =
        FACE_PIXEL_PACK_PIXEL_COUNT * (int)sizeof(uint16_t),
    FACE_PIXEL_PACK_SAMPLE_RATE = 16000,
    FACE_PIXEL_PACK_CONTEXT_BYTES = 0,
};

typedef enum {
    FACE_PIXEL_PACK_EGA_QUEST = 0,
    FACE_PIXEL_PACK_VGA_ELDER,
    FACE_PIXEL_PACK_TALKIE_CLOSEUP,
    FACE_PIXEL_PACK_DITHERED_ROGUE,
    FACE_PIXEL_PACK_PROFILE_COUNT,
} face_pixel_pack_profile_t;

typedef enum {
    FACE_PIXEL_PACK_MOUTH_SPRITES = 0,
    FACE_PIXEL_PACK_MOUTH_POLYGON,
} face_pixel_pack_mouth_system_t;

typedef struct {
    uint16_t native_width;
    uint16_t native_height;
    uint8_t scale_x;
    uint8_t scale_y;
    uint8_t mouth_system;
    uint8_t mouth_shapes;
    uint16_t palette_colours;
    const char *palette_strategy;
    const char *dither_strategy;
} face_pixel_pack_style_t;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
} face_pixel_pack_bounds_t;

/*
 * Semantic bounds are emitted from the same resolved pose used for drawing.
 * They let host/device QA distinguish a safe buffer write from aesthetically
 * clipped eyes, mouth, or head geometry.
 */
typedef struct {
    face_pixel_pack_bounds_t face;
    face_pixel_pack_bounds_t left_eye;
    face_pixel_pack_bounds_t right_eye;
    face_pixel_pack_bounds_t mouth;
} face_pixel_pack_landmarks_t;

size_t face_pixel_pack_profile_count(void);
const char *face_pixel_pack_profile_slug(face_pixel_pack_profile_t profile);
const char *face_pixel_pack_profile_name(face_pixel_pack_profile_t profile);
bool face_pixel_pack_profile_style(
    face_pixel_pack_profile_t profile,
    face_pixel_pack_style_t *style);

bool face_pixel_pack_render(
    face_pixel_pack_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

bool face_pixel_pack_render_checked(
    face_pixel_pack_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_pixel_pack_landmarks_t *landmarks);

