#pragma once

/*
 * Authored renderer variants
 * --------------------------
 *
 * Nine small, deterministic descendants of three proven StackChan visual
 * languages.  They consume the same 40-byte face_render_key_t used by the
 * firmware and write a complete 160x120 RGB565 frame.
 *
 * This contribution is intentionally isolated from the renderer registry:
 * candidate styles can be reviewed, benchmarked, and compiled to WebAssembly
 * before any one of them is selected for product integration.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FAV_FRAME_WIDTH = 160,
    FAV_FRAME_HEIGHT = 120,
    FAV_PIXEL_COUNT = FAV_FRAME_WIDTH * FAV_FRAME_HEIGHT,
    FAV_FRAME_BYTES = FAV_PIXEL_COUNT * (int)sizeof(uint16_t),
    FAV_CONTEXT_BYTES = 0,
};

typedef enum {
    FAV_PROFILE_BEAN_APPEAL_SCOUT = 0,
    FAV_PROFILE_BEAN_CLOCKWORK_PUPPET,
    FAV_PROFILE_BEAN_MANGA_SPARK,

    FAV_PROFILE_INK_NEWSROOM_EDITOR,
    FAV_PROFILE_INK_CABARET_MIME,
    FAV_PROFILE_INK_BLUEPRINT_COMPANION,

    FAV_PROFILE_CAPTAIN_COMMAND_DECK,
    FAV_PROFILE_CAPTAIN_NEBULA_DOME,
    FAV_PROFILE_CAPTAIN_SOLAR_ROGUE,

    FAV_PROFILE_COUNT,
} fav_profile_t;

typedef enum {
    FAV_LINEAGE_TOON_BEAN = 0,
    FAV_LINEAGE_TOON_INK = 1,
    FAV_LINEAGE_VGA_STAR_CAPTAIN = 2,
} fav_lineage_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t framebuffer_bytes;
    uint8_t lineage;
    uint8_t flags;
    uint16_t estimated_ops_per_pixel;
    uint16_t reserved;
} fav_info_t;

size_t fav_profile_count(void);
const char *fav_profile_slug(fav_profile_t profile);
const char *fav_profile_name(fav_profile_t profile);
fav_lineage_t fav_profile_lineage(fav_profile_t profile);
bool fav_profile_info(fav_profile_t profile, fav_info_t *info);

/*
 * Pure, allocation-free, integer-only rendering. Returns false for invalid
 * profiles, NULL pointers, or a buffer smaller than FAV_PIXEL_COUNT.
 */
bool fav_render_frame(
    fav_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

#ifdef __cplusplus
}
#endif

