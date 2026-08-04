#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * fable_mouth_geometry — standalone mouth/viseme-geometry renderer pack.
 *
 * Ten visibly different mouth-centric face renderers driven only by the
 * stable 12-byte semantic keyframe plus the 16 kHz sample clock:
 *
 *   sprite systems   preston_sprites, talkie_flap, manga_snap, dot_matrix
 *   polygon systems  polygon_jali, bezier_ribbon, origami_mask, teeth_tongue
 *   alternatives     led_vu_mouth, scope_trace
 *
 * Every renderer is a pure function of (keyframe, sample_clock): integer
 * arithmetic only, no floats, no per-frame allocation, no hidden state, so
 * native and WebAssembly builds produce byte-identical RGB565 frames.
 *
 * An optional caller-owned coarticulation stage (fmg_coart_t) smooths raw
 * keyframe streams with articulator-weighted easing before rendering; it is
 * deterministic given the same input sequence but is deliberately kept out
 * of the pure render path.
 *
 * This mirrors the host project's face_render.h / face_keyframe.h contract
 * without including those headers so the directory stays self-contained.
 */

enum {
    FMG_WIDTH = 160,
    FMG_HEIGHT = 120,
    FMG_PIXEL_COUNT = FMG_WIDTH * FMG_HEIGHT,
    FMG_FRAME_BYTES = FMG_PIXEL_COUNT * (int)sizeof(uint16_t),
    FMG_SAMPLE_RATE = 16000,
};

/* Byte-for-byte compatible with the host face_keyframe_t wire format. */
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
} fmg_keyframe_t;

enum {
    FMG_FLAG_SPEAKING = 1U << 0,
    FMG_FLAG_BLINKING = 1U << 1,
    FMG_KEYFRAME_BYTES = 12,
};

/* expression carries the host activity state, not an emotion. */
enum {
    FMG_ACTIVITY_IDLE = 0,
    FMG_ACTIVITY_LISTENING = 1,
    FMG_ACTIVITY_THINKING = 2,
    FMG_ACTIVITY_SPEAKING = 3,
};

typedef enum {
    /* Sprite systems */
    FMG_PROFILE_PRESTON_SPRITES = 0,
    FMG_PROFILE_TALKIE_FLAP,
    FMG_PROFILE_MANGA_SNAP,
    FMG_PROFILE_DOT_MATRIX,
    /* Polygon systems */
    FMG_PROFILE_POLYGON_JALI,
    FMG_PROFILE_BEZIER_RIBBON,
    FMG_PROFILE_ORIGAMI_MASK,
    FMG_PROFILE_TEETH_TONGUE,
    /* Alternatives */
    FMG_PROFILE_LED_VU_MOUTH,
    FMG_PROFILE_SCOPE_TRACE,
    FMG_PROFILE_COUNT,
} fmg_profile_t;

/* Matches the host face_render_mouth_kind_t values. */
typedef enum {
    FMG_MOUTH_KIND_NONE = 0,
    FMG_MOUTH_KIND_SPRITE = 1,
    FMG_MOUTH_KIND_ELLIPSE = 2,
    FMG_MOUTH_KIND_POLYGON = 3,
    FMG_MOUTH_KIND_LINE = 4,
    FMG_MOUTH_KIND_SEGMENTS = 5,
    FMG_MOUTH_KIND_SDF = 6,
} fmg_mouth_kind_t;

/* Same 16-byte layout as the host face_render_info_t. */
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
} fmg_info_t;

enum {
    FMG_INFO_FLAG_PIXELATED = 1U << 0,
    FMG_INFO_FLAG_SPRITE_MOUTH = 1U << 3,
    FMG_INFO_FLAG_POLYGON_MOUTH = 1U << 4,
    FMG_INFO_FLAG_IDLE_MOTION = 1U << 5,
};

size_t fmg_profile_count(void);
const char *fmg_profile_slug(fmg_profile_t profile);
const char *fmg_profile_name(fmg_profile_t profile);
bool fmg_profile_info(fmg_profile_t profile, fmg_info_t *info);

/*
 * Render one full 160x120 RGB565 frame. Pure: identical inputs always give
 * identical pixels. `sample_clock` counts 16 kHz PCM samples and also drives
 * all deterministic idle motion (blinks, saccades, brows, breathing).
 * Returns false on NULL/short buffer or unknown profile.
 */
bool fmg_render_frame(
    fmg_profile_t profile,
    const fmg_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

/*
 * Optional coarticulation stage, kept separate from the pure renderers.
 *
 * Feed raw keyframes in stream order; it emits an articulator-smoothed
 * keyframe modelling jaw inertia (slow), lip corner travel (medium) and
 * lip compression (fast), with bilabial closures (press) dominating jaw
 * opening the way plosives visually interrupt vowels. Deterministic: the
 * same reset + sequence of (keyframe, clock) calls yields the same outputs
 * on every platform. State is caller-owned; no allocation.
 */
typedef struct {
    int32_t open_q8;
    int32_t width_q8;
    int32_t round_q8;
    int32_t press_q8;
    int32_t teeth_q8;
    uint32_t last_clock;
    bool primed;
} fmg_coart_t;

void fmg_coart_reset(fmg_coart_t *state);
void fmg_coart_apply(
    fmg_coart_t *state,
    const fmg_keyframe_t *in,
    uint32_t sample_clock,
    fmg_keyframe_t *out);

#ifdef __cplusplus
}
#endif
