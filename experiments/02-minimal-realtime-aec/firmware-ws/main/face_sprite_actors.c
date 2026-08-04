#include "face_sprite_actors.h"

#include <limits.h>
#include <string.h>

#include "face_pose.h"
#include "face_stage.h"

#define RGB565(r, g, b)                                                   \
    ((uint16_t)((((uint16_t)(r) & 0xf8U) << 8U) |                         \
                (((uint16_t)(g) & 0xfcU) << 3U) |                         \
                ((uint16_t)(b) >> 3U)))

typedef struct {
    uint16_t *pixels;
    int16_t width;
    int16_t height;
    int16_t scale;
} fsa_canvas_t;

typedef struct {
    const char *slug;
    const char *name;
    uint16_t bg;
    uint16_t deep;
    uint16_t shade;
    uint16_t mid;
    uint16_t light;
    uint16_t ink;
    uint16_t eye;
    uint16_t iris;
    uint16_t accent;
    uint16_t accent2;
    uint16_t mouth;
    uint16_t tongue;
    uint8_t kind;
    uint8_t eye_half_w;
    uint8_t eye_half_h;
    uint8_t pixel_scale;
} fsa_builtin_style_t;

typedef struct {
    uint8_t left_open;
    uint8_t right_open;
    int8_t left_brow_inner;
    int8_t left_brow_outer;
    int8_t right_brow_inner;
    int8_t right_brow_outer;
    int8_t mouth_curve;
    uint8_t mouth_width;
    uint8_t mouth_open;
    uint8_t icon;
} fsa_acting_pose_t;

enum {
    FSA_ICON_NONE = 0,
    FSA_ICON_WARM,
    FSA_ICON_JOY,
    FSA_ICON_SWEAT,
    FSA_ICON_SURPRISE,
    FSA_ICON_THOUGHT,
    FSA_ICON_QUESTION,
    FSA_ICON_FOCUS,
    FSA_ICON_SLEEP,
    FSA_ICON_EXCITED,
    FSA_ICON_BLUSH,
};

/*
 * These poses borrow the durable acting grammar of classic animation
 * (silhouette, opposing brows, eye aperture, mouth corners) while remaining
 * original artwork. Values are deliberately exaggerated at 80x60 so each
 * expression survives 2x nearest-neighbour display.
 */
static const fsa_acting_pose_t ACTING[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        { 208, 208, 0, 0, 0, 0, 0, 12, 1, FSA_ICON_NONE },
    [FACE_EXPRESSION_WARM] =
        { 196, 196, -1, 0, -1, 0, 2, 15, 1, FSA_ICON_WARM },
    [FACE_EXPRESSION_JOY] =
        { 72, 72, -2, 0, -2, 0, 4, 18, 3, FSA_ICON_JOY },
    [FACE_EXPRESSION_CONCERN] =
        { 190, 218, -3, 2, -3, 2, -3, 12, 2, FSA_ICON_SWEAT },
    [FACE_EXPRESSION_SURPRISE] =
        { 255, 255, -4, -4, -4, -4, 0, 9, 7, FSA_ICON_SURPRISE },
    [FACE_EXPRESSION_THOUGHTFUL] =
        { 144, 210, 1, -2, -2, 1, -1, 10, 2, FSA_ICON_THOUGHT },
    [FACE_EXPRESSION_SKEPTICAL] =
        { 92, 214, -3, 2, 1, -2, 1, 14, 1, FSA_ICON_QUESTION },
    [FACE_EXPRESSION_DETERMINED] =
        { 152, 152, 3, -2, 3, -2, -2, 16, 2, FSA_ICON_FOCUS },
    [FACE_EXPRESSION_SLEEPY] =
        { 48, 48, 2, 1, 2, 1, -1, 10, 2, FSA_ICON_SLEEP },
    [FACE_EXPRESSION_EXCITED] =
        { 244, 244, -3, -3, -3, -3, 4, 19, 6, FSA_ICON_EXCITED },
    [FACE_EXPRESSION_EMBARRASSED] =
        { 104, 164, -2, 0, -2, 0, 2, 11, 2, FSA_ICON_BLUSH },
};

/*
 * The EGA/VGA portraits are read at an effective 40x30 contact size. A
 * single logical pixel therefore does more acting work than it does in the
 * creature and helmet profiles. These held eye silhouettes carry the
 * expression without relying on punctuation icons or a mouth-only change.
 */
static const fsa_acting_pose_t PORTRAIT_ACTING[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        { 220, 220, 0, 0, 0, 0, 0, 12, 1, FSA_ICON_NONE },
    [FACE_EXPRESSION_WARM] =
        { 176, 176, -1, 0, -1, 0, 3, 15, 1, FSA_ICON_WARM },
    [FACE_EXPRESSION_JOY] =
        { 0, 0, -2, 1, -2, 1, 5, 18, 3, FSA_ICON_JOY },
    [FACE_EXPRESSION_CONCERN] =
        { 144, 228, -4, 3, -4, 3, -4, 11, 1, FSA_ICON_SWEAT },
    [FACE_EXPRESSION_SURPRISE] =
        { 255, 255, -4, -4, -4, -4, 0, 8, 7, FSA_ICON_SURPRISE },
    [FACE_EXPRESSION_THOUGHTFUL] =
        { 76, 224, 2, -3, -3, 2, -1, 10, 1, FSA_ICON_THOUGHT },
    [FACE_EXPRESSION_SKEPTICAL] =
        { 28, 224, -4, 3, 2, -3, 2, 14, 1, FSA_ICON_QUESTION },
    [FACE_EXPRESSION_DETERMINED] =
        { 104, 104, 4, -3, 4, -3, -3, 15, 1, FSA_ICON_FOCUS },
    [FACE_EXPRESSION_SLEEPY] =
        { 0, 0, 2, 1, 2, 1, -1, 10, 1, FSA_ICON_SLEEP },
    [FACE_EXPRESSION_EXCITED] =
        { 255, 255, -4, -4, -4, -4, 5, 18, 6, FSA_ICON_EXCITED },
    [FACE_EXPRESSION_EMBARRASSED] =
        { 64, 144, -2, 0, -2, 0, 2, 11, 1, FSA_ICON_BLUSH },
};

/*
 * The chrome pilot is read through a dark visor at very small contact sizes.
 * It therefore needs larger aperture changes than the portrait sprites.  The
 * pose remains the same compact IR-facing grammar, but avoids relying on
 * detached punctuation icons to communicate emotion.
 */
static const fsa_acting_pose_t CHROME_ACTING[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        { 220, 220, 0, 0, 0, 0, 0, 13, 1, FSA_ICON_NONE },
    [FACE_EXPRESSION_WARM] =
        { 128, 128, -2, 1, -2, 1, 4, 18, 1, FSA_ICON_NONE },
    [FACE_EXPRESSION_JOY] =
        { 0, 0, -2, 0, -2, 0, 5, 19, 3, FSA_ICON_NONE },
    [FACE_EXPRESSION_CONCERN] =
        { 0, 255, -4, 3, -4, 3, -5, 9, 3, FSA_ICON_NONE },
    [FACE_EXPRESSION_SURPRISE] =
        { 255, 255, -4, -4, -4, -4, 0, 8, 8, FSA_ICON_NONE },
    [FACE_EXPRESSION_THOUGHTFUL] =
        { 72, 224, 2, -3, -3, 2, -1, 10, 2, FSA_ICON_NONE },
    [FACE_EXPRESSION_SKEPTICAL] =
        { 24, 232, -4, 3, 2, -3, 1, 15, 1, FSA_ICON_NONE },
    [FACE_EXPRESSION_DETERMINED] =
        { 112, 112, 4, -3, 4, -3, -4, 17, 2, FSA_ICON_NONE },
    [FACE_EXPRESSION_SLEEPY] =
        { 0, 0, 2, 1, 2, 1, -1, 10, 2, FSA_ICON_NONE },
    [FACE_EXPRESSION_EXCITED] =
        { 255, 255, -3, -3, -3, -3, 5, 20, 7, FSA_ICON_NONE },
    [FACE_EXPRESSION_EMBARRASSED] =
        { 72, 152, -2, 1, -2, 1, 3, 12, 2, FSA_ICON_NONE },
};

static const fsa_builtin_style_t STYLES[FSA_PROFILE_COUNT] = {
    [FSA_PROFILE_EGA_COURT_MAGE] = {
        "sprite-ega-court-mage", "Sprite sheet · EGA Court Mage",
        RGB565(8, 8, 32), RGB565(34, 20, 60), RGB565(84, 42, 92),
        RGB565(170, 86, 112), RGB565(255, 188, 152),
        RGB565(18, 12, 28), RGB565(246, 238, 214),
        RGB565(64, 220, 232), RGB565(224, 56, 180),
        RGB565(248, 204, 64), RGB565(142, 34, 72),
        RGB565(238, 88, 112), 0, 5, 3, 2,
    },
    [FSA_PROFILE_VGA_STAR_CAPTAIN] = {
        "sprite-vga-star-captain", "Sprite sheet · VGA Star Captain",
        RGB565(4, 12, 28), RGB565(24, 38, 68), RGB565(78, 48, 42),
        RGB565(160, 100, 70), RGB565(236, 188, 132),
        RGB565(22, 18, 24), RGB565(244, 238, 218),
        RGB565(70, 178, 184), RGB565(244, 168, 52),
        RGB565(76, 142, 208), RGB565(124, 48, 48),
        RGB565(190, 76, 78), 1, 6, 3, 2,
    },
    [FSA_PROFILE_TALKIE_MOON_MECHANIC] = {
        "sprite-talkie-moon-mechanic", "Sprite sheet · Talkie Moon Mechanic",
        RGB565(16, 14, 34), RGB565(38, 38, 68), RGB565(72, 96, 102),
        RGB565(116, 168, 154), RGB565(190, 224, 180),
        RGB565(12, 20, 28), RGB565(246, 240, 208),
        RGB565(250, 136, 60), RGB565(248, 88, 54),
        RGB565(102, 226, 214), RGB565(82, 44, 58),
        RGB565(230, 90, 104), 2, 6, 3, 2,
    },
    [FSA_PROFILE_JRPG_STORM_FAMILIAR] = {
        "sprite-jrpg-storm-familiar", "Sprite sheet · JRPG Storm Familiar",
        RGB565(28, 30, 72), RGB565(54, 48, 94), RGB565(100, 82, 150),
        RGB565(204, 142, 188), RGB565(255, 218, 210),
        RGB565(32, 20, 54), RGB565(255, 248, 244),
        RGB565(80, 142, 222), RGB565(255, 210, 70),
        RGB565(108, 224, 242), RGB565(126, 48, 94),
        RGB565(246, 112, 146), 3, 7, 4, 2,
    },
    [FSA_PROFILE_HANDHELD_FOREST_PET] = {
        "sprite-handheld-forest-pet", "Sprite sheet · Handheld Forest Pet",
        RGB565(154, 178, 92), RGB565(28, 52, 44),
        RGB565(56, 82, 58), RGB565(96, 124, 72),
        RGB565(190, 204, 116), RGB565(20, 38, 34),
        RGB565(188, 204, 116), RGB565(28, 52, 44),
        RGB565(56, 82, 58), RGB565(124, 146, 82),
        RGB565(28, 52, 44), RGB565(56, 82, 58),
        4, 4, 3, 3,
    },
    [FSA_PROFILE_ARCADE_CHROME_PILOT] = {
        "sprite-arcade-chrome-pilot", "Sprite sheet · Arcade Chrome Pilot",
        RGB565(5, 8, 18), RGB565(20, 28, 48), RGB565(48, 70, 94),
        RGB565(104, 126, 146), RGB565(210, 232, 234),
        RGB565(8, 12, 22), RGB565(220, 244, 250),
        RGB565(64, 224, 248), RGB565(248, 54, 78),
        RGB565(252, 184, 48), RGB565(84, 22, 42),
        RGB565(252, 82, 104), 5, 6, 3, 2,
    },
};

static const fsa_timing_t BUILTIN_TIMING = {
    800, 640, 1600, 2400, 640, 480, 960,
    FSA_TRANSITION_HOLD_CUT, 192, 56000,
};

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static uint8_t expression_for(const face_render_key_t *key);

static int32_t lerp_i32(int32_t from, int32_t to, uint8_t amount)
{
    return from +
        ((to - from) * (int32_t)amount +
         (to >= from ? 127 : -127)) / 255;
}

static void chrome_acting_for_key(
    const face_render_key_t *key,
    fsa_acting_pose_t *result)
{
    uint8_t expression = expression_for(key);
    uint8_t weight = 255U;
    if (key->stage_expression < FACE_EXPRESSION_COUNT &&
        key->expression_weight != 0U) {
        expression = key->stage_expression;
        weight = key->expression_weight;
    }
    const fsa_acting_pose_t *neutral =
        &CHROME_ACTING[FACE_EXPRESSION_NEUTRAL];
    const fsa_acting_pose_t *target = &CHROME_ACTING[expression];
    result->left_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->left_open, target->left_open, weight), 0, 255);
    result->right_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->right_open, target->right_open, weight), 0, 255);
    result->left_brow_inner = (int8_t)lerp_i32(
        neutral->left_brow_inner, target->left_brow_inner, weight);
    result->left_brow_outer = (int8_t)lerp_i32(
        neutral->left_brow_outer, target->left_brow_outer, weight);
    result->right_brow_inner = (int8_t)lerp_i32(
        neutral->right_brow_inner, target->right_brow_inner, weight);
    result->right_brow_outer = (int8_t)lerp_i32(
        neutral->right_brow_outer, target->right_brow_outer, weight);
    result->mouth_curve = (int8_t)lerp_i32(
        neutral->mouth_curve, target->mouth_curve, weight);
    result->mouth_width = (uint8_t)clamp_i32(
        lerp_i32(neutral->mouth_width, target->mouth_width, weight), 0, 255);
    result->mouth_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->mouth_open, target->mouth_open, weight), 0, 255);
    result->icon = FSA_ICON_NONE;
}

static void portrait_acting_for_key(
    const face_render_key_t *key,
    fsa_acting_pose_t *result)
{
    uint8_t expression = expression_for(key);
    uint8_t weight = 255U;
    if (key->stage_expression < FACE_EXPRESSION_COUNT &&
        key->expression_weight != 0U) {
        expression = key->stage_expression;
        weight = key->expression_weight;
    }
    const fsa_acting_pose_t *neutral =
        &PORTRAIT_ACTING[FACE_EXPRESSION_NEUTRAL];
    const fsa_acting_pose_t *target = &PORTRAIT_ACTING[expression];
    result->left_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->left_open, target->left_open, weight), 0, 255);
    result->right_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->right_open, target->right_open, weight), 0, 255);
    result->left_brow_inner = (int8_t)lerp_i32(
        neutral->left_brow_inner, target->left_brow_inner, weight);
    result->left_brow_outer = (int8_t)lerp_i32(
        neutral->left_brow_outer, target->left_brow_outer, weight);
    result->right_brow_inner = (int8_t)lerp_i32(
        neutral->right_brow_inner, target->right_brow_inner, weight);
    result->right_brow_outer = (int8_t)lerp_i32(
        neutral->right_brow_outer, target->right_brow_outer, weight);
    result->mouth_curve = (int8_t)lerp_i32(
        neutral->mouth_curve, target->mouth_curve, weight);
    result->mouth_width = (uint8_t)clamp_i32(
        lerp_i32(neutral->mouth_width, target->mouth_width, weight), 0, 255);
    result->mouth_open = (uint8_t)clamp_i32(
        lerp_i32(neutral->mouth_open, target->mouth_open, weight), 0, 255);
    result->icon = weight >= 96U ? target->icon : FSA_ICON_NONE;
}

static uint8_t max_u8(uint8_t a, uint8_t b)
{
    return a > b ? a : b;
}

static void canvas_clear(fsa_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0; index < FSA_PIXEL_COUNT; ++index) {
        canvas->pixels[index] = color;
    }
}

static void put_px(
    fsa_canvas_t *canvas, int32_t x, int32_t y, uint16_t color)
{
    const int32_t scale = canvas->scale;
    const int32_t x0 = x * scale;
    const int32_t y0 = y * scale;
    for (int32_t yy = 0; yy < scale; ++yy) {
        const int32_t py = y0 + yy;
        if (py < 0 || py >= canvas->height) {
            continue;
        }
        for (int32_t xx = 0; xx < scale; ++xx) {
            const int32_t px = x0 + xx;
            if (px >= 0 && px < canvas->width) {
                canvas->pixels[py * canvas->width + px] = color;
            }
        }
    }
}

static void rect(
    fsa_canvas_t *canvas,
    int32_t x, int32_t y, int32_t width, int32_t height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int32_t yy = 0; yy < height; ++yy) {
        for (int32_t xx = 0; xx < width; ++xx) {
            put_px(canvas, x + xx, y + yy, color);
        }
    }
}

static void line(
    fsa_canvas_t *canvas,
    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t color)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t dy = -dy_abs;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        put_px(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            return;
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

static void disc(
    fsa_canvas_t *canvas,
    int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        put_px(canvas, cx, cy, color);
        return;
    }
    const int32_t limit = rx * rx * ry * ry;
    for (int32_t y = -ry; y <= ry; ++y) {
        for (int32_t x = -rx; x <= rx; ++x) {
            if (x * x * ry * ry + y * y * rx * rx <= limit) {
                put_px(canvas, cx + x, cy + y, color);
            }
        }
    }
}

static void frame_rect(
    fsa_canvas_t *canvas,
    int32_t x, int32_t y, int32_t width, int32_t height,
    uint16_t color)
{
    rect(canvas, x, y, width, 1, color);
    rect(canvas, x, y + height - 1, width, 1, color);
    rect(canvas, x, y, 1, height, color);
    rect(canvas, x + width - 1, y, 1, height, color);
}

static uint32_t hash32(uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

static uint8_t expression_for(const face_render_key_t *key)
{
    if (key->stage_expression < FACE_EXPRESSION_COUNT &&
        key->expression_weight >= 24U) {
        return key->stage_expression;
    }
    if (key->affect_valence > 64 && key->affect_arousal > 180U) {
        return FACE_EXPRESSION_EXCITED;
    }
    if (key->affect_valence > 48) {
        return FACE_EXPRESSION_WARM;
    }
    if (key->affect_valence < -48) {
        return FACE_EXPRESSION_CONCERN;
    }
    return FACE_EXPRESSION_NEUTRAL;
}

static uint8_t ovr_mouth_fallback(const face_render_key_t *key)
{
    if (key->viseme_set == FACE_VISEME_SET_OVR15 &&
        key->viseme < FACE_VISEME_COUNT &&
        key->viseme_weight >= 24U) {
        if (key->viseme_secondary < FACE_VISEME_COUNT &&
            key->viseme_blend >= 128U) {
            return key->viseme_secondary;
        }
        return key->viseme;
    }
    if (key->controls.mouth_press > 144U) {
        return FACE_VISEME_PP;
    }
    if (key->controls.mouth_round > 172U) {
        return key->controls.mouth_open > 144U
            ? FACE_VISEME_O : FACE_VISEME_U;
    }
    if (key->tongue > 100U) {
        return FACE_VISEME_TH;
    }
    if (key->controls.mouth_teeth > 144U) {
        return FACE_VISEME_SS;
    }
    if (key->controls.mouth_open > 165U) {
        return FACE_VISEME_AA;
    }
    if (key->controls.mouth_open > 72U) {
        return FACE_VISEME_DD;
    }
    return FACE_VISEME_SIL;
}

static uint8_t auto_blink_amount(
    const fsa_timing_t *timing, uint32_t sample_clock)
{
    if (timing->auto_blink_period == 0U) {
        return 0U;
    }
    const uint32_t epoch = sample_clock / timing->auto_blink_period;
    const uint32_t in_epoch = sample_clock % timing->auto_blink_period;
    const uint32_t total = (uint32_t)timing->blink_close +
        timing->blink_hold + timing->blink_open;
    if (total == 0U || total + 128U >= timing->auto_blink_period) {
        return 0U;
    }
    const uint32_t start =
        256U + hash32(epoch + 0x5f3759dfU) %
        (timing->auto_blink_period - total - 128U);
    if (in_epoch < start) {
        return 0U;
    }
    uint32_t position = in_epoch - start;
    if (position < timing->blink_close && timing->blink_close != 0U) {
        return (uint8_t)(position * 255U / timing->blink_close);
    }
    position -= timing->blink_close;
    if (position < timing->blink_hold) {
        return 255U;
    }
    position -= timing->blink_hold;
    if (position < timing->blink_open && timing->blink_open != 0U) {
        return (uint8_t)(255U -
            position * 255U / timing->blink_open);
    }
    return 0U;
}

static uint8_t mouth_from_sheet(
    const fsa_sheet_t *sheet, const face_render_key_t *key)
{
    uint8_t set = key->viseme_set;
    uint8_t viseme = key->viseme;
    if (key->viseme_secondary != FACE_VISEME_NONE &&
        key->viseme_blend >= 128U) {
        viseme = key->viseme_secondary;
    }
    if (key->viseme_weight >= 24U) {
        for (uint16_t index = 0; index < sheet->viseme_map_count; ++index) {
            const fsa_viseme_map_t *map = &sheet->viseme_map[index];
            if (map->viseme_set == set && map->viseme == viseme) {
                return map->mouth_frame;
            }
        }
    }
    const uint8_t fallback = ovr_mouth_fallback(key);
    return fallback < FACE_VISEME_COUNT
        ? sheet->fallback_mouth[fallback]
        : 0U;
}

static void resolve_common(
    const fsa_sheet_t *sheet,
    const fsa_timing_t *timing,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fsa_resolved_t *resolved)
{
    memset(resolved, 0, sizeof(*resolved));
    const uint8_t expression = expression_for(key);
    resolved->pose = sheet != NULL
        ? sheet->expression_pose[expression]
        : expression;
    resolved->mouth = sheet != NULL
        ? mouth_from_sheet(sheet, key)
        : ovr_mouth_fallback(key);

    uint8_t blink = auto_blink_amount(timing, sample_clock);
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        blink = 255U;
    }
    const uint8_t left_closed =
        (uint8_t)(255U - key->controls.eye_left_open);
    const uint8_t right_closed =
        (uint8_t)(255U - key->controls.eye_right_open);
    blink = max_u8(blink, max_u8(left_closed, right_closed));
    if (sheet != NULL) {
        const uint8_t count = sheet->poses[resolved->pose].blink_frame_count;
        resolved->blink_frame = count > 1U
            ? (uint8_t)((blink * (count - 1U) + 127U) / 255U)
            : 0U;
    } else {
        resolved->blink_frame = (uint8_t)((blink * 3U + 127U) / 255U);
    }

    int32_t look_x = key->controls.look_x;
    int32_t look_y = key->controls.look_y;
    /* Authored eye acting remains additive to live gaze/tool direction. */
    if (expression == FACE_EXPRESSION_EMBARRASSED) {
        look_x -= 54;
        look_y += 18;
    } else if (expression == FACE_EXPRESSION_THOUGHTFUL) {
        look_x += 38;
        look_y -= 28;
    } else if (expression == FACE_EXPRESSION_SKEPTICAL) {
        look_x -= 34;
    } else if (expression == FACE_EXPRESSION_CONCERN) {
        look_y -= 18;
    }
    if (key->attention < 48U) {
        const uint32_t held =
            timing->pose_hold != 0U
                ? sample_clock / timing->pose_hold
                : sample_clock;
        const uint32_t wander = hash32(held / 20U + 0xa11ceU);
        look_x += (int32_t)((wander & 15U)) - 8;
        look_y += (int32_t)((wander >> 4U) & 7U) - 3;
    }
    resolved->gaze_x = (int8_t)clamp_i32(look_x, -127, 127);
    resolved->gaze_y = (int8_t)clamp_i32(look_y, -127, 127);
    resolved->head_x = (int8_t)clamp_i32(
        ((int32_t)key->head_yaw + key->body_lean_x) / 52, -3, 3);
    resolved->head_y = (int8_t)clamp_i32(
        ((int32_t)key->head_pitch + key->body_lean_y) / 64, -2, 2);
    resolved->roll = (int8_t)clamp_i32(key->head_roll / 48, -2, 2);
    resolved->speaking =
        key->speech_phase == FACE_SPEECH_ACTIVE ||
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U;
    resolved->anticipation =
        key->speech_phase == FACE_SPEECH_STARTING ? 255U : 0U;
    resolved->settle =
        key->speech_phase == FACE_SPEECH_ENDING ? 255U : 0U;
    if (resolved->anticipation != 0U) {
        resolved->blink_frame = 0U;
        resolved->mouth = sheet != NULL
            ? sheet->fallback_mouth[FACE_VISEME_SIL]
            : FACE_VISEME_SIL;
    }
}

size_t fsa_profile_count(void)
{
    return FSA_PROFILE_COUNT;
}

const char *fsa_profile_slug(fsa_profile_t profile)
{
    return (unsigned)profile < FSA_PROFILE_COUNT
        ? STYLES[profile].slug : "invalid-sprite-actor";
}

const char *fsa_profile_name(fsa_profile_t profile)
{
    return (unsigned)profile < FSA_PROFILE_COUNT
        ? STYLES[profile].name : "Invalid sprite actor";
}

bool fsa_profile_info(fsa_profile_t profile, fsa_info_t *info)
{
    if ((unsigned)profile >= FSA_PROFILE_COUNT || info == NULL) {
        return false;
    }
    *info = (fsa_info_t){
        FSA_WIDTH, FSA_HEIGHT, 80, 60,
        (uint16_t)(FSA_PIXEL_COUNT * sizeof(uint16_t)),
        0, 1, (uint8_t)(1U | 8U | 32U), 0, 8,
    };
    return true;
}

static bool cell_valid(const fsa_sheet_t *sheet, uint16_t cell)
{
    return cell == FSA_CELL_NONE || cell < sheet->cell_count;
}

bool fsa_validate_sheet(const fsa_sheet_t *sheet)
{
    if (sheet == NULL || sheet->magic != FSA_SHEET_MAGIC ||
        sheet->version != FSA_SHEET_VERSION ||
        sheet->native_width == 0U || sheet->native_height == 0U ||
        sheet->scale == 0U || sheet->scale > 8U ||
        (uint32_t)sheet->native_width * sheet->scale > FSA_WIDTH ||
        (uint32_t)sheet->native_height * sheet->scale > FSA_HEIGHT ||
        sheet->palette_count == 0U || sheet->palette_count > 256U ||
        sheet->atlas_width == 0U || sheet->atlas_height == 0U ||
        sheet->cell_count == 0U || sheet->pose_count == 0U ||
        sheet->mouth_bank_count == 0U || sheet->mouth_frames == 0U ||
        sheet->mouth_frames > FSA_MAX_VISEMES ||
        sheet->timing.transition_mode > FSA_TRANSITION_HOLD_CUT ||
        sheet->palette_rgb565 == NULL || sheet->atlas_pixels == NULL ||
        sheet->cells == NULL || sheet->poses == NULL ||
        sheet->mouth_cells == NULL || sheet->expression_pose == NULL ||
        sheet->fallback_mouth == NULL || sheet->name == NULL) {
        return false;
    }
    if ((size_t)sheet->atlas_width * sheet->atlas_height > SIZE_MAX / 2U) {
        return false;
    }
    const size_t atlas_pixels =
        (size_t)sheet->atlas_width * sheet->atlas_height;
    for (size_t index = 0; index < atlas_pixels; ++index) {
        if (sheet->atlas_pixels[index] >= sheet->palette_count) {
            return false;
        }
    }
    for (uint16_t index = 0; index < sheet->cell_count; ++index) {
        const fsa_cell_t *cell = &sheet->cells[index];
        if (cell->width == 0U || cell->height == 0U ||
            (uint32_t)cell->x + cell->width > sheet->atlas_width ||
            (uint32_t)cell->y + cell->height > sheet->atlas_height) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < FACE_EXPRESSION_COUNT; ++index) {
        if (sheet->expression_pose[index] >= sheet->pose_count) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < FACE_VISEME_COUNT; ++index) {
        if (sheet->fallback_mouth[index] >= sheet->mouth_frames) {
            return false;
        }
    }
    for (uint16_t index = 0; index < sheet->viseme_map_count; ++index) {
        if (sheet->viseme_map == NULL ||
            sheet->viseme_map[index].mouth_frame >= sheet->mouth_frames) {
            return false;
        }
    }
    const size_t mouth_cell_count =
        (size_t)sheet->mouth_bank_count * sheet->mouth_frames;
    for (size_t index = 0; index < mouth_cell_count; ++index) {
        if (!cell_valid(sheet, sheet->mouth_cells[index])) {
            return false;
        }
    }
    for (uint16_t index = 0; index < sheet->pose_count; ++index) {
        const fsa_pose_t *pose = &sheet->poses[index];
        if (!cell_valid(sheet, pose->base) ||
            !cell_valid(sheet, pose->overlay) ||
            !cell_valid(sheet, pose->brow_left) ||
            !cell_valid(sheet, pose->brow_right) ||
            !cell_valid(sheet, pose->pupil_left) ||
            !cell_valid(sheet, pose->pupil_right) ||
            pose->mouth_bank >= sheet->mouth_bank_count ||
            pose->blink_frame_count == 0U ||
            pose->blink_frame_count > FSA_MAX_BLINK_FRAMES) {
            return false;
        }
        for (uint8_t blink = 0; blink < pose->blink_frame_count; ++blink) {
            if (!cell_valid(sheet, pose->eye_left[blink]) ||
                !cell_valid(sheet, pose->eye_right[blink])) {
                return false;
            }
        }
    }
    return true;
}

bool fsa_resolve(
    const fsa_sheet_t *sheet,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fsa_resolved_t *resolved)
{
    if (!fsa_validate_sheet(sheet) || key == NULL || resolved == NULL) {
        return false;
    }
    resolve_common(sheet, &sheet->timing, key, sample_clock, resolved);
    return true;
}

static void external_blit(
    const fsa_sheet_t *sheet,
    const fsa_cell_t *cell,
    int32_t anchor_x,
    int32_t anchor_y,
    const fsa_resolved_t *resolved,
    uint16_t *pixels)
{
    const int32_t scale = sheet->scale;
    const int32_t center_x =
        (FSA_WIDTH - (int32_t)sheet->native_width * scale) / 2;
    const int32_t center_y =
        (FSA_HEIGHT - (int32_t)sheet->native_height * scale) / 2;
    for (uint16_t sy = 0U; sy < cell->height; ++sy) {
        const uint32_t atlas_row =
            (uint32_t)(cell->y + sy) * sheet->atlas_width + cell->x;
        for (uint16_t sx = 0U; sx < cell->width; ++sx) {
            const uint8_t palette = sheet->atlas_pixels[atlas_row + sx];
            if (palette == sheet->transparent_index) {
                continue;
            }
            int32_t nx = anchor_x + cell->origin_x + sx +
                resolved->head_x;
            const int32_t ny = anchor_y + cell->origin_y + sy +
                resolved->head_y;
            nx += (resolved->roll * (ny -
                (int32_t)sheet->native_height / 2)) / 24;
            const int32_t dx = center_x + nx * scale;
            const int32_t dy = center_y + ny * scale;
            for (int32_t yy = 0; yy < scale; ++yy) {
                const int32_t py = dy + yy;
                if (py < 0 || py >= FSA_HEIGHT) {
                    continue;
                }
                for (int32_t xx = 0; xx < scale; ++xx) {
                    const int32_t px = dx + xx;
                    if (px >= 0 && px < FSA_WIDTH) {
                        pixels[py * FSA_WIDTH + px] =
                            sheet->palette_rgb565[palette];
                    }
                }
            }
        }
    }
}

static void external_cell(
    const fsa_sheet_t *sheet,
    uint16_t cell_index,
    int32_t x,
    int32_t y,
    const fsa_resolved_t *resolved,
    uint16_t *pixels)
{
    if (cell_index != FSA_CELL_NONE) {
        external_blit(
            sheet, &sheet->cells[cell_index], x, y, resolved, pixels);
    }
}

bool fsa_render_sheet_frame(
    const fsa_sheet_t *sheet,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    fsa_resolved_t resolved;
    if (rgb565 == NULL || pixel_capacity < FSA_PIXEL_COUNT ||
        !fsa_resolve(sheet, key, sample_clock, &resolved)) {
        return false;
    }
    for (size_t index = 0; index < FSA_PIXEL_COUNT; ++index) {
        rgb565[index] = sheet->background_rgb565;
    }
    const fsa_pose_t *pose = &sheet->poses[resolved.pose];
    const uint8_t blink = resolved.blink_frame < pose->blink_frame_count
        ? resolved.blink_frame
        : (uint8_t)(pose->blink_frame_count - 1U);
    external_cell(sheet, pose->base, 0, 0, &resolved, rgb565);
    external_cell(
        sheet, pose->brow_left,
        pose->brow_left_x, pose->brow_left_y, &resolved, rgb565);
    external_cell(
        sheet, pose->brow_right,
        pose->brow_right_x, pose->brow_right_y, &resolved, rgb565);
    external_cell(
        sheet, pose->eye_left[blink],
        pose->eye_left_x, pose->eye_left_y, &resolved, rgb565);
    external_cell(
        sheet, pose->eye_right[blink],
        pose->eye_right_x, pose->eye_right_y, &resolved, rgb565);
    if (blink + 1U < pose->blink_frame_count) {
        const int32_t gaze_x = clamp_i32(
            (int32_t)resolved.gaze_x *
                (pose->pupil_max_x - pose->pupil_min_x) / 254,
            pose->pupil_min_x, pose->pupil_max_x);
        const int32_t gaze_y = clamp_i32(
            (int32_t)resolved.gaze_y *
                (pose->pupil_max_y - pose->pupil_min_y) / 254,
            pose->pupil_min_y, pose->pupil_max_y);
        external_cell(
            sheet, pose->pupil_left,
            pose->pupil_left_x + gaze_x,
            pose->pupil_left_y + gaze_y, &resolved, rgb565);
        external_cell(
            sheet, pose->pupil_right,
            pose->pupil_right_x + gaze_x,
            pose->pupil_right_y + gaze_y, &resolved, rgb565);
    }
    const size_t mouth_index =
        (size_t)pose->mouth_bank * sheet->mouth_frames +
        resolved.mouth;
    external_cell(
        sheet, sheet->mouth_cells[mouth_index],
        pose->mouth_x, pose->mouth_y, &resolved, rgb565);
    external_cell(sheet, pose->overlay, 0, 0, &resolved, rgb565);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Built-in original sprite actors                                           */

static void draw_stars(
    fsa_canvas_t *canvas, const fsa_builtin_style_t *style)
{
    static const uint8_t points[][2] = {
        { 6, 7 }, { 14, 17 }, { 21, 5 }, { 68, 8 },
        { 73, 20 }, { 61, 14 }, { 8, 38 }, { 71, 41 },
    };
    for (size_t index = 0; index < sizeof(points) / sizeof(points[0]);
         ++index) {
        put_px(
            canvas, points[index][0], points[index][1],
            (index & 1U) != 0U ? style->accent2 : style->eye);
    }
}

static void draw_backdrop(
    fsa_canvas_t *canvas, const fsa_builtin_style_t *style,
    uint32_t sample_clock)
{
    canvas_clear(canvas, style->bg);
    switch (style->kind) {
    case 0: /* EGA court, stained glass and stone */
        rect(canvas, 5, 4, 70, 52, style->deep);
        frame_rect(canvas, 5, 4, 70, 52, style->accent2);
        frame_rect(canvas, 6, 5, 68, 50, style->accent2);
        for (int32_t y = 8; y < 54; y += 7) {
            line(canvas, 6, y, 74, y, style->shade);
        }
        for (int32_t x = 10; x < 74; x += 12) {
            line(canvas, x, 5, x, 55, style->shade);
        }
        disc(canvas, 40, 17, 10, 10, style->accent);
        disc(canvas, 40, 17, 7, 7, style->deep);
        line(canvas, 40, 8, 40, 26, style->accent2);
        line(canvas, 31, 17, 49, 17, style->accent2);
        break;
    case 1: /* VGA navigation window */
        disc(canvas, 40, 26, 35, 24, style->deep);
        disc(canvas, 40, 24, 32, 21, style->shade);
        disc(canvas, 40, 23, 29, 18, style->bg);
        draw_stars(canvas, style);
        line(canvas, 7, 37, 24, 31, style->accent2);
        line(canvas, 73, 37, 56, 31, style->accent2);
        line(canvas, 10, 42, 28, 34, style->shade);
        line(canvas, 70, 42, 52, 34, style->shade);
        break;
    case 2: /* Talkie repair bay */
        rect(canvas, 2, 4, 76, 52, style->deep);
        frame_rect(canvas, 2, 4, 76, 52, style->accent2);
        for (int32_t x = 6; x < 76; x += 9) {
            line(canvas, x, 5, x - 8, 55, style->shade);
        }
        rect(canvas, 5, 8, 14, 8, style->ink);
        frame_rect(canvas, 5, 8, 14, 8, style->accent);
        line(canvas, 8, 12, 11, 9, style->accent2);
        line(canvas, 11, 9, 15, 14, style->accent2);
        put_px(
            canvas, 70, 8,
            ((sample_clock / 3200U) & 1U) != 0U
                ? style->accent : style->accent2);
        break;
    case 3: /* JRPG storm sky */
        for (int32_t y = 0; y < 60; y += 4) {
            rect(canvas, 0, y, 80, 4,
                 ((y / 4) & 1) != 0 ? style->deep : style->bg);
        }
        disc(canvas, 12, 11, 8, 4, style->shade);
        disc(canvas, 67, 16, 11, 5, style->shade);
        line(canvas, 68, 18, 64, 24, style->accent2);
        line(canvas, 64, 24, 68, 23, style->accent2);
        line(canvas, 68, 23, 65, 29, style->accent2);
        for (int32_t x = 7; x < 78; x += 14) {
            put_px(canvas, x, 52 + ((x / 7) & 2), style->accent);
        }
        break;
    case 4: /* 2-bit handheld LCD */
        rect(canvas, 1, 1, 78, 58, style->deep);
        rect(canvas, 3, 3, 74, 54, style->bg);
        frame_rect(canvas, 3, 3, 74, 54, style->mid);
        for (int32_t y = 5; y < 56; y += 2) {
            for (int32_t x = 5 + (y & 2); x < 76; x += 4) {
                put_px(canvas, x, y, style->light);
            }
        }
        rect(canvas, 6, 49, 68, 5, style->mid);
        for (int32_t x = 8; x < 73; x += 7) {
            rect(canvas, x, 47 - ((x / 7) & 1), 4, 3, style->shade);
        }
        break;
    default: /* 16-bit arcade HUD */
        rect(canvas, 1, 2, 78, 55, style->deep);
        frame_rect(canvas, 1, 2, 78, 55, style->shade);
        line(canvas, 4, 53, 20, 7, style->shade);
        line(canvas, 76, 53, 60, 7, style->shade);
        frame_rect(canvas, 5, 6, 12, 8, style->accent);
        rect(canvas, 7, 8, 7, 2, style->accent2);
        rect(canvas, 64, 7, 10, 2, style->accent);
        rect(canvas, 67, 11, 7, 2, style->accent2);
        for (int32_t x = 7; x <= 73; x += 11) {
            put_px(canvas, x, 55, style->accent2);
        }
        break;
    }
}

static void draw_shoulders_and_head(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    int32_t ox,
    int32_t oy)
{
    switch (style->kind) {
    case 0: /* angular hood and jewelled clasp */
        disc(canvas, 40 + ox, 55 + oy, 31, 9, style->shade);
        line(canvas, 11 + ox, 58 + oy, 25 + ox, 43 + oy, style->accent);
        line(canvas, 69 + ox, 58 + oy, 55 + ox, 43 + oy, style->accent);
        disc(canvas, 40 + ox, 31 + oy, 23, 26, style->deep);
        line(canvas, 17 + ox, 31 + oy, 27 + ox, 7 + oy, style->accent);
        line(canvas, 63 + ox, 31 + oy, 53 + ox, 7 + oy, style->accent);
        disc(canvas, 40 + ox, 29 + oy, 17, 22, style->mid);
        rect(canvas, 29 + ox, 12 + oy, 22, 3, style->light);
        rect(canvas, 35 + ox, 52 + oy, 10, 4, style->accent);
        put_px(canvas, 40 + ox, 53 + oy, style->accent2);
        break;
    case 1: /* banded VGA human portrait */
        disc(canvas, 40 + ox, 55 + oy, 32, 9, style->deep);
        disc(canvas, 40 + ox, 53 + oy, 27, 7, style->accent2);
        disc(canvas, 40 + ox, 28 + oy, 23, 25, style->ink);
        disc(canvas, 40 + ox, 29 + oy, 20, 23, style->shade);
        disc(canvas, 38 + ox, 27 + oy, 18, 21, style->mid);
        disc(canvas, 35 + ox, 22 + oy, 12, 14, style->light);
        for (int32_t index = 0; index < 6; ++index) {
            line(
                canvas, 23 + index + ox, 9 + (index & 1) + oy,
                18 + index / 2 + ox, 27 + index + oy,
                index < 3 ? style->shade : style->deep);
        }
        rect(canvas, 58 + ox, 23 + oy, 4, 11, style->deep);
        rect(canvas, 59 + ox, 25 + oy, 2, 6, style->iris);
        break;
    case 2: /* broad moon mechanic with cap and headset */
        disc(canvas, 40 + ox, 54 + oy, 34, 8, style->deep);
        rect(canvas, 16 + ox, 47 + oy, 48, 10, style->shade);
        disc(canvas, 40 + ox, 29 + oy, 24, 24, style->mid);
        disc(canvas, 37 + ox, 26 + oy, 20, 20, style->light);
        rect(canvas, 16 + ox, 9 + oy, 48, 7, style->deep);
        rect(canvas, 23 + ox, 6 + oy, 34, 5, style->accent2);
        rect(canvas, 57 + ox, 20 + oy, 7, 16, style->deep);
        frame_rect(canvas, 59 + ox, 23 + oy, 4, 8, style->accent);
        line(canvas, 62 + ox, 31 + oy, 57 + ox, 39 + oy, style->accent);
        break;
    case 3: /* anime familiar: ears, fluffy silhouette, cheek panels */
        disc(canvas, 40 + ox, 54 + oy, 28, 8, style->shade);
        line(canvas, 17 + ox, 24 + oy, 24 + ox, 5 + oy, style->ink);
        line(canvas, 24 + ox, 5 + oy, 34 + ox, 16 + oy, style->ink);
        line(canvas, 63 + ox, 24 + oy, 56 + ox, 5 + oy, style->ink);
        line(canvas, 56 + ox, 5 + oy, 46 + ox, 16 + oy, style->ink);
        disc(canvas, 40 + ox, 30 + oy, 24, 25, style->shade);
        disc(canvas, 40 + ox, 31 + oy, 21, 22, style->light);
        line(canvas, 20 + ox, 31 + oy, 15 + ox, 39 + oy, style->shade);
        line(canvas, 60 + ox, 31 + oy, 65 + ox, 39 + oy, style->shade);
        rect(canvas, 24 + ox, 12 + oy, 6, 4, style->accent);
        rect(canvas, 50 + ox, 12 + oy, 6, 4, style->accent);
        break;
    case 4: /* tiny LCD woodland pet */
        rect(canvas, 18 + ox, 48 + oy, 44, 8, style->shade);
        line(canvas, 21 + ox, 22 + oy, 27 + ox, 9 + oy, style->ink);
        line(canvas, 27 + ox, 9 + oy, 33 + ox, 18 + oy, style->ink);
        line(canvas, 59 + ox, 22 + oy, 53 + ox, 9 + oy, style->ink);
        line(canvas, 53 + ox, 9 + oy, 47 + ox, 18 + oy, style->ink);
        disc(canvas, 40 + ox, 32 + oy, 22, 21, style->shade);
        disc(canvas, 40 + ox, 31 + oy, 19, 18, style->light);
        for (int32_t y = 17; y < 45; y += 3) {
            put_px(canvas, 22 + ((y / 3) & 1) + ox, y + oy, style->mid);
            put_px(canvas, 58 - ((y / 3) & 1) + ox, y + oy, style->mid);
        }
        break;
    default: /* arcade chrome helmet */
        disc(canvas, 40 + ox, 55 + oy, 35, 8, style->deep);
        rect(canvas, 8 + ox, 51 + oy, 64, 7, style->shade);
        disc(canvas, 40 + ox, 29 + oy, 27, 26, style->shade);
        disc(canvas, 40 + ox, 28 + oy, 23, 22, style->mid);
        rect(canvas, 18 + ox, 11 + oy, 44, 8, style->deep);
        frame_rect(canvas, 18 + ox, 11 + oy, 44, 8, style->accent);
        rect(canvas, 22 + ox, 18 + oy, 36, 28, style->ink);
        frame_rect(canvas, 22 + ox, 18 + oy, 36, 28, style->light);
        rect(canvas, 14 + ox, 24 + oy, 7, 17, style->deep);
        rect(canvas, 59 + ox, 24 + oy, 7, 17, style->deep);
        put_px(canvas, 17 + ox, 28 + oy, style->accent2);
        put_px(canvas, 63 + ox, 28 + oy, style->accent2);
        break;
    }
}

static void draw_nose(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    int32_t ox,
    int32_t oy)
{
    if (style->kind == 5) {
        rect(canvas, 39 + ox, 32 + oy, 3, 3, style->shade);
        put_px(canvas, 40 + ox, 32 + oy, style->accent);
    } else if (style->kind == 4) {
        rect(canvas, 39 + ox, 35 + oy, 3, 2, style->ink);
    } else if (style->kind == 3) {
        put_px(canvas, 40 + ox, 35 + oy, style->mouth);
        put_px(canvas, 41 + ox, 35 + oy, style->mid);
    } else {
        line(canvas, 40 + ox, 27 + oy, 38 + ox, 35 + oy, style->shade);
        line(canvas, 38 + ox, 35 + oy, 42 + ox, 36 + oy, style->deep);
        put_px(canvas, 40 + ox, 34 + oy, style->light);
    }
}

static void draw_brow(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    bool right,
    int32_t inner_delta,
    int32_t outer_delta,
    int32_t ox,
    int32_t oy)
{
    const int32_t inner_x = right ? 44 : 36;
    const int32_t outer_x = right ? 57 : 23;
    int32_t inner_y = 19 + inner_delta;
    int32_t outer_y = 19 + outer_delta;
    if (style->kind == 3) {
        inner_y -= 2;
        outer_y -= 2;
    } else if (style->kind == 5) {
        inner_y = 20 + inner_delta;
        outer_y = 20 + outer_delta;
    }
    line(
        canvas, inner_x + ox, inner_y + oy,
        outer_x + ox, outer_y + oy, style->ink);
    line(
        canvas, inner_x + ox, inner_y + 1 + oy,
        outer_x + ox, outer_y + 1 + oy,
        style->kind == 5 ? style->accent : style->deep);
}

static uint8_t eye_open_amount(
    const face_render_key_t *key,
    const fsa_acting_pose_t *acting,
    bool right,
    const fsa_resolved_t *resolved)
{
    uint32_t amount = right ? acting->right_open : acting->left_open;
    const uint8_t control =
        right ? key->controls.eye_right_open : key->controls.eye_left_open;
    amount = (amount * control + 127U) / 255U;
    const uint8_t squint =
        right ? key->eye_right_squint : key->eye_left_squint;
    amount = amount * (255U - squint / 2U) / 255U;
    static const uint8_t blink_factor[4] = { 255, 160, 56, 0 };
    amount = amount * blink_factor[
        resolved->blink_frame < 4U ? resolved->blink_frame : 3U] / 255U;
    if (resolved->anticipation != 0U) {
        amount = amount + (255U - amount) *
            BUILTIN_TIMING.speech_eye_boost / 255U;
    }
    return (uint8_t)amount;
}

static void draw_eye(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    const face_render_key_t *key,
    const fsa_acting_pose_t *acting,
    const fsa_resolved_t *resolved,
    bool right,
    int32_t ox,
    int32_t oy)
{
    int32_t cx = right ? 51 : 29;
    int32_t cy = 27;
    int32_t half_w = style->eye_half_w;
    int32_t half_h = style->eye_half_h;
    if (style->kind == 0 || style->kind == 1) {
        half_h = 4;
    } else if (style->kind == 3) {
        cx = right ? 52 : 28;
        cy = 27;
    } else if (style->kind == 4) {
        cx = right ? 51 : 29;
        cy = 28;
    } else if (style->kind == 5) {
        cx = right ? 50 : 30;
        cy = 27;
        half_w = 7;
    }
    const uint8_t amount =
        eye_open_amount(key, acting, right, resolved);
    const int32_t visible_h = (half_h * amount + 127) / 255;

    /* Stable socket mass. Emotion changes the aperture inside, never socket. */
    if (style->kind == 5) {
        rect(
            canvas, cx - half_w - 1 + ox, cy - half_h - 1 + oy,
            half_w * 2 + 3, half_h * 2 + 3, style->shade);
        frame_rect(
            canvas, cx - half_w - 1 + ox, cy - half_h - 1 + oy,
            half_w * 2 + 3, half_h * 2 + 3, style->light);
    } else if (style->kind == 4) {
        rect(
            canvas, cx - half_w - 1 + ox, cy - half_h - 1 + oy,
            half_w * 2 + 3, half_h * 2 + 3, style->mid);
    } else {
        disc(canvas, cx + ox, cy + oy, half_w + 1, half_h + 1, style->deep);
    }

    if (visible_h <= 0) {
        const int32_t arch = acting->mouth_curve > 1 ? -1 : 0;
        line(
            canvas, cx - half_w + ox, cy + oy,
            cx + ox, cy + arch + oy, style->ink);
        line(
            canvas, cx + ox, cy + arch + oy,
            cx + half_w + ox, cy + oy, style->ink);
        return;
    }
    if (style->kind == 5) {
        int32_t aperture_half_w = half_w;
        if (acting->mouth_width <= 10U) {
            aperture_half_w -= 1;
        }
        const int32_t inner_delta = right
            ? acting->right_brow_inner : acting->left_brow_inner;
        const int32_t outer_delta = right
            ? acting->right_brow_outer : acting->left_brow_outer;
        const int32_t left_delta = right ? inner_delta : outer_delta;
        const int32_t right_delta = right ? outer_delta : inner_delta;
        const int32_t span = aperture_half_w * 2;
        for (int32_t dx = -aperture_half_w;
             dx <= aperture_half_w; ++dx) {
            const int32_t along = dx + aperture_half_w;
            const int32_t lid_delta =
                (left_delta * (span - along) +
                 right_delta * along) / span;
            const int32_t top = clamp_i32(
                cy - visible_h + lid_delta / 2,
                cy - half_h, cy + visible_h);
            for (int32_t y = top; y <= cy + visible_h; ++y) {
                put_px(canvas, cx + dx + ox, y + oy, style->eye);
            }
        }
    } else if (style->kind == 4) {
        rect(
            canvas, cx - half_w + ox, cy - visible_h + oy,
            half_w * 2 + 1, visible_h * 2 + 1, style->eye);
    } else if (style->kind == 0 || style->kind == 1) {
        /*
         * Portrait lids share the brow slope so concern, thought and
         * determination change the eye silhouette itself. Column-wise
         * filling keeps the aperture contiguous at 40x30.
         */
        const int32_t inner_delta = right
            ? acting->right_brow_inner : acting->left_brow_inner;
        const int32_t outer_delta = right
            ? acting->right_brow_outer : acting->left_brow_outer;
        const int32_t left_delta = right ? inner_delta : outer_delta;
        const int32_t right_delta = right ? outer_delta : inner_delta;
        const int32_t span = half_w * 2;
        for (int32_t dx = -half_w; dx <= half_w; ++dx) {
            const int32_t along = dx + half_w;
            const int32_t lid_delta =
                (left_delta * (span - along) +
                 right_delta * along) / span;
            const int32_t edge_trim =
                (dx == -half_w || dx == half_w)
                    ? (visible_h + 1) / 2
                    : 0;
            const int32_t extent =
                clamp_i32(visible_h - edge_trim, 1, visible_h);
            const int32_t top = clamp_i32(
                cy - extent + lid_delta / 2,
                cy - half_h, cy + extent);
            const int32_t bottom = cy + extent;
            if (top <= bottom) {
                line(
                    canvas, cx + dx + ox, top + oy,
                    cx + dx + ox, bottom + oy, style->eye);
            }
        }
    } else {
        disc(
            canvas, cx + ox, cy + oy,
            half_w, visible_h, style->eye);
    }

    const bool portrait = style->kind == 0 || style->kind == 1;
    int32_t gaze_x = resolved->gaze_x / (portrait ? 38 : 48);
    int32_t gaze_y = resolved->gaze_y / 72;
    gaze_x = clamp_i32(
        gaze_x, portrait ? -3 : -2, portrait ? 3 : 2);
    gaze_y = clamp_i32(gaze_y, -1, 1);
    int32_t pupil_rx = style->kind == 3 ? 2 : 1;
    int32_t pupil_ry = visible_h >= 3 ? 2 : 1;
    if (style->kind == 5) {
        const int32_t pupil_width =
            acting->mouth_open >= 7U ? 3 : 5;
        const int32_t pupil_height = visible_h >= 2 ? 3 : 1;
        if (visible_h < 2) {
            gaze_y = 0;
        }
        rect(
            canvas, cx - pupil_width / 2 + gaze_x + ox,
            cy - pupil_height / 2 + gaze_y + oy,
            pupil_width, pupil_height, style->iris);
        rect(
            canvas, cx + gaze_x + ox,
            cy - pupil_height / 2 + gaze_y + oy,
            pupil_width >= 5 ? 2 : 1, pupil_height, style->ink);
    } else if (portrait) {
        if (visible_h == 1) {
            gaze_y = 0;
            rect(
                canvas, cx - 1 + gaze_x + ox, cy + oy,
                3, 1, style->iris);
            put_px(canvas, cx + gaze_x + ox, cy + oy, style->ink);
        } else {
            const int32_t iris_ry = visible_h >= 3 ? 2 : 1;
            gaze_y = clamp_i32(gaze_y, 1 - visible_h, visible_h - 1);
            disc(
                canvas, cx + gaze_x + ox, cy + gaze_y + oy,
                2, iris_ry, style->iris);
            disc(
                canvas, cx + gaze_x + ox, cy + gaze_y + oy,
                1, 1, style->ink);
            if (visible_h >= 3) {
                put_px(
                    canvas, cx - 1 + gaze_x + ox,
                    cy - 1 + gaze_y + oy, style->light);
            }
        }
    } else {
        disc(
            canvas, cx + gaze_x + ox, cy + gaze_y + oy,
            pupil_rx + 1, pupil_ry, style->iris);
        disc(
            canvas, cx + gaze_x + ox, cy + gaze_y + oy,
            pupil_rx, pupil_ry, style->ink);
        if (visible_h >= 2) {
            put_px(
                canvas, cx - 1 + gaze_x + ox,
                cy - 1 + gaze_y + oy, style->light);
        }
    }
}

typedef struct {
    uint8_t width;
    uint8_t open;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
} fsa_mouth_sprite_t;

static const fsa_mouth_sprite_t MOUTH_SPRITES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = { 18, 7, 16, 0, 20, 24 },
    [FACE_VISEME_E] = { 19, 4, 0, 0, 178, 0 },
    [FACE_VISEME_I] = { 13, 3, 18, 0, 126, 0 },
    [FACE_VISEME_O] = { 10, 7, 230, 0, 0, 0 },
    [FACE_VISEME_U] = { 8, 4, 255, 0, 0, 0 },
    [FACE_VISEME_PP] = { 13, 1, 40, 255, 0, 0 },
    [FACE_VISEME_SS] = { 18, 3, 0, 20, 255, 0 },
    [FACE_VISEME_TH] = { 15, 4, 18, 0, 146, 255 },
    [FACE_VISEME_DD] = { 14, 4, 32, 0, 80, 0 },
    [FACE_VISEME_FF] = { 15, 3, 12, 100, 230, 0 },
    [FACE_VISEME_KK] = { 13, 5, 42, 0, 40, 0 },
    [FACE_VISEME_NN] = { 14, 2, 18, 210, 0, 0 },
    [FACE_VISEME_RR] = { 10, 5, 210, 0, 0, 0 },
    [FACE_VISEME_CH] = { 11, 4, 176, 0, 170, 0 },
    [FACE_VISEME_SIL] = { 12, 1, 0, 110, 0, 0 },
};

static void mouth_outline_curve(
    fsa_canvas_t *canvas,
    int32_t cx,
    int32_t cy,
    int32_t half_w,
    int32_t corner_left,
    int32_t corner_right,
    int32_t center_curve,
    uint16_t color)
{
    line(
        canvas, cx - half_w, cy + corner_left,
        cx, cy + center_curve, color);
    line(
        canvas, cx, cy + center_curve,
        cx + half_w, cy + corner_right, color);
}

static void draw_mouth(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    const face_render_key_t *key,
    const fsa_acting_pose_t *acting,
    const fsa_resolved_t *resolved,
    int32_t ox,
    int32_t oy)
{
    const bool portrait = style->kind == 0 || style->kind == 1;
    uint8_t sprite_index = resolved->mouth;
    if (sprite_index >= FACE_VISEME_COUNT) {
        sprite_index = FACE_VISEME_SIL;
    }
    fsa_mouth_sprite_t sprite = MOUTH_SPRITES[sprite_index];
    const bool speech_shape =
        resolved->speaking != 0U ||
        key->speech_phase == FACE_SPEECH_STARTING ||
        key->speech_phase == FACE_SPEECH_ENDING ||
        key->viseme_weight >= 24U;
    if (!speech_shape || sprite_index == FACE_VISEME_SIL) {
        sprite.width = acting->mouth_width;
        sprite.open = acting->mouth_open;
        if (acting->mouth_open <= 2U) {
            sprite.press = 130U;
        }
    }
    if (style->kind == 5 && acting->mouth_open >= 7U &&
        acting->mouth_curve >= -1 && acting->mouth_curve <= 1) {
        /* Surprise must remain a clear O even over a transient viseme. */
        sprite.round = 255U;
        sprite.press = 0U;
        sprite.teeth = 0U;
        sprite.tongue = 0U;
    }

    int32_t width = sprite.width;
    int32_t open = sprite.open;
    /*
     * Articulation owns the phonetic silhouette, but it must not erase
     * acting.  Blend a bounded part of the stage-directed jaw/width delta
     * into active visemes for the two portrait sprites.  This preserves
     * PP/FF/O/etc. while making joy, concern, surprise, sleepy, and excited
     * visibly different during continuous speech.
     */
    if (style->kind == 0 || style->kind == 1 || style->kind == 5) {
        const fsa_acting_pose_t *neutral =
            style->kind == 5
                ? &CHROME_ACTING[FACE_EXPRESSION_NEUTRAL]
                : &PORTRAIT_ACTING[FACE_EXPRESSION_NEUTRAL];
        const int32_t width_delta =
            (int32_t)acting->mouth_width - neutral->mouth_width;
        const int32_t open_delta =
            (int32_t)acting->mouth_open - neutral->mouth_open;
        const int32_t blend_divisor = style->kind == 5 ? 3 : 2;
        width += width_delta >= 0
            ? (width_delta + blend_divisor - 1) / blend_divisor
            : -((-width_delta + blend_divisor - 1) / blend_divisor);
        open += open_delta >= 0
            ? (open_delta + blend_divisor - 1) / blend_divisor
            : -((-open_delta + blend_divisor - 1) / blend_divisor);
    }
    if (key->controls.mouth_width > 0U) {
        width += ((int32_t)key->controls.mouth_width - 128) / 32;
    }
    if (resolved->speaking != 0U) {
        const int32_t audio_divisor =
            portrait ? 128 : style->kind == 5 ? 144 : 72;
        const int32_t control_divisor =
            portrait ? 192 : style->kind == 5 ? 192 : 96;
        open += (int32_t)key->audio_level / audio_divisor;
        open += (int32_t)key->controls.mouth_open /
            control_divisor;
    }
    width = clamp_i32(
        width,
        portrait ? 6 : 7,
        portrait ? 18 : style->kind == 5 ? 18 : 20);
    const int32_t maximum_open =
        style->kind == 5
            ? (acting->mouth_open >= 8U
                   ? 9
                   : acting->mouth_open >= 7U ? 7 : 6)
            : portrait ? 7 : 9;
    open = clamp_i32(
        open, 1, maximum_open);
    const int32_t cx = 40 + ox;
    const int32_t cy =
        (style->kind == 3 ? 43
         : style->kind == 5 ? 40
         : portrait ? 41
         : 42) + oy;
    const int32_t half_w = width / 2;
    int32_t corner_left =
        -acting->mouth_curve / 2 - key->mouth_corner_left / 48;
    int32_t corner_right =
        -acting->mouth_curve / 2 - key->mouth_corner_right / 48;
    corner_left = clamp_i32(corner_left, -3, 3);
    corner_right = clamp_i32(corner_right, -3, 3);

    if (sprite.press > 180U || open <= 1) {
        mouth_outline_curve(
            canvas, cx, cy, half_w,
            corner_left, corner_right,
            acting->mouth_curve > 1 ? 1 : acting->mouth_curve < -1 ? -1 : 0,
            style->mouth);
        /* Two-pixel terminators anchor corners and prevent detached shards. */
        rect(canvas, cx - half_w, cy + corner_left, 2, 2, style->mouth);
        rect(canvas, cx + half_w - 1, cy + corner_right, 2, 2, style->mouth);
        if (sprite_index == FACE_VISEME_FF) {
            rect(canvas, cx - half_w / 2, cy - 1, half_w, 1, style->eye);
        }
        return;
    }

    int32_t cavity_half_w = half_w;
    if (sprite.round > 160U) {
        cavity_half_w = clamp_i32(
            half_w - (int32_t)(sprite.round - 160U) / 36, 3, half_w);
    }
    if (style->kind == 5 && sprite.round > 160U) {
        disc(
            canvas, cx, cy + open / 2,
            cavity_half_w + 1, open / 2 + 2, style->mouth);
        disc(
            canvas, cx, cy + open / 2,
            cavity_half_w, open / 2 + 1, style->ink);
    } else if (style->kind == 4 || style->kind == 5) {
        rect(
            canvas, cx - cavity_half_w, cy - 1,
            cavity_half_w * 2 + 1, open + 2, style->mouth);
        rect(
            canvas, cx - cavity_half_w + 1, cy,
            cavity_half_w * 2 - 1, open, style->ink);
    } else {
        disc(
            canvas, cx, cy + open / 2,
            cavity_half_w + 1, open / 2 + 2, style->mouth);
        disc(
            canvas, cx, cy + open / 2,
            cavity_half_w, open / 2 + 1, style->ink);
    }

    const bool show_teeth = portrait
        ? (sprite_index == FACE_VISEME_E ||
           sprite_index == FACE_VISEME_SS ||
           sprite_index == FACE_VISEME_TH ||
           sprite_index == FACE_VISEME_FF ||
           sprite_index == FACE_VISEME_CH ||
           key->controls.mouth_teeth > 180U)
        : (sprite.teeth > 72U || key->controls.mouth_teeth > 112U);
    if (show_teeth) {
        const int32_t teeth_w =
            cavity_half_w * 2 - (sprite.round > 160U ? 2 : 0);
        rect(
            canvas, cx - teeth_w / 2, cy,
            teeth_w, open >= 6 ? 2 : 1, style->eye);
        if (!portrait && style->kind != 4 &&
            style->kind != 5 && teeth_w >= 10) {
            for (int32_t x = cx - teeth_w / 2 + 3;
                 x < cx + teeth_w / 2; x += 4) {
                put_px(canvas, x, cy + 1, style->shade);
            }
        }
    }
    if (sprite_index == FACE_VISEME_SS) {
        /* Sibilant: a narrow central air channel in the teeth bank. */
        rect(canvas, cx, cy, 1, open >= 6 ? 2 : 1, style->ink);
        put_px(canvas, cx - half_w + 1, cy - 1, style->eye);
        put_px(canvas, cx + half_w - 1, cy - 1, style->eye);
    } else if (sprite_index == FACE_VISEME_FF) {
        /* Labiodental contact: lower lip explicitly meets upper teeth. */
        line(
            canvas, cx - cavity_half_w + 2, cy + 2,
            cx + cavity_half_w - 2, cy + 2, style->tongue);
    } else if (sprite_index == FACE_VISEME_NN) {
        put_px(canvas, cx, cy + 1, style->mouth);
    } else if (sprite_index == FACE_VISEME_KK) {
        put_px(canvas, cx, cy + open, style->accent2);
    } else if (sprite_index == FACE_VISEME_RR) {
        put_px(canvas, cx - 1, cy + open, style->tongue);
        put_px(canvas, cx + 1, cy + open, style->tongue);
    }
    if (sprite.tongue > 80U || key->tongue > 96U) {
        disc(
            canvas, cx + (sprite_index == FACE_VISEME_TH ? 1 : 0),
            cy + open, cavity_half_w - 2, 2, style->tongue);
        line(
            canvas, cx - cavity_half_w + 2, cy + open,
            cx + cavity_half_w - 2, cy + open, style->mouth);
    }

    /* Emotional corners remain attached to the speech cavity. */
    line(
        canvas, cx - half_w, cy + corner_left,
        cx - cavity_half_w, cy + 1, style->mouth);
    line(
        canvas, cx + cavity_half_w, cy + 1,
        cx + half_w, cy + corner_right, style->mouth);
}

static void draw_chrome_expression_details(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    const fsa_acting_pose_t *acting,
    const face_render_key_t *key,
    int32_t ox,
    int32_t oy)
{
    int32_t warmth = clamp_i32(acting->mouth_curve, 0, 5);
    if (key->cheek > 144U) {
        warmth = clamp_i32(warmth + 1, 0, 5);
    }
    if (acting->mouth_curve <= -5) {
        /*
         * Concern gets attached stress creases inside the visor.  Chrome
         * has no free-standing brows or punctuation icons, so this small
         * silhouette cue prevents the asymmetrical eye from doing all the
         * emotional work by itself.
         */
        line(
            canvas, 23 + ox, 34 + oy,
            28 + ox, 37 + oy, style->accent);
        line(
            canvas, 57 + ox, 34 + oy,
            52 + ox, 37 + oy, style->accent);
    }
    if (warmth > 0) {
        const int32_t width = 2 + (warmth + 1) / 2;
        rect(canvas, 24 + ox, 35 + oy, width, 2, style->tongue);
        rect(canvas, 57 - width + ox, 35 + oy, width, 2, style->tongue);
    }
}

static void sparkle(
    fsa_canvas_t *canvas,
    int32_t x,
    int32_t y,
    uint16_t a,
    uint16_t b)
{
    line(canvas, x - 2, y, x + 2, y, a);
    line(canvas, x, y - 2, x, y + 2, b);
    put_px(canvas, x, y, b);
}

static void draw_expression_icon(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    const fsa_acting_pose_t *acting,
    const face_render_key_t *key,
    uint32_t sample_clock,
    int32_t ox,
    int32_t oy)
{
    const int32_t phase =
        (int32_t)((sample_clock / BUILTIN_TIMING.pose_hold) & 1U);
    switch (acting->icon) {
    case FSA_ICON_WARM:
        rect(canvas, 20 + ox, 36 + oy, 4, 2, style->tongue);
        rect(canvas, 56 + ox, 36 + oy, 4, 2, style->tongue);
        break;
    case FSA_ICON_JOY:
        sparkle(canvas, 14, 13, style->accent, style->accent2);
        sparkle(canvas, 67, 14, style->accent2, style->accent);
        rect(canvas, 19 + ox, 36 + oy, 5, 2, style->tongue);
        rect(canvas, 56 + ox, 36 + oy, 5, 2, style->tongue);
        break;
    case FSA_ICON_SWEAT:
        line(
            canvas, 61 + ox, 21 + oy + phase,
            63 + ox, 26 + oy + phase, style->iris);
        line(
            canvas, 63 + ox, 26 + oy + phase,
            60 + ox, 26 + oy + phase, style->iris);
        line(
            canvas, 60 + ox, 26 + oy + phase,
            61 + ox, 21 + oy + phase, style->iris);
        break;
    case FSA_ICON_SURPRISE:
        rect(canvas, 67, 10, 2, 7, style->accent2);
        rect(canvas, 67, 19, 2, 2, style->accent2);
        break;
    case FSA_ICON_THOUGHT:
        disc(canvas, 64, 36, 1, 1, style->accent);
        disc(canvas, 69, 31, 2, 2, style->accent);
        frame_rect(canvas, 70, 22, 7, 6, style->accent);
        break;
    case FSA_ICON_QUESTION:
        line(canvas, 66, 12, 70, 10, style->accent2);
        line(canvas, 70, 10, 73, 13, style->accent2);
        line(canvas, 73, 13, 69, 17, style->accent2);
        put_px(canvas, 69, 20, style->accent2);
        put_px(canvas, 70, 20, style->accent2);
        break;
    case FSA_ICON_FOCUS:
        line(canvas, 5, 20, 15, 23, style->accent);
        line(canvas, 75, 20, 65, 23, style->accent);
        line(canvas, 7, 32, 15, 31, style->accent2);
        line(canvas, 73, 32, 65, 31, style->accent2);
        break;
    case FSA_ICON_SLEEP:
        line(canvas, 61, 18, 66, 18, style->accent);
        line(canvas, 66, 18, 61, 23, style->accent);
        line(canvas, 61, 23, 66, 23, style->accent);
        line(canvas, 68, 11, 74, 11, style->accent2);
        line(canvas, 74, 11, 68, 17, style->accent2);
        line(canvas, 68, 17, 74, 17, style->accent2);
        break;
    case FSA_ICON_EXCITED:
        sparkle(canvas, 10, 13 + phase, style->accent, style->accent2);
        sparkle(canvas, 70, 12 - phase, style->accent2, style->accent);
        line(canvas, 5, 28, 14, 28, style->accent);
        line(canvas, 75, 28, 66, 28, style->accent);
        break;
    case FSA_ICON_BLUSH:
        for (int32_t x = 20; x <= 25; x += 2) {
            line(canvas, x + ox, 36 + oy, x + 2 + ox, 38 + oy,
                 style->tongue);
        }
        for (int32_t x = 54; x <= 59; x += 2) {
            line(canvas, x + ox, 36 + oy, x + 2 + ox, 38 + oy,
                 style->tongue);
        }
        break;
    default:
        if (key->cheek > 144U) {
            rect(canvas, 21 + ox, 37 + oy, 4, 2, style->tongue);
            rect(canvas, 55 + ox, 37 + oy, 4, 2, style->tongue);
        }
        break;
    }
}

static void draw_speech_coupling(
    fsa_canvas_t *canvas,
    const fsa_builtin_style_t *style,
    const fsa_resolved_t *resolved,
    const face_render_key_t *key,
    uint32_t sample_clock)
{
    if (resolved->anticipation != 0U) {
        line(canvas, 18, 17, 13, 13, style->accent2);
        line(canvas, 62, 17, 67, 13, style->accent2);
        if (style->kind == 5) {
            /*
             * The helmet's side status lamps make turn-taking anticipation
             * readable without reopening the mouth into a giant cavity.
             * They are attached to existing side panels, not free-floating
             * punctuation.
             */
            rect(canvas, 14, 25, 3, 8, style->accent2);
            rect(canvas, 63, 25, 3, 8, style->accent2);
        }
    } else if (resolved->speaking != 0U && key->audio_level > 96U) {
        const int32_t pulse =
            1 + (int32_t)((sample_clock / BUILTIN_TIMING.mouth_hold) & 1U);
        line(canvas, 7 - pulse, 39, 12, 39, style->accent);
        line(canvas, 68, 39, 73 + pulse, 39, style->accent);
    } else if (resolved->settle != 0U) {
        line(canvas, 18, 47, 22, 49, style->shade);
        line(canvas, 62, 47, 58, 49, style->shade);
    }
}

bool fsa_render_frame(
    fsa_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)profile >= FSA_PROFILE_COUNT || key == NULL ||
        rgb565 == NULL || pixel_capacity < FSA_PIXEL_COUNT) {
        return false;
    }
    const fsa_builtin_style_t *style = &STYLES[profile];
    fsa_canvas_t canvas = { rgb565, FSA_WIDTH, FSA_HEIGHT, 2 };
    fsa_resolved_t resolved;
    resolve_common(
        NULL, &BUILTIN_TIMING, key, sample_clock, &resolved);
    const uint8_t expression =
        resolved.pose < FACE_EXPRESSION_COUNT
            ? resolved.pose : FACE_EXPRESSION_NEUTRAL;
    fsa_acting_pose_t chrome_acting;
    const fsa_acting_pose_t *acting = &ACTING[expression];
    if (style->kind == 0 || style->kind == 1) {
        portrait_acting_for_key(key, &chrome_acting);
        acting = &chrome_acting;
    } else if (style->kind == 5) {
        chrome_acting_for_key(key, &chrome_acting);
        acting = &chrome_acting;
    }

    draw_backdrop(&canvas, style, sample_clock);
    int32_t ox = resolved.head_x;
    int32_t oy = resolved.head_y;
    /*
     * Pixel sprites use authored stepped roll rather than subpixel rotation.
     * It is limited to one native pixel at facial-feature height and shares
     * the held pose clock, so it cannot create adjacent-frame edge chatter.
     */
    const int32_t held_roll = resolved.roll;
    ox += held_roll;
    draw_shoulders_and_head(&canvas, style, ox, oy);

    int32_t left_inner = acting->left_brow_inner +
        key->brow_inner / 48 + key->controls.brow / 64;
    int32_t right_inner = acting->right_brow_inner +
        key->brow_inner / 48 + key->controls.brow / 64;
    int32_t left_outer = acting->left_brow_outer +
        key->brow_outer_left / 48;
    int32_t right_outer = acting->right_brow_outer +
        key->brow_outer_right / 48;
    draw_brow(
        &canvas, style, false,
        clamp_i32(left_inner, -4, 4),
        clamp_i32(left_outer, -4, 4), ox, oy);
    draw_brow(
        &canvas, style, true,
        clamp_i32(right_inner, -4, 4),
        clamp_i32(right_outer, -4, 4), ox, oy);
    draw_eye(
        &canvas, style, key, acting, &resolved, false, ox, oy);
    draw_eye(
        &canvas, style, key, acting, &resolved, true, ox, oy);
    draw_nose(&canvas, style, ox, oy);
    draw_mouth(&canvas, style, key, acting, &resolved, ox, oy);
    if (style->kind == 5) {
        draw_chrome_expression_details(
            &canvas, style, acting, key, ox, oy);
    } else {
        draw_expression_icon(
            &canvas, style, acting, key, sample_clock, ox, oy);
    }
    draw_speech_coupling(
        &canvas, style, &resolved, key, sample_clock);
    return true;
}
