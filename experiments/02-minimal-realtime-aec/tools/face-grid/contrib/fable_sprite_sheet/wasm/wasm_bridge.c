#include <stdint.h>
#include <string.h>

#include "sprite_face.h"

#include "ega_sorcerer_atlas.h"
#include "handheld_gobbo_atlas.h"
#include "terminal_operator_atlas.h"
#include "vga_navigator_atlas.h"

#include "crc32.h"
#include "scenario.h"

/*
 * WASM bridge for the byte-identical browser check and for viewer
 * integration. Mirrors the native harness: same atlases, same
 * scenario, same CRC. Exported functions are plain C symbols listed
 * in build-wasm.sh.
 */

static const sprite_atlas_t *const ATLASES[] = {
    &ega_sorcerer_atlas,
    &handheld_gobbo_atlas,
    &vga_navigator_atlas,
    &terminal_operator_atlas,
};

static const char *const NAMES[] = {
    "ega_sorcerer",
    "handheld_gobbo",
    "vga_navigator",
    "terminal_operator",
};

enum {
    ATLAS_COUNT = sizeof(ATLASES) / sizeof(ATLASES[0]),
};

static sprite_face_t face_state;
static uint16_t frame_buffer[SPRITE_FACE_PIXEL_COUNT];

uint32_t sprite_wasm_atlas_count(void)
{
    return ATLAS_COUNT;
}

const char *sprite_wasm_atlas_name(uint32_t index)
{
    return index < ATLAS_COUNT ? NAMES[index] : "";
}

uint32_t sprite_wasm_frame_bytes(void)
{
    return SPRITE_FACE_PIXEL_COUNT * (uint32_t)sizeof(uint16_t);
}

uint32_t sprite_wasm_frame_width(void)
{
    return SPRITE_FACE_WIDTH;
}

uint32_t sprite_wasm_frame_height(void)
{
    return SPRITE_FACE_HEIGHT;
}

/* Aggregate CRC over the full shared scenario; must equal the value
 * the native harness records in tests/golden_crcs.txt. */
uint32_t sprite_wasm_scenario_crc(uint32_t atlas_index)
{
    if (atlas_index >= ATLAS_COUNT ||
        !sprite_face_init(&face_state, ATLASES[atlas_index])) {
        return 0;
    }
    uint32_t crc = 0;
    face_keyframe_t keyframe;
    for (uint32_t index = 0; index < SCENARIO_FRAME_COUNT; ++index) {
        scenario_keyframe(index, &keyframe);
        if (!sprite_face_render(
                &face_state, &keyframe, scenario_clock(index),
                frame_buffer, SPRITE_FACE_PIXEL_COUNT)) {
            return 0;
        }
        crc = crc32_update(
            crc, frame_buffer,
            sizeof(uint16_t) * SPRITE_FACE_PIXEL_COUNT);
    }
    return crc;
}

/* Replay the scenario up to frame_index and copy that frame out.
 * Returns the pixel pointer (RGB565 little-endian in WASM memory),
 * or 0 on error. */
uint32_t sprite_wasm_scenario_frame(
    uint32_t atlas_index, uint32_t frame_index)
{
    if (atlas_index >= ATLAS_COUNT ||
        frame_index >= SCENARIO_FRAME_COUNT ||
        !sprite_face_init(&face_state, ATLASES[atlas_index])) {
        return 0;
    }
    face_keyframe_t keyframe;
    for (uint32_t index = 0; index <= frame_index; ++index) {
        scenario_keyframe(index, &keyframe);
        if (!sprite_face_render(
                &face_state, &keyframe, scenario_clock(index),
                frame_buffer, SPRITE_FACE_PIXEL_COUNT)) {
            return 0;
        }
    }
    return (uint32_t)(uintptr_t)frame_buffer;
}

/* Free-running interface for interactive viewers: select an atlas
 * once, then push arbitrary keyframes and clocks. */
int32_t sprite_wasm_select(uint32_t atlas_index)
{
    if (atlas_index >= ATLAS_COUNT) {
        return 0;
    }
    return sprite_face_init(&face_state, ATLASES[atlas_index]) ? 1 : 0;
}

uint32_t sprite_wasm_render(
    const uint8_t *keyframe_bytes, uint32_t sample_clock)
{
    face_keyframe_t keyframe;
    memcpy(&keyframe, keyframe_bytes, sizeof(keyframe));
    if (!sprite_face_render(
            &face_state, &keyframe, sample_clock, frame_buffer,
            SPRITE_FACE_PIXEL_COUNT)) {
        return 0;
    }
    return (uint32_t)(uintptr_t)frame_buffer;
}
