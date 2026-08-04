#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_render.h"

/*
 * Render the authored Pocket Mossling atlas through the shared FSPR player.
 *
 * This review-grid entry remains a pure function of the 40-byte render key
 * and 16 kHz clock. Device code can retain its own face_sprite_player_t when
 * it wants the player's mouth debounce/coarticulation history.
 */
bool face_sprite_mossling_render(
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
