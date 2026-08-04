#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-point cyberpunk software-shader faces.
 *
 * Standalone contribution: this header intentionally does not include any
 * firmware header. `cyber_keyframe_t` mirrors the stable 12-byte
 * `face_keyframe_t` wire format byte for byte, so an integration shim can
 * cast or memcpy between the two. The renderer is a pure function of
 * (profile, keyframe, 16 kHz sample clock); identical inputs produce
 * byte-identical RGB565 frames on ESP32-S3, host, and WebAssembly because
 * every code path is integer-only.
 *
 * The caller owns all memory. `cyber_face_ctx_t` holds init-time lookup
 * tables plus one frame-transient scratch plane; nothing is allocated and
 * nothing except scratch is written after cyber_face_init().
 */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CYBER_FACE_WIDTH = 160,
    CYBER_FACE_HEIGHT = 120,
    CYBER_FACE_PIXEL_COUNT = CYBER_FACE_WIDTH * CYBER_FACE_HEIGHT,
    CYBER_FACE_FRAME_BYTES = CYBER_FACE_PIXEL_COUNT * (int)sizeof(uint16_t),
    CYBER_FACE_SAMPLE_RATE = 16000,
};

/*
 * Mirrors face_render_profile_t values FACE_RENDER_NEON_SDF_CYAN..
 * FACE_RENDER_PALETTE_PLASMA (the FACE_RENDER_FAMILY_CYBER block).
 */
typedef enum {
    CYBER_PROFILE_NEON_SDF_CYAN = 0,
    CYBER_PROFILE_NEON_SDF_MAGENTA,
    CYBER_PROFILE_LIQUID_SMIN,
    CYBER_PROFILE_CRT_CHROMATIC,
    CYBER_PROFILE_HOLO_WIREFRAME,
    CYBER_PROFILE_VOICE_ORB,
    CYBER_PROFILE_RED_OPTIC,
    CYBER_PROFILE_HUB75_NEON,
    CYBER_PROFILE_EDGE_GLOW,
    CYBER_PROFILE_GLITCH_MASK,
    CYBER_PROFILE_PALETTE_PLASMA,
    CYBER_PROFILE_COUNT,
} cyber_profile_t;

/* Byte-for-byte mirror of the stable 12-byte face_keyframe_t. */
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
} cyber_keyframe_t;

enum {
    CYBER_KEYFRAME_FLAG_SPEAKING = 1U << 0,
    CYBER_KEYFRAME_FLAG_BLINKING = 1U << 1,
    CYBER_KEYFRAME_BYTES = 12,
};

/* Expression bytes the renderer reacts to; unknown values render neutral. */
enum {
    CYBER_EXPRESSION_NEUTRAL = 0,
    CYBER_EXPRESSION_HAPPY = 1,
    CYBER_EXPRESSION_SAD = 2,
    CYBER_EXPRESSION_ANGRY = 3,
    CYBER_EXPRESSION_SURPRISED = 4,
};

/* Layout-compatible with the firmware's 16-byte face_render_info_t. */
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
} cyber_face_info_t;

enum {
    CYBER_SIN_TABLE_SIZE = 1024,        /* full turn, int16 Q14 */
    CYBER_GLOW_TABLE_SIZE = 512,        /* |d| in Q4 -> intensity Q8 */
    CYBER_PALETTE_SIZE = 256,
    CYBER_FIELD_WIDTH = 80,
    CYBER_FIELD_HEIGHT = 60,
    CYBER_LED_WIDTH = 40,
    CYBER_LED_HEIGHT = 30,
};

typedef struct {
    int16_t sin_q14[CYBER_SIN_TABLE_SIZE];
    uint8_t glow_core[CYBER_GLOW_TABLE_SIZE];   /* tight falloff  */
    uint8_t glow_neon[CYBER_GLOW_TABLE_SIZE];   /* medium falloff */
    uint8_t glow_soft[CYBER_GLOW_TABLE_SIZE];   /* wide falloff   */
    uint16_t palette[CYBER_PROFILE_COUNT][CYBER_PALETTE_SIZE];
    uint16_t led_palette[4][CYBER_PALETTE_SIZE]; /* HUB75 cell shading */
    uint8_t vignette_x[CYBER_FIELD_WIDTH];
    uint8_t vignette_y[CYBER_FIELD_HEIGHT];
    /*
     * Frame-transient scratch. Reused as an 80x60 or 40x30 brightness
     * field, or a 160x120 additive accumulation plane, depending on the
     * profile. Fully rewritten every frame; never read across frames.
     */
    uint8_t scratch[CYBER_FACE_PIXEL_COUNT];
    uint32_t magic;
} cyber_face_ctx_t;

/* Builds every lookup table. Integer-only; call once before rendering. */
void cyber_face_init(cyber_face_ctx_t *ctx);

size_t cyber_face_profile_count(void);
const char *cyber_face_profile_slug(cyber_profile_t profile);
const char *cyber_face_profile_name(cyber_profile_t profile);
bool cyber_face_profile_info(cyber_profile_t profile,
                             cyber_face_info_t *info);

/*
 * Render exactly CYBER_FACE_WIDTH x CYBER_FACE_HEIGHT RGB565 pixels.
 * `sample_clock` counts 16 kHz PCM samples and is the only time source;
 * blinks, saccades, breathing, orbits, and glitch events all derive from
 * it deterministically. Returns false on bad arguments only.
 */
bool cyber_face_render(cyber_face_ctx_t *ctx,
                       cyber_profile_t profile,
                       const cyber_keyframe_t *keyframe,
                       uint32_t sample_clock,
                       uint16_t *rgb565,
                       size_t pixel_capacity);

#ifdef __cplusplus
}
#endif
