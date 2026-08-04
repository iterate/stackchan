#include "face_closeup_toon_actors.h"

#include "face_pose.h"
#include "face_stage.h"

#include <string.h>

#define CT_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    CT_SAFE = 4,
    CT_EXPRESSION_COUNT = 11,
};

typedef enum {
    CT_EYE_PANEL = 0,
    CT_EYE_CRESCENT,
    CT_EYE_GOGGLE,
    CT_EYE_CAT,
    CT_EYE_MANGA,
    CT_EYE_VISOR,
    CT_EYE_CREATURE,
    CT_EYE_SHIELD,
    CT_EYE_ELDER,
    CT_EYE_MECHANIC,
} ct_eye_kind_t;

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t mouth_kind;
    uint8_t mouth_grammar;
    uint8_t eye_kind;
    uint8_t ops;
    int8_t eye_x[2];
    int8_t eye_y;
    uint8_t eye_w;
    uint8_t eye_h;
    int8_t mouth_y;
    uint8_t mouth_max_w;
    uint8_t mouth_max_h;
} ct_actor_def_t;

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
} ct_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t consonant;
} ct_viseme_t;

typedef struct {
    uint16_t *pixels;
} ct_canvas_t;

static const ct_actor_def_t CT_ACTORS[FACE_CLOSEUP_TOON_COUNT] = {
    [FACE_CLOSEUP_TOON_BROW_DIALOGUE_DIRECTOR] = {
        "brow-dialogue-director",
        "Brow Dialogue Director",
        42U,
        FACE_CLOSEUP_TOON_MOUTH_CURVE,
        FACE_CLOSEUP_TOON_GRAMMAR_SIGNAL_RIBBON,
        CT_EYE_PANEL,
        11U,
        {51, 109},
        50,
        40U,
        30U,
        86,
        62U,
        27U,
    },
    [FACE_CLOSEUP_TOON_SLEEP_WAKE_DREAMER] = {
        "sleep-wake-dreamer",
        "Sleep / Wake Dreamer",
        43U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_DREAM_CUPID,
        CT_EYE_CRESCENT,
        12U,
        {54, 106},
        53,
        31U,
        29U,
        87,
        48U,
        27U,
    },
    [FACE_CLOSEUP_TOON_IRIS_PARALLAX_SCOUT] = {
        "iris-parallax-scout",
        "Iris Parallax Scout",
        44U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_SCOUT_GRIN,
        CT_EYE_GOGGLE,
        14U,
        {51, 109},
        50,
        35U,
        35U,
        87,
        52U,
        28U,
    },
    [FACE_CLOSEUP_TOON_CAT_OPTICS_FAMILIAR] = {
        "cat-optics-familiar",
        "Cat Optics Familiar",
        45U,
        FACE_CLOSEUP_TOON_MOUTH_MUZZLE,
        FACE_CLOSEUP_TOON_GRAMMAR_CAT_MUZZLE,
        CT_EYE_CAT,
        13U,
        {51, 109},
        54,
        35U,
        31U,
        89,
        48U,
        24U,
    },
    [FACE_CLOSEUP_TOON_M5_MANGA_LEAD] = {
        "m5-manga-lead",
        "M5 Manga Lead",
        46U,
        FACE_CLOSEUP_TOON_MOUTH_MANGA,
        FACE_CLOSEUP_TOON_GRAMMAR_MANGA_PETAL,
        CT_EYE_MANGA,
        15U,
        {52, 108},
        51,
        34U,
        43U,
        88,
        55U,
        28U,
    },
    [FACE_CLOSEUP_TOON_VGA_STAR_NAVIGATOR] = {
        "vga-star-navigator-performer",
        "VGA Star Navigator Performer",
        47U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_NAV_CONSOLE,
        CT_EYE_VISOR,
        13U,
        {52, 108},
        50,
        29U,
        29U,
        86,
        53U,
        27U,
    },
    [FACE_CLOSEUP_TOON_POCKET_RELAY_CREATURE] = {
        "pocket-relay-creature-performer",
        "Pocket Relay Creature Performer",
        48U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_RELAY_ELASTIC,
        CT_EYE_CREATURE,
        12U,
        {50, 110},
        48,
        33U,
        34U,
        88,
        58U,
        29U,
    },
    [FACE_CLOSEUP_TOON_EGA_QUEST_SQUIRE] = {
        "ega-quest-squire-performer",
        "EGA Quest Squire Performer",
        49U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_SQUIRE_FACET,
        CT_EYE_SHIELD,
        12U,
        {52, 108},
        52,
        29U,
        30U,
        89,
        51U,
        26U,
    },
    [FACE_CLOSEUP_TOON_VGA_ELDER_STORYTELLER] = {
        "vga-elder-storyteller",
        "VGA Elder Storyteller",
        50U,
        FACE_CLOSEUP_TOON_MOUTH_BEARD,
        FACE_CLOSEUP_TOON_GRAMMAR_ELDER_BEARD,
        CT_EYE_ELDER,
        14U,
        {51, 109},
        50,
        29U,
        28U,
        85,
        52U,
        25U,
    },
    [FACE_CLOSEUP_TOON_TALKIE_MOON_MECHANIC] = {
        "talkie-moon-mechanic",
        "Talkie Moon Mechanic",
        51U,
        FACE_CLOSEUP_TOON_MOUTH_CAVITY,
        FACE_CLOSEUP_TOON_GRAMMAR_MECHANIC_CROOK,
        CT_EYE_MECHANIC,
        15U,
        {51, 109},
        51,
        30U,
        30U,
        88,
        54U,
        27U,
    },
};

/*
 * Geometry, not hue, carries the primary emotion read.  The table drives
 * socket aperture, gaze, lids/brows, parented mouth corners, and posture.
 */
static const ct_expression_t CT_EXPRESSIONS[CT_EXPRESSION_COUNT] = {
    /* neutral */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0U},
    /* warm */
    {-2, -2, 3, 0, 1, -2, -2, 0, 0, 1, 5, 8, 8, 0, 0, -1, 0, 88U},
    /* joy */
    {-11, -11, 7, 0, 2, -3, -3, 0, 0, 9, 12, 13, 13, 0, 0, -3, 1, 226U},
    /* concern */
    {-2, 1, -2, -4, 4, -8, -8, 8, -8, -1, -5, -11, -10, -7, -4, 2, 1, 74U},
    /* surprise */
    {13, 13, 8, 0, -4, -10, -10, 0, 0, 15, -5, -4, -4, 0, 0, -4, -3, 0U},
    /* thoughtful */
    {-4, -11, 0, -11, -7, -5, 1, 5, -3, -2, -3, -5, 3, -7, -3, 1, 1, 32U},
    /* skeptical */
    {-14, -1, -3, 10, 0, -8, 2, 9, -8, -2, -5, -9, 6, 8, 3, 0, 0, 30U},
    /* determined */
    {-8, -8, 5, 0, 1, 3, 3, -9, 9, -1, 1, -7, -7, 0, 0, -2, 2, 18U},
    /* sleepy */
    {-18, -18, -7, -3, 7, 5, 5, 2, -2, 1, -5, 1, 1, -3, 0, 5, 2, 0U},
    /* excited */
    {11, 11, 11, 0, -5, -11, -11, 0, 0, 13, 13, 13, 13, 0, 0, -5, -2, 196U},
    /* embarrassed */
    {-7, -11, -4, 11, 6, -6, -8, 5, -5, -1, -3, 8, 3, 7, 4, 2, 0, 255U},
};

static const ct_viseme_t CT_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {232U, 166U, 24U, 0U, 70U, 62U, 3U},
    [FACE_VISEME_E] = {94U, 246U, 4U, 0U, 176U, 8U, 5U},
    [FACE_VISEME_I] = {54U, 224U, 2U, 0U, 128U, 6U, 7U},
    [FACE_VISEME_O] = {208U, 86U, 248U, 0U, 26U, 30U, 9U},
    [FACE_VISEME_U] = {120U, 60U, 255U, 0U, 14U, 26U, 11U},
    [FACE_VISEME_PP] = {3U, 162U, 18U, 255U, 0U, 0U, 13U},
    [FACE_VISEME_SS] = {44U, 244U, 2U, 34U, 248U, 0U, 15U},
    [FACE_VISEME_TH] = {78U, 192U, 16U, 0U, 112U, 255U, 17U},
    [FACE_VISEME_DD] = {88U, 182U, 12U, 0U, 204U, 86U, 19U},
    [FACE_VISEME_FF] = {36U, 208U, 6U, 60U, 255U, 0U, 21U},
    [FACE_VISEME_KK] = {140U, 186U, 34U, 0U, 44U, 84U, 23U},
    [FACE_VISEME_NN] = {52U, 174U, 16U, 12U, 104U, 56U, 25U},
    [FACE_VISEME_RR] = {110U, 144U, 134U, 0U, 44U, 36U, 27U},
    [FACE_VISEME_CH] = {84U, 216U, 22U, 0U, 156U, 24U, 29U},
    [FACE_VISEME_SIL] = {5U, 140U, 20U, 228U, 0U, 0U, 31U},
};

static int32_t ct_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t ct_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t ct_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t ct_wave(uint32_t sample_clock, uint32_t period)
{
    if (period < 2U) {
        return 0;
    }
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    const int32_t value = phase < half
        ? (int32_t)(phase * 254U / half) - 127
        : 127 - (int32_t)((phase - half) * 254U / half);
    return ct_clamp(value, -127, 127);
}

static bool ct_style_valid(face_closeup_toon_style_t style)
{
    return (unsigned)style < (unsigned)FACE_CLOSEUP_TOON_COUNT;
}

static void ct_put(ct_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x >= CT_SAFE && x < FACE_CLOSEUP_TOON_WIDTH - CT_SAFE &&
        y >= CT_SAFE && y < FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE) {
        canvas->pixels[
            (size_t)y * FACE_CLOSEUP_TOON_WIDTH + (size_t)x] = color;
    }
}

static void ct_clear(ct_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_CLOSEUP_TOON_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void ct_rect(
    ct_canvas_t *canvas,
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
        (int)ct_clamp(x, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE);
    const int right = (int)ct_clamp(
        x + width, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE);
    const int top =
        (int)ct_clamp(y, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE);
    const int bottom = (int)ct_clamp(
        y + height, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE);
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) {
            canvas->pixels[
                (size_t)yy * FACE_CLOSEUP_TOON_WIDTH + (size_t)xx] =
                color;
        }
    }
}

static void ct_line(
    ct_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = ct_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -ct_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ct_put(canvas, x0, y0, color);
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

static void ct_thick_line(
    ct_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    for (int offset = -radius; offset <= radius; ++offset) {
        ct_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
    }
}

static void ct_ellipse(
    ct_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    const int left = (int)ct_clamp(
        cx - rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int right = (int)ct_clamp(
        cx + rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int top = (int)ct_clamp(
        cy - ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int bottom = (int)ct_clamp(
        cy + ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int64_t limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t dx = x - cx;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= limit) {
                ct_put(canvas, x, y, color);
            }
        }
    }
}

static int32_t ct_edge(
    int ax, int ay, int bx, int by, int px, int py)
{
    return (int32_t)(px - ax) * (by - ay) -
        (int32_t)(py - ay) * (bx - ax);
}

static void ct_triangle(
    ct_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int left = (int)ct_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        CT_SAFE,
        FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int right = (int)ct_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        CT_SAFE,
        FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int top = (int)ct_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        CT_SAFE,
        FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int bottom = (int)ct_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        CT_SAFE,
        FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int32_t orientation = ct_edge(ax, ay, bx, by, cx, cy);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int32_t e0 = ct_edge(ax, ay, bx, by, x, y);
            const int32_t e1 = ct_edge(bx, by, cx, cy, x, y);
            const int32_t e2 = ct_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                ct_put(canvas, x, y, color);
            }
        }
    }
}

static void ct_quad(
    ct_canvas_t *canvas,
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
    ct_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    ct_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void ct_round_rect(
    ct_canvas_t *canvas,
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
    radius = (int)ct_clamp(radius, 0, width / 2);
    radius = (int)ct_clamp(radius, 0, height / 2);
    ct_rect(canvas, x + radius, y, width - radius * 2, height, color);
    ct_rect(canvas, x, y + radius, width, height - radius * 2, color);
    if (radius > 0) {
        ct_ellipse(canvas, x + radius, y + radius, radius, radius, color);
        ct_ellipse(
            canvas,
            x + width - radius - 1,
            y + radius,
            radius,
            radius,
            color);
        ct_ellipse(
            canvas,
            x + radius,
            y + height - radius - 1,
            radius,
            radius,
            color);
        ct_ellipse(
            canvas,
            x + width - radius - 1,
            y + height - radius - 1,
            radius,
            radius,
            color);
    }
}

static void ct_ring(
    ct_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    int thickness,
    uint16_t outer,
    uint16_t inner)
{
    ct_ellipse(canvas, cx, cy, rx, ry, outer);
    ct_ellipse(
        canvas,
        cx,
        cy,
        ct_clamp(rx - thickness, 1, rx),
        ct_clamp(ry - thickness, 1, ry),
        inner);
}

static void ct_bezier(
    ct_canvas_t *canvas,
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
        const int t = step * 16;
        const int inverse = 256 - t;
        const int x =
            (inverse * inverse * x0 +
             2 * inverse * t * cx +
             t * t * x1 +
             32768) /
            65536;
        const int y =
            (inverse * inverse * y0 +
             2 * inverse * t * cy +
             t * t * y1 +
             32768) /
            65536;
        ct_thick_line(
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

static uint8_t ct_viseme_index(uint8_t set, uint8_t raw)
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

static ct_viseme_t ct_blended_viseme(const face_render_key_t *key)
{
    const ct_viseme_t first =
        CT_VISEMES[ct_viseme_index(key->viseme_set, key->viseme)];
    const ct_viseme_t second =
        CT_VISEMES[ct_viseme_index(
            key->viseme_set, key->viseme_secondary)];
    ct_viseme_t output;
    output.open =
        (uint8_t)ct_mix(first.open, second.open, key->viseme_blend);
    output.width =
        (uint8_t)ct_mix(first.width, second.width, key->viseme_blend);
    output.round =
        (uint8_t)ct_mix(first.round, second.round, key->viseme_blend);
    output.press =
        (uint8_t)ct_mix(first.press, second.press, key->viseme_blend);
    output.teeth =
        (uint8_t)ct_mix(first.teeth, second.teeth, key->viseme_blend);
    output.tongue =
        (uint8_t)ct_mix(first.tongue, second.tongue, key->viseme_blend);
    output.consonant = (uint8_t)ct_mix(
        first.consonant, second.consonant, key->viseme_blend);
    return output;
}

size_t face_closeup_toon_count(void)
{
    return FACE_CLOSEUP_TOON_COUNT;
}

const char *face_closeup_toon_slug(face_closeup_toon_style_t style)
{
    return ct_style_valid(style) ? CT_ACTORS[style].slug : NULL;
}

const char *face_closeup_toon_name(face_closeup_toon_style_t style)
{
    return ct_style_valid(style) ? CT_ACTORS[style].name : NULL;
}

bool face_closeup_toon_info(
    face_closeup_toon_style_t style,
    face_closeup_toon_info_t *info)
{
    if (!ct_style_valid(style) || info == NULL) {
        return false;
    }
    const ct_actor_def_t *actor = &CT_ACTORS[style];
    info->slug = actor->slug;
    info->name = actor->name;
    info->legacy_profile_id = actor->legacy_id;
    info->mouth_kind = actor->mouth_kind;
    info->mouth_grammar = actor->mouth_grammar;
    info->eye_kind = actor->eye_kind;
    info->estimated_ops_per_pixel = actor->ops;
    return true;
}

bool face_closeup_toon_from_legacy_id(
    uint8_t legacy_profile_id,
    face_closeup_toon_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_CLOSEUP_TOON_COUNT; ++raw) {
        if (CT_ACTORS[raw].legacy_id == legacy_profile_id) {
            *style = (face_closeup_toon_style_t)raw;
            return true;
        }
    }
    return false;
}

bool face_closeup_toon_resolve(
    face_closeup_toon_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_closeup_toon_pose_t *pose)
{
    if (!ct_style_valid(style) || render_key == NULL || pose == NULL) {
        return false;
    }
    const ct_actor_def_t *actor = &CT_ACTORS[style];
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, sizeof(pose->source));

    const uint8_t expression =
        render_key->stage_expression < CT_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = render_key->expression_weight;
    const ct_expression_t *target = &CT_EXPRESSIONS[expression];
#define CT_EXPR(field) ((int)ct_mix(0, target->field, expression_weight))

    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->speech_phase == FACE_SPEECH_STARTING ||
        render_key->speech_phase == FACE_SPEECH_ACTIVE ||
        render_key->speech_phase == FACE_SPEECH_ENDING;

    const ct_viseme_t viseme = ct_blended_viseme(render_key);
    pose->speech_open = (uint8_t)ct_mix(
        render_key->controls.mouth_open,
        viseme.open,
        render_key->viseme_weight);
    pose->speech_width = (uint8_t)ct_mix(
        render_key->controls.mouth_width,
        viseme.width,
        render_key->viseme_weight);
    pose->speech_round = (uint8_t)ct_mix(
        render_key->controls.mouth_round,
        viseme.round,
        render_key->viseme_weight);
    pose->speech_press = (uint8_t)ct_mix(
        render_key->controls.mouth_press,
        viseme.press,
        render_key->viseme_weight);
    pose->teeth = (uint8_t)ct_mix(
        render_key->controls.mouth_teeth,
        viseme.teeth,
        render_key->viseme_weight);
    pose->tongue = (uint8_t)ct_mix(
        render_key->tongue,
        viseme.tongue,
        render_key->viseme_weight);
    pose->cheek = (uint8_t)ct_clamp(
        (int)render_key->cheek + CT_EXPR(cheek), 0, 255);
    pose->consonant = render_key->phoneme == FACE_PHONEME_NONE
        ? viseme.consonant
        : (uint8_t)(render_key->phoneme % 32U);

    int phase_gain = 0;
    switch (render_key->speech_phase) {
    case FACE_SPEECH_STARTING:
        phase_gain = 112;
        break;
    case FACE_SPEECH_ACTIVE:
        phase_gain = 255;
        break;
    case FACE_SPEECH_ENDING:
        phase_gain = 104;
        break;
    case FACE_SPEECH_IDLE:
    default:
        phase_gain = pose->speaking ? 142 : 0;
        break;
    }
    const int audio_drive =
        (int)render_key->audio_level * phase_gain / 255;
    const int speech_wave = ct_wave(
        sample_clock +
            (uint32_t)render_key->schema_version * 17U +
            (uint32_t)style * 23U,
        /*
         * sample_clock is in 16 kHz PCM samples.  The old 1,066-sample
         * period landed almost exactly on a half-cycle at 30 fps, so the
         * face alternated between opposite extremes on consecutive
         * frames.  Keep the acting bob below 2 Hz so adjacent frames
         * interpolate instead of strobing.
         */
        9600U);
    pose->speech_bob = pose->speaking
        ? (int16_t)ct_clamp(
            speech_wave * (24 + audio_drive) / (127 * 26), -4, 4)
        : 0;

    const int gaze_x = ct_clamp(
        render_key->controls.look_x / 6 +
            render_key->head_yaw / 18 +
            CT_EXPR(gaze_x),
        -13,
        13);
    const int gaze_y = ct_clamp(
        render_key->controls.look_y / 8 +
            render_key->head_pitch / 20 +
            CT_EXPR(gaze_y),
        -10,
        10);

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const uint8_t input_open = eye == 0U
            ? render_key->controls.eye_left_open
            : render_key->controls.eye_right_open;
        const uint8_t squint = eye == 0U
            ? render_key->eye_left_squint
            : render_key->eye_right_squint;
        const int expression_open = eye == 0U
            ? CT_EXPR(eye_open_left)
            : CT_EXPR(eye_open_right);
        pose->eye_x[eye] = actor->eye_x[eye];
        pose->eye_y[eye] = actor->eye_y;
        pose->eye_w[eye] = (int16_t)ct_clamp(
            actor->eye_w + CT_EXPR(eye_width), 18, 48);
        pose->eye_h[eye] = actor->eye_h;
        int open =
            actor->eye_h * (int)input_open / 255 +
            expression_open -
            (int)squint / 18 +
            ((int)render_key->affect_arousal - 128) / 48 +
            pose->speech_bob / 2;
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            open += 4;
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            open -= 1;
        }
        if ((render_key->controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            open = 2;
        }
        pose->eye_open[eye] = (int16_t)ct_clamp(
            open, 2, actor->eye_h);
        pose->pupil_x[eye] =
            (int16_t)(actor->eye_x[eye] + gaze_x);
        pose->pupil_y[eye] =
            (int16_t)(actor->eye_y + gaze_y - pose->speech_bob / 2);
        pose->pupil_radius[eye] = (int16_t)ct_clamp(
            6 + CT_EXPR(pupil) +
                ((int)render_key->attention - 128) / 58 -
                ((int)render_key->affect_arousal - 128) / 80,
            3,
            10);
        const int brow_raise = eye == 0U
            ? CT_EXPR(brow_raise_left)
            : CT_EXPR(brow_raise_right);
        const int outer = eye == 0U
            ? render_key->brow_outer_left
            : render_key->brow_outer_right;
        pose->brow_y[eye] = (int16_t)ct_clamp(
            actor->eye_y - actor->eye_h / 2 - 7 +
                brow_raise -
                render_key->controls.brow / 22 -
                render_key->brow_inner / 24 -
                outer / 30 -
                pose->speech_bob / 2,
            9,
            52);
        pose->brow_slope[eye] = (int16_t)ct_clamp(
            (eye == 0U
                ? CT_EXPR(brow_slope_left)
                : CT_EXPR(brow_slope_right)) +
                render_key->head_roll / 17 +
                outer / 24,
            -14,
            14);
    }

    pose->mouth_x = 80;
    pose->mouth_y = actor->mouth_y;
    pose->mouth_w = (int16_t)ct_clamp(
        18 + pose->speech_width * 43 / 255 -
            pose->speech_round / 19 +
            CT_EXPR(mouth_width),
        12,
        actor->mouth_max_w);
    int mouth_height =
        2 + pose->speech_open * 27 / 255 -
        pose->speech_press * 7 / 255 +
        CT_EXPR(mouth_open);
    if (render_key->speech_phase == FACE_SPEECH_STARTING) {
        mouth_height = 2 + (mouth_height - 2) * 3 / 5;
    } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
        mouth_height = 2 + (mouth_height - 2) / 2;
    }
    pose->mouth_h = (int16_t)ct_clamp(
        mouth_height, 2, actor->mouth_max_h);
    pose->mouth_corner[0] = (int16_t)ct_clamp(
        render_key->mouth_corner_left / 12 +
            render_key->affect_valence / 18 +
            CT_EXPR(mouth_corner_left),
        -14,
        14);
    pose->mouth_corner[1] = (int16_t)ct_clamp(
        render_key->mouth_corner_right / 12 +
            render_key->affect_valence / 18 +
            CT_EXPR(mouth_corner_right),
        -14,
        14);

    pose->head_roll = (int16_t)ct_clamp(
        render_key->head_roll / 11 + CT_EXPR(head_roll), -13, 13);
    pose->body_lean_x = (int16_t)ct_clamp(
        render_key->body_lean_x / 14 + CT_EXPR(body_x), -10, 10);
    pose->body_lean_y = (int16_t)ct_clamp(
        render_key->body_lean_y / 17 + CT_EXPR(body_y), -8, 8);
    const int activity_lift =
        render_key->controls.expression == FACE_ACTIVITY_SPEAKING ? -1
        : render_key->controls.expression == FACE_ACTIVITY_THINKING ? 1
        : render_key->controls.expression == FACE_ACTIVITY_LISTENING ? 0
        : 2;
    pose->face_shift_x = (int16_t)ct_clamp(
        render_key->head_yaw / 28 + pose->body_lean_x / 2,
        -7,
        7);
    pose->face_shift_y = (int16_t)ct_clamp(
        render_key->head_pitch / 34 +
            pose->body_lean_y / 3 +
            activity_lift +
            pose->speech_bob +
            (render_key->speech_phase == FACE_SPEECH_STARTING ? -2
             : render_key->speech_phase == FACE_SPEECH_ENDING ? 1
             : 0),
        -6,
        7);

    /*
     * Protocol fields alter a tiny character-integrated detail (catchlight,
     * lens tick, freckle, stitch, or helmet lamp).  This consumes the entire
     * transport record without adding a diagnostics panel to the face.
     */
    pose->detail_phase = (uint8_t)(
        (uint16_t)render_key->phoneme * 3U +
        (uint16_t)render_key->viseme_set * 5U +
        (uint16_t)render_key->schema_version * 7U +
        (uint16_t)render_key->controls.expression * 11U +
        (uint16_t)render_key->controls.flags * 13U +
        (uint16_t)pose->consonant * 17U +
        (uint16_t)pose->teeth +
        (uint16_t)pose->tongue * 3U +
        (uint16_t)pose->cheek * 5U +
        (uint16_t)render_key->attention * 7U +
        (uint16_t)(uint8_t)render_key->affect_valence * 11U +
        (uint16_t)render_key->audio_level * 13U) &
        31U;
    pose->stage_expression = expression;
    pose->expression_weight = expression_weight;
    pose->activity = render_key->controls.expression;
    pose->speech_phase = render_key->speech_phase;
    pose->attention = render_key->attention;
#undef CT_EXPR
    return true;
}

static int ct_feature_y(
    const face_closeup_toon_pose_t *pose,
    int x)
{
    return pose->face_shift_y +
        pose->head_roll * (x - 80) / 72;
}

static void ct_eye_ellipse(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    int cy,
    int width,
    int open,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil)
{
    width = (int)ct_clamp(width, 8, 54);
    open = (int)ct_clamp(open, 2, 46);
    const int rx = width / 2;
    const int ry = open / 2;
    const int left = (int)ct_clamp(
        cx - rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int right = (int)ct_clamp(
        cx + rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int top = (int)ct_clamp(
        cy - ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int bottom = (int)ct_clamp(
        cy + ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int iris_radius = (int)ct_clamp(
        pose->pupil_radius[eye] + 3, 5, 13);
    const int pupil_radius = (int)ct_clamp(
        pose->pupil_radius[eye] * 2 / 3, 2, 7);
    const int iris_x = (int)ct_clamp(
        pose->pupil_x[eye] + pose->face_shift_x,
        cx - rx / 2,
        cx + rx / 2);
    const int iris_y = (int)ct_clamp(
        pose->pupil_y[eye] + ct_feature_y(pose, cx),
        cy - ry / 2,
        cy + ry / 2);
    const int64_t eye_limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t eye_dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t eye_dx = x - cx;
            if (eye_dx * eye_dx * ry * ry +
                    eye_dy * eye_dy * rx * rx >
                eye_limit) {
                continue;
            }
            uint16_t color = sclera;
            const int iris_dx = x - iris_x;
            const int iris_dy = y - iris_y;
            if (iris_dx * iris_dx + iris_dy * iris_dy <=
                iris_radius * iris_radius) {
                color = iris;
            }
            if (iris_dx * iris_dx + iris_dy * iris_dy <=
                pupil_radius * pupil_radius) {
                color = pupil;
            }
            ct_put(canvas, x, y, color);
        }
    }
    const int glint_x =
        iris_x - pupil_radius / 2 + (pose->detail_phase % 3U);
    const int glint_y =
        iris_y - pupil_radius / 2 - (pose->detail_phase / 3U) % 2U;
    ct_ellipse(canvas, glint_x, glint_y, 1, 1, sclera);
}

/*
 * The dreamer owns a softer, lid-aware iris.  Shrinking the iris with the
 * available aperture keeps sleepy and blinking poses from leaving detached
 * pupil fragments, while the expression-weighted shrink makes joy/sleep
 * readable without an abrupt sprite-like eye swap.
 */
static void ct_draw_dreamer_eye(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    int cy,
    int width,
    int open,
    uint16_t skin,
    uint16_t sclera,
    uint16_t iris,
    uint16_t ink)
{
    width = (int)ct_clamp(width, 8, 54);
    open = (int)ct_clamp(open, 2, 46);
    const int rx = width / 2;
    const int ry = open / 2;
    const int left = (int)ct_clamp(
        cx - rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int right = (int)ct_clamp(
        cx + rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int top = (int)ct_clamp(
        cy - ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int bottom = (int)ct_clamp(
        cy + ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    int iris_radius = (int)ct_clamp(
        pose->pupil_radius[eye] + 3, 4, 12);
    int mood_shrink = 0;
    if (pose->stage_expression == FACE_EXPRESSION_JOY) {
        mood_shrink = pose->expression_weight * 3 / 4;
    } else if (pose->stage_expression == FACE_EXPRESSION_SLEEPY) {
        mood_shrink = pose->expression_weight / 2;
    }
    iris_radius = iris_radius * (255 - mood_shrink) / 255;
    iris_radius = (int)ct_clamp(
        iris_radius, 0, ry > 1 ? ry - 1 : 0);
    const int pupil_radius = iris_radius * 2 / 3;
    const int gaze_x =
        pose->pupil_x[eye] - pose->eye_x[eye];
    const int gaze_y =
        pose->pupil_y[eye] - pose->eye_y[eye];
    const int max_x = (int)ct_clamp(
        rx - iris_radius - 1, 0, rx);
    const int max_y = (int)ct_clamp(
        ry - iris_radius - 1, 0, ry);
    const int iris_x = cx + (int)ct_clamp(
        gaze_x * 5 / 4, -max_x, max_x);
    const int iris_y = cy + (int)ct_clamp(
        gaze_y * 5 / 4, -max_y, max_y);
    const int64_t eye_limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t eye_dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t eye_dx = x - cx;
            if (eye_dx * eye_dx * ry * ry +
                    eye_dy * eye_dy * rx * rx >
                eye_limit) {
                continue;
            }
            uint16_t color = sclera;
            if (iris_radius > 0) {
                const int iris_dx = x - iris_x;
                const int iris_dy = y - iris_y;
                const int distance =
                    iris_dx * iris_dx + iris_dy * iris_dy;
                if (distance <= iris_radius * iris_radius) {
                    color = iris;
                }
                if (pupil_radius > 0 &&
                    distance <= pupil_radius * pupil_radius) {
                    color = ink;
                }
            }
            ct_put(canvas, x, y, color);
        }
    }
    int lid_cover = 0;
    if (pose->stage_expression == FACE_EXPRESSION_JOY) {
        lid_cover = ry * (int)pose->expression_weight / 255;
    }
    if (open <= 10) {
        lid_cover = ry + 1;
    }
    if (lid_cover > 0) {
        ct_rect(
            canvas,
            cx - rx - 1,
            cy - ry - 1,
            width + 2,
            lid_cover,
            skin);
        ct_rect(
            canvas,
            cx - rx - 1,
            cy + ry - lid_cover + 2,
            width + 2,
            lid_cover,
            skin);
    }
    if (iris_radius >= 4) {
        ct_put(
            canvas,
            iris_x - pupil_radius / 2,
            iris_y - pupil_radius / 2,
            sclera);
    }
    if (open <= 10 ||
        pose->stage_expression == FACE_EXPRESSION_JOY) {
        const int curve =
            pose->stage_expression == FACE_EXPRESSION_JOY ? 20
            : pose->stage_expression == FACE_EXPRESSION_SLEEPY ? 2
            : 4;
        ct_bezier(
            canvas,
            cx - rx + 2,
            cy,
            cx,
            cy + curve,
            cx + rx - 2,
            cy,
            4,
            ink);
    }
}

/*
 * Scout pupils deliberately travel farther than their sockets, but their
 * catchlights stay parented to the iris.  The old generic detail-phase glint
 * jittered independently and read as dirt at 40x30.
 */
static void ct_draw_scout_eye(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    int cy,
    int width,
    int open,
    uint16_t sclera,
    uint16_t iris,
    uint16_t ink)
{
    width = (int)ct_clamp(width, 8, 54);
    open = (int)ct_clamp(open, 2, 46);
    const int rx = width / 2;
    const int ry = open / 2;
    const int iris_radius = (int)ct_clamp(
        pose->pupil_radius[eye] + 4, 6, 13);
    const int pupil_radius = (int)ct_clamp(
        pose->pupil_radius[eye] * 2 / 3, 2, 7);
    const int max_x = (int)ct_clamp(
        rx - iris_radius - 1, 0, rx);
    const int max_y = (int)ct_clamp(
        ry - iris_radius - 1, 0, ry);
    const int gaze_x =
        pose->pupil_x[eye] - pose->eye_x[eye];
    const int gaze_y =
        pose->pupil_y[eye] - pose->eye_y[eye];
    const int iris_x = cx + (int)ct_clamp(
        gaze_x * 3 / 2, -max_x, max_x);
    const int iris_y = cy + (int)ct_clamp(
        gaze_y * 3 / 2, -max_y, max_y);
    const int left = (int)ct_clamp(
        cx - rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int right = (int)ct_clamp(
        cx + rx, CT_SAFE, FACE_CLOSEUP_TOON_WIDTH - CT_SAFE - 1);
    const int top = (int)ct_clamp(
        cy - ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int bottom = (int)ct_clamp(
        cy + ry, CT_SAFE, FACE_CLOSEUP_TOON_HEIGHT - CT_SAFE - 1);
    const int64_t eye_limit = (int64_t)rx * rx * ry * ry;
    for (int y = top; y <= bottom; ++y) {
        const int64_t eye_dy = y - cy;
        for (int x = left; x <= right; ++x) {
            const int64_t eye_dx = x - cx;
            if (eye_dx * eye_dx * ry * ry +
                    eye_dy * eye_dy * rx * rx >
                eye_limit) {
                continue;
            }
            uint16_t color = sclera;
            const int iris_dx = x - iris_x;
            const int iris_dy = y - iris_y;
            const int distance =
                iris_dx * iris_dx + iris_dy * iris_dy;
            if (distance <= iris_radius * iris_radius) {
                color = iris;
            }
            if (distance <= pupil_radius * pupil_radius) {
                color = ink;
            }
            ct_put(canvas, x, y, color);
        }
    }
    if (open >= 14) {
        ct_ellipse(
            canvas,
            iris_x - pupil_radius / 2,
            iris_y - pupil_radius / 2,
            1,
            1,
            sclera);
    }
}

static void ct_draw_brow(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    int width,
    int y_offset,
    int thickness,
    uint16_t color)
{
    const int half = width / 2;
    const int y = pose->brow_y[eye] +
        ct_feature_y(pose, cx) + y_offset;
    const int slope = pose->brow_slope[eye];
    const int outer_y = y + (eye == 0U ? -slope : slope) / 2;
    const int inner_y = y + (eye == 0U ? slope : -slope) / 2;
    ct_bezier(
        canvas,
        cx - half,
        outer_y,
        cx,
        y - 2,
        cx + half,
        inner_y,
        thickness,
        color);
}

static void ct_draw_dreamer_brow(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    int width,
    uint16_t color)
{
    const int half = width / 2;
    const int feature_y = ct_feature_y(pose, cx);
    const int y = (int)ct_clamp(
        pose->brow_y[eye] + feature_y + 1,
        32 + pose->face_shift_y,
        49 + pose->face_shift_y);
    const int slope = pose->brow_slope[eye];
    const int outer_y = y + (eye == 0U ? -slope : slope) / 2;
    const int inner_y = y + (eye == 0U ? slope : -slope) / 2;
    ct_bezier(
        canvas,
        cx - half,
        outer_y,
        cx,
        y - 2,
        cx + half,
        inner_y,
        2,
        color);
}

static void ct_draw_manga_brow(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    size_t eye,
    int cx,
    uint16_t color)
{
    const int feature_y = ct_feature_y(pose, cx);
    const int y = (int)ct_clamp(
        28 + (pose->brow_y[eye] - 23) / 2 + feature_y,
        22 + pose->face_shift_y,
        35 + pose->face_shift_y);
    const int slope = pose->brow_slope[eye];
    const int outer_y = y + (eye == 0U ? -slope : slope) / 3;
    const int inner_y = y + (eye == 0U ? slope : -slope) / 3;
    ct_bezier(
        canvas,
        cx - 8,
        outer_y,
        cx,
        y - 1,
        cx + 8,
        inner_y,
        2,
        color);
}

static void ct_draw_cheeks(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    int y,
    uint16_t color)
{
    if (pose->cheek < 28U) {
        return;
    }
    const int radius_x = 2 + pose->cheek / 64U;
    const int radius_y = 1 + pose->cheek / 104U;
    const int shift_x = pose->face_shift_x;
    const int shift_y = pose->face_shift_y;
    ct_ellipse(canvas, 35 + shift_x, y + shift_y, radius_x, radius_y, color);
    ct_ellipse(canvas, 125 + shift_x, y + shift_y, radius_x, radius_y, color);
    if (pose->cheek > 180U) {
        ct_line(
            canvas,
            29 + shift_x,
            y - 3 + shift_y,
            35 + shift_x,
            y - 5 + shift_y,
            color);
        ct_line(
            canvas,
            125 + shift_x,
            y - 5 + shift_y,
            131 + shift_x,
            y - 3 + shift_y,
            color);
    }
}

typedef enum {
    CT_MOUTH_PROFILE_SOFT = 0,
    CT_MOUTH_PROFILE_FLAT,
    CT_MOUTH_PROFILE_DIAMOND,
    CT_MOUTH_PROFILE_LOWER_HEAVY,
    CT_MOUTH_PROFILE_ASYMMETRIC,
} ct_mouth_profile_t;

typedef struct {
    int cx;
    int cy;
    int half_width;
    int height;
    int left_x;
    int right_x;
    int left_y;
    int right_y;
    int curve;
    int asymmetry;
} ct_mouth_box_t;

static int ct_expression_mouth_value(
    const face_closeup_toon_pose_t *pose,
    const int8_t values[CT_EXPRESSION_COUNT])
{
    const uint8_t expression =
        pose->stage_expression < CT_EXPRESSION_COUNT
        ? pose->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    return (int)ct_mix(
        0, values[expression], pose->expression_weight);
}

static ct_mouth_box_t ct_make_mouth_box(
    const face_closeup_toon_pose_t *pose,
    int x_offset,
    int y_offset,
    int width_numerator,
    int width_denominator,
    int height_numerator,
    int height_denominator)
{
    static const int8_t WIDTH[CT_EXPRESSION_COUNT] = {
        0, 1, 4, -3, -6, -2, -3, 2, -5, 6, -2,
    };
    static const int8_t HEIGHT[CT_EXPRESSION_COUNT] = {
        0, 0, 2, -1, 3, -1, -1, -2, -1, 3, -1,
    };
    static const int8_t CURVE[CT_EXPRESSION_COUNT] = {
        0, 4, 7, -5, 0, 2, 1, 0, 2, 8, 3,
    };
    static const int8_t ASYMMETRY[CT_EXPRESSION_COUNT] = {
        0, 0, 0, -1, 0, -4, 6, 0, -1, 0, 5,
    };
    int width =
        pose->mouth_w * width_numerator /
        (width_denominator > 0 ? width_denominator : 1);
    int height =
        pose->mouth_h * height_numerator /
        (height_denominator > 0 ? height_denominator : 1);
    width += ct_expression_mouth_value(pose, WIDTH);
    height += ct_expression_mouth_value(pose, HEIGHT);
    width = (int)ct_clamp(width, 8, 70);
    height = (int)ct_clamp(height, 2, 32);

    ct_mouth_box_t box;
    box.cx = pose->mouth_x + pose->face_shift_x + x_offset;
    box.cy = pose->mouth_y + pose->face_shift_y + y_offset;
    box.half_width = width / 2;
    box.height = height;
    box.left_x = box.cx - box.half_width;
    box.right_x = box.cx + box.half_width;
    box.left_y =
        box.cy - pose->mouth_corner[0] / 2 -
        pose->head_roll * box.half_width / 72;
    box.right_y =
        box.cy - pose->mouth_corner[1] / 2 +
        pose->head_roll * box.half_width / 72;
    box.curve = ct_expression_mouth_value(pose, CURVE);
    box.asymmetry =
        ct_expression_mouth_value(pose, ASYMMETRY);
    return box;
}

static bool ct_mouth_closed(
    const face_closeup_toon_pose_t *pose,
    const ct_mouth_box_t *box)
{
    return box->height <= 3 || pose->speech_press > 222U;
}

static int ct_mouth_mid_control(
    const ct_mouth_box_t *box,
    int middle_y)
{
    return middle_y * 2 - (box->left_y + box->right_y) / 2;
}

static void ct_draw_attached_curve(
    ct_canvas_t *canvas,
    const ct_mouth_box_t *box,
    int middle_y,
    int thickness,
    uint16_t color)
{
    ct_bezier(
        canvas,
        box->left_x,
        box->left_y,
        box->cx,
        ct_mouth_mid_control(box, middle_y),
        box->right_x,
        box->right_y,
        thickness,
        color);
}

static int ct_mouth_bulge(
    ct_mouth_profile_t profile,
    int edge)
{
    edge = (int)ct_clamp(edge, 0, 255);
    switch (profile) {
    case CT_MOUTH_PROFILE_FLAT:
        return edge < 170
            ? 255
            : (255 - edge) * 3;
    case CT_MOUTH_PROFILE_DIAMOND:
        return 255 - edge;
    case CT_MOUTH_PROFILE_SOFT:
    case CT_MOUTH_PROFILE_LOWER_HEAVY:
    case CT_MOUTH_PROFILE_ASYMMETRIC:
    default:
        return 255 - edge * edge / 255;
    }
}

static void ct_draw_profile_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    const ct_mouth_box_t *box,
    ct_mouth_profile_t profile,
    int upper_share,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    int outline_thickness)
{
    upper_share = (int)ct_clamp(upper_share, 48, 207);
    const int upper_depth =
        (int)ct_clamp(
            box->height * upper_share / 255 +
                pose->speech_round / 112,
            1,
            box->height - 1);
    const int lower_depth = box->height - upper_depth;
    int previous_top = box->left_y;
    int previous_bottom = box->left_y;
    for (int x = box->left_x; x <= box->right_x; ++x) {
        const int span = box->right_x - box->left_x;
        const int fraction = span > 0
            ? (x - box->left_x) * 255 / span
            : 0;
        const int edge =
            ct_abs(x - box->cx) * 255 /
            (box->half_width > 0 ? box->half_width : 1);
        const int bulge = ct_mouth_bulge(profile, edge);
        int baseline =
            box->left_y +
            (box->right_y - box->left_y) * fraction / 255 +
            box->curve * bulge / 255;
        if (profile == CT_MOUTH_PROFILE_ASYMMETRIC) {
            baseline +=
                box->asymmetry * (fraction - 128) *
                bulge * 3 / (128 * 255);
        }
        int top = baseline - upper_depth * bulge / 255;
        int bottom = baseline + lower_depth * bulge / 255;
        if (profile == CT_MOUTH_PROFILE_LOWER_HEAVY) {
            bottom += bulge / 110;
        }
        if (bottom < top) {
            const int swap = top;
            top = bottom;
            bottom = swap;
        }
        const int teeth_depth = pose->teeth > 72U
            ? 1 + (int)pose->teeth * box->height / (255 * 3)
            : 0;
        const int tongue_depth = pose->tongue > 70U
            ? 1 + (int)pose->tongue * box->height / (255 * 3)
            : 0;
        for (int y = top; y <= bottom; ++y) {
            uint16_t color = cavity;
            if (teeth_depth > 0 && y <= top + teeth_depth) {
                color = tooth;
            } else if (tongue_depth > 0 &&
                       y >= bottom - tongue_depth) {
                color = tongue;
            }
            ct_put(canvas, x, y, color);
        }
        if (x > box->left_x) {
            ct_thick_line(
                canvas,
                x - 1,
                previous_top,
                x,
                top,
                outline_thickness,
                outline);
            ct_thick_line(
                canvas,
                x - 1,
                previous_bottom,
                x,
                bottom,
                outline_thickness,
                outline);
        }
        previous_top = top;
        previous_bottom = bottom;
    }
    ct_thick_line(
        canvas,
        box->left_x,
        box->left_y,
        box->left_x + 1,
        box->left_y,
        outline_thickness,
        outline);
    ct_thick_line(
        canvas,
        box->right_x - 1,
        box->right_y,
        box->right_x,
        box->right_y,
        outline_thickness,
        outline);
}

static void ct_draw_signal_ribbon_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 0, 1, 1, 1, 1);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_thick_line(
            canvas,
            box.left_x,
            box.left_y,
            box.cx - 4,
            middle,
            outline_thickness,
            lip);
        ct_thick_line(
            canvas,
            box.cx - 4,
            middle,
            box.cx + 4,
            middle - box.asymmetry / 3,
            outline_thickness,
            lip);
        ct_thick_line(
            canvas,
            box.cx + 4,
            middle - box.asymmetry / 3,
            box.right_x,
            box.right_y,
            outline_thickness,
            lip);
        if (pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            ct_line(
                canvas,
                box.left_x + 4,
                middle + 3,
                box.right_x - 4,
                middle + 3,
                outline);
        }
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_DIAMOND,
            128,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        ct_line(
            canvas,
            box.cx - box.half_width / 3,
            middle,
            box.cx + box.half_width / 3,
            middle,
            lip);
    }
    ct_ellipse(canvas, box.left_x, box.left_y, 1, 1, outline);
    ct_ellipse(canvas, box.right_x, box.right_y, 1, 1, outline);
}

static void ct_draw_dream_cupid_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, -1, 0, 1, 1, 1, 1);
    const int middle =
        box.cy + box.curve * 2 + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_draw_attached_curve(
            canvas,
            &box,
            middle,
            outline_thickness < 4 ? 4 : outline_thickness,
            lip);
        if (pose->stage_expression == FACE_EXPRESSION_WARM ||
            pose->stage_expression == FACE_EXPRESSION_JOY) {
            ct_bezier(
                canvas,
                box.cx - 7,
                middle,
                box.cx,
                middle + 3,
                box.cx + 7,
                middle,
                1,
                outline);
        }
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_SOFT,
            108,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        const int top = middle - box.height * 108 / 255;
        ct_line(canvas, box.cx - 6, top, box.cx, top + 2, lip);
        ct_line(canvas, box.cx, top + 2, box.cx + 6, top, lip);
    }
}

static void ct_draw_scout_grin_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 0, 6, 5, 1, 1);
    const int middle = box.cy + box.curve;
    if (ct_mouth_closed(pose, &box)) {
        ct_draw_attached_curve(
            canvas, &box, middle, outline_thickness + 1, lip);
        ct_line(
            canvas,
            box.cx - box.half_width / 3,
            middle + 2,
            box.cx + box.half_width / 3,
            middle + 2,
            outline);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_FLAT,
            82,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        if (pose->teeth > 108U) {
            ct_line(
                canvas,
                box.cx,
                middle - box.height / 3,
                box.cx,
                middle,
                outline);
        }
    }
}

static void ct_draw_cat_muzzle_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 1, 4, 5, 1, 1);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    const int center_y = middle - 1;
    if (ct_mouth_closed(pose, &box)) {
        ct_bezier(
            canvas,
            box.left_x,
            box.left_y,
            box.cx - 9,
            middle + 4,
            box.cx,
            center_y,
            outline_thickness,
            lip);
        ct_bezier(
            canvas,
            box.cx,
            center_y,
            box.cx + 9,
            middle + 4 - box.asymmetry / 2,
            box.right_x,
            box.right_y,
            outline_thickness,
            lip);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_LOWER_HEAVY,
            76,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
    }
    ct_line(
        canvas,
        box.cx,
        box.cy - box.height / 2 - 5,
        box.cx,
        ct_mouth_closed(pose, &box)
            ? center_y
            : box.cy - box.height / 2,
        outline);
}

static void ct_draw_manga_petal_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 0, 4, 5, 3, 5);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        const int closed_thickness =
            outline_thickness < 2 ? 2 : outline_thickness;
        ct_bezier(
            canvas,
            box.left_x,
            box.left_y,
            box.cx - 5,
            middle - 2,
            box.cx,
            middle + 1,
            closed_thickness,
            lip);
        ct_bezier(
            canvas,
            box.cx,
            middle + 1,
            box.cx + 5,
            middle - 2 - box.asymmetry / 2,
            box.right_x,
            box.right_y,
            closed_thickness,
            lip);
        if (pose->stage_expression == FACE_EXPRESSION_EMBARRASSED) {
            ct_line(
                canvas,
                box.cx - 4,
                middle + 3,
                box.cx + 6,
                middle + 2,
                outline);
        }
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_SOFT,
            102,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        const int top = middle - box.height * 2 / 5;
        ct_line(canvas, box.cx - 7, top, box.cx, top + 3, lip);
        ct_line(canvas, box.cx, top + 3, box.cx + 7, top, lip);
    }
}

static void ct_draw_nav_console_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 0, 1, 1, 4, 5);
    const int middle = box.cy + box.curve;
    if (ct_mouth_closed(pose, &box)) {
        ct_thick_line(
            canvas,
            box.left_x,
            box.left_y,
            box.cx - 5,
            middle,
            outline_thickness,
            lip);
        ct_thick_line(
            canvas,
            box.cx - 5,
            middle,
            box.cx + 5,
            middle - box.asymmetry / 2,
            outline_thickness + 1,
            outline);
        ct_thick_line(
            canvas,
            box.cx + 5,
            middle - box.asymmetry / 2,
            box.right_x,
            box.right_y,
            outline_thickness,
            lip);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_FLAT,
            112,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        const int tick_height =
            (int)ct_clamp(box.height / 3, 2, 6);
        for (int tick = -1; tick <= 1; ++tick) {
            const int x = box.cx + tick * 7;
            ct_line(
                canvas,
                x,
                middle - tick_height / 2,
                x,
                middle + tick_height / 2,
                lip);
        }
    }
}

static void ct_draw_relay_elastic_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 1, 1, 1, 1, 1);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_bezier(
            canvas,
            box.left_x,
            box.left_y,
            box.cx - 10,
            middle + 2,
            box.cx,
            middle,
            outline_thickness,
            lip);
        ct_bezier(
            canvas,
            box.cx,
            middle,
            box.cx + 10,
            middle + 2 - box.asymmetry / 2,
            box.right_x,
            box.right_y,
            outline_thickness,
            lip);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_LOWER_HEAVY,
            72,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
    }
    ct_ellipse(canvas, box.left_x - 2, box.left_y, 2, 2, outline);
    ct_ellipse(canvas, box.right_x + 2, box.right_y, 2, 2, outline);
}

static void ct_draw_squire_facet_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 0, 9, 10, 1, 1);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_thick_line(
            canvas,
            box.left_x,
            box.left_y,
            box.cx,
            middle,
            outline_thickness + 1,
            lip);
        ct_thick_line(
            canvas,
            box.cx,
            middle,
            box.right_x,
            box.right_y,
            outline_thickness + 1,
            lip);
        if (pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            ct_line(
                canvas,
                box.left_x + 3,
                middle + 3,
                box.right_x - 3,
                middle + 3,
                outline);
        }
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_DIAMOND,
            118,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        ct_line(
            canvas,
            box.left_x + box.half_width / 3,
            middle - box.height / 3,
            box.right_x - box.half_width / 3,
            middle - box.height / 3,
            lip);
    }
}

static void ct_draw_elder_beard_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    const ct_mouth_box_t box =
        ct_make_mouth_box(pose, 0, 1, 4, 5, 9, 10);
    const int middle = box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_draw_attached_curve(
            canvas, &box, middle, outline_thickness, lip);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_SOFT,
            96,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        ct_bezier(
            canvas,
            box.cx - box.half_width / 2,
            middle + box.height / 3,
            box.cx,
            middle + box.height / 2 + 2,
            box.cx + box.half_width / 2,
            middle + box.height / 3,
            1,
            lip);
    }
    ct_line(
        canvas,
        box.left_x - 4,
        box.left_y + 1,
        box.left_x - 1,
        box.left_y + 4,
        outline);
    ct_line(
        canvas,
        box.right_x + 1,
        box.right_y + 4,
        box.right_x + 4,
        box.right_y + 1,
        outline);
}

static void ct_draw_mechanic_crook_mouth(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    ct_mouth_box_t box =
        ct_make_mouth_box(pose, 3, 0, 9, 10, 1, 1);
    box.asymmetry += 3;
    const int knee_x = box.cx + 4;
    const int middle =
        box.cy + box.curve + box.asymmetry / 2;
    if (ct_mouth_closed(pose, &box)) {
        ct_thick_line(
            canvas,
            box.left_x,
            box.left_y,
            knee_x,
            middle + 2,
            outline_thickness,
            lip);
        ct_thick_line(
            canvas,
            knee_x,
            middle + 2,
            box.right_x,
            box.right_y - 2,
            outline_thickness,
            lip);
        ct_ellipse(
            canvas,
            box.right_x + 2,
            box.right_y - 2,
            1,
            2,
            outline);
    } else {
        ct_draw_profile_mouth(
            canvas,
            pose,
            &box,
            CT_MOUTH_PROFILE_ASYMMETRIC,
            100,
            outline,
            cavity,
            tooth,
            tongue,
            outline_thickness);
        if (pose->teeth > 100U) {
            ct_line(
                canvas,
                box.cx - 5,
                middle - box.height / 3,
                box.cx - 5,
                middle,
                outline);
        }
    }
}

static void ct_draw_mouth(
    face_closeup_toon_style_t style,
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t tooth,
    uint16_t tongue,
    uint16_t lip,
    int outline_thickness)
{
    switch (style) {
    case FACE_CLOSEUP_TOON_BROW_DIALOGUE_DIRECTOR:
        ct_draw_signal_ribbon_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_SLEEP_WAKE_DREAMER:
        ct_draw_dream_cupid_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_IRIS_PARALLAX_SCOUT:
        ct_draw_scout_grin_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_CAT_OPTICS_FAMILIAR:
        ct_draw_cat_muzzle_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_M5_MANGA_LEAD:
        ct_draw_manga_petal_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_VGA_STAR_NAVIGATOR:
        ct_draw_nav_console_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_POCKET_RELAY_CREATURE:
        ct_draw_relay_elastic_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_EGA_QUEST_SQUIRE:
        ct_draw_squire_facet_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_VGA_ELDER_STORYTELLER:
        ct_draw_elder_beard_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_TALKIE_MOON_MECHANIC:
        ct_draw_mechanic_crook_mouth(
            canvas, pose, outline, cavity, tooth, tongue, lip,
            outline_thickness);
        break;
    case FACE_CLOSEUP_TOON_COUNT:
    default:
        break;
    }
}

static void ct_draw_brow_dialogue_director(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(5, 10, 20);
    const uint16_t shell = CT_RGB565(22, 38, 67);
    const uint16_t panel = CT_RGB565(12, 22, 39);
    const uint16_t cyan = CT_RGB565(73, 238, 227);
    const uint16_t warm = CT_RGB565(255, 155, 76);
    const uint16_t eye = CT_RGB565(218, 255, 247);
    const uint16_t cavity = CT_RGB565(2, 6, 12);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_quad(
        canvas,
        29 + pose->body_lean_x,
        112 + pose->body_lean_y,
        47 + pose->body_lean_x,
        94 + pose->body_lean_y,
        113 + pose->body_lean_x,
        94 + pose->body_lean_y,
        131 + pose->body_lean_x,
        112 + pose->body_lean_y,
        shell);
    ct_quad(
        canvas,
        17 + sx,
        35 + sy,
        38 + sx,
        14 + sy,
        122 + sx,
        14 + sy,
        143 + sx,
        35 + sy,
        shell);
    ct_quad(
        canvas,
        17 + sx,
        35 + sy,
        143 + sx,
        35 + sy,
        136 + sx,
        91 + sy,
        24 + sx,
        91 + sy,
        shell);
    ct_quad(
        canvas,
        24 + sx,
        91 + sy,
        136 + sx,
        91 + sy,
        111 + sx,
        108 + sy,
        49 + sx,
        108 + sy,
        shell);
    ct_round_rect(
        canvas, 27 + sx, 25 + sy, 106, 70, 17, panel);
    ct_rect(canvas, 32 + sx, 28 + sy, 96, 3, cyan);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        const int width = pose->eye_w[eye_index];
        const int open = pose->eye_open[eye_index];
        ct_round_rect(
            canvas,
            cx - width / 2 - 3,
            cy - pose->eye_h[eye_index] / 2 - 3,
            width + 6,
            pose->eye_h[eye_index] + 6,
            7,
            shell);
        ct_round_rect(
            canvas,
            cx - width / 2,
            cy - open / 2,
            width,
            open,
            5,
            eye);
        const int gaze = (int)ct_clamp(
            pose->pupil_x[eye_index] - pose->eye_x[eye_index],
            -9,
            9);
        const int gaze_y = (int)ct_clamp(
            pose->pupil_y[eye_index] - pose->eye_y[eye_index],
            -5,
            5);
        ct_round_rect(
            canvas,
            cx + gaze - 3,
            cy + gaze_y - open / 2 + 3,
            6,
            ct_clamp(open - 6, 2, open),
            2,
            cyan);
        ct_rect(
            canvas,
            cx + gaze - 1,
            cy + gaze_y - open / 2 + 5 +
                pose->detail_phase % 3U,
            2,
            3,
            warm);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            width,
            -1,
            3,
            eye_index == 0U ? cyan : warm);
    }
    ct_draw_cheeks(canvas, pose, 72, CT_RGB565(49, 104, 112));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_BROW_DIALOGUE_DIRECTOR,
        canvas,
        pose,
        cyan,
        cavity,
        eye,
        warm,
        warm,
        2);
    /* A one-pixel chassis status scan consumes all five detail bits. */
    ct_put(
        canvas,
        64 + sx + pose->detail_phase,
        104 + sy,
        warm);
}

static void ct_draw_sleep_wake_dreamer(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(12, 12, 35);
    const uint16_t cloud = CT_RGB565(142, 137, 202);
    const uint16_t face = CT_RGB565(238, 224, 210);
    const uint16_t shade = CT_RGB565(207, 190, 198);
    const uint16_t ink = CT_RGB565(50, 40, 73);
    const uint16_t iris = CT_RGB565(96, 112, 191);
    const uint16_t star = CT_RGB565(255, 216, 107);
    const uint16_t lip = CT_RGB565(163, 82, 119);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_ellipse(
        canvas,
        80 + pose->body_lean_x,
        111 + pose->body_lean_y,
        48,
        12,
        cloud);
    ct_ellipse(canvas, 80 + sx, 61 + sy, 50, 49, shade);
    ct_ellipse(canvas, 78 + sx, 58 + sy, 46, 45, face);
    ct_triangle(
        canvas,
        49 + sx,
        24 + sy,
        84 + sx,
        6 + sy,
        112 + sx,
        32 + sy,
        cloud);
    ct_ellipse(canvas, 107 + sx, 29 + sy, 9, 9, cloud);
    ct_ellipse(canvas, 111 + sx, 25 + sy, 4, 4, star);
    ct_ellipse(canvas, 31 + sx, 63 + sy, 8, 12, face);
    ct_ellipse(canvas, 125 + sx, 63 + sy, 8, 12, face);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_draw_dreamer_eye(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            face,
            CT_RGB565(255, 252, 235),
            iris,
            ink);
        ct_draw_dreamer_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index] - 2,
            ink);
    }
    ct_draw_cheeks(canvas, pose, 74, CT_RGB565(231, 137, 151));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_SLEEP_WAKE_DREAMER,
        canvas,
        pose,
        ink,
        CT_RGB565(62, 30, 55),
        CT_RGB565(255, 247, 226),
        CT_RGB565(214, 107, 135),
        lip,
        1);
    const int detail_x =
        34 + sx + (int)(pose->detail_phase & 7U) * 2;
    const int detail_y =
        29 + sy + (int)((pose->detail_phase >> 3U) & 3U) * 2;
    ct_line(canvas, detail_x - 2, detail_y, detail_x + 2, detail_y, star);
    ct_line(canvas, detail_x, detail_y - 2, detail_x, detail_y + 2, star);
}

static void ct_draw_iris_parallax_scout(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(17, 31, 36);
    const uint16_t helmet = CT_RGB565(221, 159, 71);
    const uint16_t helmet_dark = CT_RGB565(111, 69, 44);
    const uint16_t face = CT_RGB565(228, 194, 151);
    const uint16_t frame = CT_RGB565(52, 72, 77);
    const uint16_t sclera = CT_RGB565(242, 250, 229);
    const uint16_t iris = CT_RGB565(46, 196, 190);
    const uint16_t ink = CT_RGB565(21, 31, 36);
    const uint16_t red = CT_RGB565(228, 77, 67);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_quad(
        canvas,
        27 + pose->body_lean_x,
        114 + pose->body_lean_y,
        45 + pose->body_lean_x,
        94 + pose->body_lean_y,
        115 + pose->body_lean_x,
        94 + pose->body_lean_y,
        133 + pose->body_lean_x,
        114 + pose->body_lean_y,
        helmet_dark);
    ct_ellipse(canvas, 80 + sx, 59 + sy, 54, 50, helmet_dark);
    ct_ellipse(canvas, 80 + sx, 61 + sy, 47, 44, face);
    ct_quad(
        canvas,
        29 + sx,
        41 + sy,
        43 + sx,
        16 + sy,
        117 + sx,
        16 + sy,
        132 + sx,
        41 + sy,
        helmet);
    ct_rect(canvas, 40 + sx, 18 + sy, 80, 8, helmet);
    ct_thick_line(
        canvas, 80 + sx, 15 + sy, 80 + sx, 8 + sy, 3, red);
    ct_ellipse(canvas, 80 + sx, 8 + sy, 3, 3, red);

    ct_round_rect(canvas, 32 + sx, 30 + sy, 96, 44, 16, frame);
    ct_rect(canvas, 73 + sx, 45 + sy, 14, 7, helmet);
    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_ring(
            canvas,
            cx,
            cy,
            pose->eye_w[eye_index] / 2 + 4,
            pose->eye_h[eye_index] / 2 + 4,
            4,
            helmet,
            frame);
        ct_draw_scout_eye(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index] - 4,
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index],
            -3,
            2,
            helmet_dark);
    }
    /*
     * Preserve a deterministic visual carrier for detail_phase without
     * polluting the lenses: one status bead slides inside an attached rail.
     */
    ct_line(canvas, 48 + sx, 22 + sy, 112 + sx, 22 + sy, helmet_dark);
    ct_ellipse(
        canvas,
        49 + sx + (int)pose->detail_phase * 2,
        22 + sy,
        1,
        1,
        red);
    ct_ellipse(canvas, 80 + sx, 73 + sy, 4, 6, helmet_dark);
    ct_draw_cheeks(canvas, pose, 75, CT_RGB565(205, 116, 94));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_IRIS_PARALLAX_SCOUT,
        canvas,
        pose,
        ink,
        CT_RGB565(68, 35, 37),
        CT_RGB565(255, 247, 218),
        red,
        CT_RGB565(146, 59, 55),
        2);
}

static void ct_draw_cat_optics_familiar(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(39, 26, 59);
    const uint16_t fur = CT_RGB565(188, 111, 115);
    const uint16_t fur_light = CT_RGB565(239, 184, 157);
    const uint16_t inner_ear = CT_RGB565(221, 116, 145);
    const uint16_t ink = CT_RGB565(43, 29, 46);
    const uint16_t sclera = CT_RGB565(255, 244, 211);
    const uint16_t iris = CT_RGB565(239, 194, 68);
    const uint16_t nose = CT_RGB565(114, 57, 77);
    const uint16_t tongue = CT_RGB565(232, 103, 132);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    const int ear_tilt = pose->head_roll / 2;
    ct_clear(canvas, bg);

    ct_ellipse(
        canvas,
        80 + pose->body_lean_x,
        113 + pose->body_lean_y,
        44,
        14,
        fur);
    ct_triangle(
        canvas,
        34 + sx,
        47 + sy,
        39 + sx - ear_tilt,
        10 + sy,
        67 + sx,
        30 + sy,
        fur);
    ct_triangle(
        canvas,
        93 + sx,
        30 + sy,
        121 + sx + ear_tilt,
        10 + sy,
        126 + sx,
        47 + sy,
        fur);
    ct_triangle(
        canvas,
        42 + sx,
        36 + sy,
        43 + sx - ear_tilt,
        19 + sy,
        57 + sx,
        31 + sy,
        inner_ear);
    ct_triangle(
        canvas,
        103 + sx,
        31 + sy,
        117 + sx + ear_tilt,
        19 + sy,
        118 + sx,
        36 + sy,
        inner_ear);
    ct_ellipse(canvas, 80 + sx, 62 + sy, 48, 43, fur);
    ct_ellipse(canvas, 80 + sx, 79 + sy, 35, 25, fur_light);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_eye_ellipse(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        const int pupil_x = (int)ct_clamp(
            pose->pupil_x[eye_index] + sx,
            cx - 8,
            cx + 8);
        ct_thick_line(
            canvas,
            pupil_x,
            cy - pose->eye_open[eye_index] / 4,
            pupil_x,
            cy + pose->eye_open[eye_index] / 4,
            2,
            ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index],
            0,
            2,
            ink);
    }
    ct_triangle(
        canvas,
        75 + sx,
        74 + sy,
        85 + sx,
        74 + sy,
        80 + sx,
        80 + sy,
        nose);
    const int whisker_y =
        80 + sy - (pose->mouth_corner[0] + pose->mouth_corner[1]) / 12;
    ct_line(canvas, 70 + sx, whisker_y, 24 + sx, whisker_y - 6, ink);
    ct_line(canvas, 70 + sx, whisker_y + 3, 25 + sx, whisker_y + 7, ink);
    ct_line(canvas, 90 + sx, whisker_y, 136 + sx, whisker_y - 6, ink);
    ct_line(canvas, 90 + sx, whisker_y + 3, 135 + sx, whisker_y + 7, ink);
    ct_draw_cheeks(canvas, pose, 79, inner_ear);
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_CAT_OPTICS_FAMILIAR,
        canvas,
        pose,
        ink,
        CT_RGB565(63, 28, 39),
        sclera,
        tongue,
        nose,
        1);
    const int bead_x = 25 + sx + pose->detail_phase % 8U;
    ct_ellipse(canvas, bead_x, whisker_y - 5, 1, 1, iris);
}

static void ct_draw_m5_manga_lead(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(236, 215, 184);
    const uint16_t skin = CT_RGB565(255, 218, 188);
    const uint16_t shade = CT_RGB565(230, 161, 159);
    const uint16_t hair = CT_RGB565(43, 34, 72);
    const uint16_t hair_light = CT_RGB565(96, 75, 139);
    const uint16_t ink = CT_RGB565(35, 30, 57);
    const uint16_t sclera = CT_RGB565(255, 253, 242);
    const uint16_t iris = CT_RGB565(92, 126, 221);
    const uint16_t lip = CT_RGB565(177, 67, 104);
    const uint16_t blush = CT_RGB565(243, 105, 132);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_quad(
        canvas,
        26 + pose->body_lean_x,
        115 + pose->body_lean_y,
        48 + pose->body_lean_x,
        95 + pose->body_lean_y,
        112 + pose->body_lean_x,
        95 + pose->body_lean_y,
        134 + pose->body_lean_x,
        115 + pose->body_lean_y,
        hair);
    ct_ellipse(canvas, 80 + sx, 63 + sy, 49, 48, hair);
    ct_ellipse(canvas, 80 + sx, 64 + sy, 43, 43, skin);
    ct_ellipse(canvas, 37 + sx, 63 + sy, 7, 11, skin);
    ct_ellipse(canvas, 123 + sx, 63 + sy, 7, 11, skin);
    ct_quad(
        canvas,
        31 + sx,
        44 + sy,
        43 + sx,
        19 + sy,
        117 + sx,
        19 + sy,
        129 + sx,
        44 + sy,
        hair);
    ct_triangle(
        canvas, 43 + sx, 20 + sy, 62 + sx, 49 + sy,
        72 + sx, 18 + sy, hair);
    ct_triangle(
        canvas, 65 + sx, 18 + sy, 83 + sx, 50 + sy,
        92 + sx, 19 + sy, hair);
    ct_triangle(
        canvas, 88 + sx, 19 + sy, 103 + sx, 45 + sy,
        116 + sx, 20 + sy, hair);
    ct_bezier(
        canvas,
        48 + sx,
        23 + sy,
        72 + sx,
        17 + sy,
        96 + sx,
        22 + sy,
        2,
        hair_light);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_eye_ellipse(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        ct_bezier(
            canvas,
            cx - pose->eye_w[eye_index] / 2,
            cy - pose->eye_open[eye_index] / 2,
            cx,
            cy - pose->eye_open[eye_index] / 2 - 4,
            cx + pose->eye_w[eye_index] / 2,
            cy - pose->eye_open[eye_index] / 2,
            3,
            ink);
        if (eye_index == 0U) {
            ct_line(
                canvas,
                cx - pose->eye_w[eye_index] / 2,
                cy - pose->eye_open[eye_index] / 2,
                cx - pose->eye_w[eye_index] / 2 - 5,
                cy - pose->eye_open[eye_index] / 2 - 2,
                ink);
        } else {
            ct_line(
                canvas,
                cx + pose->eye_w[eye_index] / 2,
                cy - pose->eye_open[eye_index] / 2,
                cx + pose->eye_w[eye_index] / 2 + 5,
                cy - pose->eye_open[eye_index] / 2 - 2,
                ink);
        }
        ct_draw_manga_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            hair_light);
    }
    ct_line(canvas, 78 + sx, 72 + sy, 81 + sx, 75 + sy, shade);
    ct_draw_cheeks(canvas, pose, 74, blush);
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_M5_MANGA_LEAD,
        canvas,
        pose,
        ink,
        CT_RGB565(76, 30, 52),
        sclera,
        blush,
        lip,
        1);
    const int glint_x =
        98 + sx + (int)(pose->detail_phase & 7U) * 3;
    const int glint_y =
        25 + sy + (int)((pose->detail_phase >> 3U) & 3U) * 3;
    ct_ellipse(canvas, glint_x, glint_y, 1, 1, hair_light);
}

static void ct_draw_vga_star_navigator(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(5, 9, 28);
    const uint16_t suit = CT_RGB565(48, 58, 113);
    const uint16_t helmet = CT_RGB565(90, 79, 157);
    const uint16_t helmet_light = CT_RGB565(135, 114, 200);
    const uint16_t visor = CT_RGB565(20, 45, 66);
    const uint16_t face = CT_RGB565(191, 220, 185);
    const uint16_t sclera = CT_RGB565(244, 255, 224);
    const uint16_t iris = CT_RGB565(102, 238, 204);
    const uint16_t ink = CT_RGB565(12, 23, 38);
    const uint16_t coral = CT_RGB565(255, 115, 109);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    for (int star_index = 0; star_index < 7; ++star_index) {
        const int x = 10 + star_index * 23 +
            (pose->detail_phase + (uint8_t)star_index * 3U) % 5U;
        const int y = 9 + (star_index * 17) % 70;
        ct_put(canvas, x, y, helmet_light);
    }
    ct_quad(
        canvas,
        19 + pose->body_lean_x,
        115 + pose->body_lean_y,
        44 + pose->body_lean_x,
        91 + pose->body_lean_y,
        116 + pose->body_lean_x,
        91 + pose->body_lean_y,
        141 + pose->body_lean_x,
        115 + pose->body_lean_y,
        suit);
    ct_ellipse(canvas, 80 + sx, 59 + sy, 53, 50, helmet);
    ct_quad(
        canvas,
        32 + sx,
        49 + sy,
        49 + sx,
        20 + sy,
        111 + sx,
        20 + sy,
        128 + sx,
        49 + sy,
        helmet_light);
    ct_ellipse(canvas, 80 + sx, 63 + sy, 43, 39, face);
    ct_round_rect(canvas, 31 + sx, 31 + sy, 98, 43, 17, visor);
    ct_rect(canvas, 36 + sx, 66 + sy, 88, 5, helmet_light);
    ct_triangle(
        canvas,
        76 + sx,
        13 + sy,
        84 + sx,
        13 + sy,
        80 + sx,
        5 + sy,
        coral);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_eye_ellipse(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index],
            1,
            2,
            coral);
    }
    ct_ellipse(canvas, 80 + sx, 73 + sy, 3, 5, CT_RGB565(82, 131, 105));
    ct_draw_cheeks(canvas, pose, 75, coral);
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_VGA_STAR_NAVIGATOR,
        canvas,
        pose,
        ink,
        CT_RGB565(57, 34, 48),
        sclera,
        coral,
        CT_RGB565(105, 44, 70),
        2);
}

static void ct_draw_pocket_relay_creature(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(184, 222, 201);
    const uint16_t body = CT_RGB565(70, 148, 122);
    const uint16_t skin = CT_RGB565(120, 202, 158);
    const uint16_t light = CT_RGB565(190, 235, 185);
    const uint16_t gill = CT_RGB565(235, 107, 127);
    const uint16_t ink = CT_RGB565(28, 60, 61);
    const uint16_t sclera = CT_RGB565(242, 252, 217);
    const uint16_t iris = CT_RGB565(74, 97, 180);
    const uint16_t tongue = CT_RGB565(228, 102, 126);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_ellipse(
        canvas,
        80 + pose->body_lean_x,
        112 + pose->body_lean_y,
        49,
        15,
        body);
    for (int side = -1; side <= 1; side += 2) {
        const int base_x = 80 + side * 43 + sx;
        ct_ellipse(canvas, base_x, 38 + sy, 8, 17, gill);
        ct_ellipse(canvas, base_x + side * 7, 49 + sy, 8, 16, gill);
        ct_ellipse(canvas, base_x + side * 5, 27 + sy, 7, 14, gill);
    }
    ct_ellipse(canvas, 80 + sx, 63 + sy, 51, 42, body);
    ct_ellipse(canvas, 80 + sx, 66 + sy, 46, 37, skin);
    ct_ellipse(canvas, 50 + sx, 43 + sy, 21, 20, body);
    ct_ellipse(canvas, 110 + sx, 43 + sy, 21, 20, body);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_eye_ellipse(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index] - 1,
            2,
            2,
            ink);
    }
    ct_ellipse(canvas, 80 + sx, 75 + sy, 4, 3, body);
    ct_draw_cheeks(canvas, pose, 76, gill);
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_POCKET_RELAY_CREATURE,
        canvas,
        pose,
        ink,
        CT_RGB565(39, 49, 53),
        sclera,
        tongue,
        CT_RGB565(62, 109, 91),
        2);
    const int spot_x =
        37 + sx + pose->detail_phase % 7U;
    ct_ellipse(canvas, spot_x, 58 + sy, 2, 2, light);
}

static void ct_draw_ega_quest_squire(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(31, 48, 47);
    const uint16_t cloak = CT_RGB565(39, 80, 73);
    const uint16_t steel = CT_RGB565(138, 158, 153);
    const uint16_t steel_light = CT_RGB565(210, 211, 180);
    const uint16_t face = CT_RGB565(225, 172, 126);
    const uint16_t ink = CT_RGB565(40, 35, 39);
    const uint16_t sclera = CT_RGB565(250, 239, 200);
    const uint16_t iris = CT_RGB565(72, 128, 109);
    const uint16_t plume = CT_RGB565(207, 68, 70);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_quad(
        canvas,
        21 + pose->body_lean_x,
        115 + pose->body_lean_y,
        46 + pose->body_lean_x,
        90 + pose->body_lean_y,
        114 + pose->body_lean_x,
        90 + pose->body_lean_y,
        139 + pose->body_lean_x,
        115 + pose->body_lean_y,
        cloak);
    ct_quad(
        canvas,
        27 + sx,
        48 + sy,
        44 + sx,
        17 + sy,
        116 + sx,
        17 + sy,
        133 + sx,
        48 + sy,
        steel);
    ct_quad(
        canvas,
        31 + sx,
        43 + sy,
        129 + sx,
        43 + sy,
        119 + sx,
        101 + sy,
        41 + sx,
        101 + sy,
        steel);
    ct_ellipse(canvas, 80 + sx, 67 + sy, 39, 35, face);
    ct_rect(canvas, 39 + sx, 34 + sy, 82, 9, steel_light);
    ct_triangle(
        canvas,
        73 + sx,
        18 + sy,
        84 + sx,
        5 + sy,
        92 + sx,
        19 + sy,
        plume);
    ct_triangle(
        canvas,
        84 + sx,
        5 + sy,
        105 + sx,
        9 + sy,
        91 + sx,
        17 + sy,
        plume);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_quad(
            canvas,
            cx - pose->eye_w[eye_index] / 2,
            cy,
            cx,
            cy - pose->eye_open[eye_index] / 2,
            cx + pose->eye_w[eye_index] / 2,
            cy,
            cx,
            cy + pose->eye_open[eye_index] / 2,
            sclera);
        const int pupil_x = (int)ct_clamp(
            pose->pupil_x[eye_index] + sx,
            cx - 7,
            cx + 7);
        const int pupil_y = (int)ct_clamp(
            pose->pupil_y[eye_index] +
                ct_feature_y(pose, pose->eye_x[eye_index]),
            cy - 6,
            cy + 6);
        ct_ellipse(
            canvas,
            pupil_x,
            pupil_y,
            pose->pupil_radius[eye_index],
            ct_clamp(pose->eye_open[eye_index] / 4, 2, 6),
            iris);
        ct_ellipse(canvas, pupil_x, pupil_y, 2, 3, ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index],
            0,
            3,
            ink);
    }
    ct_line(canvas, 80 + sx, 64 + sy, 76 + sx, 75 + sy, ink);
    ct_draw_cheeks(canvas, pose, 76, CT_RGB565(197, 91, 79));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_EGA_QUEST_SQUIRE,
        canvas,
        pose,
        ink,
        CT_RGB565(67, 30, 34),
        sclera,
        plume,
        CT_RGB565(127, 45, 48),
        2);
    const int stitch =
        53 + sx + pose->detail_phase % 9U;
    ct_line(canvas, stitch, 24 + sy, stitch + 3, 27 + sy, steel_light);
}

static void ct_draw_vga_elder_storyteller(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(54, 29, 44);
    const uint16_t robe = CT_RGB565(70, 67, 109);
    const uint16_t skin = CT_RGB565(220, 171, 137);
    const uint16_t shade = CT_RGB565(169, 108, 102);
    const uint16_t hair = CT_RGB565(232, 222, 190);
    const uint16_t hair_shadow = CT_RGB565(169, 167, 154);
    const uint16_t ink = CT_RGB565(49, 36, 48);
    const uint16_t sclera = CT_RGB565(250, 242, 207);
    const uint16_t iris = CT_RGB565(74, 134, 147);
    const uint16_t lip = CT_RGB565(128, 58, 73);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_ellipse(
        canvas,
        80 + pose->body_lean_x,
        113 + pose->body_lean_y,
        53,
        16,
        robe);
    ct_ellipse(canvas, 80 + sx, 59 + sy, 48, 48, shade);
    ct_ellipse(canvas, 80 + sx, 58 + sy, 44, 44, skin);
    ct_ellipse(canvas, 35 + sx, 61 + sy, 8, 13, skin);
    ct_ellipse(canvas, 125 + sx, 61 + sy, 8, 13, skin);
    ct_ellipse(canvas, 80 + sx, 21 + sy, 30, 12, hair);
    ct_ellipse(canvas, 47 + sx, 28 + sy, 15, 15, hair);
    ct_ellipse(canvas, 113 + sx, 28 + sy, 15, 15, hair);

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        ct_eye_ellipse(
            canvas,
            pose,
            eye_index,
            cx,
            cy,
            pose->eye_w[eye_index],
            pose->eye_open[eye_index],
            sclera,
            iris,
            ink);
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index] + 7,
            -1,
            4,
            hair);
        ct_line(
            canvas,
            cx - 9,
            cy + pose->eye_open[eye_index] / 2 + 3,
            cx + 8,
            cy + pose->eye_open[eye_index] / 2 + 4,
            shade);
    }
    ct_ellipse(canvas, 80 + sx, 70 + sy, 6, 9, shade);
    ct_bezier(
        canvas,
        54 + sx,
        79 + sy,
        67 + sx,
        72 + sy,
        80 + sx,
        82 + sy,
        5,
        hair);
    ct_bezier(
        canvas,
        80 + sx,
        82 + sy,
        93 + sx,
        72 + sy,
        106 + sx,
        79 + sy,
        5,
        hair);
    ct_quad(
        canvas,
        49 + sx,
        82 + sy,
        111 + sx,
        82 + sy,
        101 + sx,
        112 + sy,
        59 + sx,
        112 + sy,
        hair_shadow);
    ct_triangle(
        canvas,
        57 + sx,
        84 + sy,
        80 + sx,
        111 + sy,
        80 + sx,
        82 + sy,
        hair);
    ct_triangle(
        canvas,
        80 + sx,
        82 + sy,
        80 + sx,
        111 + sy,
        103 + sx,
        84 + sy,
        hair);
    ct_draw_cheeks(canvas, pose, 75, CT_RGB565(182, 94, 99));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_VGA_ELDER_STORYTELLER,
        canvas,
        pose,
        ink,
        CT_RGB565(60, 28, 37),
        sclera,
        CT_RGB565(174, 73, 85),
        lip,
        1);
    const int beard_glint =
        63 + sx + pose->detail_phase % 12U;
    ct_line(
        canvas,
        beard_glint,
        102 + sy,
        beard_glint + 3,
        106 + sy,
        hair);
}

static void ct_draw_talkie_moon_mechanic(
    ct_canvas_t *canvas,
    const face_closeup_toon_pose_t *pose)
{
    const uint16_t bg = CT_RGB565(19, 51, 57);
    const uint16_t workwear = CT_RGB565(31, 93, 102);
    const uint16_t cap = CT_RGB565(221, 120, 56);
    const uint16_t cap_dark = CT_RGB565(128, 67, 48);
    const uint16_t skin = CT_RGB565(231, 175, 130);
    const uint16_t shade = CT_RGB565(177, 107, 89);
    const uint16_t ink = CT_RGB565(42, 37, 42);
    const uint16_t sclera = CT_RGB565(249, 239, 206);
    const uint16_t iris = CT_RGB565(62, 145, 157);
    const uint16_t lens = CT_RGB565(91, 211, 203);
    const uint16_t lip = CT_RGB565(135, 58, 61);
    const int sx = pose->face_shift_x;
    const int sy = pose->face_shift_y;
    ct_clear(canvas, bg);

    ct_quad(
        canvas,
        20 + pose->body_lean_x,
        115 + pose->body_lean_y,
        47 + pose->body_lean_x,
        91 + pose->body_lean_y,
        113 + pose->body_lean_x,
        91 + pose->body_lean_y,
        140 + pose->body_lean_x,
        115 + pose->body_lean_y,
        workwear);
    ct_ellipse(canvas, 80 + sx, 62 + sy, 48, 45, cap_dark);
    ct_ellipse(canvas, 80 + sx, 64 + sy, 43, 41, skin);
    ct_ellipse(canvas, 36 + sx, 64 + sy, 7, 11, skin);
    ct_ellipse(canvas, 124 + sx, 64 + sy, 7, 11, skin);
    ct_quad(
        canvas,
        35 + sx,
        41 + sy,
        47 + sx,
        17 + sy,
        112 + sx,
        17 + sy,
        128 + sx,
        41 + sy,
        cap);
    ct_rect(canvas, 37 + sx, 35 + sy, 91, 8, cap_dark);
    ct_rect(canvas, 47 + sx, 18 + sy, 30, 5, CT_RGB565(244, 167, 70));

    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int cx = pose->eye_x[eye_index] + sx;
        const int cy = pose->eye_y[eye_index] +
            ct_feature_y(pose, pose->eye_x[eye_index]);
        if (eye_index == 0U) {
            ct_ring(
                canvas,
                cx,
                cy,
                pose->eye_w[eye_index] / 2 + 5,
                pose->eye_h[eye_index] / 2 + 5,
                4,
                cap_dark,
                lens);
            ct_eye_ellipse(
                canvas,
                pose,
                eye_index,
                cx,
                cy,
                pose->eye_w[eye_index] - 5,
                pose->eye_open[eye_index],
                sclera,
                iris,
                ink);
            ct_line(
                canvas,
                cx - 10,
                cy - 8 + pose->detail_phase % 5U,
                cx + 6,
                cy - 13 + pose->detail_phase % 5U,
                lens);
        } else {
            ct_eye_ellipse(
                canvas,
                pose,
                eye_index,
                cx,
                cy,
                pose->eye_w[eye_index],
                pose->eye_open[eye_index],
                sclera,
                iris,
                ink);
        }
        ct_draw_brow(
            canvas,
            pose,
            eye_index,
            pose->eye_x[eye_index] + sx,
            pose->eye_w[eye_index],
            0,
            eye_index == 0U ? 2 : 3,
            ink);
    }
    ct_line(canvas, 80 + sx, 65 + sy, 75 + sx, 76 + sy, shade);
    ct_line(canvas, 108 + sx, 71 + sy, 117 + sx, 76 + sy, cap_dark);
    ct_draw_cheeks(canvas, pose, 76, CT_RGB565(192, 90, 75));
    ct_draw_mouth(
        FACE_CLOSEUP_TOON_TALKIE_MOON_MECHANIC,
        canvas,
        pose,
        ink,
        CT_RGB565(67, 30, 32),
        sclera,
        CT_RGB565(196, 76, 79),
        lip,
        2);
}

bool face_closeup_toon_render(
    face_closeup_toon_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!ct_style_valid(style) || render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_CLOSEUP_TOON_PIXEL_COUNT) {
        return false;
    }
    face_closeup_toon_pose_t pose;
    if (!face_closeup_toon_resolve(
            style, render_key, sample_clock, &pose)) {
        return false;
    }
    ct_canvas_t canvas = {rgb565};
    switch (style) {
    case FACE_CLOSEUP_TOON_BROW_DIALOGUE_DIRECTOR:
        ct_draw_brow_dialogue_director(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_SLEEP_WAKE_DREAMER:
        ct_draw_sleep_wake_dreamer(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_IRIS_PARALLAX_SCOUT:
        ct_draw_iris_parallax_scout(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_CAT_OPTICS_FAMILIAR:
        ct_draw_cat_optics_familiar(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_M5_MANGA_LEAD:
        ct_draw_m5_manga_lead(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_VGA_STAR_NAVIGATOR:
        ct_draw_vga_star_navigator(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_POCKET_RELAY_CREATURE:
        ct_draw_pocket_relay_creature(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_EGA_QUEST_SQUIRE:
        ct_draw_ega_quest_squire(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_VGA_ELDER_STORYTELLER:
        ct_draw_vga_elder_storyteller(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_TALKIE_MOON_MECHANIC:
        ct_draw_talkie_moon_mechanic(&canvas, &pose);
        break;
    case FACE_CLOSEUP_TOON_COUNT:
    default:
        return false;
    }
    return true;
}

bool face_closeup_toon_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_closeup_toon_style_t style;
    return face_closeup_toon_from_legacy_id(
               legacy_profile_id, &style) &&
        face_closeup_toon_render(
            style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
