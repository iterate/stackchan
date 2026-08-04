#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"
#include "sprite_sheet.h"

/*
 * Sprite-sheet face playback engine.
 *
 * Consumes the stable 12-byte face_keyframe_t plus the 16 kHz sample
 * clock and composites one 160x120 RGB565 frame from an FSPR atlas.
 * Integer arithmetic only, caller-owned buffers, no allocation ever.
 *
 * Rendering is deterministic: the same (atlas, keyframe, clock) call
 * sequence produces byte-identical frames on any platform, which the
 * WASM comparison harness verifies. Autonomous idle behaviour (blinks,
 * saccades, breathing bob, idle acts) derives from hashed clock
 * windows; the only cross-frame state is the mouth debounce /
 * coarticulation machine and forced-blink edge tracking.
 */

enum {
    SPRITE_FACE_WIDTH = 160,
    SPRITE_FACE_HEIGHT = 120,
    SPRITE_FACE_PIXEL_COUNT = SPRITE_FACE_WIDTH * SPRITE_FACE_HEIGHT,
};

typedef struct {
    const sprite_atlas_t *atlas;
    uint32_t last_clock;
    uint32_t shape_since;      /* clock when current shape was entered */
    uint32_t target_since;     /* clock when last_target first seen    */
    uint32_t forced_blink_edge; /* clock of last forced transition     */
    uint8_t current_shape;     /* sprite_mouth_shape_t on display      */
    uint8_t last_target;       /* last selector output                 */
    uint8_t prev_flags;        /* previous keyframe flags (blink edge) */
    uint8_t forced_blink;      /* forced blink currently held          */
} sprite_face_t;

/*
 * Validate the atlas (magic, version, cell streams, palette bounds)
 * and reset playback state. Returns false on a malformed atlas; the
 * engine never reads out-of-bounds afterwards.
 */
bool sprite_face_init(sprite_face_t *face, const sprite_atlas_t *atlas);

/*
 * Composite one frame. `sample_clock` is in 16 kHz PCM samples and
 * must be monotonic; a backwards jump resets idle/debounce state.
 * Returns false if the destination capacity is too small (nothing is
 * written) or the engine is uninitialised.
 */
bool sprite_face_render(
    sprite_face_t *face,
    const face_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

/*
 * The shape the debounce machine would target for a keyframe, before
 * hold/coarticulation. Exposed for tests and tuning UIs.
 */
uint8_t sprite_face_select_shape(
    const sprite_selector_t *selector, const face_keyframe_t *keyframe);
