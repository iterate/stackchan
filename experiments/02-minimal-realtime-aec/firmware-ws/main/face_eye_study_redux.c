#include "face_eye_study_redux.h"

#include <string.h>

#define ESR_RGB565(red, green, blue)                                   \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8) |                     \
                (((uint16_t)(green) & 0xfcU) << 3) |                   \
                ((uint16_t)(blue) >> 3)))

enum {
    ESR_EXPRESSION_COUNT = 11,
    ESR_EXPR_NEUTRAL = 0,
    ESR_EXPR_WARM = 1,
    ESR_EXPR_JOY = 2,
    ESR_EXPR_CONCERN = 3,
    ESR_EXPR_SURPRISE = 4,
    ESR_EXPR_THOUGHTFUL = 5,
    ESR_EXPR_SKEPTICAL = 6,
    ESR_EXPR_DETERMINED = 7,
    ESR_EXPR_SLEEPY = 8,
    ESR_EXPR_EXCITED = 9,
    ESR_EXPR_EMBARRASSED = 10,
    ESR_SHAPE_ELLIPSE = 0,
    ESR_SHAPE_ROUNDRECT = 1,
    ESR_SHAPE_POINTED = 2,
    ESR_IRIS_ROUND = 0,
    ESR_IRIS_DEEP = 1,
    ESR_IRIS_SLIT = 2,
};

typedef struct {
    int16_t open_left;
    int16_t open_right;
    int16_t width_left;
    int16_t width_right;
    int16_t height_left;
    int16_t height_right;
    int16_t lid_upper_left;
    int16_t lid_upper_right;
    int16_t lid_lower_left;
    int16_t lid_lower_right;
    int16_t brow_raise_left;
    int16_t brow_raise_right;
    int16_t brow_tilt_left;
    int16_t brow_tilt_right;
    int16_t brow_arch_left;
    int16_t brow_arch_right;
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t pupil;
} esr_expression_t;

typedef struct {
    const char *slug;
    const char *name;
    uint16_t bg_top;
    uint16_t bg_bottom;
    uint16_t panel;
    uint16_t outline;
    uint16_t sclera;
    uint16_t iris;
    uint16_t iris_light;
    uint16_t pupil;
    uint16_t highlight;
    uint16_t brow;
    uint16_t accent;
    int16_t left_x;
    int16_t right_x;
    int16_t center_y;
    int16_t radius_x;
    int16_t radius_y;
    int16_t iris_radius;
    int16_t pupil_radius;
    int16_t travel_x;
    int16_t travel_y;
    int16_t brow_gap;
    int16_t brow_length;
    uint16_t blink_period_ms;
    uint16_t gaze_period_ms;
    uint8_t shape;
    uint8_t iris_kind;
    uint8_t outline_px;
    uint8_t brow_px;
    uint8_t glow_px;
    uint8_t autonomous_gaze;
    uint8_t matrix;
} esr_style_t;

typedef struct {
    uint16_t *pixels;
} esr_canvas_t;

/*
 * Every stage expression changes eye geometry.  These are profile-neutral Q8
 * deltas; profile style and authored acting layers modulate them afterward.
 */
static const esr_expression_t ESR_EXPRESSIONS[ESR_EXPRESSION_COUNT] = {
    [ESR_EXPR_NEUTRAL] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,
    },
    [ESR_EXPR_WARM] = {
        -18, -18, 18, 18, -8, -8, 8, 8, 36, 36,
        22, 22, -12, 12, 22, 22, 0, 6, -8,
    },
    [ESR_EXPR_JOY] = {
        -58, -58, 30, 30, -22, -22, 44, 44, 20, 20,
        30, 30, -34, 34, 52, 52, 0, -16, -18,
    },
    [ESR_EXPR_CONCERN] = {
        -22, -8, -8, -8, 10, 10, 44, 44, 12, 12,
        72, 72, 58, -58, 44, 44, -24, 18, -6,
    },
    [ESR_EXPR_SURPRISE] = {
        92, 92, -16, -16, 58, 58, -26, -26, -20, -20,
        108, 108, -8, 8, 24, 24, 0, -24, 52,
    },
    [ESR_EXPR_THOUGHTFUL] = {
        -54, 20, -18, 8, -16, 18, 42, 4, 16, -8,
        54, 12, 42, -10, 50, 12, -92, -58, -18,
    },
    [ESR_EXPR_SKEPTICAL] = {
        -112, 22, -10, 18, -30, 20, 70, -2, 30, -12,
        -12, 76, -46, -52, -20, 60, 82, -4, -12,
    },
    [ESR_EXPR_DETERMINED] = {
        -58, -58, 28, 28, -18, -18, 78, 78, 12, 12,
        -52, -52, -70, 70, -30, -30, 0, 24, -24,
    },
    [ESR_EXPR_SLEEPY] = {
        -100, -100, 20, 20, -40, -40, 75, 75, 16, 16,
        -36, -36, 4, -4, -18, -18, -30, 72, 18,
    },
    [ESR_EXPR_EXCITED] = {
        72, 72, 42, 42, 42, 42, -18, -18, -12, -12,
        92, 92, -28, 28, 42, 42, 0, -42, 64,
    },
    [ESR_EXPR_EMBARRASSED] = {
        -52, -65, 8, -14, -14, -28, 42, 45, 32, 40,
        26, 58, 24, -48, 18, 54, 74, 38, -28,
    },
};

static const esr_style_t ESR_STYLES[FACE_EYE_STUDY_REDUX_PROFILE_COUNT] = {
    {
        "saccade-lab", "Saccade laboratory redux",
        ESR_RGB565(236, 235, 222), ESR_RGB565(208, 215, 204),
        ESR_RGB565(222, 224, 212), ESR_RGB565(31, 42, 48),
        ESR_RGB565(255, 253, 239), ESR_RGB565(211, 100, 36),
        ESR_RGB565(244, 166, 62), ESR_RGB565(31, 42, 48),
        ESR_RGB565(255, 255, 244), ESR_RGB565(31, 42, 48),
        ESR_RGB565(225, 91, 38),
        47, 113, 60, 29, 20, 11, 4, 11, 8, 16, 24,
        3600, 1760, ESR_SHAPE_ELLIPSE, ESR_IRIS_ROUND,
        2, 3, 0, 210, 0,
    },
    {
        "brow-dialogue", "Brow dialogue redux",
        ESR_RGB565(49, 37, 39), ESR_RGB565(23, 20, 27),
        ESR_RGB565(76, 55, 55), ESR_RGB565(21, 18, 24),
        ESR_RGB565(225, 211, 184), ESR_RGB565(91, 151, 128),
        ESR_RGB565(143, 201, 164), ESR_RGB565(22, 37, 36),
        ESR_RGB565(248, 244, 218), ESR_RGB565(239, 186, 117),
        ESR_RGB565(218, 111, 92),
        48, 112, 61, 27, 18, 9, 4, 10, 7, 17, 25,
        3300, 2260, ESR_SHAPE_ROUNDRECT, ESR_IRIS_ROUND,
        2, 4, 1, 125, 0,
    },
    {
        "lid-anticipation", "Lid anticipation redux",
        ESR_RGB565(12, 23, 40), ESR_RGB565(4, 9, 20),
        ESR_RGB565(20, 47, 70), ESR_RGB565(3, 9, 18),
        ESR_RGB565(218, 234, 239), ESR_RGB565(58, 180, 226),
        ESR_RGB565(124, 225, 250), ESR_RGB565(7, 35, 58),
        ESR_RGB565(245, 254, 255), ESR_RGB565(94, 220, 248),
        ESR_RGB565(235, 82, 154),
        48, 112, 61, 31, 23, 12, 4, 12, 9, 18, 27,
        3900, 1940, ESR_SHAPE_POINTED, ESR_IRIS_ROUND,
        3, 3, 4, 175, 0,
    },
    {
        "iris-parallax", "Iris parallax redux",
        ESR_RGB565(24, 16, 31), ESR_RGB565(8, 7, 14),
        ESR_RGB565(42, 26, 47), ESR_RGB565(39, 24, 31),
        ESR_RGB565(229, 220, 195), ESR_RGB565(36, 130, 133),
        ESR_RGB565(116, 205, 167), ESR_RGB565(24, 36, 38),
        ESR_RGB565(255, 255, 238), ESR_RGB565(206, 187, 157),
        ESR_RGB565(201, 102, 121),
        47, 113, 60, 31, 23, 15, 5, 13, 9, 19, 24,
        3500, 2080, ESR_SHAPE_ELLIPSE, ESR_IRIS_DEEP,
        3, 3, 0, 160, 0,
    },
    {
        "sleep-wake", "Sleep and wake redux",
        ESR_RGB565(39, 34, 74), ESR_RGB565(10, 13, 35),
        ESR_RGB565(63, 53, 101), ESR_RGB565(71, 56, 111),
        ESR_RGB565(190, 178, 241), ESR_RGB565(115, 111, 213),
        ESR_RGB565(207, 190, 255), ESR_RGB565(37, 30, 78),
        ESR_RGB565(255, 247, 255), ESR_RGB565(206, 184, 255),
        ESR_RGB565(125, 204, 240),
        /*
         * This actor used to force every neutral frame into a narrow
         * "sleepy" slit.  At the real 40x30 target that collapsed the eyes
         * and pupils into one lavender scanline.  Keep the bedtime palette,
         * but give the authored lids a broad Cozmo-like socket to act in.
         */
        48, 112, 61, 31, 23, 12, 4, 12, 9, 20, 27,
        4700, 3180, ESR_SHAPE_ROUNDRECT, ESR_IRIS_ROUND,
        3, 4, 3, 115, 0,
    },
    {
        "curious-tilt", "Curious asymmetric gaze redux",
        ESR_RGB565(245, 213, 104), ESR_RGB565(218, 159, 65),
        ESR_RGB565(237, 188, 79), ESR_RGB565(39, 51, 57),
        ESR_RGB565(255, 248, 211), ESR_RGB565(53, 139, 169),
        ESR_RGB565(103, 193, 196), ESR_RGB565(25, 53, 63),
        ESR_RGB565(255, 255, 235), ESR_RGB565(43, 52, 58),
        ESR_RGB565(201, 75, 76),
        47, 112, 60, 29, 21, 11, 4, 12, 9, 17, 27,
        3400, 1650, ESR_SHAPE_POINTED, ESR_IRIS_ROUND,
        3, 4, 0, 205, 0,
    },
    {
        "dot-matrix-eyes", "Dot-matrix expressions redux",
        ESR_RGB565(11, 18, 18), ESR_RGB565(2, 6, 7),
        ESR_RGB565(20, 39, 38), ESR_RGB565(4, 12, 13),
        ESR_RGB565(238, 105, 50), ESR_RGB565(255, 153, 55),
        ESR_RGB565(255, 204, 86), ESR_RGB565(25, 57, 47),
        ESR_RGB565(255, 244, 174), ESR_RGB565(255, 137, 51),
        ESR_RGB565(80, 228, 177),
        47, 113, 59, 28, 21, 9, 3, 12, 9, 17, 25,
        3100, 1420, ESR_SHAPE_ELLIPSE, ESR_IRIS_ROUND,
        1, 3, 0, 220, 1,
    },
    {
        "cat-optics", "Cat optic study redux",
        ESR_RGB565(11, 30, 31), ESR_RGB565(2, 8, 13),
        ESR_RGB565(19, 48, 48), ESR_RGB565(13, 32, 31),
        ESR_RGB565(169, 203, 122), ESR_RGB565(139, 195, 74),
        ESR_RGB565(211, 232, 116), ESR_RGB565(12, 31, 30),
        ESR_RGB565(248, 255, 219), ESR_RGB565(106, 170, 95),
        ESR_RGB565(204, 114, 93),
        47, 113, 59, 31, 20, 15, 3, 12, 8, 19, 28,
        5400, 1880, ESR_SHAPE_POINTED, ESR_IRIS_SLIT,
        3, 3, 3, 195, 0,
    },
};

static int32_t esr_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t esr_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t esr_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static bool esr_is_character_style(const esr_style_t *style)
{
    return style == &ESR_STYLES[1] ||
           style == &ESR_STYLES[3] ||
           style == &ESR_STYLES[4] ||
           style == &ESR_STYLES[5] ||
           style == &ESR_STYLES[7];
}

static uint32_t esr_hash_u32(uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

static uint32_t esr_key_signature(const face_render_key_t *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < sizeof(*key); ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static int32_t esr_smoothstep_q8(int32_t value)
{
    const int32_t t = esr_clamp(value, 0, 256);
    return (t * t * (768 - 2 * t) + 32768) / 65536;
}

static uint16_t esr_blend565(
    uint16_t from, uint16_t to, uint32_t alpha32)
{
    if (alpha32 == 0U) {
        return from;
    }
    if (alpha32 >= 32U) {
        return to;
    }
    const uint32_t inverse = 32U - alpha32;
    const uint32_t red =
        (((from >> 11U) & 31U) * inverse +
         ((to >> 11U) & 31U) * alpha32 + 16U) >>
        5U;
    const uint32_t green =
        (((from >> 5U) & 63U) * inverse +
         ((to >> 5U) & 63U) * alpha32 + 16U) >>
        5U;
    const uint32_t blue =
        ((from & 31U) * inverse +
         (to & 31U) * alpha32 + 16U) >>
        5U;
    return (uint16_t)((red << 11U) | (green << 5U) | blue);
}

static void esr_put(
    esr_canvas_t *canvas,
    int32_t x,
    int32_t y,
    uint16_t color,
    uint32_t alpha32)
{
    if ((unsigned)x >= FACE_EYE_STUDY_REDUX_WIDTH ||
        (unsigned)y >= FACE_EYE_STUDY_REDUX_HEIGHT) {
        return;
    }
    uint16_t *pixel =
        &canvas->pixels[
            (size_t)y * FACE_EYE_STUDY_REDUX_WIDTH + (size_t)x];
    *pixel = esr_blend565(*pixel, color, alpha32);
}

static void esr_gradient(
    esr_canvas_t *canvas, uint16_t top, uint16_t bottom)
{
    for (int32_t y = 0; y < FACE_EYE_STUDY_REDUX_HEIGHT; ++y) {
        const uint16_t color = esr_blend565(
            top, bottom,
            (uint32_t)y * 32U /
                (FACE_EYE_STUDY_REDUX_HEIGHT - 1U));
        for (int32_t x = 0; x < FACE_EYE_STUDY_REDUX_WIDTH; ++x) {
            canvas->pixels[
                (size_t)y * FACE_EYE_STUDY_REDUX_WIDTH +
                (size_t)x] = color;
        }
    }
}

static void esr_rect(
    esr_canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color,
    uint32_t alpha32)
{
    const int32_t left = esr_clamp(x, 0, FACE_EYE_STUDY_REDUX_WIDTH);
    const int32_t right =
        esr_clamp(x + width, 0, FACE_EYE_STUDY_REDUX_WIDTH);
    const int32_t top = esr_clamp(y, 0, FACE_EYE_STUDY_REDUX_HEIGHT);
    const int32_t bottom =
        esr_clamp(y + height, 0, FACE_EYE_STUDY_REDUX_HEIGHT);
    for (int32_t yy = top; yy < bottom; ++yy) {
        for (int32_t xx = left; xx < right; ++xx) {
            esr_put(canvas, xx, yy, color, alpha32);
        }
    }
}

static void esr_disc(
    esr_canvas_t *canvas,
    int32_t cx,
    int32_t cy,
    int32_t radius,
    uint16_t color,
    uint32_t alpha32)
{
    if (radius <= 0) {
        return;
    }
    const int32_t outer = radius * radius;
    const int32_t inner = (radius - 1) * (radius - 1);
    for (int32_t y = cy - radius; y <= cy + radius; ++y) {
        for (int32_t x = cx - radius; x <= cx + radius; ++x) {
            const int32_t dx = x - cx;
            const int32_t dy = y - cy;
            const int32_t distance = dx * dx + dy * dy;
            if (distance <= inner) {
                esr_put(canvas, x, y, color, alpha32);
            } else if (distance <= outer) {
                esr_put(canvas, x, y, color, alpha32 / 2U);
            }
        }
    }
}

static void esr_line(
    esr_canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color,
    uint32_t alpha32)
{
    const int32_t dx = esr_abs(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -esr_abs(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        esr_disc(
            canvas, x0, y0,
            thickness > 1 ? thickness / 2 : 1,
            color, alpha32);
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

static void esr_curve(
    esr_canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t cx,
    int32_t cy,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color,
    uint32_t alpha32)
{
    int32_t previous_x = x0;
    int32_t previous_y = y0;
    for (int32_t step = 1; step <= 12; ++step) {
        const int32_t inverse = 12 - step;
        const int32_t x =
            (inverse * inverse * x0 +
             2 * inverse * step * cx +
             step * step * x1 +
             72) /
            144;
        const int32_t y =
            (inverse * inverse * y0 +
             2 * inverse * step * cy +
             step * step * y1 +
             72) /
            144;
        esr_line(
            canvas, previous_x, previous_y, x, y,
            thickness, color, alpha32);
        previous_x = x;
        previous_y = y;
    }
}

/*
 * The contact-scale eye actors need strokes which read as drawn lids rather
 * than plumbing.  A constant-width quadratic becomes a blunt capsule after
 * the 160x120 frame is reduced to 40x30, so the character studies use a
 * thicker middle and genuinely tapered ends.  Sixteen fixed segments keep
 * the result deterministic and integer-only.
 */
static void esr_tapered_curve(
    esr_canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t cx,
    int32_t cy,
    int32_t x1,
    int32_t y1,
    int32_t middle_thickness,
    uint16_t color,
    uint32_t alpha32)
{
    int32_t previous_x = x0;
    int32_t previous_y = y0;
    for (int32_t step = 1; step <= 16; ++step) {
        const int32_t inverse = 16 - step;
        const int32_t x =
            (inverse * inverse * x0 +
             2 * inverse * step * cx +
             step * step * x1 +
             128) /
            256;
        const int32_t y =
            (inverse * inverse * y0 +
             2 * inverse * step * cy +
             step * step * y1 +
             128) /
            256;
        const int32_t distance_from_end =
            step <= 8 ? step : 16 - step;
        const int32_t thickness = esr_clamp(
            2 + (middle_thickness - 2) *
                    distance_from_end / 8,
            2,
            middle_thickness);
        esr_line(
            canvas, previous_x, previous_y, x, y,
            thickness, color, alpha32);
        previous_x = x;
        previous_y = y;
    }
}

static bool esr_valid_profile(
    face_eye_study_redux_profile_t profile)
{
    return (int)profile >= FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID &&
           (int)profile <= FACE_EYE_STUDY_REDUX_LAST_LEGACY_ID;
}

static size_t esr_style_index(
    face_eye_study_redux_profile_t profile)
{
    return (size_t)((int)profile -
                    FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID);
}

size_t face_eye_study_redux_profile_count(void)
{
    return FACE_EYE_STUDY_REDUX_PROFILE_COUNT;
}

bool face_eye_study_redux_profile_from_index(
    size_t index, face_eye_study_redux_profile_t *profile)
{
    if (index >= FACE_EYE_STUDY_REDUX_PROFILE_COUNT ||
        profile == NULL) {
        return false;
    }
    *profile = (face_eye_study_redux_profile_t)(
        FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID + (int)index);
    return true;
}

bool face_eye_study_redux_profile_from_legacy_id(
    uint8_t legacy_id, face_eye_study_redux_profile_t *profile)
{
    if (legacy_id < FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID ||
        legacy_id > FACE_EYE_STUDY_REDUX_LAST_LEGACY_ID ||
        profile == NULL) {
        return false;
    }
    *profile = (face_eye_study_redux_profile_t)legacy_id;
    return true;
}

const char *face_eye_study_redux_profile_slug(
    face_eye_study_redux_profile_t profile)
{
    return esr_valid_profile(profile)
               ? ESR_STYLES[esr_style_index(profile)].slug
               : NULL;
}

const char *face_eye_study_redux_profile_name(
    face_eye_study_redux_profile_t profile)
{
    return esr_valid_profile(profile)
               ? ESR_STYLES[esr_style_index(profile)].name
               : NULL;
}

static int32_t esr_blink_q8(
    const esr_style_t *style,
    size_t profile_index,
    const face_render_key_t *key,
    uint32_t time_ms)
{
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        return 0;
    }
    uint32_t period = style->blink_period_ms;
    const bool character_style =
        profile_index == 1U ||
        profile_index == 3U ||
        profile_index == 4U ||
        profile_index == 5U ||
        profile_index == 7U;
    if (!character_style) {
        if (key->controls.expression == FACE_ACTIVITY_SPEAKING) {
            period = period * 4U / 5U;
        } else if (key->controls.expression == FACE_ACTIVITY_THINKING) {
            period = period * 9U / 10U;
        }
    }
    if (period < 900U) {
        period = 900U;
    }
    const uint32_t offset =
        431U + (uint32_t)profile_index * 337U;
    const uint32_t phase = (time_ms + offset) % period;
    /*
     * A broad socket can change thousands of pixels during a blink.  Keep the
     * curve stateless but give it enough samples at 30 fps to read as an
     * authored close-and-open action rather than a two-frame topology pop.
     * There is deliberately no closed-eye hold: closure is a single pose
     * between two smooth 350 ms arcs.
     */
    const uint32_t blink_span =
        profile_index == 3U ? 960U :
        profile_index == 4U || profile_index == 5U ? 900U :
        profile_index == 1U || profile_index == 2U ? 820U :
        700U;
    if (phase + blink_span < period) {
        return 256;
    }
    const uint32_t u = phase + blink_span - period;
    const uint32_t close_ms = blink_span / 2U;
    const uint32_t hold_ms = 0U;
    if (u < close_ms) {
        return 256 -
            esr_smoothstep_q8((int32_t)(u * 256U / close_ms));
    }
    if (u < close_ms + hold_ms) {
        return 0;
    }
    const uint32_t reopen_ms =
        blink_span - close_ms - hold_ms;
    return esr_smoothstep_q8(
        (int32_t)(
            (u - close_ms - hold_ms) * 256U /
            (reopen_ms > 0U ? reopen_ms : 1U)));
}

static int32_t esr_target_component(
    uint32_t cycle, uint32_t salt, uint8_t amplitude)
{
    const uint32_t hash = esr_hash_u32(cycle ^ salt);
    const int32_t signed_value =
        (int32_t)((hash >> 8U) & 511U) - 255;
    return signed_value * amplitude / 255;
}

static void esr_autonomous_gaze(
    const esr_style_t *style,
    size_t profile_index,
    uint32_t time_ms,
    int32_t *x_q8,
    int32_t *y_q8,
    uint8_t *active)
{
    const uint32_t period = style->gaze_period_ms;
    const uint32_t cycle = time_ms / period;
    const uint32_t phase = time_ms % period;
    const uint32_t transition_ms = 145U;
    const uint32_t salt =
        0x9e3779b9U ^ ((uint32_t)profile_index * 0x45d9f3bU);
    const int32_t previous_x = esr_target_component(
        cycle - 1U, salt, style->autonomous_gaze);
    const int32_t previous_y = esr_target_component(
        cycle - 1U, salt ^ 0xa511e9b3U,
        (uint8_t)(style->autonomous_gaze * 3U / 5U));
    const int32_t target_x = esr_target_component(
        cycle, salt, style->autonomous_gaze);
    const int32_t target_y = esr_target_component(
        cycle, salt ^ 0xa511e9b3U,
        (uint8_t)(style->autonomous_gaze * 3U / 5U));
    if (phase < transition_ms) {
        const int32_t blend = esr_smoothstep_q8(
            (int32_t)(phase * 256U / transition_ms));
        *x_q8 =
            previous_x + (target_x - previous_x) * blend / 256;
        *y_q8 =
            previous_y + (target_y - previous_y) * blend / 256;
        *active = 1U;
    } else {
        *x_q8 = target_x;
        *y_q8 = target_y;
        *active = 0U;
    }
}

static int32_t esr_expr(
    int16_t value, uint8_t expression_weight)
{
    return esr_mix(0, value, expression_weight);
}

/*
 * These five profiles are deliberately different characters, not palette
 * swaps.  The shared expression table supplies a common vocabulary; this
 * small target-only pass gives each actor its own timing/aperture grammar and
 * prevents a stylised squint from degenerating into a missing eye at 40x30.
 */
static void esr_apply_character_acting(
    size_t index,
    uint8_t expression,
    uint8_t weight,
    face_eye_study_redux_pose_t *pose)
{
    int32_t open_delta[2] = {0, 0};
    int32_t width_delta[2] = {0, 0};
    int32_t height_delta[2] = {0, 0};
    int32_t upper_delta[2] = {0, 0};
    int32_t lower_delta[2] = {0, 0};
    int32_t raise_delta[2] = {0, 0};
    int32_t tilt_delta[2] = {0, 0};
    int32_t arch_delta[2] = {0, 0};
    int32_t gaze_x_delta = 0;
    int32_t gaze_y_delta = 0;
    int32_t pupil_delta = 0;
    int32_t minimum_open = 0;

    if (index == 1U) {
        /* Brow dialogue: elastic brows lead, lids answer a beat behind. */
        switch (expression) {
        case ESR_EXPR_WARM:
            lower_delta[0] = 10;
            lower_delta[1] = 10;
            arch_delta[0] = 14;
            arch_delta[1] = 14;
            gaze_y_delta = 6;
            break;
        case ESR_EXPR_JOY:
            open_delta[0] = -18;
            open_delta[1] = -18;
            lower_delta[0] = 20;
            lower_delta[1] = 20;
            arch_delta[0] = 22;
            arch_delta[1] = 22;
            gaze_y_delta = -8;
            break;
        case ESR_EXPR_CONCERN:
            raise_delta[0] = 14;
            raise_delta[1] = 14;
            tilt_delta[0] = 14;
            tilt_delta[1] = -14;
            gaze_y_delta = 10;
            break;
        case ESR_EXPR_SURPRISE:
            open_delta[0] = 20;
            open_delta[1] = 20;
            raise_delta[0] = 22;
            raise_delta[1] = 22;
            height_delta[0] = 12;
            height_delta[1] = 12;
            break;
        case ESR_EXPR_THOUGHTFUL:
            open_delta[0] = 18;
            gaze_x_delta = -14;
            arch_delta[0] = 12;
            break;
        case ESR_EXPR_SKEPTICAL:
            open_delta[0] = 34;
            tilt_delta[0] = -12;
            tilt_delta[1] = -8;
            break;
        case ESR_EXPR_DETERMINED:
            upper_delta[0] = 16;
            upper_delta[1] = 16;
            tilt_delta[0] = -16;
            tilt_delta[1] = 16;
            break;
        case ESR_EXPR_SLEEPY:
            open_delta[0] = -20;
            open_delta[1] = -20;
            lower_delta[0] = 10;
            lower_delta[1] = 10;
            break;
        case ESR_EXPR_EXCITED:
            open_delta[0] = 16;
            open_delta[1] = 16;
            raise_delta[0] = 18;
            raise_delta[1] = 18;
            arch_delta[0] = 12;
            arch_delta[1] = 12;
            break;
        case ESR_EXPR_EMBARRASSED:
            lower_delta[0] = 16;
            lower_delta[1] = 20;
            arch_delta[0] = 10;
            arch_delta[1] = 18;
            gaze_y_delta = 12;
            break;
        default:
            break;
        }
        minimum_open = expression == ESR_EXPR_SLEEPY ? 26 : 48;
    } else if (index == 3U) {
        /*
         * Iris parallax: the lens performance leads; retain enough aperture
         * for its concentric iris to survive thoughtful/skeptical poses.
         */
        switch (expression) {
        case ESR_EXPR_WARM:
            open_delta[0] = 8;
            open_delta[1] = 8;
            lower_delta[0] = 8;
            lower_delta[1] = 8;
            pupil_delta = 14;
            break;
        case ESR_EXPR_JOY:
            open_delta[0] = -10;
            open_delta[1] = -10;
            lower_delta[0] = 14;
            lower_delta[1] = 14;
            pupil_delta = 10;
            break;
        case ESR_EXPR_CONCERN:
            tilt_delta[0] = 10;
            tilt_delta[1] = -10;
            gaze_y_delta = 8;
            break;
        case ESR_EXPR_SURPRISE:
            open_delta[0] = 16;
            open_delta[1] = 16;
            pupil_delta = 20;
            break;
        case ESR_EXPR_THOUGHTFUL:
            open_delta[0] = 34;
            gaze_x_delta = -10;
            break;
        case ESR_EXPR_SKEPTICAL:
            open_delta[0] = 46;
            width_delta[0] = 10;
            break;
        case ESR_EXPR_DETERMINED:
            open_delta[0] = 12;
            open_delta[1] = 12;
            upper_delta[0] = 8;
            upper_delta[1] = 8;
            pupil_delta = -12;
            break;
        case ESR_EXPR_SLEEPY:
            open_delta[0] = -14;
            open_delta[1] = -14;
            break;
        case ESR_EXPR_EXCITED:
            open_delta[0] = 18;
            open_delta[1] = 18;
            pupil_delta = 26;
            break;
        case ESR_EXPR_EMBARRASSED:
            open_delta[0] = 18;
            open_delta[1] = 16;
            gaze_y_delta = 10;
            break;
        default:
            break;
        }
        minimum_open =
            expression == ESR_EXPR_SLEEPY ? 24 :
            expression == ESR_EXPR_JOY ? 44 : 62;
    } else if (index == 4U) {
        /* Sleep/wake: broad soft sockets span drowsy through fully alert. */
        switch (expression) {
        case ESR_EXPR_WARM:
            open_delta[0] = -8;
            open_delta[1] = -8;
            lower_delta[0] = 12;
            lower_delta[1] = 12;
            gaze_y_delta = 8;
            break;
        case ESR_EXPR_JOY:
            open_delta[0] = -26;
            open_delta[1] = -26;
            lower_delta[0] = 18;
            lower_delta[1] = 18;
            break;
        case ESR_EXPR_CONCERN:
            tilt_delta[0] = 12;
            tilt_delta[1] = -12;
            raise_delta[0] = 10;
            raise_delta[1] = 10;
            break;
        case ESR_EXPR_SURPRISE:
            open_delta[0] = 28;
            open_delta[1] = 28;
            height_delta[0] = 14;
            height_delta[1] = 14;
            raise_delta[0] = 16;
            raise_delta[1] = 16;
            break;
        case ESR_EXPR_THOUGHTFUL:
            open_delta[0] = 22;
            gaze_y_delta = -10;
            break;
        case ESR_EXPR_SKEPTICAL:
            open_delta[0] = 38;
            break;
        case ESR_EXPR_DETERMINED:
            open_delta[0] = 8;
            open_delta[1] = 8;
            upper_delta[0] = 12;
            upper_delta[1] = 12;
            break;
        case ESR_EXPR_SLEEPY:
            open_delta[0] = -38;
            open_delta[1] = -38;
            lower_delta[0] = 12;
            lower_delta[1] = 12;
            gaze_y_delta = 16;
            break;
        case ESR_EXPR_EXCITED:
            open_delta[0] = 24;
            open_delta[1] = 24;
            raise_delta[0] = 16;
            raise_delta[1] = 16;
            break;
        case ESR_EXPR_EMBARRASSED:
            open_delta[0] = -12;
            open_delta[1] = -16;
            lower_delta[0] = 18;
            lower_delta[1] = 20;
            gaze_y_delta = 14;
            break;
        default:
            break;
        }
        minimum_open = expression == ESR_EXPR_SLEEPY ? 18 : 46;
    } else if (index == 5U) {
        /*
         * Curious tilt: preserve deliberate asymmetry without ever turning
         * that asymmetry into an accidental monocular face.
         */
        switch (expression) {
        case ESR_EXPR_WARM:
            height_delta[1] = 10;
            lower_delta[0] = 8;
            lower_delta[1] = 8;
            break;
        case ESR_EXPR_JOY:
            open_delta[0] = -14;
            open_delta[1] = -14;
            lower_delta[0] = 14;
            lower_delta[1] = 14;
            break;
        case ESR_EXPR_CONCERN:
            height_delta[0] = 12;
            height_delta[1] = -6;
            gaze_x_delta = -12;
            gaze_y_delta = 8;
            break;
        case ESR_EXPR_SURPRISE:
            open_delta[0] = 18;
            open_delta[1] = 18;
            height_delta[0] = 14;
            height_delta[1] = 20;
            break;
        case ESR_EXPR_THOUGHTFUL:
            open_delta[0] = 38;
            width_delta[0] = 10;
            gaze_x_delta = -12;
            break;
        case ESR_EXPR_SKEPTICAL:
            open_delta[0] = 50;
            width_delta[0] = 14;
            height_delta[1] = 8;
            break;
        case ESR_EXPR_DETERMINED:
            open_delta[0] = 12;
            open_delta[1] = 12;
            upper_delta[0] = 10;
            upper_delta[1] = 10;
            break;
        case ESR_EXPR_SLEEPY:
            open_delta[0] = -14;
            open_delta[1] = -14;
            break;
        case ESR_EXPR_EXCITED:
            open_delta[0] = 20;
            open_delta[1] = 24;
            height_delta[1] = 10;
            break;
        case ESR_EXPR_EMBARRASSED:
            open_delta[0] = 18;
            open_delta[1] = 14;
            gaze_x_delta = 12;
            gaze_y_delta = 12;
            break;
        default:
            break;
        }
        minimum_open =
            expression == ESR_EXPR_SLEEPY ? 22 :
            expression == ESR_EXPR_JOY ? 42 : 60;
    } else if (index == 7U) {
        /* Cat optics: the slit remains present while the lids do the acting. */
        switch (expression) {
        case ESR_EXPR_WARM:
            open_delta[0] = -8;
            open_delta[1] = -8;
            lower_delta[0] = 10;
            lower_delta[1] = 10;
            pupil_delta = -8;
            break;
        case ESR_EXPR_JOY:
            open_delta[0] = -22;
            open_delta[1] = -22;
            lower_delta[0] = 16;
            lower_delta[1] = 16;
            break;
        case ESR_EXPR_CONCERN:
            tilt_delta[0] = 12;
            tilt_delta[1] = -12;
            gaze_y_delta = 8;
            break;
        case ESR_EXPR_SURPRISE:
            open_delta[0] = 18;
            open_delta[1] = 18;
            pupil_delta = 18;
            break;
        case ESR_EXPR_THOUGHTFUL:
            open_delta[0] = 38;
            gaze_x_delta = -10;
            break;
        case ESR_EXPR_SKEPTICAL:
            open_delta[0] = 52;
            width_delta[0] = 12;
            break;
        case ESR_EXPR_DETERMINED:
            open_delta[0] = 12;
            open_delta[1] = 12;
            upper_delta[0] = 10;
            upper_delta[1] = 10;
            pupil_delta = -16;
            break;
        case ESR_EXPR_SLEEPY:
            open_delta[0] = -24;
            open_delta[1] = -24;
            break;
        case ESR_EXPR_EXCITED:
            open_delta[0] = 22;
            open_delta[1] = 22;
            pupil_delta = 14;
            break;
        case ESR_EXPR_EMBARRASSED:
            open_delta[0] = 18;
            open_delta[1] = 14;
            lower_delta[0] = 12;
            lower_delta[1] = 16;
            gaze_x_delta = 10;
            gaze_y_delta = 12;
            break;
        default:
            break;
        }
        minimum_open =
            expression == ESR_EXPR_SLEEPY ? 18 :
            expression == ESR_EXPR_JOY ? 40 : 58;
    } else {
        return;
    }

    for (size_t eye = 0U; eye < 2U; ++eye) {
        pose->openness_q8[eye] = esr_clamp(
            pose->openness_q8[eye] +
                esr_expr((int16_t)open_delta[eye], weight),
            0,
            255);
        pose->width_scale_q8[eye] = esr_clamp(
            pose->width_scale_q8[eye] +
                esr_expr((int16_t)width_delta[eye], weight),
            176,
            354);
        pose->height_scale_q8[eye] = esr_clamp(
            pose->height_scale_q8[eye] +
                esr_expr((int16_t)height_delta[eye], weight),
            154,
            350);
        pose->upper_lid_q8[eye] = esr_clamp(
            pose->upper_lid_q8[eye] +
                esr_expr((int16_t)upper_delta[eye], weight),
            -64,
            164);
        pose->lower_lid_q8[eye] = esr_clamp(
            pose->lower_lid_q8[eye] +
                esr_expr((int16_t)lower_delta[eye], weight),
            -48,
            140);
        pose->brow_raise_q8[eye] = esr_clamp(
            pose->brow_raise_q8[eye] +
                esr_expr((int16_t)raise_delta[eye], weight),
            -96,
            160);
        pose->brow_tilt_q8[eye] = esr_clamp(
            pose->brow_tilt_q8[eye] +
                esr_expr((int16_t)tilt_delta[eye], weight),
            -128,
            128);
        pose->brow_arch_q8[eye] = esr_clamp(
            pose->brow_arch_q8[eye] +
                esr_expr((int16_t)arch_delta[eye], weight),
            -64,
            128);
        pose->eye_tilt_q8[eye] = esr_clamp(
            pose->brow_tilt_q8[eye] / 4 +
                (index == 5U
                     ? (eye == 0U ? -24 : 24)
                     : 0),
            -56,
            56);

        /*
         * Preserve the authored minimum only while the autonomous blink is
         * open.  Multiplying by blink_q8 retains a true full closure and a
         * continuous reopen instead of pinning a permanent bright slit.
         */
        const int32_t weighted_minimum =
            esr_mix(0, minimum_open, weight) *
            pose->blink_q8 / 256;
        if (pose->openness_q8[eye] < weighted_minimum) {
            pose->openness_q8[eye] = weighted_minimum;
        }
    }
    pose->gaze_x_q8 = esr_clamp(
        pose->gaze_x_q8 +
            esr_expr((int16_t)gaze_x_delta, weight),
        -256,
        256);
    pose->gaze_y_q8 = esr_clamp(
        pose->gaze_y_q8 +
            esr_expr((int16_t)gaze_y_delta, weight),
        -224,
        224);
    pose->pupil_scale_q8 = esr_clamp(
        pose->pupil_scale_q8 +
            esr_expr((int16_t)pupil_delta, weight),
        150,
        344);
}

bool face_eye_study_redux_resolve(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_eye_study_redux_pose_t *pose)
{
    if (!esr_valid_profile(profile) ||
        key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    pose->source = *key;
    pose->input_signature = esr_key_signature(key);

    const size_t index = esr_style_index(profile);
    const esr_style_t *style = &ESR_STYLES[index];
    const uint8_t expression =
        key->stage_expression < ESR_EXPRESSION_COUNT
            ? key->stage_expression
            : ESR_EXPR_NEUTRAL;
    const uint8_t weight =
        key->stage_expression < ESR_EXPRESSION_COUNT
            ? key->expression_weight
            : 0U;
    const esr_expression_t *target = &ESR_EXPRESSIONS[expression];
    const uint32_t time_ms = sample_clock / 16U;

    pose->left_center_x_q8 = style->left_x * 256;
    pose->right_center_x_q8 = style->right_x * 256;
    pose->center_y_q8 = style->center_y * 256;

    int32_t auto_x;
    int32_t auto_y;
    esr_autonomous_gaze(
        style, index, time_ms,
        &auto_x, &auto_y, &pose->saccade_active);
    const int32_t autonomy =
        esr_clamp(300 - key->attention, 48, 256);
    auto_x = auto_x * autonomy / 256;
    auto_y = auto_y * autonomy / 256;
    pose->gaze_x_q8 = esr_clamp(
        key->controls.look_x * 2 +
            key->head_yaw +
            key->body_lean_x / 2 +
            esr_expr(target->gaze_x, weight) +
            auto_x,
        -256,
        256);
    pose->gaze_y_q8 = esr_clamp(
        key->controls.look_y * 2 +
            key->head_pitch +
            key->body_lean_y / 2 +
            esr_expr(target->gaze_y, weight) +
            auto_y,
        -224,
        224);

    pose->blink_q8 =
        esr_blink_q8(style, index, key, time_ms);
    const bool speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U;
    pose->speech_energy_q8 =
        speaking ? key->audio_level : 0;
    const int32_t speech_open =
        speaking ? key->audio_level / 6 - 10 : 0;
    const int32_t start_anticipation =
        key->speech_phase == FACE_SPEECH_STARTING ? 34 : 0;
    const int32_t ending_settle =
        key->speech_phase == FACE_SPEECH_ENDING ? -24 : 0;
    const int32_t speech_lid =
        speaking ? -key->audio_level / 9 : 0;
    const int32_t phase_lid =
        key->speech_phase == FACE_SPEECH_STARTING ? -26 :
        key->speech_phase == FACE_SPEECH_ENDING ? 24 : 0;
    pose->gaze_y_q8 = esr_clamp(
        pose->gaze_y_q8 -
            (speaking ? key->audio_level / 5 : 0) +
            (key->speech_phase == FACE_SPEECH_STARTING ? -9 : 0) +
            (key->speech_phase == FACE_SPEECH_ENDING ? 7 : 0),
        -224,
        224);

    const uint8_t requested_open[2] = {
        key->controls.eye_left_open,
        key->controls.eye_right_open,
    };
    const uint8_t squint[2] = {
        key->eye_left_squint,
        key->eye_right_squint,
    };
    const int16_t expression_open[2] = {
        target->open_left,
        target->open_right,
    };
    const int16_t expression_width[2] = {
        target->width_left,
        target->width_right,
    };
    const int16_t expression_height[2] = {
        target->height_left,
        target->height_right,
    };
    const int16_t expression_upper[2] = {
        target->lid_upper_left,
        target->lid_upper_right,
    };
    const int16_t expression_lower[2] = {
        target->lid_lower_left,
        target->lid_lower_right,
    };
    const int16_t expression_raise[2] = {
        target->brow_raise_left,
        target->brow_raise_right,
    };
    const int16_t expression_tilt[2] = {
        target->brow_tilt_left,
        target->brow_tilt_right,
    };
    const int16_t expression_arch[2] = {
        target->brow_arch_left,
        target->brow_arch_right,
    };
    const int8_t outer[2] = {
        key->brow_outer_left,
        key->brow_outer_right,
    };

    for (size_t eye = 0U; eye < 2U; ++eye) {
        int32_t openness =
            requested_open[eye] +
            esr_expr(expression_open[eye], weight) +
            speech_open + start_anticipation + ending_settle -
            squint[eye] / 2;
        if (index == 4U && !speaking) {
            /*
             * "Sleep / wake" is an acting range, not a permanently closed
             * default.  The sleepy stage direction below still closes the
             * lids strongly; neutral and conversational poses must retain
             * enough aperture to survive a 4:1 contact-scale reduction.
             */
            openness += 8;
        } else if (index == 4U && speaking) {
            openness += 16 + key->audio_level / 10;
        }
        if (index == 2U && speaking) {
            openness += key->audio_level / 7;
        }
        openness = esr_clamp(openness, 8, 255);
        pose->openness_q8[eye] =
            openness * pose->blink_q8 / 255;
        pose->width_scale_q8[eye] = esr_clamp(
            256 +
                esr_expr(expression_width[eye], weight) +
                (speaking ? key->audio_level / 12 : 0) +
                (key->speech_phase == FACE_SPEECH_STARTING ? 18 : 0) +
                (256 - pose->blink_q8) / 5,
            176,
            354);
        pose->height_scale_q8[eye] = esr_clamp(
            256 +
                esr_expr(expression_height[eye], weight),
            154,
            350);
        pose->upper_lid_q8[eye] = esr_clamp(
            esr_expr(expression_upper[eye], weight) +
                key->head_pitch / 3 +
                speech_lid + phase_lid,
            -64,
            164);
        pose->lower_lid_q8[eye] = esr_clamp(
            esr_expr(expression_lower[eye], weight) +
                squint[eye] / 4,
            -48,
            140);
        const int32_t dialogue_gain =
            index == 1U ? 3 : 2;
        pose->brow_raise_q8[eye] = esr_clamp(
            esr_expr(expression_raise[eye], weight) +
                key->controls.brow * dialogue_gain / 2 +
                key->brow_inner / 2 + outer[eye] / 3 +
                start_anticipation +
                (speaking ? key->audio_level / 9 : 0),
            -96,
            160);
        pose->brow_tilt_q8[eye] = esr_clamp(
            esr_expr(expression_tilt[eye], weight) +
                (eye == 0U ? key->brow_inner - outer[eye]
                           : outer[eye] - key->brow_inner) +
                (eye == 0U ? key->head_roll : -key->head_roll),
            -128,
            128);
        pose->brow_arch_q8[eye] = esr_clamp(
            esr_expr(expression_arch[eye], weight) +
                (speaking ? key->audio_level / 18 : 0),
            -64,
            128);
        pose->eye_tilt_q8[eye] = esr_clamp(
            pose->brow_tilt_q8[eye] / 4 +
                (index == 5U
                     ? (eye == 0U ? -24 : 24)
                     : 0),
            -56,
            56);
    }

    if (index == 5U) {
        const int32_t curiosity =
            esr_abs(pose->gaze_x_q8) / 12 +
            esr_abs(key->head_roll) / 3;
        pose->width_scale_q8[0] = esr_clamp(
            pose->width_scale_q8[0] +
                (pose->gaze_x_q8 < 0 ? curiosity : -curiosity / 2),
            176,
            354);
        pose->height_scale_q8[1] = esr_clamp(
            pose->height_scale_q8[1] +
                (pose->gaze_x_q8 > 0 ? curiosity : -curiosity / 2),
            154,
            350);
    }
    pose->pupil_scale_q8 = esr_clamp(
        256 + esr_expr(target->pupil, weight) -
            key->affect_arousal / 5 +
            (speaking ? 32 - key->audio_level / 5 : 0) +
            (255 - key->attention) / 7,
        150,
        344);
    pose->parallax_q8 =
        index == 3U ? 384 :
        index == 7U ? 300 :
        index == 0U ? 280 : 236;
    pose->expression = expression;
    pose->expression_weight = weight;
    pose->attention = key->attention;
    esr_apply_character_acting(
        index, expression, weight, pose);
    return true;
}

static bool esr_shape_inside(
    uint8_t shape,
    int32_t dx,
    int32_t dy,
    int32_t rx,
    int32_t ry)
{
    if (rx <= 0 || ry <= 0 ||
        esr_abs(dx) > rx || esr_abs(dy) > ry) {
        return false;
    }
    if (shape == ESR_SHAPE_ROUNDRECT) {
        const int32_t corner = (rx < ry ? rx : ry) * 3 / 5;
        const int32_t qx =
            esr_abs(dx) - (rx - corner);
        const int32_t qy =
            esr_abs(dy) - (ry - corner);
        if (qx <= 0 || qy <= 0) {
            return true;
        }
        return (int64_t)qx * qx +
                   (int64_t)qy * qy <=
               (int64_t)corner * corner;
    }
    const bool ellipse =
        (int64_t)dx * dx * ry * ry +
            (int64_t)dy * dy * rx * rx <=
        (int64_t)rx * rx * ry * ry;
    if (!ellipse || shape == ESR_SHAPE_ELLIPSE) {
        return ellipse;
    }
    /*
     * Pointed eyes intersect the ellipse with a relaxed diamond.  The
     * ellipse keeps the upper/lower arcs organic; the diamond pins both
     * corners without producing a polygonal aperture.
     */
    return (int64_t)esr_abs(dx) * 5 +
               (int64_t)esr_abs(dy) * rx * 4 / ry <=
           (int64_t)rx * 6;
}

static bool esr_eye_sample_inside(
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t sample_x_q2,
    int32_t sample_y_q2,
    int32_t inset_q2)
{
    const int32_t center_x_q2 =
        (eye == 0U ? pose->left_center_x_q8
                   : pose->right_center_x_q8) /
        64;
    const int32_t center_y_q2 = pose->center_y_q8 / 64;
    int32_t dx = sample_x_q2 - center_x_q2;
    int32_t dy = sample_y_q2 - center_y_q2;
    dy -= dx * pose->eye_tilt_q8[eye] / 256;
    int32_t rx =
        style->radius_x * 4 *
        pose->width_scale_q8[eye] / 256 -
        inset_q2;
    int32_t ry =
        style->radius_y * 4 *
        pose->height_scale_q8[eye] / 256 -
        inset_q2;
    if (!esr_shape_inside(style->shape, dx, dy, rx, ry)) {
        return false;
    }

    const int32_t close =
        256 - esr_clamp(pose->openness_q8[eye], 0, 256);
    int32_t upper =
        -ry + close * 307 * ry / 65536 +
        pose->upper_lid_q8[eye] * ry / 256;
    int32_t lower =
        ry - close * 205 * ry / 65536 -
        pose->lower_lid_q8[eye] * ry / 256;
    const int32_t lid_slope =
        pose->brow_tilt_q8[eye] / 2;
    upper += dx * lid_slope / 256;
    lower -= dx * lid_slope / 768;

    /*
     * The two soft-window characters use a rounded-rectangle silhouette for
     * identity, but their eyelid aperture must remain organic.  Curve both
     * lid edges inward toward the corners; stage direction changes that
     * curvature continuously, so joy is a smile arc and surprise stays open
     * and round instead of looking like a cropped square.
     */
    if (style == &ESR_STYLES[1] ||
        style == &ESR_STYLES[4]) {
        int32_t target_curve_q8 = 176;
        switch (pose->expression) {
        case ESR_EXPR_WARM:
            target_curve_q8 = 224;
            break;
        case ESR_EXPR_JOY:
            target_curve_q8 = 292;
            break;
        case ESR_EXPR_CONCERN:
            target_curve_q8 = 218;
            break;
        case ESR_EXPR_SURPRISE:
            target_curve_q8 = 88;
            break;
        case ESR_EXPR_THOUGHTFUL:
            target_curve_q8 = 232;
            break;
        case ESR_EXPR_SKEPTICAL:
            target_curve_q8 = 250;
            break;
        case ESR_EXPR_DETERMINED:
            target_curve_q8 = 270;
            break;
        case ESR_EXPR_SLEEPY:
            target_curve_q8 = 238;
            break;
        case ESR_EXPR_EXCITED:
            target_curve_q8 = 112;
            break;
        case ESR_EXPR_EMBARRASSED:
            target_curve_q8 = 260;
            break;
        default:
            break;
        }
        const int32_t curve_q8 = esr_mix(
            176, target_curve_q8, pose->expression_weight);
        const int32_t denominator =
            rx > 0 ? rx * 1536 : 1536;
        const int32_t edge_curve =
            (int32_t)(
                (int64_t)dx * dx * curve_q8 /
                denominator);
        upper += edge_curve;
        lower -= edge_curve * 3 / 4;
    }
    return dy >= upper && dy <= lower;
}

static void esr_draw_eye_layer(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t inset_q2,
    int32_t extra_q2,
    uint16_t color,
    uint32_t alpha_scale,
    bool antialias)
{
    const int32_t center_x =
        (eye == 0U ? pose->left_center_x_q8
                   : pose->right_center_x_q8) /
        256;
    const int32_t center_y = pose->center_y_q8 / 256;
    const int32_t rx =
        style->radius_x *
            pose->width_scale_q8[eye] / 256 +
        extra_q2 / 4 + 3;
    const int32_t ry =
        style->radius_y *
            pose->height_scale_q8[eye] / 256 +
        extra_q2 / 4 + 3;
    for (int32_t y = center_y - ry; y <= center_y + ry; ++y) {
        for (int32_t x = center_x - rx; x <= center_x + rx; ++x) {
            if (!antialias) {
                if (esr_eye_sample_inside(
                        style, pose, eye,
                        x * 4 + 2, y * 4 + 2,
                        inset_q2 - extra_q2)) {
                    esr_put(canvas, x, y, color, alpha_scale);
                }
                continue;
            }
            uint32_t samples = 0U;
            for (int32_t sy = 1; sy <= 3; sy += 2) {
                for (int32_t sx = 1; sx <= 3; sx += 2) {
                    samples += esr_eye_sample_inside(
                        style, pose, eye,
                        x * 4 + sx,
                        y * 4 + sy,
                        inset_q2 - extra_q2);
                }
            }
            if (samples > 0U) {
                esr_put(
                    canvas, x, y, color,
                    samples * alpha_scale / 4U);
            }
        }
    }
}

static void esr_draw_clipped_ellipse(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t cx,
    int32_t cy,
    int32_t radius_x,
    int32_t radius_y,
    uint16_t color,
    uint32_t alpha_scale)
{
    const bool antialias = radius_x <= 3 || radius_y <= 3;
    for (int32_t y = cy - radius_y - 1;
         y <= cy + radius_y + 1;
         ++y) {
        for (int32_t x = cx - radius_x - 1;
             x <= cx + radius_x + 1;
             ++x) {
            if (!antialias) {
                const int32_t dx = x - cx;
                const int32_t dy = y - cy;
                const bool in_ellipse =
                    (int64_t)dx * dx * radius_y * radius_y +
                        (int64_t)dy * dy * radius_x * radius_x <=
                    (int64_t)radius_x * radius_x *
                        radius_y * radius_y;
                if (in_ellipse &&
                    esr_eye_sample_inside(
                        style, pose, eye,
                        x * 4 + 2, y * 4 + 2,
                        style->outline_px * 4)) {
                    esr_put(canvas, x, y, color, alpha_scale);
                }
                continue;
            }
            uint32_t samples = 0U;
            for (int32_t sy = 1; sy <= 3; sy += 2) {
                for (int32_t sx = 1; sx <= 3; sx += 2) {
                    const int32_t dx = x * 4 + sx - cx * 4;
                    const int32_t dy = y * 4 + sy - cy * 4;
                    const bool in_iris =
                        (int64_t)dx * dx *
                                (radius_y * 4) *
                                (radius_y * 4) +
                            (int64_t)dy * dy *
                                (radius_x * 4) *
                                (radius_x * 4) <=
                        (int64_t)(radius_x * 4) *
                            (radius_x * 4) *
                            (radius_y * 4) *
                            (radius_y * 4);
                    samples +=
                        in_iris &&
                        esr_eye_sample_inside(
                            style, pose, eye,
                            x * 4 + sx,
                            y * 4 + sy,
                            style->outline_px * 4);
                }
            }
            if (samples > 0U) {
                esr_put(
                    canvas, x, y, color,
                    samples * alpha_scale / 4U);
            }
        }
    }
}

static void esr_iris_center(
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t *cx,
    int32_t *cy)
{
    const int32_t anchor_x =
        (eye == 0U ? pose->left_center_x_q8
                   : pose->right_center_x_q8) /
        256;
    const int32_t anchor_y = pose->center_y_q8 / 256;
    int32_t gaze_x =
        pose->gaze_x_q8 * style->travel_x *
            pose->parallax_q8 / (256 * 256);
    int32_t gaze_y =
        pose->gaze_y_q8 * style->travel_y *
            pose->parallax_q8 / (256 * 256);

    if (esr_is_character_style(style)) {
        /*
         * Clamp the whole concentric iris assembly, not its individual
         * layers.  Every layer therefore shares one center, both eyes keep a
         * coherent gaze, and a high-look cue cannot shear the pupil against
         * a socket edge or make one eye vanish.
         */
        const int32_t rx =
            style->radius_x *
                pose->width_scale_q8[eye] / 256 -
            style->outline_px;
        const int32_t ry =
            style->radius_y *
                pose->height_scale_q8[eye] / 256 -
            style->outline_px;
        const int32_t nominal_iris =
            style->iris_radius *
            pose->pupil_scale_q8 / 256;
        const int32_t max_x = esr_clamp(
            rx - nominal_iris - 2, 1, style->travel_x);
        const int32_t max_y = esr_clamp(
            ry - nominal_iris * 3 / 4 - 2,
            1,
            style->travel_y);
        gaze_x = esr_clamp(gaze_x, -max_x, max_x);
        gaze_y = esr_clamp(gaze_y, -max_y, max_y);
    }
    *cx = anchor_x + gaze_x;
    *cy = anchor_y + gaze_y;
}

static void esr_draw_iris_layer(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t radius_x,
    int32_t radius_y,
    uint16_t color,
    uint32_t alpha_scale)
{
    int32_t cx;
    int32_t cy;
    esr_iris_center(style, pose, eye, &cx, &cy);
    esr_draw_clipped_ellipse(
        canvas, style, pose, eye, cx, cy,
        radius_x, radius_y, color, alpha_scale);
}

static void esr_draw_highlight(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t rendered_radius,
    uint32_t alpha_scale)
{
    if (pose->openness_q8[eye] < 52 ||
        rendered_radius < 5 ||
        alpha_scale == 0U) {
        return;
    }
    int32_t cx;
    int32_t cy;
    esr_iris_center(style, pose, eye, &cx, &cy);
    const int32_t radius =
        rendered_radius >= 12 ? 3 : 2;
    esr_draw_clipped_ellipse(
        canvas, style, pose, eye,
        cx - rendered_radius / 3,
        cy - rendered_radius / 3,
        radius, radius,
        style->highlight, 27U * alpha_scale / 32U);
    if (style->iris_kind == ESR_IRIS_DEEP) {
        esr_draw_clipped_ellipse(
            canvas, style, pose, eye,
            cx + rendered_radius / 3,
            cy + rendered_radius / 4,
            1, 1, style->highlight,
            18U * alpha_scale / 32U);
    }
}

static int32_t esr_center_aperture_height(
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t inset_px)
{
    const int32_t ry_q2 =
        style->radius_y * 4 *
            pose->height_scale_q8[eye] / 256 -
        inset_px * 4;
    if (ry_q2 <= 0) {
        return 0;
    }
    const int32_t close =
        256 - esr_clamp(pose->openness_q8[eye], 0, 256);
    const int32_t upper_q2 =
        -ry_q2 + close * 307 * ry_q2 / 65536 +
        pose->upper_lid_q8[eye] * ry_q2 / 256;
    const int32_t lower_q2 =
        ry_q2 - close * 205 * ry_q2 / 65536 -
        pose->lower_lid_q8[eye] * ry_q2 / 256;
    return lower_q2 > upper_q2
        ? (lower_q2 - upper_q2 + 3) / 4
        : 0;
}

static void esr_draw_brow(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye)
{
    const int32_t center_x =
        (eye == 0U ? style->left_x : style->right_x);
    const int32_t center_y =
        style->center_y - style->radius_y - style->brow_gap -
        pose->brow_raise_q8[eye] / 16;
    const int32_t half = style->brow_length;
    const int32_t tilt =
        pose->brow_tilt_q8[eye] * half / 256;
    const int32_t arch =
        pose->brow_arch_q8[eye] / 12;
    const uint32_t thickness =
        style->brow_px < 4U ? 4U : style->brow_px;
    if (esr_is_character_style(style)) {
        esr_tapered_curve(
            canvas,
            center_x - half,
            center_y - tilt,
            center_x,
            center_y - arch,
            center_x + half,
            center_y + tilt,
            (int32_t)thickness + 2,
            style->brow,
            32U);
    } else {
        esr_curve(
            canvas,
            center_x - half,
            center_y - tilt,
            center_x,
            center_y - arch,
            center_x + half,
            center_y + tilt,
            thickness,
            style->brow,
            32U);
    }
}

static void esr_draw_closed_lid(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye,
    int32_t aperture_height)
{
    /*
     * At contact scale an aperture below roughly five pixels cannot carry a
     * stable iris.  Keep the socket, then key an authored lid stroke into it.
     * The stroke fades in before the iris vanishes, so neither topology nor
     * emotion pops at the hand-off.
     */
    if (aperture_height > 16) {
        return;
    }
    const int32_t center_x =
        eye == 0U ? style->left_x : style->right_x;
    int32_t center_y = style->center_y + style->radius_y / 8;
    const int32_t half =
        style->radius_x *
        pose->width_scale_q8[eye] / 280;
    int32_t tilt =
        pose->eye_tilt_q8[eye] * half / 256;
    int32_t target_center_curve = 1;
    int32_t target_left_offset = 0;
    int32_t target_right_offset = 0;
    int32_t target_center_y_offset = 0;
    int32_t target_tilt_offset = 0;

    switch (pose->expression) {
    case ESR_EXPR_WARM:
        target_center_curve = -3;
        target_left_offset = 1;
        target_right_offset = 1;
        break;
    case ESR_EXPR_JOY:
        target_center_curve = -6;
        target_left_offset = 3;
        target_right_offset = 3;
        target_center_y_offset = 1;
        break;
    case ESR_EXPR_CONCERN:
        target_center_curve = 4;
        target_left_offset = -1;
        target_right_offset = -1;
        break;
    case ESR_EXPR_THOUGHTFUL:
        target_center_curve = eye == 0U ? 2 : -2;
        target_tilt_offset = eye == 0U ? 2 : -1;
        break;
    case ESR_EXPR_SKEPTICAL:
        target_center_curve = eye == 0U ? 1 : -3;
        target_tilt_offset = eye == 0U ? -3 : 2;
        break;
    case ESR_EXPR_DETERMINED:
        target_center_curve = -1;
        target_tilt_offset = eye == 0U ? 4 : -4;
        break;
    case ESR_EXPR_SLEEPY:
        target_center_curve = 1;
        target_center_y_offset = 2;
        tilt /= 2;
        break;
    case ESR_EXPR_EXCITED:
        target_center_curve = -4;
        target_left_offset = 2;
        target_right_offset = 2;
        break;
    case ESR_EXPR_EMBARRASSED:
        target_center_curve = -3;
        target_left_offset = eye == 0U ? 1 : 3;
        target_right_offset = eye == 0U ? 3 : 1;
        target_center_y_offset = 1;
        break;
    default:
        break;
    }
    const size_t style_index =
        (size_t)(style - ESR_STYLES);
    if (style_index == 1U) {
        /* Broad dialogue lids, with a pronounced smile/resolve silhouette. */
        if (pose->expression == ESR_EXPR_JOY) {
            target_center_curve -= 1;
        } else if (pose->expression == ESR_EXPR_CONCERN) {
            target_center_curve += 1;
        } else if (pose->expression == ESR_EXPR_DETERMINED) {
            target_tilt_offset += eye == 0U ? 2 : -2;
        }
    } else if (style_index == 3U) {
        /* A compact camera-shutter closure around the deep iris. */
        target_center_curve =
            target_center_curve * 3 / 4;
    } else if (style_index == 4U) {
        /* Pillow-soft arcs exaggerate sleeping and smiling without a bar. */
        if (pose->expression == ESR_EXPR_JOY) {
            target_center_curve -= 2;
        } else if (pose->expression == ESR_EXPR_SLEEPY) {
            target_center_curve += 2;
            target_center_y_offset += 1;
        }
    } else if (style_index == 5U) {
        /* Curious eyes retain opposite, intentional inflections when shut. */
        target_tilt_offset += eye == 0U ? -1 : 1;
        if (pose->expression == ESR_EXPR_THOUGHTFUL ||
            pose->expression == ESR_EXPR_SKEPTICAL) {
            target_center_curve += eye == 0U ? 1 : -1;
        }
    } else if (style_index == 7U) {
        /* Feline lids form a sharper smile and stronger hunting squint. */
        if (pose->expression == ESR_EXPR_WARM ||
            pose->expression == ESR_EXPR_JOY) {
            target_center_curve -= 1;
        } else if (pose->expression == ESR_EXPR_DETERMINED) {
            target_tilt_offset += eye == 0U ? 2 : -2;
        }
    }
    const int32_t center_curve = esr_mix(
        1, target_center_curve, pose->expression_weight);
    const int32_t left_offset = esr_mix(
        0, target_left_offset, pose->expression_weight);
    const int32_t right_offset = esr_mix(
        0, target_right_offset, pose->expression_weight);
    center_y += esr_mix(
        0, target_center_y_offset, pose->expression_weight);
    tilt += esr_mix(
        0, target_tilt_offset, pose->expression_weight);
    const uint32_t alpha = (uint32_t)esr_clamp(
        6 + (16 - aperture_height) * 4,
        6,
        32);
    const uint32_t base_thickness =
        style->outline_px + 1U < 4U
            ? 4U
            : style->outline_px + 1U;
    const uint32_t thickness =
        base_thickness +
        (uint32_t)esr_clamp(
            (10 - aperture_height + 2) / 3,
            0,
            3);
    const uint16_t lid_color =
        esr_blend565(style->outline, style->sclera, 11U);
    if (esr_is_character_style(style)) {
        esr_tapered_curve(
            canvas,
            center_x - half,
            center_y - tilt + left_offset,
            center_x,
            center_y + center_curve,
            center_x + half,
            center_y + tilt + right_offset,
            (int32_t)thickness + 1,
            lid_color,
            alpha);
    } else {
        esr_curve(
            canvas,
            center_x - half,
            center_y - tilt + left_offset,
            center_x,
            center_y + center_curve,
            center_x + half,
            center_y + tilt + right_offset,
            thickness,
            lid_color,
            alpha);
    }
}

static void esr_draw_background(
    esr_canvas_t *canvas,
    size_t index,
    const esr_style_t *style)
{
    esr_gradient(canvas, style->bg_top, style->bg_bottom);
    if (index == 0U) {
        esr_line(canvas, 80, 10, 80, 109, 1, style->panel, 18U);
        esr_line(canvas, 10, 60, 149, 60, 1, style->panel, 18U);
        for (int32_t x = 18; x <= 142; x += 31) {
            esr_line(canvas, x, 105, x + 8, 105, 1, style->accent, 24U);
        }
    } else if (index == 1U) {
        esr_rect(canvas, 8, 13, 144, 91, style->panel, 10U);
        esr_line(canvas, 18, 101, 142, 101, 1, style->accent, 15U);
    } else if (index == 2U) {
        esr_rect(canvas, 7, 16, 146, 87, style->panel, 10U);
        esr_line(canvas, 15, 29, 145, 29, 1, style->accent, 18U);
        esr_line(canvas, 15, 92, 145, 92, 1, style->accent, 12U);
    } else if (index == 3U) {
        esr_rect(canvas, 5, 8, 6, 104, style->panel, 18U);
        esr_rect(canvas, 149, 8, 6, 104, style->panel, 18U);
        esr_line(canvas, 18, 102, 142, 102, 1, style->accent, 13U);
    } else if (index == 4U) {
        static const uint8_t STAR_X[12] = {
            14, 28, 41, 61, 76, 92, 109, 124, 144, 34, 133, 18,
        };
        static const uint8_t STAR_Y[12] = {
            14, 28, 9, 21, 13, 27, 10, 24, 16, 105, 102, 88,
        };
        for (size_t star = 0U; star < 12U; ++star) {
            esr_put(
                canvas, STAR_X[star], STAR_Y[star],
                style->accent, star % 3U == 0U ? 24U : 13U);
        }
    } else if (index == 5U) {
        esr_line(canvas, 13, 99, 147, 99, 2, style->panel, 18U);
        esr_disc(canvas, 20, 19, 4, style->accent, 13U);
        esr_disc(canvas, 140, 23, 3, style->accent, 13U);
    } else if (index == 6U) {
        esr_rect(canvas, 8, 12, 144, 94, style->panel, 32U);
        esr_rect(canvas, 12, 16, 136, 86, style->bg_bottom, 32U);
        esr_disc(canvas, 14, 14, 2, style->accent, 24U);
        esr_disc(canvas, 146, 14, 2, style->accent, 24U);
        esr_disc(canvas, 14, 104, 2, style->accent, 24U);
        esr_disc(canvas, 146, 104, 2, style->accent, 24U);
    } else {
        esr_curve(
            canvas, 9, 92, 80, 112, 151, 92,
            2, style->panel, 18U);
        esr_line(canvas, 15, 25, 38, 12, 2, style->panel, 18U);
        esr_line(canvas, 145, 25, 122, 12, 2, style->panel, 18U);
    }
}

static void esr_draw_dot_matrix(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose)
{
    const uint16_t dim = ESR_RGB565(35, 57, 48);
    const int32_t centers[2] = {
        style->left_x,
        style->right_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t open_rows = esr_clamp(
            (pose->openness_q8[eye] * 7 + 128) / 256,
            0,
            7);
        const int32_t half_columns = esr_clamp(
            pose->width_scale_q8[eye] * 4 / 256,
            3,
            4);
        const int32_t preferred_gaze_column =
            esr_clamp(pose->gaze_x_q8 / 86, -2, 2);
        const int32_t gaze_column = esr_clamp(
            preferred_gaze_column,
            -half_columns + 1,
            half_columns - 1);
        const int32_t lid_slope =
            pose->brow_tilt_q8[eye] / 58;
        const int32_t gaze_center_row =
            lid_slope * gaze_column / 5;
        const int32_t half_open_rows =
            open_rows > 0 ? (open_rows - 1) / 2 : 0;
        const int32_t gaze_row = esr_clamp(
            gaze_center_row +
                esr_clamp(pose->gaze_y_q8 / 112, -1, 1),
            gaze_center_row - half_open_rows,
            gaze_center_row + half_open_rows);
        for (int32_t row = -3; row <= 3; ++row) {
            for (int32_t column = -4; column <= 4; ++column) {
                const int32_t curved_row =
                    row - lid_slope * column / 5;
                const bool base_aperture =
                    open_rows > 0 &&
                    esr_abs(column) <= half_columns &&
                    esr_abs(curved_row) <= half_open_rows &&
                    column * column * 3 +
                            curved_row * curved_row * 5 <=
                        53;
                uint16_t base_color =
                    base_aperture ? style->sclera : dim;
                if (base_aperture &&
                    esr_abs(column - gaze_column) <= 1 &&
                    esr_abs(row - gaze_row) <= 1) {
                    base_color =
                        column == gaze_column &&
                                row == gaze_row
                            ? style->highlight
                            : style->iris;
                }
                /*
                 * Dot eyes need their acting in the pixels themselves:
                 * brows alone disappear in a 40x30 atlas tile.  Closed and
                 * smiling poses use a stable one-cell lid grammar while
                 * open poses retain a lid-aware, gazeable aperture.  Blend
                 * every cell from that base grammar instead of switching
                 * topology when a stage cue starts or ends.
                 */
                const bool smile =
                    pose->expression == ESR_EXPR_JOY ||
                    pose->expression == ESR_EXPR_WARM ||
                    pose->expression == ESR_EXPR_EMBARRASSED;
                const bool sleepy =
                    pose->expression == ESR_EXPR_SLEEPY;
                bool target_aperture = base_aperture;
                if (smile) {
                    const int32_t arc_row =
                        (pose->expression == ESR_EXPR_JOY ? -1 : 0) +
                        esr_abs(column) / 3;
                    target_aperture =
                        esr_abs(column) <= half_columns &&
                        row == arc_row;
                } else if (sleepy) {
                    target_aperture =
                        esr_abs(column) <= half_columns &&
                        row == 1;
                }

                uint16_t color = base_color;
                if (smile || sleepy) {
                    const uint16_t target_color =
                        target_aperture ? style->sclera : dim;
                    const uint32_t expression_alpha =
                        ((uint32_t)pose->expression_weight * 32U +
                         127U) /
                        255U;
                    color = esr_blend565(
                        base_color, target_color, expression_alpha);
                }
                /*
                 * A true blink is an independent continuous eyelid action.
                 * Keep one readable row at full closure without coupling it
                 * to the categorical stage-expression identifier.
                 */
                if (open_rows == 0) {
                    color =
                        esr_abs(column) <= half_columns &&
                                row == 0
                            ? style->sclera
                            : dim;
                }
                esr_rect(
                    canvas,
                    centers[eye] + column * 6 - 2,
                    style->center_y + row * 6 - 2,
                    5,
                    5,
                    color,
                    32U);
            }
        }
        esr_draw_brow(canvas, style, pose, eye);
    }
}

static void esr_draw_eye(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye)
{
    if (style->glow_px > 0U) {
        esr_draw_eye_layer(
            canvas, style, pose, eye,
            0,
            style->glow_px * 4,
            style->accent,
            7U,
            false);
    }
    esr_draw_eye_layer(
        canvas, style, pose, eye,
        0,
        0,
        style->outline,
        32U,
        true);
    esr_draw_eye_layer(
        canvas, style, pose, eye,
        style->outline_px * 4,
        0,
        style->sclera,
        32U,
        false);

    const int32_t iris_radius =
        style->iris_radius *
        pose->pupil_scale_q8 / 256;
    const int32_t aperture_height =
        esr_center_aperture_height(
            style, pose, eye, style->outline_px);
    /*
     * Keep the complete iris assembly inside a narrowing lid aperture.
     * Uniformly shrinking it costs fewer changing pixels than fading a
     * full-size disc, avoids a boolean visibility pop, and never leaves the
     * detached pupil crumbs seen in 40x30 contact sheets.
     */
    int32_t gaze_y =
        esr_abs(
            pose->gaze_y_q8 * style->travel_y *
                pose->parallax_q8 / (256 * 256));
    if (esr_is_character_style(style)) {
        int32_t clamped_x;
        int32_t clamped_y;
        esr_iris_center(
            style, pose, eye, &clamped_x, &clamped_y);
        (void)clamped_x;
        gaze_y = esr_abs(
            clamped_y - pose->center_y_q8 / 256) / 2;
    }
    const int32_t available_radius =
        (aperture_height - 6) / 2 - gaze_y;
    const int32_t rendered_radius =
        esr_clamp(available_radius, 0, iris_radius);
    /*
     * Grow the iris continuously from one pixel.  A former radius >= 8 gate
     * introduced roughly two hundred pixels per eye in a single frame.
     * Opacity-fading the whole disc is not equivalent: it touches every iris
     * pixel on every frame and makes an otherwise smooth blink look abrupt.
     * Radius growth changes only the boundary; tiny details fade in later.
     */
    const uint32_t iris_alpha = rendered_radius > 0 ? 32U : 0U;
    const uint32_t detail_alpha =
        (uint32_t)esr_clamp((rendered_radius - 5) * 32 / 5, 0, 32);
    if (iris_alpha > 0U) {
        const int32_t outer_radius = rendered_radius + 2;
        esr_draw_iris_layer(
            canvas, style, pose, eye,
            outer_radius,
            outer_radius,
            style->pupil,
            30U * iris_alpha / 32U);
        esr_draw_iris_layer(
            canvas, style, pose, eye,
            rendered_radius,
            rendered_radius,
            style->iris,
            iris_alpha);
        if (style->iris_kind == ESR_IRIS_DEEP) {
            esr_draw_iris_layer(
                canvas, style, pose, eye,
                esr_clamp(rendered_radius * 3 / 4, 1, rendered_radius),
                esr_clamp(rendered_radius * 3 / 4, 1, rendered_radius),
                style->iris_light,
                22U * iris_alpha / 32U);
        } else {
            esr_draw_iris_layer(
                canvas, style, pose, eye,
                esr_clamp(rendered_radius * 2 / 3, 1, rendered_radius),
                esr_clamp(rendered_radius * 2 / 3, 1, rendered_radius),
                style->iris_light,
                16U * iris_alpha / 32U);
        }
        int32_t pupil_x =
            style->pupil_radius *
            pose->pupil_scale_q8 / 256;
        int32_t pupil_y = pupil_x;
        if (style->iris_kind == ESR_IRIS_SLIT) {
            /*
             * A three-native-pixel slit can fall completely between samples
             * in a 4:1 contact reduction.  Five pixels remains unmistakably
             * feline while guaranteeing one dark keyed pixel at every gaze
             * position.
             */
            pupil_x = esr_clamp((pupil_x + 1) / 2, 2, 4);
            pupil_y = esr_clamp(rendered_radius * 4 / 5, 1, 14);
        }
        pupil_x = esr_clamp(pupil_x, 1, rendered_radius);
        pupil_y = esr_clamp(pupil_y, 1, rendered_radius);
        if (style->iris_kind == ESR_IRIS_SLIT) {
            /*
             * Keep the specular glint behind the slit.  Drawing it last could
             * erase the only contact-scale pupil sample at extreme gaze.
             */
            esr_draw_highlight(
                canvas, style, pose, eye,
                rendered_radius, detail_alpha);
        }
        esr_draw_iris_layer(
            canvas, style, pose, eye,
            pupil_x,
            pupil_y,
            style->pupil,
            detail_alpha);
        if (style->iris_kind != ESR_IRIS_SLIT) {
            esr_draw_highlight(
                canvas, style, pose, eye,
                rendered_radius, detail_alpha);
        }
    }
    esr_draw_closed_lid(
        canvas, style, pose, eye, aperture_height);
}

static void esr_draw_corner_acting(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye)
{
    const uint32_t alpha =
        (uint32_t)esr_clamp(
            (pose->openness_q8[eye] - 24) * 25 / 72,
            0,
            25);
    if (alpha == 0U) {
        return;
    }
    const int32_t center_x =
        eye == 0U ? style->left_x : style->right_x;
    const int32_t side = eye == 0U ? -1 : 1;
    const int32_t outer_x =
        center_x + side *
            style->radius_x *
            pose->width_scale_q8[eye] / 256;
    const int32_t y =
        style->center_y +
        side * pose->eye_tilt_q8[eye] *
            style->radius_x / 256;
    const int32_t lift =
        pose->lower_lid_q8[eye] / 24;
    esr_line(
        canvas,
        outer_x - side * 5,
        y,
        outer_x + side * 4,
        y - lift,
        4,
        style->accent,
        alpha);
}

static void esr_draw_parallax_rays(
    esr_canvas_t *canvas,
    const esr_style_t *style,
    const face_eye_study_redux_pose_t *pose,
    size_t eye)
{
    if (style->iris_kind != ESR_IRIS_DEEP) {
        return;
    }
    const uint32_t alpha =
        (uint32_t)esr_clamp(
            (pose->openness_q8[eye] - 28) * 18 / 76,
            0,
            18);
    if (alpha == 0U) {
        return;
    }
    int32_t cx;
    int32_t cy;
    esr_iris_center(style, pose, eye, &cx, &cy);
    static const int8_t RAY_X[8] = {
        -7, -4, 0, 4, 7, 4, 0, -4,
    };
    static const int8_t RAY_Y[8] = {
        0, -6, -8, -6, 0, 6, 8, 6,
    };
    for (size_t ray = 0U; ray < 8U; ++ray) {
        esr_draw_clipped_ellipse(
            canvas, style, pose, eye,
            cx + RAY_X[ray] * 3 / 4,
            cy + RAY_Y[ray] * 3 / 4,
            RAY_X[ray] == 0 ? 1 : 2,
            RAY_Y[ray] == 0 ? 1 : 2,
            style->iris_light, alpha);
    }
}

static void esr_landmarks(
    const esr_style_t *style,
    face_eye_study_redux_landmarks_t *landmarks)
{
    landmarks->face = (face_eye_study_redux_bounds_t){
        8, 8, 144, 103,
    };
    const uint16_t eye_width =
        (uint16_t)(style->radius_x * 2 + 16);
    const uint16_t eye_height =
        (uint16_t)(style->radius_y * 2 + style->brow_gap + 22);
    landmarks->left_eye = (face_eye_study_redux_bounds_t){
        (uint16_t)esr_clamp(
            style->left_x - eye_width / 2,
            1,
            FACE_EYE_STUDY_REDUX_WIDTH - 2),
        (uint16_t)esr_clamp(
            style->center_y - style->radius_y -
                style->brow_gap - 16,
            1,
            FACE_EYE_STUDY_REDUX_HEIGHT - 2),
        eye_width,
        eye_height,
    };
    landmarks->right_eye = landmarks->left_eye;
    landmarks->right_eye.x = (uint16_t)esr_clamp(
        style->right_x - eye_width / 2,
        1,
        FACE_EYE_STUDY_REDUX_WIDTH - 2);
}

bool face_eye_study_redux_render_resolved(
    face_eye_study_redux_profile_t profile,
    const face_eye_study_redux_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_eye_study_redux_landmarks_t *landmarks)
{
    if (!esr_valid_profile(profile) ||
        pose == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_EYE_STUDY_REDUX_PIXEL_COUNT) {
        return false;
    }
    const size_t index = esr_style_index(profile);
    const esr_style_t *style = &ESR_STYLES[index];
    esr_canvas_t canvas = {.pixels = rgb565};
    esr_draw_background(&canvas, index, style);
    if (style->matrix != 0U) {
        esr_draw_dot_matrix(&canvas, style, pose);
    } else {
        for (size_t eye = 0U; eye < 2U; ++eye) {
            esr_draw_eye(&canvas, style, pose, eye);
            esr_draw_parallax_rays(
                &canvas, style, pose, eye);
            esr_draw_corner_acting(
                &canvas, style, pose, eye);
            esr_draw_brow(&canvas, style, pose, eye);
        }
    }
    if (landmarks != NULL) {
        esr_landmarks(style, landmarks);
    }
    return true;
}

bool face_eye_study_redux_render_checked(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_eye_study_redux_landmarks_t *landmarks)
{
    face_eye_study_redux_pose_t pose;
    return face_eye_study_redux_resolve(
               profile, render_key, sample_clock, &pose) &&
           face_eye_study_redux_render_resolved(
               profile, &pose, rgb565,
               pixel_capacity, landmarks);
}

bool face_eye_study_redux_render(
    face_eye_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    return face_eye_study_redux_render_checked(
        profile,
        render_key,
        sample_clock,
        rgb565,
        pixel_capacity,
        NULL);
}
