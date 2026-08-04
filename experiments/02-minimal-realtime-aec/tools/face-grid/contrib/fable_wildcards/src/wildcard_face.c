#include "wildcard_face.h"

#include "wc_common.h"

typedef void (*wc_render_fn)(
    const wc_keyframe_t *, const wc_rig_t *, uint32_t, uint16_t *);

typedef struct {
    const char *slug;
    const char *name;
    wc_render_fn fn;
    uint16_t estimated_ops_per_pixel;
} wc_profile_entry_t;

static const wc_profile_entry_t WC_PROFILES[WC_PROFILE_COUNT] = {
    [WC_PROFILE_SCOPE_BEAM] = {
        "wc-scope-beam", "Oscillofluor Beam",
        wc_render_scope_beam, 22,
    },
    [WC_PROFILE_FLIPDOT_CASCADE] = {
        "wc-flipdot-cascade", "Flip-Dot Cascade",
        wc_render_flipdot_cascade, 18,
    },
    [WC_PROFILE_CHLADNI_SAND] = {
        "wc-chladni-sand", "Chladni Voiceplate",
        wc_render_chladni_sand, 34,
    },
    [WC_PROFILE_HALFTONE_PRESS] = {
        "wc-halftone-press", "Halftone Press",
        wc_render_halftone_press, 40,
    },
    [WC_PROFILE_WAYANG_LAMP] = {
        "wc-wayang-lamp", "Wayang Lamp",
        wc_render_wayang_lamp, 30,
    },
    [WC_PROFILE_FERRO_POOL] = {
        "wc-ferro-pool", "Ferro Pool",
        wc_render_ferro_pool, 36,
    },
    [WC_PROFILE_TELETEXT_SEXTANT] = {
        "wc-teletext-sextant", "Teletext Sextant",
        wc_render_teletext_sextant, 14,
    },
};

size_t wc_profile_count(void) {
    return WC_PROFILE_COUNT;
}

const char *wc_profile_slug(wc_profile_t profile) {
    if ((unsigned)profile >= WC_PROFILE_COUNT) {
        return "";
    }
    return WC_PROFILES[profile].slug;
}

const char *wc_profile_name(wc_profile_t profile) {
    if ((unsigned)profile >= WC_PROFILE_COUNT) {
        return "";
    }
    return WC_PROFILES[profile].name;
}

bool wc_profile_info(wc_profile_t profile, wc_render_info_t *info) {
    if ((unsigned)profile >= WC_PROFILE_COUNT || !info) {
        return false;
    }
    info->width = WC_FACE_WIDTH;
    info->height = WC_FACE_HEIGHT;
    info->work_width = WC_FACE_WIDTH;
    info->work_height = WC_FACE_HEIGHT;
    info->framebuffer_bytes = WC_FACE_FRAME_BYTES;
    info->family = 5; /* one past FACE_RENDER_FAMILY_CYBER: proposed WILDCARD */
    info->mouth_kind = 0;
    info->flags = 0;
    info->reserved = 0;
    info->estimated_ops_per_pixel = WC_PROFILES[profile].estimated_ops_per_pixel;
    return true;
}

bool wc_render_frame(
    wc_profile_t profile,
    const wc_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity) {
    if ((unsigned)profile >= WC_PROFILE_COUNT || !keyframe || !rgb565 ||
        pixel_capacity < (size_t)WC_FACE_PIXEL_COUNT) {
        return false;
    }
    wc_rig_t rig;
    wc_rig_derive(keyframe, sample_clock, &rig);
    WC_PROFILES[profile].fn(keyframe, &rig, sample_clock, rgb565);
    return true;
}
