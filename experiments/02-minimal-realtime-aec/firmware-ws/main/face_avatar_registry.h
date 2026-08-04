#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"
#include "face_render.h"

/*
 * Allocation-free registry for the sprite atlases compiled into a target.
 *
 * All entries share the caller-owned RGB565 framebuffer. Palette-only themes
 * can also share their compressed sprite blob, so cycling an avatar never
 * allocates or copies frame-sized memory.
 */
bool face_avatar_registry_init(void);
bool face_avatar_registry_next(void);

size_t face_avatar_registry_count(void);
size_t face_avatar_registry_current_index(void);
const char *face_avatar_registry_current_slug(void);
const char *face_avatar_registry_current_name(void);
const char *face_avatar_registry_slug_at(size_t index);
const char *face_avatar_registry_name_at(size_t index);

/*
 * The device cycle is intentionally smaller than the browser catalogue:
 * CoreS3 only exposes atlases framed for "the LCD is the creature", while
 * the workbench can also inspect full-character and legacy imported FSPR
 * atlases. Both paths still use the same renderer and atlas descriptors.
 */
size_t face_avatar_catalog_count(void);
const char *face_avatar_catalog_slug_at(size_t index);
const char *face_avatar_catalog_name_at(size_t index);
bool face_avatar_catalog_info_at(size_t index, face_render_info_t *info);

bool face_avatar_registry_render(
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

/*
 * Pure interleaved-matrix path for WASM. It uses the same compiled atlas,
 * performance preparation, and renderer as firmware without sharing the
 * firmware player's mouth-debounce history between gallery tiles.
 */
bool face_avatar_registry_render_snapshot_at(
    size_t index,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
