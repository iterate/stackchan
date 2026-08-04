#include "face_robot_redux_actors.h"

#include "face_pose.h"
#include "face_stage.h"

#include <string.h>

#define RR_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    RR_SAFE = 4,
    RR_EXPRESSION_COUNT = 11,
};

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t mouth_kind;
    bool mouthless;
    bool monocular;
    uint8_t ops;
    int8_t eye_x[2];
    int8_t eye_y;
    uint8_t eye_w;
    uint8_t eye_h;
    int8_t mouth_y;
} rr_actor_def_t;

typedef struct {
    int8_t eye_open_left;
    int8_t eye_open_right;
    int8_t eye_width;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t brow_raise_left;
    int8_t brow_raise_right;
    int8_t brow_slope_left;
    int8_t brow_slope_right;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t mouth_corner_left;
    int8_t mouth_corner_right;
    int8_t head_roll;
    int8_t body_x;
    int8_t body_y;
    int8_t pupil;
    uint8_t cheek;
} rr_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t consonant;
} rr_viseme_t;

typedef struct {
    uint16_t *pixels;
} rr_canvas_t;

/*
 * IDs and names are explicit because integration must replace the existing
 * public metadata rather than silently draw a different actor under a stale
 * identity.
 */
static const rr_actor_def_t RR_ACTORS[FACE_ROBOT_REDUX_COUNT] = {
    [FACE_ROBOT_REDUX_ROBOEYES_ALERT] = {
        "roboeyes-alert-performer",
        "RoboEyes Alert Performer",
        9U,
        FACE_ROBOT_REDUX_MOUTH_NONE,
        true,
        false,
        10U,
        {47, 113},
        58,
        43U,
        49U,
        92,
    },
    [FACE_ROBOT_REDUX_ROBOEYES_SOFT] = {
        "roboeyes-soft-performer",
        "RoboEyes Soft Performer",
        10U,
        FACE_ROBOT_REDUX_MOUTH_NONE,
        true,
        false,
        12U,
        {50, 110},
        60,
        48U,
        44U,
        92,
    },
    [FACE_ROBOT_REDUX_M5_AVATAR_CLASSIC] = {
        "m5-avatar-classic-performer",
        "M5 Avatar Classic Performer",
        11U,
        FACE_ROBOT_REDUX_MOUTH_CAVITY,
        false,
        false,
        8U,
        {51, 109},
        49,
        27U,
        37U,
        87,
    },
    [FACE_ROBOT_REDUX_M5_AVATAR_MANGA] = {
        "m5-avatar-manga-performer",
        "M5 Avatar Manga Performer",
        12U,
        FACE_ROBOT_REDUX_MOUTH_MANGA,
        false,
        false,
        12U,
        {52, 108},
        53,
        42U,
        45U,
        91,
    },
    [FACE_ROBOT_REDUX_EVE_MINIMAL] = {
        "eve-minimal-performer",
        "EVE Minimal Performer",
        13U,
        FACE_ROBOT_REDUX_MOUTH_NONE,
        true,
        false,
        10U,
        {54, 106},
        55,
        34U,
        27U,
        94,
    },
    [FACE_ROBOT_REDUX_JIBO_ORB] = {
        "jibo-orb-performer",
        "Jibo Orb Performer",
        14U,
        FACE_ROBOT_REDUX_MOUTH_NONE,
        true,
        true,
        12U,
        {80, 0},
        50,
        62U,
        62U,
        94,
    },
};

/*
 * The eleven authored poses use only geometry for their primary read. Colour
 * remains actor identity, never the sole emotion signal.
 */
static const rr_expression_t RR_EXPRESSIONS[RR_EXPRESSION_COUNT] = {
    /* neutral */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0U},
    /* warm */
    {-2, -2, 4, 0, 1, -2, -2, 0, 0, 1, 5, 7, 7, 0, 0, -1, 0, 84U},
    /* joy */
    {-12, -12, 8, 0, 1, -3, -3, 0, 0, 8, 10, 12, 12, 0, 0, -3, 1, 224U},
    /* concern */
    {-2, 1, -2, -4, 4, -7, -7, 7, -7, -1, -5, -10, -9, -7, -3, 2, 1, 72U},
    /* surprise */
    {12, 12, 8, 0, -4, -9, -9, 0, 0, 14, -5, -4, -4, 0, 0, -4, -3, 0U},
    /* thoughtful */
    {-4, -10, 0, -11, -7, -5, 1, 5, -3, -2, -3, -5, 2, -7, -3, 1, 1, 30U},
    /* skeptical */
    {-13, -1, -3, 10, 0, -7, 2, 8, -7, -2, -5, -8, 5, 8, 3, 0, 0, 28U},
    /* determined */
    {-8, -8, 6, 0, 1, 3, 3, -8, 8, -1, 1, -6, -6, 0, 0, -2, 2, 16U},
    /* sleepy */
    {-17, -17, -7, -3, 7, 5, 5, 2, -2, 1, -5, 1, 1, -3, 0, 5, 2, 0U},
    /* excited */
    {10, 10, 11, 0, -5, -10, -10, 0, 0, 12, 12, 12, 12, 0, 0, -5, -2, 192U},
    /* embarrassed */
    {-7, -11, -4, 11, 6, -5, -7, 5, -5, -1, -3, 7, 2, 7, 4, 2, 0, 255U},
};

static const rr_viseme_t RR_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {230U, 166U, 24U, 0U, 72U, 56U, 3U},
    [FACE_VISEME_E] = {90U, 245U, 4U, 0U, 172U, 8U, 5U},
    [FACE_VISEME_I] = {52U, 224U, 2U, 0U, 124U, 6U, 7U},
    [FACE_VISEME_O] = {205U, 88U, 246U, 0U, 28U, 28U, 9U},
    [FACE_VISEME_U] = {118U, 62U, 255U, 0U, 16U, 24U, 11U},
    [FACE_VISEME_PP] = {3U, 164U, 18U, 255U, 0U, 0U, 13U},
    [FACE_VISEME_SS] = {42U, 242U, 2U, 34U, 246U, 0U, 15U},
    [FACE_VISEME_TH] = {76U, 190U, 16U, 0U, 110U, 255U, 17U},
    [FACE_VISEME_DD] = {86U, 180U, 12U, 0U, 202U, 84U, 19U},
    [FACE_VISEME_FF] = {34U, 206U, 6U, 58U, 255U, 0U, 21U},
    [FACE_VISEME_KK] = {138U, 184U, 34U, 0U, 42U, 82U, 23U},
    [FACE_VISEME_NN] = {50U, 172U, 16U, 12U, 102U, 54U, 25U},
    [FACE_VISEME_RR] = {108U, 142U, 132U, 0U, 42U, 34U, 27U},
    [FACE_VISEME_CH] = {82U, 214U, 22U, 0U, 154U, 22U, 29U},
    [FACE_VISEME_SIL] = {5U, 140U, 20U, 226U, 0U, 0U, 31U},
};

static int32_t rr_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t rr_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t rr_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t rr_wave(uint32_t sample_clock, uint32_t period)
{
    if (period < 2U) {
        return 0;
    }
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    const int32_t value = phase < half
        ? (int32_t)(phase * 254U / half) - 127
        : 127 - (int32_t)((phase - half) * 254U / half);
    return rr_clamp(value, -127, 127);
}

static bool rr_style_valid(face_robot_redux_style_t style)
{
    return (unsigned)style < (unsigned)FACE_ROBOT_REDUX_COUNT;
}

static void rr_put(rr_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x >= RR_SAFE && x < FACE_ROBOT_REDUX_WIDTH - RR_SAFE &&
        y >= RR_SAFE && y < FACE_ROBOT_REDUX_HEIGHT - RR_SAFE) {
        canvas->pixels[
            (size_t)y * FACE_ROBOT_REDUX_WIDTH + (size_t)x] = color;
    }
}

static void rr_clear(rr_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_ROBOT_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void rr_rect(
    rr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left =
        (int)rr_clamp(x, RR_SAFE, FACE_ROBOT_REDUX_WIDTH - RR_SAFE);
    const int right = (int)rr_clamp(
        x + width, RR_SAFE, FACE_ROBOT_REDUX_WIDTH - RR_SAFE);
    const int top =
        (int)rr_clamp(y, RR_SAFE, FACE_ROBOT_REDUX_HEIGHT - RR_SAFE);
    const int bottom = (int)rr_clamp(
        y + height, RR_SAFE, FACE_ROBOT_REDUX_HEIGHT - RR_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            canvas->pixels[
                (size_t)yy * FACE_ROBOT_REDUX_WIDTH + (size_t)xx] =
                color;
        }
    }
}

static void rr_line(
    rr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = rr_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -rr_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        rr_put(canvas, x0, y0, color);
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

static void rr_thick_line(
    rr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        rr_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void rr_ellipse(
    rr_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    const int left = (int)rr_clamp(
        cx - rx, RR_SAFE, FACE_ROBOT_REDUX_WIDTH - RR_SAFE - 1);
    const int right = (int)rr_clamp(
        cx + rx, RR_SAFE, FACE_ROBOT_REDUX_WIDTH - RR_SAFE - 1);
    const int top = (int)rr_clamp(
        cy - ry, RR_SAFE, FACE_ROBOT_REDUX_HEIGHT - RR_SAFE - 1);
    const int bottom = (int)rr_clamp(
        cy + ry, RR_SAFE, FACE_ROBOT_REDUX_HEIGHT - RR_SAFE - 1);
    const int64_t limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t dx = x - cx;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= limit) {
                rr_put(canvas, x, y, color);
            }
        }
    }
}

static int32_t rr_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void rr_triangle(
    rr_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int left = (int)rr_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        RR_SAFE,
        FACE_ROBOT_REDUX_WIDTH - RR_SAFE - 1);
    const int right = (int)rr_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        RR_SAFE,
        FACE_ROBOT_REDUX_WIDTH - RR_SAFE - 1);
    const int top = (int)rr_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        RR_SAFE,
        FACE_ROBOT_REDUX_HEIGHT - RR_SAFE - 1);
    const int bottom = (int)rr_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        RR_SAFE,
        FACE_ROBOT_REDUX_HEIGHT - RR_SAFE - 1);
    const int32_t orientation = rr_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = rr_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = rr_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = rr_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                rr_put(canvas, x, y, color);
            }
        }
    }
}

static void rr_quad(
    rr_canvas_t *canvas,
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
    rr_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    rr_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void rr_round_rect(
    rr_canvas_t *canvas,
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
    radius = (int)rr_clamp(radius, 0, width / 2);
    radius = (int)rr_clamp(radius, 0, height / 2);
    rr_rect(canvas, x + radius, y, width - radius * 2, height, color);
    rr_rect(canvas, x, y + radius, width, height - radius * 2, color);
    if (radius > 0) {
        rr_ellipse(canvas, x + radius, y + radius, radius, radius, color);
        rr_ellipse(
            canvas,
            x + width - radius - 1,
            y + radius,
            radius,
            radius,
            color);
        rr_ellipse(
            canvas,
            x + radius,
            y + height - radius - 1,
            radius,
            radius,
            color);
        rr_ellipse(
            canvas,
            x + width - radius - 1,
            y + height - radius - 1,
            radius,
            radius,
            color);
    }
}

static void rr_ring(
    rr_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    int thickness,
    uint16_t outer,
    uint16_t inner)
{
    rr_ellipse(canvas, cx, cy, rx, ry, outer);
    rr_ellipse(
        canvas,
        cx,
        cy,
        rr_clamp(rx - thickness, 1, rx),
        rr_clamp(ry - thickness, 1, ry),
        inner);
}

static void rr_outline_round_rect(
    rr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int radius,
    int thickness,
    uint16_t outline,
    uint16_t inner)
{
    rr_round_rect(canvas, x, y, width, height, radius, outline);
    rr_round_rect(
        canvas,
        x + thickness,
        y + thickness,
        width - thickness * 2,
        height - thickness * 2,
        rr_clamp(radius - thickness, 0, radius),
        inner);
}

static uint8_t rr_viseme_index(uint8_t set, uint8_t raw)
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
    /*
     * Microsoft/custom sets retain more shape IDs. Folding rather than
     * rejecting them keeps the renderer deterministic and visibly responsive.
     */
    return raw < FACE_VISEME_COUNT
        ? raw
        : (uint8_t)(raw % FACE_VISEME_COUNT);
}

static rr_viseme_t rr_blended_viseme(const face_render_key_t *key)
{
    const rr_viseme_t first =
        RR_VISEMES[rr_viseme_index(key->viseme_set, key->viseme)];
    const rr_viseme_t second =
        RR_VISEMES[rr_viseme_index(
            key->viseme_set, key->viseme_secondary)];
    rr_viseme_t output;
    output.open =
        (uint8_t)rr_mix(first.open, second.open, key->viseme_blend);
    output.width =
        (uint8_t)rr_mix(first.width, second.width, key->viseme_blend);
    output.round =
        (uint8_t)rr_mix(first.round, second.round, key->viseme_blend);
    output.press =
        (uint8_t)rr_mix(first.press, second.press, key->viseme_blend);
    output.teeth =
        (uint8_t)rr_mix(first.teeth, second.teeth, key->viseme_blend);
    output.tongue =
        (uint8_t)rr_mix(first.tongue, second.tongue, key->viseme_blend);
    output.consonant = (uint8_t)rr_mix(
        first.consonant, second.consonant, key->viseme_blend);
    return output;
}

size_t face_robot_redux_count(void)
{
    return FACE_ROBOT_REDUX_COUNT;
}

const char *face_robot_redux_slug(face_robot_redux_style_t style)
{
    return rr_style_valid(style) ? RR_ACTORS[style].slug : NULL;
}

const char *face_robot_redux_name(face_robot_redux_style_t style)
{
    return rr_style_valid(style) ? RR_ACTORS[style].name : NULL;
}

bool face_robot_redux_info(
    face_robot_redux_style_t style,
    face_robot_redux_info_t *info)
{
    if (!rr_style_valid(style) || info == NULL) {
        return false;
    }
    const rr_actor_def_t *actor = &RR_ACTORS[style];
    info->slug = actor->slug;
    info->name = actor->name;
    info->legacy_profile_id = actor->legacy_id;
    info->mouth_kind = actor->mouth_kind;
    info->deliberate_mouthless = actor->mouthless;
    info->estimated_ops_per_pixel = actor->ops;
    return true;
}

bool face_robot_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_robot_redux_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_ROBOT_REDUX_COUNT; ++raw) {
        if (RR_ACTORS[raw].legacy_id == legacy_profile_id) {
            *style = (face_robot_redux_style_t)raw;
            return true;
        }
    }
    return false;
}

bool face_robot_redux_resolve(
    face_robot_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_robot_redux_pose_t *pose)
{
    if (!rr_style_valid(style) || render_key == NULL || pose == NULL) {
        return false;
    }
    const rr_actor_def_t *actor = &RR_ACTORS[style];
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, sizeof(pose->source));

    const uint8_t expression =
        render_key->stage_expression < RR_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = render_key->expression_weight;
    const rr_expression_t *target = &RR_EXPRESSIONS[expression];
#define RR_EXPR(field) ((int)rr_mix(0, target->field, expression_weight))

    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ACTIVE ||
        render_key->speech_phase == FACE_SPEECH_ENDING;
    pose->mouthless = actor->mouthless;

    const rr_viseme_t viseme = rr_blended_viseme(render_key);
    pose->speech_open = (uint8_t)rr_mix(
        render_key->controls.mouth_open, viseme.open,
        render_key->viseme_weight);
    pose->speech_width = (uint8_t)rr_mix(
        render_key->controls.mouth_width, viseme.width,
        render_key->viseme_weight);
    pose->speech_round = (uint8_t)rr_mix(
        render_key->controls.mouth_round, viseme.round,
        render_key->viseme_weight);
    pose->speech_press = (uint8_t)rr_mix(
        render_key->controls.mouth_press, viseme.press,
        render_key->viseme_weight);
    pose->teeth = (uint8_t)rr_mix(
        render_key->controls.mouth_teeth, viseme.teeth,
        render_key->viseme_weight);
    pose->tongue =
        (uint8_t)rr_mix(render_key->tongue, viseme.tongue,
                       render_key->viseme_weight);
    pose->cheek = (uint8_t)rr_clamp(
        (int)render_key->cheek + RR_EXPR(cheek), 0, 255);
    pose->consonant = render_key->phoneme == FACE_PHONEME_NONE
        ? viseme.consonant
        : (uint8_t)(render_key->phoneme % 32U);

    int phase_gain = 0;
    switch (render_key->speech_phase) {
    case FACE_SPEECH_STARTING:
        phase_gain = 176;
        break;
    case FACE_SPEECH_ACTIVE:
        phase_gain = 255;
        break;
    case FACE_SPEECH_ENDING:
        phase_gain = 126;
        break;
    case FACE_SPEECH_IDLE:
    default:
        phase_gain = pose->speaking ? 150 : 0;
        break;
    }
    const int audio_drive =
        (int)render_key->audio_level * phase_gain / 255;
    const int speech_wave =
        rr_wave(sample_clock + (uint32_t)render_key->schema_version * 17U,
                4800U);
    pose->speech_bob = pose->speaking
        ? (int16_t)rr_clamp(
            speech_wave * (24 + audio_drive) / (127 * 40),
            -2,
            2)
        : 0;

    const int gaze_x = rr_clamp(
        render_key->controls.look_x / 6 +
            render_key->head_yaw / 18 +
            RR_EXPR(gaze_x),
        -13,
        13);
    const int gaze_y = rr_clamp(
        render_key->controls.look_y / 8 +
            render_key->head_pitch / 20 +
            RR_EXPR(gaze_y),
        -10,
        10);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        if (actor->monocular && eye == 1U) {
            continue;
        }
        const uint8_t input_open = actor->monocular
            ? (uint8_t)(
                ((unsigned)render_key->controls.eye_left_open +
                 render_key->controls.eye_right_open) /
                2U)
            : eye == 0U
                ? render_key->controls.eye_left_open
                : render_key->controls.eye_right_open;
        const uint8_t squint = actor->monocular
            ? (uint8_t)(
                ((unsigned)render_key->eye_left_squint +
                 render_key->eye_right_squint) /
                2U)
            : eye == 0U
                ? render_key->eye_left_squint
                : render_key->eye_right_squint;
        const int expression_open = eye == 0U
            ? RR_EXPR(eye_open_left)
            : RR_EXPR(eye_open_right);
        const bool featured_eye_only =
            style == FACE_ROBOT_REDUX_ROBOEYES_ALERT ||
            style == FACE_ROBOT_REDUX_ROBOEYES_SOFT;
        const int speech_eye_open = actor->mouthless && pose->speaking
            ? featured_eye_only
                ? ((int)pose->speech_open -
                   (int)pose->speech_press / 2 - 64) / 58
                : ((int)pose->speech_open -
                   (int)pose->speech_press / 2) / 32
            : pose->speech_bob;
        pose->eye_x[eye] = actor->eye_x[eye];
        pose->eye_y[eye] = actor->eye_y;
        pose->eye_w[eye] = (int16_t)rr_clamp(
            actor->eye_w + RR_EXPR(eye_width) +
                (actor->mouthless
                    ? ((int)pose->speech_width - 128) /
                        (featured_eye_only ? 40 : 24)
                    : 0),
            actor->monocular ? 42 : 18,
            actor->monocular ? 78 : 58);
        pose->eye_h[eye] = actor->eye_h;
        int open =
            actor->eye_h * (int)input_open / 255 +
            expression_open - (int)squint / 18 +
            ((int)render_key->affect_arousal - 128) / 48 +
            speech_eye_open;
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            open += featured_eye_only ? -3 : 3;
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            open -= 1;
        }
        if ((render_key->controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            open = 2;
        }
        pose->eye_open[eye] = (int16_t)rr_clamp(
            open, 2, actor->eye_h);
        pose->pupil_x[eye] =
            (int16_t)(actor->eye_x[eye] + gaze_x);
        pose->pupil_y[eye] =
            (int16_t)(actor->eye_y + gaze_y - pose->speech_bob / 2);
        pose->pupil_radius[eye] = (int16_t)rr_clamp(
            (actor->monocular ? 14 : 6) +
                RR_EXPR(pupil) +
                ((int)render_key->attention - 128) / 64 -
                ((int)render_key->affect_arousal - 128) / 80 +
                (actor->mouthless
                    ? ((int)pose->speech_round - 128) / 50
                    : 0),
            2,
            actor->monocular ? 20 : 10);
        const int brow_raise = eye == 0U
            ? RR_EXPR(brow_raise_left)
            : RR_EXPR(brow_raise_right);
        const int outer = actor->monocular
            ? ((int)render_key->brow_outer_left +
               render_key->brow_outer_right) /
                2
            : eye == 0U
                ? render_key->brow_outer_left
                : render_key->brow_outer_right;
        const int corner_action = actor->monocular
            ? ((int)render_key->mouth_corner_left +
               render_key->mouth_corner_right) /
                2
            : eye == 0U
                ? render_key->mouth_corner_left
                : render_key->mouth_corner_right;
        pose->brow_y[eye] = (int16_t)rr_clamp(
            actor->eye_y - actor->eye_h / 2 - 7 +
                brow_raise -
                render_key->controls.brow / 22 -
                render_key->brow_inner / 24 -
                outer / 30 -
                pose->speech_bob / 2,
            10,
            52);
        pose->brow_slope[eye] = (int16_t)rr_clamp(
            (eye == 0U
                ? RR_EXPR(brow_slope_left)
                : RR_EXPR(brow_slope_right)) +
                render_key->head_roll / 17 +
                outer / 24 +
                corner_action / 42,
            -13,
            13);
    }

    pose->mouth_x = 80;
    pose->mouth_y = actor->mouth_y;
    pose->mouth_w = (int16_t)rr_clamp(
        20 + pose->speech_width * 44 / 255 -
            pose->speech_round / 19 + RR_EXPR(mouth_width),
        13,
        style == FACE_ROBOT_REDUX_M5_AVATAR_MANGA ? 58 : 70);
    pose->mouth_h = (int16_t)rr_clamp(
        2 + pose->speech_open * 27 / 255 -
            pose->speech_press * 7 / 255 + RR_EXPR(mouth_open),
        2,
        style == FACE_ROBOT_REDUX_M5_AVATAR_MANGA ? 25 : 31);
    pose->mouth_corner[0] = (int16_t)rr_clamp(
        render_key->mouth_corner_left / 12 +
            render_key->affect_valence / 18 +
            RR_EXPR(mouth_corner_left),
        -13,
        13);
    pose->mouth_corner[1] = (int16_t)rr_clamp(
        render_key->mouth_corner_right / 12 +
            render_key->affect_valence / 18 +
            RR_EXPR(mouth_corner_right),
        -13,
        13);

    pose->head_roll = (int16_t)rr_clamp(
        render_key->head_roll / 11 + RR_EXPR(head_roll), -13, 13);
    pose->body_lean_x = (int16_t)rr_clamp(
        render_key->body_lean_x / 14 + RR_EXPR(body_x), -10, 10);
    pose->body_lean_y = (int16_t)rr_clamp(
        render_key->body_lean_y / 17 + RR_EXPR(body_y), -8, 8);
    const int activity_lift =
        render_key->controls.expression == FACE_ACTIVITY_SPEAKING ? -1
        : render_key->controls.expression == FACE_ACTIVITY_THINKING ? 1
        : render_key->controls.expression == FACE_ACTIVITY_LISTENING ? 0
        : 2;
    pose->face_shift_x = (int16_t)rr_clamp(
        render_key->head_yaw / 28 + pose->body_lean_x / 2,
        -7,
        7);
    pose->face_shift_y = (int16_t)rr_clamp(
        render_key->head_pitch / 34 + pose->body_lean_y / 3 +
            activity_lift + pose->speech_bob +
            (render_key->speech_phase == FACE_SPEECH_STARTING ? -2
             : render_key->speech_phase == FACE_SPEECH_ENDING ? 1
             : 0),
        -6,
        7);
    /*
     * Protocol/activity bytes influence a tiny, character-integrated timing
     * detail (eye glint, visor notch, or orb arc), never a debug panel.
     */
    pose->detail_phase = (uint8_t)(
        (uint16_t)pose->consonant * 3U +
        (uint16_t)render_key->viseme_set * 5U +
        (uint16_t)render_key->schema_version * 7U +
        (uint16_t)render_key->controls.expression * 11U +
        (uint16_t)render_key->controls.flags * 13U +
        (uint16_t)pose->teeth +
        (uint16_t)pose->tongue * 3U +
        (uint16_t)pose->cheek * 5U +
        (uint16_t)render_key->attention * 7U +
        (uint16_t)(uint8_t)render_key->affect_valence * 11U) &
        31U;
    pose->stage_expression = expression;
    pose->expression_weight = expression_weight;
    pose->activity = render_key->controls.expression;
    pose->speech_phase = render_key->speech_phase;
    pose->attention = render_key->attention;
#undef RR_EXPR
    return true;
}

static void rr_lid_depths(
    const face_robot_redux_pose_t *pose,
    size_t eye,
    int open,
    int *top_left,
    int *top_right,
    int *bottom_left,
    int *bottom_right)
{
    const int slope = pose->brow_slope[eye];
    int tl = rr_clamp(1 + slope, 0, 10);
    int tr = rr_clamp(1 - slope, 0, 10);
    const int neutral_brow =
        pose->eye_y[eye] - pose->eye_h[eye] / 2 - 7;
    const int brow_pressure =
        rr_clamp(pose->brow_y[eye] - neutral_brow, -4, 6);
    if (brow_pressure > 0) {
        tl += brow_pressure;
        tr += brow_pressure;
    }
    int bl = 0;
    int br = 0;
    switch (pose->stage_expression) {
    case FACE_EXPRESSION_WARM:
        bl += 3;
        br += 3;
        break;
    case FACE_EXPRESSION_JOY:
        bl += 8;
        br += 8;
        break;
    case FACE_EXPRESSION_CONCERN:
        if (eye == 0U) {
            tl += 1;
            tr += 6;
        } else {
            tl += 6;
            tr += 1;
        }
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        if (eye == 1U) {
            tl += 6;
            tr += 6;
        }
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        if (eye == 0U) {
            tl += 8;
            tr += 5;
        }
        break;
    case FACE_EXPRESSION_DETERMINED:
        if (eye == 0U) {
            tr += 7;
        } else {
            tl += 7;
        }
        break;
    case FACE_EXPRESSION_SLEEPY:
        tl += 9;
        tr += 9;
        break;
    case FACE_EXPRESSION_EXCITED:
        bl += 1;
        br += 1;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        if (eye == 0U) {
            bl += 4;
            br += 6;
        } else {
            bl += 7;
            br += 4;
        }
        break;
    case FACE_EXPRESSION_NEUTRAL:
    case FACE_EXPRESSION_SURPRISE:
    default:
        break;
    }
    const int maximum = rr_clamp(open / 2 - 1, 0, 12);
    *top_left = (int)rr_clamp(tl, 0, maximum);
    *top_right = (int)rr_clamp(tr, 0, maximum);
    *bottom_left = (int)rr_clamp(bl, 0, maximum);
    *bottom_right = (int)rr_clamp(br, 0, maximum);
}

static void rr_masked_eye(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose,
    size_t eye,
    int cx,
    int cy,
    int width,
    int open,
    int radius,
    uint16_t eye_color,
    uint16_t mask_color)
{
    width = (int)rr_clamp(width, 4, 76);
    open = (int)rr_clamp(open, 2, 64);
    const int left = cx - width / 2;
    const int top = cy - open / 2;
    const int right = left + width - 1;
    const int bottom = top + open - 1;
    rr_round_rect(canvas, left, top, width, open, radius, eye_color);

    int tl;
    int tr;
    int bl;
    int br;
    rr_lid_depths(pose, eye, open, &tl, &tr, &bl, &br);
    if (tl > 0 || tr > 0) {
        rr_quad(
            canvas,
            left - 1,
            top - 1,
            right + 1,
            top - 1,
            right + 1,
            top + tr,
            left - 1,
            top + tl,
            mask_color);
    }
    if (bl > 0 || br > 0) {
        rr_quad(
            canvas,
            left - 1,
            bottom - bl,
            right + 1,
            bottom - br,
            right + 1,
            bottom + 1,
            left - 1,
            bottom + 1,
            mask_color);
    }
}

static void rr_eye_lid_limits(
    int dx,
    int half_width,
    int half_height,
    int top_left,
    int top_right,
    int bottom_left,
    int bottom_right,
    int *top,
    int *bottom)
{
    const int span = half_width * 2;
    const int column = (int)rr_clamp(dx + half_width, 0, span);
    const int top_cut =
        (top_left * (span - column) + top_right * column +
         span / 2) /
        span;
    const int bottom_cut =
        (bottom_left * (span - column) + bottom_right * column +
         span / 2) /
        span;
    *top = -half_height + top_cut;
    *bottom = half_height - bottom_cut;
}

static bool rr_inside_crisp_aperture(
    int dx,
    int dy,
    int half_width,
    int half_height,
    int top_left,
    int top_right,
    int bottom_left,
    int bottom_right,
    int bevel)
{
    if (rr_abs(dx) > half_width || rr_abs(dy) > half_height) {
        return false;
    }
    int top;
    int bottom;
    rr_eye_lid_limits(
        dx,
        half_width,
        half_height,
        top_left,
        top_right,
        bottom_left,
        bottom_right,
        &top,
        &bottom);
    const int edge = half_width - rr_abs(dx);
    const int corner_cut = edge < bevel ? bevel - edge : 0;
    top += corner_cut;
    bottom -= corner_cut;
    return dy >= top && dy <= bottom;
}

static bool rr_inside_soft_aperture(
    int dx,
    int dy,
    int half_width,
    int half_height,
    int top_left,
    int top_right,
    int bottom_left,
    int bottom_right)
{
    if (rr_abs(dx) > half_width || rr_abs(dy) > half_height) {
        return false;
    }
    const int64_t x2 = (int64_t)dx * dx;
    const int64_t y2 = (int64_t)dy * dy;
    const int64_t hw2 = (int64_t)half_width * half_width;
    const int64_t hh2 = (int64_t)half_height * half_height;
    if (x2 * hh2 + y2 * hw2 > hw2 * hh2) {
        return false;
    }
    int top;
    int bottom;
    rr_eye_lid_limits(
        dx,
        half_width,
        half_height,
        top_left,
        top_right,
        bottom_left,
        bottom_right,
        &top,
        &bottom);
    return dy >= top && dy <= bottom;
}

static void rr_draw_roboeyes_alert(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(3, 6, 10);
    const uint16_t recess = RR_RGB565(24, 27, 31);
    const uint16_t rim = RR_RGB565(76, 47, 31);
    const uint16_t cream = RR_RGB565(255, 231, 174);
    const uint16_t white = RR_RGB565(255, 250, 225);
    const uint16_t focus = RR_RGB565(16, 14, 14);
    const uint16_t accent = RR_RGB565(255, 105, 42);
    rr_clear(canvas, bg);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye] + pose->face_shift_x;
        const int cy = pose->eye_y[eye] + pose->face_shift_y +
            (eye == 0U ? -pose->head_roll / 4
                       : pose->head_roll / 4);
        const int width = (int)rr_clamp(pose->eye_w[eye], 24, 52);
        const int open =
            (int)rr_clamp(pose->eye_open[eye], 3, 49);
        const int half_width = width / 2;
        const int half_height = open / 2;
        int tl;
        int tr;
        int bl;
        int br;
        rr_lid_depths(pose, eye, open, &tl, &tr, &bl, &br);
        const int gaze_x = (int)rr_clamp(
            (pose->pupil_x[eye] - pose->eye_x[eye]) * 3,
            -9,
            9);
        const int gaze_y = (int)rr_clamp(
            (pose->pupil_y[eye] - pose->eye_y[eye]) * 2,
            -7,
            7);
        const int pupil_rx = (int)rr_clamp(
            3 + pose->pupil_radius[eye] / 2, 4, 8);
        const int pupil_ry = (int)rr_clamp(
            5 + open / 10 +
                ((int)pose->speech_round - 128) / 96,
            4,
            10);

        /*
         * A fixed 58x58 angular socket gives the eye a stable home. Only its
         * contained aperture, lids, and focus core act.
         */
        for (int dy = -29; dy <= 29; ++dy) {
            for (int dx = -29; dx <= 29; ++dx) {
                if (rr_inside_crisp_aperture(
                        dx, dy, 29, 29, 0, 0, 0, 0, 7)) {
                    rr_put(canvas, cx + dx, cy + dy, recess);
                }
                if (rr_inside_crisp_aperture(
                        dx,
                        dy,
                        half_width + 3,
                        half_height + 3,
                        tl,
                        tr,
                        bl,
                        br,
                        5)) {
                    rr_put(canvas, cx + dx, cy + dy, rim);
                }
                if (!rr_inside_crisp_aperture(
                        dx,
                        dy,
                        half_width,
                        half_height,
                        tl,
                        tr,
                        bl,
                        br,
                        4)) {
                    continue;
                }
                uint16_t color =
                    dy > half_height / 3 ? cream : white;
                const int pupil_dx = dx - gaze_x;
                const int pupil_dy = dy - gaze_y;
                if ((int64_t)pupil_dx * pupil_dx * pupil_ry * pupil_ry +
                        (int64_t)pupil_dy * pupil_dy *
                            pupil_rx * pupil_rx <=
                    (int64_t)pupil_rx * pupil_rx *
                        pupil_ry * pupil_ry) {
                    color = focus;
                    if (pupil_dx >= -pupil_rx + 1 &&
                        pupil_dx <= -pupil_rx / 3 &&
                        pupil_dy >= -pupil_ry + 2 &&
                        pupil_dy <= -pupil_ry / 3) {
                        color = accent;
                    }
                }
                rr_put(canvas, cx + dx, cy + dy, color);
            }
        }

        const int top_y = cy - half_height - 3;
        const int slope = pose->brow_slope[eye] / 2;
        rr_thick_line(
            canvas,
            cx - half_width + 3,
            top_y + slope,
            cx + half_width - 3,
            top_y - slope,
            3,
            accent);
    }

    /*
     * The protocol/detail channel is a socket-mounted status filament. Its
     * eye selection and length encode all 32 phases without floating pixels.
     */
    const size_t detail_eye = pose->detail_phase >> 4U;
    const int detail_length = 7 + (int)(pose->detail_phase & 15U);
    const int detail_cx =
        pose->eye_x[detail_eye] + pose->face_shift_x;
    const int detail_cy =
        pose->eye_y[detail_eye] + pose->face_shift_y +
        (detail_eye == 0U ? -pose->head_roll / 4
                          : pose->head_roll / 4);
    const int detail_width =
        (int)rr_clamp(pose->eye_w[detail_eye], 24, 52);
    const int detail_open =
        (int)rr_clamp(pose->eye_open[detail_eye], 3, 49);
    const int detail_half_width = detail_width / 2;
    const int detail_half_height = detail_open / 2;
    int detail_tl;
    int detail_tr;
    int detail_bl;
    int detail_br;
    rr_lid_depths(
        pose,
        detail_eye,
        detail_open,
        &detail_tl,
        &detail_tr,
        &detail_bl,
        &detail_br);
    for (int offset = 0; offset < detail_length; ++offset) {
        const int dx = -detail_half_width + 4 + offset;
        int top;
        int bottom;
        rr_eye_lid_limits(
            dx,
            detail_half_width,
            detail_half_height,
            detail_tl,
            detail_tr,
            detail_bl,
            detail_br,
            &top,
            &bottom);
        const int dy = bottom - 1;
        if (rr_inside_crisp_aperture(
                dx,
                dy,
                detail_half_width,
                detail_half_height,
                detail_tl,
                detail_tr,
                detail_bl,
                detail_br,
                4)) {
            rr_put(
                canvas,
                detail_cx + dx,
                detail_cy + dy,
                accent);
            rr_put(
                canvas,
                detail_cx + dx,
                detail_cy + dy - 1,
                accent);
        }
    }
    const int articulation_length =
        5 + (int)(pose->consonant & 31U) / 2;
    const int articulation_cx =
        pose->eye_x[1] + pose->face_shift_x;
    const int articulation_cy =
        pose->eye_y[1] + pose->face_shift_y +
        pose->head_roll / 4;
    const int articulation_width =
        (int)rr_clamp(pose->eye_w[1], 24, 52);
    const int articulation_open =
        (int)rr_clamp(pose->eye_open[1], 3, 49);
    const int articulation_half_width = articulation_width / 2;
    const int articulation_half_height = articulation_open / 2;
    int articulation_tl;
    int articulation_tr;
    int articulation_bl;
    int articulation_br;
    rr_lid_depths(
        pose,
        1U,
        articulation_open,
        &articulation_tl,
        &articulation_tr,
        &articulation_bl,
        &articulation_br);
    for (int offset = 0; offset < articulation_length; ++offset) {
        const int dx =
            articulation_half_width - 4 - offset;
        int top;
        int bottom;
        rr_eye_lid_limits(
            dx,
            articulation_half_width,
            articulation_half_height,
            articulation_tl,
            articulation_tr,
            articulation_bl,
            articulation_br,
            &top,
            &bottom);
        (void)bottom;
        const int dy = top + 1;
        if (rr_inside_crisp_aperture(
                dx,
                dy,
                articulation_half_width,
                articulation_half_height,
                articulation_tl,
                articulation_tr,
                articulation_bl,
                articulation_br,
                4)) {
            rr_put(
                canvas,
                articulation_cx + dx,
                articulation_cy + dy,
                accent);
            rr_put(
                canvas,
                articulation_cx + dx,
                articulation_cy + dy + 1,
                accent);
        }
    }
}

static void rr_draw_roboeyes_soft(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(7, 17, 24);
    const uint16_t recess = RR_RGB565(12, 49, 61);
    const uint16_t rim = RR_RGB565(43, 126, 128);
    const uint16_t aqua = RR_RGB565(169, 250, 225);
    const uint16_t light = RR_RGB565(227, 255, 238);
    const uint16_t focus = RR_RGB565(12, 48, 63);
    const uint16_t warm = RR_RGB565(255, 190, 124);
    rr_clear(canvas, bg);

    const int elastic =
        pose->speaking ? ((int)pose->speech_round - 128) / 64 : 0;
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye] + pose->face_shift_x;
        const int cy = pose->eye_y[eye] + pose->face_shift_y +
            (eye == 0U ? -pose->head_roll / 5
                       : pose->head_roll / 5);
        const int width =
            (int)rr_clamp(pose->eye_w[eye] - elastic, 24, 58);
        const int open = (int)rr_clamp(
            pose->eye_open[eye] + elastic / 3, 3, 46);
        const int half_width = width / 2;
        const int half_height = open / 2;
        int tl;
        int tr;
        int bl;
        int br;
        rr_lid_depths(pose, eye, open, &tl, &tr, &bl, &br);
        const int gaze_x = (int)rr_clamp(
            (pose->pupil_x[eye] - pose->eye_x[eye]) * 3,
            -9,
            9);
        const int gaze_y = (int)rr_clamp(
            (pose->pupil_y[eye] - pose->eye_y[eye]) * 2,
            -7,
            7);
        const int pupil_rx = (int)rr_clamp(
            5 + pose->pupil_radius[eye] / 2, 6, 10);
        const int pupil_ry = (int)rr_clamp(
            6 + open / 8 -
                ((int)pose->speech_press - 128) / 96,
            5,
            12);

        rr_ellipse(canvas, cx, cy, 32, 29, recess);
        for (int dy = -27; dy <= 27; ++dy) {
            for (int dx = -30; dx <= 30; ++dx) {
                if (rr_inside_soft_aperture(
                        dx,
                        dy,
                        half_width + 3,
                        half_height + 3,
                        tl,
                        tr,
                        bl,
                        br)) {
                    rr_put(canvas, cx + dx, cy + dy, rim);
                }
                if (!rr_inside_soft_aperture(
                        dx,
                        dy,
                        half_width,
                        half_height,
                        tl,
                        tr,
                        bl,
                        br)) {
                    continue;
                }
                uint16_t color =
                    dy < -half_height / 4 ? light : aqua;
                const int pupil_dx = dx - gaze_x;
                const int pupil_dy = dy - gaze_y;
                if ((int64_t)pupil_dx * pupil_dx *
                            pupil_ry * pupil_ry +
                        (int64_t)pupil_dy * pupil_dy *
                            pupil_rx * pupil_rx <=
                    (int64_t)pupil_rx * pupil_rx *
                        pupil_ry * pupil_ry) {
                    color = focus;
                    if (pupil_dx >= -pupil_rx + 2 &&
                        pupil_dx <= -pupil_rx / 3 &&
                        pupil_dy >= -pupil_ry + 2 &&
                        pupil_dy <= -pupil_ry / 3) {
                        color = light;
                    }
                }
                rr_put(canvas, cx + dx, cy + dy, color);
            }
        }

        /*
         * A broad upper-lid plane touches the socket, so concern,
         * determination, skepticism, and sleep remain legible when tiny.
         */
        const int top_y = cy - half_height - 2;
        const int slope = pose->brow_slope[eye] / 2;
        rr_thick_line(
            canvas,
            cx - half_width + 5,
            top_y + slope,
            cx + half_width - 5,
            top_y - slope,
            3,
            rim);
    }

    /*
     * Soft's detail channel is a warm lower-lid glimmer, keyed by eye and
     * length. It is always attached to the socket rather than a floating icon.
     */
    const size_t detail_eye = pose->detail_phase >> 4U;
    const int detail_length = 6 + (int)(pose->detail_phase & 15U);
    const int detail_cx =
        pose->eye_x[detail_eye] + pose->face_shift_x;
    const int detail_cy =
        pose->eye_y[detail_eye] + pose->face_shift_y +
        (detail_eye == 0U ? -pose->head_roll / 5
                          : pose->head_roll / 5);
    const int detail_elastic =
        pose->speaking
        ? ((int)pose->speech_round - 128) / 64
        : 0;
    const int detail_width = (int)rr_clamp(
        pose->eye_w[detail_eye] - detail_elastic, 24, 58);
    const int detail_open = (int)rr_clamp(
        pose->eye_open[detail_eye] + detail_elastic / 3,
        3,
        46);
    const int detail_half_width = detail_width / 2;
    const int detail_half_height = detail_open / 2;
    int detail_tl;
    int detail_tr;
    int detail_bl;
    int detail_br;
    rr_lid_depths(
        pose,
        detail_eye,
        detail_open,
        &detail_tl,
        &detail_tr,
        &detail_bl,
        &detail_br);
    for (int offset = 0; offset < detail_length; ++offset) {
        const int dx =
            -detail_length / 2 + offset;
        int top;
        int bottom;
        rr_eye_lid_limits(
            dx,
            detail_half_width,
            detail_half_height,
            detail_tl,
            detail_tr,
            detail_bl,
            detail_br,
            &top,
            &bottom);
        (void)top;
        const int dy = bottom - 1;
        if (rr_inside_soft_aperture(
                dx,
                dy,
                detail_half_width,
                detail_half_height,
                detail_tl,
                detail_tr,
                detail_bl,
                detail_br)) {
            rr_put(
                canvas,
                detail_cx + dx,
                detail_cy + dy,
                warm);
            rr_put(
                canvas,
                detail_cx + dx,
                detail_cy + dy - 1,
                warm);
        }
    }
    const int articulation_length =
        5 + (int)(pose->consonant & 31U) / 2;
    const int articulation_cx =
        pose->eye_x[0] + pose->face_shift_x;
    const int articulation_cy =
        pose->eye_y[0] + pose->face_shift_y -
        pose->head_roll / 5;
    const int articulation_width = (int)rr_clamp(
        pose->eye_w[0] - detail_elastic, 24, 58);
    const int articulation_open = (int)rr_clamp(
        pose->eye_open[0] + detail_elastic / 3, 3, 46);
    const int articulation_half_width =
        articulation_width / 2;
    const int articulation_half_height =
        articulation_open / 2;
    int articulation_tl;
    int articulation_tr;
    int articulation_bl;
    int articulation_br;
    rr_lid_depths(
        pose,
        0U,
        articulation_open,
        &articulation_tl,
        &articulation_tr,
        &articulation_bl,
        &articulation_br);
    for (int offset = 0; offset < articulation_length; ++offset) {
        const int dx =
            -articulation_length / 2 + offset;
        int top;
        int bottom;
        rr_eye_lid_limits(
            dx,
            articulation_half_width,
            articulation_half_height,
            articulation_tl,
            articulation_tr,
            articulation_bl,
            articulation_br,
            &top,
            &bottom);
        (void)bottom;
        const int dy = top + 1;
        if (rr_inside_soft_aperture(
                dx,
                dy,
                articulation_half_width,
                articulation_half_height,
                articulation_tl,
                articulation_tr,
                articulation_bl,
                articulation_br)) {
            rr_put(
                canvas,
                articulation_cx + dx,
                articulation_cy + dy,
                warm);
            rr_put(
                canvas,
                articulation_cx + dx,
                articulation_cy + dy + 1,
                warm);
        }
    }
}

static void rr_draw_m5_brows(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose,
    int shift_x,
    int shift_y,
    uint16_t ink,
    int thickness)
{
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int cx = pose->eye_x[eye] + shift_x;
        const int y = pose->brow_y[eye] + shift_y;
        const int slope = pose->brow_slope[eye];
        rr_thick_line(
            canvas,
            cx - 15,
            y + slope,
            cx + 15,
            y - slope,
            thickness,
            ink);
    }
}

static void rr_draw_classic_mouth(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose,
    int shift_x,
    int shift_y,
    uint16_t face,
    uint16_t ink,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x + shift_x;
    const int cy = pose->mouth_y + shift_y;
    const int width = pose->mouth_w;
    const int height = pose->mouth_h;
    if (!pose->speaking && height <= 4) {
        rr_thick_line(
            canvas,
            cx - width / 2,
            cy - pose->mouth_corner[0] / 2,
            cx,
            cy + 2,
            3,
            ink);
        rr_thick_line(
            canvas,
            cx,
            cy + 2,
            cx + width / 2,
            cy - pose->mouth_corner[1] / 2,
            3,
            ink);
        return;
    }
    rr_ellipse(canvas, cx, cy, width / 2, height / 2 + 1, ink);
    if (pose->teeth > 82U && height > 7) {
        const int tooth_h = rr_clamp(
            1 + pose->teeth * height / (255 * 3), 1, height / 3);
        rr_round_rect(
            canvas,
            cx - width / 3,
            cy - height / 2 + 2,
            width * 2 / 3,
            tooth_h,
            2,
            teeth);
    }
    if (pose->tongue > 72U && height > 9) {
        const int tongue_h = rr_clamp(
            2 + pose->tongue * height / (255 * 3), 2, height / 3);
        rr_ellipse(
            canvas,
            cx,
            cy + height / 2 - tongue_h / 2 - 1,
            width / 4,
            tongue_h,
            tongue);
    }
    /*
     * These face-colour wedges keep corner direction legible around a cavity
     * without ever exceeding the fixed mouth anchor.
     */
    if (pose->mouth_corner[0] < -4) {
        rr_triangle(
            canvas,
            cx - width / 2,
            cy - height / 2,
            cx - width / 3,
            cy - height / 2,
            cx - width / 2,
            cy,
            face);
    }
}

static void rr_draw_m5_classic(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(229, 186, 118);
    const uint16_t face = RR_RGB565(251, 221, 163);
    const uint16_t ink = RR_RGB565(27, 34, 34);
    const uint16_t cheek = RR_RGB565(232, 118, 99);
    const uint16_t teeth = RR_RGB565(255, 250, 224);
    const uint16_t tongue = RR_RGB565(198, 67, 76);
    rr_clear(canvas, bg);

    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    rr_ellipse(canvas, 13 + pose->body_lean_x / 2,
               63 + pose->body_lean_y / 2, 7, 13, face);
    rr_ellipse(canvas, 147 + pose->body_lean_x / 2,
               63 + pose->body_lean_y / 2, 7, 13, face);
    rr_ellipse(canvas, 80 + sx, 61 + sy, 67, 52, face);

    rr_draw_m5_brows(canvas, pose, sx, sy, ink, 3);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int gaze_x =
            (pose->pupil_x[eye] - pose->eye_x[eye]) / 3;
        const int gaze_y =
            (pose->pupil_y[eye] - pose->eye_y[eye]) / 4;
        const int cx = pose->eye_x[eye] + sx + gaze_x;
        const int cy = pose->eye_y[eye] + sy + gaze_y +
            (eye == 0U ? -pose->head_roll / 6
                       : pose->head_roll / 6);
        const int rx = rr_clamp(pose->eye_w[eye] / 3, 5, 13);
        const int ry = rr_clamp(pose->eye_open[eye] / 2, 1, 19);
        rr_ellipse(canvas, cx, cy, rx, ry, ink);
        if (ry > 8 && pose->attention > 164U &&
            eye == pose->detail_phase / 16U) {
            rr_ellipse(
                canvas,
                cx - 4 + (int)((pose->detail_phase % 16U) / 2U),
                cy - ry / 3 +
                    (int)(pose->detail_phase % 2U),
                1,
                2,
                teeth);
        }
    }

    if (pose->cheek > 55U) {
        const int cheek_rx = 3 + pose->cheek / 64;
        rr_ellipse(canvas, 30 + sx, 71 + sy, cheek_rx, 3, cheek);
        rr_ellipse(canvas, 130 + sx, 71 + sy, cheek_rx, 3, cheek);
    }
    rr_draw_classic_mouth(
        canvas, pose, sx, sy, face, ink, teeth, tongue);
}

static void rr_draw_manga_eye(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose,
    size_t eye,
    int sx,
    int sy,
    uint16_t face,
    uint16_t ink,
    uint16_t sclera,
    uint16_t iris,
    uint16_t shine)
{
    const int cx = pose->eye_x[eye] + sx;
    const int cy = pose->eye_y[eye] + sy +
        (eye == 0U ? -pose->head_roll / 7 : pose->head_roll / 7);
    const int rx = rr_clamp(pose->eye_w[eye] / 2, 12, 25);
    const int ry = rr_clamp(pose->eye_open[eye] / 2, 1, 23);
    if (ry <= 2) {
        rr_thick_line(canvas, cx - rx, cy, cx + rx, cy, 2, ink);
        return;
    }

    rr_ellipse(canvas, cx, cy, rx + 2, ry + 2, ink);
    rr_ellipse(canvas, cx, cy, rx, ry, sclera);

    int tl;
    int tr;
    int bl;
    int br;
    rr_lid_depths(pose, eye, ry * 2, &tl, &tr, &bl, &br);
    if (tl > 0 || tr > 0) {
        rr_quad(
            canvas,
            cx - rx - 3,
            cy - ry - 3,
            cx + rx + 3,
            cy - ry - 3,
            cx + rx + 3,
            cy - ry + tr,
            cx - rx - 3,
            cy - ry + tl,
            face);
    }
    if (bl > 0 || br > 0) {
        rr_quad(
            canvas,
            cx - rx - 3,
            cy + ry - bl,
            cx + rx + 3,
            cy + ry - br,
            cx + rx + 3,
            cy + ry + 3,
            cx - rx - 3,
            cy + ry + 3,
            face);
    }

    const int gaze_x =
        rr_clamp(pose->pupil_x[eye] - pose->eye_x[eye], -8, 8);
    const int gaze_y =
        rr_clamp(pose->pupil_y[eye] - pose->eye_y[eye], -6, 6);
    const int iris_rx = rr_clamp(pose->pupil_radius[eye] + 2, 5, 11);
    const int iris_ry = rr_clamp(ry - 3, 4, 14);
    const int px = cx + gaze_x;
    const int py = cy + gaze_y;
    rr_ellipse(canvas, px, py, iris_rx, iris_ry, iris);
    rr_ellipse(
        canvas,
        px,
        py + 1,
        rr_clamp(iris_rx - 4, 2, 7),
        rr_clamp(iris_ry - 4, 2, 8),
        ink);
    if (ry > 8 && eye == pose->detail_phase / 16U) {
        const int detail =
            (int)((pose->detail_phase % 16U) / 2U);
        rr_ellipse(
            canvas,
            px - 4 + detail,
            py - 5 + (int)(pose->detail_phase % 2U),
            2,
            3,
            shine);
        rr_ellipse(canvas, px + 3, py + 3, 1, 1, shine);
    }

    const int outside = eye == 0U ? cx - rx : cx + rx;
    const int direction = eye == 0U ? -1 : 1;
    rr_line(
        canvas,
        outside,
        cy - ry / 2,
        outside + direction * 7,
        cy - ry / 2 - 3,
        ink);
    rr_line(
        canvas,
        outside,
        cy,
        outside + direction * 6,
        cy - 1,
        ink);
}

static void rr_draw_manga_mouth(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose,
    int sx,
    int sy,
    uint16_t ink,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x + sx;
    const int cy = pose->mouth_y + sy;
    const int width = pose->mouth_w;
    const int height = pose->mouth_h;
    if (!pose->speaking && height <= 4) {
        rr_thick_line(
            canvas,
            cx - width / 2,
            cy - pose->mouth_corner[0] / 2,
            cx,
            cy + 1,
            2,
            ink);
        rr_thick_line(
            canvas,
            cx,
            cy + 1,
            cx + width / 2,
            cy - pose->mouth_corner[1] / 2,
            2,
            ink);
        return;
    }
    rr_ellipse(canvas, cx, cy, width / 2 + 2, height / 2 + 2, ink);
    rr_ellipse(canvas, cx, cy, width / 2, height / 2, cavity);
    if (pose->teeth > 96U && height > 7) {
        rr_round_rect(
            canvas,
            cx - width / 3,
            cy - height / 2 + 1,
            width * 2 / 3,
            rr_clamp(height / 4, 1, 5),
            1,
            teeth);
    }
    if (pose->tongue > 64U && height > 9) {
        rr_ellipse(
            canvas,
            cx,
            cy + height / 3,
            width / 4,
            rr_clamp(height / 5, 2, 5),
            tongue);
    }
}

static void rr_draw_m5_manga(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(39, 30, 56);
    const uint16_t hair = RR_RGB565(40, 38, 71);
    const uint16_t face = RR_RGB565(255, 225, 205);
    const uint16_t ink = RR_RGB565(38, 28, 46);
    const uint16_t sclera = RR_RGB565(255, 252, 238);
    const uint16_t iris = RR_RGB565(93, 106, 177);
    const uint16_t shine = RR_RGB565(255, 255, 255);
    const uint16_t blush = RR_RGB565(241, 124, 142);
    const uint16_t cavity = RR_RGB565(99, 37, 65);
    const uint16_t tongue = RR_RGB565(232, 94, 125);
    rr_clear(canvas, bg);

    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    rr_quad(
        canvas,
        40 + pose->body_lean_x,
        114,
        55 + pose->body_lean_x,
        96 + pose->body_lean_y,
        105 + pose->body_lean_x,
        96 + pose->body_lean_y,
        120 + pose->body_lean_x,
        114,
        hair);
    rr_ellipse(canvas, 80 + sx, 61 + sy, 62, 51, hair);
    rr_ellipse(canvas, 80 + sx, 64 + sy, 56, 47, face);
    rr_ellipse(canvas, 25 + sx, 65 + sy, 6, 11, face);
    rr_ellipse(canvas, 135 + sx, 65 + sy, 6, 11, face);

    /* Fixed fringe silhouette: four broad locks, no floating decorations. */
    rr_triangle(
        canvas, 34 + sx, 29 + sy, 64 + sx, 15 + sy,
        58 + sx, 47 + sy, hair);
    rr_triangle(
        canvas, 55 + sx, 20 + sy, 84 + sx, 12 + sy,
        75 + sx, 45 + sy, hair);
    rr_triangle(
        canvas, 78 + sx, 13 + sy, 108 + sx, 20 + sy,
        92 + sx, 47 + sy, hair);
    rr_triangle(
        canvas, 99 + sx, 18 + sy, 128 + sx, 34 + sy,
        108 + sx, 47 + sy, hair);

    rr_draw_m5_brows(canvas, pose, sx, sy + 1, ink, 2);
    rr_draw_manga_eye(
        canvas, pose, 0U, sx, sy, face, ink, sclera, iris, shine);
    rr_draw_manga_eye(
        canvas, pose, 1U, sx, sy, face, ink, sclera, iris, shine);

    if (pose->cheek > 48U) {
        const int hatch = 2 + pose->cheek / 90;
        for (int line = 0; line < hatch; ++line) {
            rr_line(
                canvas,
                28 + sx + line * 4,
                74 + sy,
                32 + sx + line * 4,
                70 + sy,
                blush);
            rr_line(
                canvas,
                122 + sx + line * 4,
                70 + sy,
                126 + sx + line * 4,
                74 + sy,
                blush);
        }
    }
    rr_draw_manga_mouth(
        canvas, pose, sx, sy, ink, cavity, shine, tongue);
}

static void rr_draw_eve(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(4, 12, 25);
    const uint16_t shadow = RR_RGB565(120, 153, 170);
    const uint16_t shell = RR_RGB565(238, 248, 247);
    const uint16_t visor = RR_RGB565(8, 17, 27);
    const uint16_t blue = RR_RGB565(32, 205, 255);
    const uint16_t glow = RR_RGB565(13, 87, 130);
    rr_clear(canvas, bg);

    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    rr_ellipse(canvas, 80 + bx, 101 + by, 27, 14, shadow);
    rr_ellipse(canvas, 80 + bx, 98 + by, 24, 14, shell);
    rr_round_rect(canvas, 71 + bx, 81 + by, 18, 21, 8, shell);
    rr_quad(
        canvas,
        51 + bx,
        91 + by,
        69 + bx,
        88 + by,
        65 + bx,
        106 + by,
        45 + bx,
        102 + by,
        shell);
    rr_quad(
        canvas,
        109 + bx,
        91 + by,
        91 + bx,
        88 + by,
        95 + bx,
        106 + by,
        115 + bx,
        102 + by,
        shell);

    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    rr_ellipse(canvas, 80 + sx, 55 + sy, 59, 47, shadow);
    rr_ellipse(canvas, 80 + sx, 53 + sy, 57, 45, shell);
    rr_outline_round_rect(
        canvas,
        23 + sx,
        28 + sy,
        114,
        55,
        25,
        2,
        glow,
        visor);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int gaze_x =
            (pose->pupil_x[eye] - pose->eye_x[eye]) / 2;
        const int gaze_y =
            (pose->pupil_y[eye] - pose->eye_y[eye]) / 3;
        const int cx = pose->eye_x[eye] + sx + gaze_x;
        const int cy = pose->eye_y[eye] + sy + gaze_y +
            (eye == 0U ? -pose->head_roll / 6
                       : pose->head_roll / 6);
        const int width = rr_clamp(
            pose->eye_w[eye] -
                ((int)pose->speech_round - 128) / 30,
            15,
            48);
        const int open = rr_clamp(
            pose->eye_open[eye] +
                ((int)pose->speech_open - 128) / 32,
            2,
            34);
        rr_round_rect(
            canvas,
            cx - width / 2 - 1,
            cy - open / 2 - 1,
            width + 2,
            open + 2,
            11,
            glow);
        rr_masked_eye(
            canvas,
            pose,
            eye,
            cx,
            cy,
            width,
            open,
            10,
            blue,
            visor);
        if (open > 9 && eye == pose->detail_phase / 16U) {
            const int glint =
                (int)((pose->detail_phase % 16U) / 2U);
            rr_rect(
                canvas,
                cx - 4 + glint,
                cy - open / 4 +
                    (int)(pose->detail_phase % 2U),
                2,
                2,
                shell);
        }
    }

    /* The neck light anticipates, sustains and settles with speech phase. */
    const int light_w =
        pose->speech_phase == FACE_SPEECH_STARTING ? 5
        : pose->speech_phase == FACE_SPEECH_ACTIVE ? 8
        : pose->speech_phase == FACE_SPEECH_ENDING ? 4
        : 2;
    rr_round_rect(
        canvas, 80 - light_w / 2 + bx, 91 + by,
        light_w, 3, 1, blue);
}

static void rr_draw_jibo(
    rr_canvas_t *canvas,
    const face_robot_redux_pose_t *pose)
{
    const uint16_t bg = RR_RGB565(21, 29, 40);
    const uint16_t shadow = RR_RGB565(106, 121, 136);
    const uint16_t shell = RR_RGB565(236, 240, 238);
    const uint16_t rim = RR_RGB565(170, 180, 185);
    const uint16_t display = RR_RGB565(19, 29, 37);
    const uint16_t blue = RR_RGB565(57, 190, 229);
    const uint16_t deep = RR_RGB565(11, 74, 101);
    rr_clear(canvas, bg);

    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    rr_ellipse(canvas, 80 + bx, 105 + by, 39, 11, shadow);
    rr_ellipse(canvas, 80 + bx, 102 + by, 36, 11, shell);
    rr_quad(
        canvas,
        68 + bx,
        91 + by,
        92 + bx,
        91 + by,
        98 + bx,
        104 + by,
        62 + bx,
        104 + by,
        rim);
    rr_round_rect(canvas, 68 + bx, 84 + by, 24, 17, 8, shell);

    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    rr_ellipse(canvas, 82 + sx, 54 + sy, 45, 45, shadow);
    rr_ellipse(canvas, 80 + sx, 51 + sy, 43, 43, shell);
    rr_ring(
        canvas,
        80 + sx,
        51 + sy,
        31,
        31,
        3,
        rim,
        display);

    const int eye_rx = rr_clamp(
        pose->eye_w[0] / 3 +
            ((int)pose->speech_width - 128) / 32,
        14,
        23);
    const int eye_ry = rr_clamp(
        pose->eye_open[0] / 3 +
            ((int)pose->speech_open - 128) / 60 -
            pose->speech_press / 128,
        2,
        21);
    const int gaze_x =
        rr_clamp(pose->pupil_x[0] - pose->eye_x[0], -9, 9);
    const int gaze_y =
        rr_clamp(pose->pupil_y[0] - pose->eye_y[0], -8, 8);
    const int cx = 80 + sx + gaze_x;
    const int cy = 51 + sy + gaze_y;
    rr_ellipse(canvas, cx, cy, eye_rx, eye_ry, deep);
    rr_ellipse(
        canvas,
        cx,
        cy,
        rr_clamp(eye_rx - 4, 2, eye_rx),
        rr_clamp(eye_ry - 3, 1, eye_ry),
        blue);

    int tl;
    int tr;
    int bl;
    int br;
    rr_lid_depths(pose, 0U, eye_ry * 2, &tl, &tr, &bl, &br);
    if (tl > 0 || tr > 0) {
        rr_quad(
            canvas,
            cx - eye_rx,
            cy - eye_ry,
            cx + eye_rx,
            cy - eye_ry,
            cx + eye_rx,
            cy - eye_ry + tr,
            cx - eye_rx,
            cy - eye_ry + tl,
            display);
    }
    if (bl > 0 || br > 0) {
        rr_quad(
            canvas,
            cx - eye_rx,
            cy + eye_ry - bl,
            cx + eye_rx,
            cy + eye_ry - br,
            cx + eye_rx,
            cy + eye_ry,
            cx - eye_rx,
            cy + eye_ry,
            display);
    }

    const int pupil = rr_clamp(
        pose->pupil_radius[0] +
            ((int)pose->speech_round - 128) / 55,
        5,
        20);
    rr_ellipse(canvas, cx, cy, pupil, pupil, display);
    const int detail_x = (int)(pose->consonant % 7U) - 3;
    const int detail_y = (int)((pose->consonant / 7U) % 5U) - 2;
    rr_ellipse(canvas, cx - pupil / 3 + detail_x,
               cy - pupil / 3 + detail_y, 2, 2, shell);
    rr_put(
        canvas,
        cx + (int)(pose->detail_phase % 8U) - 4,
        cy + (int)(pose->detail_phase / 8U) - 2,
        shell);

    /*
     * Two short rim ticks carry head roll and speech cadence while remaining
     * attached to the circular display.
     */
    const int tick = pose->head_roll / 2 + pose->speech_bob;
    rr_thick_line(
        canvas, 48 + sx, 49 + sy + tick,
        52 + sx, 45 + sy + tick, 2, blue);
    rr_thick_line(
        canvas, 108 + sx, 45 + sy - tick,
        112 + sx, 49 + sy - tick, 2, blue);
}

bool face_robot_redux_render(
    face_robot_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!rr_style_valid(style) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_ROBOT_REDUX_PIXEL_COUNT) {
        return false;
    }
    face_robot_redux_pose_t pose;
    if (!face_robot_redux_resolve(
            style, render_key, sample_clock, &pose)) {
        return false;
    }
    rr_canvas_t canvas = {rgb565};
    switch (style) {
    case FACE_ROBOT_REDUX_ROBOEYES_ALERT:
        rr_draw_roboeyes_alert(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_ROBOEYES_SOFT:
        rr_draw_roboeyes_soft(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_M5_AVATAR_CLASSIC:
        rr_draw_m5_classic(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_M5_AVATAR_MANGA:
        rr_draw_m5_manga(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_EVE_MINIMAL:
        rr_draw_eve(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_JIBO_ORB:
        rr_draw_jibo(&canvas, &pose);
        break;
    case FACE_ROBOT_REDUX_COUNT:
        return false;
    }
    return true;
}

bool face_robot_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_robot_redux_style_t style;
    return face_robot_redux_from_legacy_id(legacy_profile_id, &style) &&
        face_robot_redux_render(
            style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
