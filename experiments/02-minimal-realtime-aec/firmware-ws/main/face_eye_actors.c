#include "face_eye_actors.h"

#include "face_pose.h"
#include "face_stage.h"

#include <limits.h>
#include <string.h>

#define RGB565(r, g, b)                                                   \
    ((uint16_t)((((uint16_t)(r) & 0xf8U) << 8U) |                         \
                (((uint16_t)(g) & 0xfcU) << 3U) |                         \
                ((uint16_t)(b) >> 3U)))

typedef struct {
    const char *slug;
    const char *name;
    uint8_t mouth_kind;
    bool monocular;
    uint8_t ops;
    uint8_t eye_w;
    uint8_t eye_h;
    uint8_t pupil;
    uint16_t background;
    uint16_t primary;
    uint16_t secondary;
    uint16_t accent;
    uint16_t dark;
    uint16_t blush;
} actor_def_t;

typedef struct {
    int16_t open_left;
    int16_t open_right;
    int16_t eye_size;
    int16_t brow_raise;
    int16_t brow_slope_left;
    int16_t brow_slope_right;
    int16_t mouth_mood;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t cheek;
} expression_def_t;

typedef struct {
    int8_t scale_x;
    int8_t scale_y;
    int8_t translate_x[2];
    int8_t translate_y[2];
    int8_t upper_cover[2];
    int8_t upper_angle[2];
    int8_t upper_bend[2];
    int8_t lower_cover[2];
    int8_t lower_angle[2];
    int8_t lower_bend[2];
    int8_t pupil;
    int8_t corner;
} anki_expression_def_t;

typedef struct {
    int16_t open;
    int16_t width;
    int16_t round;
    int16_t press;
    int16_t teeth;
} mouth_shape_t;

typedef struct {
    uint16_t *pixels;
} canvas_t;

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/*
 * pycozmo's reference canvas is 128x64 with 28x40 procedural eyes.  Keep
 * the Anki actors characteristically tall when adapting that construction
 * to this renderer's 160x120 canvas; the shared pose contract caps the base
 * height at 54 pixels.
 */
static const actor_def_t ACTORS[FACE_EYE_ACTOR_COUNT] = {
    [FACE_EYE_ACTOR_VECTOR_FELT] = {
        "vector-felt", "Vector felt eyes", FACE_EYE_ACTOR_MOUTH_NONE,
        false, 13, 34, 52, 6,
        RGB565(5, 11, 20), RGB565(75, 244, 255), RGB565(17, 84, 104),
        RGB565(225, 255, 255), RGB565(2, 16, 24), RGB565(255, 92, 118),
    },
    [FACE_EYE_ACTOR_COZMO_TILES] = {
        "cozmo-tiles", "Cozmo expression tiles", FACE_EYE_ACTOR_MOUTH_NONE,
        false, 13, 34, 54, 5,
        RGB565(8, 15, 24), RGB565(53, 201, 255), RGB565(19, 73, 108),
        RGB565(220, 250, 255), RGB565(2, 8, 14), RGB565(53, 201, 255),
    },
    [FACE_EYE_ACTOR_ROBO_WEDGE] = {
        "robo-wedge", "RoboEyes angular wedges", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 7, 42, 29, 6,
        RGB565(12, 9, 6), RGB565(255, 189, 40), RGB565(128, 58, 8),
        RGB565(255, 244, 184), RGB565(20, 10, 2), RGB565(255, 74, 32),
    },
    [FACE_EYE_ACTOR_ROBO_PEBBLE] = {
        "robo-pebble", "RoboEyes soft pebbles", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 7, 36, 34, 6,
        RGB565(8, 23, 25), RGB565(207, 255, 220), RGB565(57, 132, 117),
        RGB565(255, 255, 235), RGB565(3, 19, 20), RGB565(255, 114, 135),
    },
    [FACE_EYE_ACTOR_M5_INK] = {
        "m5-ink", "M5 ink avatar", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 6, 31, 28, 5,
        RGB565(246, 235, 204), RGB565(29, 39, 43), RGB565(80, 104, 99),
        RGB565(255, 250, 225), RGB565(18, 25, 27), RGB565(235, 78, 91),
    },
    [FACE_EYE_ACTOR_MANGA_SPARK] = {
        "manga-spark", "Manga sparkle actor", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 9, 35, 42, 7,
        RGB565(38, 22, 55), RGB565(252, 245, 255), RGB565(149, 105, 207),
        RGB565(119, 239, 255), RGB565(25, 11, 39), RGB565(255, 111, 172),
    },
    [FACE_EYE_ACTOR_EVE_GLOW] = {
        "eve-glow", "EVE luminous capsules", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 7, 38, 25, 4,
        RGB565(3, 8, 18), RGB565(88, 220, 255), RGB565(18, 87, 135),
        RGB565(229, 255, 255), RGB565(1, 10, 21), RGB565(101, 207, 255),
    },
    [FACE_EYE_ACTOR_JIBO_MONOCLE] = {
        "jibo-monocle", "Jibo authored monocle", FACE_EYE_ACTOR_MOUTH_LINE,
        true, 9, 50, 50, 9,
        RGB565(8, 9, 27), RGB565(122, 223, 255), RGB565(61, 75, 150),
        RGB565(255, 252, 190), RGB565(2, 4, 17), RGB565(255, 114, 177),
    },
    [FACE_EYE_ACTOR_SACCADE_SCOPE] = {
        "saccade-scope", "Saccade oscilloscope", FACE_EYE_ACTOR_MOUTH_PIXEL,
        false, 8, 40, 28, 5,
        RGB565(2, 18, 13), RGB565(67, 255, 144), RGB565(19, 91, 60),
        RGB565(210, 255, 220), RGB565(1, 9, 7), RGB565(255, 200, 65),
    },
    [FACE_EYE_ACTOR_BROW_PUPPET] = {
        "brow-puppet", "Brow dialogue puppet", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 6, 29, 23, 5,
        RGB565(28, 12, 42), RGB565(252, 188, 73), RGB565(116, 63, 133),
        RGB565(255, 247, 211), RGB565(14, 5, 23), RGB565(255, 90, 128),
    },
    [FACE_EYE_ACTOR_LID_THEATRE] = {
        "lid-theatre", "Anticipating lid theatre", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 7, 40, 33, 5,
        RGB565(17, 13, 23), RGB565(237, 225, 183), RGB565(101, 73, 121),
        RGB565(255, 171, 69), RGB565(8, 6, 12), RGB565(224, 74, 96),
    },
    [FACE_EYE_ACTOR_IRIS_DEPTH] = {
        "iris-depth", "Parallax iris depth", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 10, 36, 36, 7,
        RGB565(2, 13, 20), RGB565(216, 248, 255), RGB565(35, 147, 175),
        RGB565(70, 255, 203), RGB565(1, 7, 12), RGB565(255, 102, 104),
    },
    [FACE_EYE_ACTOR_DAWN_SLITS] = {
        "dawn-slits", "Sleep-wake dawn slits", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 6, 42, 20, 4,
        RGB565(22, 12, 35), RGB565(255, 178, 77), RGB565(124, 62, 98),
        RGB565(255, 242, 165), RGB565(10, 5, 18), RGB565(218, 90, 142),
    },
    [FACE_EYE_ACTOR_CURIOUS_PAIR] = {
        "curious-pair", "Curious mismatched pair", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 8, 35, 35, 6,
        RGB565(6, 24, 30), RGB565(172, 255, 231), RGB565(43, 128, 135),
        RGB565(255, 234, 118), RGB565(2, 14, 18), RGB565(255, 109, 103),
    },
    [FACE_EYE_ACTOR_DOT_MARQUEE] = {
        "dot-marquee", "Dot-matrix marquee", FACE_EYE_ACTOR_MOUTH_PIXEL,
        false, 5, 35, 31, 4,
        RGB565(4, 6, 7), RGB565(255, 79, 43), RGB565(92, 23, 17),
        RGB565(255, 225, 67), RGB565(1, 2, 2), RGB565(255, 68, 98),
    },
    [FACE_EYE_ACTOR_CAT_LANTERN] = {
        "cat-lantern", "Cat-eye lantern", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 9, 42, 31, 7,
        RGB565(4, 15, 11), RGB565(242, 188, 49), RGB565(84, 111, 45),
        RGB565(255, 244, 159), RGB565(2, 7, 5), RGB565(236, 87, 84),
    },
    [FACE_EYE_ACTOR_VECTOR_STAGE] = {
        "vector-stage", "Vector proscenium close-up", FACE_EYE_ACTOR_MOUTH_NONE,
        false, 14, 42, 54, 7,
        RGB565(3, 8, 14), RGB565(65, 246, 255), RGB565(16, 107, 126),
        RGB565(229, 255, 255), RGB565(1, 13, 19), RGB565(255, 106, 129),
    },
    [FACE_EYE_ACTOR_COZMO_CONSOLE] = {
        "cozmo-console", "Cozmo console close-up", FACE_EYE_ACTOR_MOUTH_NONE,
        false, 14, 40, 52, 7,
        RGB565(8, 12, 17), RGB565(40, 196, 255), RGB565(14, 86, 116),
        RGB565(237, 253, 255), RGB565(1, 8, 13), RGB565(40, 196, 255),
    },
    [FACE_EYE_ACTOR_BROW_CHORUS] = {
        "brow-chorus", "Brow chorus close-up", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 8, 42, 31, 7,
        RGB565(231, 203, 147), RGB565(39, 25, 26), RGB565(159, 74, 57),
        RGB565(255, 245, 213), RGB565(20, 12, 15), RGB565(219, 68, 76),
    },
    [FACE_EYE_ACTOR_MOON_SLEEP] = {
        "moon-sleep", "Moonlit sleep actor", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 7, 46, 27, 5,
        RGB565(4, 9, 29), RGB565(168, 210, 255), RGB565(61, 76, 145),
        RGB565(255, 233, 151), RGB565(2, 5, 20), RGB565(200, 96, 169),
    },
    [FACE_EYE_ACTOR_IRIS_BINOCULAR] = {
        "iris-binocular", "Mechanical iris binocular", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 11, 44, 44, 8,
        RGB565(18, 15, 10), RGB565(230, 221, 188), RGB565(151, 101, 38),
        RGB565(74, 241, 197), RGB565(7, 7, 5), RGB565(230, 84, 67),
    },
    [FACE_EYE_ACTOR_CAT_MECHA] = {
        "cat-mecha", "Mecha cat mask", FACE_EYE_ACTOR_MOUTH_LINE,
        false, 10, 48, 34, 8,
        RGB565(22, 5, 8), RGB565(255, 182, 36), RGB565(157, 35, 41),
        RGB565(255, 246, 166), RGB565(9, 3, 4), RGB565(255, 74, 78),
    },
    [FACE_EYE_ACTOR_MANGA_PANEL] = {
        "manga-panel", "Manga panel close-up", FACE_EYE_ACTOR_MOUTH_CAVITY,
        false, 10, 43, 47, 8,
        RGB565(247, 223, 231), RGB565(36, 24, 42), RGB565(168, 89, 143),
        RGB565(255, 252, 244), RGB565(19, 11, 24), RGB565(239, 75, 126),
    },
};

/*
 * Lid-angle relationships are informed by the MIT-licensed pycozmo
 * expression recipes (Copyright (c) 2019-2020) and Catherine Chambers'
 * Expressive Eyes lineage. This is an independent integer implementation:
 * fixed sockets and bounded apertures replace pycozmo's floating-point
 * ProceduralFace objects.
 */
static const expression_def_t EXPRESSIONS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    [FACE_EXPRESSION_WARM] = {-18, -18, 1, 18, 10, -10, 62, 0, 2, 90},
    [FACE_EXPRESSION_JOY] = {-92, -92, 3, 34, 18, -18, 112, 0, 1, 180},
    [FACE_EXPRESSION_CONCERN] = {-15, -15, 1, 37, -36, 36, -82, -5, 5, 35},
    [FACE_EXPRESSION_SURPRISE] = {62, 62, 5, 72, 0, 0, 8, 0, -3, 20},
    [FACE_EXPRESSION_THOUGHTFUL] = {-8, -54, 1, 24, -18, -31, -16, -34, -33, 12},
    [FACE_EXPRESSION_SKEPTICAL] = {-83, -12, 0, -4, 43, 18, -24, 27, 0, 4},
    [FACE_EXPRESSION_DETERMINED] = {-47, -47, 3, -35, 46, -46, -33, 0, 2, 0},
    [FACE_EXPRESSION_SLEEPY] = {-154, -154, -2, -26, 11, -11, -4, 3, 31, 0},
    [FACE_EXPRESSION_EXCITED] = {45, 45, 7, 63, 10, -10, 105, 0, -5, 150},
    [FACE_EXPRESSION_EMBARRASSED] = {-50, -60, 0, 29, -22, 22, 38, 42, 27, 255},
};

/*
 * Integer adaptations of pycozmo's MIT-licensed ProceduralFace expression
 * recipes. Covers are pixels of black upper/lower lid mask, angles are degrees,
 * and bends are the centre rise of the curved mask edge. The stage vocabulary
 * maps to the closest original recipe: warm/joy -> Happiness, concern ->
 * Sadness, skeptical -> Skepticism, determined -> Anger, sleepy -> Tiredness,
 * excited -> Surprise+Excitement, and embarrassed -> Embarrassment.
 */
static const anki_expression_def_t ANKI_EXPRESSIONS[
    FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        0, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
        {0, 0}, {0, 0}, {0, 0}, 0, 0,
    },
    [FACE_EXPRESSION_WARM] = {
        0, -8, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
        {6, 6}, {0, 0}, {5, 5}, 0, 2,
    },
    [FACE_EXPRESSION_JOY] = {
        3, -22, {0, 0}, {-1, -1}, {0, 0}, {0, 0}, {0, 0},
        {11, 11}, {0, 0}, {9, 9}, 0, 3,
    },
    [FACE_EXPRESSION_CONCERN] = {
        -4, -5, {0, 0}, {1, 1}, {8, 8}, {20, -20}, {2, 2},
        {2, 2}, {0, 0}, {0, 0}, 1, 1,
    },
    [FACE_EXPRESSION_SURPRISE] = {
        36, 42, {0, 0}, {-2, -2}, {0, 0}, {0, 0}, {0, 0},
        {0, 0}, {0, 0}, {0, 0}, -2, 2,
    },
    [FACE_EXPRESSION_THOUGHTFUL] = {
        -3, -2, {0, -1}, {-1, 1}, {4, 8}, {18, -18}, {1, 1},
        {3, 5}, {8, -8}, {2, 2}, 0, 1,
    },
    [FACE_EXPRESSION_SKEPTICAL] = {
        -6, -6, {0, 0}, {0, 1}, {11, 4}, {-10, 25}, {0, 0},
        {3, 2}, {0, 0}, {0, 0}, 0, 1,
    },
    [FACE_EXPRESSION_DETERMINED] = {
        2, -4, {0, 0}, {0, 0}, {12, 12}, {-30, 30}, {0, 0},
        {1, 1}, {0, 0}, {0, 0}, 0, 0,
    },
    [FACE_EXPRESSION_SLEEPY] = {
        -5, -14, {0, 0}, {0, 0}, {15, 15}, {5, -5}, {1, 1},
        {8, 8}, {0, 0}, {2, 2}, 1, 1,
    },
    [FACE_EXPRESSION_EXCITED] = {
        24, 28, {0, 0}, {-2, -2}, {0, 0}, {0, 0}, {0, 0},
        {7, 7}, {0, 0}, {6, 6}, -1, 3,
    },
    [FACE_EXPRESSION_EMBARRASSED] = {
        -5, -8, {4, -6}, {4, 4}, {11, 11}, {10, -10}, {2, 2},
        {3, 3}, {0, 0}, {1, 1}, 1, 1,
    },
};

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t mix_i32(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static uint32_t hash32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

static int32_t triangle_u8(uint32_t phase, uint32_t period)
{
    const uint32_t half = period / 2U;
    const uint32_t folded = phase < half ? phase : period - phase;
    return (int32_t)((folded * 255U) / (half == 0U ? 1U : half));
}

static bool style_valid(face_eye_actor_style_t style)
{
    return (unsigned int)style < (unsigned int)FACE_EYE_ACTOR_COUNT;
}

static void plot(canvas_t *canvas, int32_t x, int32_t y, uint16_t color)
{
    if (x >= 4 && x < FACE_EYE_ACTOR_WIDTH - 4 &&
        y >= 4 && y < FACE_EYE_ACTOR_HEIGHT - 4) {
        canvas->pixels[(size_t)y * FACE_EYE_ACTOR_WIDTH + (size_t)x] = color;
    }
}

static void clear_canvas(canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0; index < FACE_EYE_ACTOR_PIXEL_COUNT; ++index) {
        canvas->pixels[index] = color;
    }
}

static void fill_rect(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int32_t left = clamp_i32(x, 4, FACE_EYE_ACTOR_WIDTH - 5);
    const int32_t top = clamp_i32(y, 4, FACE_EYE_ACTOR_HEIGHT - 5);
    const int32_t right =
        clamp_i32(x + width - 1, 4, FACE_EYE_ACTOR_WIDTH - 5);
    const int32_t bottom =
        clamp_i32(y + height - 1, 4, FACE_EYE_ACTOR_HEIGHT - 5);
    for (int32_t py = top; py <= bottom; ++py) {
        for (int32_t px = left; px <= right; ++px) {
            plot(canvas, px, py, color);
        }
    }
}

static void fill_ellipse(
    canvas_t *canvas,
    int32_t cx,
    int32_t cy,
    int32_t rx,
    int32_t ry,
    uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    const int64_t rx2 = (int64_t)rx * rx;
    const int64_t ry2 = (int64_t)ry * ry;
    const int64_t limit = rx2 * ry2;
    for (int32_t y = -ry; y <= ry; ++y) {
        for (int32_t x = -rx; x <= rx; ++x) {
            if ((int64_t)x * x * ry2 + (int64_t)y * y * rx2 <= limit) {
                plot(canvas, cx + x, cy + y, color);
            }
        }
    }
}

static void ellipse_outline(
    canvas_t *canvas,
    int32_t cx,
    int32_t cy,
    int32_t rx,
    int32_t ry,
    int32_t thickness,
    uint16_t outer,
    uint16_t inner)
{
    fill_ellipse(canvas, cx, cy, rx, ry, outer);
    fill_ellipse(
        canvas,
        cx,
        cy,
        rx - thickness,
        ry - thickness,
        inner);
}

static void fill_round_rect(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t radius,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    radius = clamp_i32(radius, 0, (width < height ? width : height) / 2);
    fill_rect(canvas, x + radius, y, width - 2 * radius, height, color);
    fill_rect(canvas, x, y + radius, width, height - 2 * radius, color);
    fill_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    fill_ellipse(
        canvas, x + width - radius - 1, y + radius, radius, radius, color);
    fill_ellipse(
        canvas, x + radius, y + height - radius - 1, radius, radius, color);
    fill_ellipse(
        canvas,
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        radius,
        color);
}

static void line(
    canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    uint16_t color)
{
    int32_t dx = abs_i32(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -abs_i32(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        plot(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t twice = error * 2;
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

static void thick_line(
    canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color)
{
    const int32_t radius = thickness / 2;
    for (int32_t offset = -radius; offset <= radius; ++offset) {
        if (abs_i32(x1 - x0) >= abs_i32(y1 - y0)) {
            line(canvas, x0, y0 + offset, x1, y1 + offset, color);
        } else {
            line(canvas, x0 + offset, y0, x1 + offset, y1, color);
        }
    }
}

static void fill_polygon(
    canvas_t *canvas,
    const point_t *points,
    size_t count,
    uint16_t color)
{
    if (count < 3U || count > 8U) {
        return;
    }
    int32_t min_y = points[0].y;
    int32_t max_y = points[0].y;
    for (size_t i = 1U; i < count; ++i) {
        if (points[i].y < min_y) {
            min_y = points[i].y;
        }
        if (points[i].y > max_y) {
            max_y = points[i].y;
        }
    }
    min_y = clamp_i32(min_y, 4, FACE_EYE_ACTOR_HEIGHT - 5);
    max_y = clamp_i32(max_y, 4, FACE_EYE_ACTOR_HEIGHT - 5);
    for (int32_t y = min_y; y <= max_y; ++y) {
        int32_t intersections[8];
        size_t found = 0U;
        for (size_t i = 0U; i < count; ++i) {
            const point_t first = points[i];
            const point_t second = points[(i + 1U) % count];
            if ((first.y <= y && second.y > y) ||
                (second.y <= y && first.y > y)) {
                intersections[found++] =
                    first.x + (int32_t)((int64_t)(y - first.y) *
                        (second.x - first.x) / (second.y - first.y));
            }
        }
        for (size_t i = 1U; i < found; ++i) {
            const int32_t value = intersections[i];
            size_t j = i;
            while (j > 0U && intersections[j - 1U] > value) {
                intersections[j] = intersections[j - 1U];
                --j;
            }
            intersections[j] = value;
        }
        for (size_t i = 0U; i + 1U < found; i += 2U) {
            const int32_t left =
                clamp_i32(intersections[i], 4, FACE_EYE_ACTOR_WIDTH - 5);
            const int32_t right =
                clamp_i32(intersections[i + 1U], 4, FACE_EYE_ACTOR_WIDTH - 5);
            for (int32_t x = left; x <= right; ++x) {
                plot(canvas, x, y, color);
            }
        }
    }
}

static mouth_shape_t ovr15_shape(uint8_t viseme)
{
    static const mouth_shape_t shapes[FACE_VISEME_COUNT] = {
        [FACE_VISEME_AA] = {236, 205, 24, 0, 18},
        [FACE_VISEME_E] = {155, 246, 0, 0, 128},
        [FACE_VISEME_I] = {102, 255, 0, 0, 155},
        [FACE_VISEME_O] = {214, 112, 255, 0, 16},
        [FACE_VISEME_U] = {112, 82, 244, 0, 10},
        [FACE_VISEME_PP] = {12, 164, 18, 255, 0},
        [FACE_VISEME_SS] = {66, 224, 0, 0, 210},
        [FACE_VISEME_TH] = {88, 190, 0, 0, 235},
        [FACE_VISEME_DD] = {82, 194, 0, 0, 164},
        [FACE_VISEME_FF] = {38, 198, 0, 176, 235},
        [FACE_VISEME_KK] = {120, 181, 20, 0, 78},
        [FACE_VISEME_NN] = {72, 202, 0, 0, 142},
        [FACE_VISEME_RR] = {104, 148, 94, 0, 72},
        [FACE_VISEME_CH] = {86, 158, 46, 34, 184},
        [FACE_VISEME_SIL] = {0, 110, 30, 210, 0},
    };
    if (viseme < FACE_VISEME_COUNT) {
        return shapes[viseme];
    }
    return shapes[FACE_VISEME_SIL];
}

static mouth_shape_t vocabulary_shape(uint8_t set, uint8_t viseme)
{
    if (set == FACE_VISEME_SET_VRM5) {
        static const uint8_t vrm_to_ovr[5] = {
            FACE_VISEME_AA, FACE_VISEME_I, FACE_VISEME_U,
            FACE_VISEME_E, FACE_VISEME_O,
        };
        return ovr15_shape(vrm_to_ovr[viseme % 5U]);
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        static const uint8_t preston_to_ovr[9] = {
            FACE_VISEME_SIL, FACE_VISEME_PP, FACE_VISEME_SS,
            FACE_VISEME_E, FACE_VISEME_AA, FACE_VISEME_O,
            FACE_VISEME_U, FACE_VISEME_FF, FACE_VISEME_TH,
        };
        return ovr15_shape(preston_to_ovr[viseme % 9U]);
    }
    if (set == FACE_VISEME_SET_MICROSOFT22) {
        static const uint8_t microsoft_to_ovr[22] = {
            FACE_VISEME_SIL, FACE_VISEME_AA, FACE_VISEME_AA,
            FACE_VISEME_O, FACE_VISEME_E, FACE_VISEME_RR,
            FACE_VISEME_E, FACE_VISEME_U, FACE_VISEME_O,
            FACE_VISEME_AA, FACE_VISEME_O, FACE_VISEME_AA,
            FACE_VISEME_KK, FACE_VISEME_RR, FACE_VISEME_NN,
            FACE_VISEME_SS, FACE_VISEME_CH, FACE_VISEME_TH,
            FACE_VISEME_FF, FACE_VISEME_DD, FACE_VISEME_KK,
            FACE_VISEME_PP,
        };
        return ovr15_shape(microsoft_to_ovr[viseme % 22U]);
    }
    return ovr15_shape(viseme);
}

static mouth_shape_t resolved_mouth_shape(const face_render_key_t *key)
{
    mouth_shape_t result = {
        key->controls.mouth_open,
        key->controls.mouth_width,
        key->controls.mouth_round,
        key->controls.mouth_press,
        key->controls.mouth_teeth,
    };
    mouth_shape_t primary = vocabulary_shape(key->viseme_set, key->viseme);
    if (key->viseme_secondary != FACE_VISEME_NONE) {
        const mouth_shape_t secondary =
            vocabulary_shape(key->viseme_set, key->viseme_secondary);
        primary.open = mix_i32(primary.open, secondary.open, key->viseme_blend);
        primary.width =
            mix_i32(primary.width, secondary.width, key->viseme_blend);
        primary.round =
            mix_i32(primary.round, secondary.round, key->viseme_blend);
        primary.press =
            mix_i32(primary.press, secondary.press, key->viseme_blend);
        primary.teeth =
            mix_i32(primary.teeth, secondary.teeth, key->viseme_blend);
    }
    result.open = mix_i32(result.open, primary.open, key->viseme_weight);
    result.width = mix_i32(result.width, primary.width, key->viseme_weight);
    result.round = mix_i32(result.round, primary.round, key->viseme_weight);
    result.press = mix_i32(result.press, primary.press, key->viseme_weight);
    result.teeth = mix_i32(result.teeth, primary.teeth, key->viseme_weight);
    return result;
}

static bool is_anki_eye_actor(face_eye_actor_style_t style)
{
    return style == FACE_EYE_ACTOR_VECTOR_FELT ||
        style == FACE_EYE_ACTOR_COZMO_TILES ||
        style == FACE_EYE_ACTOR_VECTOR_STAGE ||
        style == FACE_EYE_ACTOR_COZMO_CONSOLE;
}

static int32_t anki_base_corner(
    face_eye_actor_style_t style, size_t corner)
{
    static const uint8_t radii[4][4] = {
        /* upper-outer, upper-inner, lower-inner, lower-outer */
        {8, 7, 7, 8}, /* Vector */
        {4, 6, 5, 4}, /* Cozmo */
        {10, 8, 8, 10}, /* Vector close-up */
        {5, 7, 6, 5}, /* Cozmo close-up */
    };
    size_t row = 0U;
    if (style == FACE_EYE_ACTOR_COZMO_TILES) {
        row = 1U;
    } else if (style == FACE_EYE_ACTOR_VECTOR_STAGE) {
        row = 2U;
    } else if (style == FACE_EYE_ACTOR_COZMO_CONSOLE) {
        row = 3U;
    }
    return radii[row][corner];
}

static void resolve_anki_eye_rig(
    face_eye_actor_style_t style,
    const actor_def_t *actor,
    const face_render_key_t *key,
    uint8_t expression,
    uint8_t expression_weight,
    const mouth_shape_t *mouth,
    int32_t gaze_x,
    int32_t gaze_y,
    int32_t blink,
    face_eye_actor_pose_t *pose)
{
    const anki_expression_def_t *recipe = &ANKI_EXPRESSIONS[expression];
    const int32_t affect_energy =
        (int32_t)key->affect_arousal - 128;
    const int32_t positive_valence =
        key->affect_valence > 0 ? key->affect_valence : 0;
    const int32_t negative_valence =
        key->affect_valence < 0 ? -key->affect_valence : 0;
    const int32_t attention =
        (int32_t)key->attention - 128;
    const int32_t thinking_x =
        key->controls.expression == FACE_ACTIVITY_THINKING ? -2 : 0;
    const int32_t thinking_y =
        key->controls.expression == FACE_ACTIVITY_THINKING ? -1 : 0;
    const int32_t common_translate_x = clamp_i32(
        gaze_x * 6 / 127 + key->body_lean_x / 56 +
            key->head_yaw / 48 + thinking_x,
        -7,
        7);
    const int32_t common_translate_y = clamp_i32(
        gaze_y * 5 / 127 + key->body_lean_y / 64 +
            key->head_pitch / 32 + thinking_y,
        -6,
        6);
    const int32_t roll_angle = clamp_i32(key->head_roll / 40, -3, 3);
    const int32_t speech_scale =
        pose->speaking ? pose->eye_speech_pulse * 3 : 0;

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t side = eye == 0U ? 1 : -1;
        const int32_t eye_open = eye == 0U
            ? key->controls.eye_left_open
            : key->controls.eye_right_open;
        const int32_t squint = eye == 0U
            ? key->eye_left_squint
            : key->eye_right_squint;
        const int32_t brow_outer = eye == 0U
            ? key->brow_outer_left
            : key->brow_outer_right;

        /* eye_x/eye_y are immutable rig anchors; translation is separate. */
        pose->eye_x[eye] = eye == 0U ? 49 : 111;
        pose->eye_y[eye] = 59;
        pose->eye_w[eye] = actor->eye_w;
        pose->eye_h[eye] = actor->eye_h;
        pose->eye_translate_x[eye] = (int16_t)clamp_i32(
            common_translate_x +
                mix_i32(0, recipe->translate_x[eye], expression_weight) +
                side * pose->eye_speech_spacing,
            -9,
            9);
        pose->eye_translate_y[eye] = (int16_t)clamp_i32(
            common_translate_y +
                mix_i32(0, recipe->translate_y[eye], expression_weight),
            -8,
            8);

        int32_t scale_x =
            256 + mix_i32(0, recipe->scale_x, expression_weight) +
            affect_energy / 10 + attention / 18;
        int32_t scale_y =
            256 + mix_i32(0, recipe->scale_y, expression_weight) +
            affect_energy / 9 + attention / 16 + speech_scale;
        if (key->controls.expression == FACE_ACTIVITY_LISTENING) {
            scale_x += 3;
            scale_y += 5;
        } else if (key->controls.expression == FACE_ACTIVITY_THINKING) {
            scale_y -= 8;
        }
        if (pose->speaking) {
            scale_x += ((int32_t)mouth->width - 128) / 32;
            /*
             * Keep the direct control layer audible beneath a heavily
             * weighted viseme blend.  This is a small width squash only:
             * sockets and anchors remain fixed.
             */
            scale_x +=
                ((int32_t)key->controls.mouth_width - 128) / 16;
        }
        pose->eye_scale_x_q8[eye] =
            (int16_t)clamp_i32(scale_x, 210, 306);
        pose->eye_scale_y_q8[eye] =
            (int16_t)clamp_i32(scale_y, 190, 310);
        int32_t eye_angle =
            roll_angle +
            (eye == 0U
                ? key->mouth_corner_left
                : key->mouth_corner_right) / 64;
        if (pose->speaking) {
            eye_angle += side *
                (((int32_t)mouth->width - 128) / 64 +
                    mouth->round / 128);
        }
        pose->eye_angle[eye] =
            (int16_t)clamp_i32(eye_angle, -8, 8);

        for (size_t corner = 0U; corner < 4U; ++corner) {
            int32_t radius = anki_base_corner(style, corner);
            if (corner < 2U) {
                radius +=
                    mix_i32(0, recipe->corner, expression_weight);
            }
            radius += pose->speaking ? mouth->round / 128 : 0;
            if ((corner == 1U || corner == 2U) &&
                key->brow_inner > 48) {
                ++radius;
            }
            pose->eye_corner_radius[eye][corner] =
                (int16_t)clamp_i32(radius, 2, 13);
        }

        const int32_t actual_height =
            actor->eye_h * pose->eye_scale_y_q8[eye] / 256;
        int32_t upper_cover =
            (255 - eye_open) * actual_height / 255;
        upper_cover += squint * actual_height / 768;
        upper_cover += mix_i32(
            0,
            recipe->upper_cover[eye] * actual_height / 40,
            expression_weight);
        upper_cover -= key->controls.brow / 48;
        upper_cover += negative_valence / 48;
        int32_t lower_cover =
            mix_i32(
                0,
                recipe->lower_cover[eye] * actual_height / 40,
                expression_weight);
        lower_cover += positive_valence / 42;
        if (pose->speaking) {
            /*
             * Speech is an eye squash/unsquash, not a bob. Wide/open sounds
             * release the lower mask; pressed/rounded sounds lift it.
             */
            const int32_t speech_lid = clamp_i32(
                (mouth->press + mouth->round) / 192 -
                    pose->eye_speech_pulse / 2,
                -2,
                2);
            lower_cover += speech_lid;
            /*
             * Teeth is otherwise carried by a pupil-dilation cue in the
             * shared pose.  Authentic pupil-free Anki eyes express the same
             * consonant energy as a restrained lower-lid lift instead.
             */
            lower_cover += key->controls.mouth_teeth / 96;
        }
        if (key->phoneme != FACE_PHONEME_NONE) {
            lower_cover += 1 + (int32_t)(key->phoneme % 3U);
        }
        /*
         * Preserve a seven-pixel hairline at peak closure.  Driving a tall
         * solid eye all the way down to three pixels made the final close
         * step remove a conspicuous block of pixels at once, especially in
         * the close-up rig.  Cozmo's blink still reads as closed at this
         * aperture, while the extra intermediate mass keeps 30 fps motion
         * from looking like a binary sprite swap.
         */
        const int32_t minimum_aperture = 7;
        const int32_t blink_cover =
            blink * (actual_height - minimum_aperture) / 255;
        upper_cover += blink_cover * 3 / 4;
        lower_cover += blink_cover - blink_cover * 3 / 4;
        upper_cover = clamp_i32(
            upper_cover, 0, actual_height - minimum_aperture);
        lower_cover = clamp_i32(
            lower_cover,
            0,
            actual_height - minimum_aperture - upper_cover);

        int32_t upper_angle =
            mix_i32(0, recipe->upper_angle[eye], expression_weight);
        upper_angle += side * key->brow_inner / 15;
        upper_angle -= side * brow_outer / 18;
        upper_angle +=
            (eye == 0U
                ? key->mouth_corner_left
                : -key->mouth_corner_right) / 24;
        if (negative_valence > 0) {
            upper_angle += side * negative_valence / 8;
        }
        int32_t lower_angle =
            mix_i32(0, recipe->lower_angle[eye], expression_weight);
        lower_angle +=
            (eye == 0U
                ? key->mouth_corner_left
                : -key->mouth_corner_right) / 32;
        if (key->phoneme != FACE_PHONEME_NONE) {
            lower_angle +=
                ((int32_t)(key->phoneme % 5U) - 2) * side * 3;
        }
        if (pose->speaking) {
            lower_angle += side *
                ((mouth->teeth - mouth->round) / 96);
        }

        int32_t upper_bend =
            mix_i32(
                0,
                recipe->upper_bend[eye] * actual_height / 48,
                expression_weight);
        upper_bend += negative_valence / 64;
        upper_bend += key->brow_inner > 0 ? key->brow_inner / 48 : 0;
        if (pose->speaking) {
            upper_bend += mouth->press / 112;
        }
        int32_t lower_bend =
            mix_i32(
                0,
                recipe->lower_bend[eye] * actual_height / 48,
                expression_weight);
        lower_bend += positive_valence / 28;
        lower_bend += key->cheek / 72 + key->tongue / 128;
        if (pose->speaking) {
            lower_bend += mouth->round / 72 + mouth->teeth / 160;
        }
        pose->upper_lid_cover[eye] = (int16_t)upper_cover;
        pose->upper_lid_angle[eye] =
            (int16_t)clamp_i32(upper_angle, -34, 34);
        pose->upper_lid_bend[eye] =
            (int16_t)clamp_i32(upper_bend, 0, 10);
        pose->lower_lid_cover[eye] = (int16_t)lower_cover;
        pose->lower_lid_angle[eye] =
            (int16_t)clamp_i32(lower_angle, -18, 18);
        pose->lower_lid_bend[eye] =
            (int16_t)clamp_i32(lower_bend, 0, 11);

        const int32_t visible_height =
            actual_height - upper_cover - lower_cover;
        pose->eye_aperture[eye] = (int16_t)clamp_i32(
            visible_height, 4, actual_height);

        const int32_t actual_width =
            actor->eye_w * pose->eye_scale_x_q8[eye] / 256;
        int32_t pupil_radius =
            actor->pupil +
            mix_i32(0, recipe->pupil, expression_weight) +
            affect_energy / 80 + (int32_t)key->attention / 170;
        if (pose->speaking) {
            pupil_radius += mouth->teeth / 128 - mouth->round / 160;
            pupil_radius +=
                ((int32_t)key->controls.mouth_teeth - 128) / 96;
        }
        pupil_radius = clamp_i32(pupil_radius, 2, 9);
        const int32_t center_x =
            pose->eye_x[eye] + pose->eye_translate_x[eye];
        const int32_t center_y =
            pose->eye_y[eye] + pose->eye_translate_y[eye];
        const int32_t travel_x =
            clamp_i32(actual_width / 2 - pupil_radius - 3, 1, 7);
        const int32_t travel_y =
            clamp_i32(visible_height / 2 - pupil_radius - 2, 1, 5);
        int32_t pupil_x = center_x + gaze_x * travel_x / 127;
        int32_t pupil_y = center_y + gaze_y * travel_y / 127;
        if (expression == FACE_EXPRESSION_EMBARRASSED) {
            pupil_x += side * mix_i32(0, 3, expression_weight);
            pupil_y += mix_i32(0, 3, expression_weight);
        }
        /* Clamp each pupil locally, then keep stereo gaze from crossing. */
        pupil_x = clamp_i32(
            pupil_x,
            center_x - actual_width / 2 + pupil_radius + 2,
            center_x + actual_width / 2 - pupil_radius - 2);
        pupil_y = clamp_i32(
            pupil_y,
            center_y - visible_height / 2 + pupil_radius,
            center_y + visible_height / 2 - pupil_radius);
        pupil_x = eye == 0U
            ? clamp_i32(pupil_x, 24, 77)
            : clamp_i32(pupil_x, 83, 136);
        pose->pupil_x[eye] = (int16_t)pupil_x;
        pose->pupil_y[eye] = (int16_t)pupil_y;
        pose->pupil_radius[eye] = (int16_t)pupil_radius;
    }
}

size_t face_eye_actor_count(void)
{
    return FACE_EYE_ACTOR_COUNT;
}

const char *face_eye_actor_slug(face_eye_actor_style_t style)
{
    return style_valid(style) ? ACTORS[style].slug : NULL;
}

const char *face_eye_actor_name(face_eye_actor_style_t style)
{
    return style_valid(style) ? ACTORS[style].name : NULL;
}

bool face_eye_actor_info(
    face_eye_actor_style_t style, face_eye_actor_info_t *info)
{
    if (!style_valid(style) || info == NULL) {
        return false;
    }
    info->slug = ACTORS[style].slug;
    info->name = ACTORS[style].name;
    info->legacy_profile_id = (uint8_t)(
        style <= FACE_EYE_ACTOR_CAT_LANTERN
            ? FACE_EYE_ACTOR_FIRST_LEGACY_ID + (uint8_t)style
            : FACE_EYE_ACTOR_FIRST_RIG_LEGACY_ID +
                ((uint8_t)style - (uint8_t)FACE_EYE_ACTOR_VECTOR_STAGE));
    info->mouth_kind = ACTORS[style].mouth_kind;
    info->deliberate_monocular = ACTORS[style].monocular;
    info->estimated_ops_per_pixel = ACTORS[style].ops;
    return true;
}

bool face_eye_actor_from_legacy_id(
    uint8_t legacy_profile_id, face_eye_actor_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    if (legacy_profile_id >= FACE_EYE_ACTOR_FIRST_LEGACY_ID &&
        legacy_profile_id <= FACE_EYE_ACTOR_LAST_LEGACY_ID) {
        *style = (face_eye_actor_style_t)(
            legacy_profile_id - FACE_EYE_ACTOR_FIRST_LEGACY_ID);
        return true;
    }
    if (legacy_profile_id >= FACE_EYE_ACTOR_FIRST_RIG_LEGACY_ID &&
        legacy_profile_id <= FACE_EYE_ACTOR_LAST_RIG_LEGACY_ID) {
        *style = (face_eye_actor_style_t)(
            FACE_EYE_ACTOR_VECTOR_STAGE +
            legacy_profile_id - FACE_EYE_ACTOR_FIRST_RIG_LEGACY_ID);
        return true;
    }
    return false;
}

bool face_eye_actor_resolve(
    face_eye_actor_style_t style,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_eye_actor_pose_t *pose)
{
    if (!style_valid(style) || key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, key, sizeof(*key));

    const actor_def_t *actor = &ACTORS[style];
    uint8_t expression = FACE_EXPRESSION_NEUTRAL;
    if (key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION &&
        key->stage_expression < FACE_EXPRESSION_COUNT) {
        expression = key->stage_expression;
    }
    const uint8_t expression_weight = key->expression_weight;
    const expression_def_t *emotion = &EXPRESSIONS[expression];
    pose->stage_expression = expression;
    pose->expression_weight = expression_weight;
    pose->activity = key->controls.expression;
    pose->speech_phase = key->speech_phase;
    pose->speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        key->controls.expression == FACE_ACTIVITY_SPEAKING ||
        key->speech_phase == FACE_SPEECH_STARTING ||
        key->speech_phase == FACE_SPEECH_ACTIVE ||
        key->speech_phase == FACE_SPEECH_ENDING;
    pose->deliberate_monocular = actor->monocular;

    int32_t gaze_x = key->controls.look_x;
    int32_t gaze_y = key->controls.look_y;
    gaze_x += mix_i32(0, emotion->gaze_x, expression_weight);
    gaze_y += mix_i32(0, emotion->gaze_y, expression_weight);
    if (key->controls.expression == FACE_ACTIVITY_THINKING) {
        gaze_x -= 14;
        gaze_y -= 17;
    } else if (key->controls.expression == FACE_ACTIVITY_LISTENING) {
        gaze_y -= (int32_t)key->attention / 32;
    }
    const uint32_t fixation = sample_clock / 6400U;
    const uint32_t fixation_phase = sample_clock % 6400U;
    const uint32_t random_from =
        hash32(fixation + (uint32_t)style * 0x9e3779b9U);
    const uint32_t random_to =
        hash32(fixation + 1U + (uint32_t)style * 0x9e3779b9U);
    const int32_t fixation_t =
        (int32_t)(fixation_phase * 255U / 6400U);
    const uint8_t smooth_fixation = (uint8_t)(
        fixation_t * fixation_t * (765 - 2 * fixation_t) /
        (255 * 255));
    gaze_x += mix_i32(
        (int32_t)(random_from & 7U) - 3,
        (int32_t)(random_to & 7U) - 3,
        smooth_fixation);
    gaze_y += mix_i32(
        (int32_t)((random_from >> 4U) & 5U) - 2,
        (int32_t)((random_to >> 4U) & 5U) - 2,
        smooth_fixation);
    gaze_x += key->head_yaw / 6;
    gaze_y += key->head_pitch / 8;
    gaze_x = clamp_i32(gaze_x, -127, 127);
    gaze_y = clamp_i32(gaze_y, -127, 127);

    mouth_shape_t mouth = resolved_mouth_shape(key);
    int32_t shift_x = key->body_lean_x / 24 + key->head_yaw / 32;
    int32_t shift_y = key->body_lean_y / 28 + key->head_pitch / 32;
    shift_x = clamp_i32(shift_x, -6, 6);
    shift_y = clamp_i32(shift_y, -5, 5);

    int32_t speech_coupling = 0;
    int32_t speech_energy = 0;
    if (pose->speaking) {
        speech_energy =
            key->audio_level > mouth.open
                ? key->audio_level
                : mouth.open;
        if (key->speech_phase == FACE_SPEECH_STARTING) {
            /* Lids and brows lead the deliberately reduced starting jaw. */
            speech_coupling = 4;
        } else if (key->speech_phase == FACE_SPEECH_ENDING) {
            speech_coupling = 1;
        } else if (speech_energy >= 96) {
            speech_coupling =
                clamp_i32(2 + speech_energy / 86, 2, 4);
        }
    }
    if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE &&
        pose->speaking) {
        /*
         * Vector/Cozmo have no mouth: speech lives in their eyes. Keep the
         * socket immobile and drive a compact 1..5 px lid pulse instead.
         * STARTING leads the first phoneme; ENDING is the one-pixel eased
         * bridge back to rest. Active energy is deliberately compressed so a
         * hard viseme change cannot produce a large eye snap.
         */
        if (key->speech_phase == FACE_SPEECH_STARTING) {
            speech_coupling = 4;
            pose->eye_speech_spacing = 2;
            pose->eye_speech_corner = 2;
        } else if (key->speech_phase == FACE_SPEECH_ENDING) {
            speech_coupling = 1;
            pose->eye_speech_spacing = 1;
            pose->eye_speech_corner = 1;
        } else {
            speech_coupling =
                clamp_i32(1 + speech_energy / 52, 2, 5);
            pose->eye_speech_spacing = (int16_t)clamp_i32(
                (speech_coupling + 1) / 2, 1, 3);
            pose->eye_speech_corner = (int16_t)clamp_i32(
                (mouth.width - 64) / 64, 0, 3);
        }
        pose->eye_speech_pulse = (int16_t)speech_coupling;
    }

    int32_t blink = 0;
    const uint32_t blink_period =
        47000U + (hash32((uint32_t)style + 91U) % 11000U);
    const uint32_t blink_phase =
        (sample_clock + (uint32_t)style * 1877U) % blink_period;
    enum {
        /*
         * A 300 ms blink at 16 kHz: about four 30 fps frames closing and
         * another four opening.  The former 750 ms gesture made an entire
         * short utterance look asleep whenever its clock crossed the blink
         * window.  Eight to nine frames still read as authored animation,
         * without changing so much eye area in a single display refresh that
         * it looks like a dropped animation frame.
         */
        BLINK_SAMPLES = 4800U,
    };
    if (blink_phase < BLINK_SAMPLES) {
        /*
         * A blink is open at both ends of its window and closed only at the
         * midpoint.  Inverting this triangle made the lids snap shut at the
         * window boundary, reopen, close again, then snap open.  Keeping the
         * triangle upright gives a continuous close/open gesture.
         */
        blink = triangle_u8(blink_phase, BLINK_SAMPLES);
    }
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        blink = 255;
    }

    int32_t base_open[2] = {
        key->controls.eye_left_open,
        key->controls.eye_right_open,
    };
    const uint8_t squint[2] = {
        key->eye_left_squint,
        key->eye_right_squint,
    };
    const int16_t emotion_open[2] = {
        emotion->open_left,
        emotion->open_right,
    };
    const int32_t roll = clamp_i32(key->head_roll / 28, -4, 4);
    const int32_t eye_base_x[2] = {
        actor->monocular ? 80 : 49,
        actor->monocular ? 80 : 111,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        int32_t openness = base_open[eye];
        openness +=
            mix_i32(0, emotion_open[eye], expression_weight);
        openness -= squint[eye] / 2;
        openness += ((int32_t)key->affect_arousal - 128) / 7;
        openness -= blink;
        openness = clamp_i32(openness, 0, 255);

        int32_t width = actor->eye_w;
        if (style == FACE_EYE_ACTOR_CURIOUS_PAIR) {
            width += eye == 0U ? -4 : 4;
        }
        width = clamp_i32(width, 22, 54);
        int32_t height = actor->eye_h;
        if (style == FACE_EYE_ACTOR_CURIOUS_PAIR && eye == 1U) {
            height += 4;
        }
        height = clamp_i32(height, 12, 54);
        const int32_t aperture_boost =
            mix_i32(0, emotion->eye_size, expression_weight);
        const int32_t lid_coupling = speech_coupling;
        int32_t aperture = clamp_i32(
            4 + (height - 4) * openness / 255 + aperture_boost +
                lid_coupling,
            4,
            height);

        pose->eye_x[eye] =
            (int16_t)clamp_i32(eye_base_x[eye] + shift_x, 27, 133);
        pose->eye_y[eye] =
            (int16_t)clamp_i32(
                47 + shift_y + (eye == 0U ? -roll : roll), 28, 66);
        pose->eye_w[eye] = (int16_t)width;
        pose->eye_h[eye] = (int16_t)height;
        pose->eye_aperture[eye] = (int16_t)aperture;

        const int32_t travel_x = width / 2 - actor->pupil - 3;
        const int32_t travel_y = aperture / 2 - actor->pupil - 2;
        const int32_t eye_only_inset =
            actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE ? 3 : 0;
        const int32_t eye_only_shift =
            actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE
                ? (eye == 0U
                    ? -pose->eye_speech_spacing
                    : pose->eye_speech_spacing)
                : 0;
        const int32_t pupil_min_x =
            pose->eye_x[eye] - width / 2 + actor->pupil + 1 +
            eye_only_inset;
        const int32_t pupil_max_x =
            pose->eye_x[eye] + width / 2 - actor->pupil - 1 -
            eye_only_inset;
        const int32_t pupil_min_y =
            pose->eye_y[eye] - aperture / 2 + actor->pupil;
        const int32_t pupil_max_y =
            pose->eye_y[eye] + aperture / 2 - actor->pupil;
        pose->pupil_x[eye] = (int16_t)(
            pupil_min_x <= pupil_max_x
                ? clamp_i32(
                    pose->eye_x[eye] + eye_only_shift +
                        gaze_x * (travel_x > 1 ? travel_x : 1) / 127,
                    pupil_min_x,
                    pupil_max_x)
                : pose->eye_x[eye]);
        pose->pupil_y[eye] = (int16_t)(
            pupil_min_y <= pupil_max_y
                ? clamp_i32(
                    pose->eye_y[eye] +
                        gaze_y * (travel_y > 1 ? travel_y : 1) / 127,
                    pupil_min_y,
                    pupil_max_y)
                : pose->eye_y[eye]);
        int32_t dilation =
            ((int32_t)key->affect_arousal + key->attention - 256) / 96;
        if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE &&
            pose->speaking) {
            dilation += mouth.teeth / 100 - mouth.round / 128;
        }
        pose->pupil_radius[eye] =
            (int16_t)clamp_i32(actor->pupil + dilation, 2, 10);

        const int32_t brow_raise =
            key->controls.brow / 12 +
            mix_i32(0, emotion->brow_raise, expression_weight) / 12 +
            key->brow_inner / 24;
        const int32_t outer =
            eye == 0U ? key->brow_outer_left : key->brow_outer_right;
        pose->brow_y[eye] = (int16_t)clamp_i32(
            pose->eye_y[eye] - height / 2 - 5 - brow_raise -
                lid_coupling,
            12,
            54);
        const int32_t authored_slope = eye == 0U
            ? emotion->brow_slope_left
            : emotion->brow_slope_right;
        int32_t speech_slope = 0;
        if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE &&
            pose->speaking) {
            speech_slope =
                pose->eye_speech_corner * (eye == 0U ? 1 : -1);
        }
        pose->brow_slope[eye] = (int16_t)clamp_i32(
            mix_i32(0, authored_slope, expression_weight) / 5 +
                key->brow_inner / 18 - outer / 20 + speech_slope,
            -12,
            12);
    }
    if (actor->monocular) {
        pose->eye_w[1] = 0;
        pose->eye_h[1] = 0;
        pose->eye_aperture[1] = 0;
        pose->pupil_radius[1] = 0;
    }

    if (pose->speaking) {
        mouth.open += key->audio_level / 7;
    }
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        mouth.open = mouth.open * 3 / 4;
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        mouth.open = mouth.open / 2;
    } else if (key->speech_phase == FACE_SPEECH_IDLE && !pose->speaking) {
        mouth.open = mouth.open * 2 / 3;
    }
    if (key->phoneme != FACE_PHONEME_NONE) {
        mouth.width += ((int32_t)(key->phoneme % 7U) - 3) * 6;
    }
    mouth.open = clamp_i32(mouth.open - mouth.press / 3, 0, 255);
    mouth.width = clamp_i32(mouth.width, 0, 255);
    mouth.round = clamp_i32(mouth.round, 0, 255);
    const int32_t round_narrow = mouth.round * 18 / 255;
    pose->mouth_w = (int16_t)clamp_i32(
        18 + mouth.width * 42 / 255 - round_narrow, 14, 62);
    pose->mouth_h =
        (int16_t)clamp_i32(2 + mouth.open * 23 / 255, 2, 25);
    if (mouth.round > 170) {
        const int32_t rounded_max =
            pose->mouth_h + 9 > 14 ? pose->mouth_h + 9 : 14;
        pose->mouth_w =
            (int16_t)clamp_i32(pose->mouth_w, 14, rounded_max);
    }
    pose->mouth_x = (int16_t)clamp_i32(80 + shift_x / 2, 72, 88);
    pose->mouth_y = (int16_t)clamp_i32(
        88 + shift_y + key->head_pitch / 48, 78, 101);
    pose->mouth_mood = (int16_t)clamp_i32(
        mix_i32(0, emotion->mouth_mood, expression_weight) +
            key->affect_valence * 2 / 3,
        -127,
        127);
    pose->mouth_corner[0] = (int16_t)clamp_i32(
        key->mouth_corner_left + pose->mouth_mood, -127, 127);
    pose->mouth_corner[1] = (int16_t)clamp_i32(
        key->mouth_corner_right + pose->mouth_mood, -127, 127);
    pose->speech_open = (uint8_t)clamp_i32(mouth.open, 0, 255);
    pose->speech_width = (uint8_t)clamp_i32(mouth.width, 0, 255);
    pose->speech_round = (uint8_t)clamp_i32(mouth.round, 0, 255);
    pose->speech_press = (uint8_t)clamp_i32(mouth.press, 0, 255);
    pose->teeth = (uint8_t)clamp_i32(mouth.teeth, 0, 255);
    pose->tongue = key->tongue;
    pose->cheek = (uint8_t)clamp_i32(
        key->cheek +
            mix_i32(0, emotion->cheek, expression_weight),
        0,
        255);
    if (is_anki_eye_actor(style)) {
        resolve_anki_eye_rig(
            style,
            actor,
            key,
            expression,
            expression_weight,
            &mouth,
            gaze_x,
            gaze_y,
            blink,
            pose);
    }
    return true;
}

static void draw_brows(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose,
    int32_t thickness)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (pose->eye_w[eye] <= 0) {
            continue;
        }
        const int32_t half = pose->eye_w[eye] / 2 - 2;
        thick_line(
            canvas,
            pose->eye_x[eye] - half,
            pose->brow_y[eye] - pose->brow_slope[eye],
            pose->eye_x[eye] + half,
            pose->brow_y[eye] + pose->brow_slope[eye],
            thickness,
            actor->secondary);
    }
}

static void mask_socket_lids(
    canvas_t *canvas,
    const face_eye_actor_pose_t *pose,
    size_t eye,
    uint16_t mask,
    uint16_t lid)
{
    const int32_t hidden =
        (pose->eye_h[eye] - pose->eye_aperture[eye]) / 2;
    if (hidden > 0) {
        const int32_t x = pose->eye_x[eye] - pose->eye_w[eye] / 2 + 2;
        const int32_t width = pose->eye_w[eye] - 4;
        fill_rect(canvas, x,
            pose->eye_y[eye] - pose->eye_h[eye] / 2 + 2,
            width, hidden, mask);
        fill_rect(canvas, x,
            pose->eye_y[eye] + pose->eye_h[eye] / 2 - hidden - 1,
            width, hidden, mask);
    }
    thick_line(canvas,
        pose->eye_x[eye] - pose->eye_w[eye] / 2 + 2,
        pose->eye_y[eye] - pose->eye_aperture[eye] / 2,
        pose->eye_x[eye] + pose->eye_w[eye] / 2 - 2,
        pose->eye_y[eye] - pose->eye_aperture[eye] / 2,
        2,
        lid);
    thick_line(canvas,
        pose->eye_x[eye] - pose->eye_w[eye] / 2 + 2,
        pose->eye_y[eye] + pose->eye_aperture[eye] / 2,
        pose->eye_x[eye] + pose->eye_w[eye] / 2 - 2,
        pose->eye_y[eye] + pose->eye_aperture[eye] / 2,
        2,
        lid);
}

static int32_t sin_degrees_q10(int32_t angle)
{
    angle = clamp_i32(angle, -45, 45);
    const int32_t magnitude = abs_i32(angle);
    const int32_t sine =
        magnitude * 18 - magnitude * magnitude * magnitude / 1000;
    return angle < 0 ? -sine : sine;
}

static int32_t cos_degrees_q10(int32_t angle)
{
    const int32_t magnitude =
        abs_i32(clamp_i32(angle, -45, 45));
    return 1024 - magnitude * magnitude * 5 / 32;
}

static bool rounded_corner_contains(
    int32_t x,
    int32_t y,
    int32_t half_w,
    int32_t half_h,
    int32_t radius)
{
    radius = clamp_i32(radius, 1, half_w < half_h ? half_w : half_h);
    const int32_t corner_x =
        x < 0 ? -half_w + radius : half_w - radius;
    const int32_t corner_y =
        y < 0 ? -half_h + radius : half_h - radius;
    const int32_t dx = x - corner_x;
    const int32_t dy = y - corner_y;
    return dx * dx + dy * dy <= radius * radius;
}

static bool rounded_eye_contains(
    int32_t x,
    int32_t y,
    int32_t half_w,
    int32_t half_h,
    const int32_t radii[4])
{
    if (x < -half_w || x > half_w ||
        y < -half_h || y > half_h) {
        return false;
    }
    if (x < -half_w + radii[0] &&
        y < -half_h + radii[0]) {
        return rounded_corner_contains(
            x, y, half_w, half_h, radii[0]);
    }
    if (x > half_w - radii[1] &&
        y < -half_h + radii[1]) {
        return rounded_corner_contains(
            x, y, half_w, half_h, radii[1]);
    }
    if (x > half_w - radii[2] &&
        y > half_h - radii[2]) {
        return rounded_corner_contains(
            x, y, half_w, half_h, radii[2]);
    }
    if (x < -half_w + radii[3] &&
        y > half_h - radii[3]) {
        return rounded_corner_contains(
            x, y, half_w, half_h, radii[3]);
    }
    return true;
}

static bool anki_lids_show_pixel(
    const face_eye_actor_pose_t *pose,
    size_t eye,
    int32_t x,
    int32_t y,
    int32_t half_w,
    int32_t half_h)
{
    const int32_t curve = half_w > 0
        ? (half_w * half_w - x * x) /
            (half_w > 0 ? half_w : 1)
        : 0;
    int32_t upper =
        -half_h + pose->upper_lid_cover[eye] -
        x * pose->upper_lid_angle[eye] / 55 +
        curve * pose->upper_lid_bend[eye] /
            (half_w > 0 ? half_w : 1);
    int32_t lower =
        half_h - pose->lower_lid_cover[eye] -
        x * pose->lower_lid_angle[eye] / 55 -
        curve * pose->lower_lid_bend[eye] /
            (half_w > 0 ? half_w : 1);
    upper = clamp_i32(upper, -half_h, half_h - 2);
    lower = clamp_i32(lower, upper + 2, half_h);
    return y >= upper && y <= lower;
}

static void draw_anki_procedural_rig(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t width =
            pose->eye_w[eye] * pose->eye_scale_x_q8[eye] / 256;
        const int32_t height =
            pose->eye_h[eye] * pose->eye_scale_y_q8[eye] / 256;
        const int32_t half_w = clamp_i32(width / 2, 4, 29);
        const int32_t half_h = clamp_i32(height / 2, 4, 40);
        const int32_t center_x =
            pose->eye_x[eye] + pose->eye_translate_x[eye];
        const int32_t center_y =
            pose->eye_y[eye] + pose->eye_translate_y[eye];
        const int32_t sine = sin_degrees_q10(pose->eye_angle[eye]);
        const int32_t cosine = cos_degrees_q10(pose->eye_angle[eye]);
        const int32_t extent =
            (half_w > half_h ? half_w : half_h) + 8;
        int32_t radii[4];
        if (eye == 0U) {
            for (size_t corner = 0U; corner < 4U; ++corner) {
                radii[corner] = pose->eye_corner_radius[eye][corner] *
                    (pose->eye_scale_x_q8[eye] +
                        pose->eye_scale_y_q8[eye]) / 512;
            }
        } else {
            radii[0] = pose->eye_corner_radius[eye][1] *
                (pose->eye_scale_x_q8[eye] +
                    pose->eye_scale_y_q8[eye]) / 512;
            radii[1] = pose->eye_corner_radius[eye][0] *
                (pose->eye_scale_x_q8[eye] +
                    pose->eye_scale_y_q8[eye]) / 512;
            radii[2] = pose->eye_corner_radius[eye][3] *
                (pose->eye_scale_x_q8[eye] +
                    pose->eye_scale_y_q8[eye]) / 512;
            radii[3] = pose->eye_corner_radius[eye][2] *
                (pose->eye_scale_x_q8[eye] +
                    pose->eye_scale_y_q8[eye]) / 512;
        }
        for (size_t corner = 0U; corner < 4U; ++corner) {
            radii[corner] = clamp_i32(
                radii[corner], 2, half_w < half_h ? half_w : half_h);
        }

        for (int32_t screen_y = center_y - extent;
             screen_y <= center_y + extent;
             ++screen_y) {
            for (int32_t screen_x = center_x - extent;
                 screen_x <= center_x + extent;
                 ++screen_x) {
                const int32_t dx = screen_x - center_x;
                const int32_t dy = screen_y - center_y;
                const int32_t local_x =
                    (dx * cosine + dy * sine) / 1024;
                const int32_t local_y =
                    (-dx * sine + dy * cosine) / 1024;
                if (!rounded_eye_contains(
                        local_x, local_y, half_w, half_h, radii) ||
                    !anki_lids_show_pixel(
                        pose, eye, local_x, local_y, half_w, half_h)) {
                    continue;
                }
                /*
                 * Cozmo's final character design deliberately has neither
                 * brows nor pupils: expression and gaze live in the two
                 * solid procedural eye masses.  A dark disc clipped by a
                 * closing lid looks acceptable at 160x120 but becomes a
                 * destructive notch at 40x30, splitting sleepy and happy
                 * poses into disconnected fragments.  Keep the resolved
                 * pupil landmarks for the shared IR/debug contract, but do
                 * not rasterize them in the authentic Anki actors.
                 */
                plot(canvas, screen_x, screen_y, actor->primary);
            }
        }
    }
}

static void draw_cheeks(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose)
{
    if (pose->cheek < 24U) {
        return;
    }
    const int32_t radius = 2 + pose->cheek / 80;
    fill_ellipse(canvas, 30, 73, radius + 2, radius, actor->blush);
    fill_ellipse(canvas, 130, 73, radius + 2, radius, actor->blush);
}

static void draw_curve_mouth(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose,
    uint16_t color,
    int32_t thickness)
{
    const int32_t half = pose->mouth_w / 2;
    const int32_t left_y =
        pose->mouth_y - pose->mouth_corner[0] / 35;
    const int32_t right_y =
        pose->mouth_y - pose->mouth_corner[1] / 35;
    const int32_t middle_y =
        pose->mouth_y + pose->mouth_mood / 31;
    thick_line(
        canvas,
        pose->mouth_x - half,
        left_y,
        pose->mouth_x,
        middle_y,
        thickness,
        color);
    thick_line(
        canvas,
        pose->mouth_x,
        middle_y,
        pose->mouth_x + half,
        right_y,
        thickness,
        color);
    (void)actor;
}

static void draw_cavity_mouth(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose)
{
    if (pose->mouth_h <= 4) {
        draw_curve_mouth(canvas, actor, pose, actor->primary, 2);
        return;
    }
    const int32_t x = pose->mouth_x - pose->mouth_w / 2;
    const int32_t y = pose->mouth_y - pose->mouth_h / 2;
    fill_round_rect(
        canvas,
        x - 2,
        y - 2,
        pose->mouth_w + 4,
        pose->mouth_h + 4,
        5,
        actor->primary);
    fill_round_rect(
        canvas,
        x,
        y,
        pose->mouth_w,
        pose->mouth_h,
        4,
        actor->dark);
    if (pose->teeth > 75U) {
        const int32_t tooth_h =
            clamp_i32(1 + pose->teeth * pose->mouth_h / 1024, 1, 6);
        fill_round_rect(
            canvas,
            x + 3,
            y + 1,
            pose->mouth_w - 6,
            tooth_h,
            1,
            actor->accent);
    }
    if (pose->tongue > 40U && pose->mouth_h >= 8) {
        const int32_t tongue_h =
            clamp_i32(1 + pose->tongue * pose->mouth_h / 768, 2, 7);
        fill_ellipse(
            canvas,
            pose->mouth_x,
            y + pose->mouth_h - 1,
            pose->mouth_w / 3,
            tongue_h,
            actor->blush);
    }
}

static void draw_pixel_mouth(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose)
{
    const int32_t columns =
        clamp_i32(3 + (pose->mouth_w - 14) * 6 / 48, 3, 9);
    const int32_t rows =
        clamp_i32(1 + (pose->mouth_h - 2) * 4 / 23, 1, 5);
    const int32_t gap = 2;
    const int32_t dot = 3;
    const int32_t width = columns * dot + (columns - 1) * gap;
    const int32_t start_x = pose->mouth_x - width / 2;
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t column = 0; column < columns; ++column) {
            const int32_t curve =
                abs_i32(column - columns / 2) * pose->mouth_mood / 96;
            const int32_t y =
                pose->mouth_y + row * (dot + gap) - rows * 2 - curve;
            const bool hollow =
                rows >= 3 && row > 0 && row + 1 < rows &&
                column > 0 && column + 1 < columns;
            if (!hollow) {
                fill_rect(
                    canvas,
                    start_x + column * (dot + gap),
                    y,
                    dot,
                    dot,
                    actor->primary);
            }
        }
    }
}

static void draw_mouth(
    canvas_t *canvas,
    const actor_def_t *actor,
    const face_eye_actor_pose_t *pose)
{
    if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_CAVITY) {
        draw_cavity_mouth(canvas, actor, pose);
    } else if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_PIXEL) {
        draw_pixel_mouth(canvas, actor, pose);
    } else if (actor->mouth_kind == FACE_EYE_ACTOR_MOUTH_LINE) {
        if (pose->mouth_h > 6) {
            fill_ellipse(canvas, pose->mouth_x, pose->mouth_y,
                pose->mouth_w / 2, pose->mouth_h / 2, actor->primary);
            fill_ellipse(canvas, pose->mouth_x, pose->mouth_y,
                pose->mouth_w / 2 - 2, pose->mouth_h / 2 - 2, actor->dark);
            if (pose->teeth > 110U) {
                thick_line(canvas,
                    pose->mouth_x - pose->mouth_w / 3,
                    pose->mouth_y - pose->mouth_h / 4,
                    pose->mouth_x + pose->mouth_w / 3,
                    pose->mouth_y - pose->mouth_h / 4,
                    1,
                    actor->accent);
            }
            if (pose->tongue > 60U) {
                fill_ellipse(canvas, pose->mouth_x,
                    pose->mouth_y + pose->mouth_h / 3,
                    pose->mouth_w / 4, 1 + pose->tongue / 100,
                    actor->blush);
            }
        } else {
            draw_curve_mouth(canvas, actor, pose, actor->primary, 2);
        }
    }
    if (actor->mouth_kind != FACE_EYE_ACTOR_MOUTH_NONE) {
        const int32_t half = pose->mouth_w / 2;
        thick_line(canvas,
            pose->mouth_x - half,
            pose->mouth_y,
            pose->mouth_x - half - 4,
            pose->mouth_y - pose->mouth_corner[0] / 28,
            1,
            actor->primary);
        thick_line(canvas,
            pose->mouth_x + half,
            pose->mouth_y,
            pose->mouth_x + half + 4,
            pose->mouth_y - pose->mouth_corner[1] / 28,
            1,
            actor->primary);
    }
}

static void draw_vector_felt(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    draw_anki_procedural_rig(c, a, p);
}

static void draw_cozmo_tiles(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    draw_anki_procedural_rig(c, a, p);
}

static void draw_robo_wedge(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 9, 11, 142, 98, 7, RGB565(30, 20, 9));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t hw = p->eye_w[eye] / 2;
        const int32_t hh = p->eye_h[eye] / 2;
        const int32_t inner = eye == 0U ? 4 : -4;
        const point_t wedge[6] = {
            {(int16_t)(p->eye_x[eye] - hw),
                (int16_t)(p->eye_y[eye] - hh + (eye == 0U ? 6 : 0))},
            {(int16_t)(p->eye_x[eye] + hw),
                (int16_t)(p->eye_y[eye] - hh + (eye == 0U ? 0 : 6))},
            {(int16_t)(p->eye_x[eye] + hw - inner),
                (int16_t)(p->eye_y[eye] + hh - 2)},
            {(int16_t)(p->eye_x[eye] + hw - 7),
                (int16_t)(p->eye_y[eye] + hh)},
            {(int16_t)(p->eye_x[eye] - hw + 7),
                (int16_t)(p->eye_y[eye] + hh)},
            {(int16_t)(p->eye_x[eye] - hw - inner),
                (int16_t)(p->eye_y[eye] + hh - 2)},
        };
        fill_polygon(c, wedge, 6U, a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
        mask_socket_lids(c, p, eye, a->dark, a->accent);
        thick_line(c, p->eye_x[eye] - hw,
            p->eye_y[eye] - hh + (eye == 0U ? 5 : 0),
            p->eye_x[eye] + hw,
            p->eye_y[eye] - hh + (eye == 0U ? 0 : 5),
            3, a->accent);
    }
    draw_brows(c, a, p, 3);
    draw_mouth(c, a, p);
}

static void draw_robo_pebble(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 13, 10, 134, 100, 28, RGB565(15, 48, 48));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 + 2, p->eye_h[eye] / 2 + 2,
            3, a->secondary, a->dark);
        fill_ellipse(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 - 2,
            clamp_i32(p->eye_aperture[eye] / 2 - 1, 2, 24),
            a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
        fill_ellipse(c, p->pupil_x[eye] - 2, p->pupil_y[eye] - 2,
            2, 2, a->accent);
    }
    draw_brows(c, a, p, 2);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_m5_ink(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 8, 8, 144, 104, 18, RGB565(255, 246, 216));
    thick_line(c, 20, 18, 140, 18, 2, a->secondary);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2, 3,
            a->primary, RGB565(255, 246, 216));
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye] + 1, a->primary);
        mask_socket_lids(c, p, eye, RGB565(255, 246, 216), a->primary);
    }
    draw_brows(c, a, p, 2);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_manga_spark(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 11, 8, 138, 104, 29, RGB565(64, 36, 81));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 + 2, p->eye_h[eye] / 2 + 2,
            3, a->secondary, a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye] + 2, a->secondary);
        fill_ellipse(c, p->pupil_x[eye] - 3, p->pupil_y[eye] - 4,
            3, 4, a->accent);
        mask_socket_lids(c, p, eye, RGB565(64, 36, 81), a->secondary);
        thick_line(c, p->eye_x[eye] - p->eye_w[eye] / 2 - 2,
            p->eye_y[eye] - p->eye_h[eye] / 3,
            p->eye_x[eye] - p->eye_w[eye] / 2 - 7,
            p->eye_y[eye] - p->eye_h[eye] / 2 - 3,
            2, a->primary);
        thick_line(c, p->eye_x[eye] + p->eye_w[eye] / 2 + 2,
            p->eye_y[eye] - p->eye_h[eye] / 3,
            p->eye_x[eye] + p->eye_w[eye] / 2 + 7,
            p->eye_y[eye] - p->eye_h[eye] / 2 - 3,
            2, a->primary);
    }
    draw_brows(c, a, p, 2);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_eve_glow(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 9, 12, 142, 95, 34, RGB565(5, 20, 38));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        fill_round_rect(c, p->eye_x[eye] - p->eye_w[eye] / 2 - 4,
            p->eye_y[eye] - p->eye_h[eye] / 2 - 4,
            p->eye_w[eye] + 8, p->eye_h[eye] + 8, 12, a->secondary);
        fill_round_rect(c, p->eye_x[eye] - p->eye_w[eye] / 2,
            p->eye_y[eye] - p->eye_aperture[eye] / 2,
            p->eye_w[eye], p->eye_aperture[eye], 9, a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            clamp_i32(p->pupil_radius[eye] - 1, 2, 5),
            clamp_i32(p->pupil_radius[eye] - 1, 2, 5), a->dark);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            2, 2, a->accent);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_jibo_monocle(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_ellipse(c, 80, 58, 66, 50, RGB565(17, 21, 52));
    ellipse_outline(c, p->eye_x[0], p->eye_y[0],
        p->eye_w[0] / 2 + 7, p->eye_h[0] / 2 + 7,
        3, a->secondary, a->dark);
    ellipse_outline(c, p->eye_x[0], p->eye_y[0],
        p->eye_w[0] / 2, p->eye_h[0] / 2,
        4, a->primary, RGB565(17, 28, 65));
    fill_ellipse(c, p->pupil_x[0], p->pupil_y[0],
        p->pupil_radius[0] + 3, p->pupil_radius[0] + 3, a->secondary);
    fill_ellipse(c, p->pupil_x[0], p->pupil_y[0],
        p->pupil_radius[0], p->pupil_radius[0], a->accent);
    mask_socket_lids(c, p, 0U, RGB565(17, 28, 65), a->primary);
    for (int32_t tick = -2; tick <= 2; ++tick) {
        fill_rect(c, 78 + tick * 10, 20 + abs_i32(tick), 4, 2, a->primary);
    }
    thick_line(c, 55, p->brow_y[0], 80,
        p->brow_y[0] + p->brow_slope[0], 2, a->primary);
    thick_line(c, 80, p->brow_y[0] + p->brow_slope[0], 105,
        p->brow_y[0], 2, a->primary);
    draw_mouth(c, a, p);
}

static void draw_saccade_scope(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    for (int32_t y = 16; y <= 104; y += 8) {
        line(c, 12, y, 148, y, RGB565(4, 35, 24));
    }
    line(c, 80, 10, 80, 108, a->secondary);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2,
            2, a->primary, a->dark);
        thick_line(c, p->pupil_x[eye] - 7, p->pupil_y[eye],
            p->pupil_x[eye] + 7, p->pupil_y[eye], 1, a->accent);
        thick_line(c, p->pupil_x[eye], p->pupil_y[eye] - 7,
            p->pupil_x[eye], p->pupil_y[eye] + 7, 1, a->accent);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye], 2, 2, a->primary);
        mask_socket_lids(c, p, eye, a->dark, a->primary);
    }
    draw_brows(c, a, p, 1);
    draw_mouth(c, a, p);
}

static void draw_brow_puppet(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 12, 12, 136, 96, 18, RGB565(49, 25, 66));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2,
            2, a->secondary, a->dark);
        fill_ellipse(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 - 2,
            clamp_i32(p->eye_aperture[eye] / 2 - 1, 2, 20),
            a->accent);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
    }
    draw_brows(c, a, p, 6);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_lid_theatre(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 11, 10, 138, 101, 8, RGB565(35, 27, 43));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t hw = p->eye_w[eye] / 2;
        const int32_t hh = p->eye_h[eye] / 2;
        fill_round_rect(c, p->eye_x[eye] - hw, p->eye_y[eye] - hh,
            p->eye_w[eye], p->eye_h[eye], 7, a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
        const int32_t slope = p->brow_slope[eye];
        const point_t curtain[4] = {
            {(int16_t)(p->eye_x[eye] - hw - 2),
                (int16_t)(p->eye_y[eye] - hh - 3)},
            {(int16_t)(p->eye_x[eye] + hw + 2),
                (int16_t)(p->eye_y[eye] - hh - 3)},
            {(int16_t)(p->eye_x[eye] + hw + 2),
                (int16_t)(p->eye_y[eye] - hh + 4 + slope)},
            {(int16_t)(p->eye_x[eye] - hw - 2),
                (int16_t)(p->eye_y[eye] - hh + 4 - slope)},
        };
        fill_polygon(c, curtain, 4U, a->secondary);
        mask_socket_lids(c, p, eye, RGB565(35, 27, 43), a->secondary);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_iris_depth(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 8, 8, 144, 104, 26, RGB565(3, 25, 35));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        fill_ellipse(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2, a->primary);
        ellipse_outline(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye] + 5, p->pupil_radius[eye] + 5,
            2, a->secondary, a->accent);
        ellipse_outline(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye] + 2, p->pupil_radius[eye] + 2,
            2, a->accent, a->dark);
        fill_ellipse(c, p->pupil_x[eye] - 3, p->pupil_y[eye] - 4,
            2, 2, RGB565(255, 255, 255));
        mask_socket_lids(c, p, eye, RGB565(3, 25, 35), a->secondary);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_dawn_slits(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 10, 13, 140, 94, 28, RGB565(42, 23, 57));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        fill_round_rect(c, p->eye_x[eye] - p->eye_w[eye] / 2 - 2,
            p->eye_y[eye] - p->eye_h[eye] / 2 - 2,
            p->eye_w[eye] + 4, p->eye_h[eye] + 4,
            p->eye_h[eye] / 2 + 1, a->secondary);
        fill_round_rect(c, p->eye_x[eye] - p->eye_w[eye] / 2,
            p->eye_y[eye] - p->eye_aperture[eye] / 2,
            p->eye_w[eye], p->eye_aperture[eye],
            p->eye_aperture[eye] / 2, a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            2, clamp_i32(p->pupil_radius[eye], 2, p->eye_h[eye] / 2),
            a->dark);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_curious_pair(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 12, 9, 136, 103, 19, RGB565(11, 48, 55));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t tilt = eye == 0U ? -3 : 4;
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye] + tilt,
            p->eye_w[eye] / 2 + 2, p->eye_h[eye] / 2 + 2,
            3, a->primary, a->dark);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye] + tilt,
            p->pupil_radius[eye], p->pupil_radius[eye], a->accent);
        fill_ellipse(c, p->pupil_x[eye] - 2, p->pupil_y[eye] - 2 + tilt,
            1, 1, RGB565(255, 255, 255));
        mask_socket_lids(c, p, eye, RGB565(11, 48, 55), a->primary);
    }
    draw_brows(c, a, p, 3);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_dot_eye(
    canvas_t *c,
    const actor_def_t *a,
    const face_eye_actor_pose_t *p,
    size_t eye)
{
    const int32_t columns = 5;
    const int32_t rows = 5;
    const int32_t spacing = 7;
    const int32_t start_x = p->eye_x[eye] - 2 * spacing;
    const int32_t start_y = p->eye_y[eye] - 2 * spacing;
    const int32_t measured_rows =
        clamp_i32(1 + p->eye_aperture[eye] * rows / ACTORS[
            FACE_EYE_ACTOR_DOT_MARQUEE].eye_h, 1, rows);
    static const uint8_t authored_rows[FACE_EXPRESSION_COUNT][2] = {
        [FACE_EXPRESSION_NEUTRAL] = {4, 4},
        [FACE_EXPRESSION_WARM] = {4, 4},
        [FACE_EXPRESSION_JOY] = {3, 3},
        [FACE_EXPRESSION_CONCERN] = {4, 4},
        [FACE_EXPRESSION_SURPRISE] = {5, 5},
        [FACE_EXPRESSION_THOUGHTFUL] = {4, 3},
        [FACE_EXPRESSION_SKEPTICAL] = {2, 4},
        [FACE_EXPRESSION_DETERMINED] = {3, 3},
        [FACE_EXPRESSION_SLEEPY] = {1, 1},
        [FACE_EXPRESSION_EXCITED] = {5, 5},
        [FACE_EXPRESSION_EMBARRASSED] = {3, 4},
    };
    const int32_t open_rows = clamp_i32(
        mix_i32(
            measured_rows,
            authored_rows[p->stage_expression][eye],
            p->expression_weight),
        1,
        rows);
    const int32_t first_row = (rows - open_rows) / 2;
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t column = 0; column < columns; ++column) {
            const bool open = row >= first_row &&
                row < first_row + open_rows;
            const int32_t dx =
                abs_i32(start_x + column * spacing - p->pupil_x[eye]);
            const int32_t dy =
                abs_i32(start_y + row * spacing - p->pupil_y[eye]);
            const uint16_t color =
                open && dx <= 5 && dy <= 5 ? a->accent
                : open ? a->primary : a->dark;
            fill_rect(c, start_x + column * spacing - 2,
                start_y + row * spacing - 2, 4, 4, color);
        }
    }
}

static void draw_dot_marquee(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 8, 10, 144, 100, 5, RGB565(11, 11, 12));
    draw_dot_eye(c, a, p, 0U);
    draw_dot_eye(c, a, p, 1U);
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_cat_lantern(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    const point_t ears_left[3] = {{20, 34}, {31, 13}, {48, 35}};
    const point_t ears_right[3] = {{112, 35}, {129, 13}, {140, 34}};
    fill_polygon(c, ears_left, 3U, RGB565(18, 49, 32));
    fill_polygon(c, ears_right, 3U, RGB565(18, 49, 32));
    fill_round_rect(c, 15, 24, 130, 86, 32, RGB565(11, 39, 28));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t hw = p->eye_w[eye] / 2;
        const int32_t hh = p->eye_h[eye] / 2;
        const point_t almond[6] = {
            {(int16_t)(p->eye_x[eye] - hw), (int16_t)p->eye_y[eye]},
            {(int16_t)(p->eye_x[eye] - hw / 2),
                (int16_t)(p->eye_y[eye] - hh)},
            {(int16_t)(p->eye_x[eye] + hw / 2),
                (int16_t)(p->eye_y[eye] - hh)},
            {(int16_t)(p->eye_x[eye] + hw), (int16_t)p->eye_y[eye]},
            {(int16_t)(p->eye_x[eye] + hw / 2),
                (int16_t)(p->eye_y[eye] + hh)},
            {(int16_t)(p->eye_x[eye] - hw / 2),
                (int16_t)(p->eye_y[eye] + hh)},
        };
        fill_polygon(c, almond, 6U, a->primary);
        const int32_t slit =
            clamp_i32(1 + (255 - p->source.attention) / 70, 1, 4);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            slit, p->pupil_radius[eye] + 3, a->dark);
        fill_ellipse(c, p->pupil_x[eye] - 3, p->pupil_y[eye] - 4,
            1, 2, a->accent);
        mask_socket_lids(c, p, eye, RGB565(11, 39, 28), a->secondary);
    }
    draw_brows(c, a, p, 2);
    const int32_t my = p->mouth_y;
    const int32_t half = clamp_i32(p->mouth_w / 3, 8, 20);
    const int32_t drop = clamp_i32(3 + p->mouth_h / 3, 4, 11);
    if (p->mouth_h > 7) {
        fill_ellipse(c, p->mouth_x, my + drop / 2,
            half - 2, clamp_i32(p->mouth_h / 2, 3, 10), a->dark);
        if (p->tongue > 40U) {
            fill_ellipse(c, p->mouth_x, my + drop,
                half / 2, 2 + p->tongue / 90, a->blush);
        }
    }
    thick_line(c, p->mouth_x - half, my, p->mouth_x - half / 2,
        my + drop, 2, a->primary);
    thick_line(c, p->mouth_x - half / 2, my + drop, p->mouth_x,
        my, 2, a->primary);
    thick_line(c, p->mouth_x, my, p->mouth_x + half / 2,
        my + drop, 2, a->primary);
    thick_line(c, p->mouth_x + half / 2, my + drop,
        p->mouth_x + half, my, 2, a->primary);
    for (int32_t side = -1; side <= 1; side += 2) {
        thick_line(c, p->mouth_x + side * 8, my + 4,
            p->mouth_x + side * 36, my + 1, 1, a->secondary);
        thick_line(c, p->mouth_x + side * 8, my + 7,
            p->mouth_x + side * 35, my + 11, 1, a->secondary);
    }
}

static void draw_vector_stage(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    draw_anki_procedural_rig(c, a, p);
}

static void draw_cozmo_console(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    draw_anki_procedural_rig(c, a, p);
}

static void draw_brow_chorus(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 6, 7, 148, 106, 20, RGB565(244, 220, 171));
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2,
            3, a->primary, a->accent);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye] + 1, a->primary);
        mask_socket_lids(c, p, eye, RGB565(244, 220, 171), a->primary);
        const int32_t half = p->eye_w[eye] / 2 + 3;
        thick_line(c, p->eye_x[eye] - half,
            p->brow_y[eye] - p->brow_slope[eye],
            p->eye_x[eye] + half,
            p->brow_y[eye] + p->brow_slope[eye], 7, a->secondary);
    }
    fill_polygon(c,
        (const point_t[]){{76, 61}, {84, 61}, {80, 72}},
        3U, a->secondary);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

static void draw_moon_sleep(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 8, 8, 144, 104, 34, RGB565(10, 18, 51));
    fill_rect(c, 22, 21, 3, 3, a->accent);
    fill_rect(c, 136, 31, 2, 2, a->accent);
    fill_rect(c, 80, 15, 2, 2, a->accent);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 + 2, p->eye_h[eye] / 2 + 2,
            3, a->secondary, a->dark);
        fill_ellipse(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 - 1,
            clamp_i32(p->eye_aperture[eye] / 2, 2, 18), a->primary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
        mask_socket_lids(c, p, eye, a->dark, a->secondary);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_iris_binocular(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 7, 9, 146, 101, 12, RGB565(42, 34, 22));
    thick_line(c, 20, 17, 140, 17, 3, a->secondary);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 + 4, p->eye_h[eye] / 2 + 4,
            4, a->secondary, a->dark);
        fill_ellipse(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2, p->eye_h[eye] / 2, a->primary);
        ellipse_outline(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye] + 6, p->pupil_radius[eye] + 6,
            3, a->secondary, a->accent);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye], a->dark);
        fill_ellipse(c, p->pupil_x[eye] - 3, p->pupil_y[eye] - 4,
            2, 2, a->accent);
        mask_socket_lids(c, p, eye, RGB565(42, 34, 22), a->secondary);
    }
    draw_brows(c, a, p, 2);
    draw_mouth(c, a, p);
}

static void draw_cat_mecha(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    const point_t mask[8] = {
        {9, 26}, {31, 9}, {129, 9}, {151, 26},
        {143, 102}, {105, 111}, {55, 111}, {17, 102},
    };
    fill_polygon(c, mask, 8U, RGB565(54, 13, 18));
    thick_line(c, 80, 10, 80, 107, 2, a->secondary);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t hw = p->eye_w[eye] / 2;
        const int32_t hh = p->eye_h[eye] / 2;
        const point_t socket[6] = {
            {(int16_t)(p->eye_x[eye] - hw), (int16_t)p->eye_y[eye]},
            {(int16_t)(p->eye_x[eye] - hw / 2),
                (int16_t)(p->eye_y[eye] - hh)},
            {(int16_t)(p->eye_x[eye] + hw / 2),
                (int16_t)(p->eye_y[eye] - hh)},
            {(int16_t)(p->eye_x[eye] + hw), (int16_t)p->eye_y[eye]},
            {(int16_t)(p->eye_x[eye] + hw / 2),
                (int16_t)(p->eye_y[eye] + hh)},
            {(int16_t)(p->eye_x[eye] - hw / 2),
                (int16_t)(p->eye_y[eye] + hh)},
        };
        fill_polygon(c, socket, 6U, a->primary);
        ellipse_outline(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye] + 3, p->pupil_radius[eye] + 5,
            2, a->accent, a->dark);
        fill_rect(c, p->pupil_x[eye] - 1,
            p->pupil_y[eye] - p->pupil_radius[eye] - 4,
            3, p->pupil_radius[eye] * 2 + 9, a->dark);
        mask_socket_lids(c, p, eye, RGB565(54, 13, 18), a->secondary);
    }
    draw_brows(c, a, p, 3);
    draw_mouth(c, a, p);
}

static void draw_manga_panel(
    canvas_t *c, const actor_def_t *a, const face_eye_actor_pose_t *p)
{
    fill_round_rect(c, 5, 5, 150, 110, 8, RGB565(255, 238, 243));
    thick_line(c, 14, 16, 146, 16, 2, a->secondary);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        ellipse_outline(c, p->eye_x[eye], p->eye_y[eye],
            p->eye_w[eye] / 2 + 2, p->eye_h[eye] / 2 + 2,
            4, a->primary, a->accent);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye] + 5, p->pupil_radius[eye] + 7,
            a->secondary);
        fill_ellipse(c, p->pupil_x[eye], p->pupil_y[eye],
            p->pupil_radius[eye], p->pupil_radius[eye] + 2, a->dark);
        fill_ellipse(c, p->pupil_x[eye] - 4, p->pupil_y[eye] - 5,
            3, 4, a->accent);
        mask_socket_lids(c, p, eye, RGB565(255, 238, 243), a->primary);
        thick_line(c, p->eye_x[eye] - p->eye_w[eye] / 2,
            p->eye_y[eye] - p->eye_h[eye] / 3,
            p->eye_x[eye] - p->eye_w[eye] / 2 - 7,
            p->eye_y[eye] - p->eye_h[eye] / 2 - 4, 2, a->primary);
    }
    draw_brows(c, a, p, 3);
    draw_cheeks(c, a, p);
    draw_mouth(c, a, p);
}

bool face_eye_actor_render(
    face_eye_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!style_valid(style) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_EYE_ACTOR_PIXEL_COUNT) {
        return false;
    }
    face_eye_actor_pose_t pose;
    if (!face_eye_actor_resolve(style, render_key, sample_clock, &pose)) {
        return false;
    }
    canvas_t canvas = {rgb565};
    const actor_def_t *actor = &ACTORS[style];
    clear_canvas(&canvas, actor->background);

    switch (style) {
    case FACE_EYE_ACTOR_VECTOR_FELT:
        draw_vector_felt(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_COZMO_TILES:
        draw_cozmo_tiles(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_ROBO_WEDGE:
        draw_robo_wedge(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_ROBO_PEBBLE:
        draw_robo_pebble(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_M5_INK:
        draw_m5_ink(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_MANGA_SPARK:
        draw_manga_spark(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_EVE_GLOW:
        draw_eve_glow(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_JIBO_MONOCLE:
        draw_jibo_monocle(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_SACCADE_SCOPE:
        draw_saccade_scope(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_BROW_PUPPET:
        draw_brow_puppet(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_LID_THEATRE:
        draw_lid_theatre(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_IRIS_DEPTH:
        draw_iris_depth(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_DAWN_SLITS:
        draw_dawn_slits(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_CURIOUS_PAIR:
        draw_curious_pair(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_DOT_MARQUEE:
        draw_dot_marquee(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_CAT_LANTERN:
        draw_cat_lantern(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_VECTOR_STAGE:
        draw_vector_stage(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_COZMO_CONSOLE:
        draw_cozmo_console(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_BROW_CHORUS:
        draw_brow_chorus(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_MOON_SLEEP:
        draw_moon_sleep(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_IRIS_BINOCULAR:
        draw_iris_binocular(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_CAT_MECHA:
        draw_cat_mecha(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_MANGA_PANEL:
        draw_manga_panel(&canvas, actor, &pose);
        break;
    case FACE_EYE_ACTOR_COUNT:
    default:
        return false;
    }
    return true;
}

bool face_eye_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_eye_actor_style_t style;
    return face_eye_actor_from_legacy_id(legacy_profile_id, &style) &&
        face_eye_actor_render(
            style, render_key, sample_clock, rgb565, pixel_capacity);
}
