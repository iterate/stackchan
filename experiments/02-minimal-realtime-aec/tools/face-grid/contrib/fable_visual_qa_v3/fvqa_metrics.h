#pragma once

/*
 * fvqa_metrics: integer-only, allocation-free frame analysis for the
 * Stack-chan 160x120 RGB565 face renderers.
 *
 * Every function is a pure function of caller-owned buffers. All metrics
 * are ADVISORY: they exist to flag suspects for human review of contact
 * sheets and storyboards, never to promote a renderer on their own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FVQA_WIDTH = 160,
    FVQA_HEIGHT = 120,
    FVQA_PIXELS = FVQA_WIDTH * FVQA_HEIGHT,
    /* contact-scale ("across the room") view is a 4x box downscale */
    FVQA_SMALL_WIDTH = FVQA_WIDTH / 4,
    FVQA_SMALL_HEIGHT = FVQA_HEIGHT / 4,
    FVQA_SMALL_PIXELS = FVQA_SMALL_WIDTH * FVQA_SMALL_HEIGHT,
    /* palette-resistant structural signature: binarized pooled gradient */
    FVQA_SIG_WORDS = (FVQA_SMALL_PIXELS + 31) / 32,
    /* components smaller than this are specks, not topology */
    FVQA_MIN_COMPONENT_AREA = 5,
    /* a mask pixel differs from the background mode by at least this */
    FVQA_MASK_MARGIN = 28,
};

/*
 * Face bands used for region-split response measurement. The face is
 * centered in frame by convention: the eye band covers the upper face,
 * the mouth band the lower face, with a gap so cheek lines do not double
 * count. Eyes-only renderers legitimately score zero in the mouth band.
 */
enum {
    FVQA_EYE_BAND_TOP = 14,
    FVQA_EYE_BAND_BOTTOM = 66,    /* exclusive */
    FVQA_MOUTH_BAND_TOP = 70,
    FVQA_MOUTH_BAND_BOTTOM = 114, /* exclusive */
};

typedef struct {
    uint16_t left;   /* lit mask pixels on the 1px outer ring, per side */
    uint16_t right;
    uint16_t top;
    uint16_t bottom;
    uint16_t max_run; /* longest consecutive lit run along any one side */
    uint16_t total;
} fvqa_border_contact_t;

typedef struct {
    uint8_t band_components;  /* area-filtered components in the band */
    bool corners_detached;    /* an extreme lit pixel of the band is not
                                 part of the band's largest component */
    uint16_t largest_area;    /* area of the dominant band component */
} fvqa_mouth_report_t;

/* Scratch memory for connected-component labeling, caller-owned so the
 * metric core itself never allocates. */
typedef struct {
    int16_t labels[FVQA_PIXELS];
    int32_t stack[FVQA_PIXELS];
} fvqa_cc_scratch_t;

/* RGB565 -> 8-bit Rec.601-style luminance. */
void fvqa_luma_from_rgb565(
    const uint16_t *frame, uint8_t *luma, size_t pixel_count);

/* Most common luminance on the 1px outer ring: the background estimate. */
uint8_t fvqa_background_mode(const uint8_t *luma);

/* mask[i] = |luma[i] - background_mode| >= FVQA_MASK_MARGIN (1/0).
 * Works for dark faces on light plates as well as the usual inverse. */
void fvqa_mask_build(
    const uint8_t *luma, uint8_t background_mode, uint8_t *mask);

/* Lit mask pixels per frame border plus the longest single-side run. */
void fvqa_border_contact(
    const uint8_t *mask, fvqa_border_contact_t *out);

/* Area-filtered 4-connected component count over the full mask. */
int fvqa_component_count(
    const uint8_t *mask, fvqa_cc_scratch_t *scratch);

/* Mouth-band topology: area-filtered component count restricted to the
 * band and whether the extreme left/right lit pixels of the band belong
 * to its largest component. Detached corners on smiles are the classic
 * "floating mouth corner" defect. */
void fvqa_mouth_report(
    const uint8_t *mask,
    fvqa_cc_scratch_t *scratch,
    fvqa_mouth_report_t *out);

/* Mean absolute luminance delta over rows [band_top, band_bottom),
 * scaled by 100 (0..25500 "centi-levels"). */
uint32_t fvqa_band_delta_centi(
    const uint8_t *luma_a,
    const uint8_t *luma_b,
    int band_top,
    int band_bottom);

/* 4x4 box downscale to the contact-scale view. */
void fvqa_box_downscale4(const uint8_t *luma, uint8_t *small);

/* Palette-resistant structural signature: gradient magnitude, max-pooled
 * 4x4 to 40x30, binarized at a fixed rank (top quarter of nonzero cells,
 * minimum strength 4). Invariant under monotonic palette remaps and
 * uniform brightness shifts; sensitive to geometry. */
void fvqa_edge_signature(const uint8_t *luma, uint32_t *signature);

/* Jaccard distance between two signatures in per-mille (0..1000).
 * 0 = identical structure, 1000 = disjoint. Empty union counts as 0. */
uint32_t fvqa_signature_distance_permille(
    const uint32_t *a, const uint32_t *b);

/* Population count helper (exposed for tests). */
uint32_t fvqa_popcount32(uint32_t value);
