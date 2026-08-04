#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pixel_face.h"

/*
 * Shared engine for the pixel-character renderer family. Art is composed in
 * an 8-bit palette-index (or grayscale) surface that aliases the caller's
 * RGB565 frame memory, then expanded back-to-front by pf_present(). The
 * aliasing is safe because for every screen pixel the source art byte offset
 * is never greater than the destination byte offset.
 */

#define PF_RGB565(r, g, b) \
    ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

typedef struct {
    uint8_t *px;
    int w;
    int h;
} pf_surface_t;

/* ---- surface + present ------------------------------------------------ */

pf_surface_t pf_surface_attach(uint16_t *fb, int art_w, int art_h);

enum {
    PF_FX_NONE = 0,
    PF_FX_SCANLINE = 1u << 0, /* dim odd screen rows to 75% */
};

void pf_present(
    uint16_t *fb,
    const pf_surface_t *art,
    int scale_x,
    int scale_y,
    const uint16_t *palette,
    unsigned fx);

/* ---- primitives (all clip against the surface) ------------------------ */

void pf_clear(pf_surface_t *s, uint8_t c);
void pf_px(pf_surface_t *s, int x, int y, uint8_t c);
uint8_t pf_get(const pf_surface_t *s, int x, int y);
void pf_hline(pf_surface_t *s, int x0, int x1, int y, uint8_t c);
void pf_vline(pf_surface_t *s, int x, int y0, int y1, uint8_t c);
void pf_fill_rect(pf_surface_t *s, int x, int y, int w, int h, uint8_t c);
void pf_rect(pf_surface_t *s, int x, int y, int w, int h, uint8_t c);
void pf_line(pf_surface_t *s, int x0, int y0, int x1, int y1, uint8_t c);
void pf_fill_ellipse(pf_surface_t *s, int cx, int cy, int rx, int ry,
                     uint8_t c);
void pf_fill_circle(pf_surface_t *s, int cx, int cy, int r, uint8_t c);

/* ---- ordered dither fills --------------------------------------------- */

typedef enum {
    PF_PAT_BAYER8 = 0, /* 8×8 ordered matrix, 65 levels */
    PF_PAT_CHECKER,    /* 2×2 checkerboard (EGA skin shading) */
    PF_PAT_DIAG,       /* diagonal hatching (CGA style) */
} pf_pattern_t;

/* Returns 1 when (x, y) should take the foreground colour at `level` 0..64. */
int pf_dither_pick(pf_pattern_t pat, int level, int x, int y);
void pf_fill_rect_dither(pf_surface_t *s, int x, int y, int w, int h,
                         uint8_t c0, uint8_t c1, pf_pattern_t pat, int level);
void pf_fill_ellipse_dither(pf_surface_t *s, int cx, int cy, int rx, int ry,
                            uint8_t c0, uint8_t c1, pf_pattern_t pat,
                            int level);

/* ---- 1-bit conversions (surface holds 0..255 gray, becomes 0/1) ------- */

void pf_dither_bayer_1bit(pf_surface_t *gray);
void pf_dither_atkinson_1bit(pf_surface_t *gray);

/* ---- string-art sprites ----------------------------------------------- */

/*
 * Rows are ASCII art; `chars[i]` maps to palette index `colours[i]`.
 * '.' and ' ' are always transparent. Ragged rows are allowed.
 */
void pf_blit(pf_surface_t *s, int ox, int oy, const char *const *rows,
             int nrows, const char *chars, const uint8_t *colours);

/* ---- 3×5 micro font (A–Z, 0–9, minimal punctuation) ------------------- */

void pf_text3x5(pf_surface_t *s, int x, int y, const char *str, uint8_t c);
int pf_text3x5_width(const char *str);

/* ---- deterministic integer math --------------------------------------- */

uint32_t pf_hash32(uint32_t x);
int pf_sin8(uint8_t phase); /* -127..127 */
int32_t pf_isqrt(uint32_t v);

static inline int pf_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int pf_mini(int a, int b) { return a < b ? a : b; }
static inline int pf_maxi(int a, int b) { return a > b ? a : b; }
static inline int pf_absi(int v) { return v < 0 ? -v : v; }

/* Linear interpolation with t in 0..256. */
static inline int pf_lerp(int a, int b, int t) {
    return a + (((b - a) * t) >> 8);
}

/* ---- deterministic idle rig ------------------------------------------- */

typedef struct {
    int eye_open_l; /* 0..255, keyframe lid × blink envelope */
    int eye_open_r;
    int blink;      /* 0..255 blink envelope alone (255 = open) */
    int gaze_x;     /* keyframe look + micro-saccades, -127..127 */
    int gaze_y;
    int sacc_x;     /* idle micro-saccade component in art px, -2..2 */
    int sacc_y;
    int brow;       /* keyframe brow + idle acts, -127..127 */
    int breath;     /* -127..127 slow sine */
    int bob;        /* -127..127 speech head-bob signal */
    int flicker;    /* 0..255 smooth deterministic noise */
    uint32_t seed;  /* per-frame hash for micro decisions */
} pf_rig_t;

void pf_rig_compute(pf_rig_t *rig, const face_keyframe_t *k, uint32_t clock,
                    uint32_t salt);

/* ---- mouth systems ----------------------------------------------------- */

typedef enum {
    PF_SHAPE_REST = 0, /* lips together, relaxed */
    PF_SHAPE_MBP,      /* pressed lips: M / B / P */
    PF_SHAPE_FV,       /* lower lip under teeth: F / V */
    PF_SHAPE_SS,       /* clenched teeth: S / Z / T */
    PF_SHAPE_EE,       /* wide and shallow: EE */
    PF_SHAPE_EH,       /* neutral open: EH / DD */
    PF_SHAPE_AA,       /* tall open: AA */
    PF_SHAPE_OO,       /* small round: OO / W */
    PF_SHAPE_OH,       /* tall round: OH */
    PF_SHAPE_SMALL,    /* barely open transition */
    PF_SHAPE_COUNT,
} pf_mouth_shape_t;

pf_mouth_shape_t pf_mouth_classify(const face_keyframe_t *k);

/* Scanline-interpolated lips for the higher-colour styles. */
typedef struct {
    int cx;
    int cy;
    int half_width; /* base half width in art px */
    int open_px;    /* maximum opening in art px */
    uint8_t lip_dark;
    uint8_t lip_mid;
    uint8_t cavity;
    uint8_t teeth;
    uint8_t tongue;
} pf_lips_t;

void pf_draw_lips(pf_surface_t *s, const pf_lips_t *p,
                  const face_keyframe_t *k);

/* ---- renderer entry points (one per profile) --------------------------- */

typedef void (*pf_render_fn)(uint16_t *fb, const face_keyframe_t *k,
                             const pf_rig_t *rig, uint32_t clock);

void pf_render_ega_quest(uint16_t *fb, const face_keyframe_t *k,
                         const pf_rig_t *rig, uint32_t clock);
void pf_render_vga_elder(uint16_t *fb, const face_keyframe_t *k,
                         const pf_rig_t *rig, uint32_t clock);
void pf_render_talkie_closeup(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock);
void pf_render_pixel_automaton(uint16_t *fb, const face_keyframe_t *k,
                               const pf_rig_t *rig, uint32_t clock);
void pf_render_amber_terminal(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock);
void pf_render_pocket_rpg(uint16_t *fb, const face_keyframe_t *k,
                          const pf_rig_t *rig, uint32_t clock);
void pf_render_dithered_rogue(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock);
void pf_render_atkinson_portrait(uint16_t *fb, const face_keyframe_t *k,
                                 const pf_rig_t *rig, uint32_t clock);
void pf_render_zx_attribute(uint16_t *fb, const face_keyframe_t *k,
                            const pf_rig_t *rig, uint32_t clock);
void pf_render_cga_arcade(uint16_t *fb, const face_keyframe_t *k,
                          const pf_rig_t *rig, uint32_t clock);
void pf_render_nes_tile(uint16_t *fb, const face_keyframe_t *k,
                        const pf_rig_t *rig, uint32_t clock);
void pf_render_c64_multicolor(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock);
