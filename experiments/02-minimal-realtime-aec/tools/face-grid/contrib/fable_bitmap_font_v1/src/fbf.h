#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * fbf — fable bitmap font v1.
 *
 * A tiny, allocation-free bitmap-font and transcript-layout runtime that
 * compiles unchanged for ESP32-S3 (ESP-IDF / xtensa) and WebAssembly.
 *
 * Everything is integer arithmetic on caller-owned memory. The runtime never
 * allocates, never retains hidden state, and produces byte-identical output
 * across native, WASM, and optimization levels. Fonts are flash-resident
 * palette-indexed glyph blobs compiled by tools/fbfc.py (schema: DESIGN.md).
 *
 * Layers, lowest to highest:
 *
 *   fbf_font_*        blob validation, glyph lookup, UTF-8, metrics, kerning
 *   fbf_layout_*      word wrap, max lines, ellipsis, tail windows, reveal
 *   fbf_draw_* /      RGB565 + RGBA8888 blitting, nearest-neighbour scaling,
 *   fbf_emit ops      or deterministic retained draw commands
 *   fbf_transcript_*  realtime conversation model: deltas, partials, speaker
 *                     styles, reveal paced by the 16 kHz sample clock
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ font */

enum {
    FBF_BLOB_MAGIC = 0x31464246u, /* "FBF1" little-endian */
    FBF_BLOB_VERSION = 1,
    FBF_HEADER_BYTES = 40,
    FBF_GLYPH_RECORD_BYTES = 16,
    FBF_KERN_RECORD_BYTES = 6,
    FBF_GLYPH_NONE = 0xffff,
    FBF_REPLACEMENT_CODEPOINT = 0xfffd,
    FBF_ELLIPSIS_CODEPOINT = 0x2026,
    FBF_MAX_GLYPH_SIDE = 32, /* schema limit on glyph width/height in px */
};

/*
 * A parsed font. `blob` stays owned by the caller (typically a const flash
 * array); init only validates bounds and caches table pointers. sizeof is
 * small and constant, so fonts can live in static storage.
 */
typedef struct {
    const uint8_t *blob;
    uint32_t blob_size;
    const uint8_t *glyphs;   /* glyph_count fixed-size records            */
    const uint8_t *kerns;    /* kern_count fixed-size records             */
    const uint8_t *bitmaps;  /* shared 2-bit-per-pixel glyph pixel pool   */
    uint32_t bitmap_size;
    uint16_t glyph_count;
    uint16_t kern_count;
    uint16_t fallback_index; /* glyph drawn for unmapped codepoints       */
    uint8_t line_height;     /* baseline-to-baseline distance in font px  */
    uint8_t ascent;          /* px above baseline, cap height inclusive   */
    uint8_t descent;         /* px below baseline                         */
    int8_t tracking;         /* default extra px between glyphs           */
} fbf_font_t;

/* One decoded glyph record. Pixels are 2-bit palette indices, row-major,
 * each row padded to a whole byte. Index 0 is transparent. */
typedef struct {
    uint32_t codepoint;
    uint32_t bitmap_offset; /* into fbf_font_t.bitmaps */
    uint8_t advance;        /* pen advance in font px  */
    uint8_t width;          /* bitmap width in px      */
    uint8_t height;         /* bitmap height in px     */
    int8_t bearing_x;       /* pen to left edge        */
    int8_t bearing_y;       /* baseline up to top row  */
} fbf_glyph_t;

/* Validate a compiled blob and cache table pointers. Returns false (and
 * leaves *font zeroed) on any structural problem; a false font must not be
 * passed to other calls. */
bool fbf_font_init(fbf_font_t *font, const void *blob, uint32_t blob_size);

/* Glyph index for a codepoint; falls back to the font's replacement glyph
 * and only returns FBF_GLYPH_NONE if the font lacks one. */
uint16_t fbf_font_glyph_index(const fbf_font_t *font, uint32_t codepoint);

/* Decode record `index`; false when index is FBF_GLYPH_NONE/out of range. */
bool fbf_font_glyph(
    const fbf_font_t *font, uint16_t index, fbf_glyph_t *out);

/* Kerning adjustment in font px applied between two glyph indices. */
int8_t fbf_font_kern(
    const fbf_font_t *font, uint16_t left_index, uint16_t right_index);

/* Palette index (0..3) of one glyph pixel; 0 outside the bitmap. */
uint8_t fbf_font_glyph_pixel(
    const fbf_font_t *font, const fbf_glyph_t *glyph, uint8_t x, uint8_t y);

/* ----------------------------------------------------------------- utf-8 */

/*
 * Decode the next scalar starting at *cursor (< len). Advances *cursor by
 * the consumed byte count and returns the codepoint.
 *
 * Malformed input is deterministic, mirroring the WHATWG replacement rule:
 * every invalid sequence yields one U+FFFD per rejected byte and always
 * consumes at least one byte, so any byte stream decodes to the same glyph
 * sequence on every platform.
 */
uint32_t fbf_utf8_next(const char *text, uint32_t len, uint32_t *cursor);

/* Number of scalars fbf_utf8_next() would yield for the whole buffer. */
uint32_t fbf_utf8_count(const char *text, uint32_t len);

/* --------------------------------------------------------------- measure */

/* Width in font px of one run (no wrapping; newlines measure as glyphs of
 * width 0). Includes kerning and per-pair tracking. */
int32_t fbf_measure_utf8(
    const fbf_font_t *font, const char *text, uint32_t len, int8_t tracking);

/* ---------------------------------------------------------------- layout */

enum {
    /* Truncated output ends with an ellipsis run on its last line. */
    FBF_LAYOUT_ELLIPSIS = 1u << 0,
    /* Keep the LAST lines instead of the first (live transcript tail). */
    FBF_LAYOUT_TAIL = 1u << 1,
};

enum {
    FBF_LINE_ELLIPSIS = 1u << 0,   /* renderer appends … after this line */
    FBF_LINE_HARD_BREAK = 1u << 1, /* line ended at \n in the source     */
};

typedef struct {
    uint32_t byte_start;  /* line text is [byte_start, byte_end)         */
    uint32_t byte_end;    /* excludes the break space / newline          */
    uint32_t glyph_start; /* scalars before this line, whole text        */
    uint16_t glyph_count; /* scalars on this line                        */
    int16_t width_px;     /* measured width in font px                   */
    uint8_t flags;
    uint8_t reserved;
} fbf_line_t;

typedef struct {
    const fbf_font_t *font;
    int16_t max_width_px; /* wrap budget in font px (pre-scale)          */
    uint16_t max_lines;   /* 0 = unlimited (bounded by line_capacity)    */
    uint16_t flags;
    int8_t tracking;
} fbf_layout_spec_t;

typedef struct {
    uint16_t line_count;    /* lines written to the caller's array      */
    uint16_t line_total;    /* lines the text needs with no line limit  */
    uint16_t lines_skipped; /* leading lines dropped by FBF_LAYOUT_TAIL */
    uint32_t glyph_total;   /* scalars in the whole source text         */
    bool truncated;         /* some source text is not represented      */
} fbf_layout_info_t;

/*
 * Greedy word wrap over one UTF-8 buffer. Breaks at spaces and hard
 * newlines; a word wider than max_width_px falls back to per-glyph
 * breaking, so layout always progresses. Trailing break spaces are
 * excluded from lines. Deterministic: two passes, O(n), no memory beyond
 * the caller's `lines` array.
 *
 * `lines` may be NULL with line_capacity 0 to count lines only (info is
 * still filled, including line_total and glyph_total).
 *
 * Returns false only on invalid arguments. `lines`/`line_capacity` bound
 * the output; `info` is always filled.
 */
bool fbf_layout_utf8(
    const fbf_layout_spec_t *spec, const char *text, uint32_t len,
    fbf_line_t *lines, uint16_t line_capacity, fbf_layout_info_t *info);

/* ---------------------------------------------------------- color/style */

/* One resolved color carried in both target encodings so blitters never
 * convert per pixel. rgba byte order is R,G,B,A (canvas ImageData). */
typedef struct {
    uint16_t rgb565;
    uint8_t rgba[4];
} fbf_color_t;

fbf_color_t fbf_color_rgb(uint8_t r, uint8_t g, uint8_t b);

/*
 * A style maps the four glyph palette indices to colors. Index 0 is always
 * transparent; draw_mask bit i enables palette index i (a dim "thinking"
 * style can e.g. drop the accent plane). fbf_style_make derives shade
 * (darker) and accent (lighter) planes from the ink color with integer
 * math only.
 */
typedef struct {
    fbf_color_t color[4];
    uint8_t draw_mask;
} fbf_style_t;

fbf_style_t fbf_style_make(uint8_t r, uint8_t g, uint8_t b);

/* -------------------------------------------------- speaker/activity map */

typedef enum {
    FBF_SPEAKER_ASSISTANT = 0,
    FBF_SPEAKER_USER = 1,
    FBF_SPEAKER_SYSTEM = 2,
    FBF_SPEAKER_COUNT = 3,
} fbf_speaker_t;

typedef enum {
    FBF_TEXT_FINAL = 0,    /* committed transcript text     */
    FBF_TEXT_PARTIAL = 1,  /* tentative ASR / streaming end */
    FBF_TEXT_THINKING = 2, /* pre-speech placeholder        */
    FBF_TEXT_STATE_COUNT = 3,
} fbf_text_state_t;

/* Built-in palette: assistant mint, user amber, system grey; partial and
 * thinking states dim toward the background. Always non-NULL. */
const fbf_style_t *fbf_style_speaker(uint8_t speaker, uint8_t state);

/* --------------------------------------------------------------- surface */

typedef enum {
    FBF_FORMAT_RGB565 = 0,   /* uint16 per pixel, host order */
    FBF_FORMAT_RGBA8888 = 1, /* 4 bytes per pixel, R,G,B,A   */
} fbf_format_t;

typedef struct {
    void *pixels;
    int16_t width;
    int16_t height;
    int32_t stride_bytes; /* row pitch; 0 = tightly packed */
    uint8_t format;
} fbf_surface_t;

fbf_surface_t fbf_surface_rgb565(
    uint16_t *pixels, int16_t width, int16_t height);
fbf_surface_t fbf_surface_rgba8888(
    uint8_t *pixels, int16_t width, int16_t height);

/* Opaque rectangle fill, clipped to the surface. */
void fbf_fill(
    fbf_surface_t *surface, int16_t x, int16_t y, int16_t w, int16_t h,
    fbf_color_t color);

/* ------------------------------------------------------------------ draw */

/*
 * One deterministic retained draw command. Emitting commands instead of
 * pixels lets M5GFX/LVGL/dirty-rect backends draw glyphs themselves; the
 * unit test proves replaying a command list byte-matches direct rendering.
 */
typedef struct {
    uint16_t glyph_index;
    int16_t x; /* device px, top-left of the scaled bitmap */
    int16_t y;
    uint8_t scale;      /* 1..4 nearest-neighbour           */
    uint8_t style_slot; /* caller-defined style table index */
} fbf_draw_op_t;

/* Fixed-capacity command sink. Overflow sets `overflowed` and drops ops
 * (deterministically) instead of writing out of bounds. */
typedef struct {
    fbf_draw_op_t *ops;
    uint16_t capacity;
    uint16_t count;
    bool overflowed;
} fbf_op_list_t;

fbf_op_list_t fbf_op_list(fbf_draw_op_t *ops, uint16_t capacity);

/* Blit one glyph. (pen_x, baseline_y) are device px; glyph metrics are
 * scaled by `scale`. Fully clipped against the surface. */
void fbf_draw_glyph(
    fbf_surface_t *surface, const fbf_font_t *font, uint16_t glyph_index,
    int16_t pen_x, int16_t baseline_y, const fbf_style_t *style,
    uint8_t scale);

/* Replay a recorded op through the same blitter (style table indexed by
 * style_slot; out-of-range slots fall back to slot 0). */
void fbf_draw_op(
    fbf_surface_t *surface, const fbf_font_t *font, const fbf_draw_op_t *op,
    const fbf_style_t *const *styles, uint16_t style_count);

/*
 * Draw or record one run. Either sink may be NULL: surface-only renders,
 * ops-only records, both does both in one pass. Returns the final pen x in
 * device px. reveal_glyphs limits how many leading scalars are shown
 * (UINT32_MAX = all); style_slot tags recorded ops.
 */
int32_t fbf_draw_text(
    fbf_surface_t *surface, fbf_op_list_t *ops, const fbf_font_t *font,
    const char *text, uint32_t len, int16_t pen_x, int16_t baseline_y,
    const fbf_style_t *style, uint8_t style_slot, uint8_t scale,
    int8_t tracking, uint32_t reveal_glyphs);

/* ------------------------------------------------------------------ view */

enum {
    FBF_VIEW_FILL_BG = 1u << 0, /* fill the safe area before drawing */
    FBF_VIEW_TAIL = 1u << 1,    /* transcript keeps its newest lines */
};

/*
 * A transcript viewport: the safe area in device pixels plus the glyph
 * scale used inside it. max_lines = 0 derives the line budget from the
 * safe-area height and the font's scaled line height.
 */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint8_t scale;
    uint8_t max_lines;
    uint8_t flags;
    int8_t line_spacing_px; /* extra device px between lines */
    fbf_color_t background;
} fbf_view_t;

/* Device presets (safe areas chosen in INTEGRATION.md):
 * CoreS3  320x240 landscape — lower transcript band under the face.
 * StickS3 135x240 portrait  — lower transcript band, 1x micro font. */
fbf_view_t fbf_view_cores3_band(void);
fbf_view_t fbf_view_sticks3_band(void);

/* ------------------------------------------------------------ transcript */

enum {
    FBF_ENTRY_PARTIAL = 1u << 0, /* still open; text may extend/replace */
    FBF_ENTRY_INSTANT = 1u << 1, /* skip reveal pacing for this entry   */
    FBF_TRANSCRIPT_MAX_VIEW_LINES = 24,
    FBF_SAMPLE_RATE_HZ = 16000,
};

typedef struct {
    uint32_t byte_start; /* into the transcript text buffer */
    uint32_t byte_len;
    uint8_t speaker;
    uint8_t flags;
    uint16_t reserved;
} fbf_entry_t;

/*
 * Fixed-memory conversation model. The caller owns `text` and `entries`;
 * eviction of the oldest entries makes room when either fills, so the
 * transcript runs forever in constant space. Only the newest entry can be
 * open (partial); reveal is a single monotonic glyph cursor over the
 * concatenated entry text, advanced by the 16 kHz PCM sample clock.
 */
typedef struct {
    char *text;
    uint32_t text_capacity;
    uint32_t text_len;
    fbf_entry_t *entries;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint32_t samples_per_glyph; /* 0 = reveal instantly */
    uint32_t revealed_glyphs;
    uint32_t reveal_accum; /* carried samples < samples_per_glyph */
} fbf_transcript_t;

void fbf_transcript_init(
    fbf_transcript_t *t, char *text_buf, uint32_t text_capacity,
    fbf_entry_t *entries, uint16_t entry_capacity,
    uint32_t samples_per_glyph);

void fbf_transcript_clear(fbf_transcript_t *t);

/* Append a delta to the open entry for `speaker`, opening one (and
 * committing any other speaker's open entry) as needed. Returns false only
 * when the delta itself exceeds total capacity. */
bool fbf_transcript_append(
    fbf_transcript_t *t, uint8_t speaker, uint8_t flags,
    const char *utf8, uint32_t len);

/* Replace the open entry's text wholesale (ASR partial hypothesis).
 * Equivalent to append when no entry is open. */
bool fbf_transcript_set_partial(
    fbf_transcript_t *t, uint8_t speaker, uint8_t flags,
    const char *utf8, uint32_t len);

/* Close the open entry, marking its text final. No-op when none is open. */
void fbf_transcript_commit(fbf_transcript_t *t);

/* Advance the reveal cursor by elapsed PCM samples (16 kHz clock). */
void fbf_transcript_advance(fbf_transcript_t *t, uint32_t pcm_samples);

/* Glyphs currently revealed / total glyphs, for reveal-complete checks. */
uint32_t fbf_transcript_revealed(const fbf_transcript_t *t);
uint32_t fbf_transcript_glyphs(const fbf_transcript_t *t);

/*
 * Lay out and draw the transcript into a view. Entries render as
 * paragraphs, newest kept visible when the view has FBF_VIEW_TAIL; the
 * open entry uses the speaker's partial style. Either sink may be NULL,
 * as in fbf_draw_text. Returns the number of lines drawn.
 */
int16_t fbf_transcript_render(
    const fbf_transcript_t *t, const fbf_font_t *font,
    const fbf_view_t *view, fbf_surface_t *surface, fbf_op_list_t *ops);

#ifdef __cplusplus
}
#endif
