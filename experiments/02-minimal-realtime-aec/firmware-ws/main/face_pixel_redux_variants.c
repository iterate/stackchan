#include "face_pixel_redux_variants.h"

#include <string.h>

#include "face_pose.h"
#include "face_stage.h"

#define PV_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    PV_SAFE = 2,
    PV_LOGICAL_WIDTH = FACE_PIXEL_REDUX_LOGICAL_WIDTH,
    PV_LOGICAL_HEIGHT = FACE_PIXEL_REDUX_LOGICAL_HEIGHT,
};

typedef struct {
    const char *slug;
    const char *name;
    face_pixel_redux_actor_t base_actor;
    uint8_t authored_version;
    uint8_t palette_size;
    uint8_t ops;
} pv_definition_t;

typedef struct {
    uint16_t *pixels;
    int offset_x;
    int offset_y;
} pv_canvas_t;

static const pv_definition_t PV_DEFINITIONS[FACE_PIXEL_VARIANT_COUNT] = {
    [FACE_PIXEL_VARIANT_EGA_SUNBLADE_RANGER] = {
        "pixel-variant-ega-sunblade-ranger",
        "Pixel Variant · EGA Sunblade Ranger",
        FACE_PIXEL_REDUX_EGA_QUEST, 1U, 16U, 6U,
    },
    [FACE_PIXEL_VARIANT_EGA_TAVERN_BARD] = {
        "pixel-variant-ega-tavern-bard",
        "Pixel Variant · EGA Tavern Bard",
        FACE_PIXEL_REDUX_EGA_QUEST, 2U, 16U, 7U,
    },
    [FACE_PIXEL_VARIANT_EGA_MOONKEEP_ROGUE] = {
        "pixel-variant-ega-moonkeep-rogue",
        "Pixel Variant · EGA Moonkeep Rogue",
        FACE_PIXEL_REDUX_EGA_QUEST, 3U, 16U, 7U,
    },
    [FACE_PIXEL_VARIANT_VGA_ASTRAL_ARCHIVIST] = {
        "pixel-variant-vga-astral-archivist",
        "Pixel Variant · VGA Astral Archivist",
        FACE_PIXEL_REDUX_VGA_ELDER, 1U, 32U, 9U,
    },
    [FACE_PIXEL_VARIANT_VGA_STORM_SEER] = {
        "pixel-variant-vga-storm-seer",
        "Pixel Variant · VGA Storm Seer",
        FACE_PIXEL_REDUX_VGA_ELDER, 2U, 28U, 8U,
    },
    [FACE_PIXEL_VARIANT_VGA_HEARTH_SAGE] = {
        "pixel-variant-vga-hearth-sage",
        "Pixel Variant · VGA Hearth Sage",
        FACE_PIXEL_REDUX_VGA_ELDER, 3U, 30U, 9U,
    },
    [FACE_PIXEL_VARIANT_TALKIE_DOCKYARD_PILOT] = {
        "pixel-variant-talkie-dockyard-pilot",
        "Pixel Variant · Talkie Dockyard Pilot",
        FACE_PIXEL_REDUX_TALKIE_CLOSEUP, 1U, 44U, 10U,
    },
    [FACE_PIXEL_VARIANT_TALKIE_NEON_ENGINEER] = {
        "pixel-variant-talkie-neon-engineer",
        "Pixel Variant · Talkie Neon Engineer",
        FACE_PIXEL_REDUX_TALKIE_CLOSEUP, 2U, 46U, 10U,
    },
    [FACE_PIXEL_VARIANT_TALKIE_INTERCOM_CAPTAIN] = {
        "pixel-variant-talkie-intercom-captain",
        "Pixel Variant · Talkie Intercom Captain",
        FACE_PIXEL_REDUX_TALKIE_CLOSEUP, 3U, 40U, 9U,
    },
    [FACE_PIXEL_VARIANT_ARCADE_CRT_CONCIERGE] = {
        "pixel-variant-arcade-crt-concierge",
        "Pixel Variant · Arcade CRT Concierge",
        FACE_PIXEL_REDUX_PIXEL_AUTOMATON, 1U, 14U, 7U,
    },
    [FACE_PIXEL_VARIANT_ARCADE_SENTINEL] = {
        "pixel-variant-arcade-sentinel",
        "Pixel Variant · Arcade Sentinel",
        FACE_PIXEL_REDUX_PIXEL_AUTOMATON, 2U, 14U, 7U,
    },
    [FACE_PIXEL_VARIANT_ARCADE_PINBALL_BELLHOP] = {
        "pixel-variant-arcade-pinball-bellhop",
        "Pixel Variant · Arcade Pinball Bellhop",
        FACE_PIXEL_REDUX_PIXEL_AUTOMATON, 3U, 15U, 8U,
    },
    [FACE_PIXEL_VARIANT_POCKET_ACORN_SCOUT] = {
        "pixel-variant-pocket-acorn-scout",
        "Pixel Variant · Pocket Acorn Scout",
        FACE_PIXEL_REDUX_POCKET_RPG, 1U, 4U, 7U,
    },
    [FACE_PIXEL_VARIANT_POCKET_BOG_SPRITE] = {
        "pixel-variant-pocket-bog-sprite",
        "Pixel Variant · Pocket Bog Sprite",
        FACE_PIXEL_REDUX_POCKET_RPG, 2U, 4U, 7U,
    },
    [FACE_PIXEL_VARIANT_POCKET_MOONCAP_FAMILIAR] = {
        "pixel-variant-pocket-mooncap-familiar",
        "Pixel Variant · Pocket Mooncap Familiar",
        FACE_PIXEL_REDUX_POCKET_RPG, 3U, 4U, 8U,
    },
    [FACE_PIXEL_VARIANT_DMG_TIN_WARDEN] = {
        "pixel-variant-dmg-tin-warden",
        "Pixel Variant · DMG Tin Warden",
        FACE_PIXEL_REDUX_POCKET_RPG, 1U, 4U, 7U,
    },
    [FACE_PIXEL_VARIANT_DMG_LANTERN_MOTH] = {
        "pixel-variant-dmg-lantern-moth",
        "Pixel Variant · DMG Lantern Moth",
        FACE_PIXEL_REDUX_POCKET_RPG, 2U, 4U, 8U,
    },
    [FACE_PIXEL_VARIANT_DMG_SLIME_COURIER] = {
        "pixel-variant-dmg-slime-courier",
        "Pixel Variant · DMG Slime Courier",
        FACE_PIXEL_REDUX_POCKET_RPG, 3U, 4U, 7U,
    },
};

static int pv_clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int pv_abs(int value)
{
    return value < 0 ? -value : value;
}

static bool pv_valid(face_pixel_redux_variant_t variant)
{
    return (unsigned)variant < (unsigned)FACE_PIXEL_VARIANT_COUNT;
}

static void pv_put(
    pv_canvas_t *canvas, int x, int y, uint16_t color)
{
    x += canvas->offset_x;
    y += canvas->offset_y;
    if (x < PV_SAFE || x >= PV_LOGICAL_WIDTH - PV_SAFE ||
        y < PV_SAFE || y >= PV_LOGICAL_HEIGHT - PV_SAFE) {
        return;
    }
    const size_t physical_x =
        (size_t)x * FACE_PIXEL_REDUX_SCALE;
    const size_t physical_y =
        (size_t)y * FACE_PIXEL_REDUX_SCALE;
    const size_t top =
        physical_y * FACE_PIXEL_REDUX_WIDTH + physical_x;
    const size_t bottom = top + FACE_PIXEL_REDUX_WIDTH;
    canvas->pixels[top] = color;
    canvas->pixels[top + 1U] = color;
    canvas->pixels[bottom] = color;
    canvas->pixels[bottom + 1U] = color;
}

static void pv_clear(pv_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_PIXEL_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void pv_rect(
    pv_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left = pv_clamp(x, PV_SAFE, PV_LOGICAL_WIDTH - PV_SAFE);
    const int right =
        pv_clamp(x + width, PV_SAFE, PV_LOGICAL_WIDTH - PV_SAFE);
    const int top = pv_clamp(y, PV_SAFE, PV_LOGICAL_HEIGHT - PV_SAFE);
    const int bottom =
        pv_clamp(y + height, PV_SAFE, PV_LOGICAL_HEIGHT - PV_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            pv_put(canvas, xx, yy, color);
        }
    }
}

static void pv_frame(
    pv_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int thickness,
    uint16_t color)
{
    pv_rect(canvas, x, y, width, thickness, color);
    pv_rect(
        canvas, x, y + height - thickness,
        width, thickness, color);
    pv_rect(canvas, x, y, thickness, height, color);
    pv_rect(
        canvas, x + width - thickness, y,
        thickness, height, color);
}

static void pv_line(
    pv_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = pv_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -pv_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pv_put(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void pv_thick_line(
    pv_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        pv_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void pv_ellipse(
    pv_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    const int64_t limit = (int64_t)rx * rx * ry * ry;
    for (int y = -ry; y <= ry; ++y) {
        const int64_t vertical = (int64_t)y * y * rx * rx;
        int span = rx;
        while (span > 0 &&
               (int64_t)span * span * ry * ry + vertical > limit) {
            --span;
        }
        pv_rect(canvas, cx - span, cy + y, span * 2 + 1, 1, color);
    }
}

static int32_t pv_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void pv_triangle(
    pv_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    int left = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int right = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int top = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int bottom = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    left = pv_clamp(left, PV_SAFE, PV_LOGICAL_WIDTH - PV_SAFE - 1);
    right = pv_clamp(right, PV_SAFE, PV_LOGICAL_WIDTH - PV_SAFE - 1);
    top = pv_clamp(top, PV_SAFE, PV_LOGICAL_HEIGHT - PV_SAFE - 1);
    bottom = pv_clamp(bottom, PV_SAFE, PV_LOGICAL_HEIGHT - PV_SAFE - 1);
    const int32_t orientation = pv_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = pv_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = pv_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = pv_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 &&
                 e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 &&
                 e1 <= 0 && e2 <= 0)) {
                pv_put(canvas, x, y, color);
            }
        }
    }
}

static void pv_quad(
    pv_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    int dx,
    int dy,
    uint16_t color)
{
    pv_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    pv_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void pv_checker(
    pv_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t first,
    uint16_t second,
    unsigned period)
{
    for (int yy = y; yy < y + height; ++yy) {
        for (int xx = x; xx < x + width; ++xx) {
            const unsigned cell =
                ((unsigned)(xx - x) + (unsigned)(yy - y)) % period;
            pv_put(canvas, xx, yy, cell == 0U ? second : first);
        }
    }
}

static void pv_tune_pose(
    face_pixel_redux_variant_t variant,
    face_pixel_redux_pose_t *pose)
{
    const unsigned family = (unsigned)variant / 3U;
    const unsigned version = (unsigned)variant % 3U;
    switch (family) {
    case 0U:
        pose->eye_x[0] = version == 2U ? 30 : 31;
        pose->eye_x[1] = version == 2U ? 50 : 49;
        pose->eye_y[0] += version == 1U ? 1 : 0;
        pose->eye_y[1] += version == 1U ? 1 : 0;
        pose->eye_w[0] = (int16_t)pv_clamp(
            pose->eye_w[0] + (version == 1U ? 1 : 0), 8, 18);
        pose->eye_w[1] = (int16_t)pv_clamp(
            pose->eye_w[1] + (version == 1U ? 1 : 0), 8, 18);
        pose->mouth_y = 42;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 1U ? 4 : 1), 9, 30);
        pose->mouth_h = (int16_t)pv_clamp(
            pose->mouth_h + (version == 1U ? 2 : 0), 1, 14);
        break;
    case 1U:
        pose->eye_x[0] = version == 0U ? 28 : 29;
        pose->eye_x[1] = version == 0U ? 52 : 51;
        pose->eye_w[0] = (int16_t)pv_clamp(
            pose->eye_w[0] + (version == 2U ? 1 : 0), 9, 19);
        pose->eye_w[1] = (int16_t)pv_clamp(
            pose->eye_w[1] + (version == 2U ? 1 : 0), 9, 19);
        pose->mouth_y = version == 1U ? 42 : 41;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 2U ? 3 : 0), 10, 31);
        break;
    case 2U:
        pose->eye_x[0] = version == 0U ? 27 : 28;
        pose->eye_x[1] = version == 0U ? 53 : 52;
        pose->eye_y[0] = version == 2U ? 24 : 23;
        pose->eye_y[1] = version == 2U ? 24 : 23;
        pose->mouth_y = version == 2U ? 44 : 43;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 2U ? 2 : 0), 11, 33);
        pose->mouth_h = (int16_t)pv_clamp(
            pose->mouth_h + (version == 0U ? 1 : 0), 1, 15);
        break;
    case 3U:
        pose->eye_x[0] = version == 2U ? 29 : 28;
        pose->eye_x[1] = version == 2U ? 51 : 52;
        pose->eye_y[0] = version == 2U ? 24 : 25;
        pose->eye_y[1] = version == 2U ? 24 : 25;
        pose->mouth_y = version == 2U ? 42 : 43;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 0U ? 3 : 0), 10, 32);
        break;
    case 4U:
        pose->eye_x[0] = version == 1U ? 29 : 31;
        pose->eye_x[1] = version == 1U ? 51 : 49;
        pose->eye_y[0] = version == 1U ? 27 : 28;
        pose->eye_y[1] = version == 1U ? 27 : 28;
        pose->eye_w[0] = (int16_t)pv_clamp(
            pose->eye_w[0] + (version == 1U ? 3 : 1), 9, 16);
        pose->eye_w[1] = (int16_t)pv_clamp(
            pose->eye_w[1] + (version == 1U ? 3 : 1), 9, 16);
        pose->mouth_y = 41;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 1U ? 3 : 1), 8, 27);
        pose->mouth_h = (int16_t)pv_clamp(
            pose->mouth_h + (version == 1U ? 1 : 0), 1, 12);
        break;
    default:
        pose->eye_x[0] = version == 0U ? 29 : 28;
        pose->eye_x[1] = version == 0U ? 51 : 52;
        pose->eye_y[0] = version == 2U ? 28 : 27;
        pose->eye_y[1] = version == 2U ? 28 : 27;
        pose->eye_w[0] = (int16_t)pv_clamp(
            pose->eye_w[0] + (version == 0U ? 0 : 3), 10, 17);
        pose->eye_w[1] = (int16_t)pv_clamp(
            pose->eye_w[1] + (version == 0U ? 0 : 3), 10, 17);
        pose->mouth_y = version == 1U ? 42 : 41;
        pose->mouth_w = (int16_t)pv_clamp(
            pose->mouth_w + (version == 2U ? 4 : 1), 8, 29);
        pose->mouth_h = (int16_t)pv_clamp(
            pose->mouth_h + (version == 2U ? 1 : 0), 1, 13);
        break;
    }
}

static void pv_draw_brows(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    int half_width,
    int thickness,
    uint16_t color)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int outer_x = pose->eye_x[eye] +
            (eye == 0U ? -half_width : half_width);
        const int inner_x = pose->eye_x[eye] +
            (eye == 0U ? half_width : -half_width);
        pv_thick_line(
            canvas,
            outer_x,
            pose->brow_outer_y[eye],
            inner_x,
            pose->brow_inner_y[eye],
            thickness,
            color);
    }
}

static void pv_draw_eye(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    unsigned shape,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pv_clamp(pose->eye_w[eye], 7, 20);
    const int aperture = pv_clamp(pose->eye_open[eye], 1, 11);
    const int half_w = width / 2;
    const int half_h = pv_clamp(aperture / 2, 1, 5);
    if (aperture <= 1) {
        pv_thick_line(
            canvas, cx - half_w, cy, cx + half_w, cy,
            shape == 2U ? 2 : 1, outline);
        return;
    }
    if (shape == 0U) {
        pv_rect(
            canvas, cx - half_w - 1, cy - half_h - 1,
            width + 2, aperture + 2, outline);
        pv_rect(
            canvas, cx - half_w, cy - half_h,
            width, aperture, sclera);
    } else if (shape == 1U) {
        pv_ellipse(canvas, cx, cy, half_w + 1, half_h + 1, outline);
        pv_ellipse(canvas, cx, cy, half_w, half_h, sclera);
    } else {
        pv_quad(
            canvas,
            cx - half_w - 1, cy,
            cx - half_w / 2, cy - half_h - 1,
            cx + half_w / 2, cy - half_h - 1,
            cx + half_w + 1, cy,
            outline);
        pv_quad(
            canvas,
            cx - half_w - 1, cy,
            cx + half_w + 1, cy,
            cx + half_w / 2, cy + half_h + 1,
            cx - half_w / 2, cy + half_h + 1,
            outline);
        pv_ellipse(
            canvas, cx, cy, pv_clamp(half_w - 1, 2, 8),
            pv_clamp(half_h - 1, 1, 4), sclera);
    }
    const int radius = pv_clamp(pose->pupil_radius[eye], 1, 3);
    const int px = pv_clamp(
        pose->pupil_x[eye],
        cx - half_w + radius,
        cx + half_w - radius);
    const int py = pv_clamp(
        pose->pupil_y[eye],
        cy - half_h + radius - 1,
        cy + half_h - radius + 1);
    if (shape == 2U) {
        pv_ellipse(canvas, px, py, radius + 1, radius, iris);
        pv_rect(canvas, px, py - radius, 1, radius * 2 + 1, pupil);
    } else {
        pv_ellipse(canvas, px, py, radius + 1, radius + 1, iris);
        pv_rect(
            canvas, px - radius / 2, py - radius,
            radius + 1, radius * 2 + 1, pupil);
    }
    if (pose->attention > 72U && aperture >= 4) {
        pv_rect(canvas, px - 1, py - 1, 2, 1, glint);
    }
}

static void pv_draw_mouth(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned style,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int half = pv_clamp(
        pose->mouth_w / 2 + (style == 1U ? 1 : 0), 4, 17);
    const int left = pose->mouth_x - half;
    const int right = pose->mouth_x + half;
    const int left_y =
        pose->mouth_y + pose->mouth_corner_y[0] / 2;
    const int right_y =
        pose->mouth_y + pose->mouth_corner_y[1] / 2;
    const int height = pv_clamp(
        pose->mouth_h + (style == 1U ? 1 : 0), 1, 15);
    if (height <= 2 || pose->mouth_press > 205U) {
        const int center_y = pose->mouth_y -
            (pose->mouth_corner_y[0] +
             pose->mouth_corner_y[1]) / 4;
        pv_thick_line(
            canvas, left, left_y, pose->mouth_x, center_y,
            2, lip);
        pv_thick_line(
            canvas, pose->mouth_x, center_y, right, right_y,
            2, lip);
        if (pose->mouth_press > 225U) {
            pv_rect(canvas, pose->mouth_x - 2, center_y - 1, 4, 2, lip);
        }
        return;
    }
    const int top = pose->mouth_y - height / 2;
    const int bottom = pose->mouth_y + height / 2;
    int inner_half = half - 2 -
        (int)pose->mouth_round * half / 620;
    inner_half = pv_clamp(inner_half, 2, 15);
    if (style == 2U) {
        pv_quad(
            canvas, left, left_y,
            pose->mouth_x - inner_half, top,
            pose->mouth_x + inner_half, top,
            right, right_y, lip);
        pv_quad(
            canvas, left, left_y,
            right, right_y,
            pose->mouth_x + inner_half, bottom,
            pose->mouth_x - inner_half, bottom, lip);
        pv_quad(
            canvas,
            pose->mouth_x - inner_half + 1, top + 1,
            pose->mouth_x + inner_half - 1, top + 1,
            pose->mouth_x + inner_half - 1, bottom - 1,
            pose->mouth_x - inner_half + 1, bottom - 1,
            cavity);
    } else {
        pv_ellipse(
            canvas, pose->mouth_x, pose->mouth_y,
            inner_half + 2, pv_clamp(height / 2 + 1, 2, 8), lip);
        pv_ellipse(
            canvas, pose->mouth_x, pose->mouth_y,
            inner_half, pv_clamp(height / 2 - 1, 1, 6), cavity);
        pv_thick_line(
            canvas, left, left_y,
            pose->mouth_x - inner_half, pose->mouth_y,
            2, lip);
        pv_thick_line(
            canvas, pose->mouth_x + inner_half, pose->mouth_y,
            right, right_y, 2, lip);
    }
    if (pose->teeth > 82U && height >= 4) {
        const int tooth_width =
            pv_clamp(2 + pose->teeth / 28, 3, inner_half * 2);
        pv_rect(
            canvas, pose->mouth_x - tooth_width / 2,
            top + 1, tooth_width, pose->teeth > 190U ? 2 : 1,
            teeth);
    }
    if (pose->tongue > 58U && height >= 5) {
        const int tongue_width =
            pv_clamp(2 + pose->tongue / 30, 3, inner_half * 2);
        pv_ellipse(
            canvas, pose->mouth_x, bottom - 1,
            tongue_width / 2, pose->tongue > 180U ? 2 : 1,
            tongue);
    }
    if (pose->phoneme_shape != 0U) {
        pv_put(
            canvas,
            pose->mouth_x - 3 +
                (int)(pose->phoneme_shape % 7U),
            top + 1,
            tongue);
    }
}

static void pv_draw_led_mouth(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned style,
    uint16_t frame,
    uint16_t off,
    uint16_t on,
    uint16_t hot)
{
    const int columns = style == 2U ? 9 : 11;
    const int rows = style == 0U ? 5 : 4;
    const int spacing_x = style == 2U ? 4 : 4;
    const int width = columns * spacing_x;
    const int start_x = 40 - width / 2;
    const int start_y = pose->mouth_y - rows;
    if (style == 0U) {
        pv_ellipse(canvas, 40, pose->mouth_y, 25, 9, frame);
        pv_rect(canvas, 18, start_y - 1, 44, rows * 2 + 2, off);
    } else if (style == 1U) {
        pv_quad(
            canvas, 15, start_y + 1, 21, start_y - 4,
            59, start_y - 4, 65, start_y + 1, frame);
        pv_rect(canvas, 17, start_y, 46, rows * 2 + 3, off);
    } else {
        pv_ellipse(canvas, 40, pose->mouth_y, 22, 8, frame);
        pv_ellipse(canvas, 40, pose->mouth_y, 19, 6, off);
    }
    const int active_rows = pv_clamp(
        1 + pose->mouth_h * rows / 13 -
            (pose->mouth_press > 190U ? 1 : 0),
        1,
        rows);
    const int active_half = pv_clamp(
        2 + pose->mouth_w * columns / 58,
        2,
        columns / 2);
    const int row_start = (rows - active_rows) / 2;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int dx = pv_abs(column - columns / 2);
            const int dy = pv_abs(row - rows / 2);
            const bool round_cut =
                pose->mouth_round > 145U &&
                dx + dy > active_half + 1;
            const bool active =
                row >= row_start &&
                row < row_start + active_rows &&
                dx <= active_half &&
                !round_cut;
            uint16_t color = active ? on : off;
            if (active &&
                ((row == row_start && pose->teeth > 110U) ||
                 (row == row_start + active_rows - 1 &&
                  pose->tongue > 90U) ||
                 column == columns / 2 +
                     (int)(pose->phoneme_shape % 3U) - 1)) {
                color = hot;
            }
            pv_rect(
                canvas,
                start_x + column * spacing_x,
                start_y + row * 2,
                style == 2U ? 3 : 2,
                1,
                color);
        }
    }
    /*
     * Stable articulation rails preserve narrow consonant differences that
     * would otherwise collapse in a four- or five-row display.  They are
     * mechanical parts of the mouth, not diagnostic text: upper travel keys
     * teeth, lower travel keys tongue, and the side latch keys lip rounding.
     */
    const int rail_width = width - 5;
    const int teeth_x =
        start_x + 2 + (int)pose->teeth * rail_width / 255;
    const int tongue_x =
        start_x + 2 + (int)pose->tongue * rail_width / 255;
    const int round_y =
        start_y + (int)pose->mouth_round * (rows * 2 - 1) / 255;
    pv_rect(canvas, teeth_x, start_y - 2, 2, 1, hot);
    pv_rect(canvas, tongue_x, start_y + rows * 2 + 1, 2, 1, hot);
    pv_rect(canvas, start_x - 2, round_y, 2, 2, on);
    pv_rect(canvas, start_x + width, round_y, 2, 2, on);
    const int press_x =
        start_x + 2 + (int)pose->mouth_press * rail_width / 255;
    pv_rect(canvas, press_x, start_y + rows, 2, 1, hot);
}

static int pv_expression_lift(
    const face_pixel_redux_pose_t *pose)
{
    switch (pose->stage_expression) {
    case FACE_EXPRESSION_JOY:
    case FACE_EXPRESSION_EXCITED:
    case FACE_EXPRESSION_SURPRISE:
        return -3;
    case FACE_EXPRESSION_CONCERN:
    case FACE_EXPRESSION_SLEEPY:
        return 3;
    case FACE_EXPRESSION_SKEPTICAL:
        return 1;
    default:
        return 0;
    }
}

static void pv_draw_wayfarer(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned version)
{
    const uint16_t ink = PV_RGB565(25, 18, 34);
    const uint16_t night = PV_RGB565(15, 31, 91);
    const uint16_t blue = PV_RGB565(28, 82, 157);
    const uint16_t sky = PV_RGB565(56, 177, 206);
    const uint16_t green = PV_RGB565(22, 128, 66);
    const uint16_t mint = PV_RGB565(84, 209, 112);
    const uint16_t gold = PV_RGB565(252, 208, 73);
    const uint16_t red = PV_RGB565(170, 39, 55);
    const uint16_t plum = PV_RGB565(104, 44, 91);
    const uint16_t skin = PV_RGB565(242, 157, 91);
    const uint16_t skin_light = PV_RGB565(255, 207, 132);
    const uint16_t white = PV_RGB565(255, 248, 222);
    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    const int lift = pv_expression_lift(pose);

    if (version == 0U) {
        pv_clear(canvas, night);
        pv_checker(canvas, 2, 2, 76, 56, night, blue, 9U);
        pv_rect(canvas, 3, 48, 74, 10, blue);
        pv_triangle(
            canvas, 7 + lean_x, 58, 23 + lean_x,
            43 + lean_y, 44 + lean_x, 49 + lean_y, green);
        pv_triangle(
            canvas, 32 + lean_x, 49 + lean_y,
            61 + lean_x, 42 + lean_y, 75 + lean_x, 58, mint);
        pv_triangle(canvas, 60, 48, 72, 52, 67, 56, gold);
        pv_ellipse(canvas, 40, 29, 21, 23, red);
        pv_ellipse(canvas, 40, 30, 18, 20, skin);
        pv_triangle(canvas, 21, 18, 34, 6, 60, 14, green);
        pv_rect(canvas, 18, 15, 44, 5, green);
        pv_checker(canvas, 24, 11, 30, 4, green, mint, 3U);
        const int feather = pose->head_roll + lift;
        pv_thick_line(canvas, 54, 12, 65 + feather, 4, 2, gold);
        pv_triangle(
            canvas, 63 + feather, 3,
            71 + feather, 4,
            65 + feather, 8,
            white);
        pv_rect(canvas, 62, 41, 8, 3, gold);
        pv_rect(canvas, 66, 38, 3, 8, gold);
        pv_draw_eye(
            canvas, pose, 0U, 2U, red, white, sky, ink, white);
        pv_draw_eye(
            canvas, pose, 1U, 2U, red, white, sky, ink, white);
        pv_draw_brows(canvas, pose, 6, 2, ink);
        pv_line(
            canvas, 40 + pose->head_yaw, 29,
            38 + pose->head_yaw, 36, red);
        pv_draw_mouth(canvas, pose, 2U, red, ink, white, skin_light);
        return;
    }

    if (version == 1U) {
        pv_clear(canvas, PV_RGB565(57, 23, 67));
        pv_checker(
            canvas, 2, 2, 76, 56,
            PV_RGB565(57, 23, 67),
            PV_RGB565(93, 38, 83), 7U);
        pv_rect(canvas, 3, 51, 74, 7, PV_RGB565(77, 41, 31));
        pv_triangle(
            canvas, 8 + lean_x, 58, 25 + lean_x,
            44 + lean_y, 44 + lean_x, 50 + lean_y, plum);
        pv_triangle(
            canvas, 38 + lean_x, 50 + lean_y,
            61 + lean_x, 43 + lean_y, 75 + lean_x, 58, red);
        pv_line(canvas, 59, 38, 72, 54, gold);
        pv_ellipse(canvas, 69, 51, 5, 6, PV_RGB565(126, 70, 32));
        pv_put(canvas, 70, 48, gold);
        pv_ellipse(canvas, 40, 30, 22, 22, PV_RGB565(103, 52, 27));
        pv_ellipse(canvas, 40, 30, 18, 19, skin_light);
        pv_ellipse(canvas, 24, 27, 4, 6, skin);
        pv_ellipse(canvas, 56, 27, 4, 6, skin);
        pv_triangle(canvas, 16, 16, 34, 5, 63, 14, plum);
        pv_rect(canvas, 17, 14, 46, 5, red);
        pv_ellipse(canvas, 27, 15, 12, 5, plum);
        pv_thick_line(
            canvas, 53, 12, 64 + pose->head_roll, 5 + lift,
            2, gold);
        pv_thick_line(
            canvas, 49, 11, 57 + pose->head_roll, 3 + lift,
            2, white);
        pv_draw_eye(
            canvas, pose, 0U, 1U, plum, white, sky, ink, white);
        pv_draw_eye(
            canvas, pose, 1U, 1U, plum, white, sky, ink, white);
        pv_draw_brows(canvas, pose, 6, 2, PV_RGB565(103, 52, 27));
        pv_triangle(canvas, 38, 30, 42, 30, 40, 36, red);
        pv_draw_mouth(canvas, pose, 1U, red, ink, white, skin);
        if (pose->cheek > 70U) {
            pv_rect(canvas, 24, 35, 7, 3, red);
            pv_rect(canvas, 49, 35, 7, 3, red);
        }
        return;
    }

    pv_clear(canvas, PV_RGB565(8, 17, 35));
    pv_checker(
        canvas, 2, 2, 76, 56,
        PV_RGB565(8, 17, 35),
        PV_RGB565(18, 35, 60), 11U);
    pv_triangle(
        canvas, 4 + lean_x, 58, 24 + lean_x,
        42 + lean_y, 43 + lean_x, 49 + lean_y,
        PV_RGB565(42, 49, 87));
    pv_triangle(
        canvas, 38 + lean_x, 49 + lean_y,
        58 + lean_x, 42 + lean_y, 76 + lean_x, 58,
        PV_RGB565(28, 39, 72));
    pv_triangle(canvas, 12, 26, 32, 5, 40, 19, ink);
    pv_triangle(canvas, 68, 26, 48, 5, 40, 19, ink);
    pv_ellipse(canvas, 40, 30, 23, 23, ink);
    pv_ellipse(canvas, 40, 31, 17, 18, skin);
    pv_quad(
        canvas, 20, 20, 39, 8, 60, 20, 55, 24,
        PV_RGB565(51, 55, 94));
    pv_rect(canvas, 20, 18, 40, 5, PV_RGB565(72, 65, 106));
    pv_triangle(
        canvas, 19, 36, 27, 42, 17, 53,
        PV_RGB565(85, 31, 63));
    pv_triangle(
        canvas, 61, 36, 53, 42, 66, 51 + lift,
        PV_RGB565(119, 37, 68));
    pv_draw_eye(
        canvas, pose, 0U, 2U, ink, white, sky, ink, white);
    pv_draw_eye(
        canvas, pose, 1U, 2U, ink, white, sky, ink, white);
    pv_draw_brows(canvas, pose, 6, 2, PV_RGB565(70, 33, 36));
    pv_line(canvas, 40, 30, 38, 36, red);
    pv_draw_mouth(canvas, pose, 2U, red, ink, white, skin_light);
    pv_thick_line(
        canvas, 57 + lean_x, 48 + lean_y,
        70 + lean_x,
        45 + lean_y + lift + pose->speech_pulse,
        2, plum);
}

static void pv_draw_oracle(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned version)
{
    const uint16_t ink = PV_RGB565(23, 20, 29);
    const uint16_t night = PV_RGB565(30, 27, 57);
    const uint16_t violet = PV_RGB565(78, 56, 101);
    const uint16_t blue = PV_RGB565(43, 87, 116);
    const uint16_t cyan = PV_RGB565(90, 190, 191);
    const uint16_t silver = PV_RGB565(166, 171, 171);
    const uint16_t pale = PV_RGB565(232, 224, 198);
    const uint16_t skin = PV_RGB565(188, 132, 96);
    const uint16_t skin_light = PV_RGB565(231, 177, 125);
    const uint16_t lip = PV_RGB565(116, 52, 58);
    const uint16_t tongue = PV_RGB565(206, 94, 101);
    const uint16_t gold = PV_RGB565(235, 188, 76);
    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    const int lift = pv_expression_lift(pose);

    if (version == 0U) {
        pv_clear(canvas, night);
        pv_checker(canvas, 2, 2, 76, 56, night, violet, 10U);
        pv_ellipse(canvas, 40, 28, 31, 26, violet);
        for (int ray = 0; ray < 8; ++ray) {
            const int x = 8 + ray * 9;
            pv_rect(
                canvas, x, 5 + (ray & 1), 2, 3,
                ray == (int)(pose->attention / 32U) ? gold : cyan);
        }
        pv_triangle(
            canvas, 6 + lean_x, 58, 25 + lean_x,
            43 + lean_y, 40 + lean_x, 51 + lean_y, blue);
        pv_triangle(
            canvas, 40 + lean_x, 51 + lean_y,
            55 + lean_x, 43 + lean_y, 74 + lean_x, 58, violet);
        pv_ellipse(canvas, 40, 28, 23, 24, silver);
        pv_ellipse(canvas, 40, 29, 19, 20, skin);
        pv_rect(canvas, 22, 8, 36, 9, pale);
        pv_triangle(canvas, 20, 13, 35, 4, 61, 13, silver);
        pv_checker(canvas, 27, 8, 26, 5, silver, pale, 3U);
        pv_draw_eye(
            canvas, pose, 0U, 1U, gold, pale, cyan, ink, pale);
        pv_draw_eye(
            canvas, pose, 1U, 1U, gold, pale, cyan, ink, pale);
        pv_ellipse(
            canvas, pose->eye_x[0], pose->eye_y[0], 8, 6, gold);
        pv_ellipse(
            canvas, pose->eye_x[0], pose->eye_y[0], 6, 4, pale);
        pv_ellipse(
            canvas, pose->eye_x[1], pose->eye_y[1], 8, 6, gold);
        pv_ellipse(
            canvas, pose->eye_x[1], pose->eye_y[1], 6, 4, pale);
        pv_draw_eye(
            canvas, pose, 0U, 1U, gold, pale, cyan, ink, pale);
        pv_draw_eye(
            canvas, pose, 1U, 1U, gold, pale, cyan, ink, pale);
        pv_line(
            canvas, pose->eye_x[0] + 8, pose->eye_y[0],
            pose->eye_x[1] - 8, pose->eye_y[1], gold);
        pv_draw_brows(canvas, pose, 7, 2, silver);
        pv_triangle(canvas, 38, 28, 42, 28, 40, 36, skin_light);
        pv_triangle(canvas, 20, 36, 30, 56, 37, 45, silver);
        pv_triangle(canvas, 60, 36, 50, 56, 43, 45, silver);
        pv_triangle(canvas, 31, 47, 39, 58, 39, 45, pale);
        pv_triangle(canvas, 49, 47, 41, 58, 41, 45, pale);
        pv_draw_mouth(canvas, pose, 0U, lip, ink, pale, tongue);
        return;
    }

    if (version == 1U) {
        const uint16_t storm = PV_RGB565(15, 30, 53);
        const uint16_t electric = PV_RGB565(124, 225, 239);
        pv_clear(canvas, storm);
        pv_checker(canvas, 2, 2, 76, 56, storm, blue, 13U);
        pv_line(canvas, 6, 11, 15, 16, electric);
        pv_line(canvas, 15, 16, 10, 24, electric);
        pv_line(canvas, 69, 7, 61, 15, gold);
        pv_line(canvas, 61, 15, 67, 21, gold);
        pv_triangle(
            canvas, 4 + lean_x, 58, 22 + lean_x,
            42 + lean_y, 41 + lean_x, 50 + lean_y, blue);
        pv_triangle(
            canvas, 39 + lean_x, 50 + lean_y,
            60 + lean_x, 41 + lean_y, 76 + lean_x, 58, violet);
        pv_triangle(canvas, 13, 25, 24, 4, 35, 15, silver);
        pv_triangle(canvas, 26, 15, 39, 3, 46, 15, pale);
        pv_triangle(canvas, 42, 14, 58, 4, 66, 25, silver);
        pv_ellipse(canvas, 40, 29, 23, 23, silver);
        pv_quad(canvas, 20, 19, 40, 8, 40, 51, 19, 42, skin_light);
        pv_quad(canvas, 40, 8, 62, 19, 61, 42, 40, 51, skin);
        pv_draw_eye(
            canvas, pose, 0U, 2U, ink, pale, electric, ink, pale);
        pv_draw_eye(
            canvas, pose, 1U, 2U, ink, pale, electric, ink, pale);
        pv_draw_brows(canvas, pose, 8, 3, ink);
        pv_triangle(canvas, 38, 27, 42, 27, 40, 37, skin_light);
        pv_triangle(canvas, 18, 36, 31, 57, 36, 43, silver);
        pv_triangle(canvas, 62, 36, 49, 57, 44, 43, pale);
        pv_triangle(canvas, 30, 45, 37, 57, 40, 44, silver);
        pv_triangle(canvas, 50, 45, 43, 57, 40, 44, pale);
        pv_draw_mouth(canvas, pose, 2U, lip, ink, pale, tongue);
        pv_thick_line(
            canvas, 64, 33, 72, 27 + lift + pose->speech_pulse,
            2, electric);
        return;
    }

    const uint16_t hearth = PV_RGB565(68, 31, 28);
    const uint16_t amber = PV_RGB565(217, 118, 58);
    const uint16_t cream = PV_RGB565(247, 222, 164);
    pv_clear(canvas, hearth);
    pv_checker(canvas, 2, 2, 76, 56, hearth, amber, 9U);
    pv_rect(canvas, 3, 47, 74, 11, PV_RGB565(93, 48, 35));
    pv_triangle(
        canvas, 5 + lean_x, 58, 23 + lean_x,
        42 + lean_y, 41 + lean_x, 50 + lean_y,
        PV_RGB565(47, 92, 85));
    pv_triangle(
        canvas, 39 + lean_x, 50 + lean_y,
        57 + lean_x, 42 + lean_y, 75 + lean_x, 58,
        PV_RGB565(60, 112, 94));
    pv_ellipse(canvas, 40, 29, 25, 24, PV_RGB565(107, 67, 47));
    pv_ellipse(canvas, 40, 29, 21, 21, skin_light);
    pv_rect(canvas, 20, 9, 40, 9, silver);
    pv_ellipse(canvas, 40, 12, 21, 8, pale);
    pv_checker(canvas, 24, 8, 32, 5, silver, cream, 4U);
    pv_draw_eye(
        canvas, pose, 0U, 1U, ink, cream, blue, ink, cream);
    pv_draw_eye(
        canvas, pose, 1U, 1U, ink, cream, blue, ink, cream);
    pv_draw_brows(canvas, pose, 7, 3, PV_RGB565(93, 55, 39));
    pv_triangle(canvas, 38, 28, 42, 28, 40, 36, skin);
    const int moustache = pv_clamp(
        (pose->mouth_corner_y[0] +
         pose->mouth_corner_y[1]) / 3, -2, 2);
    pv_triangle(
        canvas, 40, 40 + moustache,
        23, 38, 36, 44, silver);
    pv_triangle(
        canvas, 40, 40 + moustache,
        57, 38, 44, 44, pale);
    pv_triangle(canvas, 19, 39, 31, 58, 37, 46, silver);
    pv_triangle(canvas, 61, 39, 49, 58, 43, 46, pale);
    pv_draw_mouth(canvas, pose, 0U, lip, ink, cream, tongue);
    pv_quad(canvas, 8, 53, 25, 44, 40, 54, 40, 58, amber);
    pv_quad(canvas, 40, 54, 55, 44, 72, 53, 40, 58, gold);
}

static void pv_draw_talkie(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned version)
{
    const uint16_t ink = PV_RGB565(13, 17, 27);
    const uint16_t bulkhead = PV_RGB565(25, 49, 66);
    const uint16_t panel = PV_RGB565(42, 78, 91);
    const uint16_t teal = PV_RGB565(56, 128, 126);
    const uint16_t cyan = PV_RGB565(86, 225, 211);
    const uint16_t amber = PV_RGB565(245, 177, 61);
    const uint16_t hair = PV_RGB565(43, 27, 29);
    const uint16_t skin_shadow = PV_RGB565(112, 62, 54);
    const uint16_t skin = PV_RGB565(194, 119, 81);
    const uint16_t skin_light = PV_RGB565(243, 171, 108);
    const uint16_t white = PV_RGB565(238, 229, 199);
    const uint16_t lip = PV_RGB565(125, 38, 55);
    const uint16_t tongue = PV_RGB565(222, 84, 102);
    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    const int lift = pv_expression_lift(pose);

    if (version == 0U) {
        pv_clear(canvas, ink);
        pv_rect(canvas, 2, 2, 76, 56, bulkhead);
        pv_checker(canvas, 4, 4, 72, 52, bulkhead, panel, 10U);
        pv_rect(canvas, 5, 47, 70, 11, PV_RGB565(39, 68, 73));
        pv_triangle(
            canvas, 4 + lean_x, 58, 21 + lean_x,
            42 + lean_y, 43 + lean_x, 52 + lean_y, teal);
        pv_triangle(
            canvas, 37 + lean_x, 52 + lean_y,
            61 + lean_x, 42 + lean_y, 76 + lean_x, 58, bulkhead);
        pv_ellipse(canvas, 41, 29, 30, 28, hair);
        pv_quad(canvas, 15, 18, 40, 5, 41, 55, 14, 43, skin_light);
        pv_quad(canvas, 40, 5, 69, 17, 66, 44, 41, 55, skin);
        pv_triangle(canvas, 12, 16, 31, 3, 56, 6, hair);
        pv_triangle(canvas, 50, 5, 70, 16, 59, 13, hair);
        pv_rect(canvas, 7, 17, 8, 25, amber);
        pv_frame(canvas, 9, 19, 5, 20, 2, ink);
        pv_line(canvas, 12, 38, 22, 44, amber);
        pv_line(
            canvas, 22, 44,
            29 + (pose->speech_phase == FACE_SPEECH_ACTIVE ? 2 : 0),
            44, amber);
        pv_rect(canvas, 28, 42, 5, 4, amber);
        pv_rect(canvas, 58, 49, 10, 4, amber);
        pv_put(canvas, 61, 50, cyan);
        pv_draw_eye(
            canvas, pose, 0U, 2U, skin_shadow, white, teal, ink, white);
        pv_draw_eye(
            canvas, pose, 1U, 2U, skin_shadow, white, teal, ink, white);
        pv_draw_brows(canvas, pose, 8, 3, hair);
        pv_triangle(canvas, 38, 26, 43, 26, 39, 38, skin_shadow);
        pv_line(canvas, 56, 23, 61, 31, lip);
        pv_draw_mouth(canvas, pose, 2U, lip, ink, white, tongue);
        return;
    }

    if (version == 1U) {
        const uint16_t magenta = PV_RGB565(218, 52, 126);
        pv_clear(canvas, PV_RGB565(11, 19, 33));
        pv_checker(
            canvas, 2, 2, 76, 56,
            PV_RGB565(11, 19, 33), panel, 12U);
        pv_line(canvas, 5, 51, 25, 41, magenta);
        pv_line(canvas, 55, 43, 75, 52, cyan);
        pv_triangle(
            canvas, 4 + lean_x, 58, 24 + lean_x,
            43 + lean_y, 43 + lean_x, 52 + lean_y,
            PV_RGB565(53, 72, 83));
        pv_triangle(
            canvas, 39 + lean_x, 52 + lean_y,
            59 + lean_x, 43 + lean_y, 76 + lean_x, 58,
            PV_RGB565(73, 91, 94));
        pv_ellipse(canvas, 40, 30, 29, 27, hair);
        pv_quad(canvas, 15, 18, 39, 7, 41, 54, 15, 44, skin);
        pv_quad(canvas, 39, 7, 67, 18, 65, 44, 41, 54, skin_shadow);
        pv_triangle(canvas, 10, 17, 31, 4, 58, 8, hair);
        pv_rect(canvas, 18, 8, 45, 7, PV_RGB565(45, 65, 74));
        pv_quad(canvas, 17, 10, 61, 10, 56, 19, 21, 19, cyan);
        pv_quad(canvas, 20, 12, 58, 12, 54, 17, 23, 17, ink);
        const int visor_lift =
            pose->stage_expression == FACE_EXPRESSION_SLEEPY ? 3 :
            pose->stage_expression == FACE_EXPRESSION_DETERMINED ? -2 :
            0;
        pv_line(canvas, 18, 17 + visor_lift, 62, 17 + visor_lift, magenta);
        pv_rect(canvas, 66, 19, 6, 20, amber);
        pv_line(canvas, 69, 38, 59, 45, amber);
        pv_draw_eye(
            canvas, pose, 0U, 2U, skin_shadow, white, cyan, ink, white);
        pv_draw_eye(
            canvas, pose, 1U, 2U, skin_shadow, white, cyan, ink, white);
        pv_draw_brows(canvas, pose, 8, 3, hair);
        pv_triangle(canvas, 38, 27, 43, 27, 40, 38, skin_light);
        pv_draw_mouth(canvas, pose, 1U, lip, ink, white, tongue);
        pv_rect(canvas, 10, 49, 15, 5, magenta);
        pv_rect(canvas, 13, 50, 5, 2, amber);
        pv_line(canvas, 55, 47, 65, 54 + lift, cyan);
        return;
    }

    const uint16_t navy = PV_RGB565(22, 34, 55);
    const uint16_t red = PV_RGB565(181, 45, 54);
    pv_clear(canvas, ink);
    pv_rect(canvas, 2, 2, 76, 56, navy);
    pv_checker(canvas, 4, 4, 72, 52, navy, bulkhead, 11U);
    pv_triangle(
        canvas, 4 + lean_x, 58, 19 + lean_x,
        42 + lean_y, 42 + lean_x, 51 + lean_y, navy);
    pv_triangle(
        canvas, 38 + lean_x, 51 + lean_y,
        63 + lean_x, 42 + lean_y, 76 + lean_x, 58,
        PV_RGB565(38, 57, 76));
    pv_rect(canvas, 8, 47, 17, 7, amber);
    pv_rect(canvas, 55, 47, 17, 7, amber);
    pv_triangle(canvas, 7, 53, 25, 45, 30, 58, navy);
    pv_triangle(canvas, 73, 53, 55, 45, 50, 58, navy);
    pv_ellipse(canvas, 40, 30, 28, 27, hair);
    pv_ellipse(canvas, 40, 31, 24, 24, skin);
    pv_quad(canvas, 16, 21, 39, 8, 40, 55, 16, 45, skin_light);
    pv_quad(canvas, 39, 8, 65, 20, 64, 45, 40, 55, skin);
    pv_rect(canvas, 15, 8, 50, 8, navy);
    pv_triangle(canvas, 14, 10, 40, 3, 66, 10, navy);
    pv_rect(canvas, 10, 14, 60, 5, ink);
    pv_rect(canvas, 17, 14, 46, 3, red);
    pv_rect(canvas, 66, 20, 6, 18, amber);
    pv_line(canvas, 69, 37, 58, 44, amber);
    pv_rect(canvas, 55, 42, 5, 4, red);
    pv_draw_eye(
        canvas, pose, 0U, 2U, skin_shadow, white, teal, ink, white);
    pv_draw_eye(
        canvas, pose, 1U, 2U, skin_shadow, white, teal, ink, white);
    pv_draw_brows(canvas, pose, 8, 3, ink);
    pv_triangle(canvas, 38, 27, 43, 27, 40, 38, skin_shadow);
    pv_draw_mouth(canvas, pose, 2U, lip, ink, white, tongue);
}

static void pv_draw_robot_eye(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    unsigned style,
    uint16_t frame,
    uint16_t glow,
    uint16_t hot,
    uint16_t dark)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pv_clamp(pose->eye_w[eye], 8, 19);
    const int aperture = pv_clamp(pose->eye_open[eye], 1, 10);
    if (style == 2U) {
        pv_ellipse(canvas, cx, cy, width / 2 + 2, aperture / 2 + 2, frame);
        pv_ellipse(canvas, cx, cy, width / 2, aperture / 2, dark);
    } else {
        const int slant = style == 1U
            ? (eye == 0U ? pose->head_roll : -pose->head_roll)
            : 0;
        pv_quad(
            canvas,
            cx - width / 2 - 2, cy - aperture / 2 + slant,
            cx + width / 2 + 2, cy - aperture / 2 - slant,
            cx + width / 2 + 2, cy + aperture / 2,
            cx - width / 2 - 2, cy + aperture / 2,
            frame);
        pv_rect(
            canvas, cx - width / 2, cy - aperture / 2,
            width, aperture, dark);
    }
    if (aperture <= 1) {
        pv_rect(canvas, cx - width / 2, cy, width, 1, hot);
        return;
    }
    const int px = pv_clamp(
        pose->pupil_x[eye], cx - width / 2 + 1, cx + width / 2 - 1);
    const int py = pv_clamp(
        pose->pupil_y[eye], cy - aperture / 2 + 1,
        cy + aperture / 2 - 1);
    if (style == 2U) {
        pv_ellipse(canvas, px, py, 3, 2, glow);
        pv_rect(canvas, px, py - 1, 1, 3, hot);
    } else {
        pv_rect(canvas, px - 2, py - 1, 5, 3, glow);
        pv_rect(canvas, px, py - 1, 1, 3, hot);
    }
}

static void pv_draw_automaton(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned version)
{
    const uint16_t black = PV_RGB565(3, 7, 15);
    const uint16_t navy = PV_RGB565(8, 24, 43);
    const uint16_t steel = PV_RGB565(49, 72, 86);
    const uint16_t light_steel = PV_RGB565(106, 139, 145);
    const uint16_t cyan_dim = PV_RGB565(10, 77, 88);
    const uint16_t cyan = PV_RGB565(42, 225, 220);
    const uint16_t white = PV_RGB565(218, 255, 239);
    const uint16_t amber = PV_RGB565(247, 161, 38);
    const uint16_t red = PV_RGB565(222, 43, 65);
    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    const int lift = pv_expression_lift(pose);

    if (version == 0U) {
        pv_clear(canvas, black);
        pv_checker(canvas, 2, 2, 76, 56, black, navy, 12U);
        pv_rect(canvas, 14 + lean_x, 49 + lean_y, 52, 9, steel);
        pv_ellipse(canvas, 40, 29, 31, 25, steel);
        pv_rect(canvas, 10, 18, 60, 30, steel);
        pv_ellipse(canvas, 40, 29, 27, 21, navy);
        pv_rect(canvas, 14, 18, 52, 29, navy);
        pv_rect(canvas, 17, 20, 46, 14, black);
        pv_frame(canvas, 15, 18, 50, 18, 3, light_steel);
        pv_draw_robot_eye(
            canvas, pose, 0U, 0U, cyan_dim, cyan, white, black);
        pv_draw_robot_eye(
            canvas, pose, 1U, 0U, cyan_dim, cyan, white, black);
        pv_draw_led_mouth(
            canvas, pose, 0U, steel, navy, cyan, white);
        pv_rect(canvas, 6, 24, 7, 14, light_steel);
        pv_rect(canvas, 67, 24, 7, 14, light_steel);
        pv_line(canvas, 40, 7, 40 + pose->head_roll, 3, light_steel);
        pv_ellipse(
            canvas, 40 + pose->head_roll, 3, 2, 2,
            pose->speech_phase == FACE_SPEECH_ACTIVE ? amber : cyan);
        pv_rect(canvas, 22, 51, 36, 4, navy);
        pv_rect(canvas, 36, 52, 3, 2, cyan);
        pv_rect(canvas, 42, 52, 3, 2, amber);
        return;
    }

    if (version == 1U) {
        pv_clear(canvas, black);
        pv_checker(canvas, 2, 2, 76, 56, black, navy, 10U);
        pv_triangle(
            canvas, 8 + lean_x, 58, 22 + lean_x,
            45 + lean_y, 40 + lean_x, 51 + lean_y, steel);
        pv_triangle(
            canvas, 40 + lean_x, 51 + lean_y,
            58 + lean_x, 45 + lean_y, 72 + lean_x, 58, light_steel);
        pv_triangle(canvas, 8, 26, 20, 8, 39, 12, steel);
        pv_triangle(canvas, 72, 26, 60, 8, 41, 12, light_steel);
        pv_rect(canvas, 13, 14, 54, 36, steel);
        pv_quad(canvas, 16, 17, 64, 17, 59, 47, 21, 47, navy);
        pv_quad(canvas, 14, 17, 40, 9, 66, 17, 62, 22, light_steel);
        pv_draw_robot_eye(
            canvas, pose, 0U, 1U, steel, cyan, white, black);
        pv_draw_robot_eye(
            canvas, pose, 1U, 1U, steel, cyan, white, black);
        pv_thick_line(
            canvas, 18, pose->brow_outer_y[0],
            37, pose->brow_inner_y[0], 3, light_steel);
        pv_thick_line(
            canvas, 43, pose->brow_inner_y[1],
            62, pose->brow_outer_y[1], 3, light_steel);
        pv_draw_led_mouth(
            canvas, pose, 1U, steel, black, cyan, white);
        pv_rect(canvas, 8, 28, 6, 13, red);
        pv_rect(canvas, 66, 28, 6, 13, red);
        pv_line(canvas, 40, 11, 49 + pose->head_roll, 4, light_steel);
        pv_triangle(
            canvas, 47 + pose->head_roll, 3,
            53 + pose->head_roll, 4,
            49 + pose->head_roll, 8,
            pose->speech_phase == FACE_SPEECH_ACTIVE ? amber : cyan);
        return;
    }

    pv_clear(canvas, PV_RGB565(18, 12, 28));
    pv_checker(
        canvas, 2, 2, 76, 56,
        PV_RGB565(18, 12, 28),
        PV_RGB565(44, 24, 55), 11U);
    pv_rect(canvas, 12 + lean_x, 48 + lean_y, 56, 10, steel);
    pv_ellipse(canvas, 40, 29, 31, 27, light_steel);
    pv_ellipse(canvas, 40, 30, 27, 23, navy);
    pv_rect(canvas, 12, 26, 7, 15, steel);
    pv_rect(canvas, 61, 26, 7, 15, steel);
    pv_ellipse(canvas, 40, 12, 22, 10, steel);
    pv_rect(canvas, 19, 11, 42, 8, steel);
    pv_rect(canvas, 23, 14, 34, 3, amber);
    pv_draw_robot_eye(
        canvas, pose, 0U, 2U, steel, cyan, white, black);
    pv_draw_robot_eye(
        canvas, pose, 1U, 2U, steel, cyan, white, black);
    pv_draw_led_mouth(
        canvas, pose, 2U, steel, black, amber, white);
    pv_line(canvas, 40, 8, 40 + pose->head_roll, 3, steel);
    pv_ellipse(
        canvas, 40 + pose->head_roll, 3, 3, 2,
        pose->speech_phase == FACE_SPEECH_ACTIVE ? red : amber);
    pv_rect(canvas, 18, 44, 7, 5, red);
    pv_rect(canvas, 55, 44, 7, 5, red);
    pv_line(canvas, 17, 52, 9, 49 + lift + pose->speech_pulse, amber);
    pv_line(canvas, 63, 52, 71, 49 + lift + pose->speech_pulse, amber);
}

static void pv_draw_moss_eye(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    unsigned style,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pv_clamp(pose->eye_w[eye], 9, 16);
    const int aperture = pv_clamp(pose->eye_open[eye], 1, 9);
    if (aperture <= 1) {
        const int slope =
            pose->stage_expression == FACE_EXPRESSION_JOY
            ? (eye == 0U ? -2 : 2)
            : 0;
        pv_thick_line(
            canvas, cx - width / 2, cy + slope,
            cx + width / 2, cy - slope, 2, outline);
        return;
    }
    if (style == 0U) {
        pv_quad(
            canvas, cx - width / 2 - 1, cy,
            cx - width / 3, cy - aperture / 2 - 1,
            cx + width / 3, cy - aperture / 2 - 1,
            cx + width / 2 + 1, cy, outline);
        pv_quad(
            canvas, cx - width / 2 - 1, cy,
            cx + width / 2 + 1, cy,
            cx + width / 3, cy + aperture / 2 + 1,
            cx - width / 3, cy + aperture / 2 + 1, outline);
        pv_ellipse(
            canvas, cx, cy, width / 2 - 1,
            pv_clamp(aperture / 2 - 1, 1, 4), sclera);
    } else {
        pv_ellipse(
            canvas, cx, cy, width / 2 + 1,
            aperture / 2 + 1, outline);
        pv_ellipse(
            canvas, cx, cy, width / 2 - 1,
            pv_clamp(aperture / 2 - 1, 1, 4), sclera);
    }
    const int px = pv_clamp(
        pose->pupil_x[eye], cx - width / 2 + 2, cx + width / 2 - 2);
    const int py = pv_clamp(
        pose->pupil_y[eye], cy - aperture / 2 + 1,
        cy + aperture / 2 - 1);
    if (style == 2U) {
        pv_ellipse(canvas, px, py, 2, 3, iris);
        pv_rect(canvas, px, py - 2, 1, 5, outline);
    } else {
        pv_ellipse(canvas, px, py, 3, 3, iris);
        pv_rect(canvas, px, py - 1, 2, 3, outline);
    }
    if (pose->attention > 72U) {
        pv_rect(canvas, px - 1, py - 1, 2, 1, glint);
    }
    if (pose->stage_expression == FACE_EXPRESSION_EXCITED &&
        aperture >= 4) {
        /*
         * A tiny four-pixel sparkle survives the exact 40x30 contact view and
         * gives excited a different eye read from merely surprised.
         */
        pv_line(canvas, px - 2, py, px + 2, py, glint);
        pv_line(canvas, px, py - 2, px, py + 2, glint);
        pv_put(canvas, px, py, outline);
    }
}

static void pv_draw_moss_mouth(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned style,
    uint16_t outline,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    face_pixel_redux_pose_t mouth_pose = *pose;
    mouth_pose.mouth_w = (int16_t)pv_clamp(
        mouth_pose.mouth_w + (style == 1U ? 2 : 0), 8, 27);
    mouth_pose.mouth_h = (int16_t)pv_clamp(
        mouth_pose.mouth_h + (style == 1U ? 1 : 0), 1, 12);
    pv_draw_mouth(
        canvas, &mouth_pose, style == 0U ? 2U : 0U,
        outline, cavity, teeth, tongue);
    if (mouth_pose.mouth_h >= 4) {
        const int half = mouth_pose.mouth_w / 2;
        pv_rect(
            canvas,
            mouth_pose.mouth_x - half - 2,
            mouth_pose.mouth_y +
                mouth_pose.mouth_corner_y[0] / 2 - 1,
            3, 3, outline);
        pv_rect(
            canvas,
            mouth_pose.mouth_x + half,
            mouth_pose.mouth_y +
                mouth_pose.mouth_corner_y[1] / 2 - 1,
            3, 3, outline);
    }
}

static int pv_moss_ear_lift(
    const face_pixel_redux_pose_t *pose,
    bool left)
{
    int lift = pv_expression_lift(pose);
    if (pose->stage_expression == FACE_EXPRESSION_SKEPTICAL) {
        lift += left ? -3 : 3;
    }
    if (pose->stage_expression == FACE_EXPRESSION_THOUGHTFUL) {
        lift += left ? 2 : -2;
    }
    if (pose->stage_expression == FACE_EXPRESSION_EMBARRASSED) {
        lift += left ? 2 : 4;
    }
    if (pose->speech_phase == FACE_SPEECH_STARTING) {
        lift -= 2;
    } else if (pose->speech_phase == FACE_SPEECH_ENDING) {
        lift += 1;
    }
    return pv_clamp(lift + pose->speech_pulse, -5, 5);
}

static int pv_moss_triangle_wave(
    uint32_t sample_clock,
    uint32_t period,
    int amplitude)
{
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    const uint32_t ramp =
        phase < half ? phase : period - phase;
    return -amplitude +
        (int)(ramp * (uint32_t)(amplitude * 2) / half);
}

static void pv_apply_moss_idle_motion(
    face_pixel_redux_pose_t *pose,
    uint32_t sample_clock,
    int *head_dx,
    int *head_dy)
{
    int turn = 0;
    const uint32_t turn_phase = sample_clock % 192000U;
    if (turn_phase >= 64000U && turn_phase < 80000U) {
        turn = (int)((turn_phase - 64000U) * 2U / 16000U);
    } else if (turn_phase >= 80000U && turn_phase < 112000U) {
        turn = 2;
    } else if (turn_phase >= 112000U && turn_phase < 128000U) {
        turn = 2 -
            (int)((turn_phase - 112000U) * 2U / 16000U);
    } else if (turn_phase >= 144000U && turn_phase < 160000U) {
        turn = -(int)((turn_phase - 144000U) / 8000U);
    } else if (turn_phase >= 160000U && turn_phase < 176000U) {
        turn = -2 +
            (int)((turn_phase - 160000U) * 2U / 16000U);
    }

    *head_dx = turn / 2;
    *head_dy = pose->speaking
        ? pv_clamp(pose->speech_pulse, -1, 1)
        : pv_moss_triangle_wave(sample_clock, 64000U, 1);

    /*
     * Turn the facial mask slightly farther than the skull.  This gives a
     * readable two-pixel three-quarter cue without redrawing the silhouette.
     */
    const int feature_turn = turn - *head_dx;
    for (size_t eye = 0U; eye < 2U; ++eye) {
        pose->eye_x[eye] =
            (int16_t)(pose->eye_x[eye] + feature_turn);
        pose->pupil_x[eye] =
            (int16_t)(pose->pupil_x[eye] + turn);
    }
    pose->mouth_x = (int16_t)(pose->mouth_x + feature_turn);

    /*
     * Full blinks retain a one-logical-pixel eyelid lead.  During idle, the
     * same stagger is scheduled roughly every four seconds, so both eyes do
     * not mechanically snap shut on the same render.
     */
    if ((pose->source.controls.flags &
         FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        if (((sample_clock / 533U) & 1U) == 0U) {
            pose->eye_open[0] = 1;
            pose->eye_open[1] = 3;
        } else {
            pose->eye_open[0] = 3;
            pose->eye_open[1] = 1;
        }
    } else if (!pose->speaking) {
        const uint32_t blink_phase = sample_clock % 64000U;
        if (blink_phase >= 48000U && blink_phase < 49600U) {
            const uint32_t blink = blink_phase - 48000U;
            if (blink < 533U) {
                pose->eye_open[0] = 1;
                pose->eye_open[1] =
                    (int16_t)pv_clamp(pose->eye_open[1], 2, 4);
            } else if (blink < 1066U) {
                pose->eye_open[0] = 1;
                pose->eye_open[1] = 1;
            } else {
                pose->eye_open[0] =
                    (int16_t)pv_clamp(pose->eye_open[0], 2, 4);
                pose->eye_open[1] = 1;
            }
        }
    }
}

static void pv_draw_mossling(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *source_pose,
    unsigned version,
    uint32_t sample_clock)
{
    const uint16_t ink = PV_RGB565(15, 56, 15);
    const uint16_t forest = PV_RGB565(48, 98, 48);
    const uint16_t moss = PV_RGB565(139, 172, 15);
    const uint16_t leaf = PV_RGB565(155, 188, 15);
    const uint16_t cream = leaf;
    const uint16_t white = leaf;
    const uint16_t pink = forest;
    face_pixel_redux_pose_t animated_pose = *source_pose;
    int head_dx = 0;
    int head_dy = 0;
    pv_apply_moss_idle_motion(
        &animated_pose, sample_clock, &head_dx, &head_dy);
    const face_pixel_redux_pose_t *pose = &animated_pose;
    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    const int left_ear = pv_moss_ear_lift(pose, true);
    const int right_ear = pv_moss_ear_lift(pose, false);
    const int squash =
        pose->speech_phase == FACE_SPEECH_STARTING ? 1 :
        pose->stage_expression == FACE_EXPRESSION_JOY ||
        pose->stage_expression == FACE_EXPRESSION_EXCITED ? -1 :
        pose->stage_expression == FACE_EXPRESSION_SLEEPY ? 2 : 0;

    if (version == 0U) {
        const uint16_t bark = ink;
        const uint16_t bark_light = forest;
        pv_clear(canvas, cream);
        pv_checker(canvas, 2, 2, 76, 56, cream, leaf, 10U);
        pv_frame(canvas, 3, 3, 74, 54, 2, ink);
        pv_triangle(
            canvas, 11 + lean_x, 58, 26 + lean_x,
            43 + lean_y + squash, 40 + lean_x,
            49 + lean_y + squash, forest);
        pv_triangle(
            canvas, 40 + lean_x, 49 + lean_y + squash,
            55 + lean_x, 43 + lean_y + squash,
            69 + lean_x, 58, moss);
        pv_line(canvas, 24, 47, 52, 56, bark);
        pv_rect(canvas, 50, 49, 7, 6, bark_light);
        canvas->offset_x = head_dx;
        canvas->offset_y = head_dy;
        pv_triangle(
            canvas, 18, 25,
            23, 10 + left_ear,
            33, 20, forest);
        pv_triangle(
            canvas, 62, 25,
            57, 10 + right_ear,
            47, 20, forest);
        pv_triangle(
            canvas, 21, 22,
            24, 13 + left_ear,
            29, 21, leaf);
        pv_triangle(
            canvas, 59, 22,
            56, 13 + right_ear,
            51, 21, leaf);
        pv_ellipse(canvas, 40, 30 + squash, 23, 21 - squash, ink);
        pv_ellipse(canvas, 40, 31 + squash, 19, 18 - squash, cream);
        pv_ellipse(canvas, 40, 14 + squash, 22, 11, bark);
        pv_rect(canvas, 20, 13 + squash, 40, 8, bark);
        pv_checker(
            canvas, 23, 12 + squash, 34, 6,
            bark, bark_light, 3U);
        const int sprout_x =
            40 + pose->head_roll + left_ear / 3;
        const int sprout_y =
            pv_clamp(8 + squash + pv_expression_lift(pose), 7, 11);
        pv_thick_line(
            canvas, 40, 13 + squash,
            sprout_x, sprout_y, 2, bark);
        pv_triangle(
            canvas, sprout_x, sprout_y + 1,
            sprout_x - 8, sprout_y - 3,
            sprout_x - 3, sprout_y + 3, leaf);
        pv_triangle(
            canvas, sprout_x, sprout_y + 1,
            sprout_x + 7, sprout_y - 4,
            sprout_x + 4, sprout_y + 3, moss);
        pv_draw_moss_eye(
            canvas, pose, 0U, 0U, ink, white, moss, white);
        pv_draw_moss_eye(
            canvas, pose, 1U, 0U, ink, white, moss, white);
        pv_draw_brows(canvas, pose, 5, 2, forest);
        pv_triangle(canvas, 38, 33, 42, 33, 40, 36, forest);
        pv_draw_moss_mouth(canvas, pose, 0U, ink, forest, white, pink);
        if (pose->cheek > 62U) {
            pv_rect(canvas, 22, 35, 7, 3, pink);
            pv_rect(canvas, 51, 35, 7, 3, pink);
        }
        canvas->offset_x = 0;
        canvas->offset_y = 0;
        const int tail_y = 47 +
            pv_clamp(right_ear + pose->speech_pulse, -4, 4);
        pv_thick_line(
            canvas, 58 + lean_x, 51 + lean_y,
            69 + lean_x, tail_y + lean_y, 2, ink);
        pv_triangle(
            canvas, 68 + lean_x, tail_y - 2 + lean_y,
            75 + lean_x, tail_y + lean_y,
            69 + lean_x, tail_y + 4 + lean_y, leaf);
        return;
    }

    if (version == 1U) {
        const uint16_t water = forest;
        const uint16_t water_light = moss;
        const uint16_t lilac = ink;
        pv_clear(canvas, water);
        pv_checker(canvas, 2, 2, 76, 56, water, water_light, 12U);
        pv_frame(canvas, 3, 3, 74, 54, 2, ink);
        pv_ellipse(canvas, 16, 12, 7, 3, leaf);
        pv_ellipse(canvas, 65, 48, 8, 3, forest);
        pv_triangle(
            canvas, 9 + lean_x, 58, 25 + lean_x,
            43 + lean_y + squash, 40 + lean_x,
            50 + lean_y + squash, forest);
        pv_triangle(
            canvas, 40 + lean_x, 50 + lean_y + squash,
            55 + lean_x, 43 + lean_y + squash,
            71 + lean_x, 58, moss);
        canvas->offset_x = head_dx;
        canvas->offset_y = head_dy;
        pv_triangle(
            canvas, 17, 28,
            11, 18 + left_ear,
            26, 23, leaf);
        pv_triangle(
            canvas, 63, 28,
            69, 18 + right_ear,
            54, 23, leaf);
        pv_triangle(
            canvas, 18, 31,
            10, 31 + left_ear,
            24, 36, lilac);
        pv_triangle(
            canvas, 62, 31,
            70, 31 + right_ear,
            56, 36, lilac);
        pv_ellipse(canvas, 40, 30 + squash, 24, 21 - squash, ink);
        pv_ellipse(canvas, 40, 30 + squash, 20, 18 - squash, cream);
        pv_ellipse(canvas, 40, 14 + squash, 27, 8, forest);
        pv_ellipse(canvas, 40, 12 + squash, 22, 6, leaf);
        const int reed_tip =
            pv_clamp(8 + squash + left_ear / 2, 7, 11);
        pv_thick_line(canvas, 40, 13 + squash, 40, reed_tip, 2, moss);
        pv_triangle(
            canvas, 40, reed_tip + 1,
            33, reed_tip - 2,
            37, reed_tip + 4, cream);
        pv_triangle(
            canvas, 40, reed_tip + 1,
            47, reed_tip - 3,
            44, reed_tip + 4, water_light);
        pv_draw_moss_eye(
            canvas, pose, 0U, 1U, ink, white, water_light, white);
        pv_draw_moss_eye(
            canvas, pose, 1U, 1U, ink, white, water_light, white);
        pv_draw_brows(canvas, pose, 6, 2, forest);
        pv_triangle(canvas, 38, 33, 42, 33, 40, 36, forest);
        pv_draw_moss_mouth(canvas, pose, 1U, ink, forest, white, pink);
        if (pose->cheek > 55U) {
            pv_ellipse(canvas, 24, 36, 4, 2, lilac);
            pv_ellipse(canvas, 56, 36, 4, 2, lilac);
        }
        canvas->offset_x = 0;
        canvas->offset_y = 0;
        const int bob =
            pv_clamp(left_ear / 2 + pose->speech_pulse, -3, 3);
        pv_ellipse(canvas, 13, 44 + bob, 5, 2, leaf);
        pv_ellipse(canvas, 69, 39 - bob, 4, 2, cream);
        return;
    }

    const uint16_t midnight = ink;
    const uint16_t indigo = forest;
    const uint16_t violet = moss;
    const uint16_t glow = leaf;
    pv_clear(canvas, midnight);
    pv_checker(canvas, 2, 2, 76, 56, midnight, indigo, 13U);
    pv_frame(canvas, 3, 3, 74, 54, 2, glow);
    pv_put(canvas, 10, 10, glow);
    pv_put(canvas, 69, 17, glow);
    pv_put(canvas, 62, 7, white);
    pv_triangle(
        canvas, 8 + lean_x, 58, 24 + lean_x,
        42 + lean_y + squash, 40 + lean_x,
        49 + lean_y + squash, indigo);
    pv_triangle(
        canvas, 40 + lean_x, 49 + lean_y + squash,
        56 + lean_x, 42 + lean_y + squash,
        72 + lean_x, 58, violet);
    canvas->offset_x = head_dx;
    canvas->offset_y = head_dy;
    pv_triangle(
        canvas, 17, 27,
        20, 13 + left_ear,
        31, 21, violet);
    pv_triangle(
        canvas, 63, 27,
        60, 13 + right_ear,
        49, 21, violet);
    pv_ellipse(canvas, 40, 31 + squash, 23, 21 - squash, ink);
    pv_ellipse(canvas, 40, 32 + squash, 18, 17 - squash, indigo);
    pv_ellipse(canvas, 40, 14 + squash, 31, 12, violet);
    pv_ellipse(canvas, 40, 12 + squash, 25, 9, moss);
    pv_rect(canvas, 35, 13 + squash, 10, 10, violet);
    pv_ellipse(canvas, 27, 10 + squash, 3, 2, glow);
    pv_ellipse(canvas, 50, 7 + squash, 4, 2, cream);
    pv_ellipse(canvas, 58, 13 + squash, 3, 2, glow);
    pv_draw_moss_eye(
        canvas, pose, 0U, 2U, ink, midnight, glow, white);
    pv_draw_moss_eye(
        canvas, pose, 1U, 2U, ink, midnight, glow, white);
    pv_draw_brows(canvas, pose, 5, 2, violet);
    pv_triangle(canvas, 38, 34, 42, 34, 40, 37, glow);
    pv_draw_moss_mouth(canvas, pose, 2U, glow, ink, white, pink);
    canvas->offset_x = 0;
    canvas->offset_y = 0;
    pv_triangle(canvas, 15, 47, 31, 41, 38, 58, violet);
    pv_triangle(canvas, 65, 47, 49, 41, 42, 58, indigo);
    const int tail_y = 46 +
        pv_clamp(right_ear + pose->speech_pulse, -4, 4);
    pv_thick_line(
        canvas, 60 + lean_x, 52 + lean_y,
        71 + lean_x, tail_y + lean_y, 2, glow);
    pv_ellipse(
        canvas, 72 + lean_x, tail_y + lean_y, 3, 3, violet);
}

static void pv_draw_dmg_eye(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    unsigned style,
    uint16_t dark,
    uint16_t mid_dark,
    uint16_t mid,
    uint16_t light)
{
    if (style != 0U) {
        pv_draw_moss_eye(
            canvas,
            pose,
            eye,
            style == 1U ? 0U : 1U,
            dark,
            style == 1U ? mid : light,
            mid_dark,
            light);
        return;
    }

    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pv_clamp(pose->eye_w[eye] + 1, 9, 16);
    const int aperture = pv_clamp(pose->eye_open[eye], 1, 8);
    if (aperture <= 1) {
        pv_thick_line(
            canvas, cx - width / 2, cy,
            cx + width / 2, cy, 2, dark);
        return;
    }
    pv_rect(
        canvas, cx - width / 2 - 1, cy - aperture / 2 - 1,
        width + 2, aperture + 2, dark);
    pv_rect(
        canvas, cx - width / 2, cy - aperture / 2,
        width, aperture, mid);
    /* The upper shade is a physical visor shutter, not an eyebrow decal. */
    const int shutter =
        pv_clamp(
            pose->brow_inner_y[eye] - pose->brow_outer_y[eye] + 2,
            1,
            aperture - 1);
    pv_rect(
        canvas, cx - width / 2, cy - aperture / 2,
        width, shutter, mid_dark);
    const int px = pv_clamp(
        pose->pupil_x[eye],
        cx - width / 2 + 2,
        cx + width / 2 - 2);
    const int py = pv_clamp(
        pose->pupil_y[eye],
        cy - aperture / 2 + shutter,
        cy + aperture / 2 - 1);
    pv_rect(canvas, px - 1, py - 1, 3, 3, dark);
    if (pose->attention > 72U) {
        pv_put(canvas, px - 1, py - 1, light);
    }
    if (pose->stage_expression == FACE_EXPRESSION_EXCITED &&
        aperture >= 4) {
        pv_line(canvas, px - 2, py, px + 2, py, light);
        pv_line(canvas, px, py - 2, px, py + 2, light);
        pv_put(canvas, px, py, dark);
    }
}

static void pv_draw_dmg_mouth(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    unsigned style,
    uint16_t dark,
    uint16_t mid_dark,
    uint16_t mid,
    uint16_t light)
{
    face_pixel_redux_pose_t mouth = *pose;
    mouth.mouth_w = (int16_t)pv_clamp(
        mouth.mouth_w + (style == 2U ? 3 : 0), 8, 29);
    mouth.mouth_h = (int16_t)pv_clamp(
        mouth.mouth_h + (style == 2U ? 1 : 0), 1, 13);
    pv_draw_mouth(
        canvas,
        &mouth,
        style == 0U ? 2U : (style == 1U ? 0U : 1U),
        mid_dark,
        dark,
        light,
        mid);

    if (style == 0U) {
        /* Hinge pins make the speech movement read as a moving jaw plate. */
        const int half = mouth.mouth_w / 2;
        pv_rect(
            canvas,
            mouth.mouth_x - half - 3,
            mouth.mouth_y - 1,
            2,
            2,
            light);
        pv_rect(
            canvas,
            mouth.mouth_x + half + 1,
            mouth.mouth_y - 1,
            2,
            2,
            light);
    } else if (style == 1U && mouth.mouth_h <= 2) {
        /* The moth's resting proboscis has a tiny hooked silhouette. */
        pv_put(canvas, mouth.mouth_x - 2, mouth.mouth_y + 1, dark);
        pv_put(canvas, mouth.mouth_x + 2, mouth.mouth_y + 1, dark);
    } else if (style == 2U && mouth.mouth_h >= 5) {
        /* A suspended highlight sells transparent, elastic gel. */
        pv_rect(
            canvas,
            mouth.mouth_x - mouth.mouth_w / 4,
            mouth.mouth_y - mouth.mouth_h / 2,
            2,
            1,
            light);
    }
}

static void pv_draw_dmg_handheld(
    pv_canvas_t *canvas,
    const face_pixel_redux_pose_t *source_pose,
    unsigned version)
{
    /*
     * Strict DMG interchange palette.  Every branch below uses these same
     * four RGB565 values; identity comes from authored clusters and motion,
     * never from palette swapping.
     */
    const uint16_t dark = PV_RGB565(15, 56, 15);
    const uint16_t mid_dark = PV_RGB565(48, 98, 48);
    const uint16_t mid = PV_RGB565(139, 172, 15);
    const uint16_t light = PV_RGB565(155, 188, 15);
    face_pixel_redux_pose_t pose = *source_pose;
    const int lean_x = pose.body_lean_x;
    const int lean_y = pose.body_lean_y;
    const int lift = pv_expression_lift(&pose);
    const int left_action = pv_moss_ear_lift(&pose, true);
    const int right_action = pv_moss_ear_lift(&pose, false);

    if (version == 0U) {
        /*
         * Tin Warden: rigid, riveted clusters and an independently hinged
         * chin.  Acting is concentrated in visor shutters, crest and jaw.
         */
        const int jaw_drop = pv_clamp(
            pose.mouth_h / 5 +
                (pose.speech_phase == FACE_SPEECH_STARTING ? 1 : 0),
            0,
            3);
        pv_clear(canvas, light);
        pv_checker(canvas, 2, 2, 76, 56, light, mid, 13U);
        pv_frame(canvas, 3, 3, 74, 54, 2, dark);
        pv_rect(canvas, 3, 51, 74, 6, mid_dark);
        pv_quad(
            canvas,
            7 + lean_x, 57,
            21 + lean_x, 42 + lean_y,
            40 + lean_x, 48 + lean_y,
            40 + lean_x, 57,
            dark);
        pv_quad(
            canvas,
            40 + lean_x, 48 + lean_y,
            59 + lean_x, 42 + lean_y,
            73 + lean_x, 57,
            40 + lean_x, 57,
            mid_dark);
        pv_rect(canvas, 13, 16, 54, 29, dark);
        pv_quad(canvas, 17, 15, 26, 8, 54, 8, 63, 15, mid_dark);
        pv_rect(canvas, 18, 17, 44, 26, mid);
        pv_rect(canvas, 12, 22, 7, 16, mid_dark);
        pv_rect(canvas, 61, 22, 7, 16, mid_dark);
        pv_ellipse(canvas, 15, 30, 3, 4, mid);
        pv_ellipse(canvas, 65, 30, 3, 4, mid);
        pv_rect(canvas, 21, 20, 38, 3, mid_dark);
        pv_draw_dmg_eye(
            canvas, &pose, 0U, 0U, dark, mid_dark, mid, light);
        pv_draw_dmg_eye(
            canvas, &pose, 1U, 0U, dark, mid_dark, mid, light);
        pv_thick_line(
            canvas,
            20,
            pose.brow_outer_y[0],
            36,
            pose.brow_inner_y[0],
            2,
            dark);
        pv_thick_line(
            canvas,
            44,
            pose.brow_inner_y[1],
            60,
            pose.brow_outer_y[1],
            2,
            dark);
        pv_rect(canvas, 24, 35, 32, 15 + jaw_drop, dark);
        pv_rect(canvas, 27, 37, 26, 11 + jaw_drop, mid);
        pv_rect(canvas, 20, 40, 5, 7 + jaw_drop, mid_dark);
        pv_rect(canvas, 55, 40, 5, 7 + jaw_drop, mid_dark);
        pose.mouth_y = (int16_t)(41 + jaw_drop);
        pv_draw_dmg_mouth(
            canvas, &pose, 0U, dark, mid_dark, mid, light);
        pv_rect(canvas, 29, 13, 3, 3, light);
        pv_rect(canvas, 48, 13, 3, 3, light);
        const int crest_x = 42 + pose.head_roll + left_action / 2;
        const int crest_y = pv_clamp(7 + lift, 5, 9);
        pv_thick_line(canvas, 40, 9, crest_x, crest_y, 2, dark);
        pv_triangle(
            canvas,
            crest_x - 1, crest_y + 1,
            crest_x + 9, crest_y - 3,
            crest_x + 5, crest_y + 5,
            dark);
        pv_triangle(
            canvas,
            crest_x + 1, crest_y + 1,
            crest_x + 7, crest_y - 1,
            crest_x + 4, crest_y + 3,
            mid);
        pv_put(canvas, 15, 30, light);
        pv_put(canvas, 65, 30, light);
        return;
    }

    if (version == 1U) {
        /*
         * Lantern Moth: the face is quiet and soft while antennae and a wing
         * mantle lead anticipation, skepticism and emotional follow-through.
         */
        const int left_wing =
            pv_clamp(left_action + pose.speech_pulse, -5, 5);
        const int right_wing =
            pv_clamp(right_action - pose.speech_pulse, -5, 5);
        pv_clear(canvas, dark);
        pv_checker(canvas, 2, 2, 76, 56, dark, mid_dark, 13U);
        pv_frame(canvas, 3, 3, 74, 54, 2, light);
        pv_quad(
            canvas,
            4,
            28 + left_wing,
            19,
            13 + left_wing,
            28,
            38,
            17,
            52,
            mid);
        pv_quad(
            canvas,
            76,
            28 + right_wing,
            61,
            13 + right_wing,
            52,
            38,
            63,
            52,
            mid);
        pv_triangle(
            canvas,
            7,
            27 + left_wing,
            19,
            18 + left_wing,
            21,
            36,
            mid_dark);
        pv_triangle(
            canvas,
            73,
            27 + right_wing,
            61,
            18 + right_wing,
            59,
            36,
            mid_dark);
        pv_ellipse(canvas, 13, 30 + left_wing, 3, 5, light);
        pv_ellipse(canvas, 67, 30 + right_wing, 3, 5, light);
        pv_triangle(
            canvas,
            7 + lean_x,
            57,
            25 + lean_x,
            42 + lean_y,
            40 + lean_x,
            50 + lean_y,
            mid_dark);
        pv_triangle(
            canvas,
            40 + lean_x,
            50 + lean_y,
            55 + lean_x,
            42 + lean_y,
            73 + lean_x,
            57,
            mid);
        pv_ellipse(canvas, 40, 30, 24, 23, dark);
        pv_ellipse(canvas, 40, 31, 20, 19, mid);
        /* Fuzzy temple clusters keep the oval from reading as a recolour. */
        pv_triangle(canvas, 18, 20, 12, 25, 19, 29, mid_dark);
        pv_triangle(canvas, 62, 20, 68, 25, 61, 29, mid_dark);
        pv_triangle(canvas, 18, 31, 13, 36, 20, 39, mid_dark);
        pv_triangle(canvas, 62, 31, 67, 36, 60, 39, mid_dark);
        const int left_tip_y = pv_clamp(7 + left_action + lift, 6, 12);
        const int right_tip_y =
            pv_clamp(7 + right_action + lift, 6, 12);
        pv_thick_line(canvas, 33, 15, 20, left_tip_y, 2, mid_dark);
        pv_thick_line(canvas, 47, 15, 60, right_tip_y, 2, mid_dark);
        pv_ellipse(canvas, 18, left_tip_y, 4, 3, light);
        pv_ellipse(canvas, 62, right_tip_y, 4, 3, light);
        pv_draw_dmg_eye(
            canvas, &pose, 0U, 1U, dark, mid_dark, mid, light);
        pv_draw_dmg_eye(
            canvas, &pose, 1U, 1U, dark, mid_dark, mid, light);
        pv_draw_brows(canvas, &pose, 6, 2, dark);
        pv_triangle(canvas, 38, 33, 42, 33, 40, 36, mid_dark);
        pv_draw_dmg_mouth(
            canvas, &pose, 1U, dark, mid_dark, mid, light);
        pv_ellipse(canvas, 40, 52, 6, 4, dark);
        pv_ellipse(canvas, 40, 51, 3, 2, light);
        return;
    }

    /*
     * Slime Courier: the gel body squashes around stable eye landmarks while
     * the diagonal strap and square bag lag behind as visually rigid props.
     */
    int squash = 0;
    if (pose.speech_phase == FACE_SPEECH_STARTING) {
        squash = 2;
    } else if (
        pose.stage_expression == FACE_EXPRESSION_JOY ||
        pose.stage_expression == FACE_EXPRESSION_EXCITED) {
        squash = -2;
    } else if (pose.stage_expression == FACE_EXPRESSION_SLEEPY) {
        squash = 3;
    }
    const int wobble =
        pv_clamp(pose.speech_pulse + pose.body_lean_x, -3, 3);
    const int body_rx = 25 + squash;
    const int body_ry = 23 - squash;
    pv_clear(canvas, light);
    pv_checker(canvas, 2, 2, 76, 56, light, mid, 11U);
    pv_frame(canvas, 3, 3, 74, 54, 2, dark);
    pv_rect(canvas, 3, 52, 74, 5, mid_dark);
    pv_ellipse(canvas, 40 + lean_x, 33 + lean_y, body_rx + 2, body_ry, dark);
    pv_ellipse(canvas, 40 + lean_x, 33 + lean_y, body_rx - 1, body_ry - 3, mid);
    const int slime_tip_y = pv_clamp(6 + lift, 3, 9);
    pv_triangle(
        canvas,
        31 + wobble,
        16,
        41 + wobble,
        slime_tip_y,
        49 + wobble,
        17,
        dark);
    pv_triangle(
        canvas,
        34 + wobble,
        16,
        41 + wobble,
        slime_tip_y + 3,
        46 + wobble,
        18,
        mid);
    pv_ellipse(canvas, 19 + lean_x, 47 + lean_y, 8, 6, mid_dark);
    pv_ellipse(canvas, 61 + lean_x, 47 + lean_y, 8, 6, mid_dark);
    /*
     * Two edge clusters imply the strap while preserving a clean facial
     * mask; a full diagonal reads as a rendering tear at this resolution.
     */
    pv_thick_line(canvas, 20, 20, 28, 28, 2, dark);
    pv_thick_line(canvas, 21, 20, 28, 27, 1, light);
    pv_thick_line(canvas, 54, 43, 62, 49, 2, dark);
    pv_thick_line(canvas, 55, 43, 62, 48, 1, light);
    pv_rect(canvas, 57 - lean_x / 2, 39 - lean_y / 2, 16, 14, dark);
    pv_rect(canvas, 59 - lean_x / 2, 41 - lean_y / 2, 12, 10, mid_dark);
    pv_rect(canvas, 63 - lean_x / 2, 43 - lean_y / 2, 4, 3, light);
    pose.eye_y[0] = (int16_t)(pose.eye_y[0] + squash / 2);
    pose.eye_y[1] = (int16_t)(pose.eye_y[1] + squash / 2);
    pose.mouth_y = (int16_t)(pose.mouth_y + squash / 2);
    pv_draw_dmg_eye(
        canvas, &pose, 0U, 2U, dark, mid_dark, mid, light);
    pv_draw_dmg_eye(
        canvas, &pose, 1U, 2U, dark, mid_dark, mid, light);
    pv_draw_brows(canvas, &pose, 6, 2, dark);
    pv_draw_dmg_mouth(
        canvas, &pose, 2U, dark, mid_dark, mid, light);
    pv_ellipse(canvas, 27 + wobble, 18 + squash, 4, 2, light);
    pv_put(canvas, 24 + wobble, 21 + squash, light);
    const int bubble_y =
        pv_clamp(12 + right_action + pose.speech_pulse, 7, 18);
    pv_ellipse(canvas, 67, bubble_y, 3, 3, mid_dark);
    pv_put(canvas, 66, bubble_y - 1, light);
}

size_t face_pixel_redux_variant_count(void)
{
    return FACE_PIXEL_VARIANT_COUNT;
}

const char *face_pixel_redux_variant_slug(
    face_pixel_redux_variant_t variant)
{
    return pv_valid(variant) ? PV_DEFINITIONS[variant].slug : NULL;
}

const char *face_pixel_redux_variant_name(
    face_pixel_redux_variant_t variant)
{
    return pv_valid(variant) ? PV_DEFINITIONS[variant].name : NULL;
}

bool face_pixel_redux_variant_info(
    face_pixel_redux_variant_t variant,
    face_pixel_redux_variant_info_t *info)
{
    if (!pv_valid(variant) || info == NULL) {
        return false;
    }
    const pv_definition_t *definition = &PV_DEFINITIONS[variant];
    *info = (face_pixel_redux_variant_info_t){
        .slug = definition->slug,
        .name = definition->name,
        .base_actor = definition->base_actor,
        .authored_version = definition->authored_version,
        .palette_size = definition->palette_size,
        .estimated_ops_per_pixel = definition->ops,
    };
    return true;
}

bool face_pixel_redux_variant_render(
    face_pixel_redux_variant_t variant,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!pv_valid(variant) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_PIXEL_REDUX_PIXEL_COUNT) {
        return false;
    }
    const pv_definition_t *definition = &PV_DEFINITIONS[variant];
    face_pixel_redux_pose_t pose;
    if (!face_pixel_redux_actor_resolve(
            definition->base_actor,
            render_key,
            sample_clock,
            &pose)) {
        return false;
    }
    pv_tune_pose(variant, &pose);
    pv_canvas_t canvas = {
        .pixels = rgb565,
        .offset_x = 0,
        .offset_y = 0,
    };
    const unsigned family = (unsigned)variant / 3U;
    const unsigned version = (unsigned)variant % 3U;
    switch (family) {
    case 0U:
        pv_draw_wayfarer(&canvas, &pose, version);
        break;
    case 1U:
        pv_draw_oracle(&canvas, &pose, version);
        break;
    case 2U:
        pv_draw_talkie(&canvas, &pose, version);
        break;
    case 3U:
        pv_draw_automaton(&canvas, &pose, version);
        break;
    case 4U:
        pv_draw_mossling(&canvas, &pose, version, sample_clock);
        break;
    case 5U:
        pv_draw_dmg_handheld(&canvas, &pose, version);
        break;
    default:
        return false;
    }
    return true;
}
