#include "face_pixel_redux_actors.h"

#include <string.h>

#include "face_pose.h"
#include "face_stage.h"

#define PR_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    PR_SAFE = 2,
    PR_EXPRESSION_COUNT = 11,
    PR_ICON_NONE = 0,
    PR_ICON_HEART,
    PR_ICON_LAUGH,
    PR_ICON_SWEAT,
    PR_ICON_BANG,
    PR_ICON_THOUGHT,
    PR_ICON_QUESTION,
    PR_ICON_FOCUS,
    PR_ICON_SLEEP,
    PR_ICON_SPARK,
    PR_ICON_BLUSH,
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
    uint8_t eye_w;
    uint8_t eye_h;
    int8_t mouth_y;
} pr_actor_def_t;

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
} pr_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
} pr_mouth_shape_t;

typedef struct {
    uint16_t *pixels;
} pr_canvas_t;

static const pr_actor_def_t PR_ACTORS[FACE_PIXEL_REDUX_ACTOR_COUNT] = {
    [FACE_PIXEL_REDUX_EGA_QUEST] = {
        "pixel-redux-ega-wayfarer",
        "Pixel Redux · EGA Wayfarer",
        0U,
        FACE_PIXEL_REDUX_MOUTH_CELS,
        16U,
        4U,
        {31, 49},
        26,
        11U,
        8U,
        41,
    },
    [FACE_PIXEL_REDUX_VGA_ELDER] = {
        "pixel-redux-vga-oracle",
        "Pixel Redux · VGA Oracle",
        1U,
        FACE_PIXEL_REDUX_MOUTH_SHADED,
        32U,
        7U,
        {29, 51},
        25,
        13U,
        9U,
        41,
    },
    [FACE_PIXEL_REDUX_TALKIE_CLOSEUP] = {
        "pixel-redux-talkie-mechanic",
        "Pixel Redux · CD-Talkie Mechanic",
        2U,
        FACE_PIXEL_REDUX_MOUTH_CINEMATIC,
        48U,
        8U,
        {27, 52},
        23,
        15U,
        10U,
        43,
    },
    [FACE_PIXEL_REDUX_PIXEL_AUTOMATON] = {
        "pixel-redux-arcade-automaton",
        "Pixel Redux · Arcade Automaton",
        3U,
        FACE_PIXEL_REDUX_MOUTH_LED,
        12U,
        5U,
        {28, 52},
        25,
        15U,
        10U,
        43,
    },
    [FACE_PIXEL_REDUX_POCKET_RPG] = {
        "pixel-redux-pocket-mossling",
        "Pixel Redux · Pocket Mossling",
        5U,
        FACE_PIXEL_REDUX_MOUTH_CHIBI,
        4U,
        4U,
        {32, 48},
        28,
        10U,
        9U,
        40,
    },
};

/*
 * Bold authored targets make the eleven stage directions survive at a 40x30
 * contact-sheet tile. They are geometry recipes, never palette swaps.
 */
static const pr_expression_t PR_EXPRESSIONS[PR_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        {210U, 210U}, 0, 0, 0, {0, 0}, {0, 0},
        18U, 142U, 20U, {0, 0}, 0, 0, 14U, PR_ICON_NONE,
    },
    [FACE_EXPRESSION_WARM] = {
        {176U, 176U}, 2, 0, 1, {-1, -1}, {-2, -2},
        28U, 188U, 12U, {4, 4}, 0, 0, 92U, PR_ICON_HEART,
    },
    [FACE_EXPRESSION_JOY] = {
        {58U, 58U}, 4, 0, 1, {-2, -2}, {-3, -3},
        150U, 236U, 8U, {8, 8}, 0, -1, 230U, PR_ICON_LAUGH,
    },
    [FACE_EXPRESSION_CONCERN] = {
        {190U, 220U}, -1, -2, 2, {2, 2}, {-5, -5},
        34U, 136U, 18U, {-4, -4}, -1, 1, 70U, PR_ICON_SWEAT,
    },
    [FACE_EXPRESSION_SURPRISE] = {
        {255U, 255U}, 3, 0, -1, {-6, -6}, {-6, -6},
        236U, 78U, 250U, {0, 0}, 0, -1, 0U, PR_ICON_BANG,
    },
    [FACE_EXPRESSION_THOUGHTFUL] = {
        {190U, 90U}, 0, -4, -3, {-3, 2}, {-1, 1},
        30U, 122U, 54U, {-3, 2}, 1, 0, 38U, PR_ICON_THOUGHT,
    },
    [FACE_EXPRESSION_SKEPTICAL] = {
        {82U, 220U}, 2, 4, 0, {-5, 3}, {1, -4},
        22U, 182U, 12U, {-5, 4}, -1, 0, 34U, PR_ICON_QUESTION,
    },
    [FACE_EXPRESSION_DETERMINED] = {
        {104U, 104U}, 4, 0, 1, {2, 2}, {5, 5},
        18U, 210U, 4U, {-3, -3}, 0, 1, 26U, PR_ICON_FOCUS,
    },
    [FACE_EXPRESSION_SLEEPY] = {
        {36U, 28U}, -2, -2, 3, {3, 3}, {2, 2},
        76U, 104U, 172U, {1, 1}, 1, 2, 18U, PR_ICON_SLEEP,
    },
    [FACE_EXPRESSION_EXCITED] = {
        {255U, 255U}, 5, 0, -2, {-7, -7}, {-6, -6},
        224U, 244U, 22U, {7, 7}, 0, -2, 184U, PR_ICON_SPARK,
    },
    [FACE_EXPRESSION_EMBARRASSED] = {
        {98U, 164U}, -1, 4, 3, {-2, 0}, {-3, -1},
        28U, 146U, 24U, {5, 0}, 1, 1, 255U, PR_ICON_BLUSH,
    },
};

static const pr_mouth_shape_t PR_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {230U, 178U, 20U, 0U, 62U, 58U},
    [FACE_VISEME_E] = {92U, 245U, 4U, 0U, 210U, 10U},
    [FACE_VISEME_I] = {54U, 226U, 2U, 0U, 140U, 6U},
    [FACE_VISEME_O] = {210U, 90U, 250U, 0U, 30U, 22U},
    [FACE_VISEME_U] = {126U, 66U, 255U, 0U, 12U, 28U},
    [FACE_VISEME_PP] = {2U, 164U, 14U, 255U, 0U, 0U},
    [FACE_VISEME_SS] = {44U, 240U, 2U, 24U, 255U, 0U},
    [FACE_VISEME_TH] = {82U, 192U, 10U, 0U, 116U, 255U},
    [FACE_VISEME_DD] = {78U, 180U, 8U, 0U, 222U, 82U},
    [FACE_VISEME_FF] = {32U, 206U, 4U, 78U, 255U, 0U},
    [FACE_VISEME_KK] = {142U, 174U, 28U, 0U, 46U, 92U},
    [FACE_VISEME_NN] = {48U, 166U, 12U, 16U, 118U, 62U},
    [FACE_VISEME_RR] = {112U, 140U, 132U, 0U, 48U, 42U},
    [FACE_VISEME_CH] = {92U, 206U, 22U, 8U, 166U, 22U},
    [FACE_VISEME_SIL] = {4U, 136U, 18U, 235U, 0U, 0U},
};

static int32_t pr_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t pr_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t pr_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t pr_wave(uint32_t sample_clock, uint32_t period)
{
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    return phase < half
        ? (int32_t)(phase * 254U / half) - 127
        : 127 - (int32_t)((phase - half) * 254U / half);
}

static bool pr_actor_valid(face_pixel_redux_actor_t actor)
{
    return (unsigned)actor < (unsigned)FACE_PIXEL_REDUX_ACTOR_COUNT;
}

/*
 * Every primitive is clipped to a two-logical-pixel safe border. The border
 * corresponds to four native pixels and remains the exact clear colour.
 */
static void pr_put(pr_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x < PR_SAFE || x >= FACE_PIXEL_REDUX_LOGICAL_WIDTH - PR_SAFE ||
        y < PR_SAFE || y >= FACE_PIXEL_REDUX_LOGICAL_HEIGHT - PR_SAFE) {
        return;
    }
    const int physical_x = x * FACE_PIXEL_REDUX_SCALE;
    const int physical_y = y * FACE_PIXEL_REDUX_SCALE;
    const size_t top =
        (size_t)physical_y * FACE_PIXEL_REDUX_WIDTH +
        (size_t)physical_x;
    const size_t bottom = top + FACE_PIXEL_REDUX_WIDTH;
    canvas->pixels[top] = color;
    canvas->pixels[top + 1U] = color;
    canvas->pixels[bottom] = color;
    canvas->pixels[bottom + 1U] = color;
}

static void pr_clear(pr_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_PIXEL_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void pr_rect(
    pr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left = (int)pr_clamp(
        x, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_WIDTH - PR_SAFE);
    const int right = (int)pr_clamp(
        x + width, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_WIDTH - PR_SAFE);
    const int top = (int)pr_clamp(
        y, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_HEIGHT - PR_SAFE);
    const int bottom = (int)pr_clamp(
        y + height, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_HEIGHT - PR_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            pr_put(canvas, xx, yy, color);
        }
    }
}

static void pr_frame(
    pr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int thickness,
    uint16_t color)
{
    pr_rect(canvas, x, y, width, thickness, color);
    pr_rect(
        canvas, x, y + height - thickness, width, thickness, color);
    pr_rect(canvas, x, y, thickness, height, color);
    pr_rect(
        canvas, x + width - thickness, y, thickness, height, color);
}

static void pr_line(
    pr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = pr_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -pr_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pr_put(canvas, x0, y0, color);
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

static void pr_thick_line(
    pr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        pr_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void pr_ellipse(
    pr_canvas_t *canvas,
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
        pr_rect(canvas, cx - span, cy + y, span * 2 + 1, 1, color);
    }
}

static int32_t pr_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void pr_triangle(
    pr_canvas_t *canvas,
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
    left = (int)pr_clamp(
        left, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_WIDTH - PR_SAFE - 1);
    right = (int)pr_clamp(
        right, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_WIDTH - PR_SAFE - 1);
    top = (int)pr_clamp(
        top, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_HEIGHT - PR_SAFE - 1);
    bottom = (int)pr_clamp(
        bottom, PR_SAFE, FACE_PIXEL_REDUX_LOGICAL_HEIGHT - PR_SAFE - 1);
    const int32_t orientation = pr_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = pr_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = pr_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = pr_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                pr_put(canvas, x, y, color);
            }
        }
    }
}

static void pr_quad(
    pr_canvas_t *canvas,
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
    pr_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    pr_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void pr_checker(
    pr_canvas_t *canvas,
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
            pr_put(canvas, xx, yy, cell == 0U ? second : first);
        }
    }
}

static uint8_t pr_viseme_index(uint8_t set, uint8_t raw)
{
    static const uint8_t VRM5[5] = {
        FACE_VISEME_AA,
        FACE_VISEME_I,
        FACE_VISEME_U,
        FACE_VISEME_E,
        FACE_VISEME_O,
    };
    static const uint8_t PRESTON9[9] = {
        FACE_VISEME_SIL,
        FACE_VISEME_PP,
        FACE_VISEME_SS,
        FACE_VISEME_E,
        FACE_VISEME_AA,
        FACE_VISEME_O,
        FACE_VISEME_U,
        FACE_VISEME_FF,
        FACE_VISEME_TH,
    };
    static const uint8_t MICROSOFT22[22] = {
        FACE_VISEME_SIL, FACE_VISEME_AA, FACE_VISEME_AA,
        FACE_VISEME_O, FACE_VISEME_E, FACE_VISEME_RR,
        FACE_VISEME_E, FACE_VISEME_U, FACE_VISEME_O,
        FACE_VISEME_AA, FACE_VISEME_O, FACE_VISEME_AA,
        FACE_VISEME_KK, FACE_VISEME_RR, FACE_VISEME_NN,
        FACE_VISEME_SS, FACE_VISEME_CH, FACE_VISEME_TH,
        FACE_VISEME_FF, FACE_VISEME_DD, FACE_VISEME_KK,
        FACE_VISEME_PP,
    };
    if (set == FACE_VISEME_SET_VRM5) {
        return VRM5[raw % 5U];
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        return PRESTON9[raw % 9U];
    }
    if (set == FACE_VISEME_SET_MICROSOFT22) {
        return MICROSOFT22[raw % 22U];
    }
    return raw < FACE_VISEME_COUNT
        ? raw
        : (uint8_t)(raw % FACE_VISEME_COUNT);
}

static pr_mouth_shape_t pr_blended_viseme(
    const face_render_key_t *key)
{
    const pr_mouth_shape_t first =
        PR_VISEMES[pr_viseme_index(key->viseme_set, key->viseme)];
    const pr_mouth_shape_t second =
        PR_VISEMES[
            pr_viseme_index(key->viseme_set, key->viseme_secondary)];
    pr_mouth_shape_t shape;
    shape.open =
        (uint8_t)pr_mix(first.open, second.open, key->viseme_blend);
    shape.width =
        (uint8_t)pr_mix(first.width, second.width, key->viseme_blend);
    shape.round =
        (uint8_t)pr_mix(first.round, second.round, key->viseme_blend);
    shape.press =
        (uint8_t)pr_mix(first.press, second.press, key->viseme_blend);
    shape.teeth =
        (uint8_t)pr_mix(first.teeth, second.teeth, key->viseme_blend);
    shape.tongue =
        (uint8_t)pr_mix(first.tongue, second.tongue, key->viseme_blend);
    return shape;
}

size_t face_pixel_redux_actor_count(void)
{
    return FACE_PIXEL_REDUX_ACTOR_COUNT;
}

const char *face_pixel_redux_actor_slug(
    face_pixel_redux_actor_t actor)
{
    return pr_actor_valid(actor) ? PR_ACTORS[actor].slug : NULL;
}

const char *face_pixel_redux_actor_name(
    face_pixel_redux_actor_t actor)
{
    return pr_actor_valid(actor) ? PR_ACTORS[actor].name : NULL;
}

bool face_pixel_redux_actor_info(
    face_pixel_redux_actor_t actor,
    face_pixel_redux_actor_info_t *info)
{
    if (!pr_actor_valid(actor) || info == NULL) {
        return false;
    }
    const pr_actor_def_t *definition = &PR_ACTORS[actor];
    info->slug = definition->slug;
    info->name = definition->name;
    info->legacy_profile_id = definition->legacy_id;
    info->mouth_kind = definition->mouth_kind;
    info->logical_width = FACE_PIXEL_REDUX_LOGICAL_WIDTH;
    info->logical_height = FACE_PIXEL_REDUX_LOGICAL_HEIGHT;
    info->palette_size = definition->palette_size;
    info->estimated_ops_per_pixel = definition->ops;
    return true;
}

bool face_pixel_redux_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_pixel_redux_actor_t *actor)
{
    if (actor == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        if (PR_ACTORS[raw].legacy_id == legacy_profile_id) {
            *actor = (face_pixel_redux_actor_t)raw;
            return true;
        }
    }
    return false;
}

bool face_pixel_redux_actor_resolve(
    face_pixel_redux_actor_t actor,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_pixel_redux_pose_t *pose)
{
    if (!pr_actor_valid(actor) || key == NULL || pose == NULL) {
        return false;
    }
    const pr_actor_def_t *definition = &PR_ACTORS[actor];
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, key, sizeof(pose->source));

    const uint8_t expression =
        key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION &&
        key->stage_expression < PR_EXPRESSION_COUNT
        ? key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const pr_expression_t *target = &PR_EXPRESSIONS[expression];
    const uint8_t weight = key->expression_weight;
    pose->stage_expression = expression;
    pose->expression_weight = weight;
    pose->activity = key->controls.expression;
    pose->speech_phase = key->speech_phase;
    pose->emotion_icon = target->icon;
    pose->attention = key->attention;
    pose->speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        key->controls.expression == FACE_ACTIVITY_SPEAKING ||
        key->speech_phase == FACE_SPEECH_STARTING ||
        key->speech_phase == FACE_SPEECH_ACTIVE ||
        key->speech_phase == FACE_SPEECH_ENDING;

    const int32_t wave = pr_wave(sample_clock, 6400U);
    pose->speech_pulse = pose->speaking
        ? (int16_t)(wave * (32 + key->audio_level) / (127 * 128))
        : 0;

    int32_t gaze_x =
        key->controls.look_x / 24 + key->head_yaw / 36 +
        pr_mix(0, target->gaze_x, weight);
    int32_t gaze_y =
        key->controls.look_y / 24 + key->head_pitch / 34 +
        pr_mix(0, target->gaze_y, weight);
    if (key->controls.expression == FACE_ACTIVITY_THINKING) {
        gaze_x -= 2;
        gaze_y -= 1;
    } else if (
        key->controls.expression == FACE_ACTIVITY_LISTENING) {
        gaze_y -= 1;
    } else if (
        key->controls.expression == FACE_ACTIVITY_IDLE) {
        gaze_x += 1;
    }
    gaze_x = pr_clamp(gaze_x, -5, 5);
    gaze_y = pr_clamp(gaze_y, -4, 4);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const uint8_t requested_open =
            eye == 0U
            ? key->controls.eye_left_open
            : key->controls.eye_right_open;
        const uint8_t requested_squint =
            eye == 0U ? key->eye_left_squint : key->eye_right_squint;
        const uint8_t target_open = target->eye_open[eye];
        const int width_delta = pr_mix(0, target->eye_width, weight);
        pose->eye_x[eye] = definition->eye_x[eye];
        pose->eye_y[eye] = definition->eye_y;
        pose->eye_w[eye] = (int16_t)pr_clamp(
            (int)definition->eye_w + width_delta, 7, 20);
        pose->eye_h[eye] = definition->eye_h;
        int open = pr_mix(requested_open, target_open, weight);
        /* Stage acting shapes the lid without erasing the live eye channel. */
        open += ((int)requested_open - 128) / 3;
        open -= requested_squint * 2 / 3;
        open += ((int)key->affect_arousal - 128) / 8;
        open += pose->speaking ? (int)key->audio_level / 24 - 3 : 0;
        open += pose->speaking ? pose->speech_pulse * 4 : 0;
        if (key->speech_phase == FACE_SPEECH_STARTING) {
            /* One-frame lid compression anticipates the first mouth cel. */
            open -= 28;
        } else if (key->speech_phase == FACE_SPEECH_ENDING) {
            open -= 10;
        }
        if ((key->controls.flags &
             FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            open = 0;
        }
        pose->eye_open[eye] = (int16_t)pr_clamp(
            1 + open * ((int)definition->eye_h - 1) / 255,
            1,
            definition->eye_h);
        const int pupil_radius = pr_clamp(
            1 + (int)key->attention / 150 -
                ((int)key->affect_arousal - 128) / 120,
            1,
            actor == FACE_PIXEL_REDUX_TALKIE_CLOSEUP ? 3 : 2);
        const int horizontal = pr_clamp(
            pose->eye_w[eye] / 2 - pupil_radius - 2, 1, 4);
        const int vertical = pr_clamp(
            pose->eye_open[eye] / 2 - pupil_radius, 0, 3);
        pose->pupil_x[eye] = (int16_t)(
            pose->eye_x[eye] +
            pr_clamp(gaze_x, -horizontal, horizontal));
        pose->pupil_y[eye] = (int16_t)(
            pose->eye_y[eye] +
            pr_clamp(gaze_y, -vertical, vertical));
        pose->pupil_radius[eye] = (int16_t)pupil_radius;

        const int target_outer = target->brow_outer[eye];
        const int target_inner = target->brow_inner[eye];
        const int input_outer =
            eye == 0U ? key->brow_outer_left : key->brow_outer_right;
        const int roll = key->head_roll / 42 * (eye == 0U ? -1 : 1);
        pose->brow_outer_y[eye] = (int16_t)pr_clamp(
            definition->eye_y - definition->eye_h / 2 - 3 +
                pr_mix(0, target_outer, weight) -
                key->controls.brow / 36 - input_outer / 30 + roll +
                key->affect_valence / 54,
            8,
            27);
        pose->brow_inner_y[eye] = (int16_t)pr_clamp(
            definition->eye_y - definition->eye_h / 2 - 3 +
                pr_mix(0, target_inner, weight) -
                key->controls.brow / 36 - key->brow_inner / 30 - roll -
                key->affect_valence / 54,
            8,
            27);
    }

    const pr_mouth_shape_t viseme = pr_blended_viseme(key);
    int open = pr_mix(
        key->controls.mouth_open, viseme.open, key->viseme_weight);
    int width = pr_mix(
        key->controls.mouth_width, viseme.width, key->viseme_weight);
    pose->mouth_round = (uint8_t)pr_mix(
        key->controls.mouth_round, viseme.round, key->viseme_weight);
    pose->mouth_press = (uint8_t)pr_mix(
        key->controls.mouth_press, viseme.press, key->viseme_weight);
    pose->teeth = (uint8_t)pr_mix(
        key->controls.mouth_teeth, viseme.teeth, key->viseme_weight);
    pose->tongue = (uint8_t)pr_mix(
        key->tongue, viseme.tongue, key->viseme_weight);

    /*
     * Stage directions author the resting silhouette. While speaking their
     * influence is reduced, preserving the PCM/viseme envelope beneath joy,
     * concern or other sustained acting.
     */
    const uint8_t mouth_weight =
        pose->speaking ? (uint8_t)(weight / 3U) : weight;
    open = pr_mix(open, target->mouth_open, mouth_weight);
    width = pr_mix(width, target->mouth_width, mouth_weight);
    pose->mouth_round = (uint8_t)pr_mix(
        pose->mouth_round, target->mouth_round, mouth_weight);
    /* Keep each direct control audible beneath heavy viseme/stage weights. */
    open += ((int)key->controls.mouth_open - 128) / 7;
    width += ((int)key->controls.mouth_width - 128) / 5;
    pose->mouth_round = (uint8_t)pr_clamp(
        (int)pose->mouth_round +
            ((int)key->controls.mouth_round - 128) / 3,
        0,
        255);
    pose->mouth_press = (uint8_t)pr_clamp(
        (int)pose->mouth_press +
            ((int)key->controls.mouth_press - 128) / 3,
        0,
        255);
    pose->teeth = (uint8_t)pr_mix(
        pose->teeth, key->controls.mouth_teeth, 80U);
    pose->tongue = (uint8_t)pr_mix(
        pose->tongue, key->tongue, 96U);
    open += pose->speaking ? (int)key->audio_level / 8 - 16 : 0;
    open -= pose->mouth_press / 5;
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        open += 16;
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        open -= 18;
    }
    open += pose->speech_pulse * 3;
    pose->mouth_x = 40;
    pose->mouth_y = definition->mouth_y;
    pose->mouth_w = (int16_t)pr_clamp(
        7 + width * 23 / 255 - pose->mouth_round / 36,
        actor == FACE_PIXEL_REDUX_POCKET_RPG ? 6 : 8,
        actor == FACE_PIXEL_REDUX_TALKIE_CLOSEUP ? 31 : 27);
    pose->mouth_h = (int16_t)pr_clamp(
        1 + open * 12 / 255 + pose->mouth_round / 90,
        1,
        actor == FACE_PIXEL_REDUX_TALKIE_CLOSEUP ? 14 : 12);
    pose->mouth_corner_y[0] = (int16_t)pr_clamp(
        -key->mouth_corner_left / 28 -
            key->affect_valence / 38 -
            pr_mix(0, target->mouth_corner[0], weight),
        -7,
        7);
    pose->mouth_corner_y[1] = (int16_t)pr_clamp(
        -key->mouth_corner_right / 28 -
            key->affect_valence / 38 -
            pr_mix(0, target->mouth_corner[1], weight),
        -7,
        7);
    pose->cheek = (uint8_t)pr_clamp(
        (int)key->cheek + pr_mix(0, target->cheek, weight), 0, 255);
    pose->body_lean_x = (int16_t)pr_clamp(
        key->body_lean_x / 35 +
            pr_mix(0, target->body_lean_x, weight),
        -4,
        4);
    pose->body_lean_y = (int16_t)pr_clamp(
        key->body_lean_y / 42 +
            pr_mix(0, target->body_lean_y, weight) +
            (key->controls.expression == FACE_ACTIVITY_THINKING ? 1 :
             key->controls.expression == FACE_ACTIVITY_IDLE ? 2 :
             key->controls.expression == FACE_ACTIVITY_LISTENING ? -1 : 0),
        -3,
        3);
    pose->head_yaw = (int16_t)pr_clamp(key->head_yaw / 38, -3, 3);
    pose->head_pitch = (int16_t)pr_clamp(key->head_pitch / 44, -2, 2);
    pose->head_roll = (int16_t)pr_clamp(key->head_roll / 36, -3, 3);
    pose->phoneme_shape = key->phoneme == FACE_PHONEME_NONE
        ? 0U
        : (uint8_t)(1U + key->phoneme % 5U);
    return true;
}

static void pr_draw_icon(
    pr_canvas_t *canvas,
    uint8_t icon,
    int x,
    int y,
    uint16_t ink,
    uint16_t accent)
{
    switch (icon) {
    case PR_ICON_HEART:
        pr_rect(canvas, x, y + 1, 2, 2, accent);
        pr_rect(canvas, x + 3, y + 1, 2, 2, accent);
        pr_rect(canvas, x - 1, y + 2, 7, 2, accent);
        pr_rect(canvas, x, y + 4, 5, 1, accent);
        pr_rect(canvas, x + 1, y + 5, 3, 1, accent);
        pr_put(canvas, x + 2, y + 6, accent);
        break;
    case PR_ICON_LAUGH:
        pr_line(canvas, x, y + 2, x + 3, y, accent);
        pr_line(canvas, x + 5, y, x + 8, y + 2, accent);
        pr_line(canvas, x, y + 5, x + 3, y + 7, accent);
        pr_line(canvas, x + 5, y + 7, x + 8, y + 5, accent);
        break;
    case PR_ICON_SWEAT:
        pr_triangle(
            canvas, x + 3, y, x, y + 6, x + 6, y + 6, accent);
        pr_put(canvas, x + 2, y + 3, ink);
        break;
    case PR_ICON_BANG:
        pr_rect(canvas, x + 2, y, 3, 6, accent);
        pr_rect(canvas, x + 2, y + 8, 3, 3, accent);
        break;
    case PR_ICON_THOUGHT:
        pr_ellipse(canvas, x + 3, y + 2, 3, 2, accent);
        pr_ellipse(canvas, x + 7, y + 3, 3, 2, accent);
        pr_put(canvas, x, y + 6, accent);
        pr_put(canvas, x - 2, y + 8, accent);
        break;
    case PR_ICON_QUESTION:
        pr_thick_line(canvas, x, y + 1, x + 3, y, 1, accent);
        pr_line(canvas, x + 3, y, x + 5, y + 2, accent);
        pr_line(canvas, x + 5, y + 2, x + 2, y + 5, accent);
        pr_rect(canvas, x + 1, y + 7, 2, 2, accent);
        break;
    case PR_ICON_FOCUS:
        pr_line(canvas, x, y + 3, x + 4, y, accent);
        pr_line(canvas, x + 6, y, x + 10, y + 3, accent);
        pr_line(canvas, x, y + 7, x + 4, y + 10, accent);
        pr_line(canvas, x + 6, y + 10, x + 10, y + 7, accent);
        break;
    case PR_ICON_SLEEP:
        pr_line(canvas, x, y, x + 5, y, accent);
        pr_line(canvas, x + 5, y, x, y + 5, accent);
        pr_line(canvas, x, y + 5, x + 5, y + 5, accent);
        pr_line(canvas, x + 6, y + 7, x + 10, y + 7, accent);
        pr_line(canvas, x + 10, y + 7, x + 6, y + 10, accent);
        pr_line(canvas, x + 6, y + 10, x + 10, y + 10, accent);
        break;
    case PR_ICON_SPARK:
        pr_line(canvas, x + 4, y, x + 4, y + 9, accent);
        pr_line(canvas, x, y + 4, x + 9, y + 4, accent);
        pr_line(canvas, x + 1, y + 1, x + 7, y + 7, accent);
        pr_line(canvas, x + 7, y + 1, x + 1, y + 7, accent);
        break;
    case PR_ICON_BLUSH:
        pr_line(canvas, x, y + 2, x + 2, y, accent);
        pr_line(canvas, x + 3, y + 2, x + 5, y, accent);
        pr_line(canvas, x + 6, y + 2, x + 8, y, accent);
        break;
    default:
        break;
    }
}

static void pr_draw_brows(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    int half_width,
    int thickness,
    uint16_t color)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int outer_x =
            pose->eye_x[eye] + (eye == 0U ? -half_width : half_width);
        const int inner_x =
            pose->eye_x[eye] + (eye == 0U ? half_width : -half_width);
        pr_thick_line(
            canvas,
            outer_x,
            pose->brow_outer_y[eye],
            inner_x,
            pose->brow_inner_y[eye],
            thickness,
            color);
    }
}

static void pr_draw_block_eye(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pose->eye_w[eye];
    const int aperture = pose->eye_open[eye];
    const int x = cx - width / 2;
    const int y = cy - aperture / 2;
    pr_rect(canvas, x - 1, y - 1, width + 2, aperture + 2, outline);
    pr_rect(canvas, x, y, width, aperture, sclera);
    if (aperture <= 1) {
        pr_line(canvas, x, cy, x + width - 1, cy, outline);
        return;
    }
    const int radius = pose->pupil_radius[eye];
    const int px = (int)pr_clamp(
        pose->pupil_x[eye], x + radius, x + width - radius - 1);
    const int py = (int)pr_clamp(
        pose->pupil_y[eye], y + radius, y + aperture - radius - 1);
    pr_ellipse(canvas, px, py, radius + 1, radius, iris);
    pr_rect(canvas, px, py - radius, 1, radius * 2 + 1, pupil);
    if (aperture >= 4 && pose->attention > 80U) {
        pr_put(canvas, px - 1, py - 1, glint);
    }
}

static void pr_draw_adventure_mouth(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int half_width = pose->mouth_w / 2;
    const int left = pose->mouth_x - half_width;
    const int right = pose->mouth_x + half_width;
    const int left_y = pose->mouth_y + pose->mouth_corner_y[0];
    const int right_y = pose->mouth_y + pose->mouth_corner_y[1];
    if (pose->mouth_h <= 2 || pose->mouth_press > 210U) {
        const int center_y = pose->mouth_y -
            (pose->mouth_corner_y[0] + pose->mouth_corner_y[1]) / 3;
        pr_thick_line(
            canvas, left, left_y, pose->mouth_x, center_y, 2, lip);
        pr_thick_line(
            canvas, pose->mouth_x, center_y, right, right_y, 2, lip);
        return;
    }
    const int top = pose->mouth_y - pose->mouth_h / 2;
    const int bottom = pose->mouth_y + pose->mouth_h / 2;
    const int round_narrow = pose->mouth_round * half_width / 510;
    const int inner_half = pr_clamp(half_width - 2 - round_narrow, 2, 14);
    pr_quad(
        canvas,
        left,
        left_y,
        pose->mouth_x - inner_half,
        top,
        pose->mouth_x + inner_half,
        top,
        right,
        right_y,
        lip);
    pr_quad(
        canvas,
        left,
        left_y,
        right,
        right_y,
        pose->mouth_x + inner_half,
        bottom,
        pose->mouth_x - inner_half,
        bottom,
        lip);
    pr_ellipse(
        canvas,
        pose->mouth_x,
        pose->mouth_y,
        inner_half,
        pr_clamp(pose->mouth_h / 2 - 1, 1, 6),
        cavity);
    if (pose->teeth > 92U) {
        const int teeth_width = pr_clamp(
            inner_half * 2 - 5 + pose->teeth / 72,
            2,
            inner_half * 2 - 1);
        pr_rect(
            canvas,
            pose->mouth_x - teeth_width / 2,
            top + 1,
            teeth_width,
            1 + pose->teeth / 150,
            teeth);
    }
    if (pose->tongue > 70U && pose->mouth_h >= 5) {
        const int tongue_half = pr_clamp(
            1 + pose->tongue / 38, 2, inner_half - 1);
        pr_rect(
            canvas,
            pose->mouth_x - tongue_half,
            bottom - 2,
            tongue_half * 2,
            2,
            tongue);
    }
    /* A one-cell tongue tip preserves fine articulation on shallow cels. */
    pr_put(
        canvas,
        pose->mouth_x - 3 + (int)((pose->tongue / 29U) % 7U),
        bottom - 1,
        tongue);
    if (pose->phoneme_shape != 0U) {
        const int tick = pose->mouth_x - 3 +
            (int)(pose->phoneme_shape % 6U);
        pr_put(canvas, tick, top + 1, tongue);
    }
}

static void pr_draw_ega_wayfarer(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose)
{
    const uint16_t black = PR_RGB565(0, 0, 0);
    const uint16_t navy = PR_RGB565(0, 0, 170);
    const uint16_t blue = PR_RGB565(85, 85, 255);
    const uint16_t cyan = PR_RGB565(85, 255, 255);
    const uint16_t green = PR_RGB565(0, 170, 0);
    const uint16_t light_green = PR_RGB565(85, 255, 85);
    const uint16_t brown = PR_RGB565(170, 85, 0);
    const uint16_t red = PR_RGB565(170, 0, 0);
    const uint16_t pink = PR_RGB565(255, 85, 85);
    const uint16_t skin = PR_RGB565(255, 170, 85);
    const uint16_t yellow = PR_RGB565(255, 255, 85);
    const uint16_t white = PR_RGB565(255, 255, 255);
    pr_clear(canvas, navy);
    pr_checker(canvas, 2, 2, 76, 56, navy, blue, 7U);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    pr_triangle(
        canvas, 14 + lean_x, 57, 25 + lean_x, 45 + lean_y,
        40 + lean_x, 48 + lean_y, green);
    pr_triangle(
        canvas, 40 + lean_x, 48 + lean_y,
        56 + lean_x, 45 + lean_y, 66 + lean_x, 57, green);
    pr_rect(canvas, 27 + lean_x, 47 + lean_y, 26, 10, green);
    pr_checker(
        canvas, 25 + lean_x, 49 + lean_y, 30, 4,
        green, light_green, 4U);
    pr_rect(canvas, 36, 43, 8, 7, pink);
    pr_rect(canvas, 37, 44, 6, 6, skin);

    /* Original adventurer silhouette: rounded face, cap and feather. */
    pr_ellipse(canvas, 40, 28, 20, 21, brown);
    pr_ellipse(canvas, 40, 29, 17, 19, skin);
    pr_rect(canvas, 21, 23, 4, 13, brown);
    pr_rect(canvas, 55, 23, 4, 13, brown);
    pr_ellipse(canvas, 23, 29, 3, 5, skin);
    pr_ellipse(canvas, 57, 29, 3, 5, skin);
    pr_rect(canvas, 25, 10, 30, 8, green);
    pr_triangle(canvas, 24, 13, 34, 5, 54, 12, green);
    pr_rect(canvas, 20, 15, 40, 4, green);
    pr_checker(canvas, 25, 10, 27, 3, green, light_green, 3U);
    const int feather = pose->head_roll +
        (pose->activity == FACE_ACTIVITY_LISTENING ? -2 : 0);
    pr_line(canvas, 53, 11, 61 + feather, 4, yellow);
    pr_line(canvas, 54, 12, 64 + feather, 7, yellow);
    pr_put(canvas, 62 + feather, 4, white);

    pr_draw_block_eye(
        canvas, pose, 0U, red, white, cyan, black, white);
    pr_draw_block_eye(
        canvas, pose, 1U, red, white, cyan, black, white);
    pr_draw_brows(canvas, pose, 5, 2, brown);
    const int nose_x = 40 + pose->head_yaw;
    pr_line(canvas, nose_x, 29, nose_x - 1, 35, red);
    pr_line(canvas, nose_x - 1, 35, nose_x + 2, 36, red);
    pr_draw_adventure_mouth(
        canvas, pose, red, black, white, pink);
    if (pose->cheek > 70U) {
        const int cheek_width = 3 + pose->cheek / 55;
        pr_checker(
            canvas, 29 - cheek_width, 34, cheek_width, 3,
            skin, pink, 2U);
        pr_checker(
            canvas, 51, 34, cheek_width, 3,
            skin, pink, 2U);
    }
    pr_draw_icon(canvas, pose->emotion_icon, 65, 7, black, yellow);
}

static void pr_draw_vga_eye(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    uint16_t shadow,
    uint16_t white,
    uint16_t iris,
    uint16_t pupil,
    uint16_t highlight)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pose->eye_w[eye];
    const int aperture = pose->eye_open[eye];
    const int x = cx - width / 2;
    const int y = cy - aperture / 2;
    pr_ellipse(canvas, cx, cy, width / 2 + 1, aperture / 2 + 1, shadow);
    pr_ellipse(canvas, cx, cy, width / 2, pr_clamp(aperture / 2, 1, 5), white);
    if (aperture > 1) {
        const int px = (int)pr_clamp(
            pose->pupil_x[eye], x + 2, x + width - 2);
        const int py = (int)pr_clamp(
            pose->pupil_y[eye], y + 1, y + aperture - 1);
        pr_ellipse(canvas, px, py, pose->pupil_radius[eye] + 1, 2, iris);
        pr_ellipse(canvas, px, py, 1, 2, pupil);
        if (pose->attention > 70U) {
            pr_put(canvas, px - 1, py - 1, highlight);
        }
    }
}

static void pr_draw_vga_elder(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose)
{
    const uint16_t night = PR_RGB565(13, 10, 28);
    const uint16_t violet = PR_RGB565(69, 45, 92);
    const uint16_t cloak = PR_RGB565(36, 65, 111);
    const uint16_t cloak_light = PR_RGB565(79, 119, 159);
    const uint16_t skin_dark = PR_RGB565(119, 72, 61);
    const uint16_t skin = PR_RGB565(200, 139, 99);
    const uint16_t skin_light = PR_RGB565(242, 190, 137);
    const uint16_t silver_dark = PR_RGB565(83, 85, 91);
    const uint16_t silver = PR_RGB565(166, 169, 166);
    const uint16_t silver_light = PR_RGB565(232, 228, 211);
    const uint16_t ink = PR_RGB565(25, 20, 26);
    const uint16_t iris = PR_RGB565(62, 148, 153);
    const uint16_t lip = PR_RGB565(125, 44, 48);
    const uint16_t tongue = PR_RGB565(205, 79, 91);
    pr_clear(canvas, night);
    pr_checker(canvas, 2, 2, 76, 56, violet, night, 5U);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    pr_triangle(
        canvas, 8 + lean_x, 57, 25 + lean_x, 43 + lean_y,
        40 + lean_x, 57, cloak);
    pr_triangle(
        canvas, 40 + lean_x, 57, 55 + lean_x, 43 + lean_y,
        72 + lean_x, 57, cloak);
    pr_checker(
        canvas, 16 + lean_x, 51 + lean_y, 48, 6,
        cloak, cloak_light, 4U);
    pr_ellipse(canvas, 40, 28, 22, 24, silver_dark);
    pr_ellipse(canvas, 40, 29, 19, 22, skin_dark);
    pr_ellipse(canvas, 39, 27, 18, 20, skin);
    pr_ellipse(canvas, 35, 23, 12, 13, skin_light);
    pr_triangle(canvas, 19, 22, 23, 7, 37, 4, silver);
    pr_triangle(canvas, 37, 4, 58, 10, 61, 24, silver);
    pr_checker(canvas, 22, 8, 36, 9, silver, silver_light, 3U);
    pr_triangle(canvas, 21, 25, 15, 35, 22, 43, silver);
    pr_triangle(canvas, 59, 24, 65, 35, 58, 44, silver);

    pr_draw_vga_eye(
        canvas, pose, 0U, skin_dark, silver_light, iris, ink, silver_light);
    pr_draw_vga_eye(
        canvas, pose, 1U, skin_dark, silver_light, iris, ink, silver_light);
    pr_draw_brows(canvas, pose, 6, 3, silver_light);
    const int nose_x = 40 + pose->head_yaw;
    pr_triangle(
        canvas, nose_x - 1, 25, nose_x - 4, 36,
        nose_x + 2, 37, skin_dark);
    pr_line(canvas, nose_x - 2, 36, nose_x + 3, 37, skin_light);

    /* Beard parts around, but never over, the animated mouth cavity. */
    pr_triangle(canvas, 21, 36, 29, 57, 37, 45, silver);
    pr_triangle(canvas, 59, 36, 51, 57, 43, 45, silver);
    pr_triangle(canvas, 29, 45, 40, 58, 51, 45, silver_light);
    pr_checker(canvas, 24, 39, 13, 7, silver, silver_light, 4U);
    pr_checker(canvas, 43, 39, 13, 7, silver, silver_light, 4U);
    pr_draw_adventure_mouth(
        canvas, pose, lip, ink, silver_light, tongue);
    const int moustache_lift =
        pr_clamp((pose->mouth_corner_y[0] +
                  pose->mouth_corner_y[1]) / 2, -3, 3);
    pr_triangle(
        canvas, 40, 39 + moustache_lift, 25, 37, 36, 43, silver_light);
    pr_triangle(
        canvas, 40, 39 + moustache_lift, 55, 37, 44, 43, silver_light);
    if (pose->cheek > 90U) {
        const int cheek_width = 3 + pose->cheek / 48;
        pr_checker(
            canvas, 27 - cheek_width, 32, cheek_width, 3,
            skin, tongue, 3U);
        pr_checker(
            canvas, 53, 32, cheek_width, 3,
            skin, tongue, 3U);
    }
    pr_draw_icon(
        canvas, pose->emotion_icon, 66, 7, night, silver_light);
}

static void pr_draw_talkie_eye(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    uint16_t lid,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pose->eye_w[eye];
    const int aperture = pose->eye_open[eye];
    const int half = width / 2;
    const int vertical = pr_clamp(aperture / 2, 1, 5);
    pr_quad(
        canvas, cx - half, cy, cx - half / 2, cy - vertical,
        cx + half / 2, cy - vertical, cx + half, cy, lid);
    pr_quad(
        canvas, cx - half, cy, cx + half, cy,
        cx + half / 2, cy + vertical, cx - half / 2, cy + vertical, lid);
    if (aperture <= 1) {
        pr_line(canvas, cx - half, cy, cx + half, cy, pupil);
        return;
    }
    pr_ellipse(canvas, cx, cy, half - 1, vertical - 1, sclera);
    const int px = (int)pr_clamp(
        pose->pupil_x[eye], cx - half + 2, cx + half - 2);
    const int py = (int)pr_clamp(
        pose->pupil_y[eye], cy - vertical + 1, cy + vertical - 1);
    pr_ellipse(canvas, px, py, pose->pupil_radius[eye] + 1, 2, iris);
    pr_ellipse(canvas, px, py, 1, 2, pupil);
    if (pose->attention > 60U) {
        pr_put(canvas, px - 1, py - 1, glint);
    }
}

static void pr_draw_talkie_closeup(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose)
{
    const uint16_t void_color = PR_RGB565(5, 11, 25);
    const uint16_t bulkhead = PR_RGB565(25, 49, 66);
    const uint16_t panel = PR_RGB565(48, 85, 91);
    const uint16_t uniform = PR_RGB565(35, 67, 73);
    const uint16_t uniform_light = PR_RGB565(74, 123, 112);
    const uint16_t hair = PR_RGB565(34, 25, 30);
    const uint16_t skin_shadow = PR_RGB565(102, 58, 49);
    const uint16_t skin = PR_RGB565(194, 120, 82);
    const uint16_t skin_light = PR_RGB565(239, 172, 112);
    const uint16_t sclera = PR_RGB565(235, 224, 194);
    const uint16_t iris = PR_RGB565(83, 166, 155);
    const uint16_t lip = PR_RGB565(123, 42, 54);
    const uint16_t cavity = PR_RGB565(45, 19, 30);
    const uint16_t tongue = PR_RGB565(222, 88, 103);
    const uint16_t amber = PR_RGB565(255, 185, 65);
    pr_clear(canvas, void_color);
    pr_rect(canvas, 2, 2, 76, 56, bulkhead);
    pr_checker(canvas, 4, 4, 72, 52, bulkhead, panel, 8U);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    pr_triangle(
        canvas, 5 + lean_x, 57, 20 + lean_x, 43 + lean_y,
        40 + lean_x, 52 + lean_y, uniform);
    pr_triangle(
        canvas, 40 + lean_x, 52 + lean_y,
        60 + lean_x, 43 + lean_y, 75 + lean_x, 57, uniform);
    pr_checker(
        canvas, 14 + lean_x, 51 + lean_y, 52, 6,
        uniform, uniform_light, 5U);

    /* Cinematic 1990s close-up: stable face, asymmetric cel shading. */
    pr_ellipse(canvas, 40, 29, 29, 28, hair);
    pr_ellipse(canvas, 40, 31, 26, 26, skin_shadow);
    pr_quad(canvas, 17, 18, 42, 7, 65, 16, 61, 43, skin);
    pr_quad(canvas, 17, 18, 42, 7, 42, 54, 19, 43, skin_light);
    pr_rect(canvas, 11, 21, 8, 17, hair);
    pr_rect(canvas, 62, 18, 7, 21, hair);
    pr_ellipse(canvas, 14, 30, 4, 7, skin);
    pr_ellipse(canvas, 66, 30, 4, 7, skin_shadow);
    pr_triangle(canvas, 12, 17, 25, 4, 50, 5, hair);
    pr_triangle(canvas, 50, 5, 68, 15, 55, 12, hair);
    pr_checker(canvas, 18, 8, 39, 7, hair, skin_shadow, 5U);

    /* Headset is a character prop and reacts to listening/speech phase. */
    pr_frame(canvas, 10, 17, 5, 24, 2, amber);
    pr_line(canvas, 13, 38, 20, 43, amber);
    pr_line(
        canvas, 20, 43,
        28 + (pose->speech_phase == FACE_SPEECH_ACTIVE ? 2 : 0),
        43, amber);
    pr_rect(canvas, 27, 42, 4, 3, amber);

    pr_draw_talkie_eye(
        canvas, pose, 0U, skin_shadow, sclera, iris, hair, sclera);
    pr_draw_talkie_eye(
        canvas, pose, 1U, skin_shadow, sclera, iris, hair, sclera);
    pr_draw_brows(canvas, pose, 7, 3, hair);
    const int nose_x = 40 + pose->head_yaw;
    pr_triangle(
        canvas, nose_x, 24, nose_x - 5, 35, nose_x + 3, 37,
        skin_shadow);
    pr_line(canvas, nose_x - 2, 35, nose_x + 4, 36, skin_light);
    pr_draw_adventure_mouth(
        canvas, pose, lip, cavity, sclera, tongue);
    pr_line(
        canvas, 27, 49 + pose->head_pitch,
        40, 53 + pose->head_pitch, skin_shadow);
    pr_line(
        canvas, 40, 53 + pose->head_pitch,
        55, 48 + pose->head_pitch, skin_shadow);
    if (pose->cheek > 72U) {
        const int cheek_width = 4 + pose->cheek / 45;
        pr_checker(
            canvas, 26 - cheek_width, 34, cheek_width, 4,
            skin_light, tongue, 4U);
        pr_checker(
            canvas, 54, 34, cheek_width, 4,
            skin, tongue, 4U);
    }
    pr_draw_icon(canvas, pose->emotion_icon, 67, 6, hair, amber);
}

static void pr_draw_robot_eye(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    uint16_t frame,
    uint16_t glow,
    uint16_t bright,
    uint16_t dark)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pose->eye_w[eye];
    const int aperture = pose->eye_open[eye];
    const int x = cx - width / 2;
    const int y = cy - aperture / 2;
    pr_rect(canvas, x - 2, y - 2, width + 4, aperture + 4, frame);
    pr_rect(canvas, x - 1, y - 1, width + 2, aperture + 2, glow);
    pr_rect(canvas, x, y, width, aperture, dark);
    if (aperture <= 1) {
        pr_rect(canvas, x, cy, width, 1, bright);
        return;
    }
    const int px = (int)pr_clamp(
        pose->pupil_x[eye], x + 1, x + width - 2);
    const int py = (int)pr_clamp(
        pose->pupil_y[eye], y + 1, y + aperture - 2);
    pr_rect(
        canvas, px - pose->pupil_radius[eye], py - 1,
        pose->pupil_radius[eye] * 2 + 1, 3, bright);
    pr_put(canvas, px, py, dark);
}

static void pr_draw_led_mouth(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    uint16_t bezel,
    uint16_t inactive,
    uint16_t active,
    uint16_t hot)
{
    const int columns = 11;
    const int rows = 5;
    const int start_x = 20;
    const int start_y = pose->mouth_y - 6;
    pr_rect(canvas, 17, start_y - 2, 46, 15, bezel);
    pr_rect(canvas, 19, start_y, 42, 11, inactive);
    const int center = columns / 2;
    const int active_half =
        pr_clamp(pose->mouth_w * columns / 54, 2, center);
    const int active_rows =
        pr_clamp(
            1 + pose->mouth_h * rows / 12 -
                (pose->mouth_press > 190U ? 1 : 0),
            1,
            rows);
    const int row_start = (rows - active_rows) / 2;
    const int rounded =
        pr_clamp(pose->mouth_round * 3 / 255, 0, 3);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int dx = pr_abs(column - center);
            const int dy = pr_abs(row - rows / 2);
            const bool within_rows =
                row >= row_start && row < row_start + active_rows;
            const bool round_cut =
                rounded > 1 && dx + dy > active_half + 1;
            const bool on =
                within_rows && dx <= active_half && !round_cut;
            uint16_t color = on ? active : inactive;
            const int corner_shift =
                column < center
                ? pose->mouth_corner_y[0] / 4
                : pose->mouth_corner_y[1] / 4;
            if (on &&
                ((row == row_start &&
                  dx <= (int)pose->teeth / 54) ||
                 (row == row_start + active_rows - 1 &&
                  dx <= (int)pose->tongue / 57) ||
                 column == center +
                     (int)(pose->phoneme_shape % 3U) - 1)) {
                color = hot;
            }
            pr_rect(
                canvas, start_x + column * 4,
                start_y + (row + corner_shift) * 2, 2, 1, color);
        }
    }
    /* Articulation pin and attached corner actuators stay within the bezel. */
    pr_rect(
        canvas,
        start_x + (3 + (int)(pose->phoneme_shape % 5U)) * 4,
        start_y + 4,
        2,
        1,
        hot);
    pr_rect(
        canvas,
        start_x + (2 + (int)((pose->tongue / 31U) % 7U)) * 4,
        start_y + 8,
        2,
        1,
        hot);
    pr_rect(
        canvas, 18,
        start_y + 5 + pose->mouth_corner_y[0],
        2, 2, active);
    pr_rect(
        canvas, 60,
        start_y + 5 + pose->mouth_corner_y[1],
        2, 2, active);
    const int latch_y =
        start_y + 1 + (int)pose->mouth_press * 7 / 255;
    pr_rect(canvas, 17, latch_y, 2, 2, hot);
    pr_rect(canvas, 61, latch_y, 2, 2, hot);
}

static void pr_draw_pixel_automaton(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose)
{
    const uint16_t black = PR_RGB565(2, 4, 10);
    const uint16_t navy = PR_RGB565(8, 18, 35);
    const uint16_t steel = PR_RGB565(45, 66, 81);
    const uint16_t steel_light = PR_RGB565(104, 131, 139);
    const uint16_t cyan_dim = PR_RGB565(9, 77, 88);
    const uint16_t cyan = PR_RGB565(44, 230, 225);
    const uint16_t white = PR_RGB565(214, 255, 239);
    const uint16_t amber = PR_RGB565(247, 160, 39);
    const uint16_t red = PR_RGB565(226, 43, 64);
    pr_clear(canvas, black);
    pr_checker(canvas, 2, 2, 76, 56, black, navy, 9U);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    pr_rect(canvas, 22 + lean_x, 49 + lean_y, 36, 8, steel);
    pr_rect(canvas, 27 + lean_x, 47 + lean_y, 26, 10, steel_light);
    pr_frame(canvas, 12, 8, 56, 45, 3, steel);
    pr_rect(canvas, 15, 11, 50, 39, navy);
    pr_rect(canvas, 18, 14, 44, 33, black);
    pr_rect(canvas, 8, 22, 7, 16, steel);
    pr_rect(canvas, 65, 22, 7, 16, steel);
    pr_rect(canvas, 10, 25, 3, 10, cyan_dim);
    pr_rect(canvas, 67, 25, 3, 10, cyan_dim);
    const int antenna_sway = pose->head_roll +
        (pose->activity == FACE_ACTIVITY_THINKING ? -2 : 0);
    pr_line(canvas, 40, 10, 45 + antenna_sway, 4, steel_light);
    pr_ellipse(
        canvas, 46 + antenna_sway, 4, 2, 2,
        pose->speech_phase == FACE_SPEECH_ACTIVE ? amber : cyan);

    pr_draw_robot_eye(
        canvas, pose, 0U, steel, cyan_dim, cyan, black);
    pr_draw_robot_eye(
        canvas, pose, 1U, steel, cyan_dim, cyan, black);
    pr_draw_brows(canvas, pose, 7, 2, steel_light);
    pr_draw_led_mouth(
        canvas, pose, steel, navy, cyan, white);
    if (pose->cheek > 72U) {
        const int cheek_size = pr_clamp(
            1 + pose->cheek / 70, 2, 4);
        pr_rect(canvas, 19 - cheek_size, 36,
            cheek_size, cheek_size, red);
        pr_rect(canvas, 61, 36, cheek_size, cheek_size, red);
    }
    const uint8_t activity = (uint8_t)(pose->activity % 4U);
    for (uint8_t bit = 0U; bit < 2U; ++bit) {
        pr_rect(
            canvas, 36 + bit * 5, 51, 3, 2,
            (activity & (1U << bit)) != 0U ? amber : cyan_dim);
    }
    pr_draw_icon(canvas, pose->emotion_icon, 67, 7, black, amber);
}

static void pr_draw_pocket_eye(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    size_t eye,
    uint16_t dark,
    uint16_t light,
    uint16_t mid)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pr_clamp(pose->eye_w[eye], 7, 12);
    const int aperture = pr_clamp(pose->eye_open[eye], 1, 8);
    const int x = cx - width / 2;
    const int y = cy - aperture / 2;
    pr_rect(canvas, x - 1, y - 1, width + 2, aperture + 2, dark);
    pr_rect(canvas, x, y, width, aperture, light);
    if (aperture > 1) {
        const int px = (int)pr_clamp(
            pose->pupil_x[eye], x + 1, x + width - 2);
        const int py = (int)pr_clamp(
            pose->pupil_y[eye], y + 1, y + aperture - 2);
        const int radius = pose->pupil_radius[eye];
        pr_rect(
            canvas, px - radius, py - radius,
            radius * 2 + 1, radius * 2 + 1, dark);
        if (pose->attention > 72U) {
            pr_put(canvas, px - 1, py - 1, mid);
        }
    }
}

static void pr_draw_pocket_mouth(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose,
    uint16_t dark,
    uint16_t light,
    uint16_t mid)
{
    const int half = pr_clamp(pose->mouth_w / 2, 3, 13);
    const int left_y = pose->mouth_y + pose->mouth_corner_y[0] / 2;
    const int right_y = pose->mouth_y + pose->mouth_corner_y[1] / 2;
    if (pose->mouth_h <= 2 || pose->mouth_press > 205U) {
        pr_line(
            canvas, pose->mouth_x - half, left_y,
            pose->mouth_x, pose->mouth_y - 1, dark);
        pr_line(
            canvas, pose->mouth_x, pose->mouth_y - 1,
            pose->mouth_x + half, right_y, dark);
        return;
    }
    const int mouth_radius = pr_clamp(
        half - pose->mouth_round / 60, 2, half);
    pr_ellipse(
        canvas, pose->mouth_x, pose->mouth_y,
        mouth_radius,
        pr_clamp(pose->mouth_h / 2, 2, 5), dark);
    /* Corner strokes remain physically attached to every open mouth cel. */
    pr_line(
        canvas, pose->mouth_x - mouth_radius, pose->mouth_y,
        pose->mouth_x - half, left_y, dark);
    pr_line(
        canvas, pose->mouth_x + mouth_radius, pose->mouth_y,
        pose->mouth_x + half, right_y, dark);
    pr_rect(
        canvas, pose->mouth_x - half - 1, left_y,
        2, 2, dark);
    pr_rect(
        canvas, pose->mouth_x + half - 1, right_y,
        2, 2, dark);
    if (pose->teeth > 100U) {
        const int teeth_width = pr_clamp(
            2 + pose->teeth / 38, 3, half * 2 - 1);
        pr_rect(canvas, pose->mouth_x - teeth_width / 2,
            pose->mouth_y - pose->mouth_h / 2 + 1,
            teeth_width, 1, light);
    }
    if (pose->tongue > 60U) {
        const int tongue_width = pr_clamp(
            2 + pose->tongue / 46, 3, half * 2 - 1);
        pr_rect(
            canvas, pose->mouth_x - tongue_width / 2,
            pose->mouth_y + pose->mouth_h / 2 - 1,
            tongue_width, 1, mid);
        pr_put(
            canvas,
            pose->mouth_x - 3 + (int)((pose->tongue / 29U) % 7U),
            pose->mouth_y + pose->mouth_h / 2 - 1,
            light);
    }
    if ((pose->phoneme_shape & 1U) != 0U) {
        pr_put(
            canvas, pose->mouth_x + half / 2,
            pose->mouth_y - pose->mouth_h / 2 + 1, light);
    }
}

static void pr_draw_pocket_rpg(
    pr_canvas_t *canvas,
    const face_pixel_redux_pose_t *pose)
{
    /* Deliberate four-tone handheld palette. */
    const uint16_t darkest = PR_RGB565(20, 42, 36);
    const uint16_t dark = PR_RGB565(50, 78, 53);
    const uint16_t mid = PR_RGB565(112, 137, 78);
    const uint16_t light = PR_RGB565(190, 202, 116);
    pr_clear(canvas, light);
    pr_checker(canvas, 2, 2, 76, 56, light, mid, 8U);
    pr_frame(canvas, 3, 3, 74, 54, 2, darkest);

    const int lean_x = pose->body_lean_x;
    const int lean_y = pose->body_lean_y;
    pr_triangle(
        canvas, 16 + lean_x, 56, 25 + lean_x, 43 + lean_y,
        40 + lean_x, 48 + lean_y, dark);
    pr_triangle(
        canvas, 40 + lean_x, 48 + lean_y,
        55 + lean_x, 43 + lean_y, 64 + lean_x, 56, dark);
    pr_checker(
        canvas, 23 + lean_x, 49 + lean_y, 34, 7,
        dark, mid, 3U);

    /* Original hooded forest familiar, not a traced game sprite. */
    pr_triangle(canvas, 19, 21, 24, 7, 34, 16, dark);
    pr_triangle(canvas, 46, 16, 56, 7, 61, 21, dark);
    pr_triangle(canvas, 22, 17, 26, 11, 31, 18, mid);
    pr_triangle(canvas, 49, 18, 54, 11, 58, 17, mid);
    pr_ellipse(canvas, 40, 29, 23, 22, darkest);
    pr_ellipse(canvas, 40, 29, 19, 18, mid);
    pr_ellipse(canvas, 40, 30, 16, 16, light);
    pr_rect(canvas, 21, 22, 5, 19, dark);
    pr_rect(canvas, 54, 22, 5, 19, dark);
    pr_checker(canvas, 25, 14, 30, 8, dark, mid, 3U);

    pr_draw_pocket_eye(canvas, pose, 0U, darkest, light, mid);
    pr_draw_pocket_eye(canvas, pose, 1U, darkest, light, mid);
    pr_draw_brows(canvas, pose, 5, 2, darkest);
    const int nose_x = 40 + pose->head_yaw;
    pr_triangle(
        canvas, nose_x - 2, 33, nose_x + 2, 33,
        nose_x, 36, dark);
    pr_draw_pocket_mouth(canvas, pose, darkest, light, dark);
    if (pose->cheek > 60U) {
        const int cheek_width = 3 + pose->cheek / 58;
        pr_checker(
            canvas, 30 - cheek_width, 35, cheek_width, 3,
            light, dark, 2U);
        pr_checker(
            canvas, 50, 35, cheek_width, 3,
            light, dark, 2U);
    }
    const int tail_lift =
        pose->speech_phase == FACE_SPEECH_ACTIVE ? -2 :
        pose->activity == FACE_ACTIVITY_LISTENING ? -1 : 1;
    const int audio_tail =
        pose->speaking ? (int)pose->source.audio_level / 96 : 0;
    pr_line(
        canvas, 57 + lean_x, 51 + lean_y,
        67 + lean_x, 47 + lean_y + tail_lift - audio_tail, darkest);
    pr_line(
        canvas, 67 + lean_x,
        47 + lean_y + tail_lift - audio_tail,
        70 + lean_x,
        51 + lean_y + tail_lift - audio_tail, dark);
    pr_draw_icon(canvas, pose->emotion_icon, 65, 8, light, darkest);
}

bool face_pixel_redux_actor_render(
    face_pixel_redux_actor_t actor,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!pr_actor_valid(actor) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_PIXEL_REDUX_PIXEL_COUNT) {
        return false;
    }
    face_pixel_redux_pose_t pose;
    if (!face_pixel_redux_actor_resolve(
            actor, render_key, sample_clock, &pose)) {
        return false;
    }
    pr_canvas_t canvas = {rgb565};
    switch (actor) {
    case FACE_PIXEL_REDUX_EGA_QUEST:
        pr_draw_ega_wayfarer(&canvas, &pose);
        break;
    case FACE_PIXEL_REDUX_VGA_ELDER:
        pr_draw_vga_elder(&canvas, &pose);
        break;
    case FACE_PIXEL_REDUX_TALKIE_CLOSEUP:
        pr_draw_talkie_closeup(&canvas, &pose);
        break;
    case FACE_PIXEL_REDUX_PIXEL_AUTOMATON:
        pr_draw_pixel_automaton(&canvas, &pose);
        break;
    case FACE_PIXEL_REDUX_POCKET_RPG:
        pr_draw_pocket_rpg(&canvas, &pose);
        break;
    default:
        return false;
    }
    return true;
}

bool face_pixel_redux_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_pixel_redux_actor_t actor;
    return face_pixel_redux_actor_from_legacy_id(
               legacy_profile_id, &actor) &&
        face_pixel_redux_actor_render(
            actor, render_key, sample_clock, rgb565, pixel_capacity);
}
