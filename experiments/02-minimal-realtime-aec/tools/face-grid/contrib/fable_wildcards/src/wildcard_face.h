#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Wildcard visual-language prototypes. Standalone mirror of the production
 * renderer contract in firmware-ws/main/face_render.h: one 160x120 RGB565
 * frame, pure function of a 12-byte semantic keyframe plus a 16 kHz sample
 * clock, integer arithmetic only, caller-owned buffers, no allocation and no
 * retained state. Field names and layout match face_keyframe_t exactly so
 * integration is a typedef swap.
 */
enum {
    WC_FACE_WIDTH = 160,
    WC_FACE_HEIGHT = 120,
    WC_FACE_PIXEL_COUNT = WC_FACE_WIDTH * WC_FACE_HEIGHT,
    WC_FACE_FRAME_BYTES = WC_FACE_PIXEL_COUNT * (int)sizeof(uint16_t),
    WC_SAMPLE_RATE_HZ = 16000,
};

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
} wc_keyframe_t;

enum {
    WC_KEYFRAME_FLAG_SPEAKING = 1U << 0,
    WC_KEYFRAME_FLAG_BLINKING = 1U << 1,
    WC_KEYFRAME_BYTES = 12,
};

_Static_assert(
    sizeof(wc_keyframe_t) == WC_KEYFRAME_BYTES,
    "wildcard keyframe must mirror the 12-byte face_keyframe_t wire format");

typedef enum {
    /* XY vector CRT: one continuous beam path, the mouth is a synthesized
     * voice waveform, deterministic two-layer P7 phosphor persistence. */
    WC_PROFILE_SCOPE_BEAM = 0,
    /* Electromechanical flip-disc panel: row-scan refresh latency, discs
     * caught mid-flip render foreshortened, blink rides the scan cascade. */
    WC_PROFILE_FLIPDOT_CASCADE,
    /* Cymatic sand plate: mouth shape selects Chladni resonance modes, sand
     * settles on nodal lines that bend around the eyes and mouth. */
    WC_PROFILE_CHLADNI_SAND,
    /* Comic letterpress: rotated AM halftone screens, a misregistered red
     * spot-color plate, brush-weight ink outlines, emphasis lines. */
    WC_PROFILE_HALFTONE_PRESS,
    /* Shadow-puppet theatre: perforated silhouette with a hinged jaw, voice
     * escapes as lamp light through the mouth, oil-lamp flicker. */
    WC_PROFILE_WAYANG_LAMP,
    /* Ferrofluid dish: Rosensweig spike crown from three-wave hexagonal
     * interference, glossy droplet eyes that sink into the pool on blink. */
    WC_PROFILE_FERRO_POOL,
    /* Teletext receiver: authentic 2x3 mosaic sextants, one foreground color
     * per cell (attribute clash), live page header and flash attribute. */
    WC_PROFILE_TELETEXT_SEXTANT,
    WC_PROFILE_COUNT,
} wc_profile_t;

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
} wc_render_info_t;

size_t wc_profile_count(void);
const char *wc_profile_slug(wc_profile_t profile);
const char *wc_profile_name(wc_profile_t profile);
bool wc_profile_info(wc_profile_t profile, wc_render_info_t *info);

/*
 * Render exactly WC_FACE_WIDTH x WC_FACE_HEIGHT RGB565 pixels into the
 * caller's buffer. `sample_clock` counts 16 kHz PCM samples and is the only
 * time source; equal inputs always produce byte-identical frames, on native
 * hosts, the ESP32-S3 and WebAssembly alike.
 */
bool wc_render_frame(
    wc_profile_t profile,
    const wc_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
