#pragma once

/*
 * fable_expression_actors_v3 — public API.
 *
 * Five structurally distinct character renderers for the StackChan face
 * lab. Each profile draws one 160x120 RGB565 frame as a pure function of
 * the 40-byte face_render_key_t (schema v2) plus the 16 kHz sample clock.
 * No heap, no floats, no retained state; byte-identical output across
 * optimization levels, hosts, and WASM.
 *
 * Unlike style packs that share one rig with different palettes, these
 * five actors have different silhouettes, feature mechanics, and acting
 * channels:
 *
 *   mochi-cat       eared plush mascot; ears/whiskers act, bead eyes
 *   karakuri-brass  segmented plate puppet; shutter irises, hinged jaw
 *   emote-sticker   bold sticker face; emoji-grammar accents, teeth/tongue
 *   will-o-wisp     emissive spirit; emotion deforms the silhouette
 *   mono-scope      cyclops robot; one big lens carries the acting
 *
 * Every actor consumes all 40 IR bytes: the 12-byte control prefix, the
 * viseme vocabulary block (OVR15 native; VRM5/Preston9/Microsoft22
 * collapsed through mapping tables), the facial-action block, the
 * head/body performance block, and stage_expression/expression_weight
 * through a non-monotonic acting-response curve (anticipation dip ->
 * rise -> overshoot -> settle), so stage cue attacks read as
 * anticipation-active-settle without renderer state.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FEA_FRAME_WIDTH = 160,
    FEA_FRAME_HEIGHT = 120,
    FEA_PIXEL_COUNT = FEA_FRAME_WIDTH * FEA_FRAME_HEIGHT,
    FEA_FRAME_BYTES = FEA_PIXEL_COUNT * (int)sizeof(uint16_t),
    FEA_CONTEXT_BYTES = 0,
};

typedef enum {
    FEA_PROFILE_MOCHI_CAT = 0,
    FEA_PROFILE_KARAKURI_BRASS = 1,
    FEA_PROFILE_EMOTE_STICKER = 2,
    FEA_PROFILE_WILL_O_WISP = 3,
    FEA_PROFILE_MONO_SCOPE = 4,
    FEA_PROFILE_COUNT = 5,
} fea_profile_t;

/*
 * Layout-identical to face_render_info_t in firmware-ws/main/face_render.h
 * (static-asserted at 16 bytes) so integration is a struct copy.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t work_width;
    uint16_t work_height;
    uint16_t framebuffer_bytes;
    uint8_t family;
    uint8_t mouth_kind;
    uint8_t flags;
    uint8_t reserved;
    uint16_t estimated_ops_per_pixel;
} fea_info_t;

/*
 * Geometry probe for tests: the same layout code that feeds the
 * rasterizer fills this, so assertions about clamping, parenting, and
 * acting run against the exact geometry that gets drawn. Screen
 * coordinates are Q4 (16 == one pixel); envelopes are Q8 (256 == 1.0).
 */
typedef struct {
    uint8_t emotion;          /* resolved stage expression, clamped */
    int16_t act_q8;           /* acting-curve response, may dip < 0 */
    uint8_t has_mouth;        /* 0 for mouthless layouts */
    /* eyes: index 0 == viewer left; cyclops mirrors one eye into both */
    int16_t eye_cx_q4[2];
    int16_t eye_cy_q4[2];
    int16_t eye_open_q8[2];   /* resolved aperture after lids/blink */
    int16_t pupil_x_q4[2];    /* hard-clamped inside the aperture */
    int16_t pupil_y_q4[2];
    int16_t pupil_r_q4[2];
    /* brows (or brow bar / brow slats depending on actor) */
    int16_t brow_y_q4[2];     /* vertical anchor at eye center x */
    int16_t brow_tilt_q8[2];  /* signed rotation, + == outer end up */
    /* mouth: corner points are the actual lip-curve endpoints after the
     * whole-face transform — parenting is asserted on these. */
    int16_t mouth_cx_q4;
    int16_t mouth_cy_q4;
    int16_t corner_x_q4[2];
    int16_t corner_y_q4[2];
    int16_t jaw_q4;           /* interior opening height */
    /* claimed extent of everything drawn brighter than background */
    int16_t extent_left_q4;
    int16_t extent_top_q4;
    int16_t extent_right_q4;
    int16_t extent_bottom_q4;
} fea_probe_t;

size_t fea_profile_count(void);
const char *fea_profile_slug(fea_profile_t profile);
const char *fea_profile_name(fea_profile_t profile);
bool fea_profile_info(fea_profile_t profile, fea_info_t *info);

/*
 * Resolve the acting geometry for one frame without drawing. Returns
 * false on NULL arguments or an invalid profile. Deterministic: the
 * same (profile, key, clock) triple always yields the same probe.
 */
bool fea_probe(
    fea_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fea_probe_t *probe);

/*
 * Render one full frame. Writes every pixel (no read-modify-write of
 * the caller buffer). Returns false on NULL arguments, invalid profile,
 * or pixel_capacity < FEA_PIXEL_COUNT.
 */
bool fea_render_frame(
    fea_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

#ifdef __cplusplus
}
#endif
