#include "face_salvage_actors.h"

#include "face_pose.h"
#include "face_stage.h"

#include <string.h>

#define SA_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    SA_SAFE = 4,
    SA_EXPRESSION_COUNT = 11,
};

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t mouth_kind;
    bool monocular;
    uint8_t ops;
    int8_t eye_x[2];
    int8_t eye_y;
    uint8_t eye_w;
    uint8_t eye_h;
    uint8_t pupil;
    int8_t mouth_y;
} sa_actor_def_t;

typedef struct {
    int8_t eye_open_left;
    int8_t eye_open_right;
    int8_t eye_width;
    int8_t brow_raise_left;
    int8_t brow_raise_right;
    int8_t brow_slope_left;
    int8_t brow_slope_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t mouth_corner_left;
    int8_t mouth_corner_right;
    int8_t pupil;
    uint8_t cheek;
} sa_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
} sa_viseme_t;

typedef struct {
    uint16_t *pixels;
} sa_canvas_t;

static const sa_actor_def_t SA_ACTORS[FACE_SALVAGE_ACTOR_COUNT] = {
    [FACE_SALVAGE_ACTOR_AMBER_TERMINAL] = {
        "amber-terminal-operator",
        "Amber Terminal Operator",
        4U,
        FACE_SALVAGE_MOUTH_GLYPH,
        false,
        7U,
        {52, 108},
        49,
        34U,
        32U,
        5U,
        87,
    },
    [FACE_SALVAGE_ACTOR_FILM_NOIR_ROGUE] = {
        "dithered-film-noir-rogue",
        "Dithered Film-Noir Rogue",
        6U,
        FACE_SALVAGE_MOUTH_LINE,
        false,
        10U,
        {55, 105},
        52,
        30U,
        24U,
        4U,
        86,
    },
    [FACE_SALVAGE_ACTOR_NEON_MASK] = {
        "neon-cyan-faceplate",
        "Neon Cyan Faceplate",
        29U,
        FACE_SALVAGE_MOUTH_CAVITY,
        false,
        12U,
        {52, 108},
        49,
        36U,
        30U,
        6U,
        84,
    },
    [FACE_SALVAGE_ACTOR_RED_OPTIC] = {
        "red-optic-performer",
        "Red Optic Performer",
        35U,
        FACE_SALVAGE_MOUTH_NONE,
        true,
        14U,
        {80, 0},
        54,
        64U,
        62U,
        14U,
        91,
    },
    [FACE_SALVAGE_ACTOR_HUB75_MASCOT] = {
        "hub75-block-mascot",
        "HUB75 Block Mascot",
        36U,
        FACE_SALVAGE_MOUTH_BLOCK,
        false,
        4U,
        {52, 108},
        47,
        34U,
        32U,
        6U,
        82,
    },
    [FACE_SALVAGE_ACTOR_ZINE_ROGUE] = {
        "two-tone-zine-rogue",
        "Two-Tone Zine Rogue",
        52U,
        FACE_SALVAGE_MOUTH_CAVITY,
        false,
        10U,
        {53, 107},
        50,
        32U,
        28U,
        5U,
        82,
    },
};

/*
 * All eleven emotions alter geometry, not merely color.  The asymmetric
 * authored poses are especially important at 160x120 where a one-pixel lid or
 * mouth-corner change reads more clearly than decorative texture.
 */
static const sa_expression_t SA_EXPRESSIONS[SA_EXPRESSION_COUNT] = {
    /* neutral */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0U},
    /* warm */
    {-1, -1, 3, -2, -2, -2, 2, 0, 1, 1, 5, 5, 0, 0, 84U},
    /* joy */
    {-9, -9, 7, -3, -3, -4, 4, 0, 1, 5, 11, 11, 1, 0, 220U},
    /* concern */
    {-2, -1, -2, -5, -5, 6, -6, -3, 3, -1, -8, -8, 1, 1, 52U},
    /* surprise */
    {9, 9, 6, -8, -8, 0, 0, 0, -3, 12, -6, 0, 0, -3, 0U},
    /* thoughtful */
    {-3, -8, 0, -5, 0, 5, -2, -10, -5, -2, -4, 1, 0, 1, 28U},
    /* skeptical */
    {-10, 0, -2, -7, 2, 7, -6, 10, 0, -1, -6, 3, 0, 0, 24U},
    /* determined */
    {-6, -6, 5, 3, 3, -7, 7, 0, 1, 0, -4, -4, 2, 1, 16U},
    /* sleepy */
    {-14, -14, -5, 4, 4, 1, -1, -2, 5, 2, 1, 1, 1, 2, 0U},
    /* excited */
    {7, 7, 8, -8, -8, -2, 2, 0, -4, 10, 10, 10, -1, -2, 180U},
    /* embarrassed */
    {-6, -9, -4, -4, -6, 4, -4, 9, 5, -1, 5, 1, 0, 1, 255U},
};

static const sa_viseme_t SA_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {225U, 164U, 22U, 0U, 72U, 58U},
    [FACE_VISEME_E] = {92U, 238U, 8U, 0U, 156U, 10U},
    [FACE_VISEME_I] = {58U, 224U, 4U, 0U, 116U, 8U},
    [FACE_VISEME_O] = {198U, 98U, 242U, 0U, 34U, 32U},
    [FACE_VISEME_U] = {116U, 72U, 255U, 0U, 18U, 28U},
    [FACE_VISEME_PP] = {4U, 168U, 20U, 255U, 0U, 0U},
    [FACE_VISEME_SS] = {46U, 232U, 4U, 30U, 244U, 0U},
    [FACE_VISEME_TH] = {78U, 188U, 18U, 0U, 108U, 255U},
    [FACE_VISEME_DD] = {84U, 176U, 14U, 0U, 192U, 84U},
    [FACE_VISEME_FF] = {38U, 202U, 8U, 50U, 255U, 0U},
    [FACE_VISEME_KK] = {132U, 182U, 30U, 0U, 48U, 84U},
    [FACE_VISEME_NN] = {52U, 170U, 18U, 12U, 104U, 56U},
    [FACE_VISEME_RR] = {106U, 142U, 128U, 0U, 48U, 36U},
    [FACE_VISEME_CH] = {88U, 206U, 26U, 0U, 148U, 26U},
    [FACE_VISEME_SIL] = {6U, 142U, 24U, 220U, 0U, 0U},
};

static int32_t sa_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t sa_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t sa_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t sa_wave(uint32_t sample_clock, uint32_t period)
{
    if (period < 2U) {
        return 0;
    }
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    const int32_t value = phase < half
        ? (int32_t)(phase * 254U / half) - 127
        : 127 - (int32_t)((phase - half) * 254U / half);
    return sa_clamp(value, -127, 127);
}

static bool sa_style_valid(face_salvage_actor_style_t style)
{
    return (unsigned)style < (unsigned)FACE_SALVAGE_ACTOR_COUNT;
}

static void sa_put(sa_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x >= SA_SAFE && x < FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE &&
        y >= SA_SAFE && y < FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE) {
        canvas->pixels[
            (size_t)y * FACE_SALVAGE_ACTOR_WIDTH + (size_t)x] = color;
    }
}

static void sa_clear(sa_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_SALVAGE_ACTOR_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void sa_rect(
    sa_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left = (int)sa_clamp(
        x, SA_SAFE, FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE);
    const int right = (int)sa_clamp(
        x + width, SA_SAFE, FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE);
    const int top = (int)sa_clamp(
        y, SA_SAFE, FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE);
    const int bottom = (int)sa_clamp(
        y + height, SA_SAFE, FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            canvas->pixels[
                (size_t)yy * FACE_SALVAGE_ACTOR_WIDTH + (size_t)xx] =
                color;
        }
    }
}

static void sa_line(
    sa_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = sa_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -sa_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        sa_put(canvas, x0, y0, color);
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

static void sa_thick_line(
    sa_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        sa_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void sa_ellipse(
    sa_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    const int left = (int)sa_clamp(
        cx - rx, SA_SAFE, FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE - 1);
    const int right = (int)sa_clamp(
        cx + rx, SA_SAFE, FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE - 1);
    const int top = (int)sa_clamp(
        cy - ry, SA_SAFE, FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE - 1);
    const int bottom = (int)sa_clamp(
        cy + ry, SA_SAFE, FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE - 1);
    const int64_t limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t dx = x - cx;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= limit) {
                sa_put(canvas, x, y, color);
            }
        }
    }
}

static int32_t sa_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void sa_triangle(
    sa_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int left = (int)sa_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        SA_SAFE,
        FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE - 1);
    const int right = (int)sa_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        SA_SAFE,
        FACE_SALVAGE_ACTOR_WIDTH - SA_SAFE - 1);
    const int top = (int)sa_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        SA_SAFE,
        FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE - 1);
    const int bottom = (int)sa_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        SA_SAFE,
        FACE_SALVAGE_ACTOR_HEIGHT - SA_SAFE - 1);
    const int32_t orientation = sa_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = sa_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = sa_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = sa_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                sa_put(canvas, x, y, color);
            }
        }
    }
}

static void sa_round_rect(
    sa_canvas_t *canvas,
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
    radius = (int)sa_clamp(radius, 0, width / 2);
    radius = (int)sa_clamp(radius, 0, height / 2);
    sa_rect(canvas, x + radius, y, width - radius * 2, height, color);
    sa_rect(canvas, x, y + radius, width, height - radius * 2, color);
    sa_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    sa_ellipse(
        canvas, x + width - radius - 1, y + radius, radius, radius, color);
    sa_ellipse(
        canvas, x + radius, y + height - radius - 1,
        radius, radius, color);
    sa_ellipse(
        canvas, x + width - radius - 1, y + height - radius - 1,
        radius, radius, color);
}

static void sa_ring(
    sa_canvas_t *canvas,
    int cx,
    int cy,
    int outer_x,
    int outer_y,
    int thickness,
    uint16_t outer,
    uint16_t inner)
{
    sa_ellipse(canvas, cx, cy, outer_x, outer_y, outer);
    sa_ellipse(
        canvas,
        cx,
        cy,
        sa_clamp(outer_x - thickness, 1, outer_x),
        sa_clamp(outer_y - thickness, 1, outer_y),
        inner);
}

static void sa_diamond(
    sa_canvas_t *canvas,
    int cx,
    int cy,
    int radius_x,
    int radius_y,
    uint16_t color)
{
    sa_triangle(
        canvas,
        cx - radius_x,
        cy,
        cx,
        cy - radius_y,
        cx + radius_x,
        cy,
        color);
    sa_triangle(
        canvas,
        cx - radius_x,
        cy,
        cx + radius_x,
        cy,
        cx,
        cy + radius_y,
        color);
}

static void sa_quad(
    sa_canvas_t *canvas,
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
    sa_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    sa_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static uint8_t sa_viseme_index(uint8_t set, uint8_t raw)
{
    static const uint8_t VRM5[5] = {
        FACE_VISEME_AA,
        FACE_VISEME_I,
        FACE_VISEME_U,
        FACE_VISEME_E,
        FACE_VISEME_O,
    };
    static const uint8_t PRESTON9[9] = {
        FACE_VISEME_AA,
        FACE_VISEME_E,
        FACE_VISEME_O,
        FACE_VISEME_U,
        FACE_VISEME_PP,
        FACE_VISEME_FF,
        FACE_VISEME_TH,
        FACE_VISEME_CH,
        FACE_VISEME_SIL,
    };
    if (set == FACE_VISEME_SET_VRM5) {
        return VRM5[raw % 5U];
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        return PRESTON9[raw % 9U];
    }
    return raw < FACE_VISEME_COUNT
        ? raw
        : (uint8_t)(raw % FACE_VISEME_COUNT);
}

static sa_viseme_t sa_blended_viseme(const face_render_key_t *key)
{
    const sa_viseme_t first =
        SA_VISEMES[sa_viseme_index(key->viseme_set, key->viseme)];
    const sa_viseme_t second =
        SA_VISEMES[sa_viseme_index(
            key->viseme_set, key->viseme_secondary)];
    sa_viseme_t output;
    output.open =
        (uint8_t)sa_mix(first.open, second.open, key->viseme_blend);
    output.width =
        (uint8_t)sa_mix(first.width, second.width, key->viseme_blend);
    output.round =
        (uint8_t)sa_mix(first.round, second.round, key->viseme_blend);
    output.press =
        (uint8_t)sa_mix(first.press, second.press, key->viseme_blend);
    output.teeth =
        (uint8_t)sa_mix(first.teeth, second.teeth, key->viseme_blend);
    output.tongue =
        (uint8_t)sa_mix(first.tongue, second.tongue, key->viseme_blend);
    return output;
}

size_t face_salvage_actor_count(void)
{
    return FACE_SALVAGE_ACTOR_COUNT;
}

const char *face_salvage_actor_slug(face_salvage_actor_style_t style)
{
    return sa_style_valid(style) ? SA_ACTORS[style].slug : NULL;
}

const char *face_salvage_actor_name(face_salvage_actor_style_t style)
{
    return sa_style_valid(style) ? SA_ACTORS[style].name : NULL;
}

bool face_salvage_actor_info(
    face_salvage_actor_style_t style,
    face_salvage_actor_info_t *info)
{
    if (!sa_style_valid(style) || info == NULL) {
        return false;
    }
    const sa_actor_def_t *actor = &SA_ACTORS[style];
    info->slug = actor->slug;
    info->name = actor->name;
    info->legacy_profile_id = actor->legacy_id;
    info->mouth_kind = actor->mouth_kind;
    info->deliberate_monocular = actor->monocular;
    info->estimated_ops_per_pixel = actor->ops;
    return true;
}

bool face_salvage_actor_from_legacy_id(
    uint8_t legacy_profile_id,
    face_salvage_actor_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_SALVAGE_ACTOR_COUNT; ++raw) {
        if (SA_ACTORS[raw].legacy_id == legacy_profile_id) {
            *style = (face_salvage_actor_style_t)raw;
            return true;
        }
    }
    return false;
}

bool face_salvage_actor_resolve(
    face_salvage_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_salvage_actor_pose_t *pose)
{
    if (!sa_style_valid(style) || render_key == NULL || pose == NULL) {
        return false;
    }
    const sa_actor_def_t *actor = &SA_ACTORS[style];
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, sizeof(pose->source));

    const uint8_t expression =
        render_key->stage_expression < SA_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = render_key->expression_weight;
    const sa_expression_t *target = &SA_EXPRESSIONS[expression];

#define SA_EXPR(field) ((int)sa_mix(0, target->field, expression_weight))
    const int eye_width_change = SA_EXPR(eye_width);
    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ACTIVE ||
        render_key->speech_phase == FACE_SPEECH_ENDING;
    pose->speech_pulse = pose->speaking
        ? (int16_t)(
            sa_wave(sample_clock, 8000U) *
            (32 + render_key->audio_level) / (127 * 48))
        : 0;
    const int gaze_x = sa_clamp(
        render_key->controls.look_x / 6 +
            render_key->head_yaw / 14 +
            SA_EXPR(gaze_x),
        -13,
        13);
    const int gaze_y = sa_clamp(
        render_key->controls.look_y / 8 +
            render_key->head_pitch / 16 +
            SA_EXPR(gaze_y),
        -10,
        10);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (actor->monocular && eye == 1U) {
            continue;
        }
        const uint8_t input_open = eye == 0U
            ? render_key->controls.eye_left_open
            : render_key->controls.eye_right_open;
        const uint8_t squint = eye == 0U
            ? render_key->eye_left_squint
            : render_key->eye_right_squint;
        int expression_open = eye == 0U
            ? SA_EXPR(eye_open_left)
            : SA_EXPR(eye_open_right);
        if (style == FACE_SALVAGE_ACTOR_RED_OPTIC) {
            expression_open *= 2;
        }
        pose->eye_x[eye] = actor->eye_x[eye];
        pose->eye_y[eye] = actor->eye_y;
        pose->eye_w[eye] =
            (int16_t)sa_clamp(actor->eye_w + eye_width_change, 18, 72);
        pose->eye_h[eye] = actor->eye_h;
        int aperture =
            (actor->eye_h * (int)input_open + 127) / 255 +
            expression_open - (int)squint / 22;
        aperture += ((int)render_key->affect_arousal - 128) / 48;
        aperture += pose->speech_pulse;
        if ((render_key->controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            aperture = 2;
        }
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            aperture += 2;
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            aperture -= 1;
        }
        pose->eye_aperture[eye] =
            (int16_t)sa_clamp(aperture, 2, actor->eye_h);
        pose->pupil_x[eye] =
            (int16_t)(actor->eye_x[eye] + gaze_x);
        pose->pupil_y[eye] =
            (int16_t)(
                actor->eye_y + gaze_y - pose->speech_pulse / 2);
        pose->pupil_radius[eye] = (int16_t)sa_clamp(
            actor->pupil +
                SA_EXPR(pupil) *
                    (style == FACE_SALVAGE_ACTOR_RED_OPTIC ? 2 : 1) -
                ((int)render_key->affect_arousal - 128) / 56 +
                ((int)render_key->attention - 128) / 96,
            2,
            actor->monocular ? 17 : 9);
        const int brow_raise = eye == 0U
            ? SA_EXPR(brow_raise_left)
            : SA_EXPR(brow_raise_right);
        const int brow_outer = eye == 0U
            ? render_key->brow_outer_left
            : render_key->brow_outer_right;
        pose->brow_y[eye] = (int16_t)sa_clamp(
            actor->eye_y - actor->eye_h / 2 - 8 +
                brow_raise - render_key->controls.brow / 24 -
                render_key->brow_inner / 28 - brow_outer / 32,
            12,
            50);
        if (style == FACE_SALVAGE_ACTOR_FILM_NOIR_ROGUE) {
            /* Reserve a readable acting band below the fixed fedora brim. */
            pose->brow_y[eye] =
                (int16_t)sa_clamp(pose->brow_y[eye] + 7, 34, 48);
        } else if (style == FACE_SALVAGE_ACTOR_HUB75_MASCOT) {
            pose->brow_y[eye] =
                (int16_t)sa_clamp(pose->brow_y[eye] + 10, 31, 42);
        } else if (style == FACE_SALVAGE_ACTOR_AMBER_TERMINAL) {
            pose->brow_y[eye] =
                (int16_t)sa_clamp(pose->brow_y[eye] + 5, 29, 43);
        }
        pose->brow_y[eye] = (int16_t)sa_clamp(
            pose->brow_y[eye] - pose->speech_pulse / 2, 12, 50);
        pose->brow_slope[eye] = (int16_t)sa_clamp(
            (eye == 0U
                ? SA_EXPR(brow_slope_left)
                : SA_EXPR(brow_slope_right)) +
                render_key->head_roll / 20 +
                brow_outer / 24,
            -11,
            11);
    }

    const sa_viseme_t viseme = sa_blended_viseme(render_key);
    const uint8_t viseme_weight = render_key->viseme_weight;
    pose->speech_open = (uint8_t)sa_mix(
        render_key->controls.mouth_open, viseme.open, viseme_weight);
    pose->speech_width = (uint8_t)sa_mix(
        render_key->controls.mouth_width, viseme.width, viseme_weight);
    pose->speech_round = (uint8_t)sa_mix(
        render_key->controls.mouth_round, viseme.round, viseme_weight);
    pose->speech_press = (uint8_t)sa_mix(
        render_key->controls.mouth_press, viseme.press, viseme_weight);
    pose->teeth = (uint8_t)sa_mix(
        render_key->controls.mouth_teeth, viseme.teeth, viseme_weight);
    pose->tongue =
        (uint8_t)sa_mix(render_key->tongue, viseme.tongue, viseme_weight);
    pose->cheek = (uint8_t)sa_clamp(
        (int)render_key->cheek + SA_EXPR(cheek), 0, 255);
    pose->mouth_x = 80;
    pose->mouth_y = actor->mouth_y;
    const int rounded_narrow = pose->speech_round / 18;
    pose->mouth_w = (int16_t)sa_clamp(
        20 + pose->speech_width * 44 / 255 -
            rounded_narrow + SA_EXPR(mouth_width),
        14,
        68);
    int mouth_height = 2 + pose->speech_open * 25 / 255 +
        SA_EXPR(mouth_open);
    mouth_height -= pose->speech_press * 7 / 255;
    const int maximum_mouth_height =
        style == FACE_SALVAGE_ACTOR_AMBER_TERMINAL ? 22
        : style == FACE_SALVAGE_ACTOR_FILM_NOIR_ROGUE ? 22
        : style == FACE_SALVAGE_ACTOR_NEON_MASK ? 24
        : style == FACE_SALVAGE_ACTOR_HUB75_MASCOT ? 20
        : style == FACE_SALVAGE_ACTOR_ZINE_ROGUE ? 18
        : 29;
    pose->mouth_h = (int16_t)sa_clamp(
        mouth_height, 2, maximum_mouth_height);
    pose->mouth_corner[0] = (int16_t)sa_clamp(
        render_key->mouth_corner_left / 12 +
            render_key->affect_valence / 18 +
            SA_EXPR(mouth_corner_left),
        -12,
        12);
    pose->mouth_corner[1] = (int16_t)sa_clamp(
        render_key->mouth_corner_right / 12 +
            render_key->affect_valence / 18 +
            SA_EXPR(mouth_corner_right),
        -12,
        12);
    pose->shoulder_lean_x = (int16_t)sa_clamp(
        render_key->body_lean_x / 12, -8, 8);
    pose->shoulder_lean_y = (int16_t)sa_clamp(
        render_key->body_lean_y / 16, -6, 6);
    pose->stage_expression = expression;
    pose->expression_weight = expression_weight;
    pose->activity = render_key->controls.expression;
    pose->speech_phase = render_key->speech_phase;
    pose->attention = render_key->attention;
    pose->deliberate_monocular = actor->monocular;
#undef SA_EXPR
    return true;
}

static void sa_status_rail(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose,
    int x,
    int y,
    uint16_t active,
    uint16_t inactive)
{
    const uint8_t phoneme = pose->source.phoneme == FACE_PHONEME_NONE
        ? 0U
        : (uint8_t)(pose->source.phoneme % 4U);
    const uint8_t values[5] = {
        (uint8_t)(pose->activity % 4U),
        (uint8_t)(pose->speech_phase % 4U),
        (uint8_t)(pose->source.viseme_set % 4U),
        phoneme,
        (uint8_t)(pose->source.schema_version % 4U),
    };
    for (size_t group = 0U; group < 5U; ++group) {
        for (uint8_t bit = 0U; bit < 2U; ++bit) {
            const bool on = (values[group] & (1U << bit)) != 0U;
            sa_rect(
                canvas,
                x + (int)group * 7 + (int)bit * 3,
                y,
                2,
                3,
                on ? active : inactive);
        }
    }
    const int attention_width = 4 + pose->attention * 30 / 255;
    sa_rect(canvas, x, y + 5, 34, 2, inactive);
    sa_rect(canvas, x, y + 5, attention_width, 2, active);
}

static void sa_draw_brows(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose,
    int half_width,
    int thickness,
    uint16_t color)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (pose->deliberate_monocular && eye == 1U) {
            continue;
        }
        const int direction = eye == 0U ? 1 : -1;
        const int slope = pose->brow_slope[eye] * direction;
        sa_thick_line(
            canvas,
            pose->eye_x[eye] - half_width,
            pose->brow_y[eye] - slope / 2,
            pose->eye_x[eye] + half_width,
            pose->brow_y[eye] + slope / 2,
            thickness,
            color);
    }
}

static void sa_draw_cavity_mouth(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int x = pose->mouth_x - pose->mouth_w / 2;
    const int y = pose->mouth_y - pose->mouth_h / 2;
    const int radius = (int)sa_clamp(
        pose->speech_round * pose->mouth_w / 510, 3, pose->mouth_w / 2);
    sa_round_rect(
        canvas,
        x - 2,
        y - 2,
        pose->mouth_w + 4,
        pose->mouth_h + 4,
        radius + 2,
        outline);
    sa_round_rect(
        canvas,
        x,
        y,
        pose->mouth_w,
        pose->mouth_h,
        radius,
        cavity);
    if (pose->mouth_h <= 4) {
        sa_thick_line(
            canvas,
            x,
            pose->mouth_y - pose->mouth_corner[0] / 3,
            x + pose->mouth_w,
            pose->mouth_y - pose->mouth_corner[1] / 3,
            2,
            outline);
        return;
    }
    if (pose->teeth > 70U) {
        const int teeth_height = 1 + pose->teeth * pose->mouth_h / 1020;
        sa_round_rect(
            canvas,
            x + 2,
            y + 1,
            pose->mouth_w - 4,
            teeth_height,
            1,
            teeth);
    }
    if (pose->tongue > 52U && pose->mouth_h >= 8) {
        const int tongue_height =
            1 + pose->tongue * pose->mouth_h / 1020;
        sa_round_rect(
            canvas,
            x + 4,
            y + pose->mouth_h - tongue_height - 1,
            pose->mouth_w - 8,
            tongue_height,
            2,
            tongue);
    }
}

static void sa_draw_line_mouth(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose,
    uint16_t line,
    uint16_t cavity)
{
    const int left = pose->mouth_x - pose->mouth_w / 2;
    const int right = pose->mouth_x + pose->mouth_w / 2;
    const int left_y = pose->mouth_y - pose->mouth_corner[0] / 2;
    const int right_y = pose->mouth_y - pose->mouth_corner[1] / 2;
    if (pose->mouth_h <= 5) {
        const int mid_y =
            (left_y + right_y) / 2 -
            (pose->mouth_corner[0] + pose->mouth_corner[1]) / 5;
        sa_thick_line(
            canvas, left, left_y, pose->mouth_x, mid_y, 2, line);
        sa_thick_line(
            canvas, pose->mouth_x, mid_y, right, right_y, 2, line);
        return;
    }
    sa_triangle(
        canvas,
        left,
        left_y,
        pose->mouth_x,
        pose->mouth_y - pose->mouth_h / 2,
        right,
        right_y,
        line);
    sa_triangle(
        canvas,
        left,
        left_y,
        right,
        right_y,
        pose->mouth_x,
        pose->mouth_y + pose->mouth_h / 2,
        line);
    sa_ellipse(
        canvas,
        pose->mouth_x,
        pose->mouth_y,
        sa_clamp(pose->mouth_w / 2 - 3, 2, 32),
        sa_clamp(pose->mouth_h / 2 - 2, 1, 13),
        cavity);
}

static void sa_draw_terminal_eye(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose,
    size_t eye,
    uint16_t glow,
    uint16_t bright,
    uint16_t dark)
{
    const int width = pose->eye_w[eye];
    const int aperture = pose->eye_aperture[eye];
    const int x = pose->eye_x[eye] - width / 2;
    const int y = pose->eye_y[eye] - aperture / 2;
    /*
     * Pixel-glyph eye: a deforming filled cell, not a static hollow bezel.
     * The large dark iris remains legible across a room and stays padded from
     * the luminous hull even on extreme gaze.
     */
    sa_rect(canvas, x - 2, y - 2, width + 4, aperture + 4, glow);
    sa_rect(canvas, x, y, width, aperture, bright);
    const int pupil_w = sa_clamp(width / 3, 8, 13);
    const int pupil_h = sa_clamp(aperture / 2, 2, 14);
    const int pupil_x = (int)sa_clamp(
        pose->pupil_x[eye],
        x + pupil_w / 2 + 2,
        x + width - pupil_w / 2 - 2);
    const int pupil_y = (int)sa_clamp(
        pose->pupil_y[eye],
        y + pupil_h / 2,
        y + aperture - pupil_h / 2);
    sa_rect(
        canvas,
        pupil_x - pupil_w / 2,
        pupil_y - pupil_h / 2,
        pupil_w,
        pupil_h,
        dark);
    if (pupil_h >= 6) {
        sa_rect(
            canvas,
            pupil_x - pupil_w / 2 + 2,
            pupil_y - pupil_h / 2 + 2,
            2,
            3,
            bright);
    }
}

static void sa_draw_amber_terminal(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t background = SA_RGB565(11, 6, 2);
    const uint16_t shell = SA_RGB565(47, 24, 4);
    const uint16_t panel = SA_RGB565(21, 12, 2);
    const uint16_t dim = SA_RGB565(111, 54, 4);
    const uint16_t amber = SA_RGB565(255, 163, 20);
    const uint16_t bright = SA_RGB565(255, 229, 126);
    const uint16_t black = SA_RGB565(5, 3, 1);
    const uint16_t blush = SA_RGB565(234, 71, 16);
    sa_clear(canvas, background);

    sa_round_rect(canvas, 14, 8, 132, 104, 8, shell);
    sa_rect(canvas, 18, 12, 124, 96, panel);
    sa_rect(canvas, 22, 18, 116, 4, dim);
    sa_rect(canvas, 22, 100, 116, 3, dim);
    sa_rect(canvas, 23, 25, 3, 70, dim);
    sa_rect(canvas, 134, 25, 3, 70, dim);

    const int lean = pose->shoulder_lean_x;
    sa_triangle(
        canvas,
        28 + lean,
        104,
        54 + lean,
        82 + pose->shoulder_lean_y,
        80 + lean,
        104,
        shell);
    sa_triangle(
        canvas,
        80 + lean,
        104,
        106 + lean,
        82 + pose->shoulder_lean_y,
        132 + lean,
        104,
        shell);
    /* Headset/operator silhouette fixes the face to a readable character. */
    sa_round_rect(canvas, 31, 29, 98, 65, 16, shell);
    sa_rect(canvas, 36, 34, 88, 55, panel);
    sa_rect(canvas, 27, 45, 8, 30, dim);
    sa_rect(canvas, 125, 45, 8, 30, dim);
    sa_line(canvas, 129, 68, 139, 76, dim);
    sa_rect(canvas, 137, 74, 4, 8, amber);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        sa_draw_terminal_eye(
            canvas, pose, eye, dim, amber, black);
    }
    sa_draw_brows(canvas, pose, 15, 2, bright);

    if (pose->cheek > 80U) {
        const int width = 2 + pose->cheek / 64;
        sa_rect(canvas, 34, 70, width * 2, 2, blush);
        sa_rect(canvas, 126 - width * 2, 70, width * 2, 2, blush);
    }

    /* A large legible pixel mouth, with viseme-controlled block topology. */
    const int left = pose->mouth_x - pose->mouth_w / 2;
    const int right = pose->mouth_x + pose->mouth_w / 2;
    const int top = pose->mouth_y - pose->mouth_h / 2;
    const int bottom = pose->mouth_y + pose->mouth_h / 2;
    if (pose->mouth_h <= 4) {
        sa_thick_line(
            canvas,
            left,
            pose->mouth_y - pose->mouth_corner[0] / 3,
            pose->mouth_x,
            pose->mouth_y -
                (pose->mouth_corner[0] + pose->mouth_corner[1]) / 3,
            3,
            amber);
        sa_thick_line(
            canvas,
            pose->mouth_x,
            pose->mouth_y -
                (pose->mouth_corner[0] + pose->mouth_corner[1]) / 3,
            right,
            pose->mouth_y - pose->mouth_corner[1] / 3,
            3,
            amber);
    } else {
        sa_rect(canvas, left - 2, top, 4, pose->mouth_h, amber);
        sa_rect(canvas, right - 2, top, 4, pose->mouth_h, amber);
        sa_rect(canvas, left, top - 2, pose->mouth_w, 4, amber);
        sa_rect(canvas, left, bottom - 2, pose->mouth_w, 4, amber);
        sa_rect(
            canvas,
            left + 4,
            top + 3,
            sa_clamp(pose->mouth_w - 8, 2, 60),
            sa_clamp(pose->mouth_h - 6, 2, 22),
            black);
        if (pose->teeth > 100U) {
            sa_rect(
                canvas,
                left + 5,
                top + 3,
                pose->mouth_w - 10,
                2 + pose->teeth / 96,
                bright);
        }
        if (pose->tongue > 100U && pose->mouth_h > 10) {
            sa_rect(
                canvas,
                left + 7,
                bottom - 5,
                pose->mouth_w - 14,
                3,
                blush);
        }
    }
    sa_status_rail(canvas, pose, 23, 106, amber, dim);
}

static bool sa_noir_inside_face(int x, int y)
{
    const int dx = x - 80;
    const int dy = y - 62;
    return dx * dx * 45 * 45 + dy * dy * 50 * 50 <=
        43 * 43 * 50 * 50;
}

static void sa_draw_film_noir(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t paper = SA_RGB565(213, 207, 187);
    const uint16_t gray = SA_RGB565(118, 115, 105);
    const uint16_t smoke = SA_RGB565(65, 64, 61);
    const uint16_t ink = SA_RGB565(17, 18, 18);
    const uint16_t white = SA_RGB565(244, 239, 218);
    const uint16_t red = SA_RGB565(135, 38, 29);
    sa_clear(canvas, paper);

    /* Stable photographic frame and coat silhouette. */
    sa_rect(canvas, 8, 8, 144, 4, ink);
    sa_rect(canvas, 8, 108, 144, 4, ink);
    sa_rect(canvas, 8, 12, 4, 96, ink);
    sa_rect(canvas, 148, 12, 4, 96, ink);
    const int lean = pose->shoulder_lean_x;
    sa_triangle(canvas, 19 + lean, 108, 53 + lean, 80, 80, 108, ink);
    sa_triangle(canvas, 80, 108, 108 + lean, 80, 141 + lean, 108, ink);
    sa_ellipse(canvas, 80, 62, 45, 50, gray);

    /* Fedora creates a clear film-noir silhouette without moving topology. */
    sa_triangle(canvas, 35, 31, 55, 12, 105, 13, ink);
    sa_triangle(canvas, 35, 31, 105, 13, 125, 32, ink);
    sa_rect(canvas, 23, 28, 114, 7, ink);
    sa_rect(canvas, 54, 24, 54, 4, smoke);

    /* Ordered halftone shadow: no clock noise, no coordinate jitter. */
    for (int y = 42; y < 101; y += 3) {
        for (int x = 38; x < 82; x += 3) {
            if (sa_noir_inside_face(x, y) &&
                ((x + y * 2) % 7) < 3) {
                sa_rect(canvas, x, y, 1, 1, ink);
            }
        }
    }
    sa_triangle(canvas, 36, 43, 79, 43, 51, 101, smoke);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye];
        const int cy = pose->eye_y[eye];
        const int half_w = pose->eye_w[eye] / 2;
        const int half_h = pose->eye_aperture[eye] / 2;
        sa_triangle(
            canvas,
            cx - half_w,
            cy,
            cx,
            cy - half_h,
            cx + half_w,
            cy,
            ink);
        sa_triangle(
            canvas,
            cx - half_w,
            cy,
            cx + half_w,
            cy,
            cx,
            cy + half_h,
            ink);
        sa_ellipse(
            canvas,
            cx,
            cy,
            sa_clamp(half_w - 3, 2, 14),
            sa_clamp(half_h - 2, 1, 11),
            white);
        const int pupil_rx = sa_clamp(
            pose->pupil_radius[eye], 1, sa_clamp(half_w - 3, 1, 12));
        const int pupil_ry = sa_clamp(
            pose->pupil_radius[eye] + 1,
            1,
            sa_clamp(half_h - 1, 1, 8));
        const int pupil_x = (int)sa_clamp(
            pose->pupil_x[eye],
            cx - half_w + pupil_rx + 2,
            cx + half_w - pupil_rx - 2);
        const int pupil_y = half_h <= pupil_ry
            ? cy
            : (int)sa_clamp(
                pose->pupil_y[eye],
                cy - half_h + pupil_ry,
                cy + half_h - pupil_ry);
        sa_ellipse(
            canvas,
            pupil_x,
            pupil_y,
            pupil_rx,
            pupil_ry,
            ink);
        if (pose->stage_expression == FACE_EXPRESSION_JOY ||
            pose->stage_expression == FACE_EXPRESSION_WARM) {
            sa_thick_line(
                canvas,
                cx - half_w + 2,
                cy + half_h - 2,
                cx,
                cy + half_h - 4,
                2,
                ink);
            sa_thick_line(
                canvas,
                cx,
                cy + half_h - 4,
                cx + half_w - 2,
                cy + half_h - 2,
                2,
                ink);
        } else if (
            pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            const int inward = eye == 0U ? 4 : -4;
            sa_thick_line(
                canvas,
                cx - half_w + 1,
                cy - half_h + (eye == 0U ? 1 : 5),
                cx + half_w - 1,
                cy - half_h + inward,
                3,
                ink);
        }
    }
    sa_draw_brows(canvas, pose, 16, 3, ink);
    sa_line(canvas, 80, 57, 73, 73, ink);
    sa_line(canvas, 73, 73, 82, 75, ink);
    /* The single spot-color mouth keeps speech readable over both shadows. */
    sa_draw_line_mouth(canvas, pose, red, ink);
    if (pose->cheek > 100U) {
        sa_thick_line(canvas, 103, 72, 119, 76, 2, red);
    }
    sa_status_rail(canvas, pose, 111, 99, red, gray);
}

static void sa_draw_neon_mask(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t background = SA_RGB565(2, 5, 18);
    const uint16_t blue_glow = SA_RGB565(10, 52, 82);
    const uint16_t cyan = SA_RGB565(24, 239, 255);
    const uint16_t cyan_white = SA_RGB565(210, 255, 255);
    const uint16_t magenta_glow = SA_RGB565(71, 8, 68);
    const uint16_t magenta = SA_RGB565(255, 53, 222);
    const uint16_t face = SA_RGB565(8, 16, 34);
    const uint16_t cavity = SA_RGB565(2, 2, 10);
    const uint16_t tongue = SA_RGB565(255, 71, 129);
    sa_clear(canvas, background);

    /* Restrained three-layer faceplate: readable at native scale. */
    sa_round_rect(canvas, 18, 10, 124, 101, 26, blue_glow);
    sa_round_rect(canvas, 22, 13, 116, 95, 23, cyan);
    sa_round_rect(canvas, 26, 17, 108, 87, 20, face);
    sa_triangle(canvas, 26, 31, 16, 52, 26, 75, magenta_glow);
    sa_triangle(canvas, 134, 31, 144, 52, 134, 75, magenta_glow);
    sa_rect(canvas, 21, 47, 5, 15, magenta);
    sa_rect(canvas, 134, 47, 5, 15, magenta);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int width = pose->eye_w[eye];
        const int aperture = pose->eye_aperture[eye];
        const int x = pose->eye_x[eye] - width / 2;
        const int y = pose->eye_y[eye] - aperture / 2;
        sa_round_rect(
            canvas, x - 3, y - 3, width + 6, aperture + 6,
            sa_clamp(aperture / 2 + 2, 3, 17), blue_glow);
        sa_round_rect(
            canvas, x - 1, y - 1, width + 2, aperture + 2,
            sa_clamp(aperture / 2, 2, 15), cyan);
        sa_round_rect(
            canvas, x + 2, y + 2, width - 4, aperture - 4,
            sa_clamp(aperture / 2 - 2, 1, 13), cavity);
        const int pupil_rx = sa_clamp(
            pose->pupil_radius[eye], 1, sa_clamp(width / 2 - 4, 1, 9));
        const int pupil_ry = sa_clamp(
            pose->pupil_radius[eye] + 2,
            1,
            sa_clamp(aperture / 2 - 2, 1, 10));
        const int pupil_x = (int)sa_clamp(
            pose->pupil_x[eye],
            x + pupil_rx + 3,
            x + width - pupil_rx - 3);
        const int pupil_y = aperture <= pupil_ry * 2
            ? y + aperture / 2
            : (int)sa_clamp(
                pose->pupil_y[eye],
                y + pupil_ry + 1,
                y + aperture - pupil_ry - 1);
        sa_ellipse(
            canvas,
            pupil_x,
            pupil_y,
            pupil_rx,
            pupil_ry,
            magenta);
        if (pupil_ry >= 4) {
            sa_rect(canvas, pupil_x - 1, pupil_y - 3, 2, 4, cyan_white);
        }
    }
    sa_draw_brows(canvas, pose, 16, 3, magenta);
    if (pose->cheek > 72U) {
        sa_line(canvas, 32, 71, 46, 75, magenta);
        sa_line(canvas, 114, 75, 128, 71, magenta);
    }
    sa_draw_cavity_mouth(
        canvas, pose, magenta, cavity, cyan_white, tongue);
    sa_status_rail(canvas, pose, 63, 106, cyan, blue_glow);
}

static void sa_draw_red_optic(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t background = SA_RGB565(8, 2, 4);
    const uint16_t shell_dark = SA_RGB565(28, 5, 8);
    const uint16_t shell = SA_RGB565(68, 10, 15);
    const uint16_t dim = SA_RGB565(120, 18, 25);
    const uint16_t red = SA_RGB565(244, 34, 42);
    const uint16_t hot = SA_RGB565(255, 174, 118);
    const uint16_t white = SA_RGB565(255, 244, 219);
    const uint16_t black = SA_RGB565(3, 1, 2);
    sa_clear(canvas, background);

    /* Mechanical mask silhouette and fixed optic socket. */
    sa_ellipse(canvas, 80, 60, 64, 52, shell_dark);
    sa_triangle(canvas, 22, 36, 10, 59, 24, 88, shell);
    sa_triangle(canvas, 138, 36, 150, 59, 136, 88, shell);
    sa_round_rect(canvas, 23, 15, 114, 91, 31, shell);
    sa_round_rect(canvas, 29, 20, 102, 82, 27, shell_dark);
    sa_ring(canvas, 80, 54, 38, 38, 5, dim, black);
    sa_ring(canvas, 80, 54, 32, 32, 3, red, black);

    const int aperture = pose->eye_aperture[0];
    const int eye_rx = pose->eye_w[0] / 2 - 8;
    const int eye_ry = sa_clamp(aperture / 2, 2, 27);
    /*
     * Real occluding shutters cover the decorative ring stack.  Their
     * expression-driven slope turns the reticle into an actual eyelid rig.
     */
    const int shutter_slope = pose->brow_slope[0] / 2;
    const int top_left = 54 - eye_ry - shutter_slope;
    const int top_right = 54 - eye_ry + shutter_slope;
    const int bottom_left = 54 + eye_ry + shutter_slope / 2;
    const int bottom_right = 54 + eye_ry - shutter_slope / 2;
    sa_quad(
        canvas,
        43,
        17,
        117,
        17,
        117,
        top_right,
        43,
        top_left,
        shell_dark);
    sa_quad(
        canvas,
        43,
        bottom_left,
        117,
        bottom_right,
        117,
        92,
        43,
        92,
        shell_dark);
    sa_ellipse(canvas, 80, 54, eye_rx + 4, eye_ry + 4, dim);
    sa_ellipse(canvas, 80, 54, eye_rx + 1, eye_ry + 1, red);
    sa_ellipse(canvas, 80, 54, eye_rx - 3, sa_clamp(eye_ry - 3, 1, 24), black);

    const int pulse = pose->speech_pulse;
    const int iris_radius =
        sa_clamp(pose->pupil_radius[0] + pulse / 2, 5, 18);
    const int iris_radius_x = sa_clamp(
        iris_radius - sa_abs(pose->pupil_x[0] - 80) / 5,
        4,
        iris_radius);
    const int horizontal_margin = iris_radius_x + 5;
    const int pupil_x = horizontal_margin >= eye_rx
        ? 80
        : (int)sa_clamp(
            pose->pupil_x[0],
            80 - eye_rx + horizontal_margin,
            80 + eye_rx - horizontal_margin);
    const int pupil_margin = sa_clamp(eye_ry - 1, 1, 7);
    const int pupil_y = (int)sa_clamp(
        pose->pupil_y[0],
        54 - eye_ry + pupil_margin,
        54 + eye_ry - pupil_margin);
    sa_ring(
        canvas,
        pupil_x,
        pupil_y,
        iris_radius_x + 4,
        iris_radius + 4,
        3,
        hot,
        red);
    sa_ellipse(
        canvas,
        pupil_x,
        pupil_y,
        sa_clamp(iris_radius_x - 1, 2, 17),
        iris_radius - 1,
        black);
    sa_ellipse(
        canvas,
        pupil_x - iris_radius / 3,
        pupil_y - iris_radius / 3,
        sa_clamp(iris_radius / 4, 1, 4),
        sa_clamp(iris_radius / 4, 1, 4),
        white);
    /* Reapply shutters last so iris/glint never leak outside the aperture. */
    sa_quad(
        canvas,
        43,
        17,
        117,
        17,
        117,
        top_right,
        43,
        top_left,
        shell_dark);
    sa_quad(
        canvas,
        43,
        bottom_left,
        117,
        bottom_right,
        117,
        92,
        43,
        92,
        shell_dark);
    int upper_bend = 0;
    int lower_bend = 0;
    switch (pose->stage_expression) {
    case FACE_EXPRESSION_WARM:
        lower_bend = 4;
        break;
    case FACE_EXPRESSION_JOY:
        lower_bend = 9;
        break;
    case FACE_EXPRESSION_CONCERN:
        upper_bend = 4;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        upper_bend = 3;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        upper_bend = 6;
        break;
    case FACE_EXPRESSION_DETERMINED:
        upper_bend = 10;
        break;
    case FACE_EXPRESSION_SLEEPY:
        upper_bend = 4;
        lower_bend = 3;
        break;
    case FACE_EXPRESSION_EXCITED:
        lower_bend = 3;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        lower_bend = 5;
        break;
    case FACE_EXPRESSION_NEUTRAL:
    case FACE_EXPRESSION_SURPRISE:
    case FACE_EXPRESSION_COUNT:
    case FACE_EXPRESSION_CUSTOM:
        break;
    }
    const int top_mid = (top_left + top_right) / 2 + upper_bend;
    const int bottom_mid =
        (bottom_left + bottom_right) / 2 - lower_bend;
    if (upper_bend > 0) {
        sa_triangle(
            canvas,
            43,
            top_left,
            117,
            top_right,
            80,
            top_mid,
            shell_dark);
    }
    if (lower_bend > 0) {
        sa_triangle(
            canvas,
            43,
            bottom_left,
            117,
            bottom_right,
            80,
            bottom_mid,
            shell_dark);
    }
    sa_thick_line(canvas, 43, top_left, 80, top_mid, 2, red);
    sa_thick_line(canvas, 80, top_mid, 117, top_right, 2, red);
    sa_thick_line(canvas, 43, bottom_left, 80, bottom_mid, 2, dim);
    sa_thick_line(canvas, 80, bottom_mid, 117, bottom_right, 2, dim);

    /*
     * The optic is mouthless by design.  Speech instead deforms its iris ring
     * and drives two restrained side meters.  Viseme width/roundness changes
     * the meter distribution, so every OVR shape remains inspectable.
     */
    const int meter =
        4 + pose->speech_open * 29 / 255 +
        pose->source.audio_level * 6 / 255;
    const int left_meter =
        sa_clamp(meter + (128 - pose->speech_round) / 22, 3, 38);
    const int right_meter =
        sa_clamp(meter + (pose->speech_width - 128) / 22, 3, 38);
    sa_rect(canvas, 31, 91 - left_meter, 5, left_meter, dim);
    sa_rect(canvas, 124, 91 - right_meter, 5, right_meter, dim);
    for (int y = 0; y < left_meter; y += 7) {
        sa_rect(canvas, 31, 88 - y, 5, 3, red);
    }
    for (int y = 0; y < right_meter; y += 7) {
        sa_rect(canvas, 124, 88 - y, 5, 3, hot);
    }

    const int brow_y = pose->brow_y[0] - 1;
    const int slope = pose->brow_slope[0];
    sa_thick_line(
        canvas, 52, brow_y - slope / 2, 80, brow_y + slope / 3,
        4, hot);
    sa_thick_line(
        canvas, 80, brow_y + slope / 3, 108, brow_y + slope / 2,
        4, hot);
    if (pose->cheek > 120U) {
        sa_diamond(canvas, 45, 91, 5, 3, red);
        sa_diamond(canvas, 115, 91, 5, 3, red);
    }
    const uint8_t viseme_code = sa_viseme_index(
        pose->source.viseme_set, pose->source.viseme);
    for (uint8_t bit = 0U; bit < 4U; ++bit) {
        sa_rect(
            canvas,
            63 + bit * 10,
            96,
            7,
            3,
            (viseme_code & (1U << bit)) != 0U ? hot : dim);
    }
    sa_status_rail(canvas, pose, 63, 105, hot, dim);
}

static int sa_snap2(int value)
{
    return value >= 0 ? (value / 2) * 2 : -((-value / 2) * 2);
}

static void sa_draw_hub75_mascot(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t background = SA_RGB565(2, 4, 9);
    const uint16_t blue = SA_RGB565(30, 102, 255);
    const uint16_t cyan = SA_RGB565(32, 238, 255);
    const uint16_t green = SA_RGB565(32, 244, 100);
    const uint16_t yellow = SA_RGB565(255, 225, 40);
    const uint16_t red = SA_RGB565(255, 45, 54);
    const uint16_t pink = SA_RGB565(255, 85, 190);
    const uint16_t white = SA_RGB565(250, 248, 222);
    const uint16_t black = SA_RGB565(3, 4, 8);
    sa_clear(canvas, background);

    /*
     * Everything is a large, two-pixel-aligned bitmap silhouette.  There is
     * no stochastic glitch layer and no single-pixel decorative snow.
     */
    const int lean = sa_snap2(pose->shoulder_lean_x);
    sa_rect(canvas, 20 + lean, 92, 120, 16, blue);
    sa_rect(canvas, 28, 20, 104, 76, cyan);
    sa_rect(canvas, 22, 34, 8, 38, green);
    sa_rect(canvas, 130, 34, 8, 38, green);
    sa_rect(canvas, 34, 16, 92, 8, blue);
    sa_rect(canvas, 38, 24, 84, 68, black);
    sa_rect(canvas, 44, 28, 72, 6, blue);
    sa_rect(canvas, 31, 76, 8, 12, yellow);
    sa_rect(canvas, 121, 76, 8, 12, yellow);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int width = sa_snap2(pose->eye_w[eye]);
        const int aperture = sa_clamp(
            sa_snap2(pose->eye_aperture[eye]), 2, 30);
        const int x = sa_snap2(pose->eye_x[eye] - width / 2);
        const int y = sa_snap2(pose->eye_y[eye] - aperture / 2);
        sa_rect(canvas, x - 2, y - 2, width + 4, aperture + 4, blue);
        sa_rect(canvas, x, y, width, aperture, white);
        const int pupil = sa_clamp(
            sa_snap2(pose->pupil_radius[eye]),
            1,
            sa_clamp(aperture / 2, 1, 8));
        const int pupil_x = sa_snap2((int)sa_clamp(
            pose->pupil_x[eye],
            x + pupil + 2,
            x + width - pupil - 2));
        const int pupil_y = aperture <= pupil * 2
            ? y + aperture / 2
            : sa_snap2((int)sa_clamp(
                pose->pupil_y[eye],
                y + pupil,
                y + aperture - pupil));
        sa_rect(
            canvas,
            pupil_x - pupil,
            pupil_y - pupil,
            pupil * 2,
            pupil * 2,
            black);
        sa_rect(canvas, pupil_x - 1, pupil_y - 1, 2, 2, green);
    }

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye];
        const int slope = pose->brow_slope[eye];
        const int y = sa_snap2(pose->brow_y[eye]);
        sa_thick_line(
            canvas,
            cx - 14,
            sa_snap2(y - slope / 2),
            cx + 14,
            sa_snap2(y + slope / 2),
            4,
            yellow);
    }
    if (pose->cheek > 68U) {
        const int cheek_width = 4 + pose->cheek / 48;
        sa_rect(canvas, 33, 69, cheek_width, 4, pink);
        sa_rect(canvas, 127 - cheek_width, 69, cheek_width, 4, pink);
    }

    const int mouth_width = sa_clamp(sa_snap2(pose->mouth_w), 14, 64);
    const int mouth_height = sa_clamp(sa_snap2(pose->mouth_h), 2, 28);
    const int left = sa_snap2(pose->mouth_x - mouth_width / 2);
    const int top = sa_snap2(pose->mouth_y - mouth_height / 2);
    if (mouth_height <= 4) {
        sa_rect(canvas, left, top, mouth_width, 4, red);
        const int lift_left = sa_snap2(pose->mouth_corner[0] / 2);
        const int lift_right = sa_snap2(pose->mouth_corner[1] / 2);
        sa_rect(canvas, left, top - lift_left, 6, 4, red);
        sa_rect(
            canvas, left + mouth_width - 6, top - lift_right, 6, 4, red);
    } else {
        sa_rect(canvas, left - 2, top - 2, mouth_width + 4, mouth_height + 4, red);
        sa_rect(canvas, left, top, mouth_width, mouth_height, black);
        if (pose->teeth > 72U) {
            sa_rect(
                canvas,
                left + 2,
                top,
                mouth_width - 4,
                sa_clamp(2 + pose->teeth / 72, 2, 5),
                white);
        }
        if (pose->tongue > 80U && mouth_height >= 8) {
            sa_rect(
                canvas,
                left + 4,
                top + mouth_height - 4,
                mouth_width - 8,
                4,
                pink);
        }
    }
    /* Fixed four-color timing legend, intentionally dashboard-like. */
    const uint16_t rail_colors[4] = {red, yellow, green, blue};
    for (size_t index = 0U; index < 4U; ++index) {
        const bool on = index <= pose->speech_phase;
        sa_rect(
            canvas,
            65 + (int)index * 8,
            104,
            6,
            4,
            on ? rail_colors[index] : black);
    }
    sa_status_rail(canvas, pose, 104, 103, green, blue);
}

static bool sa_zine_inside_face(int x, int y)
{
    const int dx = x - 80;
    const int dy = y - 60;
    return dx * dx * 44 * 44 + dy * dy * 48 * 48 <=
        42 * 42 * 48 * 48;
}

static void sa_draw_zine_rogue(
    sa_canvas_t *canvas,
    const face_salvage_actor_pose_t *pose)
{
    const uint16_t paper = SA_RGB565(250, 231, 180);
    const uint16_t ink = SA_RGB565(23, 17, 21);
    const uint16_t red = SA_RGB565(225, 37, 48);
    const uint16_t pink = SA_RGB565(255, 108, 115);
    const uint16_t white = SA_RGB565(255, 248, 218);
    const uint16_t gray = SA_RGB565(135, 116, 100);
    sa_clear(canvas, paper);

    /* Print registration marks anchor the poster; none approach the edge. */
    sa_line(canvas, 10, 16, 26, 16, ink);
    sa_line(canvas, 18, 8, 18, 24, ink);
    sa_line(canvas, 134, 16, 150, 16, red);
    sa_line(canvas, 142, 8, 142, 24, red);
    sa_rect(canvas, 12, 104, 42, 4, red);
    sa_rect(canvas, 106, 104, 42, 4, ink);

    const int lean = pose->shoulder_lean_x;
    sa_triangle(canvas, 17 + lean, 105, 53 + lean, 81, 80, 109, ink);
    sa_triangle(canvas, 80, 109, 108 + lean, 81, 143 + lean, 105, red);
    sa_ellipse(canvas, 80, 61, 44, 49, ink);
    sa_ellipse(canvas, 80, 62, 39, 44, paper);

    /* Jagged punk hair and a large red scarf distinguish it from noir. */
    sa_triangle(canvas, 38, 48, 42, 18, 59, 36, ink);
    sa_triangle(canvas, 48, 35, 65, 10, 75, 33, ink);
    sa_triangle(canvas, 67, 32, 85, 8, 91, 34, ink);
    sa_triangle(canvas, 84, 33, 111, 14, 106, 45, ink);
    sa_triangle(canvas, 101, 36, 125, 27, 117, 54, ink);
    sa_triangle(canvas, 46, 83, 80, 109, 115, 84, red);
    sa_triangle(canvas, 59, 88, 81, 100, 102, 88, pink);
    /* Permanent jaw matte keeps large visemes from erasing the character. */
    sa_ellipse(canvas, 80, 81, 32, 19, paper);
    sa_thick_line(canvas, 50, 87, 80, 101, 3, ink);
    sa_thick_line(canvas, 80, 101, 110, 87, 3, ink);

    /* Stable coarse halftone cheek patch, clearly intentional print texture. */
    for (int y = 49; y < 82; y += 4) {
        for (int x = 86; x < 119; x += 4) {
            if (sa_zine_inside_face(x, y) &&
                ((x / 4 + y / 4) & 1) == 0) {
                sa_rect(canvas, x, y, 2, 2, gray);
            }
        }
    }

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye];
        const int cy = pose->eye_y[eye];
        const int half_w = pose->eye_w[eye] / 2;
        const int half_h = pose->eye_aperture[eye] / 2;
        sa_diamond(canvas, cx, cy, half_w, half_h, ink);
        sa_diamond(
            canvas,
            cx,
            cy,
            sa_clamp(half_w - 3, 2, 15),
            sa_clamp(half_h - 2, 1, 12),
            white);
        const int pupil_radius = sa_clamp(
            pose->pupil_radius[eye],
            1,
            sa_clamp(half_h - 1, 1, 8));
        const int pupil_x = (int)sa_clamp(
            pose->pupil_x[eye],
            cx - half_w + pupil_radius + 3,
            cx + half_w - pupil_radius - 3);
        const int pupil_y = half_h <= pupil_radius
            ? cy
            : (int)sa_clamp(
                pose->pupil_y[eye],
                cy - half_h + pupil_radius,
                cy + half_h - pupil_radius);
        sa_diamond(
            canvas,
            pupil_x,
            pupil_y,
            pupil_radius,
            sa_clamp(pupil_radius + 1, 1, half_h),
            eye == 0U ? red : ink);
    }
    sa_draw_brows(canvas, pose, 16, 4, ink);
    sa_line(canvas, 80, 57, 73, 72, ink);
    sa_line(canvas, 73, 72, 82, 74, red);
    sa_draw_cavity_mouth(canvas, pose, ink, ink, white, red);
    if (pose->cheek > 90U) {
        sa_thick_line(canvas, 36, 69, 47, 73, 2, red);
    }
    sa_status_rail(canvas, pose, 15, 94, red, gray);
}

bool face_salvage_actor_render(
    face_salvage_actor_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!sa_style_valid(style) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_SALVAGE_ACTOR_PIXEL_COUNT) {
        return false;
    }
    face_salvage_actor_pose_t pose;
    if (!face_salvage_actor_resolve(
            style, render_key, sample_clock, &pose)) {
        return false;
    }
    sa_canvas_t canvas = {rgb565};
    switch (style) {
    case FACE_SALVAGE_ACTOR_AMBER_TERMINAL:
        sa_draw_amber_terminal(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_FILM_NOIR_ROGUE:
        sa_draw_film_noir(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_NEON_MASK:
        sa_draw_neon_mask(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_RED_OPTIC:
        sa_draw_red_optic(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_HUB75_MASCOT:
        sa_draw_hub75_mascot(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_ZINE_ROGUE:
        sa_draw_zine_rogue(&canvas, &pose);
        break;
    case FACE_SALVAGE_ACTOR_COUNT:
        return false;
    }
    return true;
}

bool face_salvage_actor_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_salvage_actor_style_t style;
    return face_salvage_actor_from_legacy_id(legacy_profile_id, &style) &&
        face_salvage_actor_render(
            style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
