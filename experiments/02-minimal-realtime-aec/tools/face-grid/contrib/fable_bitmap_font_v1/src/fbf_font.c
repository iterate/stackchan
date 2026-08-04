#include <string.h>

#include "fbf_internal.h"

/* FBF1 header field offsets (DESIGN.md). */
enum {
    HDR_MAGIC = 0,
    HDR_VERSION = 4,
    HDR_FLAGS = 6,
    HDR_GLYPH_COUNT = 8,
    HDR_KERN_COUNT = 10,
    HDR_LINE_HEIGHT = 12,
    HDR_ASCENT = 13,
    HDR_DESCENT = 14,
    HDR_TRACKING = 15,
    HDR_FALLBACK = 16,
    HDR_GLYPHS_OFFSET = 20,
    HDR_KERN_OFFSET = 24,
    HDR_BITMAP_OFFSET = 28,
    HDR_BITMAP_SIZE = 32,
    HDR_CHECKSUM = 36,
};

/* Glyph record field offsets. */
enum {
    GR_CODEPOINT = 0,
    GR_BITMAP_OFFSET = 4,
    GR_ADVANCE = 8,
    GR_WIDTH = 9,
    GR_HEIGHT = 10,
    GR_BEARING_X = 11,
    GR_BEARING_Y = 12,
};

/* Kern record field offsets. */
enum {
    KR_LEFT = 0,
    KR_RIGHT = 2,
    KR_DX = 4,
};

static uint32_t fnv1a32(const uint8_t *data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

static const uint8_t *glyph_record(const fbf_font_t *font, uint16_t index)
{
    return font->glyphs + (uint32_t)index * FBF_GLYPH_RECORD_BYTES;
}

bool fbf_font_init(fbf_font_t *font, const void *blob, uint32_t blob_size)
{
    if (font == NULL) {
        return false;
    }
    memset(font, 0, sizeof(*font));
    const uint8_t *b = (const uint8_t *)blob;
    if (b == NULL || blob_size < FBF_HEADER_BYTES) {
        return false;
    }
    if (fbf_rd_u32(b + HDR_MAGIC) != FBF_BLOB_MAGIC ||
        fbf_rd_u16(b + HDR_VERSION) != FBF_BLOB_VERSION) {
        return false;
    }

    const uint16_t glyph_count = fbf_rd_u16(b + HDR_GLYPH_COUNT);
    const uint16_t kern_count = fbf_rd_u16(b + HDR_KERN_COUNT);
    const uint32_t glyphs_offset = fbf_rd_u32(b + HDR_GLYPHS_OFFSET);
    const uint32_t kern_offset = fbf_rd_u32(b + HDR_KERN_OFFSET);
    const uint32_t bitmap_offset = fbf_rd_u32(b + HDR_BITMAP_OFFSET);
    const uint32_t bitmap_size = fbf_rd_u32(b + HDR_BITMAP_SIZE);

    /* Tables must tile the payload exactly, in order. */
    if (glyph_count == 0 ||
        glyphs_offset != FBF_HEADER_BYTES ||
        kern_offset !=
            glyphs_offset + (uint32_t)glyph_count * FBF_GLYPH_RECORD_BYTES ||
        bitmap_offset !=
            kern_offset + (uint32_t)kern_count * FBF_KERN_RECORD_BYTES ||
        bitmap_offset + bitmap_size != blob_size) {
        return false;
    }
    if (fnv1a32(b + FBF_HEADER_BYTES, blob_size - FBF_HEADER_BYTES) !=
        fbf_rd_u32(b + HDR_CHECKSUM)) {
        return false;
    }

    /* Glyph records: strictly increasing codepoints, bitmaps in bounds. */
    uint32_t prev_cp = 0;
    for (uint16_t i = 0; i < glyph_count; i++) {
        const uint8_t *g =
            b + glyphs_offset + (uint32_t)i * FBF_GLYPH_RECORD_BYTES;
        const uint32_t cp = fbf_rd_u32(g + GR_CODEPOINT);
        if (i > 0 && cp <= prev_cp) {
            return false;
        }
        prev_cp = cp;
        const uint8_t width = g[GR_WIDTH];
        const uint8_t height = g[GR_HEIGHT];
        if (width > FBF_MAX_GLYPH_SIDE || height > FBF_MAX_GLYPH_SIDE) {
            return false;
        }
        if (width > 0 && height > 0) {
            const uint32_t need = fbf_rd_u32(g + GR_BITMAP_OFFSET) +
                fbf_row_bytes(width) * height;
            if (need > bitmap_size) {
                return false;
            }
        }
    }

    /* Kern records: valid indices, strictly increasing pairs. */
    uint32_t prev_pair = 0;
    for (uint16_t i = 0; i < kern_count; i++) {
        const uint8_t *k =
            b + kern_offset + (uint32_t)i * FBF_KERN_RECORD_BYTES;
        const uint16_t left = fbf_rd_u16(k + KR_LEFT);
        const uint16_t right = fbf_rd_u16(k + KR_RIGHT);
        if (left >= glyph_count || right >= glyph_count) {
            return false;
        }
        const uint32_t pair = ((uint32_t)left << 16) | right;
        if (i > 0 && pair <= prev_pair) {
            return false;
        }
        prev_pair = pair;
    }

    const uint16_t fallback = fbf_rd_u16(b + HDR_FALLBACK);
    if (fallback != FBF_GLYPH_NONE && fallback >= glyph_count) {
        return false;
    }

    font->blob = b;
    font->blob_size = blob_size;
    font->glyphs = b + glyphs_offset;
    font->kerns = b + kern_offset;
    font->bitmaps = b + bitmap_offset;
    font->bitmap_size = bitmap_size;
    font->glyph_count = glyph_count;
    font->kern_count = kern_count;
    font->fallback_index = fallback;
    font->line_height = b[HDR_LINE_HEIGHT];
    font->ascent = b[HDR_ASCENT];
    font->descent = b[HDR_DESCENT];
    font->tracking = (int8_t)b[HDR_TRACKING];
    return true;
}

uint16_t fbf_font_glyph_index(const fbf_font_t *font, uint32_t codepoint)
{
    uint32_t lo = 0;
    uint32_t hi = font->glyph_count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        const uint32_t cp =
            fbf_rd_u32(glyph_record(font, (uint16_t)mid) + GR_CODEPOINT);
        if (cp == codepoint) {
            return (uint16_t)mid;
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return font->fallback_index;
}

bool fbf_font_glyph(
    const fbf_font_t *font, uint16_t index, fbf_glyph_t *out)
{
    if (index >= font->glyph_count) {
        return false;
    }
    const uint8_t *g = glyph_record(font, index);
    out->codepoint = fbf_rd_u32(g + GR_CODEPOINT);
    out->bitmap_offset = fbf_rd_u32(g + GR_BITMAP_OFFSET);
    out->advance = g[GR_ADVANCE];
    out->width = g[GR_WIDTH];
    out->height = g[GR_HEIGHT];
    out->bearing_x = (int8_t)g[GR_BEARING_X];
    out->bearing_y = (int8_t)g[GR_BEARING_Y];
    return true;
}

int8_t fbf_font_kern(
    const fbf_font_t *font, uint16_t left_index, uint16_t right_index)
{
    const uint32_t key = ((uint32_t)left_index << 16) | right_index;
    uint32_t lo = 0;
    uint32_t hi = font->kern_count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        const uint8_t *k =
            font->kerns + mid * FBF_KERN_RECORD_BYTES;
        const uint32_t pair =
            ((uint32_t)fbf_rd_u16(k + KR_LEFT) << 16) |
            fbf_rd_u16(k + KR_RIGHT);
        if (pair == key) {
            return (int8_t)k[KR_DX];
        }
        if (pair < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

uint8_t fbf_font_glyph_pixel(
    const fbf_font_t *font, const fbf_glyph_t *glyph, uint8_t x, uint8_t y)
{
    if (x >= glyph->width || y >= glyph->height) {
        return 0;
    }
    const uint32_t offset = glyph->bitmap_offset +
        fbf_row_bytes(glyph->width) * y + (x >> 2);
    return (uint8_t)((font->bitmaps[offset] >> ((x & 3u) * 2u)) & 3u);
}

/* ----------------------------------------------------------------- utf-8 */

uint32_t fbf_utf8_next(const char *text, uint32_t len, uint32_t *cursor)
{
    const uint8_t *s = (const uint8_t *)text;
    uint32_t i = *cursor;
    if (i >= len) {
        return 0;
    }
    const uint8_t b0 = s[i++];
    if (b0 < 0x80) {
        *cursor = i;
        return b0;
    }

    uint32_t cp;
    uint32_t need;
    uint8_t first_lo = 0x80;
    uint8_t first_hi = 0xbf;
    if (b0 >= 0xc2 && b0 <= 0xdf) {
        cp = b0 & 0x1fu;
        need = 1;
    } else if (b0 >= 0xe0 && b0 <= 0xef) {
        cp = b0 & 0x0fu;
        need = 2;
        if (b0 == 0xe0) {
            first_lo = 0xa0; /* reject overlong */
        } else if (b0 == 0xed) {
            first_hi = 0x9f; /* reject surrogates */
        }
    } else if (b0 >= 0xf0 && b0 <= 0xf4) {
        cp = b0 & 0x07u;
        need = 3;
        if (b0 == 0xf0) {
            first_lo = 0x90; /* reject overlong */
        } else if (b0 == 0xf4) {
            first_hi = 0x8f; /* reject > U+10FFFF */
        }
    } else {
        /* 0x80..0xc1, 0xf5..0xff can never start a sequence. */
        *cursor = i;
        return FBF_REPLACEMENT_CODEPOINT;
    }

    uint32_t got = 0;
    for (uint32_t k = 0; k < need; k++) {
        if (i >= len) {
            break; /* truncated tail: consume what we saw */
        }
        const uint8_t b = s[i];
        const uint8_t lo = (k == 0) ? first_lo : 0x80;
        const uint8_t hi = (k == 0) ? first_hi : 0xbf;
        if (b < lo || b > hi) {
            /* WHATWG: leave the offending byte for the next call. */
            *cursor = i;
            return FBF_REPLACEMENT_CODEPOINT;
        }
        cp = (cp << 6) | (b & 0x3fu);
        i++;
        got++;
    }
    *cursor = i;
    return (got == need) ? cp : FBF_REPLACEMENT_CODEPOINT;
}

uint32_t fbf_utf8_count(const char *text, uint32_t len)
{
    uint32_t cursor = 0;
    uint32_t count = 0;
    while (cursor < len) {
        (void)fbf_utf8_next(text, len, &cursor);
        count++;
    }
    return count;
}

/* --------------------------------------------------------------- measure */

int32_t fbf_measure_utf8(
    const fbf_font_t *font, const char *text, uint32_t len, int8_t tracking)
{
    fbf_pen_t pen;
    fbf_pen_reset(&pen);
    uint32_t cursor = 0;
    while (cursor < len) {
        const uint32_t cp = fbf_utf8_next(text, len, &cursor);
        if (cp == '\n' || cp == '\r') {
            continue;
        }
        (void)fbf_pen_feed(
            font, &pen, fbf_font_glyph_index(font, cp), tracking);
    }
    return pen.pen;
}
