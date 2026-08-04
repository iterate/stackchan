#include "face_authored_variants.h"

#include <string.h>

#include "face_sprite_actors.h"
#include "face_stage.h"
#include "fta.h"

#define FAV_RGB565(r, g, b)                                               \
    ((uint16_t)((((uint16_t)(r) & 0xf8U) << 8U) |                         \
                (((uint16_t)(g) & 0xfcU) << 3U) |                         \
                ((uint16_t)(b) >> 3U)))

typedef enum {
    FAV_OVERLAY_BEAN_SCOUT = 0,
    FAV_OVERLAY_BEAN_PUPPET,
    FAV_OVERLAY_BEAN_MANGA,
    FAV_OVERLAY_INK_EDITOR,
    FAV_OVERLAY_INK_MIME,
    FAV_OVERLAY_INK_BLUEPRINT,
    FAV_OVERLAY_CAPTAIN_COMMAND,
    FAV_OVERLAY_CAPTAIN_NEBULA,
    FAV_OVERLAY_CAPTAIN_ROGUE,
} fav_overlay_t;

typedef struct {
    const char *slug;
    const char *name;
    fav_lineage_t lineage;
    fav_overlay_t overlay;
    uint16_t shadow;
    uint16_t highlight;
    uint8_t source_color;
    uint8_t contrast;
    uint16_t ink;
    uint16_t primary;
    uint16_t secondary;
    uint16_t light;
} fav_style_t;

static const fav_style_t FAV_STYLES[FAV_PROFILE_COUNT] = {
    [FAV_PROFILE_BEAN_APPEAL_SCOUT] = {
        "bean-appeal-scout", "Bean · Appeal Scout",
        FAV_LINEAGE_TOON_BEAN, FAV_OVERLAY_BEAN_SCOUT,
        FAV_RGB565(6, 24, 42), FAV_RGB565(247, 242, 201), 150U, 20U,
        FAV_RGB565(12, 32, 46), FAV_RGB565(36, 184, 180),
        FAV_RGB565(238, 170, 46), FAV_RGB565(245, 246, 224),
    },
    [FAV_PROFILE_BEAN_CLOCKWORK_PUPPET] = {
        "bean-clockwork-puppet", "Bean · Clockwork Puppet",
        FAV_LINEAGE_TOON_BEAN, FAV_OVERLAY_BEAN_PUPPET,
        FAV_RGB565(42, 18, 35), FAV_RGB565(245, 190, 118), 130U, 28U,
        FAV_RGB565(42, 25, 30), FAV_RGB565(167, 82, 45),
        FAV_RGB565(234, 173, 48), FAV_RGB565(255, 222, 148),
    },
    [FAV_PROFILE_BEAN_MANGA_SPARK] = {
        "bean-manga-spark", "Bean · Manga Spark",
        FAV_LINEAGE_TOON_BEAN, FAV_OVERLAY_BEAN_MANGA,
        FAV_RGB565(35, 24, 58), FAV_RGB565(255, 224, 228), 142U, 24U,
        FAV_RGB565(45, 25, 65), FAV_RGB565(174, 64, 148),
        FAV_RGB565(58, 190, 214), FAV_RGB565(255, 242, 244),
    },
    [FAV_PROFILE_INK_NEWSROOM_EDITOR] = {
        "ink-newsroom-editor", "Ink · Newsroom Editor",
        FAV_LINEAGE_TOON_INK, FAV_OVERLAY_INK_EDITOR,
        FAV_RGB565(27, 30, 32), FAV_RGB565(242, 231, 196), 38U, 38U,
        FAV_RGB565(28, 30, 31), FAV_RGB565(112, 92, 65),
        FAV_RGB565(178, 54, 45), FAV_RGB565(248, 238, 206),
    },
    [FAV_PROFILE_INK_CABARET_MIME] = {
        "ink-cabaret-mime", "Ink · Cabaret Mime",
        FAV_LINEAGE_TOON_INK, FAV_OVERLAY_INK_MIME,
        FAV_RGB565(30, 20, 30), FAV_RGB565(255, 241, 216), 72U, 34U,
        FAV_RGB565(34, 28, 34), FAV_RGB565(156, 28, 57),
        FAV_RGB565(225, 72, 90), FAV_RGB565(255, 244, 224),
    },
    [FAV_PROFILE_INK_BLUEPRINT_COMPANION] = {
        "ink-blueprint-companion", "Ink · Blueprint Companion",
        FAV_LINEAGE_TOON_INK, FAV_OVERLAY_INK_BLUEPRINT,
        FAV_RGB565(4, 20, 48), FAV_RGB565(113, 236, 238), 0U, 44U,
        FAV_RGB565(114, 242, 242), FAV_RGB565(22, 125, 174),
        FAV_RGB565(246, 174, 58), FAV_RGB565(194, 255, 248),
    },
    [FAV_PROFILE_CAPTAIN_COMMAND_DECK] = {
        "captain-command-deck", "Captain · Command Deck",
        FAV_LINEAGE_VGA_STAR_CAPTAIN, FAV_OVERLAY_CAPTAIN_COMMAND,
        FAV_RGB565(3, 14, 37), FAV_RGB565(245, 210, 154), 185U, 26U,
        FAV_RGB565(12, 20, 38), FAV_RGB565(38, 104, 188),
        FAV_RGB565(242, 179, 48), FAV_RGB565(247, 228, 176),
    },
    [FAV_PROFILE_CAPTAIN_NEBULA_DOME] = {
        "captain-nebula-dome", "Captain · Nebula Dome",
        FAV_LINEAGE_VGA_STAR_CAPTAIN, FAV_OVERLAY_CAPTAIN_NEBULA,
        FAV_RGB565(13, 10, 50), FAV_RGB565(206, 199, 255), 118U, 30U,
        FAV_RGB565(17, 13, 47), FAV_RGB565(115, 74, 194),
        FAV_RGB565(62, 219, 226), FAV_RGB565(226, 224, 255),
    },
    [FAV_PROFILE_CAPTAIN_SOLAR_ROGUE] = {
        "captain-solar-rogue", "Captain · Solar Rogue",
        FAV_LINEAGE_VGA_STAR_CAPTAIN, FAV_OVERLAY_CAPTAIN_ROGUE,
        FAV_RGB565(36, 15, 15), FAV_RGB565(249, 195, 113), 112U, 34U,
        FAV_RGB565(35, 20, 20), FAV_RGB565(154, 45, 36),
        FAV_RGB565(247, 155, 35), FAV_RGB565(255, 224, 159),
    },
};

static int32_t fav_clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static uint8_t fav_clamp_u8(int32_t value)
{
    return (uint8_t)fav_clamp_i32(value, 0, 255);
}

static int8_t fav_clamp_i8(int32_t value)
{
    return (int8_t)fav_clamp_i32(value, -127, 127);
}

static uint8_t fav_r8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 11U) & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint8_t fav_g8(uint16_t color)
{
    const uint8_t value = (uint8_t)((color >> 5U) & 0x3fU);
    return (uint8_t)((value << 2U) | (value >> 4U));
}

static uint8_t fav_b8(uint16_t color)
{
    const uint8_t value = (uint8_t)(color & 0x1fU);
    return (uint8_t)((value << 3U) | (value >> 2U));
}

static uint16_t fav_pack_rgb(int32_t red, int32_t green, int32_t blue)
{
    return FAV_RGB565(
        fav_clamp_u8(red), fav_clamp_u8(green), fav_clamp_u8(blue));
}

static uint16_t fav_blend(uint16_t under, uint16_t over, uint8_t alpha)
{
    const int32_t inverse = 255 - alpha;
    const int32_t red =
        ((int32_t)fav_r8(under) * inverse +
         (int32_t)fav_r8(over) * alpha + 127) /
        255;
    const int32_t green =
        ((int32_t)fav_g8(under) * inverse +
         (int32_t)fav_g8(over) * alpha + 127) /
        255;
    const int32_t blue =
        ((int32_t)fav_b8(under) * inverse +
         (int32_t)fav_b8(over) * alpha + 127) /
        255;
    return fav_pack_rgb(red, green, blue);
}

static uint16_t fav_tone_pixel(uint16_t source, const fav_style_t *style)
{
    const int32_t source_r = fav_r8(source);
    const int32_t source_g = fav_g8(source);
    const int32_t source_b = fav_b8(source);
    int32_t luma =
        (77 * source_r + 150 * source_g + 29 * source_b + 128) >> 8;
    luma =
        128 + ((luma - 128) * (256 + (int32_t)style->contrast)) / 256;
    luma = fav_clamp_i32(luma, 0, 255);

    const int32_t shadow_r = fav_r8(style->shadow);
    const int32_t shadow_g = fav_g8(style->shadow);
    const int32_t shadow_b = fav_b8(style->shadow);
    const int32_t light_r = fav_r8(style->highlight);
    const int32_t light_g = fav_g8(style->highlight);
    const int32_t light_b = fav_b8(style->highlight);
    const int32_t tone_r =
        shadow_r + ((light_r - shadow_r) * luma + 127) / 255;
    const int32_t tone_g =
        shadow_g + ((light_g - shadow_g) * luma + 127) / 255;
    const int32_t tone_b =
        shadow_b + ((light_b - shadow_b) * luma + 127) / 255;
    const int32_t keep = style->source_color;
    const int32_t tint = 255 - keep;
    return fav_pack_rgb(
        (tone_r * tint + source_r * keep + 127) / 255,
        (tone_g * tint + source_g * keep + 127) / 255,
        (tone_b * tint + source_b * keep + 127) / 255);
}

/*
 * A blueprint needs dark paper and luminous construction lines, which is the
 * inverse of Toon Ink's pale paper and dark ink. Preserve the exact source
 * background as navy, then invert only the artwork luminance into a cyan
 * drafting range. This keeps the face plate visibly separate from the page
 * while making eyes, brows and mouth read as light linework at 40x30.
 */
static uint16_t fav_blueprint_pixel(
    uint16_t source, uint16_t source_background,
    const fav_style_t *style)
{
    if (source == source_background) {
        return style->shadow;
    }
    const int32_t source_r = fav_r8(source);
    const int32_t source_g = fav_g8(source);
    const int32_t source_b = fav_b8(source);
    const int32_t luma =
        (77 * source_r + 150 * source_g + 29 * source_b + 128) >> 8;
    int32_t inverse = 255 - luma;
    inverse =
        128 + ((inverse - 128) * (288 + (int32_t)style->contrast)) / 288;
    inverse = fav_clamp_i32(inverse, 0, 255);

    const uint16_t plate = FAV_RGB565(8, 51, 76);
    const int32_t plate_r = fav_r8(plate);
    const int32_t plate_g = fav_g8(plate);
    const int32_t plate_b = fav_b8(plate);
    const int32_t line_r = fav_r8(style->light);
    const int32_t line_g = fav_g8(style->light);
    const int32_t line_b = fav_b8(style->light);
    return fav_pack_rgb(
        plate_r + ((line_r - plate_r) * inverse + 127) / 255,
        plate_g + ((line_g - plate_g) * inverse + 127) / 255,
        plate_b + ((line_b - plate_b) * inverse + 127) / 255);
}

static void fav_tone_frame(uint16_t *pixels, const fav_style_t *style)
{
    const uint16_t source_background = pixels[0];
    if (style->overlay == FAV_OVERLAY_INK_BLUEPRINT) {
        for (size_t index = 0U;
             index < (size_t)FAV_PIXEL_COUNT; ++index) {
            pixels[index] = fav_blueprint_pixel(
                pixels[index], source_background, style);
        }
        return;
    }
    for (size_t index = 0U; index < (size_t)FAV_PIXEL_COUNT; ++index) {
        pixels[index] = fav_tone_pixel(pixels[index], style);
    }
}

static void fav_pixel(
    uint16_t *pixels, int32_t x, int32_t y, uint16_t color)
{
    if (x >= 0 && x < FAV_FRAME_WIDTH && y >= 0 &&
        y < FAV_FRAME_HEIGHT) {
        pixels[(size_t)y * FAV_FRAME_WIDTH + (size_t)x] = color;
    }
}

static void fav_pixel_alpha(
    uint16_t *pixels, int32_t x, int32_t y,
    uint16_t color, uint8_t alpha)
{
    if (x >= 0 && x < FAV_FRAME_WIDTH && y >= 0 &&
        y < FAV_FRAME_HEIGHT) {
        const size_t index =
            (size_t)y * FAV_FRAME_WIDTH + (size_t)x;
        pixels[index] = fav_blend(pixels[index], color, alpha);
    }
}

static void fav_rect(
    uint16_t *pixels,
    int32_t left, int32_t top, int32_t right, int32_t bottom,
    uint16_t color)
{
    left = fav_clamp_i32(left, 0, FAV_FRAME_WIDTH);
    right = fav_clamp_i32(right, 0, FAV_FRAME_WIDTH);
    top = fav_clamp_i32(top, 0, FAV_FRAME_HEIGHT);
    bottom = fav_clamp_i32(bottom, 0, FAV_FRAME_HEIGHT);
    for (int32_t y = top; y < bottom; ++y) {
        for (int32_t x = left; x < right; ++x) {
            pixels[(size_t)y * FAV_FRAME_WIDTH + (size_t)x] = color;
        }
    }
}

static void fav_circle(
    uint16_t *pixels,
    int32_t center_x, int32_t center_y, int32_t radius,
    uint16_t color)
{
    if (radius <= 0) {
        return;
    }
    const int32_t radius_squared = radius * radius;
    for (int32_t y = -radius; y <= radius; ++y) {
        for (int32_t x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius_squared) {
                fav_pixel(pixels, center_x + x, center_y + y, color);
            }
        }
    }
}

static void fav_ring(
    uint16_t *pixels,
    int32_t center_x, int32_t center_y,
    int32_t radius_x, int32_t radius_y, int32_t thickness,
    uint16_t color)
{
    if (radius_x <= 0 || radius_y <= 0 || thickness <= 0) {
        return;
    }
    const int64_t rx2 = (int64_t)radius_x * radius_x;
    const int64_t ry2 = (int64_t)radius_y * radius_y;
    const int32_t inner_x = fav_clamp_i32(radius_x - thickness, 1, radius_x);
    const int32_t inner_y = fav_clamp_i32(radius_y - thickness, 1, radius_y);
    const int64_t ix2 = (int64_t)inner_x * inner_x;
    const int64_t iy2 = (int64_t)inner_y * inner_y;
    const int64_t outer_limit = rx2 * ry2;
    const int64_t inner_limit = ix2 * iy2;
    for (int32_t y = -radius_y; y <= radius_y; ++y) {
        for (int32_t x = -radius_x; x <= radius_x; ++x) {
            const int64_t outer =
                (int64_t)x * x * ry2 + (int64_t)y * y * rx2;
            if (outer > outer_limit) {
                continue;
            }
            const int64_t inner =
                (int64_t)x * x * iy2 + (int64_t)y * y * ix2;
            if (inner >= inner_limit) {
                fav_pixel(pixels, center_x + x, center_y + y, color);
            }
        }
    }
}

static void fav_line(
    uint16_t *pixels,
    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    int32_t thickness, uint16_t color)
{
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    const int32_t sx = dx < 0 ? -1 : 1;
    const int32_t sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int32_t error = dx - dy;
    const int32_t radius = fav_clamp_i32(thickness / 2, 0, 4);
    for (;;) {
        if (radius == 0) {
            fav_pixel(pixels, x0, y0, color);
        } else {
            fav_circle(pixels, x0, y0, radius, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t twice = error * 2;
        if (twice > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (twice < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static int64_t fav_edge(
    int32_t ax, int32_t ay, int32_t bx, int32_t by,
    int32_t px, int32_t py)
{
    return (int64_t)(px - ax) * (by - ay) -
           (int64_t)(py - ay) * (bx - ax);
}

static void fav_triangle(
    uint16_t *pixels,
    int32_t x0, int32_t y0,
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2,
    uint16_t color)
{
    const int32_t left = fav_clamp_i32(
        x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2),
        0, FAV_FRAME_WIDTH - 1);
    const int32_t right = fav_clamp_i32(
        x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2),
        0, FAV_FRAME_WIDTH - 1);
    const int32_t top = fav_clamp_i32(
        y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2),
        0, FAV_FRAME_HEIGHT - 1);
    const int32_t bottom = fav_clamp_i32(
        y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2),
        0, FAV_FRAME_HEIGHT - 1);
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            const int64_t a = fav_edge(x0, y0, x1, y1, x, y);
            const int64_t b = fav_edge(x1, y1, x2, y2, x, y);
            const int64_t c = fav_edge(x2, y2, x0, y0, x, y);
            const bool negative = a < 0 || b < 0 || c < 0;
            const bool positive = a > 0 || b > 0 || c > 0;
            if (!(negative && positive)) {
                fav_pixel(pixels, x, y, color);
            }
        }
    }
}

static uint8_t fav_expression(const face_render_key_t *key)
{
    if (key->stage_expression < FACE_EXPRESSION_COUNT &&
        key->expression_weight > 0U) {
        return key->stage_expression;
    }
    return FACE_EXPRESSION_NEUTRAL;
}

static uint16_t fav_emotion_accent(
    const fav_style_t *style, uint8_t expression)
{
    switch (expression) {
    case FACE_EXPRESSION_JOY:
    case FACE_EXPRESSION_EXCITED:
    case FACE_EXPRESSION_SURPRISE:
        return style->secondary;
    case FACE_EXPRESSION_CONCERN:
    case FACE_EXPRESSION_EMBARRASSED:
        return FAV_RGB565(224, 65, 78);
    case FACE_EXPRESSION_THOUGHTFUL:
    case FACE_EXPRESSION_SKEPTICAL:
        return FAV_RGB565(78, 198, 214);
    case FACE_EXPRESSION_DETERMINED:
        return FAV_RGB565(242, 125, 38);
    case FACE_EXPRESSION_SLEEPY:
        return fav_blend(style->ink, style->primary, 72U);
    default:
        return style->primary;
    }
}

static void fav_pose_offset(
    const face_render_key_t *key, int32_t *offset_x, int32_t *offset_y)
{
    *offset_x =
        ((int32_t)key->head_yaw * 4) / 127 +
        ((int32_t)key->body_lean_x * 2) / 127;
    *offset_y =
        ((int32_t)key->head_pitch * 3) / 127 -
        ((int32_t)key->body_lean_y * 2) / 127;
}

static void fav_tune_key(
    fav_profile_t profile,
    const face_render_key_t *source,
    face_render_key_t *key)
{
    memcpy(key, source, sizeof(*key));
    switch (profile) {
    case FAV_PROFILE_BEAN_APPEAL_SCOUT:
        if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) == 0U) {
            key->controls.eye_left_open =
                fav_clamp_u8((int32_t)key->controls.eye_left_open + 8);
            key->controls.eye_right_open =
                fav_clamp_u8((int32_t)key->controls.eye_right_open + 8);
        }
        key->controls.mouth_width =
            fav_clamp_u8((int32_t)key->controls.mouth_width + 8);
        break;
    case FAV_PROFILE_BEAN_CLOCKWORK_PUPPET:
        key->controls.look_x =
            fav_clamp_i8(((int32_t)key->controls.look_x * 4) / 5);
        key->controls.look_y =
            fav_clamp_i8(((int32_t)key->controls.look_y * 4) / 5);
        key->controls.mouth_width =
            fav_clamp_u8((int32_t)key->controls.mouth_width - 8);
        break;
    case FAV_PROFILE_BEAN_MANGA_SPARK:
        if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) == 0U) {
            key->controls.eye_left_open =
                fav_clamp_u8((int32_t)key->controls.eye_left_open + 16);
            key->controls.eye_right_open =
                fav_clamp_u8((int32_t)key->controls.eye_right_open + 16);
        }
        key->controls.mouth_width =
            fav_clamp_u8((int32_t)key->controls.mouth_width + 15);
        key->cheek = fav_clamp_u8((int32_t)key->cheek + 18);
        break;
    case FAV_PROFILE_INK_NEWSROOM_EDITOR:
        key->brow_inner =
            fav_clamp_i8(((int32_t)key->brow_inner * 5) / 4);
        key->brow_outer_left =
            fav_clamp_i8(((int32_t)key->brow_outer_left * 5) / 4);
        key->brow_outer_right =
            fav_clamp_i8(((int32_t)key->brow_outer_right * 5) / 4);
        break;
    case FAV_PROFILE_INK_CABARET_MIME:
        key->controls.mouth_width =
            fav_clamp_u8((int32_t)key->controls.mouth_width + 18);
        key->cheek = fav_clamp_u8((int32_t)key->cheek + 26);
        break;
    case FAV_PROFILE_INK_BLUEPRINT_COMPANION:
        key->controls.mouth_press =
            fav_clamp_u8((int32_t)key->controls.mouth_press + 14);
        key->controls.look_x =
            fav_clamp_i8(((int32_t)key->controls.look_x * 6) / 5);
        break;
    case FAV_PROFILE_CAPTAIN_COMMAND_DECK:
        key->head_roll =
            fav_clamp_i8(((int32_t)key->head_roll * 3) / 4);
        key->body_lean_x =
            fav_clamp_i8(((int32_t)key->body_lean_x * 3) / 4);
        break;
    case FAV_PROFILE_CAPTAIN_NEBULA_DOME:
        key->controls.look_x =
            fav_clamp_i8(((int32_t)key->controls.look_x * 6) / 5);
        key->controls.look_y =
            fav_clamp_i8(((int32_t)key->controls.look_y * 6) / 5);
        break;
    case FAV_PROFILE_CAPTAIN_SOLAR_ROGUE:
        key->head_roll =
            fav_clamp_i8(
                (int32_t)key->head_roll +
                ((int32_t)key->affect_valence * 5) / 127);
        key->controls.mouth_width =
            fav_clamp_u8((int32_t)key->controls.mouth_width + 10);
        break;
    default:
        break;
    }
}

static void fav_background_dots(
    uint16_t *pixels, uint16_t color, int32_t spacing, int32_t phase)
{
    const uint16_t background = pixels[0];
    for (int32_t y = 8 + phase; y < FAV_FRAME_HEIGHT - 6; y += spacing) {
        for (int32_t x = 8 + ((y / spacing) & 1) * (spacing / 2);
             x < FAV_FRAME_WIDTH - 6; x += spacing) {
            const size_t index =
                (size_t)y * FAV_FRAME_WIDTH + (size_t)x;
            if (pixels[index] == background) {
                fav_rect(pixels, x, y, x + 2, y + 2, color);
            }
        }
    }
}

static void fav_overlay_bean_scout(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_line(pixels, 42 + ox, 25 + oy, 118 + ox, 25 + oy, 5, style->ink);
    fav_line(pixels, 44 + ox, 25 + oy, 116 + ox, 25 + oy, 2, style->primary);
    fav_circle(pixels, 80 + ox, 25 + oy, 6, style->ink);
    fav_circle(pixels, 80 + ox, 25 + oy, 3, accent);

    fav_circle(pixels, 23 + ox, 58 + oy, 7, style->ink);
    fav_circle(pixels, 23 + ox, 58 + oy, 4, style->primary);
    fav_circle(pixels, 137 + ox, 58 + oy, 7, style->ink);
    fav_circle(pixels, 137 + ox, 58 + oy, 4, style->primary);
    fav_rect(pixels, 17 + ox, 56 + oy, 25 + ox, 61 + oy, style->primary);
    fav_rect(pixels, 135 + ox, 56 + oy, 143 + ox, 61 + oy, style->primary);

    fav_triangle(
        pixels, 47 + ox, 97 + oy, 75 + ox, 97 + oy,
        69 + ox, 114 + oy, style->primary);
    fav_triangle(
        pixels, 113 + ox, 97 + oy, 85 + ox, 97 + oy,
        91 + ox, 114 + oy, style->primary);
    fav_rect(pixels, 69 + ox, 101 + oy, 91 + ox, 108 + oy, style->ink);
    fav_rect(pixels, 73 + ox, 102 + oy, 87 + ox, 106 + oy, accent);
}

static void fav_overlay_bean_puppet(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_line(pixels, 47 + ox, 23 + oy, 113 + ox, 23 + oy, 5, style->ink);
    fav_line(pixels, 49 + ox, 22 + oy, 61 + ox, 12 + oy, 4, style->primary);
    fav_line(pixels, 61 + ox, 12 + oy, 75 + ox, 23 + oy, 4, style->primary);
    fav_line(pixels, 73 + ox, 23 + oy, 80 + ox, 9 + oy, 4, style->primary);
    fav_line(pixels, 80 + ox, 9 + oy, 88 + ox, 23 + oy, 4, style->primary);
    fav_line(pixels, 86 + ox, 23 + oy, 100 + ox, 12 + oy, 4, style->primary);
    fav_line(pixels, 100 + ox, 12 + oy, 112 + ox, 22 + oy, 4, style->primary);
    fav_circle(pixels, 80 + ox, 11 + oy, 3, accent);

    fav_circle(pixels, 24 + ox, 61 + oy, 8, style->ink);
    fav_circle(pixels, 24 + ox, 61 + oy, 5, style->secondary);
    fav_circle(pixels, 24 + ox, 61 + oy, 2, style->ink);
    fav_circle(pixels, 136 + ox, 61 + oy, 8, style->ink);
    fav_circle(pixels, 136 + ox, 61 + oy, 5, style->secondary);
    fav_circle(pixels, 136 + ox, 61 + oy, 2, style->ink);

    fav_line(pixels, 45 + ox, 76 + oy, 54 + ox, 73 + oy, 2, style->primary);
    fav_line(pixels, 48 + ox, 82 + oy, 56 + ox, 79 + oy, 2, style->primary);
    fav_line(pixels, 115 + ox, 76 + oy, 106 + ox, 73 + oy, 2, style->primary);
    fav_line(pixels, 112 + ox, 82 + oy, 104 + ox, 79 + oy, 2, style->primary);

    fav_line(pixels, 47 + ox, 99 + oy, 68 + ox, 113 + oy, 4, style->ink);
    fav_line(pixels, 113 + ox, 99 + oy, 92 + ox, 113 + oy, 4, style->ink);
    fav_rect(pixels, 68 + ox, 105 + oy, 92 + ox, 112 + oy, style->primary);
    fav_rect(pixels, 75 + ox, 106 + oy, 85 + ox, 110 + oy, accent);
}

static void fav_overlay_bean_manga(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_triangle(
        pixels, 31 + ox, 27 + oy, 58 + ox, 16 + oy,
        53 + ox, 39 + oy, style->ink);
    fav_triangle(
        pixels, 50 + ox, 18 + oy, 77 + ox, 13 + oy,
        69 + ox, 38 + oy, style->ink);
    fav_triangle(
        pixels, 68 + ox, 14 + oy, 98 + ox, 15 + oy,
        86 + ox, 39 + oy, style->ink);
    fav_triangle(
        pixels, 91 + ox, 16 + oy, 126 + ox, 29 + oy,
        105 + ox, 40 + oy, style->ink);
    fav_line(pixels, 46 + ox, 20 + oy, 98 + ox, 17 + oy, 3, style->primary);

    fav_triangle(
        pixels, 28 + ox, 31 + oy, 38 + ox, 34 + oy,
        32 + ox, 77 + oy, style->ink);
    fav_triangle(
        pixels, 132 + ox, 31 + oy, 122 + ox, 34 + oy,
        128 + ox, 77 + oy, style->ink);

    const uint8_t cheek_alpha =
        expression == FACE_EXPRESSION_EMBARRASSED ||
                expression == FACE_EXPRESSION_JOY ||
                expression == FACE_EXPRESSION_EXCITED
            ? 224U
            : 112U;
    for (int32_t lane = 0; lane < 3; ++lane) {
        fav_line(
            pixels, 42 + ox + lane * 5, 78 + oy,
            47 + ox + lane * 5, 75 + oy, 2,
            fav_blend(style->primary, accent, cheek_alpha));
        fav_line(
            pixels, 118 + ox - lane * 5, 78 + oy,
            113 + ox - lane * 5, 75 + oy, 2,
            fav_blend(style->primary, accent, cheek_alpha));
    }

    fav_triangle(
        pixels, 52 + ox, 98 + oy, 76 + ox, 98 + oy,
        68 + ox, 113 + oy, style->primary);
    fav_triangle(
        pixels, 108 + ox, 98 + oy, 84 + ox, 98 + oy,
        92 + ox, 113 + oy, style->secondary);
    fav_rect(pixels, 74 + ox, 102 + oy, 86 + ox, 108 + oy, accent);
}

static void fav_overlay_ink_editor(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);
    fav_background_dots(pixels, style->primary, 14, 0);

    fav_rect(pixels, 34 + ox, 19 + oy, 126 + ox, 25 + oy, style->ink);
    fav_triangle(
        pixels, 45 + ox, 19 + oy, 53 + ox, 7 + oy,
        53 + ox, 19 + oy, style->ink);
    fav_rect(pixels, 52 + ox, 7 + oy, 108 + ox, 20 + oy, style->ink);
    fav_triangle(
        pixels, 108 + ox, 7 + oy, 118 + ox, 20 + oy,
        108 + ox, 20 + oy, style->ink);
    fav_rect(pixels, 55 + ox, 15 + oy, 114 + ox, 19 + oy, style->primary);
    fav_rect(pixels, 75 + ox, 15 + oy, 86 + ox, 19 + oy, accent);

    fav_line(pixels, 45 + ox, 99 + oy, 69 + ox, 117 + oy, 4, style->ink);
    fav_line(pixels, 115 + ox, 99 + oy, 91 + ox, 117 + oy, 4, style->ink);
    fav_triangle(
        pixels, 47 + ox, 101 + oy, 69 + ox, 116 + oy,
        58 + ox, 116 + oy, style->primary);
    fav_triangle(
        pixels, 113 + ox, 101 + oy, 91 + ox, 116 + oy,
        102 + ox, 116 + oy, style->primary);
    fav_rect(pixels, 73 + ox, 104 + oy, 87 + ox, 110 + oy, accent);
}

static void fav_overlay_ink_mime(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_ring(pixels, 76 + ox, 18 + oy, 45, 14, 5, style->ink);
    fav_rect(pixels, 35 + ox, 17 + oy, 116 + ox, 24 + oy, style->primary);
    fav_triangle(
        pixels, 39 + ox, 17 + oy, 82 + ox, 5 + oy,
        117 + ox, 18 + oy, style->primary);
    fav_circle(pixels, 82 + ox, 5 + oy, 3, style->ink);
    fav_rect(pixels, 70 + ox, 17 + oy, 87 + ox, 21 + oy, accent);

    fav_line(pixels, 48 + ox, 69 + oy, 54 + ox, 75 + oy, 2, style->primary);
    fav_line(pixels, 112 + ox, 69 + oy, 106 + ox, 75 + oy, 2, style->primary);
    fav_circle(pixels, 118 + ox, 82 + oy, 2, style->ink);

    fav_triangle(
        pixels, 48 + ox, 104 + oy, 77 + ox, 110 + oy,
        54 + ox, 117 + oy, style->primary);
    fav_triangle(
        pixels, 112 + ox, 104 + oy, 83 + ox, 110 + oy,
        106 + ox, 117 + oy, style->primary);
    fav_circle(pixels, 80 + ox, 110 + oy, 6, style->ink);
    fav_circle(pixels, 80 + ox, 110 + oy, 3, accent);
}

static void fav_overlay_ink_blueprint(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);
    const uint16_t background = pixels[0];
    const uint16_t grid = fav_blend(background, style->primary, 76U);

    for (int32_t x = 10; x < FAV_FRAME_WIDTH; x += 20) {
        for (int32_t y = 6; y < FAV_FRAME_HEIGHT - 5; ++y) {
            const size_t index =
                (size_t)y * FAV_FRAME_WIDTH + (size_t)x;
            if (pixels[index] == background) {
                fav_pixel_alpha(pixels, x, y, grid, 150U);
            }
        }
    }
    for (int32_t y = 10; y < FAV_FRAME_HEIGHT; y += 20) {
        for (int32_t x = 6; x < FAV_FRAME_WIDTH - 5; ++x) {
            const size_t index =
                (size_t)y * FAV_FRAME_WIDTH + (size_t)x;
            if (pixels[index] == background) {
                fav_pixel_alpha(pixels, x, y, grid, 150U);
            }
        }
    }

    fav_rect(pixels, 14 + ox, 45 + oy, 27 + ox, 69 + oy, style->ink);
    fav_rect(pixels, 17 + ox, 48 + oy, 27 + ox, 66 + oy, style->primary);
    fav_rect(pixels, 21 + ox, 53 + oy, 28 + ox, 61 + oy, accent);
    fav_line(pixels, 27 + ox, 48 + oy, 34 + ox, 43 + oy, 3, style->primary);
    fav_line(pixels, 27 + ox, 66 + oy, 34 + ox, 72 + oy, 3, style->primary);

    fav_rect(pixels, 133 + ox, 45 + oy, 146 + ox, 69 + oy, style->ink);
    fav_rect(pixels, 133 + ox, 48 + oy, 143 + ox, 66 + oy, style->primary);
    fav_rect(pixels, 132 + ox, 53 + oy, 139 + ox, 61 + oy, accent);
    fav_line(pixels, 133 + ox, 48 + oy, 126 + ox, 43 + oy, 3, style->primary);
    fav_line(pixels, 133 + ox, 66 + oy, 126 + ox, 72 + oy, 3, style->primary);

    fav_line(pixels, 57 + ox, 105 + oy, 103 + ox, 105 + oy, 4, style->ink);
    for (int32_t lane = 0; lane < 5; ++lane) {
        const uint16_t lane_color =
            lane == (int32_t)(expression % 5U) ? accent : style->primary;
        fav_rect(
            pixels, 59 + ox + lane * 9, 103 + oy,
            65 + ox + lane * 9, 108 + oy, lane_color);
    }
}

static void fav_overlay_captain_command(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_rect(pixels, 75 + ox, 4 + oy, 86 + ox, 19 + oy, style->ink);
    fav_triangle(
        pixels, 75 + ox, 7 + oy, 80 + ox, 1 + oy,
        86 + ox, 7 + oy, style->secondary);
    fav_rect(pixels, 78 + ox, 7 + oy, 83 + ox, 16 + oy, accent);

    fav_triangle(
        pixels, 18 + ox, 99 + oy, 50 + ox, 91 + oy,
        57 + ox, 113 + oy, style->primary);
    fav_triangle(
        pixels, 142 + ox, 99 + oy, 110 + ox, 91 + oy,
        103 + ox, 113 + oy, style->primary);
    fav_line(pixels, 23 + ox, 99 + oy, 51 + ox, 94 + oy, 4, style->secondary);
    fav_line(pixels, 137 + ox, 99 + oy, 109 + ox, 94 + oy, 4, style->secondary);
    fav_rect(pixels, 105 + ox, 104 + oy, 130 + ox, 116 + oy, style->ink);
    fav_rect(pixels, 108 + ox, 106 + oy, 128 + ox, 109 + oy, style->secondary);
    fav_rect(pixels, 108 + ox, 112 + oy, 122 + ox, 115 + oy, accent);
}

static void fav_overlay_captain_nebula(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);
    fav_background_dots(
        pixels, fav_blend(style->primary, accent, 120U), 17,
        (int32_t)((key->attention / 64U) & 3U));

    fav_ring(pixels, 80 + ox, 53 + oy, 66, 50, 3, style->primary);
    fav_ring(pixels, 80 + ox, 53 + oy, 61, 46, 2, style->secondary);
    fav_line(pixels, 48 + ox, 14 + oy, 68 + ox, 8 + oy, 3, style->light);
    fav_line(pixels, 43 + ox, 20 + oy, 50 + ox, 17 + oy, 2, style->light);

    fav_ring(pixels, 80 + ox, 105 + oy, 45, 12, 4, style->ink);
    fav_line(pixels, 42 + ox, 105 + oy, 67 + ox, 113 + oy, 6, style->primary);
    fav_line(pixels, 118 + ox, 105 + oy, 93 + ox, 113 + oy, 6, style->primary);
    fav_rect(pixels, 70 + ox, 107 + oy, 90 + ox, 115 + oy, accent);
}

static void fav_overlay_captain_rogue(
    uint16_t *pixels, const fav_style_t *style,
    const face_render_key_t *key, uint8_t expression)
{
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const uint16_t accent = fav_emotion_accent(style, expression);

    fav_triangle(
        pixels, 132 + ox, 35 + oy, 148 + ox, 27 + oy,
        145 + ox, 51 + oy, style->ink);
    fav_triangle(
        pixels, 134 + ox, 37 + oy, 145 + ox, 31 + oy,
        142 + ox, 48 + oy, style->secondary);
    fav_rect(pixels, 132 + ox, 47 + oy, 147 + ox, 62 + oy, style->ink);
    fav_rect(pixels, 135 + ox, 49 + oy, 144 + ox, 58 + oy, accent);
    fav_line(pixels, 133 + ox, 58 + oy, 124 + ox, 67 + oy, 3, style->secondary);

    fav_line(pixels, 51 + ox, 41 + oy, 59 + ox, 47 + oy, 2, style->secondary);
    fav_line(pixels, 55 + ox, 40 + oy, 62 + ox, 44 + oy, 2, style->light);

    fav_rect(pixels, 35 + ox, 99 + oy, 125 + ox, 110 + oy, style->ink);
    fav_rect(pixels, 39 + ox, 100 + oy, 121 + ox, 107 + oy, style->primary);
    fav_line(pixels, 43 + ox, 101 + oy, 117 + ox, 107 + oy, 3, style->secondary);
    fav_circle(pixels, 45 + ox, 105 + oy, 7, style->ink);
    fav_circle(pixels, 45 + ox, 105 + oy, 4, accent);
}

static void fav_draw_overlay(
    fav_profile_t profile,
    uint16_t *pixels,
    const fav_style_t *style,
    const face_render_key_t *key)
{
    const uint8_t expression = fav_expression(key);
    switch (style->overlay) {
    case FAV_OVERLAY_BEAN_SCOUT:
        fav_overlay_bean_scout(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_BEAN_PUPPET:
        fav_overlay_bean_puppet(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_BEAN_MANGA:
        fav_overlay_bean_manga(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_INK_EDITOR:
        fav_overlay_ink_editor(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_INK_MIME:
        fav_overlay_ink_mime(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_INK_BLUEPRINT:
        fav_overlay_ink_blueprint(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_CAPTAIN_COMMAND:
        fav_overlay_captain_command(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_CAPTAIN_NEBULA:
        fav_overlay_captain_nebula(pixels, style, key, expression);
        break;
    case FAV_OVERLAY_CAPTAIN_ROGUE:
        fav_overlay_captain_rogue(pixels, style, key, expression);
        break;
    default:
        (void)profile;
        break;
    }
}

/*
 * The 80x60 VGA mouth bank intentionally shares silhouettes where real
 * articulation shares the same jaw pose. At 2x presentation that can make
 * I/DD and O/RR pixel-identical, so retain their linguistically meaningful
 * distinction with tiny, mouth-attached contact marks: DD shows the central
 * alveolar tooth contact and RR shows the curled tongue tip.
 */
static void fav_draw_captain_viseme_detail(
    uint16_t *pixels,
    const fav_style_t *style,
    const face_render_key_t *key)
{
    if (key->viseme_set != FACE_VISEME_SET_OVR15 ||
        key->viseme_weight < 24U || key->viseme >= 15U) {
        return;
    }
    uint8_t viseme = key->viseme;
    if (key->viseme_secondary < 15U && key->viseme_blend >= 128U) {
        viseme = key->viseme_secondary;
    }
    int32_t ox;
    int32_t oy;
    fav_pose_offset(key, &ox, &oy);
    const int32_t center_x = 80 + ox;
    const int32_t lip_y = 82 + oy;
    if (viseme == 8U) { /* DD: tongue against the alveolar ridge */
        fav_rect(
            pixels, center_x - 2, lip_y,
            center_x + 2, lip_y + 2, style->light);
        fav_rect(
            pixels, center_x - 1, lip_y + 2,
            center_x + 1, lip_y + 4, style->secondary);
    } else if (viseme == 12U) { /* RR: visible curled tongue tip */
        fav_circle(pixels, center_x, lip_y + 6, 2, style->secondary);
        fav_pixel(pixels, center_x, lip_y + 4, style->light);
    }
}

size_t fav_profile_count(void)
{
    return FAV_PROFILE_COUNT;
}

const char *fav_profile_slug(fav_profile_t profile)
{
    return (unsigned)profile < FAV_PROFILE_COUNT
        ? FAV_STYLES[profile].slug
        : NULL;
}

const char *fav_profile_name(fav_profile_t profile)
{
    return (unsigned)profile < FAV_PROFILE_COUNT
        ? FAV_STYLES[profile].name
        : NULL;
}

fav_lineage_t fav_profile_lineage(fav_profile_t profile)
{
    return (unsigned)profile < FAV_PROFILE_COUNT
        ? FAV_STYLES[profile].lineage
        : FAV_LINEAGE_TOON_BEAN;
}

bool fav_profile_info(fav_profile_t profile, fav_info_t *info)
{
    if ((unsigned)profile >= FAV_PROFILE_COUNT || info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->width = FAV_FRAME_WIDTH;
    info->height = FAV_FRAME_HEIGHT;
    info->framebuffer_bytes = FAV_FRAME_BYTES;
    info->lineage = (uint8_t)FAV_STYLES[profile].lineage;
    info->flags = 0x07U; /* emotion + viseme + speech-phase */
    info->estimated_ops_per_pixel = 31U;
    return true;
}

bool fav_render_frame(
    fav_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)profile >= FAV_PROFILE_COUNT || key == NULL ||
        rgb565 == NULL || pixel_capacity < (size_t)FAV_PIXEL_COUNT) {
        return false;
    }
    face_render_key_t tuned;
    fav_tune_key(profile, key, &tuned);
    const fav_style_t *style = &FAV_STYLES[profile];
    bool rendered;
    if (style->lineage == FAV_LINEAGE_TOON_BEAN) {
        rendered = fta_render_frame(
            FTA_PROFILE_TOON_BEAN, &tuned, sample_clock,
            rgb565, pixel_capacity);
    } else if (style->lineage == FAV_LINEAGE_TOON_INK) {
        rendered = fta_render_frame(
            FTA_PROFILE_TOON_INK, &tuned, sample_clock,
            rgb565, pixel_capacity);
    } else {
        rendered = fsa_render_frame(
            FSA_PROFILE_VGA_STAR_CAPTAIN, &tuned, sample_clock,
            rgb565, pixel_capacity);
    }
    if (!rendered) {
        return false;
    }
    fav_tone_frame(rgb565, style);
    if (style->lineage == FAV_LINEAGE_VGA_STAR_CAPTAIN) {
        fav_draw_captain_viseme_detail(rgb565, style, &tuned);
    }
    fav_draw_overlay(profile, rgb565, style, &tuned);
    return true;
}
