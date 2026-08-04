#include "face_pixel_pack_internal.h"

#include <string.h>

typedef struct {
    int corner_left;
    int corner_right;
    int cheek;
    int squint_left;
    int squint_right;
    int brow_inner;
    int brow_outer_left;
    int brow_outer_right;
    int head_roll;
    int gaze_x;
    int gaze_y;
    int mouth_open_bias;
    int mouth_width_bias;
    int mouth_round_bias;
} fpp_expression_target_t;

static const fpp_expression_target_t
    EXPRESSION_TARGETS[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] =
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        [FACE_EXPRESSION_WARM] =
            {70, 70, 96, 32, 32, 35, 24, 24, 0, 0, 0, 0, 34, 0},
        [FACE_EXPRESSION_JOY] =
            {118, 118, 142, 92, 92, 48, 34, 34, 0, 0, -3, 28, 52, 0},
        [FACE_EXPRESSION_CONCERN] =
            {-78, -78, 28, 30, 30, 112, -72, -72, -10, 0, 8, -34, -32, 0},
        [FACE_EXPRESSION_SURPRISE] =
            {4, 4, 0, -48, -48, 82, 68, 68, 0, 0, -4, 86, -28, 96},
        [FACE_EXPRESSION_THOUGHTFUL] =
            {-34, 28, 18, 24, 112, 48, 52, -42, 14, -52, -26, -12, -12, 72},
        [FACE_EXPRESSION_SKEPTICAL] =
            {-68, 38, 22, 50, 176, -48, -64, 112, -20, 56, 2, -22, 20, 20},
        [FACE_EXPRESSION_DETERMINED] =
            {-50, -50, 46, 126, 126, -96, -74, -74, 0, 0, -8, -52, 42, 0},
        [FACE_EXPRESSION_SLEEPY] =
            {8, 8, 0, 210, 196, -80, -56, -56, 9, 0, 64, -86, -30, 44},
        [FACE_EXPRESSION_EXCITED] =
            {96, 96, 90, 0, 0, 92, 76, 76, 0, 0, -8, 78, 40, 30},
        [FACE_EXPRESSION_EMBARRASSED] =
            {80, 36, 190, 110, 60, 64, 62, -20, 12, 72, 14, -10, 22, 66},
};

typedef struct {
    int open;
    int width;
    int round;
    int press;
    int teeth;
} fpp_viseme_target_t;

int fpp_clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

int fpp_abs(int value)
{
    return value < 0 ? -value : value;
}

int fpp_min(int first, int second)
{
    return first < second ? first : second;
}

int fpp_max(int first, int second)
{
    return first > second ? first : second;
}

int fpp_lerp(int first, int second, int weight)
{
    return first + ((second - first) * weight + 127) / 255;
}

uint32_t fpp_hash32(uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

int32_t fpp_isqrt(uint32_t value)
{
    uint32_t result = 0U;
    uint32_t bit = 1U << 30U;
    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return (int32_t)result;
}

fpp_surface_t fpp_surface_attach(
    uint16_t *frame, int width, int height)
{
    fpp_surface_t surface = {
        .pixels = (uint8_t *)frame,
        .width = width,
        .height = height,
    };
    return surface;
}

void fpp_present(
    uint16_t *frame,
    const fpp_surface_t *surface,
    int scale_x,
    int scale_y,
    const uint16_t *palette)
{
    /*
     * The indexed surface aliases the first half of the RGB565 framebuffer.
     * Expanding from bottom-right keeps every indexed read ahead of its
     * two-byte destination write, so no scratch allocation is needed.
     */
    for (int y = FACE_PIXEL_PACK_HEIGHT - 1; y >= 0; --y) {
        const uint8_t *source =
            surface->pixels +
            (size_t)(y / scale_y) * (size_t)surface->width;
        uint16_t *destination =
            frame + (size_t)y * FACE_PIXEL_PACK_WIDTH;
        for (int x = FACE_PIXEL_PACK_WIDTH - 1; x >= 0; --x) {
            destination[x] = palette[source[x / scale_x]];
        }
    }
}

void fpp_clear(fpp_surface_t *surface, uint8_t colour)
{
    memset(
        surface->pixels, colour,
        (size_t)surface->width * (size_t)surface->height);
}

void fpp_pixel(
    fpp_surface_t *surface, int x, int y, uint8_t colour)
{
    if (x >= 0 && x < surface->width &&
        y >= 0 && y < surface->height) {
        surface->pixels[
            (size_t)y * (size_t)surface->width + (size_t)x] = colour;
    }
}

void fpp_hline(
    fpp_surface_t *surface, int x0, int x1, int y, uint8_t colour)
{
    if (y < 0 || y >= surface->height) {
        return;
    }
    if (x0 > x1) {
        const int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    x0 = fpp_max(x0, 0);
    x1 = fpp_min(x1, surface->width - 1);
    if (x0 > x1) {
        return;
    }
    uint8_t *row =
        surface->pixels + (size_t)y * (size_t)surface->width;
    for (int x = x0; x <= x1; ++x) {
        row[x] = colour;
    }
}

void fpp_vline(
    fpp_surface_t *surface, int x, int y0, int y1, uint8_t colour)
{
    if (x < 0 || x >= surface->width) {
        return;
    }
    if (y0 > y1) {
        const int swap = y0;
        y0 = y1;
        y1 = swap;
    }
    y0 = fpp_max(y0, 0);
    y1 = fpp_min(y1, surface->height - 1);
    for (int y = y0; y <= y1; ++y) {
        surface->pixels[
            (size_t)y * (size_t)surface->width + (size_t)x] = colour;
    }
}

void fpp_fill_rect(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t colour)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int row = 0; row < height; ++row) {
        fpp_hline(surface, x, x + width - 1, y + row, colour);
    }
}

void fpp_rect(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t colour)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    fpp_hline(surface, x, x + width - 1, y, colour);
    fpp_hline(surface, x, x + width - 1, y + height - 1, colour);
    fpp_vline(surface, x, y, y + height - 1, colour);
    fpp_vline(surface, x + width - 1, y, y + height - 1, colour);
}

void fpp_line(
    fpp_surface_t *surface,
    int x0,
    int y0,
    int x1,
    int y1,
    uint8_t colour)
{
    const int dx = fpp_abs(x1 - x0);
    const int dy = -fpp_abs(y1 - y0);
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        fpp_pixel(surface, x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void fpp_fill_ellipse(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius_x,
    int radius_y,
    uint8_t colour)
{
    if (radius_x <= 0 || radius_y <= 0) {
        fpp_pixel(surface, centre_x, centre_y, colour);
        return;
    }
    const uint32_t radius_y_squared =
        (uint32_t)(radius_y * radius_y);
    const uint32_t radius_x_squared =
        (uint32_t)(radius_x * radius_x);
    for (int offset_y = -radius_y;
         offset_y <= radius_y;
         ++offset_y) {
        const uint32_t remaining =
            radius_y_squared -
            (uint32_t)(offset_y * offset_y);
        const int offset_x = (int)fpp_isqrt(
            remaining * radius_x_squared / radius_y_squared);
        fpp_hline(
            surface, centre_x - offset_x, centre_x + offset_x,
            centre_y + offset_y, colour);
    }
}

void fpp_fill_circle(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius,
    uint8_t colour)
{
    fpp_fill_ellipse(
        surface, centre_x, centre_y, radius, radius, colour);
}

static bool checker_pick(
    unsigned density, int x, int y)
{
    static const uint8_t ORDER[4] = {0U, 2U, 3U, 1U};
    const unsigned threshold = fpp_min((int)density, 4);
    return ORDER[((unsigned)y & 1U) * 2U + ((unsigned)x & 1U)] <
           threshold;
}

void fpp_fill_checker(
    fpp_surface_t *surface,
    int x,
    int y,
    int width,
    int height,
    uint8_t first,
    uint8_t second,
    unsigned density)
{
    const int x1 = fpp_min(x + width - 1, surface->width - 1);
    const int y1 = fpp_min(y + height - 1, surface->height - 1);
    for (int target_y = fpp_max(y, 0);
         target_y <= y1;
         ++target_y) {
        for (int target_x = fpp_max(x, 0);
             target_x <= x1;
             ++target_x) {
            fpp_pixel(
                surface, target_x, target_y,
                checker_pick(density, target_x, target_y)
                    ? second
                    : first);
        }
    }
}

void fpp_fill_ellipse_checker(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    int radius_x,
    int radius_y,
    uint8_t first,
    uint8_t second,
    unsigned density)
{
    if (radius_x <= 0 || radius_y <= 0) {
        return;
    }
    const uint32_t radius_y_squared =
        (uint32_t)(radius_y * radius_y);
    const uint32_t radius_x_squared =
        (uint32_t)(radius_x * radius_x);
    for (int offset_y = -radius_y;
         offset_y <= radius_y;
         ++offset_y) {
        const uint32_t remaining =
            radius_y_squared -
            (uint32_t)(offset_y * offset_y);
        const int offset_x = (int)fpp_isqrt(
            remaining * radius_x_squared / radius_y_squared);
        for (int x = centre_x - offset_x;
             x <= centre_x + offset_x;
             ++x) {
            fpp_pixel(
                surface, x, centre_y + offset_y,
                checker_pick(density, x, centre_y + offset_y)
                    ? second
                    : first);
        }
    }
}

void fpp_dither_bayer_1bit(fpp_surface_t *surface)
{
    static const uint8_t BAYER8[8][8] = {
        {0, 32, 8, 40, 2, 34, 10, 42},
        {48, 16, 56, 24, 50, 18, 58, 26},
        {12, 44, 4, 36, 14, 46, 6, 38},
        {60, 28, 52, 20, 62, 30, 54, 22},
        {3, 35, 11, 43, 1, 33, 9, 41},
        {51, 19, 59, 27, 49, 17, 57, 25},
        {15, 47, 7, 39, 13, 45, 5, 37},
        {63, 31, 55, 23, 61, 29, 53, 21},
    };
    for (int y = 0; y < surface->height; ++y) {
        uint8_t *row =
            surface->pixels + (size_t)y * (size_t)surface->width;
        for (int x = 0; x < surface->width; ++x) {
            const int threshold =
                ((int)BAYER8[y & 7][x & 7] << 2) + 2;
            row[x] = row[x] > threshold ? 1U : 0U;
        }
    }
}

static uint8_t glyph_row(char character, int row)
{
    static const struct {
        char character;
        uint8_t rows[5];
    } GLYPHS[] = {
        {'A', {2, 5, 7, 5, 5}},
        {'B', {6, 5, 6, 5, 6}},
        {'C', {3, 4, 4, 4, 3}},
        {'D', {6, 5, 5, 5, 6}},
        {'E', {7, 4, 6, 4, 7}},
        {'F', {7, 4, 6, 4, 4}},
        {'G', {3, 4, 5, 5, 3}},
        {'H', {5, 5, 7, 5, 5}},
        {'I', {7, 2, 2, 2, 7}},
        {'J', {1, 1, 1, 5, 2}},
        {'K', {5, 5, 6, 5, 5}},
        {'L', {4, 4, 4, 4, 7}},
        {'M', {7, 7, 5, 5, 5}},
        {'N', {5, 7, 7, 5, 5}},
        {'O', {2, 5, 5, 5, 2}},
        {'P', {6, 5, 6, 4, 4}},
        {'Q', {2, 5, 5, 2, 1}},
        {'R', {6, 5, 6, 5, 5}},
        {'S', {3, 4, 2, 1, 6}},
        {'T', {7, 2, 2, 2, 2}},
        {'U', {5, 5, 5, 5, 7}},
        {'V', {5, 5, 5, 2, 2}},
        {'W', {5, 5, 5, 7, 7}},
        {'X', {5, 5, 2, 5, 5}},
        {'Y', {5, 5, 2, 2, 2}},
        {'Z', {7, 1, 2, 4, 7}},
    };
    for (size_t index = 0U;
         index < sizeof(GLYPHS) / sizeof(GLYPHS[0]);
         ++index) {
        if (GLYPHS[index].character == character) {
            return GLYPHS[index].rows[row];
        }
    }
    return 0U;
}

void fpp_text3x5(
    fpp_surface_t *surface,
    int x,
    int y,
    const char *text,
    uint8_t colour)
{
    for (size_t character = 0U;
         text[character] != '\0';
         ++character) {
        for (int row = 0; row < 5; ++row) {
            const uint8_t bits = glyph_row(text[character], row);
            for (int column = 0; column < 3; ++column) {
                if ((bits & (uint8_t)(4U >> column)) != 0U) {
                    fpp_pixel(
                        surface, x + column, y + row, colour);
                }
            }
        }
        x += 4;
    }
}

static fpp_viseme_target_t viseme_target(uint8_t set, uint8_t viseme)
{
    fpp_viseme_target_t target = {100, 140, 28, 8, 50};
    if (set == FACE_VISEME_SET_VRM5) {
        switch (viseme) {
        case 0:
            return (fpp_viseme_target_t){224, 146, 12, 0, 50};
        case 1:
            return (fpp_viseme_target_t){74, 228, 5, 0, 110};
        case 2:
            return (fpp_viseme_target_t){92, 80, 226, 0, 20};
        case 3:
            return (fpp_viseme_target_t){112, 210, 18, 0, 120};
        case 4:
            return (fpp_viseme_target_t){168, 92, 214, 0, 20};
        default:
            return target;
        }
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        static const fpp_viseme_target_t PRESTON[9] = {
            {0, 142, 10, 32, 0},
            {18, 150, 0, 225, 0},
            {78, 220, 4, 0, 210},
            {128, 176, 16, 0, 135},
            {220, 150, 12, 0, 55},
            {68, 88, 220, 0, 10},
            {174, 92, 210, 0, 15},
            {44, 190, 8, 90, 190},
            {0, 140, 0, 0, 0},
        };
        return viseme < 9U ? PRESTON[viseme] : target;
    }
    if (set != FACE_VISEME_SET_OVR15) {
        return target;
    }
    switch (viseme) {
    case FACE_VISEME_AA:
        return (fpp_viseme_target_t){232, 148, 8, 0, 48};
    case FACE_VISEME_E:
    case FACE_VISEME_I:
        return (fpp_viseme_target_t){82, 224, 6, 0, 120};
    case FACE_VISEME_O:
        return (fpp_viseme_target_t){184, 94, 220, 0, 18};
    case FACE_VISEME_U:
        return (fpp_viseme_target_t){92, 82, 232, 0, 12};
    case FACE_VISEME_PP:
        return (fpp_viseme_target_t){0, 150, 0, 235, 0};
    case FACE_VISEME_SS:
    case FACE_VISEME_DD:
    case FACE_VISEME_NN:
        return (fpp_viseme_target_t){52, 198, 4, 76, 224};
    case FACE_VISEME_TH:
        return (fpp_viseme_target_t){88, 184, 4, 30, 185};
    case FACE_VISEME_FF:
        return (fpp_viseme_target_t){48, 190, 2, 150, 232};
    case FACE_VISEME_KK:
    case FACE_VISEME_RR:
    case FACE_VISEME_CH:
        return (fpp_viseme_target_t){128, 156, 22, 4, 82};
    case FACE_VISEME_SIL:
        return (fpp_viseme_target_t){0, 140, 0, 16, 0};
    default:
        return target;
    }
}

static int expression_signed(
    int explicit_value, int target, int weight)
{
    const int scaled = target * weight / 255;
    if (explicit_value == 0 || target == 0) {
        return target == 0 ? explicit_value : scaled;
    }
    return (explicit_value + scaled) / 2;
}

static int expression_unsigned(
    int explicit_value, int target, int weight)
{
    const int scaled = target * weight / 255;
    return fpp_max(explicit_value, scaled);
}

static int blink_envelope(uint32_t sample_clock, uint32_t salt)
{
    enum {
        SLOT_SAMPLES = 56000,
        CLOSE_SAMPLES = 1440,
        HOLD_SAMPLES = 640,
        OPEN_SAMPLES = 2080,
        TOTAL_SAMPLES =
            CLOSE_SAMPLES + HOLD_SAMPLES + OPEN_SAMPLES,
    };
    const uint32_t slot = sample_clock / SLOT_SAMPLES;
    const uint32_t position = sample_clock % SLOT_SAMPLES;
    const uint32_t hash =
        fpp_hash32(salt ^ (slot * 0x9e3779b9U));
    const uint32_t start =
        4000U + hash % (SLOT_SAMPLES - TOTAL_SAMPLES - 8000U);
    if (position < start || position >= start + TOTAL_SAMPLES) {
        return 255;
    }
    uint32_t elapsed = position - start;
    if (elapsed < CLOSE_SAMPLES) {
        return 255 - (int)(elapsed * 255U / CLOSE_SAMPLES);
    }
    elapsed -= CLOSE_SAMPLES;
    if (elapsed < HOLD_SAMPLES) {
        return 0;
    }
    elapsed -= HOLD_SAMPLES;
    return (int)(elapsed * 255U / OPEN_SAMPLES);
}

static void resolve_saccade(
    uint32_t sample_clock,
    uint32_t salt,
    int *x,
    int *y)
{
    enum {
        SLOT_SAMPLES = 19200,
        MOVE_SAMPLES = 2240,
    };
    const uint32_t slot = sample_clock / SLOT_SAMPLES;
    const uint32_t position = sample_clock % SLOT_SAMPLES;
    const uint32_t current =
        fpp_hash32(salt ^ (slot * 0x85ebca6bU));
    const uint32_t previous =
        fpp_hash32(salt ^ ((slot - 1U) * 0x85ebca6bU));
    int current_x = (int)((current >> 2U) % 5U) - 2;
    int current_y = (int)((current >> 7U) % 3U) - 1;
    int previous_x = (int)((previous >> 2U) % 5U) - 2;
    int previous_y = (int)((previous >> 7U) % 3U) - 1;
    if ((current & 7U) < 5U) {
        current_x = 0;
        current_y = 0;
    }
    if ((previous & 7U) < 5U) {
        previous_x = 0;
        previous_y = 0;
    }
    if (position < MOVE_SAMPLES) {
        const int linear =
            (int)(position * 255U / MOVE_SAMPLES);
        const int smooth =
            (linear * linear * (765 - 2 * linear) + 32512) /
            65025;
        *x = fpp_lerp(previous_x, current_x, smooth);
        *y = fpp_lerp(previous_y, current_y, smooth);
    } else {
        *x = current_x;
        *y = current_y;
    }
}

void fpp_resolve_pose(
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint32_t salt,
    fpp_pose_t *pose)
{
    const unsigned expression =
        key->stage_expression < FACE_EXPRESSION_COUNT
            ? key->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const int expression_weight = key->expression_weight;
    const fpp_expression_target_t *target =
        &EXPRESSION_TARGETS[expression];

    fpp_viseme_target_t primary =
        viseme_target(key->viseme_set, key->viseme);
    const fpp_viseme_target_t secondary =
        viseme_target(key->viseme_set, key->viseme_secondary);
    if (key->viseme_secondary != FACE_VISEME_NONE &&
        key->viseme_blend > 0U) {
        const int blend = key->viseme_blend;
        primary.open = fpp_lerp(primary.open, secondary.open, blend);
        primary.width = fpp_lerp(primary.width, secondary.width, blend);
        primary.round = fpp_lerp(primary.round, secondary.round, blend);
        primary.press = fpp_lerp(primary.press, secondary.press, blend);
        primary.teeth = fpp_lerp(primary.teeth, secondary.teeth, blend);
    }
    const int viseme_weight = key->viseme_weight / 3;
    pose->mouth_open = fpp_lerp(
        key->controls.mouth_open, primary.open, viseme_weight);
    pose->mouth_width = fpp_lerp(
        key->controls.mouth_width, primary.width, viseme_weight);
    pose->mouth_round = fpp_lerp(
        key->controls.mouth_round, primary.round, viseme_weight);
    pose->mouth_press = fpp_lerp(
        key->controls.mouth_press, primary.press, viseme_weight);
    pose->mouth_teeth = fpp_lerp(
        key->controls.mouth_teeth, primary.teeth, viseme_weight);

    pose->mouth_open = fpp_clamp(
        pose->mouth_open +
            target->mouth_open_bias * expression_weight / 255,
        0, 255);
    pose->mouth_width = fpp_clamp(
        pose->mouth_width +
            target->mouth_width_bias * expression_weight / 255,
        0, 255);
    pose->mouth_round = fpp_clamp(
        pose->mouth_round +
            target->mouth_round_bias * expression_weight / 255,
        0, 255);
    pose->mouth_corner_left = expression_signed(
        key->mouth_corner_left, target->corner_left,
        expression_weight);
    pose->mouth_corner_right = expression_signed(
        key->mouth_corner_right, target->corner_right,
        expression_weight);
    pose->cheek = expression_unsigned(
        key->cheek, target->cheek, expression_weight);
    const int squint_left = expression_unsigned(
        key->eye_left_squint, target->squint_left,
        expression_weight);
    const int squint_right = expression_unsigned(
        key->eye_right_squint, target->squint_right,
        expression_weight);

    int blink = blink_envelope(sample_clock, salt);
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        blink = 0;
    }
    pose->eye_open_left =
        (int)key->controls.eye_left_open * blink / 255;
    pose->eye_open_right =
        (int)key->controls.eye_right_open * blink / 255;
    pose->eye_open_left =
        pose->eye_open_left *
        fpp_clamp(255 - squint_left, 18, 255) / 255;
    pose->eye_open_right =
        pose->eye_open_right *
        fpp_clamp(255 - squint_right, 18, 255) / 255;

    int saccade_x;
    int saccade_y;
    resolve_saccade(
        sample_clock, salt, &saccade_x, &saccade_y);
    pose->gaze_x = fpp_clamp(
        (int)key->controls.look_x +
            target->gaze_x * expression_weight / 255 +
            saccade_x * 10,
        -127, 127);
    pose->gaze_y = fpp_clamp(
        (int)key->controls.look_y +
            target->gaze_y * expression_weight / 255 +
            saccade_y * 10,
        -127, 127);
    pose->brow_inner = expression_signed(
        key->brow_inner, target->brow_inner, expression_weight);
    pose->brow_outer_left = expression_signed(
        key->brow_outer_left, target->brow_outer_left,
        expression_weight);
    pose->brow_outer_right = expression_signed(
        key->brow_outer_right, target->brow_outer_right,
        expression_weight);
    if (pose->brow_inner == 0 &&
        pose->brow_outer_left == 0 &&
        pose->brow_outer_right == 0) {
        pose->brow_inner = key->controls.brow;
        pose->brow_outer_left = key->controls.brow;
        pose->brow_outer_right = key->controls.brow;
    }
    pose->head_roll = fpp_clamp(
        expression_signed(
            key->head_roll, target->head_roll, expression_weight),
        -127, 127);
    pose->head_yaw = key->head_yaw;
    pose->head_pitch = key->head_pitch;
    pose->body_lean_x = key->body_lean_x;
    pose->body_lean_y = key->body_lean_y;
    pose->valence = key->affect_valence;
    pose->arousal = key->affect_arousal;

    const uint32_t breath_phase =
        (sample_clock % 60800U) * 256U / 60800U;
    const int triangle =
        breath_phase < 128U
            ? (int)breath_phase * 2 - 127
            : 383 - (int)breath_phase * 2;
    pose->breath = triangle;
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U) {
        const uint32_t bob_phase =
            (sample_clock % 4480U) * 256U / 4480U;
        const int bob_triangle =
            bob_phase < 128U
                ? (int)bob_phase * 2 - 127
                : 383 - (int)bob_phase * 2;
        pose->speech_bob =
            bob_triangle * pose->mouth_open / 255;
    } else {
        pose->speech_bob = 0;
    }
    pose->stage_expression = (int)expression;
}

fpp_mouth_shape_t fpp_classify_mouth(const fpp_pose_t *pose)
{
    if (pose->mouth_press > 150 && pose->mouth_open < 65) {
        return FPP_MOUTH_MBP;
    }
    if (pose->mouth_teeth > 155 &&
        pose->mouth_press > 85 &&
        pose->mouth_open < 115) {
        return FPP_MOUTH_FV;
    }
    if (pose->mouth_open < 27) {
        return pose->mouth_teeth > 120
                   ? FPP_MOUTH_SS
                   : FPP_MOUTH_REST;
    }
    if (pose->mouth_round > 140) {
        return pose->mouth_open > 130
                   ? FPP_MOUTH_OH
                   : FPP_MOUTH_OO;
    }
    if (pose->mouth_open > 182) {
        return FPP_MOUTH_AA;
    }
    if (pose->mouth_width > 164 && pose->mouth_open < 112) {
        return FPP_MOUTH_EE;
    }
    if (pose->mouth_open > 96) {
        return FPP_MOUTH_EH;
    }
    return FPP_MOUTH_SMALL;
}

typedef struct {
    const char *const *rows;
    uint8_t row_count;
    uint8_t width;
} mouth_sprite_t;

static const char *const MOUTH_REST[] = {
    "..o.........o..", ".ooooooooooooo.", "..lllllllllll.."};
static const char *const MOUTH_MBP[] = {
    "...lllllllll...", "..ooooooooooo..", "...lllllllll..."};
static const char *const MOUTH_FV[] = {
    "..ooooooooooo..", ".ottttttttttto.", "..lllllllllll..",
    "...ooooooooo..."};
static const char *const MOUTH_SS[] = {
    ".ooooooooooooo.", "ottttttttttttto", "o.ooooooooooo.o",
    ".ooooooooooooo."};
static const char *const MOUTH_EE[] = {
    ".ooooooooooooo.", "ottttttttttttto", "occccccccccccco",
    "ottttttttttttto", ".ooooooooooooo."};
static const char *const MOUTH_EH[] = {
    "...ooooooooo...", "..ottttttttto..", "..occccccccco..",
    "..ocgggggggco..", "...ooooooooo..."};
static const char *const MOUTH_AA[] = {
    "...ooooooooo...", "..ottttttttto..", ".occccccccccco.",
    ".occccccccccco.", ".ocgggggggggco.", "..occccccccco..",
    "...ooooooooo..."};
static const char *const MOUTH_OO[] = {
    ".....ooooo.....", "....olccclo....", "....olccclo....",
    ".....ooooo....."};
static const char *const MOUTH_OH[] = {
    "....ooooooo....", "...ottttttto...", "...occccccco...",
    "...ocgggggco...", "...occccccco...", "....ooooooo...."};
static const char *const MOUTH_SMALL[] = {
    "...ooooooooo...", "..occccccccco..", "...ooooooooo..."};

static const mouth_sprite_t MOUTHS[FPP_MOUTH_SHAPE_COUNT] = {
    [FPP_MOUTH_REST] = {MOUTH_REST, 3, 15},
    [FPP_MOUTH_MBP] = {MOUTH_MBP, 3, 15},
    [FPP_MOUTH_FV] = {MOUTH_FV, 4, 15},
    [FPP_MOUTH_SS] = {MOUTH_SS, 4, 15},
    [FPP_MOUTH_EE] = {MOUTH_EE, 5, 15},
    [FPP_MOUTH_EH] = {MOUTH_EH, 5, 15},
    [FPP_MOUTH_AA] = {MOUTH_AA, 7, 15},
    [FPP_MOUTH_OO] = {MOUTH_OO, 4, 15},
    [FPP_MOUTH_OH] = {MOUTH_OH, 6, 15},
    [FPP_MOUTH_SMALL] = {MOUTH_SMALL, 3, 15},
};

void fpp_draw_mouth_sprite(
    fpp_surface_t *surface,
    int centre_x,
    int centre_y,
    fpp_mouth_shape_t shape,
    const uint8_t colours[5])
{
    if ((int)shape < 0 || shape >= FPP_MOUTH_SHAPE_COUNT) {
        shape = FPP_MOUTH_REST;
    }
    const mouth_sprite_t *sprite = &MOUTHS[shape];
    const int origin_x = centre_x - sprite->width / 2;
    const int origin_y = centre_y - sprite->row_count / 2;
    for (int y = 0; y < sprite->row_count; ++y) {
        const char *row = sprite->rows[y];
        for (int x = 0; row[x] != '\0'; ++x) {
            uint8_t colour;
            switch (row[x]) {
            case 'o':
                colour = colours[0];
                break;
            case 'l':
                colour = colours[1];
                break;
            case 'c':
                colour = colours[2];
                break;
            case 't':
                colour = colours[3];
                break;
            case 'g':
                colour = colours[4];
                break;
            default:
                continue;
            }
            fpp_pixel(
                surface, origin_x + x, origin_y + y, colour);
        }
    }
}

void fpp_draw_polygon_lips(
    fpp_surface_t *surface,
    const fpp_lips_t *lips,
    const fpp_pose_t *pose)
{
    int width =
        lips->half_width +
        (lips->half_width * (pose->mouth_width - 128)) / 512;
    width -=
        lips->half_width * pose->mouth_round / 640;
    width = fpp_max(width, 3);
    int opening = pose->mouth_open * lips->max_open / 255;
    opening = fpp_max(
        opening - pose->mouth_press * lips->max_open / 512, 0);
    const int width_squared = width * width;
    const int smile_left = pose->mouth_corner_left / 38;
    const int smile_right = pose->mouth_corner_right / 38;
    for (int offset_x = -width;
         offset_x <= width;
         ++offset_x) {
        const int arch =
            (width_squared - offset_x * offset_x) * 255 /
            width_squared;
        const int gap = opening * arch / 255;
        const int corner =
            fpp_lerp(
                smile_left, smile_right,
                (offset_x + width) * 255 / (width * 2));
        const int centre_y = lips->centre_y - corner;
        const int top = centre_y - gap / 2;
        const int bottom = centre_y + (gap + 1) / 2;
        const int x = lips->centre_x + offset_x;
        if (gap <= 0) {
            fpp_pixel(surface, x, centre_y, lips->lip_dark);
            fpp_pixel(surface, x, centre_y + 1, lips->lip_mid);
            continue;
        }
        fpp_vline(surface, x, top, bottom, lips->cavity);
        if (pose->mouth_teeth > 62 && gap > 2) {
            const int teeth_rows =
                1 + (pose->mouth_teeth - 62) *
                        fpp_max(gap / 2, 1) /
                        193;
            fpp_vline(
                surface, x, top,
                top + fpp_min(teeth_rows, gap / 2),
                lips->teeth);
        }
        if (gap > 5 &&
            offset_x * offset_x < width_squared * 2 / 5) {
            fpp_vline(
                surface, x, bottom - gap / 4, bottom,
                lips->tongue);
        }
        fpp_pixel(surface, x, top - 1, lips->lip_dark);
        fpp_pixel(surface, x, bottom + 1, lips->lip_dark);
        fpp_pixel(surface, x, bottom + 2, lips->lip_mid);
    }
    fpp_pixel(
        surface, lips->centre_x - width - 1,
        lips->centre_y - smile_left, lips->lip_dark);
    fpp_pixel(
        surface, lips->centre_x + width + 1,
        lips->centre_y - smile_right, lips->lip_dark);
}
