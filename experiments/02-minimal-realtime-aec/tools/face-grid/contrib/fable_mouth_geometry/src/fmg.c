#include "fmg_internal.h"

typedef void (*fmg_render_fn)(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px);

typedef struct {
    const char *slug;
    const char *name;
    fmg_render_fn render;
    uint8_t mouth_kind;
    uint8_t flags;
    uint16_t estimated_ops_per_pixel;
} fmg_profile_desc_t;

static const fmg_profile_desc_t s_profiles[FMG_PROFILE_COUNT] = {
    [FMG_PROFILE_PRESTON_SPRITES] = {
        "fmg-preston-sprites", "Preston Blair sprites", fmg_render_preston,
        FMG_MOUTH_KIND_SPRITE,
        FMG_INFO_FLAG_SPRITE_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 6},
    [FMG_PROFILE_TALKIE_FLAP] = {
        "fmg-talkie-flap", "EGA talkie flap", fmg_render_talkie,
        FMG_MOUTH_KIND_SPRITE,
        FMG_INFO_FLAG_SPRITE_MOUTH | FMG_INFO_FLAG_IDLE_MOTION |
            FMG_INFO_FLAG_PIXELATED, 4},
    [FMG_PROFILE_MANGA_SNAP] = {
        "fmg-manga-snap", "Manga snap", fmg_render_manga,
        FMG_MOUTH_KIND_SPRITE,
        FMG_INFO_FLAG_SPRITE_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 5},
    [FMG_PROFILE_DOT_MATRIX] = {
        "fmg-dot-matrix", "Flip-dot matrix", fmg_render_dotmatrix,
        FMG_MOUTH_KIND_SPRITE,
        FMG_INFO_FLAG_SPRITE_MOUTH | FMG_INFO_FLAG_IDLE_MOTION |
            FMG_INFO_FLAG_PIXELATED, 7},
    [FMG_PROFILE_POLYGON_JALI] = {
        "fmg-polygon-jali", "JALI jaw-lip polygon", fmg_render_jali,
        FMG_MOUTH_KIND_POLYGON,
        FMG_INFO_FLAG_POLYGON_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 9},
    [FMG_PROFILE_BEZIER_RIBBON] = {
        "fmg-bezier-ribbon", "Bezier ribbon lips", fmg_render_ribbon,
        FMG_MOUTH_KIND_POLYGON,
        FMG_INFO_FLAG_POLYGON_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 10},
    [FMG_PROFILE_ORIGAMI_MASK] = {
        "fmg-origami-mask", "Origami mask", fmg_render_origami,
        FMG_MOUTH_KIND_POLYGON,
        FMG_INFO_FLAG_POLYGON_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 11},
    [FMG_PROFILE_TEETH_TONGUE] = {
        "fmg-teeth-tongue", "Teeth & tongue closeup", fmg_render_teeth,
        FMG_MOUTH_KIND_POLYGON,
        FMG_INFO_FLAG_POLYGON_MOUTH | FMG_INFO_FLAG_IDLE_MOTION, 9},
    [FMG_PROFILE_LED_VU_MOUTH] = {
        "fmg-led-vu-mouth", "LED VU mouth", fmg_render_ledvu,
        FMG_MOUTH_KIND_SEGMENTS,
        FMG_INFO_FLAG_IDLE_MOTION | FMG_INFO_FLAG_PIXELATED, 5},
    [FMG_PROFILE_SCOPE_TRACE] = {
        "fmg-scope-trace", "Oscilloscope trace", fmg_render_scope,
        FMG_MOUTH_KIND_LINE, FMG_INFO_FLAG_IDLE_MOTION, 8},
};

size_t fmg_profile_count(void)
{
    return FMG_PROFILE_COUNT;
}

const char *fmg_profile_slug(fmg_profile_t profile)
{
    if ((unsigned)profile >= FMG_PROFILE_COUNT) {
        return NULL;
    }
    return s_profiles[profile].slug;
}

const char *fmg_profile_name(fmg_profile_t profile)
{
    if ((unsigned)profile >= FMG_PROFILE_COUNT) {
        return NULL;
    }
    return s_profiles[profile].name;
}

bool fmg_profile_info(fmg_profile_t profile, fmg_info_t *info)
{
    if ((unsigned)profile >= FMG_PROFILE_COUNT || info == NULL) {
        return false;
    }
    const fmg_profile_desc_t *d = &s_profiles[profile];
    info->width = FMG_WIDTH;
    info->height = FMG_HEIGHT;
    info->work_width = FMG_WIDTH;
    info->work_height = FMG_HEIGHT;
    info->framebuffer_bytes = FMG_FRAME_BYTES;
    info->family = 3; /* FACE_RENDER_FAMILY_MOUTH in the host enum */
    info->mouth_kind = d->mouth_kind;
    info->flags = d->flags;
    info->reserved = 0;
    info->estimated_ops_per_pixel = d->estimated_ops_per_pixel;
    return true;
}

bool fmg_render_frame(
    fmg_profile_t profile,
    const fmg_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)profile >= FMG_PROFILE_COUNT || keyframe == NULL ||
        rgb565 == NULL || pixel_capacity < (size_t)FMG_PIXEL_COUNT) {
        return false;
    }
    s_profiles[profile].render(keyframe, sample_clock, rgb565);
    return true;
}
