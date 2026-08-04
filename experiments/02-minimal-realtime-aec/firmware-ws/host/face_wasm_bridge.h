#pragma once

#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"
#include "face_render.h"
#include "face_stage.h"

enum {
    STACKCHAN_WASM_ABI_VERSION = 2,
    STACKCHAN_WASM_PROFILE_ENVELOPE_FAST = 0,
    STACKCHAN_WASM_PROFILE_ENVELOPE_SMOOTH = 1,
    STACKCHAN_WASM_PROFILE_VISEME_RESPONSIVE = 2,
    STACKCHAN_WASM_PROFILE_VISEME_BALANCED = 3,
    STACKCHAN_WASM_PROFILE_SPECTRAL_RESPONSIVE = 4,
    STACKCHAN_WASM_PROFILE_SPECTRAL_SMOOTH = 5,
};

typedef struct {
    uint32_t frame_index;
    uint32_t playout_samples;
    uint16_t level;
    uint8_t viseme;
    uint8_t confidence;
    uint16_t state_bytes;
    uint16_t model_bytes;
} stackchan_wasm_metrics_t;

_Static_assert(
    sizeof(stackchan_wasm_metrics_t) == 16,
    "WASM metrics ABI must remain exactly 16 bytes");

uint32_t stackchan_wasm_abi_version(void);
size_t stackchan_wasm_keyframe_size(void);
size_t stackchan_wasm_render_key_size(void);
size_t stackchan_wasm_metrics_size(void);
size_t stackchan_wasm_render_frame_bytes(void);
size_t stackchan_wasm_stage_cue_size(void);
size_t stackchan_wasm_render_profile_count(void);
const char *stackchan_wasm_render_profile_slug(uint32_t profile);
const char *stackchan_wasm_render_profile_name(uint32_t profile);
const char *stackchan_wasm_render_profile_family_name(uint32_t profile);
int stackchan_wasm_render_profile_info(
    uint32_t profile, face_render_info_t *info);
void *stackchan_wasm_create(
    uint32_t profile,
    uint32_t sample_rate,
    const uint8_t *viseme_model,
    size_t viseme_model_bytes);
void stackchan_wasm_destroy(void *instance);
void stackchan_wasm_push_pcm(
    void *instance, const int16_t *samples, size_t sample_count);
int stackchan_wasm_snapshot(
    const void *instance,
    face_render_key_t *render_key,
    stackchan_wasm_metrics_t *metrics);
int stackchan_wasm_render(
    uint32_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
int stackchan_wasm_apply_stage_cue(
    const face_stage_cue_t *cue,
    uint32_t sample_clock,
    face_render_key_t *render_key);
