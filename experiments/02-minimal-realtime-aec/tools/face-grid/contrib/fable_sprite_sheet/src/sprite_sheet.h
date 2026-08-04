#pragma once

#include <stdint.h>

/*
 * FSPR: a compact, asset-agnostic sprite-sheet atlas format for talking
 * faces on MCU-class targets.
 *
 * The atlas is pure const data (flash-resident on device, .rodata in the
 * browser build). Pixels are stored as palette indices, optionally
 * PackBits-run-length encoded; the palette is RGB565 so a cell decodes
 * straight into the framebuffer with no per-pixel colour conversion.
 *
 * A face is composited from layers, painter's order:
 *
 *   base -> brows -> eyes (lid state) -> pupils -> mouth -> overlay
 *
 * Every non-base layer is a small delta patch pasted at an anchor, which
 * is the classic talking-portrait trick (Sierra/LucasArts close-ups kept
 * one base face and swapped tiny mouth/eye rectangles). Cells are
 * deduplicated and trimmed at build time by the converter.
 *
 * All coordinates are in native atlas pixels. An atlas declares a scale
 * factor (1 or 2); the engine centres native_width x native_height cells
 * on the 160x120 output and nearest-neighbour upscales while blitting.
 */

enum {
    SPRITE_SHEET_MAGIC = 0x46535052u, /* "FSPR" */
    SPRITE_SHEET_VERSION = 1,
    SPRITE_CELL_NONE = 0xFFFFu,
    SPRITE_MAX_LID_CELLS = 6,
    SPRITE_MAX_BROW_CELLS = 5,
    SPRITE_MAX_CYCLES = 4,
};

typedef enum {
    SPRITE_CELL_ENCODING_RAW = 0,
    SPRITE_CELL_ENCODING_PACKBITS = 1,
} sprite_cell_encoding_t;

/*
 * Canonical mouth-shape identifiers. These are the nine mouth shapes of
 * the Hanna-Barbera / Preston Blair tradition as popularised by Rhubarb
 * Lip Sync (shapes A-H plus X): six core shapes, two teeth shapes, and
 * a rest shape. Sheets do not need to provide all nine; the converter
 * resolves missing shapes through a fallback chain at build time, so
 * the engine always sees a fully populated table. A two-frame
 * open/closed sheet is a valid degenerate case.
 */
typedef enum {
    SPRITE_MOUTH_X = 0, /* rest / idle closed                     */
    SPRITE_MOUTH_A = 1, /* closed, lips pressed (M, B, P)         */
    SPRITE_MOUTH_B = 2, /* slightly open, clenched teeth (S, EE)  */
    SPRITE_MOUTH_C = 3, /* half open (EH, AE, consonants)         */
    SPRITE_MOUTH_D = 4, /* wide open (AA)                         */
    SPRITE_MOUTH_E = 5, /* rounded open (AO, ER)                  */
    SPRITE_MOUTH_F = 6, /* puckered (UW, OO, W)                   */
    SPRITE_MOUTH_G = 7, /* upper teeth on lower lip (F, V)        */
    SPRITE_MOUTH_H = 8, /* long L, tongue raised                  */
    SPRITE_MOUTH_SHAPE_COUNT = 9,
} sprite_mouth_shape_t;

enum {
    SPRITE_SLOT_FLIP_X = 1u << 0,
};

enum {
    SPRITE_SEQ_WHILE_SPEAKING = 1u << 0,
};

enum {
    SPRITE_ATLAS_FLAG_BREATHE = 1u << 0,
    SPRITE_ATLAS_FLAG_IDLE_SACCADES = 1u << 1,
    SPRITE_ATLAS_FLAG_AUTO_BLINK = 1u << 2,
};

/* One rectangle of indexed pixels inside the shared blob. offset_x/y
 * shift a trimmed cell back to its logical box, so anchors stay stable
 * however aggressively the converter trims transparent borders. */
typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t offset_x;
    int16_t offset_y;
    uint32_t data_offset;
    uint32_t data_length;
    uint8_t encoding;
    uint8_t reserved[3];
} sprite_cell_t;

/* Mouth slot: anchor plus a fully resolved shape -> cell table. */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t cells[SPRITE_MOUTH_SHAPE_COUNT];
} sprite_mouth_slot_t;

/* Eye slot: lid cells ordered fully open (index 0) to fully closed
 * (index cell_count - 1). cell_count == 0 disables the layer. */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t cell_count;
    uint16_t cells[SPRITE_MAX_LID_CELLS];
    uint8_t flags;
    uint8_t reserved;
} sprite_eye_slot_t;

/* Pupil slot: one cell drawn at the rest anchor plus a gaze offset.
 * The offset is clamped so the pupil never escapes the eye white. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t min_x;
    int16_t min_y;
    int16_t max_x;
    int16_t max_y;
    uint16_t cell;
    uint8_t range_x;
    uint8_t range_y;
} sprite_pupil_slot_t;

/* Brow slot: level cells ordered lowest (frown) to highest (raised),
 * plus a per-keyframe pixel lift for continuous motion. */
typedef struct {
    int16_t x;
    int16_t y;
    uint16_t cell_count;
    uint16_t cells[SPRITE_MAX_BROW_CELLS];
    uint8_t max_lift;
    uint8_t flags;
} sprite_brow_slot_t;

/* One expression bank. face_keyframe_t.expression selects a bank;
 * out-of-range expressions fall back to bank 0. */
typedef struct {
    uint16_t base_cell;
    sprite_mouth_slot_t mouth;
    sprite_eye_slot_t eye_left;
    sprite_eye_slot_t eye_right;
    sprite_pupil_slot_t pupil_left;
    sprite_pupil_slot_t pupil_right;
    sprite_brow_slot_t brow_left;
    sprite_brow_slot_t brow_right;
} sprite_bank_t;

/* One overlay animation frame: a cell drawn at (x, y) for duration
 * samples of the 16 kHz clock (16 samples per millisecond). */
typedef struct {
    uint16_t cell;
    uint16_t duration;
    int16_t x;
    int16_t y;
} sprite_seq_frame_t;

typedef struct {
    const sprite_seq_frame_t *frames;
    uint16_t frame_count;
    uint8_t flags;
    uint8_t reserved;
} sprite_sequence_t;

/* Classic palette cycling (Mark Ferrari style): palette entries
 * [first, first + count) rotate forward one step every `period`
 * samples. Free ambient animation - glow shimmer, scanner sweeps -
 * with zero extra frame storage. */
typedef struct {
    uint8_t first;
    uint8_t count;
    uint8_t reserved[2];
    uint32_t period;
} sprite_cycle_t;

/* Data-driven thresholds for mapping the continuous mouth parameters of
 * face_keyframe_t onto the nine canonical shapes. Defaults follow the
 * firmware's procedural sprite-mouth classifier so the same keyframe
 * stream reads the same across renderers. All values compare against
 * 0..255 keyframe fields. */
typedef struct {
    uint8_t open_min;      /* below: mouth counts as closed          */
    uint8_t press_min;     /* closed + press above -> shape A        */
    uint8_t teeth_min;     /* above: teeth shapes G/B/H              */
    uint8_t teeth_open;    /* teeth + open above -> B or H           */
    uint8_t teeth_round;   /* teeth + round above -> H               */
    uint8_t round_min;     /* above: rounded shapes E/F              */
    uint8_t round_open;    /* rounded + open above -> E else F       */
    uint8_t wide_min;      /* above: wide shapes D/B                 */
    uint8_t wide_open;     /* wide + open above -> D else B          */
    uint8_t open_wide;     /* open above -> D regardless of width    */
} sprite_selector_t;

/* Deterministic-timing parameters, all in 16 kHz samples. Defaults
 * follow Rhubarb Lip Sync's animation constants (70 ms minimum shape,
 * 120 ms before a pause may close the mouth) and the classic animator
 * blink recipe (fast close, short hold, slower open, one blink every
 * few seconds). */
typedef struct {
    uint16_t mouth_min_hold;    /* min time a mouth shape is shown   */
    uint16_t mouth_close_delay; /* silence needed before rest shape  */
    uint16_t blink_close;       /* lid closing time                  */
    uint16_t blink_hold;        /* lids-shut time                    */
    uint16_t blink_open;        /* lid opening time (slower)         */
    uint16_t reserved;
    uint32_t blink_window;      /* one autonomous blink per window   */
    uint32_t gaze_window;       /* one idle saccade per window       */
    uint32_t idle_window;       /* one idle-act roll per window      */
    uint32_t breathe_period;    /* full bob cycle                    */
} sprite_timing_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t native_width;
    uint16_t native_height;
    uint8_t scale;
    uint8_t transparent_index;
    uint16_t palette_count;
    uint16_t cell_count;
    uint8_t bank_count;
    uint8_t sequence_count;
    uint8_t flags;
    uint8_t reserved;
    uint16_t background;    /* RGB565 letterbox / show-through fill */
    sprite_selector_t selector;
    sprite_timing_t timing;
    const uint16_t *palette;         /* RGB565[palette_count]       */
    const sprite_cell_t *cells;
    const uint8_t *blob;
    uint32_t blob_size;
    const sprite_bank_t *banks;
    const sprite_sequence_t *sequences;
    const sprite_cycle_t *cycles;
    uint8_t cycle_count;
    const char *name;
} sprite_atlas_t;

/* Wire-stable defaults used by the converter when a manifest does not
 * override them; exposed so tests and docs stay in sync.
 *
 * Timing: 70 ms shape hold and 120 ms close delay are Rhubarb's
 * constants; blink 80/40/120 ms sits inside the physiological
 * 100-400 ms envelope with the fast-close slow-open convention; one
 * blink window per 4 s matches conversational blink rates. */
#define SPRITE_SELECTOR_DEFAULTS \
    { 20, 128, 160, 96, 140, 150, 128, 170, 120, 170 }

#define SPRITE_TIMING_DEFAULTS \
    { 1120, 1920, 1280, 640, 1920, 0, 64000, 20800, 96000, 67200 }
