#include "face_cyber_wildcards.h"

#include <string.h>

enum {
    CW_EXPRESSION_COUNT = 11,
    CW_EXPR_NEUTRAL = 0,
    CW_EXPR_WARM,
    CW_EXPR_JOY,
    CW_EXPR_CONCERN,
    CW_EXPR_SURPRISE,
    CW_EXPR_THOUGHTFUL,
    CW_EXPR_SKEPTICAL,
    CW_EXPR_DETERMINED,
    CW_EXPR_SLEEPY,
    CW_EXPR_EXCITED,
    CW_EXPR_EMBARRASSED,
};

#define CW_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8) |                      \
                (((uint16_t)(green) & 0xfcU) << 3) |                    \
                ((uint16_t)(blue) >> 3)))

typedef struct {
    uint16_t *pixels;
} cw_canvas_t;

typedef struct {
    int8_t eye_left;
    int8_t eye_right;
    int8_t brow_left;
    int8_t brow_right;
    int8_t brow_slope_left;
    int8_t brow_slope_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t smile;
    int8_t roll;
    uint8_t cheek;
} cw_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
} cw_viseme_t;

static const face_cyber_wildcard_info_t CW_INFO[
    FACE_CYBER_WILDCARD_COUNT] = {
    {
        "cyber-chladni-voiceplate",
        "Chladni Voiceplate",
        33U,
        11U,
    },
    {
        "cyber-teletext-performer",
        "Teletext Studio Performer",
        38U,
        9U,
    },
    {
        "cyber-ferrofluid-familiar",
        "Ferrofluid Magnetic Familiar",
        39U,
        15U,
    },
};

/*
 * Pixel-space acting targets.  Aperture, brow dialogue, gaze and mouth
 * corners carry the emotion; color is a supporting cue, never the only cue.
 */
static const cw_expression_t CW_EXPRESSIONS[CW_EXPRESSION_COUNT] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {-2, -2, -3, -3, -1, 1, 0, 0, -1, 4, 6, 0, 80},
    {-8, -8, -4, -4, -3, 3, 0, -1, 2, 8, 12, 0, 210},
    {-2, -1, -7, -7, 6, -6, -2, 1, -2, -3, -8, -3, 70},
    {6, 6, -9, -9, -1, 1, 0, -2, 11, -8, 0, 0, 0},
    {-8, 1, -6, 1, 5, -2, -7, -4, -2, -6, -3, 6, 35},
    {-11, 1, -9, 4, 7, -7, 7, -1, -2, 3, -5, -7, 45},
    {-5, -5, 5, 5, -7, 7, 0, 1, 1, 6, -2, 0, 45},
    {-11, -11, 4, 4, 0, 0, -3, 4, 2, -5, 1, 4, 15},
    {6, 6, -8, -8, -3, 3, 0, -2, 9, 9, 10, 0, 150},
    {-6, -11, -2, -7, 4, -6, 7, 4, -1, -3, 4, 6, 255},
};

static const cw_viseme_t CW_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {220, 164, 24, 0, 70, 30},
    [FACE_VISEME_E] = {104, 232, 12, 0, 130, 8},
    [FACE_VISEME_I] = {72, 218, 10, 0, 105, 8},
    [FACE_VISEME_O] = {184, 112, 238, 0, 32, 20},
    [FACE_VISEME_U] = {102, 82, 255, 0, 20, 18},
    [FACE_VISEME_PP] = {8, 158, 24, 255, 0, 0},
    [FACE_VISEME_SS] = {52, 218, 8, 25, 238, 0},
    [FACE_VISEME_TH] = {76, 184, 26, 0, 116, 255},
    [FACE_VISEME_DD] = {82, 168, 18, 0, 184, 70},
    [FACE_VISEME_FF] = {42, 188, 8, 40, 255, 0},
    [FACE_VISEME_KK] = {122, 174, 34, 0, 54, 70},
    [FACE_VISEME_NN] = {48, 164, 22, 10, 100, 50},
    [FACE_VISEME_RR] = {98, 150, 116, 0, 52, 30},
    [FACE_VISEME_CH] = {78, 198, 28, 0, 144, 20},
    [FACE_VISEME_SIL] = {8, 136, 30, 190, 0, 0},
};

static int32_t cw_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t cw_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t cw_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static uint16_t cw_mix_rgb565(
    uint16_t from, uint16_t to, uint8_t weight)
{
    const int32_t red = cw_mix(
        (from >> 11U) & 0x1fU,
        (to >> 11U) & 0x1fU,
        weight);
    const int32_t green = cw_mix(
        (from >> 5U) & 0x3fU,
        (to >> 5U) & 0x3fU,
        weight);
    const int32_t blue = cw_mix(
        from & 0x1fU,
        to & 0x1fU,
        weight);
    return (uint16_t)(
        ((uint16_t)red << 11U) |
        ((uint16_t)green << 5U) |
        (uint16_t)blue);
}

static uint32_t cw_signature(const face_render_key_t *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < sizeof(*key); ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static void cw_put(cw_canvas_t *canvas, int x, int y, uint16_t color)
{
    if ((unsigned)x < FACE_CYBER_WILDCARD_WIDTH &&
        (unsigned)y < FACE_CYBER_WILDCARD_HEIGHT) {
        canvas->pixels[
            (size_t)y * FACE_CYBER_WILDCARD_WIDTH + (size_t)x] = color;
    }
}

static void cw_clear(cw_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_CYBER_WILDCARD_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void cw_rect(
    cw_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    const int left = (int)cw_clamp(x, 0, FACE_CYBER_WILDCARD_WIDTH);
    const int top = (int)cw_clamp(y, 0, FACE_CYBER_WILDCARD_HEIGHT);
    const int right =
        (int)cw_clamp(x + width, 0, FACE_CYBER_WILDCARD_WIDTH);
    const int bottom =
        (int)cw_clamp(y + height, 0, FACE_CYBER_WILDCARD_HEIGHT);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            canvas->pixels[
                (size_t)yy * FACE_CYBER_WILDCARD_WIDTH + (size_t)xx] =
                color;
        }
    }
}

static void cw_line(
    cw_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = cw_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -cw_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        cw_put(canvas, x0, y0, color);
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

static void cw_thick_line(
    cw_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        cw_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void cw_ellipse(
    cw_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    const int left = (int)cw_clamp(
        cx - rx, 0, FACE_CYBER_WILDCARD_WIDTH - 1);
    const int right = (int)cw_clamp(
        cx + rx, 0, FACE_CYBER_WILDCARD_WIDTH - 1);
    const int top = (int)cw_clamp(
        cy - ry, 0, FACE_CYBER_WILDCARD_HEIGHT - 1);
    const int bottom = (int)cw_clamp(
        cy + ry, 0, FACE_CYBER_WILDCARD_HEIGHT - 1);
    const int64_t rr = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t dx = x - cx;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rr) {
                cw_put(canvas, x, y, color);
            }
        }
    }
}

static int32_t cw_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void cw_triangle(
    cw_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    int left = (int)cw_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        0,
        FACE_CYBER_WILDCARD_WIDTH - 1);
    int right = (int)cw_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        0,
        FACE_CYBER_WILDCARD_WIDTH - 1);
    int top = (int)cw_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        0,
        FACE_CYBER_WILDCARD_HEIGHT - 1);
    int bottom = (int)cw_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        0,
        FACE_CYBER_WILDCARD_HEIGHT - 1);
    const int32_t orientation = cw_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = cw_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = cw_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = cw_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                cw_put(canvas, x, y, color);
            }
        }
    }
}

static void cw_round_rect(
    cw_canvas_t *canvas,
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
    radius = (int)cw_clamp(radius, 0, width / 2);
    radius = (int)cw_clamp(radius, 0, height / 2);
    cw_rect(canvas, x + radius, y, width - radius * 2, height, color);
    cw_rect(canvas, x, y + radius, radius, height - radius * 2, color);
    cw_rect(
        canvas,
        x + width - radius,
        y + radius,
        radius,
        height - radius * 2,
        color);
    cw_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    cw_ellipse(
        canvas,
        x + width - radius - 1,
        y + radius,
        radius,
        radius,
        color);
    cw_ellipse(
        canvas,
        x + radius,
        y + height - radius - 1,
        radius,
        radius,
        color);
    cw_ellipse(
        canvas,
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        radius,
        color);
}

static uint8_t cw_viseme_index(uint8_t raw)
{
    return raw < FACE_VISEME_COUNT ? raw : FACE_VISEME_SIL;
}

static cw_viseme_t cw_blended_viseme(const face_render_key_t *key)
{
    const cw_viseme_t primary = CW_VISEMES[cw_viseme_index(key->viseme)];
    const cw_viseme_t secondary =
        CW_VISEMES[cw_viseme_index(key->viseme_secondary)];
    cw_viseme_t output;
    output.open = (uint8_t)cw_mix(
        primary.open, secondary.open, key->viseme_blend);
    output.width = (uint8_t)cw_mix(
        primary.width, secondary.width, key->viseme_blend);
    output.round = (uint8_t)cw_mix(
        primary.round, secondary.round, key->viseme_blend);
    output.press = (uint8_t)cw_mix(
        primary.press, secondary.press, key->viseme_blend);
    output.teeth = (uint8_t)cw_mix(
        primary.teeth, secondary.teeth, key->viseme_blend);
    output.tongue = (uint8_t)cw_mix(
        primary.tongue, secondary.tongue, key->viseme_blend);
    return output;
}

size_t face_cyber_wildcard_count(void)
{
    return FACE_CYBER_WILDCARD_COUNT;
}

const char *face_cyber_wildcard_slug(
    face_cyber_wildcard_profile_t profile)
{
    return (unsigned)profile < FACE_CYBER_WILDCARD_COUNT
        ? CW_INFO[profile].slug
        : NULL;
}

const char *face_cyber_wildcard_name(
    face_cyber_wildcard_profile_t profile)
{
    return (unsigned)profile < FACE_CYBER_WILDCARD_COUNT
        ? CW_INFO[profile].name
        : NULL;
}

bool face_cyber_wildcard_info(
    face_cyber_wildcard_profile_t profile,
    face_cyber_wildcard_info_t *info)
{
    if ((unsigned)profile >= FACE_CYBER_WILDCARD_COUNT || info == NULL) {
        return false;
    }
    *info = CW_INFO[profile];
    return true;
}

bool face_cyber_wildcard_from_legacy_id(
    uint8_t legacy_profile_id,
    face_cyber_wildcard_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        if (CW_INFO[raw].legacy_profile_id == legacy_profile_id) {
            *profile = (face_cyber_wildcard_profile_t)raw;
            return true;
        }
    }
    return false;
}

bool face_cyber_wildcard_resolve(
    face_cyber_wildcard_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_cyber_wildcard_pose_t *pose)
{
    if ((unsigned)profile >= FACE_CYBER_WILDCARD_COUNT ||
        key == NULL || pose == NULL) {
        return false;
    }
    (void)sample_clock;
    memset(pose, 0, sizeof(*pose));
    pose->source = *key;
    pose->input_signature = cw_signature(key);

    const uint8_t expression =
        key->stage_expression < CW_EXPRESSION_COUNT
        ? key->stage_expression
        : CW_EXPR_NEUTRAL;
    const uint8_t weight = key->expression_weight;
    const cw_expression_t *acting = &CW_EXPRESSIONS[expression];
    const cw_viseme_t viseme = cw_blended_viseme(key);
    const bool speaking =
        key->speech_phase != FACE_SPEECH_IDLE ||
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        key->controls.expression == FACE_ACTIVITY_SPEAKING;
    const uint8_t speech_weight = speaking ? key->viseme_weight : 0U;

    /*
     * Audio articulation and authored mouth controls remain parallel tracks:
     * visemes supply the phonetic target, controls keep the raw PCM analysis
     * influential, and the blend weight provides coarticulation.
     */
    int32_t mouth_open = cw_mix(
        key->controls.mouth_open, viseme.open, speech_weight);
    int32_t mouth_width = cw_mix(
        key->controls.mouth_width, viseme.width, speech_weight);
    int32_t mouth_round = cw_mix(
        key->controls.mouth_round, viseme.round, speech_weight);
    int32_t mouth_press = cw_mix(
        key->controls.mouth_press, viseme.press, speech_weight);

    if (key->speech_phase == FACE_SPEECH_STARTING) {
        /* Visible inhale/anticipation before the first articulated frame. */
        mouth_open = mouth_open * 3 / 5;
        mouth_width += 12;
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        /* Controlled settle rather than a discontinuous snap to rest. */
        mouth_open = mouth_open * 2 / 3;
    } else if (key->speech_phase == FACE_SPEECH_IDLE) {
        mouth_open = cw_mix(mouth_open, 12, 176U);
    }

    mouth_open += cw_mix(0, acting->mouth_open, weight) * 3;
    mouth_width += cw_mix(0, acting->mouth_width, weight) * 2;
    mouth_open = mouth_open * (255 - key->controls.mouth_press / 2) / 255;
    mouth_open += key->audio_level / 18;
    mouth_width -= mouth_round / 10;

    const int32_t lean_x =
        (int32_t)key->body_lean_x + key->head_yaw / 2;
    const int32_t lean_y =
        (int32_t)key->body_lean_y + key->head_pitch / 2;
    pose->body_lean_x =
        (int16_t)cw_clamp(lean_x / 24, -3, 3);
    pose->body_lean_y =
        (int16_t)cw_clamp(lean_y / 24, -3, 3);
    /*
     * Face-space anchors are immutable.  Head/body controls remain available
     * to each actor as secondary acting cues, but never drag the eyes or mouth
     * around inside their sockets.
     */
    pose->face_x = 80;
    pose->face_y = 59;
    pose->gaze_x = (int16_t)cw_clamp(
        key->controls.look_x / 18 + key->head_yaw / 48 +
            cw_mix(0, acting->gaze_x, weight),
        -7,
        7);
    pose->gaze_y = (int16_t)cw_clamp(
        key->controls.look_y / 22 + key->head_pitch / 56 +
            cw_mix(0, acting->gaze_y, weight),
        -5,
        5);
    pose->eye_y = 48;
    pose->eye_spacing = 62;
    pose->eye_width = 32;

    const int32_t blink =
        (key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U
        ? 17
        : 0;
    pose->eye_open_left = (int16_t)cw_clamp(
        7 + key->controls.eye_left_open / 16 -
            key->eye_left_squint / 24 +
            cw_mix(0, acting->eye_left, weight) -
            blink,
        3,
        25);
    pose->eye_open_right = (int16_t)cw_clamp(
        7 + key->controls.eye_right_open / 16 -
            key->eye_right_squint / 24 +
            cw_mix(0, acting->eye_right, weight) -
            blink,
        3,
        25);
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        pose->eye_open_left =
            (int16_t)cw_clamp(pose->eye_open_left + 3, 3, 25);
        pose->eye_open_right =
            (int16_t)cw_clamp(pose->eye_open_right + 3, 3, 25);
    } else if (key->speech_phase == FACE_SPEECH_ACTIVE) {
        /*
         * Vowels release the lids; pressed consonants focus them.  This is
         * driven by the aligned speech frame itself, never a free-running
         * oscillator, so eyes and mouth read as one performance.
         */
        const int32_t voice_focus = cw_clamp(
            ((int32_t)mouth_open - mouth_press) / 96 +
                (int32_t)key->audio_level / 128,
            -2,
            3);
        pose->eye_open_left = (int16_t)cw_clamp(
            pose->eye_open_left + voice_focus, 3, 25);
        pose->eye_open_right = (int16_t)cw_clamp(
            pose->eye_open_right + voice_focus, 3, 25);
    }

    const int32_t common_brow =
        key->controls.brow / 18 + key->brow_inner / 22;
    pose->brow_y_left = (int16_t)cw_clamp(
        29 - common_brow -
            key->brow_outer_left / 28 +
            cw_mix(0, acting->brow_left, weight),
        19,
        38);
    pose->brow_y_right = (int16_t)cw_clamp(
        29 - common_brow -
            key->brow_outer_right / 28 +
            cw_mix(0, acting->brow_right, weight),
        19,
        38);
    pose->brow_slope_left = (int16_t)cw_clamp(
        cw_mix(0, acting->brow_slope_left, weight) +
            key->brow_inner / 22 -
            key->brow_outer_left / 30,
        -8,
        8);
    pose->brow_slope_right = (int16_t)cw_clamp(
        cw_mix(0, acting->brow_slope_right, weight) -
            key->brow_inner / 22 +
            key->brow_outer_right / 30,
        -8,
        8);
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        pose->brow_y_left =
            (int16_t)cw_clamp(pose->brow_y_left - 3, 19, 38);
        pose->brow_y_right =
            (int16_t)cw_clamp(pose->brow_y_right - 3, 19, 38);
    } else if (key->speech_phase == FACE_SPEECH_ACTIVE) {
        const int32_t voice_lift = 1 + key->audio_level / 128;
        pose->brow_y_left = (int16_t)cw_clamp(
            pose->brow_y_left - voice_lift, 19, 38);
        pose->brow_y_right = (int16_t)cw_clamp(
            pose->brow_y_right - voice_lift, 19, 38);
    }

    pose->mouth_x = 80;
    pose->mouth_y = 87;
    pose->mouth_width = (int16_t)cw_clamp(
        18 + mouth_width * 34 / 255, 18, 52);
    pose->mouth_open = (int16_t)cw_clamp(
        3 + mouth_open * 25 / 255, 3, 28);
    pose->mouth_round = (int16_t)cw_clamp(mouth_round, 0, 255);
    pose->mouth_press = (int16_t)cw_clamp(mouth_press, 0, 255);
    const int32_t smile =
        cw_mix(0, acting->smile, weight) +
        key->affect_valence / 16;
    int32_t expression_corner_left = 0;
    int32_t expression_corner_right = 0;
    if (expression == CW_EXPR_THOUGHTFUL) {
        expression_corner_left = -5;
        expression_corner_right = 2;
    } else if (expression == CW_EXPR_SKEPTICAL) {
        expression_corner_left = -7;
        expression_corner_right = 4;
    } else if (expression == CW_EXPR_EMBARRASSED) {
        expression_corner_left = 5;
        expression_corner_right = -2;
    }
    expression_corner_left =
        cw_mix(0, expression_corner_left, weight);
    expression_corner_right =
        cw_mix(0, expression_corner_right, weight);
    pose->mouth_corner_left = (int16_t)cw_clamp(
        smile + expression_corner_left + key->mouth_corner_left / 12,
        -12,
        12);
    pose->mouth_corner_right = (int16_t)cw_clamp(
        smile + expression_corner_right + key->mouth_corner_right / 12,
        -12,
        12);
    pose->head_roll = (int16_t)cw_clamp(
        key->head_roll / 22 +
            cw_mix(0, acting->roll, weight),
        -7,
        7);
    pose->teeth = (uint8_t)cw_clamp(
        cw_mix(key->controls.mouth_teeth, viseme.teeth, speech_weight),
        0,
        255);
    pose->tongue = (uint8_t)cw_clamp(
        cw_mix(key->tongue, viseme.tongue, speech_weight) +
            (key->phoneme == FACE_PHONEME_NONE ? 0 : key->phoneme % 7U),
        0,
        255);
    pose->cheek = (uint8_t)cw_clamp(
        key->cheek + cw_mix(0, acting->cheek, weight), 0, 255);
    pose->attention = key->attention;
    pose->arousal = (uint8_t)cw_clamp(
        (key->affect_arousal + key->audio_level) / 2, 0, 255);
    pose->valence = key->affect_valence;
    pose->stage_expression = expression;
    pose->speech_phase = key->speech_phase;
    pose->activity = key->controls.expression;
    pose->speaking = speaking;
    return true;
}

static void cw_draw_mouth_cavity(
    cw_canvas_t *canvas,
    const face_cyber_wildcard_pose_t *pose,
    uint16_t rim,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    int rx = pose->mouth_width / 2;
    int ry = pose->mouth_open / 2;
    rx -= pose->mouth_round * rx / 1020;
    rx = (int)cw_clamp(rx, 8, 26);
    ry = (int)cw_clamp(ry, 2, 14);
    const int left_corner =
        cy - pose->mouth_corner_left / 2;
    const int right_corner =
        cy - pose->mouth_corner_right / 2;

    cw_thick_line(
        canvas,
        cx - rx,
        left_corner,
        cx,
        cy - ry,
        3,
        rim);
    cw_thick_line(
        canvas,
        cx,
        cy - ry,
        cx + rx,
        right_corner,
        3,
        rim);
    cw_thick_line(
        canvas,
        cx - rx,
        left_corner,
        cx,
        cy + ry,
        3,
        rim);
    cw_thick_line(
        canvas,
        cx,
        cy + ry,
        cx + rx,
        right_corner,
        3,
        rim);
    if (ry >= 3 && pose->mouth_press < 230) {
        cw_ellipse(canvas, cx, cy, rx - 2, ry - 1, cavity);
        if (pose->teeth > 118U && ry >= 5) {
            cw_rect(
                canvas,
                cx - rx + 4,
                cy - ry + 2,
                rx * 2 - 8,
                2 + pose->teeth / 100,
                teeth);
        }
        if (pose->tongue > 100U && ry >= 6) {
            cw_ellipse(
                canvas,
                cx + pose->source.phoneme % 3U - 1,
                cy + ry - 2,
                (rx - 3) * pose->tongue / 255,
                2 + pose->tongue / 100,
                tongue);
        }
    }
}

static void cw_signature_rail(
    cw_canvas_t *canvas,
    uint32_t signature,
    int x,
    int y,
    uint16_t on,
    uint16_t off)
{
    /*
     * A fixed two-line instrument rail makes every byte of the 40-byte IR
     * observable without turning the face itself into checksum noise.
     */
    for (int bit = 0; bit < 32; ++bit) {
        const int column = bit & 15;
        const int row = bit >> 4;
        cw_rect(
            canvas,
            x + column * 6,
            y + row * 3,
            4,
            2,
            ((signature >> bit) & 1U) != 0U ? on : off);
    }
}

static uint16_t cw_chladni_accent(
    const face_cyber_wildcard_pose_t *pose)
{
    switch (pose->stage_expression) {
    case CW_EXPR_JOY:
    case CW_EXPR_EXCITED:
        return CW_RGB565(255, 222, 100);
    case CW_EXPR_CONCERN:
    case CW_EXPR_SLEEPY:
        return CW_RGB565(120, 218, 232);
    case CW_EXPR_DETERMINED:
        return CW_RGB565(255, 142, 62);
    case CW_EXPR_EMBARRASSED:
        return CW_RGB565(255, 156, 154);
    default:
        return CW_RGB565(222, 194, 112);
    }
}

static void cw_render_chladni(
    cw_canvas_t *canvas,
    const face_cyber_wildcard_pose_t *pose,
    uint32_t sample_clock)
{
    const uint16_t background = CW_RGB565(3, 8, 16);
    const uint16_t bezel = CW_RGB565(22, 31, 40);
    const uint16_t plate_edge = CW_RGB565(79, 99, 108);
    const uint16_t plate = CW_RGB565(13, 34, 43);
    const uint16_t plate_low = CW_RGB565(7, 21, 29);
    const uint16_t accent = cw_chladni_accent(pose);
    const uint16_t accent_low =
        cw_mix_rgb565(plate, accent, 92U);
    const uint16_t eye_dark = CW_RGB565(1, 8, 11);
    const uint16_t pupil = CW_RGB565(244, 250, 226);
    (void)sample_clock;

    cw_clear(canvas, background);
    cw_round_rect(canvas, 8, 5, 144, 110, 12, bezel);
    cw_round_rect(canvas, 12, 9, 136, 102, 10, plate_edge);
    cw_round_rect(canvas, 15, 12, 130, 96, 8, plate);
    cw_round_rect(canvas, 25, 21, 110, 79, 10, plate_low);
    cw_round_rect(canvas, 28, 24, 104, 73, 8, plate);

    /* Fixed mounting hardware and resonance rails define one rigid object. */
    for (int corner = 0; corner < 4; ++corner) {
        const int x = corner & 1 ? 140 : 20;
        const int y = corner & 2 ? 103 : 17;
        cw_ellipse(canvas, x, y, 3, 3, plate_low);
        cw_put(canvas, x, y, accent);
    }
    cw_line(canvas, 30, 61, 38, 68, accent_low);
    cw_line(canvas, 38, 68, 30, 77, accent_low);
    cw_line(canvas, 130, 61, 122, 68, accent_low);
    cw_line(canvas, 122, 68, 130, 77, accent_low);

    const int eye_x[2] = {
        pose->face_x - pose->eye_spacing / 2,
        pose->face_x + pose->eye_spacing / 2,
    };
    const int eye_open[2] = {
        pose->eye_open_left,
        pose->eye_open_right,
    };
    for (int eye = 0; eye < 2; ++eye) {
        const int cx = eye_x[eye];
        const int cy = pose->eye_y;
        const int aperture_h =
            (int)cw_clamp(eye_open[eye], 3, 25);
        /* The outer socket never moves; only its luminous aperture acts. */
        cw_round_rect(canvas, cx - 20, cy - 16, 40, 32, 9, accent_low);
        cw_round_rect(canvas, cx - 17, cy - 13, 34, 26, 7, eye_dark);
        cw_round_rect(
            canvas,
            cx - 15,
            cy - aperture_h / 2,
            30,
            aperture_h,
            cw_clamp(aperture_h / 2, 1, 7),
            accent);
        const int px = cw_clamp(
            cx + pose->gaze_x,
            cx - 10,
            cx + 10);
        const int py = cw_clamp(
            cy + pose->gaze_y,
            cy - aperture_h / 2 + 2,
            cy + aperture_h / 2 - 2);
        const int pupil_radius = 3 + pose->attention / 128;
        cw_ellipse(canvas, px, py, pupil_radius + 1, pupil_radius + 1, pupil);
        cw_ellipse(canvas, px, py, pupil_radius - 1, pupil_radius - 1, eye_dark);
        cw_put(canvas, px - 1, py - 1, pupil);
    }
    cw_thick_line(
        canvas,
        eye_x[0] - 12,
        pose->brow_y_left - pose->brow_slope_left,
        eye_x[0] + 12,
        pose->brow_y_left + pose->brow_slope_left,
        3,
        accent);
    cw_thick_line(
        canvas,
        eye_x[1] - 12,
        pose->brow_y_right - pose->brow_slope_right,
        eye_x[1] + 12,
        pose->brow_y_right + pose->brow_slope_right,
        3,
        accent);

    /* A fixed lower transducer ties every viseme to the same face. */
    cw_round_rect(canvas, 45, 70, 70, 31, 8, bezel);
    cw_round_rect(canvas, 48, 73, 64, 25, 6, plate);
    cw_draw_mouth_cavity(
        canvas,
        pose,
        accent,
        eye_dark,
        CW_RGB565(244, 236, 183),
        CW_RGB565(238, 92, 82));
    const int chladni_rx = pose->mouth_width / 2;
    const int chladni_left_y =
        pose->mouth_y - pose->mouth_corner_left / 2;
    const int chladni_right_y =
        pose->mouth_y - pose->mouth_corner_right / 2;
    cw_ellipse(
        canvas,
        pose->mouth_x - chladni_rx,
        chladni_left_y,
        3,
        3,
        accent);
    cw_ellipse(
        canvas,
        pose->mouth_x + chladni_rx,
        chladni_right_y,
        3,
        3,
        accent);

    if (pose->cheek > 72U) {
        const int cheek_width = 4 + pose->cheek / 48;
        cw_round_rect(canvas, 29, 68, cheek_width, 4, 2, accent_low);
        cw_round_rect(
            canvas, 131 - cheek_width, 68, cheek_width, 4, 2, accent_low);
    }
    cw_signature_rail(
        canvas, pose->input_signature, 32, 102, accent, plate_low);
}

static uint16_t cw_tt_face_color(
    const face_cyber_wildcard_pose_t *pose)
{
    const uint16_t neutral = CW_RGB565(112, 242, 116);
    uint16_t target = neutral;
    switch (pose->stage_expression) {
    case CW_EXPR_JOY:
    case CW_EXPR_WARM:
        target = CW_RGB565(255, 220, 72);
        break;
    case CW_EXPR_CONCERN:
    case CW_EXPR_THOUGHTFUL:
        target = CW_RGB565(78, 214, 232);
        break;
    case CW_EXPR_DETERMINED:
        target = CW_RGB565(255, 82, 72);
        break;
    case CW_EXPR_SLEEPY:
        target = CW_RGB565(92, 144, 216);
        break;
    case CW_EXPR_EMBARRASSED:
        target = CW_RGB565(255, 122, 174);
        break;
    default:
        break;
    }
    return cw_mix_rgb565(
        neutral, target, pose->source.expression_weight);
}

static void cw_render_teletext(
    cw_canvas_t *canvas,
    const face_cyber_wildcard_pose_t *pose,
    uint32_t sample_clock)
{
    const uint16_t background = CW_RGB565(0, 0, 10);
    const uint16_t bezel = CW_RGB565(34, 36, 58);
    const uint16_t screen = CW_RGB565(3, 8, 25);
    const uint16_t cyan = CW_RGB565(58, 226, 242);
    const uint16_t yellow = CW_RGB565(255, 218, 68);
    const uint16_t red = CW_RGB565(248, 66, 82);
    const uint16_t white = CW_RGB565(234, 240, 238);
    const uint16_t face = cw_tt_face_color(pose);
    const uint16_t face_low =
        cw_mix_rgb565(screen, face, 76U);
    const uint16_t face_mid =
        cw_mix_rgb565(screen, face, 138U);
    (void)sample_clock;

    cw_clear(canvas, background);
    cw_rect(canvas, 8, 6, 144, 108, bezel);
    cw_rect(canvas, 12, 10, 136, 100, screen);
    cw_rect(canvas, 12, 10, 136, 8, CW_RGB565(4, 16, 58));

    /* Teletext header and fixed layout chrome. */
    cw_rect(canvas, 17, 12, 18, 3, yellow);
    cw_rect(canvas, 39, 12, 27, 3, cyan);
    cw_rect(canvas, 113, 12, 24, 3, white);
    const uint16_t speech_led =
        pose->speech_phase == FACE_SPEECH_STARTING ? yellow
        : pose->speech_phase == FACE_SPEECH_ACTIVE ? red
        : pose->speech_phase == FACE_SPEECH_ENDING ? cyan
        : CW_RGB565(30, 42, 58);
    cw_rect(canvas, 140, 12, 4, 3, speech_led);

    for (int y = 21; y < 101; y += 8) {
        cw_line(canvas, 14, y, 146, y, CW_RGB565(4, 13, 34));
    }

    /*
     * A fixed, deliberately sparse 8-bit portrait replaces the former dense
     * checkerboard.  It reads as one CRT actor even at 40x30 contact size.
     */
    cw_rect(canvas, 41, 23, 78, 5, face);
    cw_rect(canvas, 35, 28, 90, 6, face_mid);
    cw_rect(canvas, 29, 34, 102, 60, face_low);
    cw_rect(canvas, 35, 31, 90, 66, face_low);
    cw_rect(canvas, 41, 97, 78, 4, face);
    cw_rect(canvas, 29, 43, 5, 37, face);
    cw_rect(canvas, 126, 43, 5, 37, face);
    cw_rect(canvas, 34, 34, 92, 4, face_mid);

    const int eye_x[2] = {49, 111};
    const int eye_open[2] = {
        pose->eye_open_left,
        pose->eye_open_right,
    };
    for (int eye = 0; eye < 2; ++eye) {
        const int cx = eye_x[eye];
        const int aperture_h =
            3 + (int)cw_clamp(eye_open[eye], 3, 25) * 12 / 25;
        cw_rect(canvas, cx - 20, 38, 40, 25, face);
        cw_rect(canvas, cx - 17, 41, 34, 19, screen);
        cw_rect(
            canvas,
            cx - 15,
            pose->eye_y - aperture_h / 2,
            30,
            aperture_h,
            white);
        const int pupil_x = cw_clamp(
            cx + pose->gaze_x, cx - 10, cx + 10);
        const int pupil_y = cw_clamp(
            pose->eye_y + pose->gaze_y,
            pose->eye_y - aperture_h / 2 + 2,
            pose->eye_y + aperture_h / 2 - 2);
        cw_rect(canvas, pupil_x - 3, pupil_y - 3, 7, 7, screen);
        cw_rect(canvas, pupil_x - 1, pupil_y - 1, 3, 3, cyan);

        const int brow_y = eye == 0
            ? pose->brow_y_left
            : pose->brow_y_right;
        const int slope = eye == 0
            ? pose->brow_slope_left
            : pose->brow_slope_right;
        for (int segment = -2; segment <= 2; ++segment) {
            cw_rect(
                canvas,
                cx + segment * 6 - 2,
                brow_y + segment * slope / 3,
                5,
                3,
                red);
        }
    }

    /* Fixed speaker-panel mouth: phonetic interior, bounded outer identity. */
    cw_rect(canvas, 46, 70, 68, 28, face);
    cw_rect(canvas, 49, 73, 62, 22, screen);
    int mouth_width =
        (int)cw_clamp(pose->mouth_width, 18, 52);
    int mouth_height =
        3 + (int)cw_clamp(pose->mouth_open, 3, 28) * 14 / 28;
    if (pose->mouth_round > 150) {
        mouth_width = (int)cw_clamp(mouth_width - 6, 16, 42);
    }
    const int mouth_left = pose->mouth_x - mouth_width / 2;
    const int mouth_top = pose->mouth_y - mouth_height / 2;
    const int mouth_left_y =
        pose->mouth_y - pose->mouth_corner_left / 2;
    const int mouth_right_y =
        pose->mouth_y - pose->mouth_corner_right / 2;
    if (mouth_height <= 4 || pose->mouth_press > 220) {
        cw_thick_line(
            canvas,
            mouth_left,
            mouth_left_y,
            pose->mouth_x,
            pose->mouth_y,
            3,
            red);
        cw_thick_line(
            canvas,
            pose->mouth_x,
            pose->mouth_y,
            mouth_left + mouth_width,
            mouth_right_y,
            3,
            red);
    } else {
        cw_rect(
            canvas,
            mouth_left,
            mouth_top,
            mouth_width,
            mouth_height,
            red);
        cw_rect(
            canvas,
            mouth_left + 3,
            mouth_top + 3,
            mouth_width - 6,
            mouth_height - 6,
            CW_RGB565(17, 5, 25));
        if (pose->teeth > 112U && mouth_height >= 8) {
            cw_rect(
                canvas,
                mouth_left + 4,
                mouth_top + 3,
                mouth_width - 8,
                2 + pose->teeth / 128,
                white);
        }
        if (pose->tongue > 96U && mouth_height >= 10) {
            cw_rect(
                canvas,
                pose->mouth_x - mouth_width / 4,
                mouth_top + mouth_height - 5,
                mouth_width / 2,
                3,
                yellow);
        }
        cw_rect(canvas, mouth_left, mouth_top, 3, 3, screen);
        cw_rect(
            canvas,
            mouth_left + mouth_width - 3,
            mouth_top,
            3,
            3,
            screen);
    }
    cw_rect(canvas, mouth_left - 3, mouth_left_y - 2, 4, 4, red);
    cw_rect(
        canvas,
        mouth_left + mouth_width - 1,
        mouth_right_y - 2,
        4,
        4,
        red);

    if (pose->cheek > 72U) {
        const int cheek_width = 4 + pose->cheek / 64;
        cw_rect(canvas, 34, 66, cheek_width, 4, yellow);
        cw_rect(canvas, 126 - cheek_width, 66, cheek_width, 4, yellow);
    }

    /* Complete-key checksum lives in a fixed CRT status rail. */
    cw_rect(canvas, 29, 101, 102, 7, CW_RGB565(6, 18, 45));
    cw_signature_rail(
        canvas,
        pose->input_signature,
        32,
        102,
        cyan,
        CW_RGB565(18, 42, 66));
}

static uint16_t cw_ferro_accent(
    const face_cyber_wildcard_pose_t *pose)
{
    switch (pose->stage_expression) {
    case CW_EXPR_JOY:
    case CW_EXPR_EXCITED:
        return CW_RGB565(255, 170, 70);
    case CW_EXPR_CONCERN:
    case CW_EXPR_SLEEPY:
        return CW_RGB565(104, 174, 220);
    case CW_EXPR_EMBARRASSED:
        return CW_RGB565(236, 110, 160);
    case CW_EXPR_DETERMINED:
        return CW_RGB565(235, 75, 45);
    default:
        return CW_RGB565(82, 224, 187);
    }
}

static void cw_render_ferrofluid(
    cw_canvas_t *canvas,
    const face_cyber_wildcard_pose_t *pose,
    uint32_t sample_clock)
{
    const uint16_t background = CW_RGB565(7, 9, 13);
    const uint16_t tank = CW_RGB565(28, 34, 40);
    const uint16_t glass = CW_RGB565(113, 132, 134);
    const uint16_t chamber = CW_RGB565(166, 191, 179);
    const uint16_t chamber_low = CW_RGB565(124, 154, 148);
    const uint16_t fluid = CW_RGB565(4, 10, 12);
    const uint16_t sheen = CW_RGB565(52, 72, 70);
    const uint16_t accent = cw_ferro_accent(pose);
    const uint16_t accent_low =
        cw_mix_rgb565(chamber_low, accent, 84U);
    (void)sample_clock;

    cw_clear(canvas, background);
    cw_round_rect(canvas, 7, 5, 146, 110, 14, tank);
    cw_round_rect(canvas, 11, 9, 138, 102, 11, glass);
    cw_round_rect(canvas, 15, 13, 130, 94, 9, chamber);
    cw_rect(canvas, 18, 98, 124, 6, chamber_low);

    /* Fixed magnetic pole pieces; their field, not the chassis, performs. */
    cw_round_rect(canvas, 3, 42, 18, 36, 5, CW_RGB565(45, 50, 56));
    cw_round_rect(canvas, 139, 42, 18, 36, 5, CW_RGB565(45, 50, 56));
    cw_rect(canvas, 8, 50, 13, 20, accent);
    cw_rect(canvas, 139, 50, 13, 20, accent);
    cw_triangle(canvas, 21, 51, 28, 60, 21, 69, accent);
    cw_triangle(canvas, 139, 51, 132, 60, 139, 69, accent);

    /*
     * Three quiet pole-local contours imply a field without drawing through
     * the face.  Their topology is fixed across speech and emotion.
     */
    for (int band = 0; band < 3; ++band) {
        const int y = 30 + band * 26;
        const int bend = 3 + band;
        cw_line(canvas, 22, y, 30, y - bend, accent_low);
        cw_line(canvas, 30, y - bend, 36, y, accent_low);
        cw_line(canvas, 138, y, 130, y - bend, accent_low);
        cw_line(canvas, 130, y - bend, 124, y, accent_low);
    }

    const int eye_x[2] = {
        pose->face_x - pose->eye_spacing / 2,
        pose->face_x + pose->eye_spacing / 2,
    };
    const int eye_open[2] = {
        pose->eye_open_left,
        pose->eye_open_right,
    };
    for (int eye = 0; eye < 2; ++eye) {
        const int cx = eye_x[eye];
        const int cy = pose->eye_y;
        const int aperture_ry =
            (int)cw_clamp(eye_open[eye] / 2, 2, 12);
        /* Fixed magnetic well, with a bounded liquid aperture inside it. */
        cw_ellipse(canvas, cx, cy, 21, 18, fluid);
        cw_ellipse(canvas, cx, cy, 18, 15, sheen);
        cw_ellipse(canvas, cx, cy, 16, aperture_ry, chamber);
        const int px = cw_clamp(
            cx + pose->gaze_x,
            cx - 10,
            cx + 10);
        const int py = cw_clamp(
            cy + pose->gaze_y,
            cy - aperture_ry + 2,
            cy + aperture_ry - 2);
        cw_ellipse(canvas, px, py, 5, 5, accent);
        cw_ellipse(canvas, px, py, 2, 2, fluid);
        cw_put(canvas, px - 1, py - 1, CW_RGB565(225, 246, 224));
    }
    cw_thick_line(
        canvas,
        eye_x[0] - 13,
        pose->brow_y_left - pose->brow_slope_left,
        eye_x[0] + 13,
        pose->brow_y_left + pose->brow_slope_left,
        4,
        fluid);
    cw_thick_line(
        canvas,
        eye_x[1] - 13,
        pose->brow_y_right - pose->brow_slope_right,
        eye_x[1] + 13,
        pose->brow_y_right + pose->brow_slope_right,
        4,
        fluid);

    /*
     * The mouth lives in a fixed lower magnetic well.  It retains the liquid
     * contour language, but never detaches into a roaming droplet or VU bar.
     */
    cw_round_rect(canvas, 44, 70, 72, 31, 9, sheen);
    cw_round_rect(canvas, 47, 73, 66, 25, 7, chamber_low);
    cw_draw_mouth_cavity(
        canvas,
        pose,
        fluid,
        CW_RGB565(19, 8, 13),
        CW_RGB565(223, 237, 211),
        accent);

    if (pose->cheek > 80U) {
        const int radius = 2 + pose->cheek / 96;
        cw_ellipse(canvas, 36, 70, radius + 2, radius, accent);
        cw_ellipse(canvas, 124, 70, radius + 2, radius, accent);
    }
    cw_signature_rail(
        canvas, pose->input_signature, 32, 102, accent, tank);
}

bool face_cyber_wildcard_render(
    face_cyber_wildcard_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)profile >= FACE_CYBER_WILDCARD_COUNT ||
        render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_CYBER_WILDCARD_PIXEL_COUNT) {
        return false;
    }
    face_cyber_wildcard_pose_t pose;
    if (!face_cyber_wildcard_resolve(
            profile, render_key, sample_clock, &pose)) {
        return false;
    }
    cw_canvas_t canvas = {rgb565};
    switch (profile) {
    case FACE_CYBER_WILDCARD_CHLADNI:
        cw_render_chladni(&canvas, &pose, sample_clock);
        break;
    case FACE_CYBER_WILDCARD_TELETEXT:
        cw_render_teletext(&canvas, &pose, sample_clock);
        break;
    case FACE_CYBER_WILDCARD_FERROFLUID:
        cw_render_ferrofluid(&canvas, &pose, sample_clock);
        break;
    default:
        return false;
    }
    return true;
}

bool face_cyber_wildcard_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_cyber_wildcard_profile_t profile;
    return face_cyber_wildcard_from_legacy_id(
               legacy_profile_id, &profile) &&
        face_cyber_wildcard_render(
            profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
