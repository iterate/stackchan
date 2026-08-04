#pragma once

#include <stdint.h>

#include "face_keyframe.h"

/*
 * Shared deterministic keyframe scenario. The native harness and the
 * WASM bridge compile this same file, feed the same keyframes at the
 * same sample clocks, and must produce byte-identical frames.
 */

enum {
    SCENARIO_FRAME_COUNT = 300, /* ten seconds at 30 fps */
};

uint32_t scenario_clock(uint32_t frame_index);
void scenario_keyframe(uint32_t frame_index, face_keyframe_t *keyframe);
