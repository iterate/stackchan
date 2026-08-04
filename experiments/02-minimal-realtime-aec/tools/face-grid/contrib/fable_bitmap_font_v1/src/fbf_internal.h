#pragma once

#include "fbf.h"

/* Shared internals. Blob fields are read bytewise so the runtime is
 * endian-independent and never performs an unaligned load (xtensa traps
 * on those when the blob lands unaligned in flash). */

static inline uint16_t fbf_rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t fbf_rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t fbf_row_bytes(uint32_t width_px)
{
    return (width_px * 2u + 7u) / 8u;
}

/*
 * Pen state machine shared by measure, layout, and draw so every consumer
 * agrees on advance, tracking, and kerning to the pixel. Feed one glyph
 * index at a time; `fbf_pen_feed` returns the pen position the glyph draws
 * at (font px) and advances past it.
 */
typedef struct {
    int32_t pen;
    uint16_t prev;
    bool has_prev;
} fbf_pen_t;

static inline void fbf_pen_reset(fbf_pen_t *p)
{
    p->pen = 0;
    p->prev = FBF_GLYPH_NONE;
    p->has_prev = false;
}

/* Shared between layout (trim math) and draw (rendering): the ellipsis is
 * the font's native U+2026, or three periods, or the fallback glyph. */
void fbf_ellipsis_run(
    const fbf_font_t *font, uint16_t *glyph_index, uint8_t *repeat);
int32_t fbf_ellipsis_width(const fbf_font_t *font, int8_t tracking);

/* Draw/record the ellipsis run at pen_x; returns the final pen x. */
int32_t fbf_draw_ellipsis(
    fbf_surface_t *surface, fbf_op_list_t *ops, const fbf_font_t *font,
    int16_t pen_x, int16_t baseline_y, const fbf_style_t *style,
    uint8_t style_slot, uint8_t scale, int8_t tracking);

static inline int32_t fbf_pen_feed(
    const fbf_font_t *font, fbf_pen_t *p, uint16_t glyph_index,
    int8_t tracking)
{
    fbf_glyph_t glyph;
    if (!fbf_font_glyph(font, glyph_index, &glyph)) {
        return p->pen; /* unmapped and no fallback: invisible, no width */
    }
    if (p->has_prev) {
        p->pen += tracking + fbf_font_kern(font, p->prev, glyph_index);
    }
    const int32_t at = p->pen;
    p->pen = at + glyph.advance;
    p->prev = glyph_index;
    p->has_prev = true;
    return at;
}
