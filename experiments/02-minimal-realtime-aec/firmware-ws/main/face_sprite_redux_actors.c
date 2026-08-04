#include "face_sprite_redux_actors.h"

#include <string.h>

#include "face_pose.h"
#include "face_stage.h"

#define SR_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xf8U) << 8U) | \
                (((uint16_t)(g) & 0xfcU) << 3U) | \
                ((uint16_t)(b) >> 3U)))

enum {
    SR_SAFE = 4,
    SR_EXPRESSION_COUNT = 11,
    SR_MOUTH_CELL_WIDTH = 15,
    SR_MOUTH_CELL_HEIGHT = 9,
    SR_ICON_NONE = 0,
    SR_ICON_HEART,
    SR_ICON_LAUGH,
    SR_ICON_SWEAT,
    SR_ICON_BANG,
    SR_ICON_THOUGHT,
    SR_ICON_QUESTION,
    SR_ICON_FOCUS,
    SR_ICON_SLEEP,
    SR_ICON_SPARK,
    SR_ICON_BLUSH,
};

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t mouth_kind;
    uint8_t palette_size;
    uint8_t ops;
    int8_t eye_x[2];
    int8_t eye_y;
    int8_t eye_w[2];
    int8_t eye_h;
    int8_t mouth_y;
} sr_actor_def_t;

typedef struct {
    uint8_t eye_open[2];
    int8_t eye_width;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t brow_outer[2];
    int8_t brow_inner[2];
    uint8_t mouth_open;
    uint8_t mouth_width;
    uint8_t mouth_round;
    int8_t mouth_corner[2];
    int8_t body_lean_x;
    int8_t body_lean_y;
    uint8_t cheek;
    uint8_t icon;
    int8_t silhouette_lift[2];
    int8_t silhouette_tilt[2];
} sr_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
} sr_mouth_shape_t;

typedef struct {
    uint16_t *pixels;
} sr_canvas_t;

static const sr_actor_def_t SR_ACTORS[FACE_SPRITE_REDUX_ACTOR_COUNT] = {
    [FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC] = {
        "sprite-redux-talkie-moon-mechanic",
        "Sprite Redux · Talkie Moon Mechanic",
        58U,
        FACE_SPRITE_REDUX_MOUTH_EGA_CELS,
        24U,
        7U,
        {28, 52},
        26,
        {14, 13},
        9,
        43,
    },
    [FACE_SPRITE_REDUX_JRPG_STORM_FAMILIAR] = {
        "sprite-redux-jrpg-storm-familiar",
        "Sprite Redux · JRPG Storm Familiar",
        59U,
        FACE_SPRITE_REDUX_MOUTH_ARCADE_CELS,
        24U,
        7U,
        {27, 53},
        27,
        {15, 15},
        10,
        43,
    },
    [FACE_SPRITE_REDUX_HANDHELD_FOREST_PET] = {
        "sprite-redux-handheld-forest-pet",
        "Sprite Redux · Handheld Forest Pet",
        60U,
        FACE_SPRITE_REDUX_MOUTH_EGA_CELS,
        4U,
        5U,
        {28, 52},
        27,
        {14, 14},
        9,
        42,
    },
};

/*
 * Authored targets are intentionally bold enough to remain readable when a
 * native 160x120 frame is reduced to a 40x30 comparison tile.
 */
static const sr_expression_t SR_EXPRESSIONS[SR_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        {212U, 212U}, 0, 0, 0, {0, 0}, {0, 0},
        16U, 142U, 18U, {0, 0}, 0, 0, 12U, SR_ICON_NONE,
        {0, 0}, {0, 0},
    },
    [FACE_EXPRESSION_WARM] = {
        {184U, 184U}, 2, 0, 1, {-1, -1}, {-2, -2},
        30U, 190U, 10U, {4, 4}, 0, 0, 94U, SR_ICON_HEART,
        {-1, -1}, {1, -1},
    },
    [FACE_EXPRESSION_JOY] = {
        {62U, 62U}, 4, 0, 1, {-3, -3}, {-4, -4},
        152U, 232U, 6U, {7, 7}, 0, -1, 226U, SR_ICON_LAUGH,
        {-3, -3}, {3, -3},
    },
    [FACE_EXPRESSION_CONCERN] = {
        {190U, 222U}, -1, -2, 2, {2, 2}, {-5, -5},
        26U, 138U, 20U, {-4, -4}, -1, 1, 72U, SR_ICON_SWEAT,
        {2, -1}, {-3, 2},
    },
    [FACE_EXPRESSION_SURPRISE] = {
        {255U, 255U}, 4, 0, -1, {-6, -6}, {-6, -6},
        238U, 74U, 252U, {0, 0}, 0, -1, 8U, SR_ICON_BANG,
        {-4, -4}, {-2, 2},
    },
    [FACE_EXPRESSION_THOUGHTFUL] = {
        {190U, 96U}, 0, -4, -3, {-3, 2}, {-1, 1},
        24U, 126U, 58U, {-3, 2}, 1, 0, 34U, SR_ICON_THOUGHT,
        {1, -2}, {-4, -1},
    },
    [FACE_EXPRESSION_SKEPTICAL] = {
        {86U, 224U}, 2, 4, 0, {-5, 3}, {1, -4},
        18U, 184U, 10U, {-5, 4}, -1, 0, 32U, SR_ICON_QUESTION,
        {3, -2}, {-5, 3},
    },
    [FACE_EXPRESSION_DETERMINED] = {
        {108U, 108U}, 4, 0, 1, {2, 2}, {5, 5},
        14U, 210U, 4U, {-3, -3}, 0, 1, 24U, SR_ICON_FOCUS,
        {1, 1}, {5, -5},
    },
    [FACE_EXPRESSION_SLEEPY] = {
        {38U, 30U}, -2, -2, 3, {3, 3}, {2, 2},
        64U, 108U, 170U, {1, 1}, 1, 2, 18U, SR_ICON_SLEEP,
        {3, 3}, {-4, 4},
    },
    [FACE_EXPRESSION_EXCITED] = {
        {255U, 255U}, 5, 0, -2, {-7, -7}, {-6, -6},
        226U, 242U, 20U, {7, 7}, 0, -2, 182U, SR_ICON_SPARK,
        {-5, -5}, {4, -4},
    },
    [FACE_EXPRESSION_EMBARRASSED] = {
        {102U, 168U}, -1, 4, 3, {-2, 0}, {-3, -1},
        24U, 148U, 24U, {5, 0}, 1, 1, 255U, SR_ICON_BLUSH,
        {2, 0}, {-3, 2},
    },
};

static const sr_mouth_shape_t SR_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {232U, 178U, 18U, 0U, 58U, 72U},
    [FACE_VISEME_E] = {92U, 246U, 3U, 0U, 226U, 8U},
    [FACE_VISEME_I] = {50U, 226U, 2U, 0U, 148U, 4U},
    [FACE_VISEME_O] = {214U, 88U, 250U, 0U, 24U, 20U},
    [FACE_VISEME_U] = {126U, 64U, 255U, 0U, 10U, 26U},
    [FACE_VISEME_PP] = {2U, 164U, 12U, 255U, 0U, 0U},
    [FACE_VISEME_SS] = {42U, 242U, 2U, 18U, 255U, 0U},
    [FACE_VISEME_TH] = {82U, 194U, 9U, 0U, 112U, 255U},
    [FACE_VISEME_DD] = {76U, 182U, 7U, 0U, 224U, 78U},
    [FACE_VISEME_FF] = {30U, 208U, 3U, 82U, 255U, 0U},
    [FACE_VISEME_KK] = {144U, 174U, 26U, 0U, 42U, 96U},
    [FACE_VISEME_NN] = {46U, 168U, 10U, 14U, 118U, 58U},
    [FACE_VISEME_RR] = {114U, 140U, 134U, 0U, 46U, 40U},
    [FACE_VISEME_CH] = {92U, 208U, 20U, 8U, 170U, 20U},
    [FACE_VISEME_SIL] = {4U, 136U, 16U, 236U, 0U, 0U},
};

/*
 * A real, palette-indexed 15-cell mouth sheet. These are source cels, not
 * post-process marks: the blitter below crops transparent pixels and maps the
 * same authored indices into each actor's own palette. L=lip/outline,
 * C=cavity, T=teeth, U=tongue and H=highlight.
 */
static const char
SR_MOUTH_SHEET[FACE_VISEME_COUNT][SR_MOUTH_CELL_HEIGHT]
              [SR_MOUTH_CELL_WIDTH + 1] = {
    [FACE_VISEME_AA] = {
        ".....LLLLL.....",
        "...LLCCCCCLL...",
        "..LCCCTTTCCCL..",
        ".LCCCCCCCCCCCL.",
        ".LCCCCCCCCCCCL.",
        ".LCCCCUCCCCCCL.",
        "..LCCUUUCCCCL..",
        "...LLCCCCCLL...",
        ".....LLLLL.....",
    },
    [FACE_VISEME_E] = {
        "...............",
        "..LLLLLLLLLLL..",
        ".LTTTTTTTTTTTL.",
        ".LTTTTTTTTTTTL.",
        ".LCCCCCCCCCCCL.",
        "..LCCCCCCCCCL..",
        "...LLUUUUULL...",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_I] = {
        "...............",
        "...............",
        "...LLLLLLLLL...",
        "..LTTTTTTTTTL..",
        "..LCCCCCCCCCL..",
        "...LLLLLLLLL...",
        "...............",
        "...............",
        "...............",
    },
    [FACE_VISEME_O] = {
        ".....LLLLL.....",
        "...LLCCCCCLL...",
        "..LCCCCCCCCCL..",
        ".LCCCCCCCCCCCL.",
        ".LCCCCCCCCCCCL.",
        ".LCCCCCCCCCCCL.",
        "..LCCCCCCCCCL..",
        "...LLCCCCCLL...",
        ".....LLLLL.....",
    },
    [FACE_VISEME_U] = {
        "...............",
        ".....LLLLL.....",
        "....LCCCCCL....",
        "...LCCCCCCCL...",
        "...LCCCCCCCL...",
        "...LCCUUUCCL...",
        "....LCCCCCL....",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_PP] = {
        "...............",
        "...............",
        "...............",
        "..LLLLLLLLLLL..",
        ".LLHHHHHHHHHLL.",
        "..LLLLLLLLLLL..",
        "...............",
        "...............",
        "...............",
    },
    [FACE_VISEME_SS] = {
        "...............",
        "..LLLLLLLLLLL..",
        ".LTTTTTTTTTTTL.",
        ".LTTTHTHTHTTTL.",
        ".LCCCCCCCCCCCL.",
        "..LCCCCCCCCCL..",
        "...LLLLLLLLL...",
        "...............",
        "...............",
    },
    [FACE_VISEME_TH] = {
        "...............",
        "...LLLLLLLLL...",
        "..LTTTTTTTTTL..",
        ".LCCCCCCCCCCCL.",
        ".LCCUUUUUUUCCL.",
        "..LUUUUUUUUUL..",
        "...LLUUUUULL...",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_DD] = {
        "...............",
        "...LLLLLLLLL...",
        "..LTTTTTTTTTL..",
        "..LTTHTTTHTTL..",
        ".LCCCCCCCCCCCL.",
        "..LCCUUUUUCCL..",
        "...LLCCCCCLL...",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_FF] = {
        "...............",
        "..LLLLLLLLLLL..",
        ".LTTTTTTTTTTTL.",
        ".LTTTTTTTTTTTL.",
        "..LHHLLLLLHHL..",
        "...LLUUUUULL...",
        ".....LLLLL.....",
        "...............",
        "...............",
    },
    [FACE_VISEME_KK] = {
        "...............",
        "...LLLLLLLLL...",
        "..LCCCCCCCCCL..",
        ".LCCCCCCCCCCCL.",
        ".LCCCCCCCCCCCL.",
        ".LCCCUUUUCCCCL.",
        "..LCCUUUUCCCL..",
        "...LLCCCCCLL...",
        ".....LLLLL.....",
    },
    [FACE_VISEME_NN] = {
        "...............",
        "...LLLLLLLLL...",
        "..LTTTTTTTTTL..",
        ".LTTTTTHTTTTTL.",
        ".LCCCCCCCCCCCL.",
        "..LCCUUUUUCCL..",
        "...LLUUUUULL...",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_RR] = {
        "...............",
        "....LLLLLLL....",
        "...LCCCCCCCL...",
        "..LCCCCCCCCCL..",
        "..LCCCUUUCCCL..",
        "..LCCUUUUUCCL..",
        "...LCCCCCCCL...",
        "....LLLLLLL....",
        "...............",
    },
    [FACE_VISEME_CH] = {
        "...............",
        "....LLLLLLL....",
        "...LCCCCCCCL...",
        "..LCCCCCCCCCL..",
        "..LCCCCCCCCCL..",
        "...LCCUUUCCL...",
        "....LCCCCCL....",
        ".....LLLLL.....",
        "...............",
    },
    [FACE_VISEME_SIL] = {
        "...............",
        "...............",
        "...............",
        "...LLLLLLLLL...",
        "..LLHHHHHHHLL..",
        "...LLLLLLLLL...",
        "...............",
        "...............",
        "...............",
    },
};

static const uint8_t SR_MOUTH_MIN_HEIGHT[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = 7U,
    [FACE_VISEME_E] = 5U,
    [FACE_VISEME_I] = 3U,
    [FACE_VISEME_O] = 8U,
    [FACE_VISEME_U] = 6U,
    [FACE_VISEME_PP] = 3U,
    [FACE_VISEME_SS] = 4U,
    [FACE_VISEME_TH] = 6U,
    [FACE_VISEME_DD] = 5U,
    [FACE_VISEME_FF] = 4U,
    [FACE_VISEME_KK] = 7U,
    [FACE_VISEME_NN] = 5U,
    [FACE_VISEME_RR] = 6U,
    [FACE_VISEME_CH] = 6U,
    [FACE_VISEME_SIL] = 3U,
};

static int32_t sr_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t sr_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t sr_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t sr_wave(uint32_t sample_clock, uint32_t period)
{
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    return phase < half
        ? (int32_t)(phase * 254U / half) - 127
        : 127 - (int32_t)((phase - half) * 254U / half);
}

static bool sr_actor_valid(face_sprite_redux_actor_t actor)
{
    return (unsigned)actor < (unsigned)FACE_SPRITE_REDUX_ACTOR_COUNT;
}

/*
 * Every primitive clips to a four-logical-pixel border. At exact 2x output
 * this leaves an eight-physical-pixel untouched frame around every actor,
 * or two untouched pixels after the unsmoothed 40x30 audit reduction.
 */
static void sr_put(sr_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x < SR_SAFE ||
        x >= FACE_SPRITE_REDUX_LOGICAL_WIDTH - SR_SAFE ||
        y < SR_SAFE ||
        y >= FACE_SPRITE_REDUX_LOGICAL_HEIGHT - SR_SAFE) {
        return;
    }
    const int physical_x = x * FACE_SPRITE_REDUX_SCALE;
    const int physical_y = y * FACE_SPRITE_REDUX_SCALE;
    const size_t top =
        (size_t)physical_y * FACE_SPRITE_REDUX_WIDTH +
        (size_t)physical_x;
    const size_t bottom = top + FACE_SPRITE_REDUX_WIDTH;
    canvas->pixels[top] = color;
    canvas->pixels[top + 1U] = color;
    canvas->pixels[bottom] = color;
    canvas->pixels[bottom + 1U] = color;
}

static void sr_clear(sr_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_SPRITE_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void sr_rect(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left = (int)sr_clamp(
        x, SR_SAFE, FACE_SPRITE_REDUX_LOGICAL_WIDTH - SR_SAFE);
    const int right = (int)sr_clamp(
        x + width, SR_SAFE, FACE_SPRITE_REDUX_LOGICAL_WIDTH - SR_SAFE);
    const int top = (int)sr_clamp(
        y, SR_SAFE, FACE_SPRITE_REDUX_LOGICAL_HEIGHT - SR_SAFE);
    const int bottom = (int)sr_clamp(
        y + height, SR_SAFE, FACE_SPRITE_REDUX_LOGICAL_HEIGHT - SR_SAFE);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            sr_put(canvas, px, py, color);
        }
    }
}

static void sr_frame(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int thickness,
    uint16_t color)
{
    sr_rect(canvas, x, y, width, thickness, color);
    sr_rect(canvas, x, y + height - thickness, width, thickness, color);
    sr_rect(canvas, x, y, thickness, height, color);
    sr_rect(canvas, x + width - thickness, y, thickness, height, color);
}

static void sr_line(
    sr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    const int dx = (int)sr_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -(int)sr_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        sr_put(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
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

static void sr_thick_line(
    sr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        if (sr_abs(x1 - x0) >= sr_abs(y1 - y0)) {
            sr_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
        } else {
            sr_line(canvas, x0 + offset, y0, x1 + offset, y1, color);
        }
    }
}

static void sr_ellipse(
    sr_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius_x,
    int radius_y,
    uint16_t color)
{
    if (radius_x < 1 || radius_y < 1) {
        return;
    }
    const int64_t rx2 = (int64_t)radius_x * radius_x;
    const int64_t ry2 = (int64_t)radius_y * radius_y;
    const int64_t limit = rx2 * ry2;
    for (int y = -radius_y; y <= radius_y; ++y) {
        for (int x = -radius_x; x <= radius_x; ++x) {
            const int64_t value =
                (int64_t)x * x * ry2 + (int64_t)y * y * rx2;
            if (value <= limit) {
                sr_put(canvas, center_x + x, center_y + y, color);
            }
        }
    }
}

static int32_t sr_edge(
    int ax,
    int ay,
    int bx,
    int by,
    int px,
    int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void sr_triangle(
    sr_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int left = (int)sr_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        SR_SAFE,
        FACE_SPRITE_REDUX_LOGICAL_WIDTH - SR_SAFE - 1);
    const int right = (int)sr_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        SR_SAFE,
        FACE_SPRITE_REDUX_LOGICAL_WIDTH - SR_SAFE - 1);
    const int top = (int)sr_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        SR_SAFE,
        FACE_SPRITE_REDUX_LOGICAL_HEIGHT - SR_SAFE - 1);
    const int bottom = (int)sr_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        SR_SAFE,
        FACE_SPRITE_REDUX_LOGICAL_HEIGHT - SR_SAFE - 1);
    const int32_t orientation = sr_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t ab = sr_edge(ax, ay, bx, by, x, y);
            const int32_t bc = sr_edge(bx, by, cx, cy, x, y);
            const int32_t ca = sr_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && ab >= 0 && bc >= 0 && ca >= 0) ||
                (orientation < 0 && ab <= 0 && bc <= 0 && ca <= 0)) {
                sr_put(canvas, x, y, color);
            }
        }
    }
}

static void sr_quad(
    sr_canvas_t *canvas,
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
    sr_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    sr_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void sr_checker(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t a,
    uint16_t b,
    unsigned period)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            const unsigned cell =
                (unsigned)(px + py * 3) % (period == 0U ? 1U : period);
            sr_put(canvas, px, py, cell == 0U ? b : a);
        }
    }
}

size_t face_sprite_redux_actor_count(void)
{
    return FACE_SPRITE_REDUX_ACTOR_COUNT;
}

const char *face_sprite_redux_actor_slug(
    face_sprite_redux_actor_t actor)
{
    return sr_actor_valid(actor)
        ? SR_ACTORS[actor].slug
        : "invalid-sprite-redux-actor";
}

const char *face_sprite_redux_actor_name(
    face_sprite_redux_actor_t actor)
{
    return sr_actor_valid(actor)
        ? SR_ACTORS[actor].name
        : "Invalid sprite redux actor";
}

bool face_sprite_redux_actor_info(
    face_sprite_redux_actor_t actor,
    face_sprite_redux_actor_info_t *info)
{
    if (!sr_actor_valid(actor) || info == NULL) {
        return false;
    }
    const sr_actor_def_t *definition = &SR_ACTORS[actor];
    info->slug = definition->slug;
    info->name = definition->name;
    info->legacy_profile_id = definition->legacy_id;
    info->mouth_kind = definition->mouth_kind;
    info->logical_width = FACE_SPRITE_REDUX_LOGICAL_WIDTH;
    info->logical_height = FACE_SPRITE_REDUX_LOGICAL_HEIGHT;
    info->palette_size = definition->palette_size;
    info->estimated_ops_per_pixel = definition->ops;
    return true;
}

bool face_sprite_redux_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_sprite_redux_actor_t *actor)
{
    if (actor == NULL) {
        return false;
    }
    for (size_t raw = 0U;
         raw < FACE_SPRITE_REDUX_ACTOR_COUNT;
         ++raw) {
        if (SR_ACTORS[raw].legacy_id == legacy_profile_id) {
            *actor = (face_sprite_redux_actor_t)raw;
            return true;
        }
    }
    return false;
}

static uint8_t sr_map_viseme(uint8_t set, uint8_t viseme)
{
    static const uint8_t vrm5[5] = {
        FACE_VISEME_AA,
        FACE_VISEME_I,
        FACE_VISEME_U,
        FACE_VISEME_E,
        FACE_VISEME_O,
    };
    static const uint8_t preston9[9] = {
        FACE_VISEME_AA,
        FACE_VISEME_PP,
        FACE_VISEME_E,
        FACE_VISEME_O,
        FACE_VISEME_FF,
        FACE_VISEME_TH,
        FACE_VISEME_SS,
        FACE_VISEME_RR,
        FACE_VISEME_SIL,
    };
    static const uint8_t microsoft22[22] = {
        FACE_VISEME_SIL,
        FACE_VISEME_AA,
        FACE_VISEME_AA,
        FACE_VISEME_O,
        FACE_VISEME_E,
        FACE_VISEME_RR,
        FACE_VISEME_I,
        FACE_VISEME_U,
        FACE_VISEME_O,
        FACE_VISEME_AA,
        FACE_VISEME_O,
        FACE_VISEME_AA,
        FACE_VISEME_KK,
        FACE_VISEME_RR,
        FACE_VISEME_NN,
        FACE_VISEME_SS,
        FACE_VISEME_CH,
        FACE_VISEME_TH,
        FACE_VISEME_FF,
        FACE_VISEME_DD,
        FACE_VISEME_KK,
        FACE_VISEME_PP,
    };
    if (viseme == FACE_VISEME_NONE) {
        return FACE_VISEME_SIL;
    }
    switch (set) {
        case FACE_VISEME_SET_OVR15:
            return viseme < FACE_VISEME_COUNT
                ? viseme
                : FACE_VISEME_SIL;
        case FACE_VISEME_SET_VRM5:
            return viseme < 5U
                ? vrm5[viseme]
                : FACE_VISEME_SIL;
        case FACE_VISEME_SET_PRESTON9:
            return viseme < 9U
                ? preston9[viseme]
                : FACE_VISEME_SIL;
        case FACE_VISEME_SET_MICROSOFT22:
            return viseme < 22U
                ? microsoft22[viseme]
                : FACE_VISEME_SIL;
        default:
            return (uint8_t)(viseme % FACE_VISEME_COUNT);
    }
}

static sr_mouth_shape_t sr_blended_viseme(
    const face_render_key_t *key)
{
    const uint8_t primary =
        sr_map_viseme(key->viseme_set, key->viseme);
    const uint8_t secondary =
        sr_map_viseme(key->viseme_set, key->viseme_secondary);
    const sr_mouth_shape_t a = SR_VISEMES[primary];
    const sr_mouth_shape_t b = SR_VISEMES[secondary];
    const uint8_t weight = key->viseme_blend;
    const sr_mouth_shape_t result = {
        (uint8_t)sr_mix(a.open, b.open, weight),
        (uint8_t)sr_mix(a.width, b.width, weight),
        (uint8_t)sr_mix(a.round, b.round, weight),
        (uint8_t)sr_mix(a.press, b.press, weight),
        (uint8_t)sr_mix(a.teeth, b.teeth, weight),
        (uint8_t)sr_mix(a.tongue, b.tongue, weight),
    };
    return result;
}

bool face_sprite_redux_actor_resolve(
    face_sprite_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_sprite_redux_pose_t *pose)
{
    if (!sr_actor_valid(actor) || render_key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, sizeof(pose->source));
    const sr_actor_def_t *definition = &SR_ACTORS[actor];
    const uint8_t stage =
        render_key->schema_version == FACE_RENDER_KEY_SCHEMA_VERSION &&
        render_key->stage_expression < FACE_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const sr_expression_t *target = &SR_EXPRESSIONS[stage];
    const uint8_t weight = render_key->expression_weight;
    pose->stage_expression = stage;
    pose->expression_weight = weight;
    pose->activity = render_key->controls.expression;
    pose->speech_phase = render_key->speech_phase;
    pose->attention = render_key->attention;
    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->controls.expression == FACE_ACTIVITY_SPEAKING ||
        render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ACTIVE ||
        render_key->speech_phase == FACE_SPEECH_ENDING;
    pose->emotion_icon = target->icon;
    const int phase_drive = sr_clamp(
        ((int)render_key->viseme_weight * 2 +
         render_key->controls.mouth_open +
         render_key->audio_level) /
            4,
        0,
        255);
    pose->anticipation_q8 =
        render_key->speech_phase == FACE_SPEECH_STARTING
        ? (uint8_t)phase_drive
        : 0U;
    pose->settle_q8 =
        render_key->speech_phase == FACE_SPEECH_ENDING
        ? (uint8_t)(255 - phase_drive)
        : 0U;

    const int speech_wave = pose->speaking
        ? sr_wave(
            sample_clock + (uint32_t)actor * 271U,
            1066U + (uint32_t)actor * 133U) / 43
        : 0;
    pose->speech_pulse = (int16_t)sr_clamp(speech_wave, -3, 3);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int direct_open = eye == 0U
            ? render_key->controls.eye_left_open
            : render_key->controls.eye_right_open;
        const int squint = eye == 0U
            ? render_key->eye_left_squint
            : render_key->eye_right_squint;
        int open_control = sr_mix(
            direct_open, target->eye_open[eye], weight);
        /* Direct lid controls remain visible under a full stage-expression. */
        open_control += (direct_open - 128) / 4;
        open_control -= squint / 3;
        open_control +=
            ((int)render_key->affect_arousal - 128) / 10;
        if ((render_key->controls.flags &
             FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            open_control = 0;
        }
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            open_control += 24 + pose->anticipation_q8 / 4;
        } else if (render_key->speech_phase == FACE_SPEECH_ACTIVE) {
            open_control += speech_wave * (eye == 0U ? 4 : -3);
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            open_control -= 18 + pose->settle_q8 / 3;
        }
        const int eye_width = sr_clamp(
            definition->eye_w[eye] +
                sr_mix(0, target->eye_width, weight) +
                (eye == 0U
                    ? render_key->brow_outer_left / 64
                    : render_key->brow_outer_right / 64),
            actor == FACE_SPRITE_REDUX_HANDHELD_FOREST_PET
                ? 10
                : 9,
            actor == FACE_SPRITE_REDUX_JRPG_STORM_FAMILIAR
                ? 18
                : 17);
        pose->eye_x[eye] = definition->eye_x[eye];
        pose->eye_y[eye] = definition->eye_y;
        pose->eye_w[eye] = (int16_t)eye_width;
        pose->eye_h[eye] = definition->eye_h;
        pose->eye_open[eye] = (int16_t)sr_clamp(
            1 + open_control * (definition->eye_h - 1) / 255,
            1,
            definition->eye_h);

        int gaze_x =
            render_key->controls.look_x / 28 +
            sr_mix(0, target->gaze_x, weight) +
            render_key->head_yaw / 54;
        int gaze_y =
            render_key->controls.look_y / 34 +
            sr_mix(0, target->gaze_y, weight) +
            render_key->head_pitch / 64;
        if (render_key->speech_phase == FACE_SPEECH_ACTIVE) {
            gaze_x += speech_wave / 2;
            gaze_y += (speech_wave + (int)eye) % 2;
        }
        const int pupil_radius =
            render_key->attention > 180U ? 1 :
            render_key->attention < 60U ? 3 : 2;
        const int horizontal = sr_clamp(
            eye_width / 2 - pupil_radius - 2, 1, 4);
        const int vertical = sr_clamp(
            pose->eye_open[eye] / 2 - pupil_radius, 0, 3);
        pose->pupil_x[eye] = (int16_t)(
            pose->eye_x[eye] +
            sr_clamp(gaze_x, -horizontal, horizontal));
        pose->pupil_y[eye] = (int16_t)(
            pose->eye_y[eye] +
            sr_clamp(gaze_y, -vertical, vertical));
        pose->pupil_radius[eye] = (int16_t)pupil_radius;

        const int target_outer = target->brow_outer[eye];
        const int target_inner = target->brow_inner[eye];
        const int input_outer = eye == 0U
            ? render_key->brow_outer_left
            : render_key->brow_outer_right;
        const int roll =
            render_key->head_roll / 40 * (eye == 0U ? -1 : 1);
        pose->brow_outer_y[eye] = (int16_t)sr_clamp(
            definition->eye_y - definition->eye_h / 2 - 3 +
                sr_mix(0, target_outer, weight) -
                render_key->controls.brow / 34 -
                input_outer / 28 + roll +
                render_key->affect_valence / 56,
            8,
            29);
        pose->brow_inner_y[eye] = (int16_t)sr_clamp(
            definition->eye_y - definition->eye_h / 2 - 3 +
                sr_mix(0, target_inner, weight) -
                render_key->controls.brow / 34 -
                render_key->brow_inner / 28 - roll -
                render_key->affect_valence / 56,
            8,
            29);
    }

    const uint8_t primary_viseme =
        sr_map_viseme(render_key->viseme_set, render_key->viseme);
    const uint8_t secondary_viseme =
        sr_map_viseme(
            render_key->viseme_set, render_key->viseme_secondary);
    pose->viseme_index =
        render_key->viseme_blend >= 128U
        ? secondary_viseme
        : primary_viseme;
    const sr_mouth_shape_t viseme = sr_blended_viseme(render_key);
    int open = sr_mix(
        render_key->controls.mouth_open,
        viseme.open,
        render_key->viseme_weight);
    int width = sr_mix(
        render_key->controls.mouth_width,
        viseme.width,
        render_key->viseme_weight);
    pose->mouth_round = (uint8_t)sr_mix(
        render_key->controls.mouth_round,
        viseme.round,
        render_key->viseme_weight);
    pose->mouth_press = (uint8_t)sr_mix(
        render_key->controls.mouth_press,
        viseme.press,
        render_key->viseme_weight);
    pose->teeth = (uint8_t)sr_mix(
        render_key->controls.mouth_teeth,
        viseme.teeth,
        render_key->viseme_weight);
    pose->tongue = (uint8_t)sr_mix(
        render_key->tongue,
        viseme.tongue,
        render_key->viseme_weight);

    const uint8_t mouth_weight =
        pose->speaking ? (uint8_t)(weight / 3U) : weight;
    open = sr_mix(open, target->mouth_open, mouth_weight);
    width = sr_mix(width, target->mouth_width, mouth_weight);
    pose->mouth_round = (uint8_t)sr_mix(
        pose->mouth_round, target->mouth_round, mouth_weight);
    /* Keep continuous controls visible beneath a strong viseme or emotion. */
    open += ((int)render_key->controls.mouth_open - 128) / 2;
    width += ((int)render_key->controls.mouth_width - 128) / 5;
    pose->mouth_round = (uint8_t)sr_clamp(
        (int)pose->mouth_round +
            ((int)render_key->controls.mouth_round - 128) / 3,
        0,
        255);
    pose->mouth_press = (uint8_t)sr_clamp(
        (int)pose->mouth_press +
            ((int)render_key->controls.mouth_press - 128) / 3,
        0,
        255);
    pose->teeth = (uint8_t)sr_mix(
        pose->teeth, render_key->controls.mouth_teeth, 80U);
    pose->tongue = (uint8_t)sr_mix(
        pose->tongue, render_key->tongue, 96U);
    if (pose->speaking) {
        open += (int)render_key->audio_level / 8 - 16;
    }
    open -= pose->mouth_press / 5;
    if (render_key->speech_phase == FACE_SPEECH_STARTING) {
        open =
            open * (96 + pose->anticipation_q8 / 2) / 255;
        width += 2 + pose->anticipation_q8 / 80;
    } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
        open =
            open * (255 - pose->settle_q8 * 3 / 4) / 255;
        width -= 1 + pose->settle_q8 / 96;
    }
    open += speech_wave * 3;

    pose->mouth_x = 40;
    pose->mouth_y = definition->mouth_y;
    pose->mouth_w = (int16_t)sr_clamp(
        7 + width * 23 / 255 - pose->mouth_round / 38,
        7,
        actor == FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC
            ? 29
            : actor == FACE_SPRITE_REDUX_JRPG_STORM_FAMILIAR
                ? 27
                : 25);
    pose->mouth_h = (int16_t)sr_clamp(
        1 + open * 12 / 255 + pose->mouth_round / 92,
        1,
        actor == FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC
            ? 14
            : 13);
    pose->mouth_corner_y[0] = (int16_t)sr_clamp(
        -render_key->mouth_corner_left / 28 -
            render_key->affect_valence / 38 -
            sr_mix(0, target->mouth_corner[0], weight),
        -6,
        6);
    pose->mouth_corner_y[1] = (int16_t)sr_clamp(
        -render_key->mouth_corner_right / 28 -
            render_key->affect_valence / 38 -
            sr_mix(0, target->mouth_corner[1], weight),
        -6,
        6);
    pose->cheek = (uint8_t)sr_clamp(
        (int)render_key->cheek +
            sr_mix(0, target->cheek, weight),
        0,
        255);
    pose->body_lean_x = (int16_t)sr_clamp(
        render_key->body_lean_x / 34 +
            sr_mix(0, target->body_lean_x, weight),
        -4,
        4);
    pose->body_lean_y = (int16_t)sr_clamp(
        render_key->body_lean_y / 40 +
            sr_mix(0, target->body_lean_y, weight) +
            (pose->speaking
                ? ((int)render_key->audio_level - 128) / 64
                : 0) +
            (render_key->controls.expression == FACE_ACTIVITY_THINKING
                ? 1
                : render_key->controls.expression == FACE_ACTIVITY_IDLE
                    ? 2
                    : render_key->controls.expression ==
                        FACE_ACTIVITY_LISTENING
                        ? -1
                        : 0),
        -3,
        3);
    pose->head_yaw =
        (int16_t)sr_clamp(render_key->head_yaw / 38, -3, 3);
    pose->head_pitch =
        (int16_t)sr_clamp(render_key->head_pitch / 43, -2, 2);
    pose->head_roll =
        (int16_t)sr_clamp(render_key->head_roll / 35, -3, 3);
    for (size_t side = 0U; side < 2U; ++side) {
        const int direction = side == 0U ? -1 : 1;
        pose->silhouette_lift[side] = (int16_t)sr_clamp(
            sr_mix(0, target->silhouette_lift[side], weight) -
                render_key->head_pitch / 50 -
                ((int)render_key->affect_arousal - 128) / 64 -
                (render_key->speech_phase == FACE_SPEECH_STARTING
                    ? 1 + pose->anticipation_q8 / 128
                    : render_key->speech_phase == FACE_SPEECH_ENDING
                        ? -(1 + pose->settle_q8 / 160)
                        : 0),
            -6,
            5);
        pose->silhouette_tilt[side] = (int16_t)sr_clamp(
            sr_mix(0, target->silhouette_tilt[side], weight) +
                direction * render_key->head_roll / 36 +
                direction *
                    ((int)render_key->attention - 128) / 72,
            -7,
            7);
    }
    pose->phoneme_shape =
        render_key->phoneme == FACE_PHONEME_NONE
        ? 0U
        : (uint8_t)(1U + render_key->phoneme % 5U);
    if (pose->mouth_press > 196U || pose->mouth_h <= 2) {
        pose->mouth_cel = FACE_VISEME_SIL;
    } else if (pose->tongue > 172U) {
        pose->mouth_cel = FACE_VISEME_TH;
    } else if (pose->mouth_round > 188U) {
        pose->mouth_cel =
            pose->mouth_h > 7 ? FACE_VISEME_O : FACE_VISEME_U;
    } else if (pose->teeth > 156U && pose->mouth_h < 8) {
        pose->mouth_cel = FACE_VISEME_E;
    } else {
        pose->mouth_cel = FACE_VISEME_AA;
    }
    /*
     * A sprite performance cuts to authored semantic cels at strong viseme
     * weights. Continuous dimensions still shape each cel and coarticulate it.
     */
    if (render_key->viseme_weight > 128U) {
        pose->mouth_cel = pose->viseme_index;
    }
    return true;
}

static void sr_draw_brows(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    int half_width,
    int thickness,
    uint16_t color)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const bool left = eye == 0U;
        const int outer_x =
            pose->eye_x[eye] + (left ? -half_width : half_width);
        const int inner_x =
            pose->eye_x[eye] + (left ? half_width : -half_width);
        sr_thick_line(
            canvas,
            outer_x,
            pose->brow_outer_y[eye],
            inner_x,
            pose->brow_inner_y[eye],
            thickness,
            color);
    }
}

static void sr_draw_icon(
    sr_canvas_t *canvas,
    uint8_t icon,
    int x,
    int y,
    uint16_t ink,
    uint16_t bright)
{
    switch (icon) {
        case SR_ICON_HEART:
            sr_put(canvas, x, y, bright);
            sr_put(canvas, x + 2, y, bright);
            sr_rect(canvas, x - 1, y + 1, 5, 2, bright);
            sr_line(canvas, x, y + 3, x + 1, y + 4, bright);
            sr_line(canvas, x + 3, y + 3, x + 1, y + 4, bright);
            break;
        case SR_ICON_LAUGH:
            sr_line(canvas, x - 2, y, x, y - 2, bright);
            sr_line(canvas, x + 2, y - 2, x + 4, y, bright);
            sr_line(canvas, x - 1, y + 2, x + 3, y + 2, bright);
            break;
        case SR_ICON_SWEAT:
            sr_triangle(
                canvas, x + 1, y - 2, x - 1, y + 3,
                x + 3, y + 3, bright);
            break;
        case SR_ICON_BANG:
            sr_thick_line(canvas, x + 1, y - 2, x + 1, y + 3, 2, bright);
            sr_rect(canvas, x, y + 5, 3, 2, bright);
            break;
        case SR_ICON_THOUGHT:
            sr_ellipse(canvas, x + 1, y, 3, 2, bright);
            sr_put(canvas, x - 2, y + 4, bright);
            break;
        case SR_ICON_QUESTION:
            sr_line(canvas, x - 1, y - 1, x + 2, y - 1, bright);
            sr_line(canvas, x + 2, y - 1, x + 2, y + 1, bright);
            sr_line(canvas, x + 2, y + 1, x, y + 3, bright);
            sr_put(canvas, x, y + 5, bright);
            break;
        case SR_ICON_FOCUS:
            sr_line(canvas, x - 3, y, x + 4, y, bright);
            sr_line(canvas, x, y - 3, x, y + 4, bright);
            sr_put(canvas, x, y, ink);
            break;
        case SR_ICON_SLEEP:
            sr_line(canvas, x - 1, y - 1, x + 3, y - 1, bright);
            sr_line(canvas, x + 3, y - 1, x - 1, y + 3, bright);
            sr_line(canvas, x - 1, y + 3, x + 3, y + 3, bright);
            break;
        case SR_ICON_SPARK:
            sr_line(canvas, x, y - 3, x, y + 3, bright);
            sr_line(canvas, x - 3, y, x + 3, y, bright);
            sr_line(canvas, x - 2, y - 2, x + 2, y + 2, bright);
            sr_line(canvas, x + 2, y - 2, x - 2, y + 2, bright);
            break;
        case SR_ICON_BLUSH:
            sr_line(canvas, x - 2, y, x, y - 2, bright);
            sr_line(canvas, x + 1, y, x + 3, y - 2, bright);
            break;
        default:
            break;
    }
}

static void sr_draw_indexed_mouth(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue,
    uint16_t highlight)
{
    const uint8_t cel = pose->mouth_cel < FACE_VISEME_COUNT
        ? pose->mouth_cel
        : FACE_VISEME_SIL;
    int source_left = SR_MOUTH_CELL_WIDTH;
    int source_right = -1;
    int source_top = SR_MOUTH_CELL_HEIGHT;
    int source_bottom = -1;
    for (int y = 0; y < SR_MOUTH_CELL_HEIGHT; ++y) {
        for (int x = 0; x < SR_MOUTH_CELL_WIDTH; ++x) {
            const char index = SR_MOUTH_SHEET[cel][y][x];
            if (index != '.' && index != '\0') {
                source_left = x < source_left ? x : source_left;
                source_right = x > source_right ? x : source_right;
                source_top = y < source_top ? y : source_top;
                source_bottom = y > source_bottom ? y : source_bottom;
            }
        }
    }
    if (source_right < source_left || source_bottom < source_top) {
        return;
    }

    const int source_width = source_right - source_left + 1;
    const int source_height = source_bottom - source_top + 1;
    const int width = (int)sr_clamp(
        pose->mouth_w - pose->mouth_press / 32,
        7,
        29);
    const int height = (int)sr_clamp(
        pose->mouth_h > SR_MOUTH_MIN_HEIGHT[cel]
            ? pose->mouth_h
            : SR_MOUTH_MIN_HEIGHT[cel],
        2,
        14);
    const int left = pose->mouth_x - width / 2;
    const int top = pose->mouth_y - height / 2;
    for (int y = 0; y < height; ++y) {
        const int source_y =
            source_top + y * source_height / height;
        for (int x = 0; x < width; ++x) {
            const int source_x =
                source_left + x * source_width / width;
            const char index = SR_MOUTH_SHEET[cel][source_y][source_x];
            uint16_t color;
            switch (index) {
                case 'L':
                    color = lip;
                    break;
                case 'C':
                    color = cavity;
                    break;
                case 'T':
                    color = pose->teeth > 72U ? teeth : cavity;
                    break;
                case 'U':
                    color = pose->tongue > 48U ? tongue : cavity;
                    break;
                case 'H':
                    color = highlight;
                    break;
                default:
                    continue;
            }
            const int corner_offset =
                pose->mouth_corner_y[0] +
                (pose->mouth_corner_y[1] -
                 pose->mouth_corner_y[0]) *
                    x /
                    (width > 1 ? width - 1 : 1);
            sr_put(canvas, left + x, top + y + corner_offset, color);
        }
    }

    /*
     * Fine indexed accents preserve continuous IR influence without turning
     * the mouth into an amplitude bar. They stay inside the authored cel.
     */
    const int accent_left = pose->mouth_x - width / 3;
    const int accent_span = sr_clamp(width * 2 / 3, 1, 18);
    sr_put(
        canvas,
        accent_left + (int)(pose->teeth % (uint8_t)accent_span),
        top + 1 + pose->mouth_corner_y[0] / 2,
        teeth);
    sr_put(
        canvas,
        accent_left + (int)(pose->tongue % (uint8_t)accent_span),
        top + height - 2 + pose->mouth_corner_y[1] / 2,
        tongue);
    if (pose->phoneme_shape != 0U) {
        sr_put(
            canvas,
            pose->mouth_x - 3 + pose->phoneme_shape,
            pose->mouth_y,
            highlight);
    }
}

static void sr_draw_talkie_eye(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    size_t eye,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int half = pose->eye_w[eye] / 2;
    const int aperture = pose->eye_open[eye];
    const int vertical = sr_clamp(aperture / 2, 1, 4);
    if (aperture <= 1) {
        sr_thick_line(canvas, cx - half, cy, cx + half, cy, 2, outline);
        return;
    }
    sr_quad(
        canvas,
        cx - half, cy,
        cx - half / 2, cy - vertical,
        cx + half / 2, cy - vertical,
        cx + half, cy,
        outline);
    sr_quad(
        canvas,
        cx - half, cy,
        cx + half, cy,
        cx + half / 2, cy + vertical,
        cx - half / 2, cy + vertical,
        outline);
    sr_ellipse(canvas, cx, cy, half - 1, vertical - 1, sclera);
    const int px = (int)sr_clamp(
        pose->pupil_x[eye], cx - half + 2, cx + half - 2);
    const int py = (int)sr_clamp(
        pose->pupil_y[eye], cy - vertical + 1, cy + vertical - 1);
    sr_rect(
        canvas,
        px - pose->pupil_radius[eye] / 2,
        py - pose->pupil_radius[eye],
        pose->pupil_radius[eye] + 1,
        pose->pupil_radius[eye] * 2 + 1,
        iris);
    if (pose->attention > 72U) {
        sr_put(canvas, px - 1, py - 1, glint);
    }
}

static void sr_draw_talkie_mouth(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    sr_draw_indexed_mouth(
        canvas, pose, lip, cavity, teeth, tongue, teeth);
}

static void sr_draw_talkie_moon_mechanic(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose)
{
    const uint16_t bay = SR_RGB565(13, 20, 32);
    const uint16_t bay_mid = SR_RGB565(31, 47, 57);
    const uint16_t bay_light = SR_RGB565(57, 79, 79);
    const uint16_t ink = SR_RGB565(25, 18, 27);
    const uint16_t coverall = SR_RGB565(35, 82, 91);
    const uint16_t coverall_light = SR_RGB565(72, 139, 132);
    const uint16_t orange_dark = SR_RGB565(139, 58, 30);
    const uint16_t orange = SR_RGB565(238, 112, 42);
    const uint16_t amber = SR_RGB565(255, 191, 63);
    const uint16_t skin_shadow = SR_RGB565(133, 72, 57);
    const uint16_t skin = SR_RGB565(219, 137, 92);
    const uint16_t skin_light = SR_RGB565(255, 197, 126);
    const uint16_t cream = SR_RGB565(250, 235, 191);
    const uint16_t teal = SR_RGB565(62, 201, 188);
    const uint16_t lip = SR_RGB565(129, 42, 52);
    const uint16_t tongue = SR_RGB565(235, 79, 87);

    sr_clear(canvas, bay);
    /* Quiet repair-bay cues stay behind the portrait and off every edge. */
    sr_rect(canvas, 6, 8, 10, 35, bay_mid);
    sr_frame(canvas, 7, 9, 8, 11, 2, bay_light);
    sr_ellipse(canvas, 11, 14, 3, 3, teal);
    sr_thick_line(canvas, 69, 9, 69, 43, 2, bay_mid);
    sr_rect(canvas, 65, 12, 9, 3, bay_light);
    sr_rect(canvas, 67, 36, 6, 5, bay_mid);
    for (int rivet = 0; rivet < 4; ++rivet) {
        sr_put(canvas, 9 + rivet * 20, 53, bay_light);
    }

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    sr_ellipse(canvas, 40 + lean_x, 55 + lean_y, 31, 10, ink);
    sr_quad(
        canvas, 11 + lean_x, 56, 24 + lean_x, 45 + lean_y,
        56 + lean_x, 45 + lean_y, 70 + lean_x, 56,
        coverall);
    sr_quad(
        canvas, 17 + lean_x, 54, 28 + lean_x, 47 + lean_y,
        39 + lean_x, 51 + lean_y, 39 + lean_x, 56,
        coverall_light);
    sr_line(canvas, 40 + lean_x, 48 + lean_y,
        40 + lean_x, 55, amber);

    /* Broad talkie close-up: cap and goggles form the readable silhouette. */
    sr_ellipse(canvas, 40, 30, 25, 24, ink);
    sr_ellipse(canvas, 40, 31, 22, 22, skin_shadow);
    sr_quad(canvas, 19, 21, 38, 12, 40, 52, 18, 41, skin);
    sr_quad(canvas, 38, 12, 61, 20, 62, 40, 40, 52, skin_shadow);
    sr_ellipse(canvas, 17, 31, 3, 7, skin);
    sr_ellipse(canvas, 63, 31, 3, 7, skin_shadow);

    const int cap_left_y = 11 + pose->silhouette_lift[0];
    const int cap_right_y = 10 + pose->silhouette_lift[1];
    sr_quad(
        canvas,
        18 + pose->silhouette_tilt[0], cap_left_y + 5,
        27, cap_left_y - 4,
        54, cap_right_y - 3,
        63 + pose->silhouette_tilt[1], cap_right_y + 7,
        orange_dark);
    sr_quad(
        canvas, 23, cap_left_y - 1, 34, cap_left_y - 6,
        54, cap_right_y - 3, 58, cap_right_y + 2, orange);
    sr_quad(
        canvas, 15 + pose->silhouette_tilt[0], cap_left_y + 5,
        37, cap_left_y + 2, 48, cap_right_y + 5,
        25, cap_left_y + 9, amber);
    sr_rect(canvas, 31, 8, 14, 3, orange);

    /* The offset goggle rig is furniture attached to the face, never a HUD. */
    sr_frame(canvas, 20, 19, 17, 13, 2, orange_dark);
    sr_line(canvas, 37, 22, 44, 22, orange_dark);
    sr_rect(canvas, 22, 21, 13, 2, amber);
    sr_draw_talkie_eye(canvas, pose, 0U, ink, cream, teal, cream);
    sr_draw_talkie_eye(canvas, pose, 1U, ink, cream, teal, cream);
    sr_draw_brows(canvas, pose, 7, 2, ink);

    const int nose_x = 40 + pose->head_yaw;
    sr_triangle(
        canvas, nose_x + 1, 28, nose_x - 3, 37,
        nose_x + 4, 38, skin_shadow);
    sr_line(canvas, nose_x - 1, 37, nose_x + 4, 37, skin_light);
    sr_draw_talkie_mouth(canvas, pose, lip, bay, cream, tongue);

    if (pose->cheek > 54U) {
        const int width = 3 + pose->cheek / 58;
        sr_checker(canvas, 20, 36, width, 3, skin, tongue, 3U);
        sr_checker(canvas, 55, 36, width, 3, skin_shadow, tongue, 3U);
    }
    sr_frame(canvas, 63, 23, 8, 14, 2, teal);
    sr_line(canvas, 66, 36, 58, 41, bay_light);
    const int badge_y =
        52 + (pose->activity == FACE_ACTIVITY_THINKING ? -2 :
              pose->activity == FACE_ACTIVITY_IDLE ? 1 : 0);
    sr_rect(canvas, 36, badge_y, 8, 3,
        pose->speaking ? orange : coverall_light);
    sr_put(canvas, 37 + pose->phoneme_shape, badge_y + 1, ink);
    sr_draw_icon(canvas, pose->emotion_icon, 69, 9, bay, amber);
}

static void sr_draw_familiar_eye(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    size_t eye,
    uint16_t lid,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int half = pose->eye_w[eye] / 2;
    const int vertical = sr_clamp(pose->eye_open[eye] / 2, 1, 5);
    if (pose->eye_open[eye] <= 1) {
        sr_thick_line(canvas, cx - half, cy, cx + half, cy, 2, lid);
        return;
    }
    /* Perspective-correct asymmetric almond cells. */
    const int nose_bias = eye == 0U ? 1 : -1;
    sr_quad(
        canvas,
        cx - half, cy + 1,
        cx - half / 2 + nose_bias, cy - vertical,
        cx + half / 2 + nose_bias, cy - vertical,
        cx + half, cy,
        lid);
    sr_quad(
        canvas,
        cx - half, cy + 1,
        cx + half, cy,
        cx + half / 2, cy + vertical,
        cx - half / 2, cy + vertical,
        lid);
    sr_ellipse(canvas, cx, cy, half - 1, vertical - 1, sclera);
    const int px = (int)sr_clamp(
        pose->pupil_x[eye], cx - half + 2, cx + half - 2);
    const int py = (int)sr_clamp(
        pose->pupil_y[eye], cy - vertical + 1, cy + vertical - 1);
    sr_ellipse(
        canvas, px, py,
        pose->pupil_radius[eye] + (eye == 0U ? 1 : 0),
        pose->pupil_radius[eye], iris);
    sr_rect(canvas, px, py - pose->pupil_radius[eye], 1,
        pose->pupil_radius[eye] * 2 + 1, pupil);
    if (pose->attention > 66U) {
        sr_put(canvas, px - 1, py - 1, glint);
    }
}

static void sr_draw_familiar_mouth(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    uint16_t lip_dark,
    uint16_t lip_light,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    sr_draw_indexed_mouth(
        canvas, pose, lip_dark, cavity, teeth, tongue, lip_light);
}

static void sr_draw_jrpg_storm_familiar(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose)
{
    const uint16_t storm = SR_RGB565(24, 24, 62);
    const uint16_t cloud = SR_RGB565(50, 51, 94);
    const uint16_t cloud_light = SR_RGB565(78, 76, 132);
    const uint16_t ink = SR_RGB565(34, 18, 59);
    const uint16_t fur_dark = SR_RGB565(76, 50, 119);
    const uint16_t fur = SR_RGB565(151, 98, 172);
    const uint16_t fur_light = SR_RGB565(240, 181, 214);
    const uint16_t face = SR_RGB565(255, 225, 220);
    const uint16_t sclera = SR_RGB565(255, 250, 237);
    const uint16_t iris = SR_RGB565(72, 153, 225);
    const uint16_t cyan = SR_RGB565(90, 222, 236);
    const uint16_t yellow = SR_RGB565(255, 214, 76);
    const uint16_t lip = SR_RGB565(125, 43, 94);
    const uint16_t cavity = SR_RGB565(54, 22, 70);
    const uint16_t tongue = SR_RGB565(244, 105, 151);

    sr_clear(canvas, storm);
    /* Subdued storm atmosphere: no road, large bolt, or edge furniture. */
    sr_ellipse(canvas, 10, 14, 6, 3, cloud);
    sr_ellipse(canvas, 69, 17, 7, 3, cloud);
    sr_line(canvas, 8, 20, 6, 24, cloud_light);
    sr_line(canvas, 72, 23, 70, 27, cloud_light);
    sr_put(canvas, 11, 30, cyan);
    sr_put(canvas, 69, 34, cyan);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    sr_ellipse(canvas, 40 + lean_x, 54 + lean_y, 28, 9, ink);
    sr_ellipse(canvas, 40 + lean_x, 52 + lean_y, 25, 8, fur_dark);
    sr_triangle(canvas, 14 + lean_x, 54, 23 + lean_x, 45 + lean_y,
        31 + lean_x, 55, fur);
    sr_triangle(canvas, 66 + lean_x, 54, 57 + lean_x, 45 + lean_y,
        49 + lean_x, 55, fur);

    /* Emotion-authored ear tips carry silhouette acting without moving eyes. */
    const int left_tip_x = 21 + pose->silhouette_tilt[0];
    const int left_tip_y = 7 + pose->silhouette_lift[0];
    const int right_tip_x = 59 + pose->silhouette_tilt[1];
    const int right_tip_y = 7 + pose->silhouette_lift[1];
    sr_triangle(canvas, 14, 24, left_tip_x, left_tip_y, 35, 19, ink);
    sr_triangle(canvas, 66, 24, right_tip_x, right_tip_y, 45, 19, ink);
    sr_triangle(canvas, 18, 22, left_tip_x, left_tip_y + 4, 32, 19, fur);
    sr_triangle(canvas, 62, 22, right_tip_x, right_tip_y + 4, 48, 19, fur);
    sr_triangle(canvas, 21, 19, left_tip_x, left_tip_y + 6, 28, 18, cyan);
    sr_triangle(canvas, 59, 19, right_tip_x, right_tip_y + 6, 52, 18, cyan);

    sr_ellipse(canvas, 40, 31, 27, 24, ink);
    sr_ellipse(canvas, 40, 32, 24, 22, fur_dark);
    sr_ellipse(canvas, 40, 32, 21, 20, face);
    sr_triangle(canvas, 30, 14, 36, 9, 40, 17, fur_light);
    sr_triangle(canvas, 36, 13, 43, 8, 45, 18, fur);
    sr_ellipse(canvas, 17, 32, 3, 6, fur);
    sr_ellipse(canvas, 63, 32, 3, 6, fur_dark);

    sr_draw_familiar_eye(
        canvas, pose, 0U, ink, sclera, iris, ink, sclera);
    sr_draw_familiar_eye(
        canvas, pose, 1U, ink, sclera, iris, ink, sclera);
    sr_draw_brows(canvas, pose, 8, 2, ink);
    const int nose_x = 40 + pose->head_yaw;
    sr_triangle(canvas, nose_x, 34, nose_x - 2, 37,
        nose_x + 2, 37, lip);
    sr_line(canvas, nose_x, 37, nose_x, 39, fur_dark);
    sr_ellipse(canvas, 32, 40, 6, 4, fur_light);
    sr_ellipse(canvas, 48, 40, 6, 4, fur_light);
    sr_draw_familiar_mouth(
        canvas, pose, lip, tongue, cavity, sclera, tongue);

    if (pose->cheek > 52U) {
        const int width = 2 + pose->cheek / 60;
        sr_checker(canvas, 20, 36, width, 3, face, tongue, 3U);
        sr_checker(canvas, 56, 36, width, 3, face, tongue, 3U);
    }
    const int charm_y =
        52 + (pose->activity == FACE_ACTIVITY_THINKING ? -2 :
              pose->activity == FACE_ACTIVITY_IDLE ? 1 : 0);
    sr_triangle(canvas, 37, charm_y, 43, charm_y,
        40, charm_y + 4, yellow);
    sr_put(canvas, 40, charm_y + 1,
        pose->speaking ? cyan : fur_dark);
    sr_draw_icon(canvas, pose->emotion_icon, 69, 9, storm, yellow);
}

static void sr_draw_handheld_eye(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    size_t eye,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int half = pose->eye_w[eye] / 2;
    const int vertical = sr_clamp(pose->eye_open[eye] / 2, 1, 4);
    if (pose->eye_open[eye] <= 1) {
        sr_thick_line(canvas, cx - half, cy, cx + half, cy, 2, outline);
        return;
    }
    /* Chunky LCD sockets deliberately survive the exact 40x30 audit. */
    sr_rect(
        canvas, cx - half - 1, cy - vertical - 1,
        half * 2 + 3, vertical * 2 + 3, outline);
    sr_rect(
        canvas, cx - half + 1, cy - vertical,
        half * 2 - 1, vertical * 2 + 1, sclera);
    const int px = (int)sr_clamp(
        pose->pupil_x[eye], cx - half + 2, cx + half - 2);
    const int py = (int)sr_clamp(
        pose->pupil_y[eye], cy - vertical + 1, cy + vertical - 1);
    sr_rect(canvas, px - 2, py - 2, 5, 5, iris);
    sr_rect(canvas, px, py - 2, 2, 5, pupil);
    if (pose->attention > 54U) {
        sr_put(canvas, px - 1, py - 1, glint);
    }
}

static void sr_draw_handheld_mouth(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose,
    uint16_t outline,
    uint16_t upper,
    uint16_t lower,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    sr_draw_indexed_mouth(
        canvas, pose, outline, cavity, teeth, tongue,
        pose->mouth_press > 160U ? upper : lower);
}

static void sr_draw_handheld_forest_pet(
    sr_canvas_t *canvas,
    const face_sprite_redux_pose_t *pose)
{
    /* Four high-contrast LCD tones, deliberately not green-on-green mush. */
    const uint16_t paper = SR_RGB565(226, 232, 177);
    const uint16_t light = SR_RGB565(190, 207, 139);
    const uint16_t mid = SR_RGB565(104, 128, 83);
    const uint16_t dark = SR_RGB565(35, 49, 36);

    sr_clear(canvas, paper);
    /* Sparse forest cues are inset and quieter than the face. */
    sr_triangle(canvas, 7, 42, 11, 29, 16, 42, light);
    sr_rect(canvas, 10, 41, 3, 10, mid);
    sr_triangle(canvas, 64, 43, 69, 28, 74, 43, light);
    sr_rect(canvas, 68, 42, 3, 9, mid);
    for (int x = 8; x < 74; x += 8) {
        sr_put(canvas, x, 54 - (x / 8) % 2, mid);
    }

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    sr_rect(canvas, 15 + lean_x, 49 + lean_y, 50, 7, dark);
    sr_rect(canvas, 19 + lean_x, 48 + lean_y, 42, 7, mid);
    sr_rect(canvas, 27 + lean_x, 50 + lean_y, 26, 6, light);

    const int left_tip_x = 22 + pose->silhouette_tilt[0];
    const int left_tip_y = 8 + pose->silhouette_lift[0];
    const int right_tip_x = 58 + pose->silhouette_tilt[1];
    const int right_tip_y = 8 + pose->silhouette_lift[1];
    sr_triangle(canvas, 16, 25, left_tip_x, left_tip_y, 35, 20, dark);
    sr_triangle(canvas, 64, 25, right_tip_x, right_tip_y, 45, 20, dark);
    sr_triangle(canvas, 20, 22, left_tip_x, left_tip_y + 4, 32, 20, mid);
    sr_triangle(canvas, 60, 22, right_tip_x, right_tip_y + 4, 48, 20, mid);
    /* Leaf-notched ear interiors distinguish the pet from the familiar. */
    sr_line(canvas, left_tip_x, left_tip_y + 4, 27, 19, light);
    sr_line(canvas, right_tip_x, right_tip_y + 4, 53, 19, light);

    sr_ellipse(canvas, 40, 32, 25, 23, dark);
    sr_rect(canvas, 18, 25, 45, 18, mid);
    sr_ellipse(canvas, 40, 31, 21, 20, light);
    sr_rect(canvas, 23, 18, 34, 5, paper);
    sr_rect(canvas, 18, 29, 5, 9, mid);
    sr_rect(canvas, 58, 29, 5, 9, mid);
    /* Pixel forehead leaf is part of the silhouette and follows pitch. */
    sr_triangle(canvas, 35, 17 + pose->head_pitch,
        40, 10 + pose->silhouette_lift[0],
        42, 19 + pose->head_pitch, mid);
    sr_triangle(canvas, 40, 10 + pose->silhouette_lift[1],
        48, 15 + pose->head_pitch,
        42, 20 + pose->head_pitch, dark);

    sr_draw_handheld_eye(
        canvas, pose, 0U, dark, paper, mid, dark, paper);
    sr_draw_handheld_eye(
        canvas, pose, 1U, dark, paper, mid, dark, paper);
    sr_draw_brows(canvas, pose, 7, 2, dark);
    const int nose_x = 40 + pose->head_yaw;
    sr_rect(canvas, nose_x - 2, 34, 5, 3, dark);
    sr_put(canvas, nose_x - 1, 34, paper);
    sr_ellipse(canvas, 32, 39, 6, 4, paper);
    sr_ellipse(canvas, 48, 39, 6, 4, paper);
    sr_draw_handheld_mouth(
        canvas, pose, dark, mid, light, mid, paper, light);

    if (pose->cheek > 46U) {
        const int width = 2 + pose->cheek / 64;
        sr_checker(canvas, 20, 37, width, 3, light, dark, 3U);
        sr_checker(canvas, 56, 37, width, 3, light, dark, 3U);
    }
    const int seed_y =
        51 + (pose->activity == FACE_ACTIVITY_THINKING ? -2 :
              pose->activity == FACE_ACTIVITY_IDLE ? 1 : 0);
    sr_rect(canvas, 37, seed_y, 7, 4,
        pose->speaking ? dark : mid);
    sr_put(canvas, 38 + pose->phoneme_shape, seed_y + 1, paper);
    sr_draw_icon(canvas, pose->emotion_icon, 69, 9, paper, dark);
}

bool face_sprite_redux_actor_render(
    face_sprite_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!sr_actor_valid(actor) || render_key == NULL ||
        rgb565 == NULL ||
        pixel_capacity < FACE_SPRITE_REDUX_PIXEL_COUNT) {
        return false;
    }
    face_sprite_redux_pose_t pose;
    if (!face_sprite_redux_actor_resolve(
            actor, render_key, sample_clock, &pose)) {
        return false;
    }
    sr_canvas_t canvas = {rgb565};
    switch (actor) {
        case FACE_SPRITE_REDUX_TALKIE_MOON_MECHANIC:
            sr_draw_talkie_moon_mechanic(&canvas, &pose);
            break;
        case FACE_SPRITE_REDUX_JRPG_STORM_FAMILIAR:
            sr_draw_jrpg_storm_familiar(&canvas, &pose);
            break;
        case FACE_SPRITE_REDUX_HANDHELD_FOREST_PET:
            sr_draw_handheld_forest_pet(&canvas, &pose);
            break;
        default:
            return false;
    }
    return true;
}

bool face_sprite_redux_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_sprite_redux_actor_t actor;
    return face_sprite_redux_actor_from_legacy_id(
               legacy_profile_id, &actor) &&
        face_sprite_redux_actor_render(
            actor,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
