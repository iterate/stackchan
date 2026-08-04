#include <string.h>

#include "fbf_internal.h"

/*
 * Wrap iterator: produces one laid-out line per call, re-deriving all
 * state from the byte/glyph cursor so pass one (counting) and pass two
 * (emitting) of fbf_layout_utf8 agree exactly. Greedy word wrap:
 *
 *   - '\n' is a hard break (consumed, never rendered);
 *   - '\r' is consumed invisibly so CRLF input lays out like LF;
 *   - a run of spaces is a break opportunity; wrapping there drops the
 *     whole run;
 *   - a word wider than the budget breaks per glyph, so a line always
 *     carries at least one glyph and layout always progresses.
 *
 * Glyph counters number every scalar of the source text exactly as
 * fbf_utf8_count does, which is what maps the transcript reveal cursor
 * onto lines.
 */
typedef struct {
    const fbf_font_t *font;
    const char *text;
    uint32_t len;
    int32_t max_width;
    int8_t tracking;
    uint32_t cursor;
    uint32_t glyph_cursor;
} wrap_iter_t;

static void wrap_iter_init(
    wrap_iter_t *it, const fbf_layout_spec_t *spec, const char *text,
    uint32_t len)
{
    it->font = spec->font;
    it->text = text;
    it->len = len;
    it->max_width = spec->max_width_px;
    it->tracking = spec->tracking;
    it->cursor = 0;
    it->glyph_cursor = 0;
}

static bool wrap_iter_next(wrap_iter_t *it, fbf_line_t *line)
{
    if (it->cursor >= it->len) {
        return false;
    }
    memset(line, 0, sizeof(*line));
    line->byte_start = it->cursor;
    line->glyph_start = it->glyph_cursor;

    fbf_pen_t pen;
    fbf_pen_reset(&pen);

    bool have_break = false;
    bool in_space_run = false;
    uint32_t break_byte_end = 0;
    uint32_t break_glyphs = 0;
    int32_t break_width = 0;
    uint32_t resume_byte = 0;
    uint32_t resume_glyph = 0;

    while (it->cursor < it->len) {
        uint32_t next_byte = it->cursor;
        const uint32_t cp =
            fbf_utf8_next(it->text, it->len, &next_byte);

        if (cp == '\n') {
            line->byte_end = it->cursor;
            line->glyph_count =
                (uint16_t)(it->glyph_cursor - line->glyph_start);
            line->width_px = (int16_t)pen.pen;
            line->flags |= FBF_LINE_HARD_BREAK;
            it->cursor = next_byte;
            it->glyph_cursor++;
            return true;
        }
        if (cp == '\r') {
            it->cursor = next_byte;
            it->glyph_cursor++;
            continue;
        }
        if (cp == ' ') {
            if (!in_space_run) {
                in_space_run = true;
                have_break = true;
                break_byte_end = it->cursor;
                break_glyphs = it->glyph_cursor - line->glyph_start;
                break_width = pen.pen;
            }
            (void)fbf_pen_feed(
                it->font, &pen,
                fbf_font_glyph_index(it->font, cp), it->tracking);
            it->cursor = next_byte;
            it->glyph_cursor++;
            resume_byte = it->cursor;
            resume_glyph = it->glyph_cursor;
            continue;
        }
        in_space_run = false;

        const uint32_t before_byte = it->cursor;
        const uint32_t before_glyphs =
            it->glyph_cursor - line->glyph_start;
        const int32_t before_width = pen.pen;
        (void)fbf_pen_feed(
            it->font, &pen,
            fbf_font_glyph_index(it->font, cp), it->tracking);

        if (pen.pen > it->max_width && before_glyphs > 0) {
            if (have_break && break_glyphs > 0) {
                line->byte_end = break_byte_end;
                line->glyph_count = (uint16_t)break_glyphs;
                line->width_px = (int16_t)break_width;
                it->cursor = resume_byte;
                it->glyph_cursor = resume_glyph;
            } else {
                line->byte_end = before_byte;
                line->glyph_count = (uint16_t)before_glyphs;
                line->width_px = (int16_t)before_width;
                it->cursor = before_byte;
            }
            return true;
        }
        it->cursor = next_byte;
        it->glyph_cursor++;
    }

    line->byte_end = it->len;
    line->glyph_count = (uint16_t)(it->glyph_cursor - line->glyph_start);
    line->width_px = (int16_t)pen.pen;
    return true;
}

/*
 * The ellipsis run: the font's own U+2026 when it maps natively, else
 * three periods, else whatever the fallback glyph provides for U+2026.
 * fbf_draw.c uses the same helper so trimming and rendering agree.
 */
void fbf_ellipsis_run(
    const fbf_font_t *font, uint16_t *glyph_index, uint8_t *repeat)
{
    fbf_glyph_t glyph;
    uint16_t index = fbf_font_glyph_index(font, FBF_ELLIPSIS_CODEPOINT);
    if (fbf_font_glyph(font, index, &glyph) &&
        glyph.codepoint == FBF_ELLIPSIS_CODEPOINT) {
        *glyph_index = index;
        *repeat = 1;
        return;
    }
    const uint16_t dot = fbf_font_glyph_index(font, '.');
    if (fbf_font_glyph(font, dot, &glyph) && glyph.codepoint == '.') {
        *glyph_index = dot;
        *repeat = 3;
        return;
    }
    *glyph_index = index;
    *repeat = (uint8_t)(index == FBF_GLYPH_NONE ? 0 : 1);
}

int32_t fbf_ellipsis_width(const fbf_font_t *font, int8_t tracking)
{
    uint16_t index;
    uint8_t repeat;
    fbf_ellipsis_run(font, &index, &repeat);
    fbf_pen_t pen;
    fbf_pen_reset(&pen);
    for (uint8_t i = 0; i < repeat; i++) {
        (void)fbf_pen_feed(font, &pen, index, tracking);
    }
    return pen.pen;
}

/* Shorten `line` until its width plus the ellipsis fits the budget,
 * preferring to cut after a non-space glyph. */
static void trim_line_for_ellipsis(
    const fbf_layout_spec_t *spec, const char *text, fbf_line_t *line)
{
    const int32_t ell_width =
        fbf_ellipsis_width(spec->font, spec->tracking);
    const int32_t budget = spec->max_width_px - ell_width;

    fbf_pen_t pen;
    fbf_pen_reset(&pen);
    uint32_t cursor = line->byte_start;
    uint32_t glyphs = 0;
    uint32_t best_byte = line->byte_start;
    uint32_t best_glyphs = 0;
    int32_t best_width = 0;
    while (cursor < line->byte_end) {
        const uint32_t cp = fbf_utf8_next(text, line->byte_end, &cursor);
        if (cp != '\r') {
            (void)fbf_pen_feed(
                spec->font, &pen,
                fbf_font_glyph_index(spec->font, cp), spec->tracking);
        }
        glyphs++;
        if (pen.pen <= budget && cp != ' ' && cp != '\r') {
            best_byte = cursor;
            best_glyphs = glyphs;
            best_width = pen.pen;
        }
    }
    line->byte_end = best_byte;
    line->glyph_count = (uint16_t)best_glyphs;
    line->width_px = (int16_t)(best_width + ell_width);
    line->flags |= FBF_LINE_ELLIPSIS;
}

bool fbf_layout_utf8(
    const fbf_layout_spec_t *spec, const char *text, uint32_t len,
    fbf_line_t *lines, uint16_t line_capacity, fbf_layout_info_t *info)
{
    if (info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    if (spec == NULL || spec->font == NULL ||
        (text == NULL && len > 0) ||
        (lines == NULL && line_capacity > 0)) {
        return false;
    }

    info->glyph_total = fbf_utf8_count(text, len);

    wrap_iter_t it;
    fbf_line_t scratch;

    /* Pass one: count. */
    wrap_iter_init(&it, spec, text, len);
    uint32_t total = 0;
    while (wrap_iter_next(&it, &scratch)) {
        total++;
        if (total == 0xffff) {
            break; /* line_total saturates; capacity bounds output */
        }
    }
    info->line_total = (uint16_t)total;

    const uint32_t limit = spec->max_lines ? spec->max_lines : 0xffffu;
    uint32_t kept = limit < line_capacity ? limit : line_capacity;
    if (kept > total) {
        kept = total;
    }
    const bool tail = (spec->flags & FBF_LAYOUT_TAIL) != 0;
    const uint32_t skip = tail ? total - kept : 0;

    /* Pass two: emit. */
    wrap_iter_init(&it, spec, text, len);
    uint32_t index = 0;
    uint16_t emitted = 0;
    while (emitted < kept && wrap_iter_next(&it, &scratch)) {
        if (index++ < skip) {
            continue;
        }
        lines[emitted++] = scratch;
    }

    info->line_count = emitted;
    info->lines_skipped = (uint16_t)skip;
    info->truncated = kept < total;

    if (!tail && info->truncated && emitted > 0 &&
        (spec->flags & FBF_LAYOUT_ELLIPSIS) != 0) {
        trim_line_for_ellipsis(spec, text, &lines[emitted - 1]);
    }
    return true;
}
