#include "face_avatar_registry.h"

#include "face_performance.h"
#include "face_sprite_mossling_generated.h"
#include "face_sprite_sheet.h"
#include "face_sprite_showcase.h"

typedef struct {
    const char *slug;
    const char *name;
    const face_sprite_atlas_t *atlas;
    uint16_t work_width;
    uint16_t work_height;
    uint8_t flags;
} face_avatar_entry_t;

#include "face_avatar_catalog_generated.inc"

/*
 * These imported sprite experiments remain visible in the browser workbench,
 * but are not framed for the physical StackChan LCD. The generated catalogue
 * above is the complete tap-to-cycle device list.
 */
static const face_avatar_entry_t BROWSER_ONLY_AVATARS[] = {
    {
        "sprite-sheet-pocket-mossling",
        "Pocket Mossling · authored DMG sprite",
        &face_sprite_pocket_mossling_authored_atlas,
        40U,
        30U,
        FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_HALF_RES,
    },
    {
        "sprite-vga-star-navigator",
        "Sprite · VGA star navigator",
        &face_sprite_vga_star_navigator_atlas,
        80U,
        60U,
        FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_HALF_RES,
    },
    {
        "sprite-pocket-relay-creature",
        "Sprite · pocket relay creature",
        &face_sprite_pocket_relay_creature_atlas,
        40U,
        30U,
        FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_HALF_RES,
    },
};

static const face_performance_profile_t SPRITE_PERFORMANCE = {
    .seed = 0x47424f54U,
    .motion_gain = 208U,
    .gaze_gain = 208U,
    .expression_gain = 224U,
    .blink_gain = 240U,
};

static face_sprite_player_t s_player;
static size_t s_current;
static bool s_ready;

static size_t avatar_count(void)
{
    return sizeof(GENERATED_AVATARS) / sizeof(GENERATED_AVATARS[0]);
}

static size_t catalog_count(void)
{
    return avatar_count() +
           sizeof(BROWSER_ONLY_AVATARS) /
               sizeof(BROWSER_ONLY_AVATARS[0]);
}

static const face_avatar_entry_t *catalog_entry(size_t index)
{
    if (index < avatar_count()) {
        return &GENERATED_AVATARS[index];
    }
    index -= avatar_count();
    return index <
                   sizeof(BROWSER_ONLY_AVATARS) /
                       sizeof(BROWSER_ONLY_AVATARS[0])
               ? &BROWSER_ONLY_AVATARS[index]
               : NULL;
}

static int8_t clamp_motion(int8_t value)
{
    if (value < -32) {
        return -32;
    }
    return value > 32 ? 32 : value;
}

static bool prepare_render_key(
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_render_key_t *animated)
{
    if (render_key == NULL || animated == NULL) {
        return false;
    }
    *animated = *render_key;
    if (animated->controls.eye_left_open == 0U &&
        animated->controls.eye_right_open == 0U &&
        (animated->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) == 0U) {
        animated->controls.eye_left_open = 255U;
        animated->controls.eye_right_open = 255U;
    }
    if (!face_performance_apply(
            &SPRITE_PERFORMANCE,
            sample_clock,
            animated,
            NULL)) {
        return false;
    }

    /*
     * The LCD itself is StackChan's head. Keep occasional acting movement,
     * but cap it to one native sprite pixel and never add a second body
     * translation. This restores life without making a portrait float
     * around inside the physical face.
     */
    animated->head_yaw = clamp_motion(animated->head_yaw);
    animated->head_pitch = clamp_motion(animated->head_pitch);
    animated->head_roll = 0;
    animated->body_lean_x = 0;
    animated->body_lean_y = 0;
    return true;
}

static bool select_avatar(size_t index)
{
    if (index >= avatar_count()) {
        return false;
    }

    face_sprite_player_t candidate;
    if (!face_sprite_player_init(
            &candidate, GENERATED_AVATARS[index].atlas)) {
        return false;
    }
    s_player = candidate;
    s_current = index;
    s_ready = true;
    return true;
}

bool face_avatar_registry_init(void)
{
    return s_ready || select_avatar(0U);
}

bool face_avatar_registry_next(void)
{
    if (!s_ready && !face_avatar_registry_init()) {
        return false;
    }
    return select_avatar((s_current + 1U) % avatar_count());
}

size_t face_avatar_registry_count(void)
{
    return avatar_count();
}

size_t face_avatar_registry_current_index(void)
{
    return s_current;
}

const char *face_avatar_registry_current_slug(void)
{
    return GENERATED_AVATARS[s_current].slug;
}

const char *face_avatar_registry_current_name(void)
{
    return GENERATED_AVATARS[s_current].name;
}

const char *face_avatar_registry_slug_at(size_t index)
{
    return index < avatar_count() ? GENERATED_AVATARS[index].slug : NULL;
}

const char *face_avatar_registry_name_at(size_t index)
{
    return index < avatar_count() ? GENERATED_AVATARS[index].name : NULL;
}

size_t face_avatar_catalog_count(void)
{
    return catalog_count();
}

const char *face_avatar_catalog_slug_at(size_t index)
{
    const face_avatar_entry_t *entry = catalog_entry(index);
    return entry != NULL ? entry->slug : NULL;
}

const char *face_avatar_catalog_name_at(size_t index)
{
    const face_avatar_entry_t *entry = catalog_entry(index);
    return entry != NULL ? entry->name : NULL;
}

bool face_avatar_catalog_info_at(size_t index, face_render_info_t *info)
{
    const face_avatar_entry_t *entry = catalog_entry(index);
    if (entry == NULL || info == NULL) {
        return false;
    }
    *info = (face_render_info_t){
        .width = FACE_RENDER_WIDTH,
        .height = FACE_RENDER_HEIGHT,
        .work_width = entry->work_width,
        .work_height = entry->work_height,
        .framebuffer_bytes = FACE_RENDER_FRAME_BYTES,
        .family = FACE_RENDER_FAMILY_PIXEL,
        .mouth_kind = FACE_RENDER_MOUTH_SPRITE,
        .flags = entry->flags,
        .estimated_ops_per_pixel = 4U,
    };
    return true;
}

bool face_avatar_registry_render(
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (render_key == NULL || rgb565 == NULL ||
        (!s_ready && !face_avatar_registry_init())) {
        return false;
    }

    face_render_key_t animated;
    if (!prepare_render_key(render_key, sample_clock, &animated)) {
        return false;
    }

    return face_sprite_render(
        &s_player,
        &animated,
        sample_clock,
        rgb565,
        pixel_capacity);
}

bool face_avatar_registry_render_snapshot_at(
    size_t index,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    const face_avatar_entry_t *entry = catalog_entry(index);
    if (entry == NULL || render_key == NULL || rgb565 == NULL) {
        return false;
    }
    face_sprite_player_t player;
    face_render_key_t animated;
    if (!face_sprite_player_init(&player, entry->atlas) ||
        !prepare_render_key(render_key, sample_clock, &animated)) {
        return false;
    }
    return face_sprite_render_snapshot(
        &player,
        &animated,
        sample_clock,
        rgb565,
        pixel_capacity);
}
