#include <string.h>

#include "fbf_internal.h"

/* --------------------------------------------------------- color/style */

static uint16_t pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

fbf_color_t fbf_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    fbf_color_t c;
    c.rgb565 = pack565(r, g, b);
    c.rgba[0] = r;
    c.rgba[1] = g;
    c.rgba[2] = b;
    c.rgba[3] = 255;
    return c;
}

fbf_style_t fbf_style_make(uint8_t r, uint8_t g, uint8_t b)
{
    fbf_style_t s;
    s.color[0] = fbf_color_rgb(0, 0, 0); /* never drawn */
    s.color[1] = fbf_color_rgb(r, g, b);
    s.color[2] = fbf_color_rgb( /* shade: 5/8 ink */
        (uint8_t)((r * 5) / 8), (uint8_t)((g * 5) / 8),
        (uint8_t)((b * 5) / 8));
    s.color[3] = fbf_color_rgb( /* accent: halfway to white */
        (uint8_t)(r + (255 - r) / 2), (uint8_t)(g + (255 - g) / 2),
        (uint8_t)(b + (255 - b) / 2));
    s.draw_mask = 0x0e; /* planes 1..3 */
    return s;
}

/* Speaker/state palette. Rows: assistant, user, system; columns: final,
 * partial, thinking. Values chosen to read on the dark face background
 * at both 1x and 2x. */
#define FBF_STYLE(r, g, b) \
    { \
        { \
            {0x0000, {0, 0, 0, 255}}, \
            {(uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | \
                 ((b) >> 3)), \
                {(r), (g), (b), 255}}, \
            {(uint16_t)(((((r) * 5 / 8) >> 3) << 11) | \
                 ((((g) * 5 / 8) >> 2) << 5) | (((b) * 5 / 8) >> 3)), \
                {(uint8_t)((r) * 5 / 8), (uint8_t)((g) * 5 / 8), \
                    (uint8_t)((b) * 5 / 8), 255}}, \
            {(uint16_t)(((((r) + (255 - (r)) / 2) >> 3) << 11) | \
                 ((((g) + (255 - (g)) / 2) >> 2) << 5) | \
                 (((b) + (255 - (b)) / 2) >> 3)), \
                {(uint8_t)((r) + (255 - (r)) / 2), \
                    (uint8_t)((g) + (255 - (g)) / 2), \
                    (uint8_t)((b) + (255 - (b)) / 2), 255}}, \
        }, \
        0x0e \
    }

static const fbf_style_t k_speaker_styles
    [FBF_SPEAKER_COUNT][FBF_TEXT_STATE_COUNT] = {
    /* assistant: mint */
    {FBF_STYLE(122, 236, 178), FBF_STYLE(82, 158, 120),
        FBF_STYLE(56, 106, 82)},
    /* user: amber */
    {FBF_STYLE(248, 200, 96), FBF_STYLE(166, 134, 66),
        FBF_STYLE(112, 91, 46)},
    /* system: slate */
    {FBF_STYLE(168, 180, 192), FBF_STYLE(113, 121, 129),
        FBF_STYLE(76, 82, 88)},
};

const fbf_style_t *fbf_style_speaker(uint8_t speaker, uint8_t state)
{
    if (speaker >= FBF_SPEAKER_COUNT) {
        speaker = FBF_SPEAKER_SYSTEM;
    }
    if (state >= FBF_TEXT_STATE_COUNT) {
        state = FBF_TEXT_FINAL;
    }
    return &k_speaker_styles[speaker][state];
}

/* --------------------------------------------------------------- surface */

fbf_surface_t fbf_surface_rgb565(
    uint16_t *pixels, int16_t width, int16_t height)
{
    fbf_surface_t s;
    s.pixels = pixels;
    s.width = width;
    s.height = height;
    s.stride_bytes = width * 2;
    s.format = FBF_FORMAT_RGB565;
    return s;
}

fbf_surface_t fbf_surface_rgba8888(
    uint8_t *pixels, int16_t width, int16_t height)
{
    fbf_surface_t s;
    s.pixels = pixels;
    s.width = width;
    s.height = height;
    s.stride_bytes = width * 4;
    s.format = FBF_FORMAT_RGBA8888;
    return s;
}

static void write_span(
    fbf_surface_t *surface, int16_t x, int16_t y, int16_t count,
    fbf_color_t color)
{
    if (y < 0 || y >= surface->height) {
        return;
    }
    int16_t x0 = x;
    int16_t x1 = (int16_t)(x + count);
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > surface->width) {
        x1 = surface->width;
    }
    if (x0 >= x1) {
        return;
    }
    uint8_t *row = (uint8_t *)surface->pixels +
        (int32_t)y * surface->stride_bytes;
    if (surface->format == FBF_FORMAT_RGB565) {
        uint16_t *px = (uint16_t *)row;
        for (int16_t i = x0; i < x1; i++) {
            px[i] = color.rgb565;
        }
    } else {
        for (int16_t i = x0; i < x1; i++) {
            uint8_t *p = row + (int32_t)i * 4;
            p[0] = color.rgba[0];
            p[1] = color.rgba[1];
            p[2] = color.rgba[2];
            p[3] = color.rgba[3];
        }
    }
}

void fbf_fill(
    fbf_surface_t *surface, int16_t x, int16_t y, int16_t w, int16_t h,
    fbf_color_t color)
{
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    for (int16_t row = 0; row < h; row++) {
        write_span(surface, x, (int16_t)(y + row), w, color);
    }
}

/* ------------------------------------------------------------------ draw */

fbf_op_list_t fbf_op_list(fbf_draw_op_t *ops, uint16_t capacity)
{
    fbf_op_list_t list;
    list.ops = ops;
    list.capacity = ops == NULL ? 0 : capacity;
    list.count = 0;
    list.overflowed = false;
    return list;
}

static void op_push(
    fbf_op_list_t *ops, uint16_t glyph_index, int16_t x, int16_t y,
    uint8_t scale, uint8_t style_slot)
{
    if (ops->count >= ops->capacity) {
        ops->overflowed = true;
        return;
    }
    fbf_draw_op_t *op = &ops->ops[ops->count++];
    op->glyph_index = glyph_index;
    op->x = x;
    op->y = y;
    op->scale = scale;
    op->style_slot = style_slot;
}

/* Core blitter: draw a glyph bitmap with its top-left at (dest_x, dest_y)
 * device px, each font pixel expanded to scale x scale. Each glyph row is
 * decoded once into palette indices and reused for all its scaled rows. */
static void blit_glyph(
    fbf_surface_t *surface, const fbf_font_t *font,
    const fbf_glyph_t *glyph, int16_t dest_x, int16_t dest_y,
    const fbf_style_t *style, uint8_t scale)
{
    uint8_t row_px[FBF_MAX_GLYPH_SIDE];
    for (uint8_t gy = 0; gy < glyph->height; gy++) {
        for (uint8_t gx = 0; gx < glyph->width; gx++) {
            row_px[gx] = fbf_font_glyph_pixel(font, glyph, gx, gy);
        }
        for (uint8_t sy = 0; sy < scale; sy++) {
            const int16_t y =
                (int16_t)(dest_y + (int16_t)gy * scale + sy);
            for (uint8_t gx = 0; gx < glyph->width; gx++) {
                const uint8_t plane = row_px[gx];
                if (plane == 0 ||
                    ((style->draw_mask >> plane) & 1u) == 0) {
                    continue;
                }
                write_span(
                    surface, (int16_t)(dest_x + (int16_t)gx * scale), y,
                    (int16_t)scale, style->color[plane]);
            }
        }
    }
}

static uint8_t clamp_scale(uint8_t scale)
{
    if (scale < 1) {
        return 1;
    }
    if (scale > 4) {
        return 4;
    }
    return scale;
}

void fbf_draw_glyph(
    fbf_surface_t *surface, const fbf_font_t *font, uint16_t glyph_index,
    int16_t pen_x, int16_t baseline_y, const fbf_style_t *style,
    uint8_t scale)
{
    fbf_glyph_t glyph;
    if (surface == NULL || surface->pixels == NULL || style == NULL ||
        !fbf_font_glyph(font, glyph_index, &glyph)) {
        return;
    }
    scale = clamp_scale(scale);
    blit_glyph(
        surface, font, &glyph,
        (int16_t)(pen_x + glyph.bearing_x * scale),
        (int16_t)(baseline_y - glyph.bearing_y * scale), style, scale);
}

void fbf_draw_op(
    fbf_surface_t *surface, const fbf_font_t *font, const fbf_draw_op_t *op,
    const fbf_style_t *const *styles, uint16_t style_count)
{
    fbf_glyph_t glyph;
    if (surface == NULL || surface->pixels == NULL || op == NULL ||
        styles == NULL || style_count == 0 ||
        !fbf_font_glyph(font, op->glyph_index, &glyph)) {
        return;
    }
    const fbf_style_t *style =
        styles[op->style_slot < style_count ? op->style_slot : 0];
    blit_glyph(
        surface, font, &glyph, op->x, op->y, style,
        clamp_scale(op->scale));
}

int32_t fbf_draw_ellipsis(
    fbf_surface_t *surface, fbf_op_list_t *ops, const fbf_font_t *font,
    int16_t pen_x, int16_t baseline_y, const fbf_style_t *style,
    uint8_t style_slot, uint8_t scale, int8_t tracking)
{
    uint16_t index;
    uint8_t repeat;
    fbf_ellipsis_run(font, &index, &repeat);
    scale = clamp_scale(scale);
    fbf_pen_t pen;
    fbf_pen_reset(&pen);
    fbf_glyph_t glyph;
    for (uint8_t i = 0; i < repeat; i++) {
        const int32_t at = fbf_pen_feed(font, &pen, index, tracking);
        if (!fbf_font_glyph(font, index, &glyph) ||
            glyph.width == 0 || glyph.height == 0) {
            continue;
        }
        const int16_t dest_x =
            (int16_t)(pen_x + (at + glyph.bearing_x) * scale);
        const int16_t dest_y =
            (int16_t)(baseline_y - glyph.bearing_y * scale);
        if (surface != NULL && surface->pixels != NULL &&
            style != NULL) {
            blit_glyph(
                surface, font, &glyph, dest_x, dest_y, style, scale);
        }
        if (ops != NULL) {
            op_push(ops, index, dest_x, dest_y, scale, style_slot);
        }
    }
    return pen_x + pen.pen * scale;
}

int32_t fbf_draw_text(
    fbf_surface_t *surface, fbf_op_list_t *ops, const fbf_font_t *font,
    const char *text, uint32_t len, int16_t pen_x, int16_t baseline_y,
    const fbf_style_t *style, uint8_t style_slot, uint8_t scale,
    int8_t tracking, uint32_t reveal_glyphs)
{
    if (font == NULL || (text == NULL && len > 0)) {
        return pen_x;
    }
    scale = clamp_scale(scale);
    fbf_pen_t pen;
    fbf_pen_reset(&pen);
    uint32_t cursor = 0;
    uint32_t seen = 0;
    fbf_glyph_t glyph;
    while (cursor < len) {
        if (seen >= reveal_glyphs) {
            break;
        }
        const uint32_t cp = fbf_utf8_next(text, len, &cursor);
        seen++;
        if (cp == '\n' || cp == '\r') {
            continue;
        }
        const uint16_t index = fbf_font_glyph_index(font, cp);
        const int32_t at = fbf_pen_feed(font, &pen, index, tracking);
        if (!fbf_font_glyph(font, index, &glyph) ||
            glyph.width == 0 || glyph.height == 0) {
            continue;
        }
        const int16_t dest_x =
            (int16_t)(pen_x + (at + glyph.bearing_x) * scale);
        const int16_t dest_y =
            (int16_t)(baseline_y - glyph.bearing_y * scale);
        if (surface != NULL && surface->pixels != NULL &&
            style != NULL) {
            blit_glyph(
                surface, font, &glyph, dest_x, dest_y, style, scale);
        }
        if (ops != NULL) {
            op_push(ops, index, dest_x, dest_y, scale, style_slot);
        }
    }
    return pen_x + pen.pen * scale;
}
