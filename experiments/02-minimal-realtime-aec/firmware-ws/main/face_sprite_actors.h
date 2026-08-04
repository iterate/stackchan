/*
 * StackChan sprite actors
 * -----------------------
 *
 * A small, deterministic talking-portrait backend for indexed sprite sheets.
 * The public descriptor is deliberately boring: a palette, one byte-indexed
 * atlas, rectangle references, semantic expression poses and viseme maps.
 * This makes importing original or correctly licensed game-style artwork a
 * data-conversion job rather than a renderer rewrite.
 *
 * The six built-in actors are original procedural pixel artwork released
 * under CC0-1.0. They are drawn by the same semantic pose solver used by the
 * external-sheet path and are not copied or traced from commercial games.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "face_keyframe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FSA_WIDTH = 160,
    FSA_HEIGHT = 120,
    FSA_PIXEL_COUNT = FSA_WIDTH * FSA_HEIGHT,
    FSA_SHEET_MAGIC = 0x46534132U, /* "FSA2" */
    FSA_SHEET_VERSION = 2,
    FSA_CELL_NONE = 0xffffU,
    FSA_MAX_BLINK_FRAMES = 4,
    FSA_MAX_VISEMES = 32,
};

typedef enum {
    FSA_PROFILE_EGA_COURT_MAGE = 0,
    FSA_PROFILE_VGA_STAR_CAPTAIN,
    FSA_PROFILE_TALKIE_MOON_MECHANIC,
    FSA_PROFILE_JRPG_STORM_FAMILIAR,
    FSA_PROFILE_HANDHELD_FOREST_PET,
    FSA_PROFILE_ARCADE_CHROME_PILOT,
    FSA_PROFILE_COUNT,
} fsa_profile_t;

/*
 * A cell is a rectangle in an uncompressed, palette-indexed atlas. Atlas
 * pixels equal to transparent_index do not write. The converter may trim
 * cells while retaining their logical anchor via origin_x/y.
 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    int16_t origin_x;
    int16_t origin_y;
} fsa_cell_t;

/*
 * One semantic pose. eye_* arrays run open -> closed. The pupils are moved
 * inside pupil_*_min/max before compositing, never by moving the eye socket.
 */
typedef struct {
    uint16_t base;
    uint16_t overlay;
    uint16_t mouth_bank;
    uint16_t eye_left[FSA_MAX_BLINK_FRAMES];
    uint16_t eye_right[FSA_MAX_BLINK_FRAMES];
    uint16_t brow_left;
    uint16_t brow_right;
    uint16_t pupil_left;
    uint16_t pupil_right;
    int16_t mouth_x;
    int16_t mouth_y;
    int16_t eye_left_x;
    int16_t eye_left_y;
    int16_t eye_right_x;
    int16_t eye_right_y;
    int16_t brow_left_x;
    int16_t brow_left_y;
    int16_t brow_right_x;
    int16_t brow_right_y;
    int16_t pupil_left_x;
    int16_t pupil_left_y;
    int16_t pupil_right_x;
    int16_t pupil_right_y;
    int8_t pupil_min_x;
    int8_t pupil_max_x;
    int8_t pupil_min_y;
    int8_t pupil_max_y;
    uint8_t blink_frame_count;
    uint8_t reserved[3];
} fsa_pose_t;

typedef struct {
    uint8_t viseme_set;
    uint8_t viseme;
    uint8_t mouth_frame;
    uint8_t reserved;
} fsa_viseme_map_t;

typedef enum {
    /* Authored pixel cells hold, then cut on the next semantic boundary. */
    FSA_TRANSITION_HOLD_CUT = 0,
} fsa_transition_mode_t;

/*
 * Timing is in 16 kHz PCM samples. pose_hold is the minimum authored sprite
 * hold, speech_anticipation widens the eyes before the first active mouth,
 * and speech_settle keeps the last expression after speech ends. blink frames
 * are stepped intentionally; all other controls remain stable between holds.
 */
typedef struct {
    uint16_t pose_hold;
    uint16_t mouth_hold;
    uint16_t speech_anticipation;
    uint16_t speech_settle;
    uint16_t blink_close;
    uint16_t blink_hold;
    uint16_t blink_open;
    uint8_t transition_mode;
    uint8_t speech_eye_boost;
    uint32_t auto_blink_period;
} fsa_timing_t;

/*
 * External sheet format. `mouth_cells` is bank-major:
 *
 *   mouth_cells[pose.mouth_bank * mouth_frames + mouth_frame]
 *
 * `expression_pose[11]` maps StackChan's stage-expression vocabulary to pose
 * records. `fallback_mouth[15]` maps OVR15 when no explicit mapping row
 * exists. Atlas scale may be 1..8; the compositor centers the native canvas
 * and bounds-checks every destination pixel.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t native_width;
    uint16_t native_height;
    uint8_t scale;
    uint8_t transparent_index;
    uint16_t palette_count;
    uint16_t atlas_width;
    uint16_t atlas_height;
    uint16_t cell_count;
    uint16_t pose_count;
    uint16_t mouth_bank_count;
    uint16_t mouth_frames;
    uint16_t viseme_map_count;
    uint16_t reserved;
    uint16_t background_rgb565;
    uint16_t reserved2;
    const uint16_t *palette_rgb565;
    const uint8_t *atlas_pixels;
    const fsa_cell_t *cells;
    const fsa_pose_t *poses;
    const uint16_t *mouth_cells;
    const fsa_viseme_map_t *viseme_map;
    const uint8_t *expression_pose; /* exactly 11 rows */
    const uint8_t *fallback_mouth;  /* exactly 15 rows */
    fsa_timing_t timing;
    const char *name;
} fsa_sheet_t;

typedef struct {
    uint8_t pose;
    uint8_t mouth;
    uint8_t blink_frame;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t head_x;
    int8_t head_y;
    int8_t roll;
    uint8_t anticipation;
    uint8_t settle;
    uint8_t speaking;
    uint8_t reserved;
} fsa_resolved_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t work_width;
    uint16_t work_height;
    uint16_t framebuffer_bytes;
    uint8_t family;
    uint8_t mouth_kind;
    uint8_t flags;
    uint8_t reserved;
    uint16_t estimated_ops_per_pixel;
} fsa_info_t;

size_t fsa_profile_count(void);
const char *fsa_profile_slug(fsa_profile_t profile);
const char *fsa_profile_name(fsa_profile_t profile);
bool fsa_profile_info(fsa_profile_t profile, fsa_info_t *info);

/* Full structural and reference validation for user-supplied sheets. */
bool fsa_validate_sheet(const fsa_sheet_t *sheet);

/* Resolve the deterministic held sprite state without rasterizing. */
bool fsa_resolve(
    const fsa_sheet_t *sheet,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fsa_resolved_t *resolved);

/* Render an imported indexed sheet. Pure, allocation-free, integer-only. */
bool fsa_render_sheet_frame(
    const fsa_sheet_t *sheet,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

/* Render one of the six built-in CC0 visual languages. */
bool fsa_render_frame(
    fsa_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);

#ifdef __cplusplus
}
#endif
