#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_pixel_redux_actors.h"

/*
 * Eighteen authored follow-ons to the five Pixel Redux actors.  The first
 * fifteen provide three visibly different silhouettes and acting theses per
 * original; the final three form a strict four-shade handheld family.  This
 * pack is kept independent from the production registry so it can be reviewed
 * and frozen before profile IDs are assigned.
 */
typedef enum {
    FACE_PIXEL_VARIANT_EGA_SUNBLADE_RANGER = 0,
    FACE_PIXEL_VARIANT_EGA_TAVERN_BARD,
    FACE_PIXEL_VARIANT_EGA_MOONKEEP_ROGUE,
    FACE_PIXEL_VARIANT_VGA_ASTRAL_ARCHIVIST,
    FACE_PIXEL_VARIANT_VGA_STORM_SEER,
    FACE_PIXEL_VARIANT_VGA_HEARTH_SAGE,
    FACE_PIXEL_VARIANT_TALKIE_DOCKYARD_PILOT,
    FACE_PIXEL_VARIANT_TALKIE_NEON_ENGINEER,
    FACE_PIXEL_VARIANT_TALKIE_INTERCOM_CAPTAIN,
    FACE_PIXEL_VARIANT_ARCADE_CRT_CONCIERGE,
    FACE_PIXEL_VARIANT_ARCADE_SENTINEL,
    FACE_PIXEL_VARIANT_ARCADE_PINBALL_BELLHOP,
    FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT,
    FACE_PIXEL_VARIANT_POCKET_BOG_SPRITE,
    FACE_PIXEL_VARIANT_POCKET_MOONCAP_FAMILIAR,
    FACE_PIXEL_VARIANT_DMG_TIN_WARDEN,
    FACE_PIXEL_VARIANT_DMG_LANTERN_MOTH,
    FACE_PIXEL_VARIANT_DMG_SLIME_COURIER,
    FACE_PIXEL_VARIANT_COUNT,
} face_pixel_redux_variant_t;

typedef struct {
    const char *slug;
    const char *name;
    face_pixel_redux_actor_t base_actor;
    uint8_t authored_version;
    uint8_t palette_size;
    uint8_t estimated_ops_per_pixel;
} face_pixel_redux_variant_info_t;

size_t face_pixel_redux_variant_count(void);

const char *face_pixel_redux_variant_slug(
    face_pixel_redux_variant_t variant);

const char *face_pixel_redux_variant_name(
    face_pixel_redux_variant_t variant);

bool face_pixel_redux_variant_info(
    face_pixel_redux_variant_t variant,
    face_pixel_redux_variant_info_t *info);

bool face_pixel_redux_variant_render(
    face_pixel_redux_variant_t variant,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
