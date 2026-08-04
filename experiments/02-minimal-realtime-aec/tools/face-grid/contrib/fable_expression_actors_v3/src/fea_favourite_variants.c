#include "fea_favourite_variants_internal.h"

/*
 * Isolated registry for the nine artist-pass descendants.  Keeping this
 * table out of fea_registry.c lets the main face-grid choose stable global
 * enum values without perturbing the five original FEA actors.
 */

typedef struct {
    const char *slug;
    const char *name;
    const char *thesis;
    fea_favourite_lineage_t lineage;
    uint16_t ops_per_pixel;
} fv_profile_entry_t;

enum {
    FV_MOUTH_POLYGON = 3,
    FV_FLAG_EYE_FOCUS = 1U << 2,
    FV_FLAG_POLYGON_MOUTH = 1U << 4,
    FV_FLAG_IDLE_MOTION = 1U << 5,
};

static const fv_profile_entry_t FV_PROFILES[FEA_FAVOURITE_PROFILE_COUNT] = {
    [FEA_FAVOURITE_LANTERN_BLOOM] = {
        "fea-lantern-bloom",
        "Lantern Bloom",
        "A bell-flame spirit whose petal corona and luminous body "
            "bloom, droop and contract with the performance.",
        FEA_FAVOURITE_LINEAGE_WILL_O_WISP,
        15,
    },
    [FEA_FAVOURITE_COMET_RASCAL] = {
        "fea-comet-rascal",
        "Comet Rascal",
        "A swept, asymmetric comet actor whose ribbon tail becomes "
            "an emphatic line of action.",
        FEA_FAVOURITE_LINEAGE_WILL_O_WISP,
        14,
    },
    [FEA_FAVOURITE_MOON_MOTH_ORACLE] = {
        "fea-moon-moth-oracle",
        "Moon-Moth Oracle",
        "A winged lantern-moth whose crescent wings fold and flare "
            "like a readable emotional cape.",
        FEA_FAVOURITE_LINEAGE_WILL_O_WISP,
        16,
    },
    [FEA_FAVOURITE_GILDED_NOH] = {
        "fea-gilded-noh",
        "Gilded Noh",
        "A layered lacquer-and-brass theatre mask with fan eyelids, "
            "cheek plates and a visibly hinged jaw.",
        FEA_FAVOURITE_LINEAGE_KARAKURI_BRASS,
        16,
    },
    [FEA_FAVOURITE_BEETLE_AUTOMATON] = {
        "fea-beetle-automaton",
        "Beetle Automaton",
        "A domed clockwork beetle whose antenna brows, lens eyes and "
            "mandibles turn facial acting into mechanics.",
        FEA_FAVOURITE_LINEAGE_KARAKURI_BRASS,
        16,
    },
    [FEA_FAVOURITE_KINTSUGI_MARIONETTE] = {
        "fea-kintsugi-marionette",
        "Kintsugi Marionette",
        "A cracked porcelain puppet with gold seams, floating cheek "
            "plates, pin joints and a ribbon-like jaw.",
        FEA_FAVOURITE_LINEAGE_KARAKURI_BRASS,
        17,
    },
    [FEA_FAVOURITE_POP_BURST] = {
        "fea-pop-burst",
        "Pop Burst",
        "A die-cut comic reaction sticker whose starburst silhouette "
            "and orbiting glyphs punch every emotion.",
        FEA_FAVOURITE_LINEAGE_EMOTE_STICKER,
        14,
    },
    [FEA_FAVOURITE_FELT_PATCH_PAL] = {
        "fea-felt-patch-pal",
        "Felt Patch Pal",
        "A scalloped embroidered patch with button-and-stitch features, "
            "applique cheeks, folds and a loose acting thread.",
        FEA_FAVOURITE_LINEAGE_EMOTE_STICKER,
        16,
    },
    [FEA_FAVOURITE_SPEECH_BUBBLE_SPRITE] = {
        "fea-speech-bubble-sprite",
        "Speech-Bubble Sprite",
        "A living comic balloon whose body, tail and punctuation all "
            "lean into the line delivery.",
        FEA_FAVOURITE_LINEAGE_EMOTE_STICKER,
        15,
    },
};

static bool fv_profile_valid(fea_favourite_profile_t profile)
{
    return (unsigned)profile < FEA_FAVOURITE_PROFILE_COUNT;
}

size_t fea_favourite_profile_count(void)
{
    return FEA_FAVOURITE_PROFILE_COUNT;
}

const char *fea_favourite_profile_slug(fea_favourite_profile_t profile)
{
    if (!fv_profile_valid(profile)) {
        return NULL;
    }
    return FV_PROFILES[profile].slug;
}

const char *fea_favourite_profile_name(fea_favourite_profile_t profile)
{
    if (!fv_profile_valid(profile)) {
        return NULL;
    }
    return FV_PROFILES[profile].name;
}

const char *fea_favourite_profile_thesis(fea_favourite_profile_t profile)
{
    if (!fv_profile_valid(profile)) {
        return NULL;
    }
    return FV_PROFILES[profile].thesis;
}

bool fea_favourite_profile_lineage(
    fea_favourite_profile_t profile,
    fea_favourite_lineage_t *lineage)
{
    if (!fv_profile_valid(profile) || lineage == NULL) {
        return false;
    }
    *lineage = FV_PROFILES[profile].lineage;
    return true;
}

bool fea_favourite_profile_info(
    fea_favourite_profile_t profile,
    fea_info_t *info)
{
    if (!fv_profile_valid(profile) || info == NULL) {
        return false;
    }
    info->width = FEA_FRAME_WIDTH;
    info->height = FEA_FRAME_HEIGHT;
    info->work_width = FEA_FRAME_WIDTH;
    info->work_height = FEA_FRAME_HEIGHT;
    info->framebuffer_bytes = FEA_FRAME_BYTES;
    info->family = 5;
    info->mouth_kind = FV_MOUTH_POLYGON;
    info->flags = FV_FLAG_EYE_FOCUS | FV_FLAG_POLYGON_MOUTH |
        FV_FLAG_IDLE_MOTION;
    info->reserved = 0;
    info->estimated_ops_per_pixel = FV_PROFILES[profile].ops_per_pixel;
    return true;
}

bool fea_favourite_probe(
    fea_favourite_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fea_probe_t *probe)
{
    if (!fv_profile_valid(profile) || key == NULL || probe == NULL) {
        return false;
    }
    fea_pose_t pose;
    fea_favourite_layout_t layout;
    fea_solve(key, sample_clock, &pose);
    fea_favourite_layout_build(profile, &pose, sample_clock, &layout);
    fea_favourite_layout_probe(profile, &pose, &layout, probe);
    return true;
}

bool fea_favourite_render_frame(
    fea_favourite_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!fv_profile_valid(profile) || key == NULL || rgb565 == NULL ||
        pixel_capacity < (size_t)FEA_PIXEL_COUNT) {
        return false;
    }

    fea_pose_t pose;
    fea_canvas_t canvas = { rgb565 };
    fea_solve(key, sample_clock, &pose);

    if (profile <= FEA_FAVOURITE_MOON_MOTH_ORACLE) {
        fea_favourite_wisp_render(
            profile, &pose, sample_clock, &canvas);
    } else if (profile <= FEA_FAVOURITE_KINTSUGI_MARIONETTE) {
        fea_favourite_karakuri_render(
            profile, &pose, sample_clock, &canvas);
    } else {
        fea_favourite_sticker_render(
            profile, &pose, sample_clock, &canvas);
    }
    return true;
}
