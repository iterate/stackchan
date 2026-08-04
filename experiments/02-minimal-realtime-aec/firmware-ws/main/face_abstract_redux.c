#include "face_abstract_redux.h"

#include "face_pose.h"
#include "face_stage.h"

#include <string.h>

#define AR_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    AR_SAFE = 3,
    AR_EXPRESSION_COUNT = 11,
};

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t mouth_kind;
    uint8_t ops;
} ar_actor_t;

typedef struct {
    int8_t eye_open_left;
    int8_t eye_open_right;
    int8_t brow_raise_left;
    int8_t brow_raise_right;
    int8_t brow_slope_left;
    int8_t brow_slope_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t corner_left;
    int8_t corner_right;
    int8_t lean_x;
    int8_t lean_y;
} ar_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
} ar_viseme_t;

typedef struct {
    uint16_t *pixels;
} ar_canvas_t;

static const ar_actor_t AR_ACTORS[FACE_ABSTRACT_REDUX_COUNT] = {
    [FACE_ABSTRACT_REDUX_NEON_RIBBON] = {
        "neon-ribbon-performer",
        "Neon Ribbon Performer",
        30U,
        FACE_ABSTRACT_REDUX_MOUTH_RIBBON,
        13U,
    },
    [FACE_ABSTRACT_REDUX_LIQUID_FAMILIAR] = {
        "liquid-droplet-familiar",
        "Liquid Droplet Familiar",
        31U,
        FACE_ABSTRACT_REDUX_MOUTH_LIQUID,
        14U,
    },
    [FACE_ABSTRACT_REDUX_CRT_PUPPET] = {
        "crt-phosphor-puppet",
        "CRT Phosphor Puppet",
        32U,
        FACE_ABSTRACT_REDUX_MOUTH_MATRIX,
        5U,
    },
    [FACE_ABSTRACT_REDUX_VOICE_ORBIT] = {
        "voice-orbit-familiar",
        "Voice Orbit Familiar",
        34U,
        FACE_ABSTRACT_REDUX_MOUTH_NONE,
        12U,
    },
    [FACE_ABSTRACT_REDUX_EDGE_SENTINEL] = {
        "edge-light-sentinel",
        "Edge-Light Sentinel",
        37U,
        FACE_ABSTRACT_REDUX_MOUTH_LINE,
        8U,
    },
};

/*
 * Geometry, not palette, carries all eleven directions.  Asymmetry is
 * intentional for thought, scepticism, and embarrassment.
 */
static const ar_expression_t AR_EXPRESSIONS[AR_EXPRESSION_COUNT] = {
    /* neutral */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* warm */
    {-2, -2, -2, -2, -2, 2, 0, 1, 1, 5, 5, 5, 0, 1},
    /* joy */
    {-8, -8, -4, -4, -4, 4, 0, 1, 5, 10, 10, 10, 0, -1},
    /* concern */
    {1, 0, -6, -6, 6, -6, -3, 3, -1, -3, -8, -8, -2, 2},
    /* surprise */
    {9, 9, -9, -9, 0, 0, 0, -3, 13, -8, 0, 0, 0, -2},
    /* thoughtful */
    {-4, -10, -4, 1, 5, -2, -10, -5, -2, -5, -4, 2, 3, 0},
    /* skeptical */
    {-10, 1, -7, 3, 7, -7, 10, 0, -2, 3, -7, 4, -3, 0},
    /* determined */
    {-7, -7, 3, 3, -7, 7, 0, 1, 0, 5, -4, -4, 0, 1},
    /* sleepy */
    {-15, -15, 4, 4, 1, -1, -3, 5, 2, -5, 1, 1, 2, 3},
    /* excited */
    {8, 8, -9, -9, -3, 3, 0, -4, 11, 12, 9, 9, 0, -3},
    /* embarrassed */
    {-7, -11, -3, -6, 4, -5, 10, 5, -1, -2, 5, 1, 3, 2},
};

static const ar_viseme_t AR_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {225U, 164U, 20U, 0U},
    [FACE_VISEME_E] = {96U, 238U, 8U, 0U},
    [FACE_VISEME_I] = {58U, 224U, 4U, 0U},
    [FACE_VISEME_O] = {202U, 96U, 244U, 0U},
    [FACE_VISEME_U] = {118U, 72U, 255U, 0U},
    [FACE_VISEME_PP] = {4U, 166U, 20U, 255U},
    [FACE_VISEME_SS] = {44U, 232U, 4U, 34U},
    [FACE_VISEME_TH] = {80U, 188U, 18U, 0U},
    [FACE_VISEME_DD] = {86U, 176U, 14U, 0U},
    [FACE_VISEME_FF] = {40U, 202U, 8U, 54U},
    [FACE_VISEME_KK] = {134U, 182U, 30U, 0U},
    [FACE_VISEME_NN] = {52U, 170U, 18U, 12U},
    [FACE_VISEME_RR] = {108U, 142U, 128U, 0U},
    [FACE_VISEME_CH] = {90U, 206U, 26U, 0U},
    [FACE_VISEME_SIL] = {5U, 142U, 24U, 224U},
};

static int ar_clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int ar_abs(int value)
{
    return value < 0 ? -value : value;
}

static int ar_mix(int from, int to, uint8_t weight)
{
    return from + ((to - from) * (int)weight + 127) / 255;
}

static uint8_t ar_smooth_q8(int value)
{
    const int t = ar_clamp(value, 0, 255);
    return (uint8_t)(
        t * t * (765 - t * 2) / (255 * 255));
}

static uint32_t ar_input_signature(const face_render_key_t *render_key)
{
    const uint8_t *bytes = (const uint8_t *)render_key;
    uint32_t signature = 2166136261U;
    for (size_t index = 0U; index < sizeof(*render_key); ++index) {
        signature ^= bytes[index];
        signature *= 16777619U;
    }
    return signature;
}

static int ar_wave(uint32_t sample_clock, uint32_t period)
{
    if (period < 2U) {
        return 0;
    }
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    return phase < half
        ? (int)(phase * 254U / half) - 127
        : 127 - (int)((phase - half) * 254U / half);
}

static uint16_t ar_mix_color(
    uint16_t from, uint16_t to, uint8_t weight)
{
    const int red = ar_mix(
        (from >> 11U) & 0x1fU,
        (to >> 11U) & 0x1fU,
        weight);
    const int green = ar_mix(
        (from >> 5U) & 0x3fU,
        (to >> 5U) & 0x3fU,
        weight);
    const int blue = ar_mix(
        from & 0x1fU,
        to & 0x1fU,
        weight);
    return (uint16_t)(
        ((uint16_t)red << 11U) |
        ((uint16_t)green << 5U) |
        (uint16_t)blue);
}

static bool ar_style_valid(face_abstract_redux_style_t style)
{
    return (unsigned)style < (unsigned)FACE_ABSTRACT_REDUX_COUNT;
}

static void ar_put(
    ar_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x >= AR_SAFE && x < FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE &&
        y >= AR_SAFE && y < FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE) {
        canvas->pixels[
            (size_t)y * FACE_ABSTRACT_REDUX_WIDTH + (size_t)x] = color;
    }
}

static void ar_clear(ar_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_ABSTRACT_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void ar_rect(
    ar_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    const int left = ar_clamp(x, AR_SAFE, FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE);
    const int top = ar_clamp(y, AR_SAFE, FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE);
    const int right = ar_clamp(
        x + width, AR_SAFE, FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE);
    const int bottom = ar_clamp(
        y + height, AR_SAFE, FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            ar_put(canvas, xx, yy, color);
        }
    }
}

static void ar_line(
    ar_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    int dx = ar_abs(x1 - x0);
    const int step_x = x0 < x1 ? 1 : -1;
    int dy = -ar_abs(y1 - y0);
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    const int radius = ar_clamp(thickness, 1, 7) / 2;
    for (;;) {
        ar_rect(
            canvas,
            x0 - radius,
            y0 - radius,
            radius * 2 + 1,
            radius * 2 + 1,
            color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (twice <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

static void ar_ellipse(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius_x,
    int radius_y,
    uint16_t color)
{
    if (radius_x < 1 || radius_y < 1) {
        return;
    }
    const int left = ar_clamp(
        center_x - radius_x, AR_SAFE, FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int right = ar_clamp(
        center_x + radius_x, AR_SAFE, FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int top = ar_clamp(
        center_y - radius_y, AR_SAFE, FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int bottom = ar_clamp(
        center_y + radius_y, AR_SAFE, FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int64_t limit =
        (int64_t)radius_x * radius_x * radius_y * radius_y;
    for (int y = top; y <= bottom; ++y) {
        const int64_t offset_y = y - center_y;
        for (int x = left; x <= right; ++x) {
            const int64_t offset_x = x - center_x;
            if (offset_x * offset_x * radius_y * radius_y +
                    offset_y * offset_y * radius_x * radius_x <=
                limit) {
                ar_put(canvas, x, y, color);
            }
        }
    }
}

static void ar_ellipse_outline(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius_x,
    int radius_y,
    int thickness,
    uint16_t color,
    uint16_t interior)
{
    ar_ellipse(canvas, center_x, center_y, radius_x, radius_y, color);
    ar_ellipse(
        canvas,
        center_x,
        center_y,
        ar_clamp(radius_x - thickness, 1, radius_x),
        ar_clamp(radius_y - thickness, 1, radius_y),
        interior);
}

static int64_t ar_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int64_t)(px - ax) * (by - ay) -
        (int64_t)(py - ay) * (bx - ax);
}

static void ar_triangle(
    ar_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int left = ar_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        AR_SAFE,
        FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int right = ar_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        AR_SAFE,
        FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int top = ar_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        AR_SAFE,
        FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int bottom = ar_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        AR_SAFE,
        FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int64_t orientation = ar_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int64_t e0 = ar_edge(ax, ay, bx, by, x, y);
            const int64_t e1 = ar_edge(bx, by, cx, cy, x, y);
            const int64_t e2 = ar_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                ar_put(canvas, x, y, color);
            }
        }
    }
}

static void ar_gradient(
    ar_canvas_t *canvas, uint16_t top, uint16_t bottom)
{
    /*
     * ar_rect() deliberately preserves a three-pixel safe border. Seed the
     * whole frame first so those pixels are deterministic rather than stale
     * caller memory.
     */
    ar_clear(canvas, top);
    for (int y = 0; y < FACE_ABSTRACT_REDUX_HEIGHT; ++y) {
        const uint16_t color = ar_mix_color(
            top,
            bottom,
            (uint8_t)(y * 255 / (FACE_ABSTRACT_REDUX_HEIGHT - 1)));
        ar_rect(canvas, 0, y, FACE_ABSTRACT_REDUX_WIDTH, 1, color);
    }
}

static uint8_t ar_viseme_index(
    uint8_t set, uint8_t raw)
{
    if (raw == FACE_VISEME_NONE) {
        return FACE_VISEME_SIL;
    }
    if (set == FACE_VISEME_SET_VRM5) {
        static const uint8_t mapping[5] = {
            FACE_VISEME_AA,
            FACE_VISEME_I,
            FACE_VISEME_U,
            FACE_VISEME_E,
            FACE_VISEME_O,
        };
        return raw < 5U ? mapping[raw] : FACE_VISEME_SIL;
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        static const uint8_t mapping[9] = {
            FACE_VISEME_SIL,
            FACE_VISEME_PP,
            FACE_VISEME_DD,
            FACE_VISEME_AA,
            FACE_VISEME_O,
            FACE_VISEME_U,
            FACE_VISEME_E,
            FACE_VISEME_TH,
            FACE_VISEME_I,
        };
        return raw < 9U ? mapping[raw] : FACE_VISEME_SIL;
    }
    return raw < FACE_VISEME_COUNT
        ? raw
        : (uint8_t)(raw % FACE_VISEME_COUNT);
}

static ar_viseme_t ar_blended_viseme(
    const face_render_key_t *render_key)
{
    const ar_viseme_t first = AR_VISEMES[ar_viseme_index(
        render_key->viseme_set, render_key->viseme)];
    const ar_viseme_t second = AR_VISEMES[ar_viseme_index(
        render_key->viseme_set, render_key->viseme_secondary)];
    return (ar_viseme_t){
        .open = (uint8_t)ar_mix(
            first.open, second.open, render_key->viseme_blend),
        .width = (uint8_t)ar_mix(
            first.width, second.width, render_key->viseme_blend),
        .round = (uint8_t)ar_mix(
            first.round, second.round, render_key->viseme_blend),
        .press = (uint8_t)ar_mix(
            first.press, second.press, render_key->viseme_blend),
    };
}

size_t face_abstract_redux_count(void)
{
    return FACE_ABSTRACT_REDUX_COUNT;
}

const char *face_abstract_redux_slug(
    face_abstract_redux_style_t style)
{
    return ar_style_valid(style) ? AR_ACTORS[style].slug : NULL;
}

const char *face_abstract_redux_name(
    face_abstract_redux_style_t style)
{
    return ar_style_valid(style) ? AR_ACTORS[style].name : NULL;
}

bool face_abstract_redux_info(
    face_abstract_redux_style_t style,
    face_abstract_redux_info_t *info)
{
    if (!ar_style_valid(style) || info == NULL) {
        return false;
    }
    info->slug = AR_ACTORS[style].slug;
    info->name = AR_ACTORS[style].name;
    info->legacy_profile_id = AR_ACTORS[style].legacy_id;
    info->mouth_kind = AR_ACTORS[style].mouth_kind;
    info->estimated_ops_per_pixel = AR_ACTORS[style].ops;
    return true;
}

bool face_abstract_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_abstract_redux_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        if (AR_ACTORS[raw].legacy_id == legacy_profile_id) {
            *style = (face_abstract_redux_style_t)raw;
            return true;
        }
    }
    return false;
}

bool face_abstract_redux_resolve(
    face_abstract_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_abstract_redux_pose_t *pose)
{
    if (!ar_style_valid(style) || render_key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, sizeof(pose->source));
    pose->input_signature = ar_input_signature(render_key);
    pose->face_center_x = 80;
    switch (style) {
    case FACE_ABSTRACT_REDUX_NEON_RIBBON:
        pose->face_center_y = 55;
        pose->eye_center_x[0] = 50;
        pose->eye_center_x[1] = 110;
        pose->eye_center_y[0] = 55;
        pose->eye_center_y[1] = 55;
        pose->mouth_center_x = 80;
        pose->mouth_center_y = 91;
        break;
    case FACE_ABSTRACT_REDUX_LIQUID_FAMILIAR:
        pose->face_center_y = 57;
        pose->eye_center_x[0] = 58;
        pose->eye_center_x[1] = 102;
        pose->eye_center_y[0] = 53;
        pose->eye_center_y[1] = 53;
        pose->mouth_center_x = 80;
        pose->mouth_center_y = 87;
        break;
    case FACE_ABSTRACT_REDUX_CRT_PUPPET:
        pose->face_center_y = 53;
        pose->eye_center_x[0] = 50;
        pose->eye_center_x[1] = 102;
        pose->eye_center_y[0] = 53;
        pose->eye_center_y[1] = 53;
        pose->mouth_center_x = 80;
        pose->mouth_center_y = 84;
        break;
    case FACE_ABSTRACT_REDUX_VOICE_ORBIT:
        pose->face_center_y = 59;
        pose->eye_center_x[0] = 80;
        pose->eye_center_x[1] = 80;
        pose->eye_center_y[0] = 59;
        pose->eye_center_y[1] = 59;
        pose->mouth_center_x = 80;
        pose->mouth_center_y = 92;
        break;
    case FACE_ABSTRACT_REDUX_EDGE_SENTINEL:
        pose->face_center_y = 57;
        pose->eye_center_x[0] = 59;
        pose->eye_center_x[1] = 101;
        pose->eye_center_y[0] = 58;
        pose->eye_center_y[1] = 58;
        pose->mouth_center_x = 80;
        pose->mouth_center_y = 86;
        break;
    default:
        return false;
    }
    const uint8_t expression =
        render_key->stage_expression < AR_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const uint8_t weight = render_key->expression_weight;
    const ar_expression_t *target = &AR_EXPRESSIONS[expression];
#define AR_EXPR(field) ar_mix(0, target->field, weight)
    pose->stage_expression = expression;
    pose->expression_weight = weight;
    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ACTIVE ||
        render_key->speech_phase == FACE_SPEECH_ENDING;
    pose->speech_phase = render_key->speech_phase;
    (void)sample_clock;
    pose->gaze_x = (int16_t)ar_clamp(
        render_key->controls.look_x / 7 +
            render_key->head_yaw / 14 +
            AR_EXPR(gaze_x),
        -12,
        12);
    pose->gaze_y = (int16_t)ar_clamp(
        render_key->controls.look_y / 9 +
            render_key->head_pitch / 16 +
            AR_EXPR(gaze_y),
        -8,
        8);
    const uint8_t eye_inputs[2] = {
        render_key->controls.eye_left_open,
        render_key->controls.eye_right_open,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        int aperture = 7 + eye_inputs[eye] * 17 / 255 +
            (eye == 0U
                ? AR_EXPR(eye_open_left)
                : AR_EXPR(eye_open_right));
        aperture -= (eye == 0U
                ? render_key->eye_left_squint
                : render_key->eye_right_squint) /
            26;
        if ((render_key->controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            aperture = 1;
        }
        pose->eye_open[eye] =
            (int16_t)ar_clamp(aperture, 1, 31);
        const int brow_raise = eye == 0U
            ? AR_EXPR(brow_raise_left)
            : AR_EXPR(brow_raise_right);
        const int brow_outer = eye == 0U
            ? render_key->brow_outer_left
            : render_key->brow_outer_right;
        pose->brow_y[eye] = (int16_t)ar_clamp(
            29 + brow_raise - render_key->brow_inner / 30 -
                brow_outer / 34,
            14,
            43);
        pose->brow_slope[eye] = (int16_t)ar_clamp(
            (eye == 0U
                ? AR_EXPR(brow_slope_left)
                : AR_EXPR(brow_slope_right)) +
                render_key->head_roll / 18,
            -11,
            11);
    }
    const ar_viseme_t viseme = ar_blended_viseme(render_key);
    pose->speech_open = (uint8_t)ar_mix(
        render_key->controls.mouth_open,
        viseme.open,
        render_key->viseme_weight);
    pose->speech_width = (uint8_t)ar_mix(
        render_key->controls.mouth_width,
        viseme.width,
        render_key->viseme_weight);
    pose->speech_round = (uint8_t)ar_mix(
        render_key->controls.mouth_round,
        viseme.round,
        render_key->viseme_weight);
    pose->speech_press = (uint8_t)ar_mix(
        render_key->controls.mouth_press,
        viseme.press,
        render_key->viseme_weight);
    const int raw_drive = pose->speaking
        ? ((int)render_key->viseme_weight * 3 +
              render_key->controls.mouth_open) /
            4
        : 0;
    pose->speech_drive_q8 = ar_smooth_q8(raw_drive);
    pose->anticipation_q8 =
        render_key->speech_phase == FACE_SPEECH_STARTING
        ? pose->speech_drive_q8
        : 0U;
    pose->settle_q8 =
        render_key->speech_phase == FACE_SPEECH_ENDING
        ? (uint8_t)(255U - pose->speech_drive_q8)
        : 0U;
    int phase_gain = pose->speaking ? 255 : 0;
    if (render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ENDING) {
        phase_gain = pose->speech_drive_q8;
    }
    const int base_envelope = ar_smooth_q8(
        ((int)pose->speech_open * 3 + pose->speech_round) / 4);
    pose->speech_envelope_q8 = (uint8_t)(
        base_envelope * phase_gain / 255);
    pose->speech_pulse =
        pose->speaking
        ? 1 + pose->speech_envelope_q8 * 5 / 255
        : 0;
    int resolved_open = pose->speech_open;
    if (render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ENDING) {
        resolved_open = resolved_open * phase_gain / 255;
    }
    pose->mouth_width = (int16_t)ar_clamp(
        24 + pose->speech_width * 42 / 255 -
            pose->speech_round / 22 + AR_EXPR(mouth_width),
        14,
        70);
    pose->mouth_height = (int16_t)ar_clamp(
        2 + resolved_open * 25 / 255 -
            pose->speech_press * 6 / 255 + AR_EXPR(mouth_open),
        2,
        32);
    pose->mouth_corner[0] = (int16_t)ar_clamp(
        render_key->mouth_corner_left / 13 +
            render_key->affect_valence / 19 +
            AR_EXPR(corner_left),
        -12,
        12);
    pose->mouth_corner[1] = (int16_t)ar_clamp(
        render_key->mouth_corner_right / 13 +
            render_key->affect_valence / 19 +
            AR_EXPR(corner_right),
        -12,
        12);
    pose->lean_x = (int16_t)ar_clamp(
        render_key->body_lean_x / 15 +
            render_key->head_roll / 24 +
            AR_EXPR(lean_x),
        -7,
        7);
    pose->lean_y = (int16_t)ar_clamp(
        render_key->body_lean_y / 18 +
            render_key->head_pitch / 24 +
            AR_EXPR(lean_y),
        -5,
        5);
    if ((render_key->controls.flags &
            FACE_KEYFRAME_FLAG_BLINKING) == 0U) {
        const int start_lift =
            render_key->speech_phase == FACE_SPEECH_STARTING
            ? 2 + pose->anticipation_q8 / 92
            : 0;
        const int active_lift =
            render_key->speech_phase == FACE_SPEECH_ACTIVE
            ? 1 + pose->speech_envelope_q8 / 150
            : 0;
        const int ending_lift =
            render_key->speech_phase == FACE_SPEECH_ENDING
            ? (255 - pose->settle_q8) / 170
            : 0;
        pose->eye_open[0] = (int16_t)ar_clamp(
            pose->eye_open[0] + start_lift + active_lift + ending_lift,
            1,
            31);
        pose->eye_open[1] = (int16_t)ar_clamp(
            pose->eye_open[1] + start_lift +
                active_lift * 2 / 3 + ending_lift / 2,
            1,
            31);
        pose->brow_y[0] = (int16_t)ar_clamp(
            pose->brow_y[0] - start_lift -
                active_lift / 2 + pose->settle_q8 / 170,
            14,
            43);
        pose->brow_y[1] = (int16_t)ar_clamp(
            pose->brow_y[1] - start_lift * 2 / 3 -
                active_lift / 3 + pose->settle_q8 / 128,
            14,
            43);
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            pose->gaze_y = (int16_t)ar_clamp(
                pose->gaze_y - 1 - pose->anticipation_q8 / 170,
                -8,
                8);
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            pose->gaze_x = (int16_t)(
                pose->gaze_x *
                (255 - pose->settle_q8 / 2) /
                255);
            pose->gaze_y = (int16_t)ar_clamp(
                pose->gaze_y + pose->settle_q8 / 150,
                -8,
                8);
        }
    }
#undef AR_EXPR
    return true;
}

static void ar_draw_brows(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    int left_x,
    int right_x,
    int half_width,
    int thickness,
    uint16_t color)
{
    const int centers[2] = {left_x, right_x};
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int direction = eye == 0U ? 1 : -1;
        const int slope = pose->brow_slope[eye] * direction;
        ar_line(
            canvas,
            centers[eye] - half_width,
            pose->brow_y[eye] - slope / 2,
            centers[eye] + half_width,
            pose->brow_y[eye] + slope / 2,
            thickness,
            color);
    }
}

static void ar_draw_eye(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int half_width,
    int aperture,
    int gaze_x,
    int gaze_y,
    uint16_t rim,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil)
{
    const int half_height = ar_clamp(aperture, 1, 30);
    ar_ellipse(canvas, center_x, center_y, half_width + 3, half_height + 3, rim);
    ar_ellipse(canvas, center_x, center_y, half_width, half_height, sclera);
    if (half_height <= 2) {
        ar_line(
            canvas,
            center_x - half_width,
            center_y,
            center_x + half_width,
            center_y,
            2,
            rim);
        return;
    }
    const int iris_x = center_x + ar_clamp(gaze_x, -half_width / 2, half_width / 2);
    const int iris_y = center_y + ar_clamp(gaze_y, -half_height / 2, half_height / 2);
    const int iris_radius = ar_clamp(half_height * 2 / 3, 2, 9);
    ar_ellipse(canvas, iris_x, iris_y, iris_radius, iris_radius, iris);
    ar_ellipse(
        canvas,
        iris_x,
        iris_y,
        ar_clamp(iris_radius / 2, 1, 5),
        ar_clamp(iris_radius / 2, 1, 5),
        pupil);
    ar_rect(canvas, iris_x - 2, iris_y - 3, 2, 2, sclera);
}

static void ar_draw_cavity_mouth(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    int center_x,
    int center_y,
    uint16_t rim,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int radius_x = pose->mouth_width / 2;
    const int radius_y = pose->mouth_height / 2;
    const int left_x = center_x - radius_x;
    const int right_x = center_x + radius_x;
    const int left_y = center_y - pose->mouth_corner[0] / 3;
    const int right_y = center_y - pose->mouth_corner[1] / 3;
    if (pose->mouth_height <= 4) {
        ar_line(
            canvas, left_x, left_y, center_x, center_y + radius_y, 3, rim);
        ar_line(
            canvas, center_x, center_y + radius_y, right_x, right_y, 3, rim);
        return;
    }
    /*
     * Two parented wedges keep both mouth corners attached to the face.
     * Unlike the old scaled oval, their endpoints rise for a smile, fall for
     * concern, and can disagree for scepticism.  Round visemes continuously
     * narrow this same topology in resolve(), avoiding threshold pops.
     */
    ar_triangle(
        canvas,
        left_x - 2,
        left_y,
        right_x + 2,
        right_y,
        center_x,
        center_y - radius_y - 2,
        rim);
    ar_triangle(
        canvas,
        left_x - 2,
        left_y,
        right_x + 2,
        right_y,
        center_x,
        center_y + radius_y + 2,
        rim);
    ar_triangle(
        canvas,
        left_x + 2,
        left_y,
        right_x - 2,
        right_y,
        center_x,
        center_y - radius_y + 2,
        cavity);
    ar_triangle(
        canvas,
        left_x + 2,
        left_y,
        right_x - 2,
        right_y,
        center_x,
        center_y + radius_y - 2,
        cavity);
    /*
     * Keep a one-pixel tooth bed in the cavity and fade it from cavity colour
     * as the teeth control rises.  Adding/removing the whole bright row at an
     * integer threshold produced a visible pop once per jaw cycle.
     */
    const int tooth_height = 1 +
        pose->source.controls.mouth_teeth *
            ar_clamp(radius_y - 1, 0, radius_y) /
            510;
    ar_rect(
        canvas,
        center_x - radius_x + 3,
        center_y - radius_y + 2,
        radius_x * 2 - 6,
        ar_clamp(tooth_height, 1, radius_y),
        ar_mix_color(
            cavity,
            teeth,
            pose->source.controls.mouth_teeth));
    if (pose->source.tongue > 56U && radius_y >= 5) {
        ar_ellipse(
            canvas,
            center_x,
            center_y + radius_y - 2,
            ar_clamp(radius_x - 5, 2, radius_x),
            ar_clamp(pose->source.tongue * radius_y / 765, 1, radius_y),
            tongue);
    }
}

/*
 * A soft, continuously parameterised mouth for organic and neon actors.
 *
 * A scaled ellipse made every emotion read as the same jaw flap.  Build the
 * lip edges from the two parented corners instead: the endpoints remain
 * attached, the centre returns to the facial baseline, and the same topology
 * produces a smile, frown, asymmetric smirk, press, or open viseme.
 */
static void ar_draw_soft_mouth(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    int center_x,
    int center_y,
    uint16_t rim,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int radius_x = ar_clamp(pose->mouth_width / 2, 7, 35);
    const int radius_y = ar_clamp(pose->mouth_height / 2, 1, 16);
    const int left_x = center_x - radius_x;
    const int right_x = center_x + radius_x;
    const int left_y = center_y - pose->mouth_corner[0] / 2;
    const int right_y = center_y - pose->mouth_corner[1] / 2;

    if (pose->mouth_height <= 4 || pose->speech_press > 232U) {
        const int press_y =
            center_y + (pose->speech_round > 176U ? 1 : 0);
        ar_line(canvas, left_x, left_y, center_x, press_y, 3, rim);
        ar_line(canvas, center_x, press_y, right_x, right_y, 3, rim);
        return;
    }

    const uint8_t tooth_weight = pose->source.controls.mouth_teeth;
    const uint8_t tongue_weight = pose->source.tongue;
    int previous_top = left_y;
    int previous_bottom = left_y;
    const int mean_corner_y = (left_y + right_y) / 2;
    for (int x = left_x; x <= right_x; ++x) {
        const int fraction =
            (x - left_x) * 255 / ar_clamp(right_x - left_x, 1, 70);
        const int edge =
            ar_abs(x - center_x) * 255 / ar_clamp(radius_x, 1, 35);
        const int bulge = 255 - edge * edge / 255;
        const int corner_line =
            left_y + (right_y - left_y) * fraction / 255;
        /*
         * At the endpoints the baseline is exactly the authored corner.  At
         * the centre it returns to center_y, giving valence a readable curve
         * instead of translating a whole oval up and down.
         */
        const int baseline =
            corner_line + (center_y - mean_corner_y) * bulge / 255;
        const int top_depth =
            (radius_y + pose->speech_round / 104) * bulge / 255;
        const int bottom_depth =
            (radius_y + pose->speech_open / 112) * bulge / 255;
        const int top = baseline - top_depth;
        const int bottom = baseline + bottom_depth;
        const int local_height = ar_clamp(bottom - top + 1, 1, 40);
        const int tooth_depth =
            tooth_weight * local_height / (255 * 3);
        const int tongue_depth =
            tongue_weight * local_height / (255 * 3);
        for (int y = top; y <= bottom; ++y) {
            uint16_t color = cavity;
            if (tooth_depth > 0 && y < top + tooth_depth) {
                color = ar_mix_color(cavity, teeth, tooth_weight);
            } else if (tongue_depth > 0 &&
                       y > bottom - tongue_depth) {
                color = ar_mix_color(cavity, tongue, tongue_weight);
            }
            ar_put(canvas, x, y, color);
        }
        if (x > left_x) {
            ar_line(
                canvas,
                x - 1,
                previous_top,
                x,
                top,
                2,
                rim);
            ar_line(
                canvas,
                x - 1,
                previous_bottom,
                x,
                bottom,
                2,
                rim);
        }
        previous_top = top;
        previous_bottom = bottom;
    }
    ar_rect(canvas, left_x - 1, left_y - 1, 3, 3, rim);
    ar_rect(canvas, right_x - 1, right_y - 1, 3, 3, rim);
}

static void ar_render_neon_ribbon(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint32_t sample_clock)
{
    ar_gradient(canvas, AR_RGB565(3, 4, 18), AR_RGB565(13, 2, 26));
    const uint16_t magenta = AR_RGB565(255, 48, 202);
    const uint16_t cyan = AR_RGB565(38, 239, 255);
    const uint16_t violet = AR_RGB565(80, 20, 118);
    const uint16_t white = AR_RGB565(238, 252, 255);
    const uint16_t dark = AR_RGB565(5, 2, 15);
    const int center_x = pose->face_center_x;
    const int center_y = pose->face_center_y;
    (void)sample_clock;
    /*
     * A stable visor gives every moving feature a parent silhouette at the
     * physical 40x30 size.  Previously the eyes, brows, nose, mouth, and
     * audio meters downsampled into unrelated neon fragments.
     */
    ar_ellipse_outline(
        canvas,
        center_x,
        center_y + 6,
        72,
        52,
        4,
        violet,
        dark);
    ar_draw_eye(
        canvas,
        pose->eye_center_x[0],
        pose->eye_center_y[0],
        22,
        pose->eye_open[0],
        pose->gaze_x,
        pose->gaze_y,
        magenta,
        white,
        cyan,
        dark);
    ar_draw_eye(
        canvas,
        pose->eye_center_x[1],
        pose->eye_center_y[1],
        22,
        pose->eye_open[1],
        pose->gaze_x,
        pose->gaze_y,
        cyan,
        white,
        magenta,
        dark);
    /*
     * Draw the expression rails over the eye rims so they remain attached
     * upper lids instead of hovering one contact pixel above the sockets.
     */
    ar_draw_brows(
        canvas,
        pose,
        pose->eye_center_x[0],
        pose->eye_center_x[1],
        20,
        4,
        magenta);
    ar_line(
        canvas,
        center_x - 9,
        center_y + 9,
        center_x,
        center_y + 14,
        2,
        violet);
    ar_line(
        canvas,
        center_x,
        center_y + 14,
        center_x + 9,
        center_y + 9,
        2,
        violet);
    ar_draw_soft_mouth(
        canvas,
        pose,
        pose->mouth_center_x,
        pose->mouth_center_y,
        magenta,
        dark,
        white,
        cyan);
    if (pose->speaking) {
        const int energy =
            3 + pose->speech_envelope_q8 * 8 / 255;
        const int radius_x = pose->mouth_width / 2;
        for (int side = -1; side <= 1; side += 2) {
            const int anchor_x =
                center_x + side * radius_x;
            ar_line(
                canvas,
                anchor_x,
                pose->mouth_center_y,
                anchor_x + side * 6,
                pose->mouth_center_y,
                2,
                magenta);
            ar_line(
                canvas,
                anchor_x + side * 7,
                pose->mouth_center_y - energy / 2,
                anchor_x + side * 7,
                pose->mouth_center_y + energy / 2,
                2,
                cyan);
        }
    }
}

static void ar_render_liquid(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint32_t sample_clock)
{
    ar_gradient(canvas, AR_RGB565(2, 18, 28), AR_RGB565(2, 5, 12));
    const uint16_t body = AR_RGB565(18, 166, 164);
    const uint16_t light = AR_RGB565(126, 255, 226);
    const uint16_t deep = AR_RGB565(1, 44, 56);
    const uint16_t ink = AR_RGB565(1, 10, 18);
    const uint16_t coral = AR_RGB565(255, 100, 128);
    const int center_x = pose->face_center_x;
    const int center_y = pose->face_center_y;
    /*
     * Idle breathing belongs to the character; speech does not squash and
     * translate the whole silhouette.  Freezing this one-pixel wobble while
     * speaking prevents the body from masquerading as lip-sync.
     */
    const int wobble =
        pose->speaking ? 0 : ar_wave(sample_clock, 12800U) / 96;
    ar_ellipse(canvas, center_x, center_y + 10, 47 + wobble, 48 - wobble, deep);
    ar_ellipse(canvas, center_x - 30, center_y + 25, 18, 24, body);
    ar_ellipse(canvas, center_x + 30, center_y + 23, 19, 25, body);
    ar_ellipse(canvas, center_x, center_y, 43 + wobble, 43 - wobble, body);
    ar_ellipse(canvas, center_x - 18, center_y - 25, 16, 8, light);
    ar_triangle(
        canvas,
        center_x - 39,
        center_y + 19,
        center_x - 49,
        center_y + 43,
        center_x - 27,
        center_y + 35,
        body);
    ar_triangle(
        canvas,
        center_x + 39,
        center_y + 18,
        center_x + 49,
        center_y + 42,
        center_x + 27,
        center_y + 35,
        body);
    ar_draw_brows(
        canvas, pose, center_x - 22, center_x + 22, 13, 3, deep);
    const int eye_y = pose->eye_center_y[0];
    ar_draw_eye(
        canvas,
        pose->eye_center_x[0],
        eye_y,
        14,
        ar_clamp(pose->eye_open[0] * 3 / 4, 2, 19),
        pose->gaze_x,
        pose->gaze_y,
        deep,
        light,
        coral,
        ink);
    ar_draw_eye(
        canvas,
        pose->eye_center_x[1],
        eye_y,
        14,
        ar_clamp(pose->eye_open[1] * 3 / 4, 2, 19),
        pose->gaze_x,
        pose->gaze_y,
        deep,
        light,
        coral,
        ink);
    ar_triangle(
        canvas,
        center_x - 31,
        eye_y + ar_clamp(pose->eye_open[0] * 3 / 8, 2, 10),
        center_x - 13,
        eye_y + ar_clamp(pose->eye_open[0] * 3 / 8, 2, 10),
        center_x - 18,
        eye_y + ar_clamp(pose->eye_open[0] * 3 / 8, 2, 10) + 7,
        light);
    ar_triangle(
        canvas,
        center_x + 13,
        eye_y + ar_clamp(pose->eye_open[1] * 3 / 8, 2, 10),
        center_x + 31,
        eye_y + ar_clamp(pose->eye_open[1] * 3 / 8, 2, 10),
        center_x + 18,
        eye_y + ar_clamp(pose->eye_open[1] * 3 / 8, 2, 10) + 7,
        light);
    ar_draw_soft_mouth(
        canvas,
        pose,
        pose->mouth_center_x,
        pose->mouth_center_y,
        light,
        ink,
        AR_RGB565(238, 255, 230),
        coral);
    if (pose->source.cheek > 80U ||
        pose->stage_expression == FACE_EXPRESSION_EMBARRASSED) {
        ar_ellipse(canvas, center_x - 35, center_y + 19, 6, 3, coral);
        ar_ellipse(canvas, center_x + 35, center_y + 19, 6, 3, coral);
    }
}

static void ar_fill_crt_octagon(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int left_x,
    int left_y,
    int top_left_x,
    int top_y,
    int top_right_x,
    int right_x,
    int right_y,
    int bottom_y,
    int side_span,
    uint16_t color)
{
    const int x[8] = {
        left_x,
        top_left_x,
        top_right_x,
        right_x,
        right_x,
        top_right_x,
        top_left_x,
        left_x,
    };
    const int y[8] = {
        left_y - side_span,
        top_y,
        top_y,
        right_y - side_span,
        right_y + side_span,
        bottom_y,
        bottom_y,
        left_y + side_span,
    };
    for (size_t edge = 0U; edge < 8U; ++edge) {
        const size_t next = (edge + 1U) % 8U;
        ar_triangle(
            canvas,
            center_x,
            center_y,
            x[edge],
            y[edge],
            x[next],
            y[next],
            color);
    }
}

/*
 * Contact-scale CRT mouth.
 *
 * Geometry remains continuous at native resolution, while four-pixel rims
 * survive as one intentional phosphor cell at the physical 40x30 contact
 * scale.  Every viseme and expression deforms one connected octagon.
 */
static void ar_draw_crt_mouth(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint16_t phosphor,
    uint16_t screen,
    uint16_t hot)
{
    static const int8_t EXPRESSION_RADIUS[AR_EXPRESSION_COUNT] = {
        4, 4, 7, 4, 11, 4, 4, 3, 3, 9, 4,
    };
    static const int8_t EXPRESSION_HALF_WIDTH_DELTA[AR_EXPRESSION_COUNT] = {
        0, 2, 3, -1, -3, -1, 2, 3, -2, 3, -1,
    };
    static const int8_t EXPRESSION_CORNER_LEFT[AR_EXPRESSION_COUNT] = {
        0, 2, 4, -4, 0, 2, -2, -1, 0, 3, 2,
    };
    static const int8_t EXPRESSION_CORNER_RIGHT[AR_EXPRESSION_COUNT] = {
        0, 2, 4, -4, 0, 0, 2, -1, 0, 3, -2,
    };
    const size_t expression =
        pose->stage_expression < AR_EXPRESSION_COUNT
            ? pose->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = pose->expression_weight;
    /*
     * The phosphor viewport is centred at x=76.  Keeping the facial anchors
     * on that centre matters at 40x30: the old x=80 mouth looked like it was
     * sliding off the right cheek even though it was technically in bounds.
     */
    const int center_x = 76;
    const int center_y = 84;
    const int half_width = ar_clamp(
        (pose->mouth_width + 1) / 2 +
            EXPRESSION_HALF_WIDTH_DELTA[expression] * 4 *
                expression_weight / 255,
        14,
        34);
    const int resolved_radius = ar_clamp(
        (pose->mouth_height + 1) / 2,
        3,
        12);
    const int directed_radius = ar_mix(
        3,
        EXPRESSION_RADIUS[expression],
        expression_weight);
    int radius_y =
        resolved_radius > directed_radius
            ? resolved_radius
            : directed_radius;
    radius_y = ar_mix(
        radius_y,
        3,
        pose->speech_press);
    radius_y = ar_clamp(radius_y, 3, 12);
    const int directed_left =
        EXPRESSION_CORNER_LEFT[expression] * 4 *
        expression_weight / 255;
    const int directed_right =
        EXPRESSION_CORNER_RIGHT[expression] * 4 *
        expression_weight / 255;
    const int top_y = center_y - radius_y;
    const int bottom_y = center_y + radius_y;
    const int left_y = ar_clamp(
        center_y -
            ar_clamp(
                pose->mouth_corner[0] + directed_left,
                -9,
                9),
        top_y,
        bottom_y);
    const int right_y = ar_clamp(
        center_y -
            ar_clamp(
                pose->mouth_corner[1] + directed_right,
                -9,
                9),
        top_y,
        bottom_y);
    const int left_x = center_x - half_width;
    const int right_x = center_x + half_width;
    const int inset = ar_clamp(half_width / 3, 8, 14);
    const int top_left_x = left_x + inset;
    const int top_right_x = right_x - inset;
    ar_fill_crt_octagon(
        canvas,
        center_x,
        center_y,
        left_x,
        left_y,
        top_left_x,
        top_y,
        top_right_x,
        right_x,
        right_y,
        bottom_y,
        ar_clamp(radius_y / 3, 1, 4),
        phosphor);

    /*
     * Carve a cavity from the same octagon.  Its colour fades in as height
     * grows, avoiding a binary "closed line -> open sprite" transition.
     */
    const int inner_radius = ar_clamp(radius_y - 4, 0, radius_y);
    const int inner_left_x = left_x + 5;
    const int inner_right_x = right_x - 5;
    const int inner_inset = ar_clamp(inset - 2, 3, 12);
    const int inner_top_left_x = inner_left_x + inner_inset;
    const int inner_top_right_x = inner_right_x - inner_inset;
    const int inner_left_y = ar_mix(left_y, center_y, 84U);
    const int inner_right_y = ar_mix(right_y, center_y, 84U);
    const uint8_t cavity_weight = (uint8_t)ar_clamp(
        (radius_y - 2) * 72,
        0,
        255);
    ar_fill_crt_octagon(
        canvas,
        center_x,
        center_y,
        inner_left_x,
        inner_left_y,
        inner_top_left_x,
        center_y - inner_radius,
        inner_top_right_x,
        inner_right_x,
        inner_right_y,
        center_y + inner_radius,
        ar_clamp(inner_radius / 3, 0, 3),
        ar_mix_color(phosphor, screen, cavity_weight));

    /* Teeth and tongue stay inside the connected rim and fade continuously. */
    ar_line(
        canvas,
        inner_top_left_x,
        center_y - inner_radius + 2,
        inner_top_right_x,
        center_y - inner_radius + 2,
        2,
        ar_mix_color(
            screen, hot, pose->source.controls.mouth_teeth));
    ar_line(
        canvas,
        center_x - half_width / 2,
        center_y + inner_radius - 2,
        center_x + half_width / 2,
        center_y + inner_radius - 2,
        2,
        ar_mix_color(screen, phosphor, pose->source.tongue));

    /*
     * A centre seam gives quiet phonemes one continuous readable gesture.
     * It fades into the cavity instead of disappearing at a radius threshold,
     * so opening speech cannot pop between unrelated mouth sprites.  Paint it
     * last so zero-strength teeth/tongue channels cannot erase the quiet lip.
     */
    const uint8_t seam_fade = (uint8_t)ar_clamp(
        (radius_y - 3) * 72,
        0,
        255);
    const uint16_t seam_color =
        ar_mix_color(phosphor, screen, seam_fade);
    ar_line(
        canvas,
        left_x + 2,
        left_y,
        center_x,
        center_y + 1,
        4,
        seam_color);
    ar_line(
        canvas,
        center_x,
        center_y + 1,
        right_x - 2,
        right_y,
        4,
        seam_color);
}

static void ar_fill_crt_eye_octagon(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int half_width,
    int half_height,
    int top_tilt,
    int bottom_tilt,
    uint16_t color)
{
    const int left = center_x - half_width;
    const int right = center_x + half_width;
    const int chamfer = ar_clamp(
        half_height / 2,
        2,
        4);
    const int top_left_y = center_y - half_height - top_tilt;
    const int top_right_y = center_y - half_height + top_tilt;
    const int bottom_left_y =
        center_y + half_height - bottom_tilt;
    const int bottom_right_y =
        center_y + half_height + bottom_tilt;
    const int x[8] = {
        left,
        left + chamfer,
        right - chamfer,
        right,
        right,
        right - chamfer,
        left + chamfer,
        left,
    };
    const int y[8] = {
        top_left_y + chamfer,
        top_left_y,
        top_right_y,
        top_right_y + chamfer,
        bottom_right_y - chamfer,
        bottom_right_y,
        bottom_left_y,
        bottom_left_y - chamfer,
    };
    for (size_t edge = 0U; edge < 8U; ++edge) {
        const size_t next = (edge + 1U) % 8U;
        ar_triangle(
            canvas,
            center_x,
            center_y,
            x[edge],
            y[edge],
            x[next],
            y[next],
            color);
    }
}

/*
 * One chunky phosphor socket with a Cozmo-like acting grammar: height,
 * asymmetry, gaze, and an angled upper lid all deform one stable silhouette.
 * The yellow signal is the socket's attached upper lid, never a floating
 * eyebrow sprite.
 */
static void ar_draw_crt_eye(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int half_width,
    int half_height,
    int top_tilt,
    int bottom_tilt,
    uint8_t lid_heat,
    int gaze_x,
    int gaze_y,
    uint16_t rim,
    uint16_t phosphor,
    uint16_t screen,
    uint16_t hot)
{
    half_width = ar_clamp(half_width, 10, 18);
    half_height = ar_clamp(half_height, 2, 16);
    top_tilt = ar_clamp(top_tilt, -6, 6);
    bottom_tilt = ar_clamp(bottom_tilt, -4, 4);
    const int outer_pad = ar_clamp(2 + half_height / 4, 2, 5);
    ar_fill_crt_eye_octagon(
        canvas,
        center_x,
        center_y,
        half_width + outer_pad,
        half_height + outer_pad,
        top_tilt,
        bottom_tilt,
        rim);
    ar_fill_crt_eye_octagon(
        canvas,
        center_x,
        center_y,
        half_width,
        half_height,
        top_tilt,
        bottom_tilt,
        phosphor);

    const int pupil_x = center_x + ar_clamp(gaze_x, -8, 8);
    const int pupil_y = center_y +
        ar_clamp(gaze_y, -ar_clamp(half_height - 4, 0, 10),
            ar_clamp(half_height - 4, 0, 10));
    const int pupil_h = ar_clamp(half_height * 2 - 6, 4, 12);
    const uint8_t pupil_weight = (uint8_t)ar_clamp(
        (half_height - 2) * 56,
        0,
        255);
    const uint16_t pupil_color =
        ar_mix_color(phosphor, screen, pupil_weight);
    ar_rect(
        canvas,
        pupil_x - 4,
        pupil_y - pupil_h / 2,
        8,
        pupil_h,
        pupil_color);
    ar_rect(
        canvas,
        pupil_x,
        pupil_y - pupil_h / 2,
        4,
        4,
        ar_mix_color(
            pupil_color,
            hot,
            (uint8_t)(
                (uint16_t)pupil_weight *
                (uint16_t)lid_heat /
                255U)));

    /*
     * Repaint the upper edge last.  It shares the exact top vertices with
     * the phosphor aperture, so it reads as an expressive lid at 40x30.
     */
    ar_line(
        canvas,
        center_x - half_width + 3,
        center_y - half_height - top_tilt,
        center_x + half_width - 3,
        center_y - half_height + top_tilt,
        4,
        ar_mix_color(phosphor, hot, lid_heat));
}

static void ar_render_crt(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint32_t sample_clock)
{
    (void)sample_clock;
    const uint16_t case_dark = AR_RGB565(18, 24, 21);
    const uint16_t case_light = AR_RGB565(72, 84, 67);
    const uint16_t screen = AR_RGB565(2, 20, 10);
    const uint16_t phosphor = AR_RGB565(116, 255, 112);
    const uint16_t hot = AR_RGB565(255, 226, 82);
    const uint16_t dim = AR_RGB565(18, 70, 37);
    ar_clear(canvas, AR_RGB565(3, 5, 4));
    ar_rect(canvas, 14, 9, 132, 102, case_light);
    ar_rect(canvas, 18, 13, 124, 94, case_dark);
    ar_rect(canvas, 25, 20, 102, 80, screen);
    ar_rect(canvas, 132, 28, 5, 44, dim);
    ar_rect(
        canvas,
        132,
        72 - pose->source.attention * 36 / 255,
        5,
        4 + pose->source.attention * 36 / 255,
        hot);
    for (int y = 24; y < 98; y += 8) {
        ar_rect(canvas, 27, y, 98, 1, dim);
    }
    static const int8_t EYE_HALF_HEIGHT[AR_EXPRESSION_COUNT][2] = {
        {10, 10}, /* neutral */
        {8, 8},   /* warm */
        {3, 3},   /* joy */
        {9, 9},   /* concern */
        {14, 14}, /* surprise */
        {9, 4},   /* thoughtful */
        {4, 9},   /* skeptical */
        {6, 6},   /* determined */
        {3, 3},   /* sleepy */
        {13, 13}, /* excited */
        {6, 4},   /* embarrassed */
    };
    static const int8_t EYE_HALF_WIDTH_DELTA[AR_EXPRESSION_COUNT][2] = {
        {0, 0},
        {1, 1},
        {2, 2},
        {-1, -1},
        {-2, -2},
        {0, 1},
        {1, 0},
        {1, 1},
        {2, 2},
        {-1, -1},
        {-1, 0},
    };
    static const int8_t EYE_TOP_TILT[AR_EXPRESSION_COUNT][2] = {
        {0, 0},
        {-2, 2},
        {-3, 3},
        {-4, 4},
        {0, 0},
        {-1, 2},
        {2, -1},
        {4, -4},
        {0, 0},
        {-2, 2},
        {-1, 2},
    };
    static const int8_t EYE_BOTTOM_TILT[AR_EXPRESSION_COUNT][2] = {
        {0, 0},
        {0, 0},
        {0, 0},
        {1, -1},
        {0, 0},
        {0, 1},
        {-1, 0},
        {1, -1},
        {0, 0},
        {0, 0},
        {0, 1},
    };
    static const int8_t EYE_Y_DELTA[AR_EXPRESSION_COUNT][2] = {
        {0, 0},
        {1, 1},
        {1, 1},
        {0, 0},
        {-2, -2},
        {-1, 1},
        {1, -1},
        {0, 0},
        {2, 2},
        {-1, -1},
        {1, 2},
    };
    static const uint8_t LID_HEAT[AR_EXPRESSION_COUNT] = {
        72U, 108U, 196U, 212U, 164U, 188U,
        220U, 244U, 88U, 208U, 164U,
    };
    const size_t expression =
        pose->stage_expression < AR_EXPRESSION_COUNT
            ? pose->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = pose->expression_weight;
    const int centers[2] = {
        pose->eye_center_x[0],
        pose->eye_center_x[1],
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int base_half_height =
            ar_clamp((pose->eye_open[eye] + 1) / 2, 2, 16);
        int half_height = ar_mix(
            base_half_height,
            EYE_HALF_HEIGHT[expression][eye],
            expression_weight);
        if ((pose->source.controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            half_height = 2;
        }
        const int half_width = ar_mix(
            16,
            16 + EYE_HALF_WIDTH_DELTA[expression][eye],
            expression_weight);
        const int top_tilt =
            EYE_TOP_TILT[expression][eye] *
            expression_weight / 255;
        const int bottom_tilt =
            EYE_BOTTOM_TILT[expression][eye] *
            expression_weight / 255;
        const int eye_y =
            pose->eye_center_y[eye] +
            EYE_Y_DELTA[expression][eye] *
                expression_weight / 255;
        const uint8_t lid_heat = (uint8_t)ar_mix(
            56,
            LID_HEAT[expression],
            expression_weight);
        ar_draw_crt_eye(
            canvas,
            centers[eye],
            eye_y,
            half_width,
            half_height,
            top_tilt,
            bottom_tilt,
            lid_heat,
            pose->gaze_x / 2,
            pose->gaze_y / 2,
            dim,
            phosphor,
            screen,
            hot);
    }
    const uint8_t embarrassment = expression ==
            FACE_EXPRESSION_EMBARRASSED
        ? expression_weight
        : 0U;
    ar_line(
        canvas,
        centers[0] - 16,
        66,
        centers[0] - 8,
        69,
        3,
        ar_mix_color(screen, phosphor, embarrassment));
    ar_line(
        canvas,
        centers[1] + 8,
        69,
        centers[1] + 16,
        66,
        3,
        ar_mix_color(screen, phosphor, embarrassment));
    ar_draw_crt_mouth(canvas, pose, phosphor, screen, hot);
    /* Three stable console lamps: activity, speech envelope, and attention. */
    const uint8_t lamp_weights[3] = {
        (uint8_t)(pose->source.controls.expression * 255U /
            FACE_ACTIVITY_SPEAKING),
        pose->speech_envelope_q8,
        pose->source.attention,
    };
    for (int lamp = 0; lamp < 3; ++lamp) {
        ar_rect(
            canvas,
            31 + lamp * 13,
            104,
            8,
            3,
            ar_mix_color(dim, hot, lamp_weights[lamp]));
    }
}

static void ar_draw_ring_ticks(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int radius,
    int length,
    uint16_t color)
{
    static const int8_t directions[12][2] = {
        {0, -8}, {4, -7}, {7, -4}, {8, 0}, {7, 4}, {4, 7},
        {0, 8}, {-4, 7}, {-7, 4}, {-8, 0}, {-7, -4}, {-4, -7},
    };
    for (size_t index = 0U; index < 12U; ++index) {
        const int dx = directions[index][0];
        const int dy = directions[index][1];
        ar_line(
            canvas,
            center_x + dx * radius / 8,
            center_y + dy * radius / 8,
            center_x + dx * (radius + length) / 8,
            center_y + dy * (radius + length) / 8,
            2,
            color);
    }
}

/*
 * Expand a circular ring without moving its complete opaque circumference in
 * one integer step.  The base ring is stable; three one-pixel outer shells
 * fade in successively from the known dark interior.  This keeps the topology
 * continuous at raster scale while retaining integer-only voice expansion.
 */
static void ar_draw_expanding_ring(
    ar_canvas_t *canvas,
    int center_x,
    int center_y,
    int base_radius,
    int thickness,
    int expansion_q8,
    uint16_t color,
    uint16_t interior)
{
    enum {
        AR_VOICE_RING_EXPANSION = 3,
    };
    const int inner_radius =
        ar_clamp(base_radius - thickness, 1, base_radius);
    const int maximum_radius =
        base_radius + AR_VOICE_RING_EXPANSION;
    const int left = ar_clamp(
        center_x - maximum_radius,
        AR_SAFE,
        FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int right = ar_clamp(
        center_x + maximum_radius,
        AR_SAFE,
        FACE_ABSTRACT_REDUX_WIDTH - AR_SAFE - 1);
    const int top = ar_clamp(
        center_y - maximum_radius,
        AR_SAFE,
        FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int bottom = ar_clamp(
        center_y + maximum_radius,
        AR_SAFE,
        FACE_ABSTRACT_REDUX_HEIGHT - AR_SAFE - 1);
    const int64_t inner_squared =
        (int64_t)inner_radius * inner_radius;
    const int64_t maximum_squared =
        (int64_t)maximum_radius * maximum_radius;

    for (int y = top; y <= bottom; ++y) {
        const int64_t offset_y = y - center_y;
        for (int x = left; x <= right; ++x) {
            const int64_t offset_x = x - center_x;
            const int64_t distance_squared =
                offset_x * offset_x + offset_y * offset_y;
            if (distance_squared <= inner_squared ||
                distance_squared > maximum_squared) {
                continue;
            }
            int shell = 0;
            while (shell < AR_VOICE_RING_EXPANSION) {
                const int radius = base_radius + shell;
                if (distance_squared <= (int64_t)radius * radius) {
                    break;
                }
                ++shell;
            }
            const int weight = shell == 0
                ? 255
                : ar_clamp(
                    expansion_q8 - (shell - 1) * 256,
                    0,
                    255);
            if (weight > 0) {
                ar_put(
                    canvas,
                    x,
                    y,
                    ar_mix_color(
                        interior,
                        color,
                        (uint8_t)weight));
            }
        }
    }
}

static void ar_render_voice_orbit(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint32_t sample_clock)
{
    ar_gradient(canvas, AR_RGB565(2, 9, 28), AR_RGB565(8, 1, 18));
    const uint16_t blue = AR_RGB565(64, 164, 255);
    const uint16_t cyan = AR_RGB565(72, 255, 231);
    const uint16_t white = AR_RGB565(235, 250, 255);
    const uint16_t violet = AR_RGB565(139, 72, 255);
    const uint16_t dark = AR_RGB565(3, 7, 20);
    const int center_x = pose->face_center_x;
    const int center_y = pose->face_center_y;
    const int orbit = pose->speaking
        ? pose->lean_x / 2
        : ar_wave(sample_clock, 14400U) / 96;
    static const int8_t EXPRESSION_RX_DELTA[AR_EXPRESSION_COUNT] = {
        0, 2, 4, -2, 2, -1, 1, 3, 4, 3, -1,
    };
    static const int8_t EXPRESSION_RY_DELTA[AR_EXPRESSION_COUNT] = {
        0, -1, -3, 2, 2, 0, -1, -2, -4, 3, 1,
    };
    const size_t expression =
        pose->stage_expression < AR_EXPRESSION_COUNT
            ? pose->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const int shape_rx =
        EXPRESSION_RX_DELTA[expression] *
        pose->expression_weight / 255;
    const int shape_ry =
        EXPRESSION_RY_DELTA[expression] *
        pose->expression_weight / 255;
    const int voice_expand_q8 =
        pose->speech_envelope_q8 * 3;
    ar_ellipse(
        canvas,
        center_x,
        center_y,
        47 + orbit + shape_rx,
        47 - orbit + shape_ry,
        violet);
    ar_ellipse(
        canvas,
        center_x,
        center_y,
        42 + orbit + shape_rx,
        42 - orbit + shape_ry,
        dark);
    if (pose->speaking) {
        ar_draw_expanding_ring(
            canvas,
            center_x,
            center_y,
            38,
            2,
            voice_expand_q8,
            ar_mix_color(blue, cyan, pose->speech_envelope_q8),
            dark);
    }
    ar_ellipse_outline(
        canvas, center_x, center_y, 35, 35, 2, blue, dark);
    const int aperture =
        ar_clamp((pose->eye_open[0] + pose->eye_open[1]) / 2, 2, 29);
    ar_draw_eye(
        canvas,
        center_x,
        center_y,
        29,
        aperture,
        pose->gaze_x,
        pose->gaze_y,
        cyan,
        white,
        violet,
        dark);
    ar_draw_brows(
        canvas,
        pose,
        center_x - 15,
        center_x + 15,
        10,
        3,
        cyan);
    if (pose->speaking) {
        const int energy =
            2 + pose->speech_envelope_q8 * 7 / 255;
        ar_draw_ring_ticks(
            canvas,
            center_x,
            center_y,
            43,
            energy,
            cyan);
    }
    if (pose->stage_expression == FACE_EXPRESSION_EMBARRASSED) {
        ar_line(
            canvas, center_x - 39, center_y + 22,
            center_x - 31, center_y + 25, 2, AR_RGB565(255, 86, 170));
        ar_line(
            canvas, center_x + 31, center_y + 25,
            center_x + 39, center_y + 22, 2, AR_RGB565(255, 86, 170));
    }
}

static void ar_render_edge_sentinel(
    ar_canvas_t *canvas,
    const face_abstract_redux_pose_t *pose,
    uint32_t sample_clock)
{
    (void)sample_clock;
    ar_clear(canvas, AR_RGB565(1, 4, 8));
    const uint16_t cyan = AR_RGB565(42, 235, 255);
    const uint16_t amber = AR_RGB565(255, 178, 52);
    const uint16_t steel = AR_RGB565(18, 42, 54);
    const uint16_t ink = AR_RGB565(2, 8, 13);
    const uint16_t white = AR_RGB565(225, 248, 255);
    const int center_x = pose->face_center_x;
    const int center_y = pose->face_center_y;
    ar_triangle(canvas, 18, 116, center_x, 82, 142, 116, steel);
    ar_triangle(
        canvas,
        center_x - 42,
        center_y - 33,
        center_x + 42,
        center_y - 33,
        center_x + 34,
        center_y + 40,
        steel);
    ar_triangle(
        canvas,
        center_x - 42,
        center_y - 33,
        center_x - 34,
        center_y + 40,
        center_x + 34,
        center_y + 40,
        steel);
    ar_line(
        canvas,
        center_x - 42,
        center_y - 33,
        center_x - 34,
        center_y + 40,
        3,
        cyan);
    ar_line(
        canvas,
        center_x + 42,
        center_y - 33,
        center_x + 34,
        center_y + 40,
        3,
        amber);
    ar_line(
        canvas,
        center_x - 34,
        center_y + 40,
        center_x + 34,
        center_y + 40,
        3,
        cyan);
    ar_rect(canvas, center_x - 39, center_y - 15, 78, 34, ink);
    ar_line(
        canvas,
        center_x - 39,
        center_y - 15,
        center_x + 39,
        center_y - 15,
        2,
        amber);
    ar_line(
        canvas,
        center_x - 39,
        center_y + 19,
        center_x + 39,
        center_y + 19,
        2,
        cyan);
    ar_draw_brows(
        canvas,
        pose,
        center_x - 21,
        center_x + 21,
        15,
        3,
        amber);
    ar_draw_eye(
        canvas,
        pose->eye_center_x[0],
        pose->eye_center_y[0],
        15,
        ar_clamp(pose->eye_open[0] * 5 / 8, 2, 15),
        pose->gaze_x,
        pose->gaze_y,
        cyan,
        white,
        amber,
        ink);
    ar_draw_eye(
        canvas,
        pose->eye_center_x[1],
        pose->eye_center_y[1],
        15,
        ar_clamp(pose->eye_open[1] * 5 / 8, 2, 15),
        pose->gaze_x,
        pose->gaze_y,
        amber,
        white,
        cyan,
        ink);
    const int mouth_y = pose->mouth_center_y;
    /*
     * Keep the same corner-parented mouth topology across rest, consonants,
     * and vowels.  ar_draw_cavity_mouth already collapses continuously to a
     * bent line at low aperture; switching renderers here caused two visible
     * pops whenever the temporal track crossed the old height threshold.
     */
    ar_draw_cavity_mouth(
        canvas,
        pose,
        center_x,
        mouth_y,
        amber,
        ink,
        white,
        cyan);
    ar_rect(
        canvas,
        center_x - 24,
        108,
        48,
        3,
        AR_RGB565(10, 32, 40));
    ar_rect(
        canvas,
        center_x - 24,
        108,
        6 + pose->source.attention * 24 / 255 +
            pose->speech_envelope_q8 * 18 / 255,
        3,
        cyan);
}

bool face_abstract_redux_render(
    face_abstract_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!ar_style_valid(style) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_ABSTRACT_REDUX_PIXEL_COUNT) {
        return false;
    }
    face_abstract_redux_pose_t pose;
    if (!face_abstract_redux_resolve(
            style, render_key, sample_clock, &pose)) {
        return false;
    }
    ar_canvas_t canvas = {.pixels = rgb565};
    switch (style) {
    case FACE_ABSTRACT_REDUX_NEON_RIBBON:
        ar_render_neon_ribbon(&canvas, &pose, sample_clock);
        break;
    case FACE_ABSTRACT_REDUX_LIQUID_FAMILIAR:
        ar_render_liquid(&canvas, &pose, sample_clock);
        break;
    case FACE_ABSTRACT_REDUX_CRT_PUPPET:
        ar_render_crt(&canvas, &pose, sample_clock);
        break;
    case FACE_ABSTRACT_REDUX_VOICE_ORBIT:
        ar_render_voice_orbit(&canvas, &pose, sample_clock);
        break;
    case FACE_ABSTRACT_REDUX_EDGE_SENTINEL:
        ar_render_edge_sentinel(&canvas, &pose, sample_clock);
        break;
    default:
        return false;
    }
    return true;
}

bool face_abstract_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_abstract_redux_style_t style;
    return face_abstract_redux_from_legacy_id(
               legacy_profile_id, &style) &&
        face_abstract_redux_render(
            style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
