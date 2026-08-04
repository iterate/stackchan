#pragma once

/*
 * Artist-pass extensions for the three strongest FEA lineages.
 *
 * This deliberately lives beside, rather than inside, the original FEA
 * registry.  The nine profiles can be reviewed and integrated without
 * renumbering or changing any of the five preserved actors.
 *
 * Like the original pack, every renderer is a pure integer-only function
 * of the schema-v2 key plus the 16 kHz sample clock.  It has no heap,
 * retained state, platform APIs, or host/WASM divergence.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fea.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FEA_FAVOURITE_LANTERN_BLOOM = 0,
    FEA_FAVOURITE_COMET_RASCAL,
    FEA_FAVOURITE_MOON_MOTH_ORACLE,
    FEA_FAVOURITE_GILDED_NOH,
    FEA_FAVOURITE_BEETLE_AUTOMATON,
    FEA_FAVOURITE_KINTSUGI_MARIONETTE,
    FEA_FAVOURITE_POP_BURST,
    FEA_FAVOURITE_FELT_PATCH_PAL,
    FEA_FAVOURITE_SPEECH_BUBBLE_SPRITE,
    FEA_FAVOURITE_PROFILE_COUNT,
} fea_favourite_profile_t;

typedef enum {
    FEA_FAVOURITE_LINEAGE_WILL_O_WISP = 0,
    FEA_FAVOURITE_LINEAGE_KARAKURI_BRASS,
    FEA_FAVOURITE_LINEAGE_EMOTE_STICKER,
} fea_favourite_lineage_t;

size_t fea_favourite_profile_count(void);
const char *fea_favourite_profile_slug(fea_favourite_profile_t profile);
const char *fea_favourite_profile_name(fea_favourite_profile_t profile);
const char *fea_favourite_profile_thesis(fea_favourite_profile_t profile);
bool fea_favourite_profile_lineage(
    fea_favourite_profile_t profile,
    fea_favourite_lineage_t *lineage);
bool fea_favourite_profile_info(
    fea_favourite_profile_t profile,
    fea_info_t *info);

bool fea_favourite_probe(
    fea_favourite_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fea_probe_t *probe);

bool fea_favourite_render_frame(
    fea_favourite_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

#ifdef __cplusplus
}
#endif
