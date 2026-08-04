/*
 * fable_toon_acting — style registry and public entry points.
 *
 * Three looks share one acting solver, so acting quality is uniform and
 * only proportions, palette, and stroke treatment change:
 *
 *   toon-bean  — warm cream plate, big soft eyes, the flagship face.
 *   toon-ink   — line art on paper with a single accent colour.
 *   toon-ember — emissive amber features on dark glass (night-friendly).
 *
 * Proportions follow the baby-schema findings (large eyes set below the
 * vertical face centre, wide inter-eye gap, small low mouth, tall
 * forehead) rather than any specific character.
 */

#include "fta_internal.h"

#define FTA_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xf8U) << 8) | (((g) & 0xfcU) << 3) | ((b) >> 3)))

static const fta_style_t STYLES[FTA_PROFILE_COUNT] = {
    [FTA_PROFILE_TOON_BEAN] = {
        .slug = "toon-bean",
        .name = "Toon Bean",
        .salt = 0x7ab1faceU,
        .look = FTA_LOOK_FILLED,
        .palette = {
            .background = FTA_RGB565(16U, 42U, 52U),
            .plate = FTA_RGB565(246U, 236U, 214U),
            .plate_outline = FTA_RGB565(190U, 168U, 138U),
            .sclera = FTA_RGB565(253U, 253U, 250U),
            .iris = FTA_RGB565(52U, 138U, 148U),
            .pupil = FTA_RGB565(24U, 28U, 44U),
            .glint = FTA_RGB565(255U, 255U, 255U),
            .lid = FTA_RGB565(246U, 236U, 214U),
            .lash = FTA_RGB565(70U, 58U, 54U),
            .brow = FTA_RGB565(64U, 48U, 40U),
            .mouth_interior = FTA_RGB565(88U, 34U, 40U),
            .lip = FTA_RGB565(64U, 48U, 40U),
            .teeth = FTA_RGB565(250U, 248U, 238U),
            .tongue = FTA_RGB565(224U, 110U, 108U),
            .blush = FTA_RGB565(240U, 130U, 122U),
            .sweat = FTA_RGB565(160U, 210U, 240U),
        },
        .plate_half_w_q4 = 58 * FTA_Q4,
        .plate_half_h_q4 = 46 * FTA_Q4,
        .plate_radius_q4 = 26 * FTA_Q4,
        .eye_offset_x_q4 = 23 * FTA_Q4,
        .eye_offset_y_q4 = 6 * FTA_Q4,
        .eye_half_w_q4 = 15 * FTA_Q4,
        .eye_half_h_q4 = 17 * FTA_Q4,
        .iris_r_q4 = (17 * FTA_Q4) / 2,
        .pupil_r_q4 = 5 * FTA_Q4,
        .brow_gap_q4 = 6 * FTA_Q4,
        .brow_half_w_q4 = 12 * FTA_Q4,
        .brow_thickness_q4 = 36,
        .mouth_offset_y_q4 = 29 * FTA_Q4,
        .mouth_half_w_q4 = 17 * FTA_Q4,
        .mouth_max_open_q4 = 13 * FTA_Q4,
        .lip_thickness_q4 = 29,
        .blush_offset_x_q4 = 32 * FTA_Q4,
        .blush_offset_y_q4 = 17 * FTA_Q4,
        .blush_half_w_q4 = 9 * FTA_Q4,
        .blush_half_h_q4 = 5 * FTA_Q4,
        .gaze_travel_q8 = 216U,
        .motion_gain_q8 = 255U,
        .accent_gain_q8 = 255U,
        .ops_estimate = 11U,
    },
    [FTA_PROFILE_TOON_INK] = {
        .slug = "toon-ink",
        .name = "Toon Ink",
        .salt = 0x1d0cabb1U,
        .look = FTA_LOOK_INK,
        .palette = {
            .background = FTA_RGB565(210U, 214U, 218U),
            .plate = FTA_RGB565(246U, 242U, 232U),
            .plate_outline = FTA_RGB565(34U, 34U, 40U),
            .sclera = FTA_RGB565(255U, 253U, 246U),
            .iris = FTA_RGB565(104U, 128U, 144U),
            .pupil = FTA_RGB565(26U, 26U, 32U),
            .glint = FTA_RGB565(255U, 255U, 255U),
            .lid = FTA_RGB565(246U, 242U, 232U),
            .lash = FTA_RGB565(34U, 34U, 40U),
            .brow = FTA_RGB565(34U, 34U, 40U),
            .mouth_interior = FTA_RGB565(74U, 60U, 68U),
            .lip = FTA_RGB565(34U, 34U, 40U),
            .teeth = FTA_RGB565(255U, 253U, 246U),
            .tongue = FTA_RGB565(190U, 92U, 104U),
            .blush = FTA_RGB565(196U, 72U, 84U),
            .sweat = FTA_RGB565(90U, 128U, 154U),
        },
        .plate_half_w_q4 = 54 * FTA_Q4,
        .plate_half_h_q4 = 48 * FTA_Q4,
        .plate_radius_q4 = 34 * FTA_Q4,
        .eye_offset_x_q4 = 21 * FTA_Q4,
        .eye_offset_y_q4 = 5 * FTA_Q4,
        .eye_half_w_q4 = 13 * FTA_Q4,
        .eye_half_h_q4 = 15 * FTA_Q4,
        .iris_r_q4 = 7 * FTA_Q4,
        .pupil_r_q4 = (9 * FTA_Q4) / 2,
        .brow_gap_q4 = 6 * FTA_Q4,
        .brow_half_w_q4 = 11 * FTA_Q4,
        .brow_thickness_q4 = 42,
        .mouth_offset_y_q4 = 30 * FTA_Q4,
        .mouth_half_w_q4 = 19 * FTA_Q4,
        .mouth_max_open_q4 = 10 * FTA_Q4,
        .lip_thickness_q4 = 26,
        .blush_offset_x_q4 = 28 * FTA_Q4,
        .blush_offset_y_q4 = 13 * FTA_Q4,
        .blush_half_w_q4 = 6 * FTA_Q4,
        .blush_half_h_q4 = 3 * FTA_Q4,
        .gaze_travel_q8 = 200U,
        .motion_gain_q8 = 230U,
        .accent_gain_q8 = 240U,
        .ops_estimate = 9U,
    },
    [FTA_PROFILE_TOON_EMBER] = {
        .slug = "toon-ember",
        .name = "Toon Ember",
        .salt = 0x3a3be201U,
        .look = FTA_LOOK_EMBER,
        .palette = {
            .background = FTA_RGB565(10U, 12U, 20U),
            .plate = FTA_RGB565(28U, 32U, 44U),
            .plate_outline = FTA_RGB565(140U, 92U, 28U),
            /* dark socket; the bright iris is the light emitter, so the
             * eye mass stays visible in every expression */
            .sclera = FTA_RGB565(16U, 18U, 26U),
            .iris = FTA_RGB565(230U, 152U, 40U),
            .pupil = FTA_RGB565(10U, 10U, 10U),
            .glint = FTA_RGB565(255U, 236U, 180U),
            .lid = FTA_RGB565(28U, 32U, 44U),
            .lash = FTA_RGB565(246U, 182U, 72U),
            .brow = FTA_RGB565(246U, 182U, 72U),
            .mouth_interior = FTA_RGB565(16U, 13U, 12U),
            .lip = FTA_RGB565(232U, 152U, 48U),
            .tongue = FTA_RGB565(190U, 96U, 44U),
            .teeth = FTA_RGB565(205U, 165U, 104U),
            .blush = FTA_RGB565(235U, 130U, 70U),
            .sweat = FTA_RGB565(240U, 214U, 150U),
        },
        .plate_half_w_q4 = 60 * FTA_Q4,
        .plate_half_h_q4 = 44 * FTA_Q4,
        .plate_radius_q4 = 20 * FTA_Q4,
        .eye_offset_x_q4 = 24 * FTA_Q4,
        .eye_offset_y_q4 = 4 * FTA_Q4,
        .eye_half_w_q4 = 17 * FTA_Q4,
        .eye_half_h_q4 = 19 * FTA_Q4,
        .iris_r_q4 = (19 * FTA_Q4) / 2,
        .pupil_r_q4 = 6 * FTA_Q4,
        .brow_gap_q4 = 5 * FTA_Q4,
        .brow_half_w_q4 = 13 * FTA_Q4,
        .brow_thickness_q4 = 48,
        .mouth_offset_y_q4 = 28 * FTA_Q4,
        .mouth_half_w_q4 = 16 * FTA_Q4,
        .mouth_max_open_q4 = 12 * FTA_Q4,
        .lip_thickness_q4 = 38,
        .blush_offset_x_q4 = 32 * FTA_Q4,
        .blush_offset_y_q4 = 13 * FTA_Q4,
        .blush_half_w_q4 = 7 * FTA_Q4,
        .blush_half_h_q4 = 4 * FTA_Q4,
        .gaze_travel_q8 = 230U,
        .motion_gain_q8 = 255U,
        .accent_gain_q8 = 255U,
        .ops_estimate = 10U,
    },
};

const fta_style_t *fta_style_for(fta_profile_t profile)
{
    if ((size_t)profile >= FTA_PROFILE_COUNT) {
        return (const fta_style_t *)0;
    }
    return &STYLES[profile];
}

size_t fta_profile_count(void)
{
    return FTA_PROFILE_COUNT;
}

const char *fta_profile_slug(fta_profile_t profile)
{
    const fta_style_t *style = fta_style_for(profile);
    return style ? style->slug : (const char *)0;
}

const char *fta_profile_name(fta_profile_t profile)
{
    const fta_style_t *style = fta_style_for(profile);
    return style ? style->name : (const char *)0;
}

bool fta_profile_info(fta_profile_t profile, fta_info_t *info)
{
    const fta_style_t *style = fta_style_for(profile);
    if (style == (const fta_style_t *)0 || info == (fta_info_t *)0) {
        return false;
    }
    info->width = FTA_FRAME_WIDTH;
    info->height = FTA_FRAME_HEIGHT;
    info->work_width = FTA_FRAME_WIDTH;
    info->work_height = FTA_FRAME_HEIGHT;
    info->framebuffer_bytes = FTA_FRAME_BYTES;
    /* proposed FACE_RENDER_FAMILY_TOON = 5 (see README integration) */
    info->family = 5U;
    /* curve-based mouth: FACE_RENDER_MOUTH_POLYGON */
    info->mouth_kind = 3U;
    /* FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION */
    info->flags = (uint8_t)((1U << 2) | (1U << 5));
    info->reserved = 0U;
    info->estimated_ops_per_pixel = style->ops_estimate;
    return true;
}

bool fta_solve(
    fta_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fta_rig_t *rig)
{
    const fta_style_t *style = fta_style_for(profile);
    if (style == (const fta_style_t *)0 ||
        key == (const face_render_key_t *)0 || rig == (fta_rig_t *)0) {
        return false;
    }
    fta_solve_rig(style, key, sample_clock, rig);
    return true;
}

bool fta_render_frame(
    fta_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    const fta_style_t *style = fta_style_for(profile);
    if (style == (const fta_style_t *)0 ||
        key == (const face_render_key_t *)0 || rgb565 == (uint16_t *)0 ||
        pixel_capacity < (size_t)FTA_PIXEL_COUNT) {
        return false;
    }
    fta_rig_t rig;
    fta_solve_rig(style, key, sample_clock, &rig);
    fta_canvas_t canvas = {rgb565};
    fta_draw_rig(&canvas, style, &rig);
    return true;
}
