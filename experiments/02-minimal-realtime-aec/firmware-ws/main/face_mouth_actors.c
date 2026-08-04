#include "face_mouth_actors.h"

#include <string.h>

enum {
    MA_EXPRESSION_COUNT = 11,
    MA_EXPR_NEUTRAL = 0,
    MA_EXPR_WARM = 1,
    MA_EXPR_JOY = 2,
    MA_EXPR_CONCERN = 3,
    MA_EXPR_SURPRISE = 4,
    MA_EXPR_THOUGHTFUL = 5,
    MA_EXPR_SKEPTICAL = 6,
    MA_EXPR_DETERMINED = 7,
    MA_EXPR_SLEEPY = 8,
    MA_EXPR_EXCITED = 9,
    MA_EXPR_EMBARRASSED = 10,
};

#define MA_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8) |                      \
                (((uint16_t)(green) & 0xfcU) << 3) |                    \
                ((uint16_t)(blue) >> 3)))

typedef struct {
    uint16_t *pixels;
} ma_canvas_t;

typedef struct {
    int8_t eye_left;
    int8_t eye_right;
    int8_t brow_left;
    int8_t brow_right;
    int8_t brow_slant_left;
    int8_t brow_slant_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t smile;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t tilt;
    uint8_t cheek;
} ma_expression_t;

/*
 * Pixel-space targets are intentionally bold.  At full expression weight all
 * eleven reads differ at thumbnail size, while the continuous action fields
 * remain free to modify the result.
 */
static const ma_expression_t MA_EXPRESSIONS[MA_EXPRESSION_COUNT] = {
    [MA_EXPR_NEUTRAL] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    [MA_EXPR_WARM] = {-1, -1, 2, 2, -1, 1, 0, 0, 5, -1, 3, 0, 90},
    [MA_EXPR_JOY] = {-5, -5, 1, 1, -2, 2, 0, -1, 10, 2, 6, 0, 220},
    [MA_EXPR_CONCERN] = {-1, 0, 6, 6, 4, -4, -2, 1, -6, -1, -2, -3, 70},
    [MA_EXPR_SURPRISE] = {5, 5, 8, 8, -1, 1, 0, -2, 0, 10, -8, 0, 0},
    [MA_EXPR_THOUGHTFUL] = {-2, 1, 3, 0, 2, 0, -6, -4, -2, -2, -5, 5, 35},
    [MA_EXPR_SKEPTICAL] = {-5, 1, 7, -3, 3, -3, 5, 0, -3, -2, 1, -7, 45},
    [MA_EXPR_DETERMINED] = {-4, -4, -4, -4, -5, 5, 0, 1, -1, 1, 5, 0, 55},
    [MA_EXPR_SLEEPY] = {-8, -8, -3, -3, 0, 0, -3, 5, 1, 3, -4, 4, 20},
    [MA_EXPR_EXCITED] = {4, 4, 7, 7, -2, 2, 0, -2, 9, 8, 7, 0, 150},
    [MA_EXPR_EMBARRASSED] = {-3, -6, 2, 4, 1, -2, 6, 3, 4, -1, -1, 5, 255},
};

static const char *const MA_PROFILE_SLUGS[FACE_MOUTH_ACTOR_COUNT] = {
    "actor-preston",
    "actor-jali",
    "actor-ribbon",
    "actor-teeth-tongue",
    "actor-led-vu",
    "actor-origami",
};

static const char *const MA_PROFILE_NAMES[FACE_MOUTH_ACTOR_COUNT] = {
    "Pip, cel-animation sprite actor",
    "Jali, faceted jaw-and-lip android",
    "Ruby, cabaret ribbon performer",
    "Munch, teeth-and-tongue creature",
    "VU-5, segmented display robot",
    "Ori, expressive folded-paper fox",
};

static int32_t ma_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t ma_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static uint8_t ma_mix_u8(uint8_t from, uint8_t to, uint8_t weight)
{
    return (uint8_t)ma_clamp(ma_mix(from, to, weight), 0, 255);
}

static uint32_t ma_key_signature(const face_render_key_t *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < sizeof(*key); ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static void ma_put(
    ma_canvas_t *canvas, int x, int y, uint16_t color)
{
    if ((unsigned)x < FACE_MOUTH_ACTORS_WIDTH &&
        (unsigned)y < FACE_MOUTH_ACTORS_HEIGHT) {
        canvas->pixels[
            (size_t)y * FACE_MOUTH_ACTORS_WIDTH + (size_t)x] = color;
    }
}

static void ma_clear(ma_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_MOUTH_ACTORS_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void ma_rect(
    ma_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    int left = (int)ma_clamp(x, 0, FACE_MOUTH_ACTORS_WIDTH);
    int top = (int)ma_clamp(y, 0, FACE_MOUTH_ACTORS_HEIGHT);
    int right =
        (int)ma_clamp(x + width, 0, FACE_MOUTH_ACTORS_WIDTH);
    int bottom =
        (int)ma_clamp(y + height, 0, FACE_MOUTH_ACTORS_HEIGHT);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            canvas->pixels[
                (size_t)yy * FACE_MOUTH_ACTORS_WIDTH + (size_t)xx] =
                color;
        }
    }
}

static void ma_line(
    ma_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ma_put(canvas, x0, y0, color);
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

static void ma_thick_line(
    ma_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        ma_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static int32_t ma_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
           (int32_t)(py - ay) * (bx - ax);
}

static void ma_triangle(
    ma_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    int left = ax;
    int right = ax;
    int top = ay;
    int bottom = ay;
    if (bx < left) {
        left = bx;
    }
    if (cx < left) {
        left = cx;
    }
    if (bx > right) {
        right = bx;
    }
    if (cx > right) {
        right = cx;
    }
    if (by < top) {
        top = by;
    }
    if (cy < top) {
        top = cy;
    }
    if (by > bottom) {
        bottom = by;
    }
    if (cy > bottom) {
        bottom = cy;
    }
    left = (int)ma_clamp(left, 0, FACE_MOUTH_ACTORS_WIDTH - 1);
    right = (int)ma_clamp(right, 0, FACE_MOUTH_ACTORS_WIDTH - 1);
    top = (int)ma_clamp(top, 0, FACE_MOUTH_ACTORS_HEIGHT - 1);
    bottom = (int)ma_clamp(bottom, 0, FACE_MOUTH_ACTORS_HEIGHT - 1);
    const int32_t orientation = ma_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = ma_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = ma_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = ma_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                ma_put(canvas, x, y, color);
            }
        }
    }
}

static void ma_quad(
    ma_canvas_t *canvas,
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
    ma_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    ma_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void ma_ellipse(
    ma_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius_x,
    int radius_y,
    uint16_t color)
{
    if (radius_x <= 0 || radius_y <= 0) {
        return;
    }
    const int32_t rx2 = radius_x * radius_x;
    const int32_t ry2 = radius_y * radius_y;
    const int32_t limit = rx2 * ry2;
    for (int dy = -radius_y; dy <= radius_y; ++dy) {
        int span = radius_x;
        const int32_t vertical = dy * dy * rx2;
        while (span > 0 && span * span * ry2 + vertical > limit) {
            --span;
        }
        ma_rect(
            canvas,
            center_x - span,
            center_y + dy,
            span * 2 + 1,
            1,
            color);
    }
}

static void ma_ellipse_ring(
    ma_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius_x,
    int radius_y,
    int thickness,
    uint16_t outer,
    uint16_t inner)
{
    ma_ellipse(canvas, center_x, center_y, radius_x, radius_y, outer);
    ma_ellipse(
        canvas,
        center_x,
        center_y,
        radius_x - thickness,
        radius_y - thickness,
        inner);
}

static void ma_round_rect(
    ma_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int radius,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    radius = (int)ma_clamp(radius, 0, (width < height ? width : height) / 2);
    ma_rect(canvas, x + radius, y, width - radius * 2, height, color);
    ma_rect(canvas, x, y + radius, width, height - radius * 2, color);
    ma_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    ma_ellipse(
        canvas, x + width - radius - 1, y + radius, radius, radius, color);
    ma_ellipse(
        canvas, x + radius, y + height - radius - 1, radius, radius, color);
    ma_ellipse(
        canvas,
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        radius,
        color);
}

static void ma_quadratic(
    ma_canvas_t *canvas,
    int x0,
    int y0,
    int cx,
    int cy,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    int previous_x = x0;
    int previous_y = y0;
    for (int step = 1; step <= 16; ++step) {
        const int inverse = 16 - step;
        const int x =
            (inverse * inverse * x0 +
             2 * inverse * step * cx +
             step * step * x1 +
             128) /
            256;
        const int y =
            (inverse * inverse * y0 +
             2 * inverse * step * cy +
             step * step * y1 +
             128) /
            256;
        ma_thick_line(
            canvas,
            previous_x,
            previous_y,
            x,
            y,
            thickness,
            color);
        previous_x = x;
        previous_y = y;
    }
}

static int ma_feature_x(
    const face_mouth_actor_pose_t *pose, int x, int y)
{
    /*
     * The painted chassis is a registration frame, not a camera layer.
     * Head cues are resolved into gaze, brows and mouth landmarks below;
     * translating every feature at once turns a one-pixel fixed-point
     * crossing into a large raster discontinuity.
     */
    (void)pose;
    (void)y;
    return x;
}

static int ma_feature_y(
    const face_mouth_actor_pose_t *pose, int y)
{
    (void)pose;
    return y;
}

static uint8_t ma_viseme_class(const face_render_key_t *key)
{
    if (key->viseme == FACE_VISEME_NONE ||
        key->viseme_weight < 8U) {
        return 0U;
    }
    if (key->viseme_set == FACE_VISEME_SET_PRESTON9) {
        static const uint8_t PRESTON_MAP[9] = {
            0U, 1U, 2U, 3U, 4U, 5U, 7U, 9U, 11U,
        };
        return PRESTON_MAP[key->viseme % 9U];
    }
    if (key->viseme_set == FACE_VISEME_SET_VRM5) {
        static const uint8_t VRM_MAP[5] = {0U, 1U, 2U, 3U, 4U};
        return VRM_MAP[key->viseme % 5U];
    }
    /*
     * OVR15 is the native face_pose vocabulary.  Microsoft/custom values are
     * safely folded into the same articulatory classes instead of indexing a
     * renderer-specific atlas.
     */
    return (uint8_t)(key->viseme % FACE_VISEME_COUNT);
}

static void ma_viseme_target(
    uint8_t viseme, int *width, int *opening, int *round)
{
    static const int8_t WIDTH[FACE_VISEME_COUNT] = {
        5, 10, 12, -7, -10, 4, 10, 1, 4, 5, 3, 7, -2, 8, 0,
    };
    static const int8_t OPEN[FACE_VISEME_COUNT] = {
        18, 10, 7, 17, 11, 1, 4, 7, 9, 3, 12, 6, 9, 7, 1,
    };
    static const int8_t ROUND[FACE_VISEME_COUNT] = {
        1, 0, 0, 13, 17, 0, 0, 3, 1, 0, 2, 1, 7, 2, 0,
    };
    const uint8_t safe =
        viseme < FACE_VISEME_COUNT ? viseme : FACE_VISEME_SIL;
    *width = WIDTH[safe];
    *opening = OPEN[safe];
    *round = ROUND[safe];
}

static void ma_landmarks_for_profile(
    face_mouth_actor_profile_t profile,
    face_mouth_actor_landmarks_t *landmarks)
{
    static const face_mouth_actor_landmarks_t TABLE[FACE_MOUTH_ACTOR_COUNT] = {
        [FACE_MOUTH_ACTOR_PRESTON] = {
            {27, 5, 106, 111}, {39, 29, 35, 35},
            {86, 29, 35, 35}, {43, 70, 74, 35},
        },
        [FACE_MOUTH_ACTOR_JALI] = {
            {23, 4, 114, 112}, {36, 29, 38, 30},
            {86, 29, 38, 30}, {39, 68, 82, 38},
        },
        [FACE_MOUTH_ACTOR_RIBBON] = {
            {25, 5, 110, 111}, {37, 28, 38, 35},
            {85, 28, 38, 35}, {38, 69, 84, 37},
        },
        [FACE_MOUTH_ACTOR_TEETH_TONGUE] = {
            {20, 5, 120, 111}, {33, 27, 40, 38},
            {87, 27, 40, 38}, {34, 65, 92, 44},
        },
        [FACE_MOUTH_ACTOR_LED_VU] = {
            {18, 7, 124, 108}, {32, 29, 42, 31},
            {86, 29, 42, 31}, {38, 68, 84, 38},
        },
        [FACE_MOUTH_ACTOR_ORIGAMI] = {
            {20, 4, 120, 112}, {34, 29, 42, 31},
            {84, 29, 42, 31}, {42, 68, 76, 38},
        },
    };
    *landmarks = TABLE[profile];
}

size_t face_mouth_actors_profile_count(void)
{
    return FACE_MOUTH_ACTOR_COUNT;
}

const char *face_mouth_actors_profile_slug(
    face_mouth_actor_profile_t profile)
{
    return (unsigned)profile < FACE_MOUTH_ACTOR_COUNT
               ? MA_PROFILE_SLUGS[profile]
               : NULL;
}

const char *face_mouth_actors_profile_name(
    face_mouth_actor_profile_t profile)
{
    return (unsigned)profile < FACE_MOUTH_ACTOR_COUNT
               ? MA_PROFILE_NAMES[profile]
               : NULL;
}

bool face_mouth_actors_resolve(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_mouth_actor_pose_t *pose)
{
    if ((unsigned)profile >= FACE_MOUTH_ACTOR_COUNT ||
        key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    pose->source = *key;
    pose->input_signature = ma_key_signature(key);

    const uint8_t expression =
        key->stage_expression < MA_EXPRESSION_COUNT
            ? key->stage_expression
            : MA_EXPR_NEUTRAL;
    const ma_expression_t *target = &MA_EXPRESSIONS[expression];
    const uint8_t expression_weight =
        key->stage_expression < MA_EXPRESSION_COUNT
            ? key->expression_weight
            : 0U;
    /*
     * Keep the silhouette and feature sockets registered to the display.
     * Large-scale pose IR is expressed by small, independent facial actions
     * instead of a quantized translation/rotation of the whole raster.
     */
    pose->head_x = 80;
    pose->head_y = 58;
    pose->head_roll = 0;
    const int local_roll = (int)ma_clamp(
        key->head_roll / 24 +
            ma_mix(0, target->tilt, expression_weight) / 2,
        -5,
        5);
    pose->eye_y = (int16_t)ma_clamp(
        45 + key->head_pitch / 36 + key->body_lean_y / 48,
        42,
        48);

    pose->gaze_x = (int16_t)ma_clamp(
        key->controls.look_x / 18 +
            ma_mix(0, target->gaze_x, expression_weight) +
            key->head_yaw / 35 + key->body_lean_x / 64,
        -8,
        8);
    pose->gaze_y = (int16_t)ma_clamp(
        key->controls.look_y / 20 +
            ma_mix(0, target->gaze_y, expression_weight) +
            key->head_pitch / 40 + key->body_lean_y / 64,
        -6,
        6);

    int eye_left =
        4 + key->controls.eye_left_open * 12 / 255 +
        ma_mix(0, target->eye_left, expression_weight) -
        key->eye_left_squint * 7 / 255;
    int eye_right =
        4 + key->controls.eye_right_open * 12 / 255 +
        ma_mix(0, target->eye_right, expression_weight) -
        key->eye_right_squint * 7 / 255;
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        eye_left = 2;
        eye_right = 2;
    }
    pose->eye_left_open =
        (uint8_t)ma_clamp(eye_left, 2, 19);
    pose->eye_right_open =
        (uint8_t)ma_clamp(eye_right, 2, 19);

    pose->brow_left = (int16_t)ma_clamp(
        key->controls.brow / 24 + key->brow_inner / 30 +
            key->brow_outer_left / 38 +
            ma_mix(0, target->brow_left, expression_weight) +
            local_roll / 2,
        -7,
        10);
    pose->brow_right = (int16_t)ma_clamp(
        key->controls.brow / 24 + key->brow_inner / 30 +
            key->brow_outer_right / 38 +
            ma_mix(0, target->brow_right, expression_weight) -
            local_roll / 2,
        -7,
        10);
    pose->brow_slant_left = (int16_t)ma_clamp(
        key->brow_inner / 24 - key->brow_outer_left / 32 +
            ma_mix(0, target->brow_slant_left, expression_weight),
        -8,
        8);
    pose->brow_slant_right = (int16_t)ma_clamp(
        -key->brow_inner / 24 + key->brow_outer_right / 32 +
            ma_mix(0, target->brow_slant_right, expression_weight),
        -8,
        8);

    int viseme_width = 0;
    int viseme_open = 1;
    int viseme_round = 0;
    const uint8_t primary = ma_viseme_class(key);
    ma_viseme_target(primary, &viseme_width, &viseme_open, &viseme_round);
    if (key->viseme_secondary != FACE_VISEME_NONE &&
        key->viseme_blend > 0U) {
        int secondary_width;
        int secondary_open;
        int secondary_round;
        const face_render_key_t secondary_key = {
            .viseme = key->viseme_secondary,
            .viseme_weight = 255U,
            .viseme_set = key->viseme_set,
        };
        const uint8_t secondary = ma_viseme_class(&secondary_key);
        ma_viseme_target(
            secondary,
            &secondary_width,
            &secondary_open,
            &secondary_round);
        viseme_width =
            ma_mix(viseme_width, secondary_width, key->viseme_blend);
        viseme_open =
            ma_mix(viseme_open, secondary_open, key->viseme_blend);
        viseme_round =
            ma_mix(viseme_round, secondary_round, key->viseme_blend);
    }
    viseme_width =
        ma_mix(0, viseme_width, key->viseme_weight);
    viseme_open =
        ma_mix(1, viseme_open, key->viseme_weight);
    viseme_round =
        ma_mix(0, viseme_round, key->viseme_weight);

    const int round =
        key->controls.mouth_round * 13 / 255 + viseme_round;
    const int press = key->controls.mouth_press * 18 / 255;
    const int audio =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U
            ? key->audio_level * 3 / 255
            : 0;
    int opening =
        2 + key->controls.mouth_open * 12 / 255 +
        viseme_open + audio - press +
        ma_mix(0, target->mouth_open, expression_weight);
    if (primary == FACE_VISEME_PP || key->controls.mouth_press > 220U) {
        opening = 2;
    }
    pose->mouth_x = (int16_t)ma_clamp(
        80 + key->head_yaw / 30 +
            key->body_lean_x / 48 +
            (key->mouth_corner_right - key->mouth_corner_left) / 22,
        75,
        85);
    pose->mouth_y = (int16_t)ma_clamp(
        84 + key->head_pitch / 40 + key->body_lean_y / 48,
        81,
        88);
    int resolved_width = (int)ma_clamp(
        38 + key->controls.mouth_width * 19 / 255 +
            viseme_width - round +
            ma_mix(0, target->mouth_width, expression_weight),
        23,
        68);
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        /* One authored anticipation pose: width leads before the jaw. */
        resolved_width += 3;
        opening = (opening * 3 + 2) / 4;
        pose->brow_left =
            (int16_t)ma_clamp(pose->brow_left + 1, -7, 10);
        pose->brow_right =
            (int16_t)ma_clamp(pose->brow_right + 1, -7, 10);
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        /* Settle closes progressively without snapping to the rest line. */
        opening = (opening * 2 + 2) / 3;
        resolved_width -= 2;
    }
    opening = (int)ma_clamp(opening, 2, 31);
    /*
     * Even an O/U remains a face mouth rather than a vertical lozenge.  The
     * aperture may become rounder, but its bounded horizontal box always
     * exceeds the opening and leaves room for readable corners.
     */
    const int minimum_open_width =
        opening * 3 / 2 > opening + 10
            ? opening * 3 / 2
            : opening + 10;
    if (resolved_width < minimum_open_width) {
        resolved_width = minimum_open_width;
    }
    pose->mouth_width =
        (int16_t)ma_clamp(resolved_width, 23, 68);
    pose->mouth_open = (int16_t)opening;
    pose->mouth_round = (int16_t)ma_clamp(round, 0, 25);
    const int jaw_coupling =
        (int)ma_clamp((opening - 12) / 5, 0, 3);
    pose->eye_left_open = (uint8_t)ma_clamp(
        (int)pose->eye_left_open - jaw_coupling / 2, 2, 19);
    pose->eye_right_open = (uint8_t)ma_clamp(
        (int)pose->eye_right_open - jaw_coupling / 2, 2, 19);
    pose->brow_left =
        (int16_t)ma_clamp(pose->brow_left + jaw_coupling / 2, -7, 10);
    pose->brow_right =
        (int16_t)ma_clamp(pose->brow_right + jaw_coupling / 2, -7, 10);
    const int smile = (int)ma_clamp(
        (key->mouth_corner_left + key->mouth_corner_right) / 22 +
            key->affect_valence / 24 +
            ma_mix(0, target->smile, expression_weight),
        -11,
        12);
    int asymmetry = 0;
    if (expression == MA_EXPR_EMBARRASSED) {
        asymmetry = ma_mix(0, 6, expression_weight);
    } else if (expression == MA_EXPR_SKEPTICAL) {
        asymmetry = ma_mix(0, -4, expression_weight);
    } else if (expression == MA_EXPR_THOUGHTFUL) {
        asymmetry = ma_mix(0, 3, expression_weight);
    }
    pose->mouth_smile = (int16_t)smile;
    pose->mouth_corner_left = (int16_t)ma_clamp(
        -smile -
            ((int)key->mouth_corner_left -
             (int)key->mouth_corner_right) /
                22 +
            asymmetry + local_roll / 2,
        -10,
        10);
    pose->mouth_corner_right = (int16_t)ma_clamp(
        -smile +
            ((int)key->mouth_corner_left -
             (int)key->mouth_corner_right) /
                22 -
            asymmetry - local_roll / 2,
        -10,
        10);
    pose->teeth = ma_mix_u8(
        key->controls.mouth_teeth,
        (uint8_t)(primary == FACE_VISEME_FF ? 255U : 118U),
        key->viseme_weight / 2U);
    pose->tongue = ma_mix_u8(
        key->tongue,
        (uint8_t)(
            primary == FACE_VISEME_TH || primary == FACE_VISEME_NN
                ? 230U
                : 40U),
        key->viseme_weight);
    pose->cheek = (uint8_t)ma_clamp(
        ma_mix_u8(key->cheek, target->cheek, expression_weight) +
            jaw_coupling * 12,
        0,
        255);
    pose->attention = key->attention;
    pose->arousal = (uint8_t)ma_clamp(
        ((int)key->affect_arousal + key->audio_level) / 2,
        0,
        255);
    pose->expression = expression;
    pose->speech_phase = key->speech_phase;
    pose->viseme_class = primary;
    pose->animation_phase = (uint8_t)(
        sample_clock / (53U + (uint32_t)profile * 7U));
    return true;
}

static void ma_draw_brow(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    int center_x,
    int raise,
    int slant,
    int thickness,
    uint16_t color)
{
    const int y = ma_feature_y(pose, 28 - raise);
    const int x = ma_feature_x(pose, center_x, 28);
    ma_thick_line(
        canvas,
        x - 11,
        y - slant / 2,
        x + 11,
        y + slant / 2,
        thickness,
        color);
}

static void ma_draw_soft_eye(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    int center_x,
    uint8_t openness,
    uint16_t outline,
    uint16_t white,
    uint16_t iris,
    bool lashes)
{
    const int y = ma_feature_y(pose, pose->eye_y);
    const int x = ma_feature_x(pose, center_x, pose->eye_y);
    const int radius_y = openness / 2 + 1;
    if (openness <= 3U) {
        ma_quadratic(
            canvas, x - 12, y, x, y + 3, x + 12, y, 2, outline);
        ma_rect(canvas, x - 2 + pose->gaze_x / 3, y, 4, 2, iris);
    } else {
        ma_ellipse_ring(
            canvas, x, y, 14, radius_y + 1, 2, outline, white);
        const int pupil_x =
            (int)ma_clamp(x + pose->gaze_x, x - 7, x + 7);
        const int pupil_y =
            (int)ma_clamp(y + pose->gaze_y, y - radius_y / 2,
                          y + radius_y / 2);
        ma_ellipse(canvas, pupil_x, pupil_y, 5, 5, iris);
        ma_ellipse(canvas, pupil_x, pupil_y, 2, 3, outline);
        ma_rect(canvas, pupil_x - 1, pupil_y - 2, 2, 2, white);
    }
    if (lashes) {
        ma_line(canvas, x - 12, y - radius_y, x - 16, y - radius_y - 3, outline);
        ma_line(canvas, x + 12, y - radius_y, x + 16, y - radius_y - 3, outline);
    }
}

static void ma_draw_cheeks(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    uint16_t color,
    bool pixels)
{
    if (pose->cheek < 25U) {
        return;
    }
    const int radius = 2 + pose->cheek / 64;
    const int y = ma_feature_y(pose, 67);
    const int left_x = ma_feature_x(pose, 43, 67);
    const int right_x = ma_feature_x(pose, 117, 67);
    if (pixels) {
        ma_rect(canvas, left_x - radius, y, radius * 2, 3, color);
        ma_rect(canvas, right_x - radius, y, radius * 2, 3, color);
    } else {
        ma_ellipse(canvas, left_x, y, radius + 2, radius, color);
        ma_ellipse(canvas, right_x, y, radius + 2, radius, color);
    }
}

static void ma_draw_detail_mark(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    uint16_t color)
{
    /*
     * A stable registration glint gives the clock a deliberately tiny visual
     * footprint.  The complete input signature remains available in the
     * public pose, but is never converted into a teleporting screen position.
     */
    ma_put(canvas, 79, 12, color);
    if ((pose->animation_phase & 0x20U) != 0U) {
        ma_put(canvas, 80, 12, color);
    }
}

static int ma_curve_y(
    int left_y,
    int middle_y,
    int right_y,
    int t_q8,
    bool faceted)
{
    if (faceted) {
        /*
         * Four authored planes approximate the curve.  More than one plane
         * per side avoids the generic diamond/hex "collision hull" look
         * while retaining an unmistakably constructed polygon lip.
         */
        const int first_t = (t_q8 / 64) * 64;
        const int second_t = first_t < 256 ? first_t + 64 : 256;
        const int first_inverse = 256 - first_t;
        const int second_inverse = 256 - second_t;
        const int first_y =
            (left_y * first_inverse * first_inverse +
             2 * middle_y * first_inverse * first_t +
             right_y * first_t * first_t +
             32768) /
            65536;
        const int second_y =
            (left_y * second_inverse * second_inverse +
             2 * middle_y * second_inverse * second_t +
             right_y * second_t * second_t +
             32768) /
            65536;
        return first_y +
               (second_y - first_y) * (t_q8 - first_t) / 64;
    }
    const int inverse = 256 - t_q8;
    return (
        left_y * inverse * inverse +
        2 * middle_y * inverse * t_q8 +
        right_y * t_q8 * t_q8 +
        32768) /
        65536;
}

/*
 * Shared corner-driven mouth grammar.  It never builds a collision-hull
 * diamond: left and right corners are stable anchors and the upper/lower
 * envelopes bow independently between them.  `pixel_step` gives Preston a
 * stepped cel silhouette; `faceted` gives JALI/origami straight lip planes.
 */
static void ma_draw_envelope_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    uint16_t upper_lip,
    uint16_t lower_lip,
    uint16_t inside,
    uint16_t tooth,
    uint16_t tongue,
    int lip_thickness,
    int pixel_step,
    bool faceted,
    bool show_teeth,
    bool show_tongue)
{
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int half = pose->mouth_width / 2;
    const int open = pose->mouth_open;
    const int left_y = cy + pose->mouth_corner_left;
    const int right_y = cy + pose->mouth_corner_right;
    const int curve = pose->mouth_smile / 3;
    const int endpoint_average = (left_y + right_y) / 2;
    const int center_control =
        2 * (cy + curve) - endpoint_average;
    const int upper_mid = center_control - open * 4 / 5;
    const int lower_mid =
        center_control + open * 6 / 5 + pose->mouth_round / 4;
    lip_thickness = (int)ma_clamp(lip_thickness, 2, 6);
    pixel_step = (int)ma_clamp(pixel_step, 1, 4);

    if (open <= 3 ||
        pose->source.controls.mouth_press > 220U ||
        pose->viseme_class == FACE_VISEME_PP) {
        ma_quadratic(
            canvas,
            cx - half,
            left_y,
            cx,
            cy + curve,
            cx + half,
            right_y,
            lip_thickness,
            upper_lip);
        /* Bilabial pressure has visible mass, not a one-pixel scratch. */
        if (pose->source.controls.mouth_press > 150U ||
            pose->viseme_class == FACE_VISEME_PP) {
            ma_quadratic(
                canvas,
                cx - half + 2,
                left_y + 2,
                cx,
                cy + curve + 2,
                cx + half - 2,
                right_y + 2,
                2,
                lower_lip);
        }
        return;
    }

    for (int offset = -half; offset <= half; offset += pixel_step) {
        const int t_q8 = (offset + half) * 256 / (half * 2);
        int upper = ma_curve_y(
            left_y, upper_mid, right_y, t_q8, faceted);
        int lower = ma_curve_y(
            left_y, lower_mid, right_y, t_q8, faceted);
        if (pixel_step > 1) {
            upper = (upper / pixel_step) * pixel_step;
            lower = (lower / pixel_step) * pixel_step;
        }
        if (lower < upper + 1) {
            lower = upper + 1;
        }
        const int x = cx + offset;
        ma_rect(
            canvas,
            x,
            upper - lip_thickness,
            pixel_step,
            lip_thickness + 1,
            upper_lip);
        ma_rect(
            canvas,
            x,
            upper + 1,
            pixel_step,
            lower - upper - 1,
            inside);
        if (show_teeth && pose->teeth > 72U &&
            lower - upper > 6) {
            const int teeth_height = (int)ma_clamp(
                2 + pose->teeth / 70, 2, (lower - upper) / 2);
            ma_rect(
                canvas,
                x,
                upper + 1,
                pixel_step,
                teeth_height,
                tooth);
        }
        if (show_tongue && pose->tongue > 48U &&
            lower - upper > 8) {
            const int center_distance =
                t_q8 > 128 ? t_q8 - 128 : 128 - t_q8;
            if (center_distance < 88) {
                int tongue_height =
                    2 + (int)pose->tongue * (88 - center_distance) /
                            (255 * 11);
                tongue_height = (int)ma_clamp(
                    tongue_height, 2, (lower - upper) / 2);
                ma_rect(
                    canvas,
                    x,
                    lower - tongue_height,
                    pixel_step,
                    tongue_height,
                    tongue);
            }
        }
        ma_rect(
            canvas,
            x,
            lower,
            pixel_step,
            lip_thickness,
            lower_lip);
    }
    ma_ellipse(
        canvas, cx - half, left_y, lip_thickness, lip_thickness, upper_lip);
    ma_ellipse(
        canvas, cx + half, right_y, lip_thickness, lip_thickness, upper_lip);
}

static void ma_draw_preston_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t ink = MA_RGB565(55, 38, 45);
    const uint16_t lip = MA_RGB565(178, 55, 62);
    const uint16_t inside = MA_RGB565(62, 27, 42);
    const uint16_t teeth = MA_RGB565(255, 238, 189);
    const uint16_t tongue = MA_RGB565(235, 102, 105);
    ma_draw_envelope_mouth(
        canvas,
        pose,
        ink,
        lip,
        inside,
        teeth,
        tongue,
        3,
        3,
        false,
        true,
        true);
}

static void ma_render_preston(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(32, 77, 86);
    const uint16_t shadow = MA_RGB565(20, 52, 62);
    const uint16_t jacket = MA_RGB565(47, 92, 143);
    const uint16_t collar = MA_RGB565(238, 180, 74);
    const uint16_t face = MA_RGB565(244, 180, 82);
    const uint16_t face_light = MA_RGB565(255, 205, 105);
    const uint16_t ear = MA_RGB565(215, 124, 64);
    const uint16_t hair = MA_RGB565(70, 49, 49);
    const uint16_t ink = MA_RGB565(55, 38, 45);
    const uint16_t white = MA_RGB565(255, 244, 205);
    const uint16_t iris = MA_RGB565(37, 112, 123);
    const uint16_t blush = MA_RGB565(224, 92, 76);
    ma_clear(canvas, background);
    ma_rect(canvas, 0, 106, 160, 14, shadow);
    ma_ellipse(canvas, 80, 118, 58, 27, jacket);
    ma_triangle(canvas, 57, 104, 80, 117, 74, 99, collar);
    ma_triangle(canvas, 103, 104, 80, 117, 86, 99, collar);

    const int hx = 80;
    const int hy = 58;
    ma_ellipse(canvas, hx - 49, hy - 4, 12, 18, hair);
    ma_ellipse(canvas, hx + 49, hy - 4, 12, 18, hair);
    ma_ellipse(canvas, hx - 48, hy, 9, 13, ear);
    ma_ellipse(canvas, hx + 48, hy, 9, 13, ear);
    ma_ellipse(canvas, hx, hy, 48, 53, hair);
    ma_ellipse(canvas, hx, hy + 3, 44, 50, face);
    ma_ellipse(canvas, hx - 9, hy - 7, 30, 39, face_light);
    ma_rect(canvas, hx - 39, hy - 40, 12, 9, hair);
    ma_rect(canvas, hx - 26, hy - 49, 13, 10, hair);
    ma_rect(canvas, hx - 12, hy - 52, 14, 11, hair);
    ma_rect(canvas, hx + 3, hy - 49, 16, 9, hair);
    ma_rect(canvas, hx + 20, hy - 43, 15, 8, hair);

    ma_draw_brow(
        canvas, pose, 56, pose->brow_left, pose->brow_slant_left, 3, hair);
    ma_draw_brow(
        canvas, pose, 104, pose->brow_right, pose->brow_slant_right, 3, hair);
    ma_draw_soft_eye(
        canvas, pose, 56, pose->eye_left_open, ink, white, iris, false);
    ma_draw_soft_eye(
        canvas, pose, 104, pose->eye_right_open, ink, white, iris, false);
    const int nose_x = ma_feature_x(pose, 80, 61);
    const int nose_y = ma_feature_y(pose, 61);
    ma_rect(canvas, nose_x - 2, nose_y - 4, 4, 8, ear);
    ma_rect(canvas, nose_x, nose_y + 3, 5, 3, ear);
    ma_draw_cheeks(canvas, pose, blush, true);
    ma_draw_detail_mark(canvas, pose, ear);
    ma_draw_preston_mouth(canvas, pose);
}

static void ma_draw_jali_eye(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    int center_x,
    uint8_t openness,
    uint16_t ink,
    uint16_t glow)
{
    const int x = ma_feature_x(pose, center_x, pose->eye_y);
    const int y = ma_feature_y(pose, pose->eye_y);
    const int open = openness / 2 + 2;
    ma_quad(
        canvas,
        x - 15,
        y,
        x,
        y - open,
        x + 15,
        y,
        x,
        y + open,
        ink);
    ma_quad(
        canvas,
        x - 10,
        y,
        x,
        y - open + 2,
        x + 10,
        y,
        x,
        y + open - 2,
        glow);
    const int pupil_x = x + pose->gaze_x;
    const int pupil_y =
        (int)ma_clamp(y + pose->gaze_y, y - open + 2, y + open - 2);
    ma_rect(canvas, pupil_x - 2, pupil_y - 3, 5, 7, ink);
}

static void ma_draw_jali_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t edge = MA_RGB565(255, 90, 149);
    const uint16_t upper = MA_RGB565(207, 56, 126);
    const uint16_t lower = MA_RGB565(156, 43, 113);
    const uint16_t inside = MA_RGB565(39, 22, 58);
    const uint16_t tooth = MA_RGB565(229, 242, 233);
    ma_draw_envelope_mouth(
        canvas,
        pose,
        edge,
        lower,
        inside,
        tooth,
        upper,
        3,
        1,
        true,
        true,
        pose->tongue > 80U);
    /*
     * Two short planar highlights communicate the separate JALI lip axis
     * without turning the aperture into a diamond.
     */
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int half = pose->mouth_width / 2;
    if (pose->mouth_open > 3) {
        const int endpoint_average =
            (cy + pose->mouth_corner_left +
             cy + pose->mouth_corner_right) /
            2;
        const int upper_control =
            2 * (cy + pose->mouth_smile / 3) -
            endpoint_average - pose->mouth_open * 4 / 5;
        const int upper_center = ma_curve_y(
            cy + pose->mouth_corner_left,
            upper_control,
            cy + pose->mouth_corner_right,
            128,
            true);
        ma_line(
            canvas,
            cx - half + 5,
            cy + pose->mouth_corner_left - 2,
            cx - 2,
            upper_center - 2,
            upper);
        ma_line(
            canvas,
            cx + 2,
            upper_center - 2,
            cx + half - 5,
            cy + pose->mouth_corner_right - 2,
            upper);
    }
}

static void ma_render_jali(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(22, 19, 42);
    const uint16_t halo = MA_RGB565(45, 39, 77);
    const uint16_t shoulder = MA_RGB565(63, 50, 103);
    const uint16_t dark = MA_RGB565(39, 28, 66);
    const uint16_t mid = MA_RGB565(104, 72, 137);
    const uint16_t light = MA_RGB565(169, 123, 175);
    const uint16_t face = MA_RGB565(128, 90, 153);
    const uint16_t ink = MA_RGB565(27, 21, 48);
    const uint16_t glow = MA_RGB565(117, 244, 226);
    const uint16_t cheek = MA_RGB565(222, 80, 153);
    ma_clear(canvas, background);
    ma_ellipse(canvas, 80, 57, 69, 58, halo);
    ma_triangle(canvas, 13, 120, 52, 90, 80, 120, shoulder);
    ma_triangle(canvas, 147, 120, 108, 90, 80, 120, mid);
    const int hx = 80;
    const int hy = 58;
    ma_triangle(canvas, hx, hy - 54, hx - 52, hy - 22, hx, hy + 56, dark);
    ma_triangle(canvas, hx, hy - 54, hx, hy + 56, hx + 52, hy - 22, mid);
    ma_triangle(canvas, hx - 52, hy - 22, hx - 42, hy + 28, hx, hy + 56, mid);
    ma_triangle(canvas, hx + 52, hy - 22, hx, hy + 56, hx + 42, hy + 28, light);
    ma_triangle(canvas, hx, hy - 50, hx - 43, hy - 20, hx, hy + 4, face);
    ma_triangle(canvas, hx, hy - 50, hx, hy + 4, hx + 43, hy - 20, light);
    ma_triangle(canvas, hx - 43, hy - 20, hx, hy + 4, hx - 40, hy + 28, face);
    ma_triangle(canvas, hx + 43, hy - 20, hx + 40, hy + 28, hx, hy + 4, mid);
    ma_line(canvas, hx, hy - 45, hx, hy + 48, glow);

    ma_draw_brow(
        canvas, pose, 55, pose->brow_left, pose->brow_slant_left, 3, glow);
    ma_draw_brow(
        canvas, pose, 105, pose->brow_right, pose->brow_slant_right, 3, glow);
    ma_draw_jali_eye(
        canvas, pose, 55, pose->eye_left_open, ink, glow);
    ma_draw_jali_eye(
        canvas, pose, 105, pose->eye_right_open, ink, glow);
    ma_triangle(
        canvas,
        ma_feature_x(pose, 80, 57),
        ma_feature_y(pose, 57),
        ma_feature_x(pose, 74, 69),
        ma_feature_y(pose, 69),
        ma_feature_x(pose, 85, 68),
        ma_feature_y(pose, 68),
        light);
    ma_draw_cheeks(canvas, pose, cheek, false);
    ma_draw_detail_mark(canvas, pose, glow);
    ma_draw_jali_mouth(canvas, pose);
}

static void ma_draw_ribbon_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t dark = MA_RGB565(80, 20, 47);
    const uint16_t lip = MA_RGB565(184, 38, 86);
    const uint16_t light = MA_RGB565(255, 105, 135);
    const uint16_t inside = MA_RGB565(57, 16, 42);
    const uint16_t teeth = MA_RGB565(255, 244, 218);
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int half = pose->mouth_width / 2;
    const int open = pose->mouth_open;
    const int left_y = cy + pose->mouth_corner_left;
    const int right_y = cy + pose->mouth_corner_right;
    const int upper_control = cy - open / 2 + pose->mouth_smile / 3;
    const int lower_control = cy + open / 2 + pose->mouth_smile / 3;
    ma_draw_envelope_mouth(
        canvas,
        pose,
        dark,
        dark,
        inside,
        teeth,
        light,
        2,
        1,
        false,
        true,
        false);
    ma_quadratic(
        canvas, cx - half, left_y, cx, upper_control,
        cx + half, right_y, 5, dark);
    ma_quadratic(
        canvas, cx - half + 1, left_y - 1, cx, upper_control,
        cx + half - 1, right_y - 1, 3, lip);
    ma_quadratic(
        canvas, cx - half, left_y, cx, lower_control,
        cx + half, right_y, 5, dark);
    ma_quadratic(
        canvas, cx - half + 1, left_y, cx, lower_control - 1,
        cx + half - 1, right_y, 3, lip);
    ma_quadratic(
        canvas,
        cx - half / 2,
        upper_control + 1,
        cx,
        upper_control - 1,
        cx + half / 3,
        upper_control + 1,
        1,
        light);
}

static void ma_render_ribbon(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(58, 25, 69);
    const uint16_t curtain = MA_RGB565(83, 31, 85);
    const uint16_t gold = MA_RGB565(242, 180, 73);
    const uint16_t dress = MA_RGB565(109, 25, 68);
    const uint16_t skin = MA_RGB565(246, 191, 161);
    const uint16_t light = MA_RGB565(255, 218, 188);
    const uint16_t hair = MA_RGB565(75, 34, 45);
    const uint16_t ink = MA_RGB565(72, 31, 49);
    const uint16_t white = MA_RGB565(255, 245, 225);
    const uint16_t iris = MA_RGB565(80, 113, 103);
    const uint16_t blush = MA_RGB565(230, 109, 120);
    ma_clear(canvas, background);
    ma_triangle(canvas, 0, 0, 35, 0, 10, 120, curtain);
    ma_triangle(canvas, 160, 0, 125, 0, 150, 120, curtain);
    ma_rect(canvas, 0, 7, 160, 3, gold);
    ma_ellipse(canvas, 80, 119, 55, 28, dress);
    const int hx = 80;
    const int hy = 58;
    ma_ellipse(canvas, hx, hy - 2, 49, 54, hair);
    ma_ellipse(canvas, hx, hy + 3, 44, 49, skin);
    ma_ellipse(canvas, hx - 10, hy - 5, 27, 36, light);
    ma_triangle(canvas, hx - 45, hy - 26, hx - 9, hy - 52, hx - 2, hy - 32, hair);
    ma_triangle(canvas, hx + 45, hy - 26, hx + 9, hy - 52, hx + 2, hy - 32, hair);
    ma_quadratic(
        canvas, hx - 39, hy - 27, hx, hy - 59, hx + 39, hy - 27, 4, hair);

    ma_draw_brow(
        canvas, pose, 56, pose->brow_left, pose->brow_slant_left, 3, hair);
    ma_draw_brow(
        canvas, pose, 104, pose->brow_right, pose->brow_slant_right, 3, hair);
    ma_draw_soft_eye(
        canvas, pose, 56, pose->eye_left_open, ink, white, iris, true);
    ma_draw_soft_eye(
        canvas, pose, 104, pose->eye_right_open, ink, white, iris, true);
    ma_quadratic(
        canvas,
        ma_feature_x(pose, 76, 61),
        ma_feature_y(pose, 61),
        ma_feature_x(pose, 80, 67),
        ma_feature_y(pose, 67),
        ma_feature_x(pose, 85, 64),
        ma_feature_y(pose, 64),
        2,
        MA_RGB565(203, 128, 112));
    ma_draw_cheeks(canvas, pose, blush, false);
    ma_draw_detail_mark(canvas, pose, gold);
    ma_draw_ribbon_mouth(canvas, pose);
}

static void ma_draw_creature_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t outline = MA_RGB565(35, 60, 55);
    const uint16_t inside = MA_RGB565(64, 31, 51);
    const uint16_t gum = MA_RGB565(208, 73, 100);
    const uint16_t tooth = MA_RGB565(255, 244, 199);
    const uint16_t tongue = MA_RGB565(244, 91, 119);
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int half = pose->mouth_width / 2;
    ma_draw_envelope_mouth(
        canvas,
        pose,
        outline,
        gum,
        inside,
        tooth,
        tongue,
        4,
        1,
        false,
        false,
        true);
    if (pose->mouth_open > 4 && pose->teeth > 56U) {
        const int teeth_count = 5 + pose->mouth_width / 18;
        const int usable = half * 2 - 12;
        const int endpoint_average =
            (cy + pose->mouth_corner_left +
             cy + pose->mouth_corner_right) /
            2;
        const int upper_mid =
            2 * (cy + pose->mouth_smile / 3) -
            endpoint_average - pose->mouth_open * 4 / 5;
        for (int tooth_index = 0;
             tooth_index < teeth_count;
             ++tooth_index) {
            const int offset =
                -half + 6 + tooth_index * usable /
                                  (teeth_count > 1 ? teeth_count - 1 : 1);
            const int t_q8 = (offset + half) * 256 / (half * 2);
            const int upper = ma_curve_y(
                cy + pose->mouth_corner_left,
                upper_mid,
                cy + pose->mouth_corner_right,
                t_q8,
                false);
            const int tooth_height =
                3 + (pose->teeth > 80U ? pose->teeth / 75 : 0);
            ma_triangle(
                canvas,
                cx + offset - 3,
                upper + 1,
                cx + offset + 3,
                upper + 1,
                cx + offset,
                upper + 1 + tooth_height,
                tooth);
        }
    }
}

static void ma_render_teeth_tongue(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(186, 226, 192);
    const uint16_t shadow = MA_RGB565(101, 169, 137);
    const uint16_t body = MA_RGB565(63, 137, 111);
    const uint16_t face = MA_RGB565(103, 184, 132);
    const uint16_t belly = MA_RGB565(159, 214, 150);
    const uint16_t horn = MA_RGB565(249, 209, 118);
    const uint16_t ink = MA_RGB565(31, 69, 61);
    const uint16_t white = MA_RGB565(255, 248, 216);
    const uint16_t iris = MA_RGB565(228, 103, 75);
    const uint16_t blush = MA_RGB565(230, 112, 117);
    ma_clear(canvas, background);
    ma_ellipse(canvas, 80, 121, 61, 30, body);
    ma_ellipse(canvas, 80, 119, 29, 18, belly);
    const int hx = 80;
    const int hy = 58;
    ma_triangle(canvas, hx - 37, hy - 38, hx - 27, hy - 59, hx - 17, hy - 38, horn);
    ma_triangle(canvas, hx + 37, hy - 38, hx + 27, hy - 59, hx + 17, hy - 38, horn);
    ma_ellipse(canvas, hx - 47, hy - 10, 14, 20, body);
    ma_ellipse(canvas, hx + 47, hy - 10, 14, 20, body);
    ma_round_rect(canvas, hx - 48, hy - 48, 96, 101, 31, shadow);
    ma_round_rect(canvas, hx - 44, hy - 47, 88, 98, 29, face);
    ma_ellipse(canvas, hx - 11, hy - 9, 29, 36, belly);
    ma_ellipse(canvas, hx - 29, hy - 28, 9, 5, MA_RGB565(126, 198, 143));
    ma_ellipse(canvas, hx + 25, hy - 37, 6, 4, MA_RGB565(126, 198, 143));

    ma_draw_brow(
        canvas, pose, 54, pose->brow_left, pose->brow_slant_left, 4, ink);
    ma_draw_brow(
        canvas, pose, 106, pose->brow_right, pose->brow_slant_right, 4, ink);
    ma_draw_soft_eye(
        canvas, pose, 54, pose->eye_left_open, ink, white, iris, false);
    ma_draw_soft_eye(
        canvas, pose, 106, pose->eye_right_open, ink, white, iris, false);
    const int nostril_y = ma_feature_y(pose, 66);
    ma_ellipse(
        canvas, ma_feature_x(pose, 75, 66), nostril_y, 2, 2, ink);
    ma_ellipse(
        canvas, ma_feature_x(pose, 85, 66), nostril_y, 2, 2, ink);
    ma_draw_cheeks(canvas, pose, blush, false);
    ma_draw_detail_mark(canvas, pose, horn);
    ma_draw_creature_mouth(canvas, pose);
}

static void ma_draw_led_eye(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    int center_x,
    uint8_t openness,
    uint16_t off,
    uint16_t on,
    uint16_t hot)
{
    const int x = ma_feature_x(pose, center_x, pose->eye_y);
    const int y = ma_feature_y(pose, pose->eye_y);
    const int rows = (int)ma_clamp(openness / 3, 1, 5);
    for (int row = -2; row <= 2; ++row) {
        for (int column = -3; column <= 3; ++column) {
            const int distance_y = row < 0 ? -row : row;
            const bool active = distance_y < (rows + 1) / 2;
            uint16_t color = active ? on : off;
            if (active && column == pose->gaze_x / 3 &&
                row == pose->gaze_y / 3) {
                color = hot;
            }
            ma_rect(
                canvas,
                x + column * 4 - 1,
                y + row * 4 - 1,
                3,
                3,
                color);
        }
    }
}

static void ma_draw_led_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t off = MA_RGB565(18, 59, 64);
    const uint16_t cool = MA_RGB565(32, 221, 202);
    const uint16_t warm = MA_RGB565(247, 190, 54);
    const uint16_t hot = MA_RGB565(250, 86, 74);
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int columns = 9;
    const int spacing = 7;
    const int max_rows = 5;
    static const uint8_t COLUMN_THRESHOLD_Q8[9] = {
        48U, 160U, 16U, 208U, 96U, 208U, 16U, 160U, 48U,
    };
    for (int column = 0; column < columns; ++column) {
        const int centered = column - columns / 2;
        const int t_q8 = column * 256 / (columns - 1);
        const int envelope_q8 =
            pose->mouth_open * 256 / 5 +
            (pose->mouth_width > 48 ? 256 : 0) -
            (centered < 0 ? -centered : centered) *
                pose->mouth_round * 256 / 75 +
            pose->arousal * 256 / 96;
        const int smile_shift =
            ma_curve_y(
                cy + pose->mouth_corner_left,
                cy + pose->mouth_smile / 3,
                cy + pose->mouth_corner_right,
                t_q8,
                false) -
            cy;
        for (int row = 0; row < max_rows; ++row) {
            const int y = cy + (row - 2) * 5 + smile_shift;
            uint16_t color = off;
            if (row == 0 ||
                envelope_q8 >=
                    row * 256 + COLUMN_THRESHOLD_Q8[column]) {
                color = row >= 4 ? hot : (row >= 3 ? warm : cool);
            }
            ma_round_rect(
                canvas,
                cx + centered * spacing - 2,
                y - 2,
                5,
                4,
                1,
                color);
        }
    }
}

static void ma_render_led(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(9, 27, 37);
    const uint16_t shadow = MA_RGB565(13, 43, 54);
    const uint16_t shell_dark = MA_RGB565(49, 86, 97);
    const uint16_t shell = MA_RGB565(92, 139, 145);
    const uint16_t shell_light = MA_RGB565(151, 188, 184);
    const uint16_t screen = MA_RGB565(12, 36, 48);
    const uint16_t off = MA_RGB565(18, 59, 64);
    const uint16_t on = MA_RGB565(25, 212, 198);
    const uint16_t hot = MA_RGB565(198, 255, 217);
    const uint16_t cheek = MA_RGB565(245, 157, 48);
    ma_clear(canvas, background);
    ma_rect(canvas, 0, 105, 160, 15, shadow);
    ma_quad(canvas, 32, 120, 47, 94, 113, 94, 128, 120, shell_dark);
    const int hx = 80;
    const int hy = 58;
    ma_round_rect(canvas, hx - 58, hy - 44, 116, 91, 15, shell_dark);
    ma_round_rect(canvas, hx - 53, hy - 48, 106, 91, 13, shell);
    ma_rect(canvas, hx - 42, hy - 44, 84, 5, shell_light);
    ma_round_rect(canvas, hx - 46, hy - 35, 92, 68, 9, screen);
    ma_round_rect(canvas, hx - 62, hy - 14, 10, 27, 3, shell);
    ma_round_rect(canvas, hx + 52, hy - 14, 10, 27, 3, shell);
    ma_rect(canvas, hx - 17, hy - 53, 34, 6, shell_light);
    ma_rect(canvas, hx - 3, hy - 60, 6, 8, shell_light);
    ma_rect(canvas, hx - 2, hy - 66, 4, 6, hot);
    ma_draw_led_eye(
        canvas, pose, 54, pose->eye_left_open, off, on, hot);
    ma_draw_led_eye(
        canvas, pose, 106, pose->eye_right_open, off, on, hot);
    ma_draw_brow(
        canvas, pose, 54, pose->brow_left, pose->brow_slant_left, 2, on);
    ma_draw_brow(
        canvas, pose, 106, pose->brow_right, pose->brow_slant_right, 2, on);
    if (pose->cheek > 25U) {
        const int bars = 1 + pose->cheek / 80;
        for (int bar = 0; bar < bars; ++bar) {
            ma_rect(
                canvas,
                ma_feature_x(pose, 35 + bar * 4, 67),
                ma_feature_y(pose, 67),
                2,
                5,
                cheek);
            ma_rect(
                canvas,
                ma_feature_x(pose, 123 - bar * 4, 67),
                ma_feature_y(pose, 67),
                2,
                5,
                cheek);
        }
    }
    ma_draw_detail_mark(canvas, pose, shell_light);
    ma_draw_led_mouth(canvas, pose);
}

static void ma_draw_origami_eye(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose,
    int center_x,
    uint8_t openness,
    uint16_t ink,
    uint16_t iris)
{
    const int x = ma_feature_x(pose, center_x, pose->eye_y);
    const int y = ma_feature_y(pose, pose->eye_y);
    const int open = openness / 3 + 2;
    ma_triangle(canvas, x - 17, y - 2, x + 14, y - open, x + 10, y + open, ink);
    ma_triangle(canvas, x - 17, y - 2, x + 10, y + open, x - 5, y + 1, iris);
    const int pupil_x = x + pose->gaze_x / 2;
    ma_triangle(
        canvas,
        pupil_x - 2,
        y - 3,
        pupil_x + 3,
        y,
        pupil_x - 2,
        y + 3,
        MA_RGB565(241, 220, 145));
}

static void ma_draw_origami_mouth(
    ma_canvas_t *canvas,
    const face_mouth_actor_pose_t *pose)
{
    const uint16_t fold = MA_RGB565(122, 67, 83);
    const uint16_t lip = MA_RGB565(181, 74, 91);
    const uint16_t inside = MA_RGB565(52, 34, 54);
    const uint16_t light = MA_RGB565(242, 181, 152);
    const int cx = ma_feature_x(pose, pose->mouth_x, pose->mouth_y);
    const int cy = ma_feature_y(pose, pose->mouth_y);
    const int half = pose->mouth_width / 2;
    ma_draw_envelope_mouth(
        canvas,
        pose,
        lip,
        fold,
        inside,
        light,
        lip,
        3,
        1,
        true,
        true,
        pose->tongue > 96U);
    /* Creases continue from the pinned corners into the paper muzzle. */
    ma_line(
        canvas,
        cx - half,
        cy + pose->mouth_corner_left,
        cx - half - 7,
        cy + pose->mouth_corner_left - 3,
        fold);
    ma_line(
        canvas,
        cx + half,
        cy + pose->mouth_corner_right,
        cx + half + 7,
        cy + pose->mouth_corner_right - 3,
        fold);
}

static void ma_render_origami(
    ma_canvas_t *canvas, const face_mouth_actor_pose_t *pose)
{
    const uint16_t background = MA_RGB565(28, 39, 61);
    const uint16_t moon = MA_RGB565(56, 70, 91);
    const uint16_t body = MA_RGB565(151, 75, 79);
    const uint16_t dark = MA_RGB565(117, 56, 69);
    const uint16_t mid = MA_RGB565(205, 115, 101);
    const uint16_t light = MA_RGB565(240, 163, 128);
    const uint16_t cream = MA_RGB565(250, 210, 165);
    const uint16_t ink = MA_RGB565(46, 35, 55);
    const uint16_t iris = MA_RGB565(132, 201, 184);
    const uint16_t cheek = MA_RGB565(224, 101, 110);
    ma_clear(canvas, background);
    ma_ellipse(canvas, 80, 48, 61, 55, moon);
    ma_triangle(canvas, 21, 120, 62, 88, 80, 120, dark);
    ma_triangle(canvas, 139, 120, 98, 88, 80, 120, body);
    const int hx = 80;
    const int hy = 58;
    ma_triangle(canvas, hx - 45, hy - 19, hx - 38, hy - 55, hx - 7, hy - 43, dark);
    ma_triangle(canvas, hx + 45, hy - 19, hx + 38, hy - 55, hx + 7, hy - 43, body);
    ma_triangle(canvas, hx - 35, hy - 44, hx - 31, hy - 30, hx - 10, hy - 39, cream);
    ma_triangle(canvas, hx + 35, hy - 44, hx + 31, hy - 30, hx + 10, hy - 39, cream);
    ma_triangle(canvas, hx, hy - 49, hx - 48, hy - 17, hx, hy + 57, mid);
    ma_triangle(canvas, hx, hy - 49, hx, hy + 57, hx + 48, hy - 17, light);
    ma_triangle(canvas, hx - 48, hy - 17, hx - 36, hy + 29, hx, hy + 57, dark);
    ma_triangle(canvas, hx + 48, hy - 17, hx, hy + 57, hx + 36, hy + 29, mid);
    ma_triangle(canvas, hx, hy - 45, hx - 34, hy - 13, hx, hy + 5, light);
    ma_triangle(canvas, hx, hy - 45, hx, hy + 5, hx + 34, hy - 13, cream);
    ma_triangle(canvas, hx - 34, hy - 13, hx, hy + 5, hx - 31, hy + 29, mid);
    ma_triangle(canvas, hx + 34, hy - 13, hx + 31, hy + 29, hx, hy + 5, light);

    ma_draw_brow(
        canvas, pose, 55, pose->brow_left, pose->brow_slant_left, 2, ink);
    ma_draw_brow(
        canvas, pose, 105, pose->brow_right, pose->brow_slant_right, 2, ink);
    ma_draw_origami_eye(
        canvas, pose, 55, pose->eye_left_open, ink, iris);
    ma_draw_origami_eye(
        canvas, pose, 105, pose->eye_right_open, ink, iris);
    ma_triangle(
        canvas,
        ma_feature_x(pose, 74, 63),
        ma_feature_y(pose, 63),
        ma_feature_x(pose, 86, 63),
        ma_feature_y(pose, 63),
        ma_feature_x(pose, 80, 70),
        ma_feature_y(pose, 70),
        ink);
    ma_draw_cheeks(canvas, pose, cheek, false);
    ma_draw_detail_mark(canvas, pose, cream);
    ma_draw_origami_mouth(canvas, pose);
}

bool face_mouth_actors_render_resolved(
    face_mouth_actor_profile_t profile,
    const face_mouth_actor_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_actor_landmarks_t *landmarks)
{
    if ((unsigned)profile >= FACE_MOUTH_ACTOR_COUNT ||
        pose == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_MOUTH_ACTORS_PIXEL_COUNT) {
        return false;
    }
    ma_canvas_t canvas = {.pixels = rgb565};
    switch (profile) {
    case FACE_MOUTH_ACTOR_PRESTON:
        ma_render_preston(&canvas, pose);
        break;
    case FACE_MOUTH_ACTOR_JALI:
        ma_render_jali(&canvas, pose);
        break;
    case FACE_MOUTH_ACTOR_RIBBON:
        ma_render_ribbon(&canvas, pose);
        break;
    case FACE_MOUTH_ACTOR_TEETH_TONGUE:
        ma_render_teeth_tongue(&canvas, pose);
        break;
    case FACE_MOUTH_ACTOR_LED_VU:
        ma_render_led(&canvas, pose);
        break;
    case FACE_MOUTH_ACTOR_ORIGAMI:
        ma_render_origami(&canvas, pose);
        break;
    default:
        return false;
    }
    if (landmarks != NULL) {
        ma_landmarks_for_profile(profile, landmarks);
    }
    return true;
}

bool face_mouth_actors_render_checked(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_actor_landmarks_t *landmarks)
{
    face_mouth_actor_pose_t pose;
    return face_mouth_actors_resolve(
               profile, render_key, sample_clock, &pose) &&
           face_mouth_actors_render_resolved(
               profile, &pose, rgb565, pixel_capacity, landmarks);
}

bool face_mouth_actors_render(
    face_mouth_actor_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    return face_mouth_actors_render_checked(
        profile,
        render_key,
        sample_clock,
        rgb565,
        pixel_capacity,
        NULL);
}
