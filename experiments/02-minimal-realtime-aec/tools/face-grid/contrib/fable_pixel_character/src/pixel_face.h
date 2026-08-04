#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

/*
 * fable_pixel_character — standalone pixel/adventure-game portrait renderers.
 *
 * Every renderer is a pure function of (profile, keyframe, sample clock):
 * integer arithmetic only, no allocation, no retained state, no PCM access.
 * Output is one 160×120 RGB565 frame into a caller-owned buffer, matching
 * the face_render.h contract so profiles can later be folded into the
 * production dispatch table.
 *
 * The deterministic idle rig (blinks, saccades, brow acts, breathing,
 * speech head-bob) is derived from `sample_clock` (16 kHz PCM samples)
 * plus a per-profile salt, so device and WASM builds are byte-identical.
 */

enum {
    PIXEL_FACE_WIDTH = 160,
    PIXEL_FACE_HEIGHT = 120,
    PIXEL_FACE_PIXEL_COUNT = PIXEL_FACE_WIDTH * PIXEL_FACE_HEIGHT,
    PIXEL_FACE_FRAME_BYTES = PIXEL_FACE_PIXEL_COUNT * (int)sizeof(uint16_t),
    PIXEL_FACE_SAMPLE_RATE = 16000,
};

typedef enum {
    /* Sierra-style 16-colour EGA hero portrait, checkerboard dither pairs. */
    PIXEL_FACE_EGA_QUEST = 0,
    /* 256-colour-era closeup with banded ramp shading and polygon lips. */
    PIXEL_FACE_VGA_ELDER,
    /* Talkie-era closeup with a nine-shape sprite mouth bank and head bob. */
    PIXEL_FACE_TALKIE_CLOSEUP,
    /* Riveted robot with LED-matrix eyes and a VU-segment mouth. */
    PIXEL_FACE_PIXEL_AUTOMATON,
    /* Character-cell terminal face composed from a phosphor glyph font. */
    PIXEL_FACE_AMBER_TERMINAL,
    /* Four-shade handheld dialogue scene with typing mock text. */
    PIXEL_FACE_POCKET_RPG,
    /* 1-bit hooded rogue, ordered Bayer 8×8 dither of procedural shading. */
    PIXEL_FACE_DITHERED_ROGUE,
    /* 1-bit librarian, Atkinson error diffusion (6/8 of error diffused). */
    PIXEL_FACE_ATKINSON_PORTRAIT,
    /* 8×8 ink/paper attribute cells with deliberate attribute clash. */
    PIXEL_FACE_ZX_ATTRIBUTE,
    /* Cyan/magenta/white four-colour cadet with cross-hatch dither. */
    PIXEL_FACE_CGA_ARCADE,
    /* Tile-constrained minstrel: 3-colour subpalettes per 16×16 block. */
    PIXEL_FACE_NES_TILE,
    /* Double-wide multicolor punk with raster-bar backdrop. */
    PIXEL_FACE_C64_MULTICOLOR,
    PIXEL_FACE_PROFILE_COUNT,
} pixel_face_profile_t;

typedef enum {
    PIXEL_FACE_MOUTH_SPRITE_BANK = 0, /* discrete shape swap from a bank   */
    PIXEL_FACE_MOUTH_POLYGON = 1,     /* scanline-interpolated lip curves  */
    PIXEL_FACE_MOUTH_SEGMENTS = 2,    /* LED/VU segment bar                */
    PIXEL_FACE_MOUTH_GLYPH = 3,       /* character-cell glyph strings      */
    PIXEL_FACE_MOUTH_FLAP = 4,        /* quantised 3-frame flap            */
} pixel_face_mouth_system_t;

typedef struct {
    /* Logical art grid before integer upscale (e.g. 80×60 at 2×2). */
    uint16_t art_width;
    uint16_t art_height;
    uint8_t art_scale_x;
    uint8_t art_scale_y;
    uint8_t mouth_system;      /* pixel_face_mouth_system_t */
    uint8_t mouth_shape_count; /* discrete shapes, 0 for continuous */
    uint16_t colour_count;     /* colours the style may emit */
    /* Strategy strings are static; useful for the manifest and review. */
    const char *palette_strategy;
    const char *dither_strategy;
} pixel_face_style_t;

size_t pixel_face_profile_count(void);
const char *pixel_face_profile_slug(pixel_face_profile_t profile);
const char *pixel_face_profile_name(pixel_face_profile_t profile);
bool pixel_face_profile_style(
    pixel_face_profile_t profile, pixel_face_style_t *style);

/*
 * Render exactly PIXEL_FACE_WIDTH × PIXEL_FACE_HEIGHT RGB565 pixels.
 * Returns false when the profile is unknown, `keyframe`/`rgb565` is NULL,
 * or `pixel_capacity` is smaller than PIXEL_FACE_PIXEL_COUNT.
 */
bool pixel_face_render(
    pixel_face_profile_t profile,
    const face_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
