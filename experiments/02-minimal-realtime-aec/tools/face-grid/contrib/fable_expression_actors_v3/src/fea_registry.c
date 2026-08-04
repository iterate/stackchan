#include "fea_internal.h"

/*
 * Profile registry and public entry points. Renderers are pure
 * functions of (profile, key, clock); the solver runs once and the
 * actor turns the resolved pose into pixels.
 */

typedef struct {
    const char *slug;
    const char *name;
    fea_actor_render_fn render;
    fea_actor_probe_fn probe;
    uint8_t mouth_kind;         /* face_render_mouth_kind_t values */
    uint8_t flags;              /* face_render.h flag values */
    uint16_t ops_per_pixel;
} fea_profile_entry_t;

/* face_render.h numeric values, restated locally so the pack builds
 * standalone; INTEGRATION.md maps them back symbolically. */
enum {
    FEA_MOUTH_NONE = 0,
    FEA_MOUTH_POLYGON = 3,
    FEA_FLAG_EYE_FOCUS = 1U << 2,
    FEA_FLAG_POLYGON_MOUTH = 1U << 4,
    FEA_FLAG_IDLE_MOTION = 1U << 5,
    FEA_FLAG_NO_MOUTH = 1U << 7,
};

static const fea_profile_entry_t PROFILES[FEA_PROFILE_COUNT] = {
    [FEA_PROFILE_MOCHI_CAT] = {
        "fea-mochi-cat", "Mochi Cat (plush mascot)",
        fea_mochi_render, fea_mochi_probe,
        FEA_MOUTH_POLYGON,
        FEA_FLAG_EYE_FOCUS | FEA_FLAG_POLYGON_MOUTH |
            FEA_FLAG_IDLE_MOTION,
        11,
    },
    [FEA_PROFILE_KARAKURI_BRASS] = {
        "fea-karakuri-brass", "Karakuri Brass (plate puppet)",
        fea_karakuri_render, fea_karakuri_probe,
        FEA_MOUTH_POLYGON,
        FEA_FLAG_EYE_FOCUS | FEA_FLAG_POLYGON_MOUTH |
            FEA_FLAG_IDLE_MOTION,
        12,
    },
    [FEA_PROFILE_EMOTE_STICKER] = {
        "fea-emote-sticker", "Emote Sticker (badge face)",
        fea_sticker_render, fea_sticker_probe,
        FEA_MOUTH_POLYGON,
        FEA_FLAG_EYE_FOCUS | FEA_FLAG_POLYGON_MOUTH |
            FEA_FLAG_IDLE_MOTION,
        11,
    },
    [FEA_PROFILE_WILL_O_WISP] = {
        "fea-will-o-wisp", "Will-o-Wisp (night spirit)",
        fea_wisp_render, fea_wisp_probe,
        FEA_MOUTH_POLYGON,
        FEA_FLAG_EYE_FOCUS | FEA_FLAG_POLYGON_MOUTH |
            FEA_FLAG_IDLE_MOTION,
        13,
    },
    [FEA_PROFILE_MONO_SCOPE] = {
        "fea-mono-scope", "Mono Scope (cyclops robot)",
        fea_scope_render, fea_scope_probe,
        FEA_MOUTH_NONE,
        FEA_FLAG_EYE_FOCUS | FEA_FLAG_IDLE_MOTION |
            FEA_FLAG_NO_MOUTH,
        12,
    },
};

size_t fea_profile_count(void)
{
    return FEA_PROFILE_COUNT;
}

const char *fea_profile_slug(fea_profile_t profile)
{
    if ((unsigned)profile >= FEA_PROFILE_COUNT) {
        return NULL;
    }
    return PROFILES[profile].slug;
}

const char *fea_profile_name(fea_profile_t profile)
{
    if ((unsigned)profile >= FEA_PROFILE_COUNT) {
        return NULL;
    }
    return PROFILES[profile].name;
}

bool fea_profile_info(fea_profile_t profile, fea_info_t *info)
{
    if ((unsigned)profile >= FEA_PROFILE_COUNT || info == NULL) {
        return false;
    }
    info->width = FEA_FRAME_WIDTH;
    info->height = FEA_FRAME_HEIGHT;
    info->work_width = FEA_FRAME_WIDTH;
    info->work_height = FEA_FRAME_HEIGHT;
    info->framebuffer_bytes = FEA_FRAME_BYTES;
    info->family = 5;              /* FACE_RENDER_FAMILY_TOON slot */
    info->mouth_kind = PROFILES[profile].mouth_kind;
    info->flags = PROFILES[profile].flags;
    info->reserved = 0;
    info->estimated_ops_per_pixel = PROFILES[profile].ops_per_pixel;
    return true;
}

bool fea_probe(
    fea_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fea_probe_t *probe)
{
    if ((unsigned)profile >= FEA_PROFILE_COUNT || key == NULL ||
        probe == NULL) {
        return false;
    }
    fea_pose_t pose;
    fea_solve(key, sample_clock, &pose);
    PROFILES[profile].probe(&pose, sample_clock, probe);
    return true;
}

bool fea_render_frame(
    fea_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)profile >= FEA_PROFILE_COUNT || key == NULL ||
        rgb565 == NULL || pixel_capacity < (size_t)FEA_PIXEL_COUNT) {
        return false;
    }
    fea_pose_t pose;
    fea_solve(key, sample_clock, &pose);
    fea_canvas_t canvas = { rgb565 };
    PROFILES[profile].render(&pose, sample_clock, &canvas);
    return true;
}
