#include "face_pixel_pack_internal.h"

typedef struct {
    const char *slug;
    const char *name;
    uint32_t salt;
    face_pixel_pack_style_t style;
    fpp_renderer_t render;
} pixel_profile_t;

static const pixel_profile_t PROFILES[FACE_PIXEL_PACK_PROFILE_COUNT] = {
    [FACE_PIXEL_PACK_EGA_QUEST] = {
        "pixel-ega-quest",
        "Pixel / EGA Quest",
        0xe6a00001U,
        {
            80, 60, 2, 2, FACE_PIXEL_PACK_MOUTH_SPRITES, 10, 16,
            "fixed EGA 16-colour palette",
            "checkerboard shade pairs",
        },
        fpp_render_ega_quest,
    },
    [FACE_PIXEL_PACK_VGA_ELDER] = {
        "pixel-vga-elder",
        "Pixel / VGA Elder",
        0x06a00002U,
        {
            160, 120, 1, 1, FACE_PIXEL_PACK_MOUTH_POLYGON, 0, 26,
            "hand-picked skin, beard, and robe ramps",
            "nested banded shading",
        },
        fpp_render_vga_elder,
    },
    [FACE_PIXEL_PACK_TALKIE_CLOSEUP] = {
        "pixel-talkie-closeup",
        "Pixel / Talkie Closeup",
        0x7a1c0003U,
        {
            80, 60, 2, 2, FACE_PIXEL_PACK_MOUTH_SPRITES, 10, 23,
            "flat talkie fills with dark outlines",
            "checkerboard stubble",
        },
        fpp_render_talkie_closeup,
    },
    [FACE_PIXEL_PACK_DITHERED_ROGUE] = {
        "pixel-dithered-rogue",
        "Pixel / Dithered Rogue",
        0xd0060007U,
        {
            160, 120, 1, 1, FACE_PIXEL_PACK_MOUTH_POLYGON, 0, 2,
            "one-bit ink on near-black",
            "ordered Bayer 8x8 over grayscale",
        },
        fpp_render_dithered_rogue,
    },
};

static const pixel_profile_t *profile_get(face_pixel_pack_profile_t profile)
{
    if ((int)profile < 0 || profile >= FACE_PIXEL_PACK_PROFILE_COUNT) {
        return NULL;
    }
    return &PROFILES[profile];
}

size_t face_pixel_pack_profile_count(void)
{
    return FACE_PIXEL_PACK_PROFILE_COUNT;
}

const char *face_pixel_pack_profile_slug(face_pixel_pack_profile_t profile)
{
    const pixel_profile_t *entry = profile_get(profile);
    return entry != NULL ? entry->slug : NULL;
}

const char *face_pixel_pack_profile_name(face_pixel_pack_profile_t profile)
{
    const pixel_profile_t *entry = profile_get(profile);
    return entry != NULL ? entry->name : NULL;
}

bool face_pixel_pack_profile_style(
    face_pixel_pack_profile_t profile,
    face_pixel_pack_style_t *style)
{
    const pixel_profile_t *entry = profile_get(profile);
    if (entry == NULL || style == NULL) {
        return false;
    }
    *style = entry->style;
    return true;
}

bool face_pixel_pack_render(
    face_pixel_pack_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    return face_pixel_pack_render_checked(
        profile, render_key, sample_clock, rgb565, pixel_capacity, NULL);
}

bool face_pixel_pack_render_checked(
    face_pixel_pack_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_pixel_pack_landmarks_t *landmarks)
{
    const pixel_profile_t *entry = profile_get(profile);
    if (entry == NULL || render_key == NULL ||
        rgb565 == NULL ||
        pixel_capacity < (size_t)FACE_PIXEL_PACK_PIXEL_COUNT) {
        return false;
    }
    if (entry->render != NULL) {
        fpp_pose_t pose;
        fpp_resolve_pose(
            render_key, sample_clock, entry->salt, &pose);
        entry->render(
            rgb565, &pose, sample_clock, landmarks);
        return true;
    }
    for (size_t index = 0U;
         index < (size_t)FACE_PIXEL_PACK_PIXEL_COUNT;
         ++index) {
        rgb565[index] = 0U;
    }
    if (landmarks != NULL) {
        *landmarks = (face_pixel_pack_landmarks_t){
            {32, 8, 96, 104},
            {47, 40, 24, 18},
            {89, 40, 24, 18},
            {54, 72, 52, 28},
        };
    }
    return true;
}
