#include "fable_studies.h"

#include "fable_ease.h"

enum {
    W = FABLE_STUDY_WIDTH,
    H = FABLE_STUDY_HEIGHT,
};

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xf8U) << 8) | (((g) & 0xfcU) << 3) | \
                (((b) & 0xf8U) >> 3)))

/* Quarter-pixel offsets truncate toward zero so both signs match. */
static int32_t q2px(int32_t v)
{
    return v / 4;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return fable_clamp_i32(v, lo, hi);
}

/* ------------------------------------------------------------------ */
/* Span-based primitives (all clipped to the canvas)                   */
/* ------------------------------------------------------------------ */

static void hspan(uint16_t *fb, int32_t x0, int32_t x1, int32_t y,
                  uint16_t c)
{
    if (y < 0 || y >= H) {
        return;
    }
    x0 = clampi(x0, 0, W - 1);
    x1 = clampi(x1, 0, W - 1);
    if (x1 < x0) {
        return;
    }
    uint16_t *row = fb + (size_t)y * W;
    for (int32_t x = x0; x <= x1; x++) {
        row[x] = c;
    }
}

static void vspan(uint16_t *fb, int32_t x, int32_t y0, int32_t y1,
                  uint16_t c)
{
    if (x < 0 || x >= W) {
        return;
    }
    y0 = clampi(y0, 0, H - 1);
    y1 = clampi(y1, 0, H - 1);
    for (int32_t y = y0; y <= y1; y++) {
        fb[(size_t)y * W + x] = c;
    }
}

static void fill_rect(uint16_t *fb, int32_t x, int32_t y, int32_t w,
                      int32_t h, uint16_t c)
{
    for (int32_t i = 0; i < h; i++) {
        hspan(fb, x, x + w - 1, y + i, c);
    }
}

static void fill_rrect(uint16_t *fb, int32_t x, int32_t y, int32_t w,
                       int32_t h, int32_t r, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const int32_t max_r = (w < h ? w : h) / 2;
    if (r > max_r) {
        r = max_r;
    }
    for (int32_t i = 0; i < h; i++) {
        int32_t dy = 0;
        if (i < r) {
            dy = r - i;
        } else if (i >= h - r) {
            dy = i - (h - r) + 1;
        }
        int32_t inset = 0;
        if (dy > 0) {
            inset = r - (int32_t)fable_isqrt(
                (uint32_t)(r * r - dy * dy));
        }
        hspan(fb, x + inset, x + w - 1 - inset, y + i, c);
    }
}

static void fill_ellipse(uint16_t *fb, int32_t cx, int32_t cy,
                         int32_t rx, int32_t ry, uint16_t c)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (int32_t dy = -ry; dy <= ry; dy++) {
        const int32_t half =
            (rx * (int32_t)fable_isqrt(
                       (uint32_t)(ry * ry - dy * dy))) / ry;
        hspan(fb, cx - half, cx + half, cy + dy, c);
    }
}

/*
 * Background: vertical gradient between two colors, dithered only by the
 * 6-bit green channel of RGB565 (no per-pixel work: one color per row).
 */
static void fill_bg(uint16_t *fb, int32_t rt, int32_t gt, int32_t bt,
                    int32_t rb, int32_t gb, int32_t bb)
{
    for (int32_t y = 0; y < H; y++) {
        const int32_t r = rt + ((rb - rt) * y) / (H - 1);
        const int32_t g = gt + ((gb - gt) * y) / (H - 1);
        const int32_t b = bt + ((bb - bt) * y) / (H - 1);
        const uint16_t c =
            RGB565((uint32_t)r, (uint32_t)g, (uint32_t)b);
        uint16_t *row = fb + (size_t)y * W;
        for (int32_t x = 0; x < W; x++) {
            row[x] = c;
        }
    }
}

/*
 * Upper eyelid: a slanted cover in the surround color. closed_px is the
 * coverage at the eye centre; tilt shifts the cover edge linearly across
 * the eye (positive tilt lifts the inner edge - pass mirrored signs for
 * the two eyes). A 2 px darker band draws the lash line when the lid is
 * visibly lowered.
 */
static void draw_lid(uint16_t *fb, int32_t x, int32_t y, int32_t w,
                     int32_t h, int32_t closed_px, int32_t tilt,
                     uint16_t cover, uint16_t edge)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const int32_t cx = x + w / 2;
    for (int32_t px = x; px < x + w; px++) {
        int32_t cover_px = closed_px + (tilt * (px - cx)) / 48;
        cover_px = clampi(cover_px, 0, h);
        if (cover_px <= 0) {
            continue;
        }
        vspan(fb, px, y, y + cover_px - 1, cover);
        if (closed_px >= 2) {
            vspan(fb, px, y + cover_px - 1, y + cover_px, edge);
        }
    }
}

/* Slanted brow stroke: linear column interpolation, fixed thickness. */
static void draw_brow(uint16_t *fb, int32_t x0, int32_t y0, int32_t x1,
                      int32_t y1, int32_t thickness, uint16_t c)
{
    if (x1 <= x0) {
        return;
    }
    for (int32_t x = x0; x <= x1; x++) {
        const int32_t y = y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
        vspan(fb, x, y, y + thickness - 1, c);
    }
}

/* Segment with linear x interpolation over rows (antenna stems). */
static void draw_stem(uint16_t *fb, int32_t x0, int32_t y0, int32_t x1,
                      int32_t y1, int32_t thickness, uint16_t c)
{
    if (y1 <= y0) {
        return;
    }
    for (int32_t y = y0; y <= y1; y++) {
        const int32_t x = x0 + ((x1 - x0) * (y - y0)) / (y1 - y0);
        hspan(fb, x, x + thickness - 1, y, c);
    }
}

/*
 * Shared keyframe mouth: closed lips render as a bar, open mouths as a
 * rounded cavity; mouth_round trades width for roundness, mouth_press
 * thins the shape, mouth_teeth adds a light band across the cavity.
 */
static void draw_mouth(uint16_t *fb, const fable_motion_pose_t *pose,
                       int32_t cx, int32_t cy, int32_t max_w,
                       int32_t max_h, uint16_t lip, uint16_t cavity,
                       uint16_t teeth)
{
    int32_t w = (max_w * (96 + ((int32_t)pose->mouth_width * 160) / 255)) /
                256;
    int32_t h = (max_h * (int32_t)pose->mouth_open) / 255;
    const int32_t round_q8 = (int32_t)pose->mouth_round;
    const int32_t press = (int32_t)pose->mouth_press;

    w -= (w * round_q8) / 640;          /* rounder mouths narrow */
    h -= (h * press) / 512;             /* pressed lips flatten */
    w = clampi(w, 6, max_w);
    h = clampi(h, 0, max_h);

    if (h < 3) {
        const int32_t bar = press > 128 ? 1 : 2;
        fill_rrect(fb, cx - w / 2, cy - bar, w, 2 * bar, bar, lip);
        return;
    }
    if (round_q8 > 140) {
        fill_ellipse(fb, cx, cy, w / 2 + 1, h / 2 + 1, lip);
        fill_ellipse(fb, cx, cy, w / 2 - 1, h / 2 - 1, cavity);
    } else {
        const int32_t r = 2 + (h * round_q8) / 640;
        fill_rrect(fb, cx - w / 2 - 1, cy - h / 2 - 1, w + 2, h + 2,
                   r + 1, lip);
        fill_rrect(fb, cx - w / 2 + 1, cy - h / 2 + 1, w - 2, h - 2,
                   r, cavity);
    }
    if (pose->mouth_teeth > 96 && h >= 6) {
        const int32_t band = clampi(h / 4, 2, 5);
        fill_rect(fb, cx - w / 2 + 3, cy - h / 2 + 1, w - 6, band,
                  teeth);
    }
}

/* Resolve activity the same way the engine does (staging study). */
static uint8_t activity_of(const fable_keyframe_t *kf)
{
    if (kf == NULL) {
        return FABLE_ACTIVITY_IDLE;
    }
    if (kf->expression <= FABLE_ACTIVITY_SPEAKING) {
        if (kf->expression == FABLE_ACTIVITY_IDLE &&
            (kf->flags & FABLE_KEYFRAME_FLAG_SPEAKING) != 0U) {
            return FABLE_ACTIVITY_SPEAKING;
        }
        return kf->expression;
    }
    return (kf->flags & FABLE_KEYFRAME_FLAG_SPEAKING) != 0U
               ? FABLE_ACTIVITY_SPEAKING
               : FABLE_ACTIVITY_IDLE;
}

/* Lid coverage in pixels for an eye of height eye_h. */
static int32_t lid_cover_px(uint16_t lid_q10, int32_t eye_h)
{
    return (eye_h * (FABLE_ONE - (int32_t)lid_q10)) >> 10;
}

/* ------------------------------------------------------------------ */
/* Study 1: Curious Scout - lead and follow, anticipation, arcs        */
/* ------------------------------------------------------------------ */

static void draw_curious_scout(uint16_t *fb,
                               const fable_motion_pose_t *pose)
{
    const uint16_t bg_eye = RGB565(90, 225, 255);
    const uint16_t eye_core = RGB565(180, 246, 255);
    const uint16_t cover = RGB565(10, 13, 32);
    const uint16_t lash = RGB565(34, 90, 120);
    const uint16_t stem_c = RGB565(60, 130, 160);

    fill_bg(fb, 8, 10, 28, 16, 20, 48);

    const int32_t ex = q2px(pose->eye_x_q2);
    const int32_t ey = q2px(pose->eye_y_q2);
    const int32_t hx = q2px(pose->head_x_q2);
    const int32_t hy = q2px(pose->head_y_q2);
    const int32_t bx = q2px(pose->body_x_q2);
    const int32_t by = q2px(pose->body_y_q2);
    const int32_t breath_lift = ((int32_t)pose->breath - 128) / 64;

    /* Antenna: base rides the head, tip drags with the body layer
       (exaggerated follow-through). */
    const int32_t base_x = 80 + hx;
    const int32_t base_y = 24 + hy + breath_lift;
    const int32_t tip_x = base_x + (bx - hx) * 3;
    const int32_t tip_y = base_y - 13 + (by - hy);
    draw_stem(fb, tip_x, tip_y, base_x, base_y, 2, stem_c);
    fill_ellipse(fb, tip_x + 1, tip_y, 3, 3, bg_eye);

    /* Eyes float on the dark field, Cozmo-style rounded panels. */
    const int32_t eye_w = 34 - (int32_t)pose->stretch / 16;
    const int32_t eye_h = 30 + (int32_t)pose->stretch / 8;
    const int32_t eye_cy = 56 + ey + breath_lift;

    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        const int32_t cx = 80 + sgn * 27 + ex;
        const int32_t x = cx - eye_w / 2;
        const int32_t y = eye_cy - eye_h / 2;
        fill_rrect(fb, x, y, eye_w, eye_h, 8, bg_eye);
        fill_rrect(fb, x + 4, y + 4, eye_w - 8, eye_h - 8, 5,
                   eye_core);
        const uint16_t lid_q10 =
            side == 0 ? pose->lid_left_q10 : pose->lid_right_q10;
        draw_lid(fb, x - 1, y - 1, eye_w + 2, eye_h + 2,
                 lid_cover_px(lid_q10, eye_h + 2),
                 -sgn * (int32_t)pose->lid_tilt, cover, lash);

        const int32_t brow =
            side == 0 ? pose->brow_left : pose->brow_right;
        const int32_t brow_y = y - 9 - brow / 8;
        draw_brow(fb, cx - 14, brow_y + sgn * ((int32_t)brow / 12),
                  cx + 14, brow_y - sgn * ((int32_t)brow / 12), 3,
                  stem_c);
    }

    draw_mouth(fb, pose, 80 + hx, 97 + hy, 34, 16,
               RGB565(70, 180, 210), RGB565(16, 34, 56),
               RGB565(150, 230, 245));
}

/* ------------------------------------------------------------------ */
/* Study 2: Ember Breath - squash and stretch, blink phrasing          */
/* ------------------------------------------------------------------ */

static void draw_ember_breath(uint16_t *fb,
                              const fable_motion_pose_t *pose)
{
    const uint16_t shadow = RGB565(64, 26, 10);
    const uint16_t plate = RGB565(198, 100, 34);
    const uint16_t plate_rim = RGB565(140, 62, 20);
    const uint16_t eye_c = RGB565(255, 216, 140);
    const uint16_t lash = RGB565(120, 52, 18);

    fill_bg(fb, 26, 12, 6, 42, 19, 8);

    const int32_t hx = q2px(pose->head_x_q2);
    const int32_t hy = q2px(pose->head_y_q2);
    const int32_t bx = q2px(pose->body_x_q2);
    const int32_t by = q2px(pose->body_y_q2);
    const int32_t ex = q2px(pose->eye_x_q2);
    const int32_t ey = q2px(pose->eye_y_q2);

    /* Volume-conserving squash and stretch driven by the breath. */
    const int32_t stretch = (int32_t)pose->stretch;
    const int32_t plate_h = 92 + stretch / 2;
    const int32_t plate_w = 124 - stretch / 3;
    const int32_t plate_x = 80 + hx - plate_w / 2;
    /* The base of the body stays planted; the top rises on inhale. */
    const int32_t plate_y = 106 + hy - plate_h;

    /* Under-shadow drags with the body layer: cheap depth. */
    fill_rrect(fb, plate_x + (bx - hx) * 2 - 2, plate_y + (by - hy) + 4,
               plate_w + 4, plate_h + 2, 24, shadow);
    fill_rrect(fb, plate_x, plate_y, plate_w, plate_h, 22, plate_rim);
    fill_rrect(fb, plate_x + 3, plate_y + 3, plate_w - 6, plate_h - 6,
               19, plate);

    const int32_t eye_ry = 13 + stretch / 12;
    const int32_t eye_cy = plate_y + 34 + (ey - hy);

    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        const int32_t cx = 80 + sgn * 30 + ex;
        fill_ellipse(fb, cx, eye_cy, 13, eye_ry, eye_c);
        const uint16_t lid_q10 =
            side == 0 ? pose->lid_left_q10 : pose->lid_right_q10;
        draw_lid(fb, cx - 14, eye_cy - eye_ry - 1, 29, 2 * eye_ry + 2,
                 lid_cover_px(lid_q10, 2 * eye_ry + 2),
                 -sgn * (int32_t)pose->lid_tilt, plate, lash);

        const int32_t brow =
            side == 0 ? pose->brow_left : pose->brow_right;
        const int32_t brow_y = eye_cy - eye_ry - 8 - brow / 8;
        draw_brow(fb, cx - 11, brow_y + sgn * (brow / 16), cx + 11,
                  brow_y - sgn * (brow / 16), 3, plate_rim);
    }

    draw_mouth(fb, pose, 80 + hx, plate_y + plate_h - 22, 40, 18,
               RGB565(110, 44, 16), RGB565(52, 18, 8),
               RGB565(255, 236, 190));
}

/* ------------------------------------------------------------------ */
/* Study 3: Pip Spark - overshoot, exaggeration, secondary cheeks      */
/* ------------------------------------------------------------------ */

static void draw_pip_spark(uint16_t *fb, const fable_motion_pose_t *pose)
{
    const uint16_t eye_c = RGB565(170, 255, 210);
    const uint16_t eye_core = RGB565(235, 255, 244);
    const uint16_t cover = RGB565(6, 14, 14);
    const uint16_t lash = RGB565(40, 120, 92);
    const uint16_t cheek = RGB565(255, 130, 150);

    fill_bg(fb, 5, 12, 12, 10, 26, 24);

    /* Exaggeration: head motion reads at 150%, so the persona's
       overshoot is unmistakable. */
    const int32_t ex = q2px(pose->eye_x_q2 * 3) / 2;
    const int32_t ey = q2px(pose->eye_y_q2 * 3) / 2;
    const int32_t hx = q2px(pose->head_x_q2 * 3) / 2;
    const int32_t hy = q2px(pose->head_y_q2 * 3) / 2;
    const int32_t bx = q2px(pose->body_x_q2 * 3) / 2;
    const int32_t by = q2px(pose->body_y_q2 * 3) / 2;
    const int32_t stretch = (int32_t)pose->stretch;
    const int32_t bob = ((int32_t)pose->breath - 128) / 48;

    /* Secondary action: cheek dots trail on the body layer. */
    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        fill_ellipse(fb, 80 + sgn * 38 + bx, 74 + by + bob, 5, 4,
                     cheek);
    }

    const int32_t eye_w = 18 - stretch / 8;
    const int32_t eye_h = 22 + stretch / 4;
    const int32_t eye_cy = 56 + ey + bob;

    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        const int32_t cx = 80 + sgn * 22 + ex;
        const int32_t x = cx - eye_w / 2;
        const int32_t y = eye_cy - eye_h / 2;
        fill_rrect(fb, x, y, eye_w, eye_h, 7, eye_c);
        fill_rrect(fb, x + 3, y + 3, eye_w - 6, eye_h - 6, 4,
                   eye_core);
        const uint16_t lid_q10 =
            side == 0 ? pose->lid_left_q10 : pose->lid_right_q10;
        draw_lid(fb, x - 1, y - 1, eye_w + 2, eye_h + 2,
                 lid_cover_px(lid_q10, eye_h + 2),
                 -sgn * (int32_t)pose->lid_tilt, cover, lash);

        const int32_t brow =
            side == 0 ? pose->brow_left : pose->brow_right;
        draw_brow(fb, cx - 8, y - 7 - brow / 8 + sgn * (brow / 16),
                  cx + 8, y - 7 - brow / 8 - sgn * (brow / 16), 2,
                  lash);
    }

    draw_mouth(fb, pose, 80 + hx, 90 + hy, 26, 14,
               RGB565(90, 210, 170), RGB565(10, 30, 26),
               RGB565(220, 255, 240));
}

/* ------------------------------------------------------------------ */
/* Study 4: Moss Drowse - slow in/slow out, heavy lids, yawns          */
/* ------------------------------------------------------------------ */

static void draw_moss_drowse(uint16_t *fb,
                             const fable_motion_pose_t *pose)
{
    const uint16_t eye_c = RGB565(168, 190, 120);
    const uint16_t eye_dim = RGB565(120, 140, 84);
    const uint16_t cover = RGB565(16, 24, 12);
    const uint16_t lash = RGB565(70, 92, 44);

    fill_bg(fb, 12, 18, 9, 22, 32, 16);

    const int32_t ex = q2px(pose->eye_x_q2);
    const int32_t ey = q2px(pose->eye_y_q2);
    const int32_t hx = q2px(pose->head_x_q2);
    const int32_t hy = q2px(pose->head_y_q2);
    /* The whole face rises and falls with the slow breath. */
    const int32_t bob = ((int32_t)pose->breath - 100) / 32;

    const int32_t eye_rx = 20;
    const int32_t eye_ry = 11 + (int32_t)pose->stretch / 16;
    const int32_t eye_cy = 58 + ey + bob + hy / 2;

    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        const int32_t cx = 80 + sgn * 30 + ex;
        fill_ellipse(fb, cx, eye_cy, eye_rx, eye_ry, eye_dim);
        fill_ellipse(fb, cx, eye_cy, eye_rx - 3, eye_ry - 2, eye_c);
        const uint16_t lid_q10 =
            side == 0 ? pose->lid_left_q10 : pose->lid_right_q10;
        draw_lid(fb, cx - eye_rx - 1, eye_cy - eye_ry - 1,
                 2 * eye_rx + 3, 2 * eye_ry + 2,
                 lid_cover_px(lid_q10, 2 * eye_ry + 2),
                 -sgn * (int32_t)pose->lid_tilt, cover, lash);

        const int32_t brow =
            side == 0 ? pose->brow_left : pose->brow_right;
        const int32_t brow_y = eye_cy - eye_ry - 7 - brow / 10;
        draw_brow(fb, cx - 13, brow_y - sgn * (brow / 20), cx + 13,
                  brow_y + sgn * (brow / 20), 2, lash);
    }

    draw_mouth(fb, pose, 80 + hx, 92 + hy + bob, 28, 18,
               RGB565(96, 118, 60), RGB565(30, 40, 20),
               RGB565(200, 220, 160));
}

/* ------------------------------------------------------------------ */
/* Study 5: Sage Stager - staging via pose and lighting                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t rt, gt, bt;
    uint8_t rb, gb, bb;
} stage_light_t;

static const stage_light_t STAGE_LIGHTS[4] = {
    { 12, 12, 20, 20, 20, 34 },  /* idle: neutral dusk */
    { 10, 24, 26, 14, 42, 44 },  /* listening: teal lift */
    { 18, 12, 30, 30, 20, 52 },  /* thinking: violet */
    { 26, 16, 10, 46, 26, 14 },  /* speaking: warm spotlight */
};

static void draw_sage_stager(uint16_t *fb, const fable_keyframe_t *kf,
                             const fable_motion_pose_t *pose)
{
    const uint8_t activity = activity_of(kf);
    const stage_light_t *light = &STAGE_LIGHTS[activity & 3U];
    const uint16_t plate = RGB565(70, 66, 96);
    const uint16_t plate_rim = RGB565(44, 40, 66);
    const uint16_t eye_c = RGB565(214, 204, 255);
    const uint16_t lash = RGB565(30, 26, 48);

    fill_bg(fb, light->rt, light->gt, light->bt, light->rb, light->gb,
            light->bb);

    const int32_t ex = q2px(pose->eye_x_q2);
    const int32_t ey = q2px(pose->eye_y_q2);
    const int32_t hx = q2px(pose->head_x_q2);
    const int32_t hy = q2px(pose->head_y_q2);
    const int32_t stretch = (int32_t)pose->stretch;

    const int32_t plate_w = 116 - stretch / 4;
    const int32_t plate_h = 86 + stretch / 2;
    const int32_t plate_x = 80 + hx - plate_w / 2;
    const int32_t plate_y = 60 + hy - plate_h / 2;
    fill_rrect(fb, plate_x, plate_y, plate_w, plate_h, 18, plate_rim);
    fill_rrect(fb, plate_x + 3, plate_y + 3, plate_w - 6, plate_h - 6,
               15, plate);

    const int32_t eye_cy = plate_y + 30 + (ey - hy);

    for (int32_t side = 0; side < 2; side++) {
        const int32_t sgn = side == 0 ? -1 : 1;
        const int32_t cx = 80 + hx + sgn * 26 + (ex - hx);
        fill_ellipse(fb, cx, eye_cy, 14, 10, eye_c);
        fill_ellipse(fb, cx + (ex - hx), eye_cy + (ey - hy) / 2, 5, 5,
                     lash);
        const uint16_t lid_q10 =
            side == 0 ? pose->lid_left_q10 : pose->lid_right_q10;
        draw_lid(fb, cx - 15, eye_cy - 11, 31, 22,
                 lid_cover_px(lid_q10, 22),
                 -sgn * (int32_t)pose->lid_tilt, plate, lash);

        const int32_t brow =
            side == 0 ? pose->brow_left : pose->brow_right;
        const int32_t brow_y = eye_cy - 16 - brow / 6;
        draw_brow(fb, cx - 12, brow_y + sgn * (brow / 10), cx + 12,
                  brow_y - sgn * (brow / 10), 4, plate_rim);
    }

    draw_mouth(fb, pose, 80 + hx, plate_y + plate_h - 20, 44, 16,
               RGB565(40, 36, 60), RGB565(18, 16, 30),
               RGB565(226, 220, 250));
}

/* ------------------------------------------------------------------ */
/* Registry                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *slug;
    const char *name;
    const fable_persona_t *persona;
} study_info_t;

static const study_info_t STUDIES[FABLE_STUDY_COUNT] = {
    { "curious-scout", "Curious Scout", &FABLE_PERSONA_CURIOUS },
    { "ember-breath", "Ember Breath", &FABLE_PERSONA_CALM },
    { "pip-spark", "Pip Spark", &FABLE_PERSONA_PERKY },
    { "moss-drowse", "Moss Drowse", &FABLE_PERSONA_SLEEPY },
    { "sage-stager", "Sage Stager", &FABLE_PERSONA_SAGE },
};

const char *fable_study_slug(fable_study_t study)
{
    if ((uint32_t)study >= (uint32_t)FABLE_STUDY_COUNT) {
        return "unknown";
    }
    return STUDIES[study].slug;
}

const char *fable_study_name(fable_study_t study)
{
    if ((uint32_t)study >= (uint32_t)FABLE_STUDY_COUNT) {
        return "Unknown";
    }
    return STUDIES[study].name;
}

const fable_persona_t *fable_study_persona(fable_study_t study)
{
    if ((uint32_t)study >= (uint32_t)FABLE_STUDY_COUNT) {
        return &FABLE_PERSONA_CALM;
    }
    return STUDIES[study].persona;
}

bool fable_study_render(fable_study_t study,
                        const fable_keyframe_t *keyframe,
                        uint32_t sample_clock,
                        uint16_t *rgb565,
                        size_t pixel_capacity)
{
    if (rgb565 == NULL || pixel_capacity < (size_t)FABLE_STUDY_PIXELS ||
        (uint32_t)study >= (uint32_t)FABLE_STUDY_COUNT) {
        return false;
    }

    fable_motion_pose_t pose;
    fable_motion_eval(STUDIES[study].persona, keyframe, sample_clock,
                      &pose);

    switch (study) {
    case FABLE_STUDY_CURIOUS_SCOUT:
        draw_curious_scout(rgb565, &pose);
        break;
    case FABLE_STUDY_EMBER_BREATH:
        draw_ember_breath(rgb565, &pose);
        break;
    case FABLE_STUDY_PIP_SPARK:
        draw_pip_spark(rgb565, &pose);
        break;
    case FABLE_STUDY_MOSS_DROWSE:
        draw_moss_drowse(rgb565, &pose);
        break;
    case FABLE_STUDY_SAGE_STAGER:
    default:
        draw_sage_stager(rgb565, keyframe, &pose);
        break;
    }
    return true;
}
