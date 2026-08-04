#include "cyber_internal.h"

/* Mirrors of the firmware's metadata vocabulary (face_render.h). */
enum {
    FAMILY_CYBER = 4,
    MOUTH_NONE = 0,
    MOUTH_ELLIPSE = 2,
    MOUTH_POLYGON = 3,
    MOUTH_LINE = 4,
    MOUTH_SDF = 6,
    FLAG_PIXELATED = 1u << 0,
    FLAG_SHADER = 1u << 1,
    FLAG_IDLE_MOTION = 1u << 5,
    FLAG_HALF_RES = 1u << 6,
    FLAG_NO_MOUTH = 1u << 7,
};

typedef struct {
    const char *slug;
    const char *name;
    uint16_t work_width;
    uint16_t work_height;
    uint8_t mouth_kind;
    uint8_t flags;
    uint16_t estimated_ops_per_pixel;
} profile_meta_t;

/*
 * estimated_ops_per_pixel: conservative estimated ESP32-S3 cycles per
 * output pixel, derived from the measured host benchmark scaled by a
 * pessimistic 100x host-to-S3 factor (see README "Performance"). At
 * 240 MHz and 30 fps the budget is ~416 cycles per output pixel, so
 * every profile sits at least 2x inside budget by this estimate.
 */
static const profile_meta_t profile_meta[CYBER_PROFILE_COUNT] = {
    [CYBER_PROFILE_NEON_SDF_CYAN] = {
        "neon-sdf-cyan", "Neon SDF Cyan", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 143,
    },
    [CYBER_PROFILE_NEON_SDF_MAGENTA] = {
        "neon-sdf-magenta", "Neon SDF Magenta", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 168,
    },
    [CYBER_PROFILE_LIQUID_SMIN] = {
        "liquid-smin", "Liquid Smooth-Min", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 184,
    },
    [CYBER_PROFILE_CRT_CHROMATIC] = {
        "crt-chromatic", "CRT Chromatic", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 180,
    },
    [CYBER_PROFILE_HOLO_WIREFRAME] = {
        "holo-wireframe", "Holo Wireframe", 160, 120,
        MOUTH_POLYGON, FLAG_SHADER | FLAG_IDLE_MOTION, 26,
    },
    [CYBER_PROFILE_VOICE_ORB] = {
        "voice-orb", "Voice Orb", 80, 60,
        MOUTH_NONE,
        FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES | FLAG_NO_MOUTH,
        121,
    },
    [CYBER_PROFILE_RED_OPTIC] = {
        "red-optic", "Red Optic", 80, 60,
        MOUTH_NONE,
        FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES | FLAG_NO_MOUTH,
        83,
    },
    [CYBER_PROFILE_HUB75_NEON] = {
        "hub75-neon", "HUB75 Neon", 40, 30,
        MOUTH_SDF,
        FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_PIXELATED | FLAG_HALF_RES,
        29,
    },
    [CYBER_PROFILE_EDGE_GLOW] = {
        "edge-glow", "Edge Glow", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 99,
    },
    [CYBER_PROFILE_GLITCH_MASK] = {
        "glitch-mask", "Glitch Mask", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 169,
    },
    [CYBER_PROFILE_PALETTE_PLASMA] = {
        "palette-plasma", "Palette Plasma", 80, 60,
        MOUTH_SDF, FLAG_SHADER | FLAG_IDLE_MOTION | FLAG_HALF_RES, 155,
    },
};

size_t cyber_face_profile_count(void)
{
    return CYBER_PROFILE_COUNT;
}

const char *cyber_face_profile_slug(cyber_profile_t profile)
{
    if ((unsigned)profile >= CYBER_PROFILE_COUNT) {
        return NULL;
    }
    return profile_meta[profile].slug;
}

const char *cyber_face_profile_name(cyber_profile_t profile)
{
    if ((unsigned)profile >= CYBER_PROFILE_COUNT) {
        return NULL;
    }
    return profile_meta[profile].name;
}

bool cyber_face_profile_info(cyber_profile_t profile,
                             cyber_face_info_t *info)
{
    if ((unsigned)profile >= CYBER_PROFILE_COUNT || info == NULL) {
        return false;
    }
    const profile_meta_t *meta = &profile_meta[profile];
    info->width = CYBER_FACE_WIDTH;
    info->height = CYBER_FACE_HEIGHT;
    info->work_width = meta->work_width;
    info->work_height = meta->work_height;
    info->framebuffer_bytes = CYBER_FACE_FRAME_BYTES;
    info->family = FAMILY_CYBER;
    info->mouth_kind = meta->mouth_kind;
    info->flags = meta->flags;
    info->reserved = 0;
    info->estimated_ops_per_pixel = meta->estimated_ops_per_pixel;
    return true;
}

bool cyber_face_render(cyber_face_ctx_t *ctx, cyber_profile_t profile,
                       const cyber_keyframe_t *keyframe,
                       uint32_t sample_clock, uint16_t *rgb565,
                       size_t pixel_capacity)
{
    if (ctx == NULL || keyframe == NULL || rgb565 == NULL ||
        (unsigned)profile >= CYBER_PROFILE_COUNT ||
        pixel_capacity < (size_t)CYBER_FACE_PIXEL_COUNT ||
        ctx->magic != CYBER_CTX_MAGIC) {
        return false;
    }
    cyber_motion_t motion;
    cyber_motion_compute(ctx, keyframe, sample_clock, &motion);
    cyber_render_profile(ctx, profile, &motion, keyframe, sample_clock,
                         rgb565);
    return true;
}
