#include "face_robot_eyes_internal.h"

#include <string.h>

/*
 * Profile tables and frame composition.
 *
 * Style constants follow the observed conventions of the referenced
 * robots (independently re-implemented; no source was copied):
 *  - Anki Vector/Cozmo: blink is squash-and-stretch — the eye widens as
 *    it closes into a line (the engine's blink table widens scaleX while
 *    scaleY collapses); looking up scales eyes up ~1.05-1.1, looking
 *    down ~0.85-0.9; the eye on the gaze side grows slightly; darts
 *    curve because the lagging axis trails the dominant one.
 *  - FluxGarage RoboEyes: rounded-rect eyes, mood expressed with angled
 *    lid cuts, confusion/laughter as decaying flicker shakes.
 *  - m5stack-avatar / Stack-chan: small disc eyes, open-ratio blinks
 *    that collapse to a lid line, whole-face breathing bob.
 *  - EVE: two glowing tilted teardrop eyes on black, expression carried
 *    entirely by shape and tilt.
 */

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF

static const fre_profile_def_t FRE_DEFS[FRE_PROFILE_COUNT] = {
    [FRE_PROFILE_VECTOR_ROUNDED] = {
        .slug = "fre_vector_rounded",
        .name = "Fable Vector Rounded",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 100, .reopen_pct = 100, .wander_pct = 100,
            .micro_pct = 60, .overshoot_pct = 45, .brow_gain_pct = 0,
            .lid_lead_ms = 60, .act_mask = 0xFFFF, .asym_pct = 3,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0x07EC, .eye_edge_color = 0x0568,
            .eye_cy = 56, .eye_gap = 62, .eye_hw = 17, .eye_hh = 21,
            .corner_r = { 7, 7, 7, 7 },
            .travel_x = 13, .travel_y = 9,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 3, .glow_alpha = 7,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 11,
        },
    },
    [FRE_PROFILE_COZMO_CUBIC] = {
        .slug = "fre_cozmo_cubic",
        .name = "Fable Cozmo Cubic",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 110, .reopen_pct = 90, .wander_pct = 110,
            .micro_pct = 40, .overshoot_pct = 55, .brow_gain_pct = 0,
            .lid_lead_ms = 50, .act_mask = 0xFFFF, .asym_pct = 3,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0x07FF, .eye_edge_color = 0x05DA,
            .highlight_color = C_WHITE,
            .eye_cy = 54, .eye_gap = 58, .eye_hw = 15, .eye_hh = 17,
            .corner_r = { 3, 3, 3, 3 },
            .travel_x = 12, .travel_y = 9,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_GLINT,
            .glow_range = 2, .glow_alpha = 5,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 10,
        },
    },
    [FRE_PROFILE_ROBOEYES_ALERT] = {
        .slug = "fre_roboeyes_alert",
        .name = "Fable RoboEyes Alert",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 80, .reopen_pct = 70, .wander_pct = 130,
            .micro_pct = 0, .overshoot_pct = 70, .brow_gain_pct = 0,
            .lid_lead_ms = 40, .act_mask = 0xFFFF, .asym_pct = 2,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = C_WHITE, .eye_edge_color = C_WHITE,
            .eye_cy = 56, .eye_gap = 60, .eye_hw = 14, .eye_hh = 14,
            .corner_r = { 5, 5, 5, 5 },
            .travel_x = 15, .travel_y = 11,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .base_mood = FRE_MOOD_ALERT,
            .mouth_kind = FRE_MOUTH_NONE,
            .ops_estimate = 8,
        },
    },
    [FRE_PROFILE_ROBOEYES_SOFT] = {
        .slug = "fre_roboeyes_soft",
        .name = "Fable RoboEyes Soft",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 150, .reopen_pct = 170, .wander_pct = 60,
            .micro_pct = 0, .overshoot_pct = 20, .brow_gain_pct = 0,
            .lid_lead_ms = 90,
            .act_mask = (1U << FRE_ACT_SLOW_BLINK) |
                        (1U << FRE_ACT_GLANCE_ASIDE) |
                        (1U << FRE_ACT_DRIFT_REFOCUS) |
                        (1U << FRE_ACT_SQUINT),
            .cat_slow_blink = 1, .asym_pct = 6,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0xFFDA, .eye_edge_color = 0xEE97,
            .eye_cy = 58, .eye_gap = 58, .eye_hw = 15, .eye_hh = 12,
            .corner_r = { 6, 6, 6, 6 },
            .travel_x = 11, .travel_y = 7,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .base_mood = FRE_MOOD_TIRED,
            .mouth_kind = FRE_MOUTH_NONE,
            .ops_estimate = 8,
        },
    },
    [FRE_PROFILE_M5_AVATAR_CLASSIC] = {
        .slug = "fre_m5_avatar_classic",
        .name = "Fable M5 Avatar Classic",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 95, .reopen_pct = 100, .wander_pct = 90,
            .micro_pct = 30, .overshoot_pct = 30, .brow_gain_pct = 0,
            .lid_lead_ms = 40, .act_mask = 0xFFFF, .asym_pct = 3,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = C_WHITE, .eye_edge_color = C_WHITE,
            .eye_cy = 50, .eye_gap = 78, .eye_hw = 8, .eye_hh = 8,
            .corner_r = { 8, 8, 8, 8 },
            .travel_x = 8, .travel_y = 6,
            .shape = FRE_SHAPE_STADIUM, .iris_kind = FRE_IRIS_NONE,
            .mouth_kind = FRE_MOUTH_LINE,
            .mouth_cy = 92, .mouth_hw = 18, .mouth_hh = 10,
            .closed_line = 1,
            .ops_estimate = 6,
        },
    },
    [FRE_PROFILE_M5_AVATAR_MANGA] = {
        .slug = "fre_m5_avatar_manga",
        .name = "Fable M5 Avatar Manga",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 90, .reopen_pct = 100, .wander_pct = 90,
            .micro_pct = 40, .overshoot_pct = 40, .brow_gain_pct = 120,
            .lid_lead_ms = 50, .act_mask = 0xFFFF, .asym_pct = 4,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = C_WHITE, .eye_edge_color = 0xC618,
            .highlight_color = C_WHITE,
            .eye_cy = 52, .eye_gap = 72, .eye_hw = 12, .eye_hh = 14,
            .corner_r = { 11, 11, 11, 11 },
            .travel_x = 9, .travel_y = 7,
            .iris_r = 9, .pupil_r = 4, .iris_travel_extra = 2,
            .iris_color = 0x29A9, .pupil_color = 0x1085,
            .shape = FRE_SHAPE_STADIUM, .iris_kind = FRE_IRIS_FULL,
            .has_brow = 1, .brow_color = C_WHITE,
            .brow_dy = 22, .brow_len = 10, .brow_th = 2,
            .mouth_kind = FRE_MOUTH_LINE,
            .mouth_cy = 94, .mouth_hw = 14, .mouth_hh = 8,
            .closed_line = 1,
            .ops_estimate = 9,
        },
    },
    [FRE_PROFILE_EVE_MINIMAL] = {
        .slug = "fre_eve_minimal",
        .name = "Fable EVE Minimal",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 130, .reopen_pct = 120, .wander_pct = 70,
            .micro_pct = 0, .overshoot_pct = 25, .brow_gain_pct = 0,
            .lid_lead_ms = 70,
            .act_mask = (1U << FRE_ACT_GLANCE_ASIDE) |
                        (1U << FRE_ACT_SLOW_BLINK) |
                        (1U << FRE_ACT_TILT) |
                        (1U << FRE_ACT_BROW_FLASH),
            .tilt_acts = 1, .asym_pct = 2,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = 0x0841,
            .eye_color = 0x051F, .eye_edge_color = 0x033B,
            .eye_cy = 58, .eye_gap = 64, .eye_hw = 16, .eye_hh = 11,
            .corner_r = { 10, 10, 6, 6 },
            .travel_x = 10, .travel_y = 7,
            .shape = FRE_SHAPE_WEDGE, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 6, .glow_alpha = 14,
            .mouth_kind = FRE_MOUTH_NONE,
            .ops_estimate = 13,
        },
    },
    [FRE_PROFILE_JIBO_ORB] = {
        .slug = "fre_jibo_orb",
        .name = "Fable Jibo Orb",
        .family = FRE_FAMILY_ROBOT,
        .tuning = {
            .blink_cycle_pct = 120, .reopen_pct = 110, .wander_pct = 80,
            .micro_pct = 25, .overshoot_pct = 30, .brow_gain_pct = 0,
            .lid_lead_ms = 60,
            .act_mask = (1U << FRE_ACT_GLANCE_ASIDE) |
                        (1U << FRE_ACT_DRIFT_REFOCUS) |
                        (1U << FRE_ACT_SQUINT) |
                        (1U << FRE_ACT_LOOK_UP_THINK) |
                        (1U << FRE_ACT_SLOW_BLINK),
            .asym_pct = 0,
        },
        .style = {
            .bg_top = 0x10A2, .bg_bottom = 0x2945,
            .eye_color = 0xDFFF, .eye_edge_color = 0x9E7F,
            .eye_cy = 58, .eye_gap = 0, .eye_hw = 16, .eye_hh = 16,
            .corner_r = { 16, 16, 16, 16 },
            .travel_x = 22, .travel_y = 13,
            .shape = FRE_SHAPE_DISC, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 6, .glow_alpha = 10,
            .mouth_kind = FRE_MOUTH_NONE,
            .single_eye = 1,
            .ops_estimate = 9,
        },
    },
    [FRE_PROFILE_SACCADE_LAB] = {
        .slug = "fre_saccade_lab",
        .name = "Fable Saccade Lab",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 100, .reopen_pct = 100, .wander_pct = 100,
            .micro_pct = 100, .overshoot_pct = -40, .brow_gain_pct = 0,
            .lid_lead_ms = 60, .act_mask = 0xFFFF, .asym_pct = 3,
        },
        .style = {
            .bg_top = 0x18E3, .bg_bottom = 0x10A2,
            .eye_color = 0xF79E, .eye_edge_color = 0xAD55,
            .iris_color = 0xCC65, .pupil_color = 0x18A2,
            .highlight_color = C_WHITE,
            .eye_cy = 56, .eye_gap = 62, .eye_hw = 17, .eye_hh = 13,
            .corner_r = { 12, 12, 12, 12 },
            .travel_x = 9, .travel_y = 6,
            .iris_r = 8, .pupil_r = 3, .iris_travel_extra = 3,
            .shape = FRE_SHAPE_STADIUM, .iris_kind = FRE_IRIS_FULL,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 16,
        },
    },
    [FRE_PROFILE_BROW_DIALOGUE] = {
        .slug = "fre_brow_dialogue",
        .name = "Fable Brow Dialogue",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 100, .reopen_pct = 100, .wander_pct = 80,
            .micro_pct = 30, .overshoot_pct = 40, .brow_gain_pct = 170,
            .lid_lead_ms = 50, .act_mask = 0xFFFF, .asym_pct = 4,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0xAEDF, .eye_edge_color = 0x74BB,
            .eye_cy = 60, .eye_gap = 60, .eye_hw = 13, .eye_hh = 13,
            .corner_r = { 6, 6, 6, 6 },
            .travel_x = 10, .travel_y = 7,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .has_brow = 1, .brow_color = 0xAEDF,
            .brow_dy = 21, .brow_len = 12, .brow_th = 3,
            .glow_range = 2, .glow_alpha = 5,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 10,
        },
    },
    [FRE_PROFILE_LID_ANTICIPATION] = {
        .slug = "fre_lid_anticipation",
        .name = "Fable Lid Anticipation",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 110, .reopen_pct = 130, .wander_pct = 110,
            .micro_pct = 40, .overshoot_pct = 30, .brow_gain_pct = 0,
            .lid_lead_ms = 110, .act_mask = 0xFFFF, .asym_pct = 4,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0xFD44, .eye_edge_color = 0xB3C2,
            .eye_cy = 56, .eye_gap = 60, .eye_hw = 15, .eye_hh = 17,
            .corner_r = { 8, 8, 8, 8 },
            .travel_x = 11, .travel_y = 9,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 3, .glow_alpha = 6,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 10,
        },
    },
    [FRE_PROFILE_IRIS_PARALLAX] = {
        .slug = "fre_iris_parallax",
        .name = "Fable Iris Parallax",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 105, .reopen_pct = 100, .wander_pct = 95,
            .micro_pct = 70, .overshoot_pct = 25, .brow_gain_pct = 0,
            .lid_lead_ms = 60, .act_mask = 0xFFFF, .asym_pct = 3,
        },
        .style = {
            .bg_top = 0x0861, .bg_bottom = 0x0020,
            .eye_color = 0xE73C, .eye_edge_color = 0x8C51,
            .iris_color = 0x33BE, .pupil_color = 0x1064,
            .highlight_color = C_WHITE,
            .eye_cy = 56, .eye_gap = 66, .eye_hw = 15, .eye_hh = 15,
            .corner_r = { 15, 15, 15, 15 },
            .travel_x = 7, .travel_y = 5,
            .iris_r = 9, .pupil_r = 4, .iris_travel_extra = 5,
            .shape = FRE_SHAPE_DISC, .iris_kind = FRE_IRIS_FULL,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 17,
        },
    },
    [FRE_PROFILE_SLEEP_WAKE] = {
        .slug = "fre_sleep_wake",
        .name = "Fable Sleep Wake",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 120, .reopen_pct = 150, .wander_pct = 70,
            .micro_pct = 30, .overshoot_pct = 20, .brow_gain_pct = 60,
            .lid_lead_ms = 80,
            .act_mask = (1U << FRE_ACT_SLOW_BLINK) |
                        (1U << FRE_ACT_DRIFT_REFOCUS) |
                        (1U << FRE_ACT_SQUINT) |
                        (1U << FRE_ACT_GLANCE_ASIDE),
            .drowsy = 1, .asym_pct = 8,
        },
        .style = {
            .bg_top = 0x0841, .bg_bottom = 0x0000,
            .eye_color = 0x9CFF, .eye_edge_color = 0x6B7D,
            .eye_cy = 58, .eye_gap = 60, .eye_hw = 15, .eye_hh = 15,
            .corner_r = { 9, 9, 9, 9 },
            .travel_x = 10, .travel_y = 7,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 4, .glow_alpha = 7,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 10,
        },
    },
    [FRE_PROFILE_CURIOUS_TILT] = {
        .slug = "fre_curious_tilt",
        .name = "Fable Curious Tilt",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 95, .reopen_pct = 90, .wander_pct = 110,
            .micro_pct = 50, .overshoot_pct = 50, .brow_gain_pct = 80,
            .lid_lead_ms = 50, .act_mask = 0xFFFF,
            .tilt_acts = 1, .asym_pct = 4,
        },
        .style = {
            .bg_top = C_BLACK, .bg_bottom = C_BLACK,
            .eye_color = 0x07F2, .eye_edge_color = 0x05CD,
            .eye_cy = 56, .eye_gap = 60, .eye_hw = 15, .eye_hh = 18,
            .corner_r = { 8, 8, 8, 8 },
            .travel_x = 12, .travel_y = 9,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .glow_range = 3, .glow_alpha = 6,
            .mouth_kind = FRE_MOUTH_NONE,
            .ops_estimate = 11,
        },
    },
    [FRE_PROFILE_DOT_MATRIX_EYES] = {
        .slug = "fre_dot_matrix_eyes",
        .name = "Fable Dot Matrix Eyes",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 90, .reopen_pct = 80, .wander_pct = 120,
            .micro_pct = 0, .overshoot_pct = 60, .brow_gain_pct = 0,
            .lid_lead_ms = 40, .act_mask = 0xFFFF, .asym_pct = 0,
        },
        .style = {
            .bg_top = 0x0841, .bg_bottom = 0x0841,
            .eye_color = 0xFB43, .eye_edge_color = 0xFB43,
            .eye_cy = 56, .eye_gap = 62, .eye_hw = 16, .eye_hh = 18,
            .corner_r = { 6, 6, 6, 6 },
            .travel_x = 12, .travel_y = 9,
            .shape = FRE_SHAPE_ROUNDRECT, .iris_kind = FRE_IRIS_NONE,
            .mouth_kind = FRE_MOUTH_NONE,
            .pixel_grid = 4,
            .ops_estimate = 7,
        },
    },
    [FRE_PROFILE_CAT_OPTICS] = {
        .slug = "fre_cat_optics",
        .name = "Fable Cat Optics",
        .family = FRE_FAMILY_EYES,
        .tuning = {
            .blink_cycle_pct = 170, .reopen_pct = 140, .wander_pct = 120,
            .micro_pct = 80, .overshoot_pct = -30, .brow_gain_pct = 0,
            .lid_lead_ms = 70, .act_mask = 0xFFFF,
            .cat_slow_blink = 1, .asym_pct = 6,
        },
        .style = {
            .bg_top = 0x10E3, .bg_bottom = 0x0861,
            .eye_color = 0xAE68, .eye_edge_color = 0x63E4,
            .iris_color = 0xAE68, .pupil_color = 0x0841,
            .highlight_color = 0xEFFB,
            .eye_cy = 56, .eye_gap = 62, .eye_hw = 15, .eye_hh = 13,
            .corner_r = { 12, 12, 8, 8 },
            .travel_x = 10, .travel_y = 7,
            .iris_r = 12, .pupil_r = 5, .iris_travel_extra = 2,
            .shape = FRE_SHAPE_STADIUM, .iris_kind = FRE_IRIS_SLIT,
            .mouth_kind = FRE_MOUTH_NONE,
            .closed_line = 1,
            .ops_estimate = 15,
        },
    },
};

const fre_profile_def_t *fre_profile_def(fre_profile_t profile)
{
    if ((int)profile < 0 || profile >= FRE_PROFILE_COUNT) {
        return NULL;
    }
    return &FRE_DEFS[profile];
}

size_t fre_profile_count(void)
{
    return FRE_PROFILE_COUNT;
}

const char *fre_profile_slug(fre_profile_t profile)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    return def != NULL ? def->slug : NULL;
}

const char *fre_profile_name(fre_profile_t profile)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    return def != NULL ? def->name : NULL;
}

const char *fre_profile_family_name(fre_profile_t profile)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    if (def == NULL) {
        return NULL;
    }
    return def->family == FRE_FAMILY_ROBOT ? "robot" : "eyes";
}

bool fre_profile_info(fre_profile_t profile, fre_profile_info_t *info)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    if (def == NULL || info == NULL) {
        return false;
    }
    info->width = FRE_FRAME_WIDTH;
    info->height = FRE_FRAME_HEIGHT;
    info->work_width = FRE_FRAME_WIDTH;
    info->work_height = FRE_FRAME_HEIGHT;
    info->framebuffer_bytes = FRE_FRAME_BYTES;
    info->family = def->family;
    info->mouth_kind = def->style.mouth_kind;
    info->flags = FRE_FLAG_IDLE_MOTION;
    if (def->style.mouth_kind == FRE_MOUTH_NONE) {
        info->flags |= FRE_FLAG_NO_MOUTH;
    }
    if (def->style.iris_kind != FRE_IRIS_NONE) {
        info->flags |= FRE_FLAG_EYE_FOCUS;
    }
    if (def->style.pixel_grid != 0) {
        info->flags |= FRE_FLAG_PIXELATED;
    }
    info->reserved = 0;
    info->estimated_ops_per_pixel = def->style.ops_estimate;
    return true;
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t cx_q4;
    int32_t cy_q4;
    int32_t sin_q14; /* face tilt */
    int32_t cos_q14;
} fre_face_frame_t;

/* Rotate a face-relative point by the face tilt. */
static void fre_face_point(
    const fre_face_frame_t *f, int32_t dx_q4, int32_t dy_q4,
    int32_t *x_q4, int32_t *y_q4)
{
    *x_q4 = f->cx_q4 + (int32_t)fre_sar64(
        (int64_t)f->cos_q14 * dx_q4 - (int64_t)f->sin_q14 * dy_q4, 14);
    *y_q4 = f->cy_q4 + (int32_t)fre_sar64(
        (int64_t)f->sin_q14 * dx_q4 + (int64_t)f->cos_q14 * dy_q4, 14);
}

/* Build the draw request for one eye. `which` is 0 viewer-left. */
static void fre_build_eye(
    const fre_profile_def_t *def,
    const fre_rig_t *rig,
    const fre_face_frame_t *face,
    int which,
    fre_eye_draw_t *e)
{
    const fre_style_t *st = &def->style;
    int32_t mirror = (which == 0) ? -1 : 1;
    int32_t open_q8 = fre_min(rig->openness_q8[which], 280);

    /* Gaze offset in pixels. */
    int32_t gx_q4 = (rig->gaze_x_q8 * st->travel_x * FRE_Q4) / 256;
    int32_t gy_q4 = (rig->gaze_y_q8 * st->travel_y * FRE_Q4) / 256;

    /* Eye-pair layout, then whole-face tilt. */
    int32_t base_dx = st->single_eye
        ? 0 : mirror * (st->eye_gap * FRE_Q4) / 2;
    fre_face_point(face, base_dx + gx_q4, gy_q4, &e->cx_q4, &e->cy_q4);

    /* Anki-style vertical gaze scaling: eyes grow looking up, shrink
     * looking down; the eye on the gaze side grows a few percent. */
    int32_t vscale = 256;
    if (rig->gaze_y_q8 < 0) {
        vscale += (-rig->gaze_y_q8 * 20) / 256;
    } else {
        vscale -= (rig->gaze_y_q8 * 30) / 256;
    }
    int32_t side_boost = 0;
    if (!st->single_eye) {
        if ((rig->gaze_x_q8 > 40 && which == 1) ||
            (rig->gaze_x_q8 < -40 && which == 0)) {
            side_boost = (fre_abs(rig->gaze_x_q8) - 40) * 12 / 216;
        }
    }

    int32_t sx = (rig->scale_x_q8 * (256 + side_boost / 2)) >> 8;
    int32_t sy = (((rig->scale_y_q8 * vscale) >> 8) *
        (256 + side_boost)) >> 8;

    int32_t hw = (st->eye_hw * FRE_Q4 * sx) >> 8;
    int32_t hh = (st->eye_hh * FRE_Q4 * sy) >> 8;

    /* Blink as squash-and-stretch (Vector's blink table widens the eye
     * while it collapses) unless the style keeps rigid lids. */
    bool lid_cut_style = st->closed_line != 0;
    int32_t open_c = fre_min(open_q8, 256);
    if (!lid_cut_style) {
        hh = (hh * fre_max(open_q8, 12)) >> 8;
        hw = (hw * (256 + ((256 - open_c) * 70) / 256)) >> 8;
    } else if (open_q8 > 256) {
        /* Reopen overshoot still stretches rigid-lid eyes a touch. */
        hh = (hh * open_q8) >> 8;
    }

    e->hw_q4 = fre_max(hw, 8);
    e->hh_q4 = fre_max(hh, 6);

    /* Corner radii track the eye size so squash keeps the silhouette. */
    int32_t rmax = fre_min(e->hw_q4, e->hh_q4);
    for (int i = 0; i < 4; ++i) {
        int idx = i;
        if (which == 1) {
            idx = (i == 0) ? 1 : (i == 1) ? 0 : (i == 2) ? 3 : 2;
        }
        int32_t r = st->corner_r[idx] * FRE_Q4;
        if (st->shape == FRE_SHAPE_STADIUM || st->shape == FRE_SHAPE_DISC) {
            r = rmax;
        }
        e->r_q4[i] = fre_clamp(r, 0, rmax);
    }

    /* Face tilt plus the EVE wedge's own outward cant. */
    int32_t rot_sin = face->sin_q14;
    int32_t rot_cos = face->cos_q14;
    if (st->shape == FRE_SHAPE_WEDGE) {
        /* ~9 degrees outward: sin 0.156, cos 0.988. */
        int32_t ws = mirror * 2556;
        int32_t wc = 16184;
        int32_t ns = (int32_t)fre_sar64(
            (int64_t)rot_sin * wc + (int64_t)rot_cos * ws, 14);
        int32_t nc = (int32_t)fre_sar64(
            (int64_t)rot_cos * wc - (int64_t)rot_sin * ws, 14);
        rot_sin = ns;
        rot_cos = nc;
    }
    e->rot_sin_q14 = rot_sin;
    e->rot_cos_q14 = rot_cos;

    /* Lids: rigid-lid styles cover the aperture from above (72%) and
     * below (28%), with mood tilt on the upper lid. Squash styles keep
     * lids parked outside the shape. */
    if (lid_cut_style) {
        int32_t closed = 256 - open_c;
        int32_t upper_drop = (2 * e->hh_q4 * closed * 72 / 100) >> 8;
        int32_t lower_rise = (2 * e->hh_q4 * closed * 28 / 100) >> 8;
        e->ulid_base_q4 = -e->hh_q4 + upper_drop;
        e->llid_base_q4 = e->hh_q4 - lower_rise;
    } else {
        e->ulid_base_q4 = -e->hh_q4 * 2;
        e->llid_base_q4 = e->hh_q4 * 2;
    }
    int32_t ulid_slope = 0;
    int32_t llid_bend = 0;
    int32_t mood = st->base_mood;
    if (mood == FRE_MOOD_TIRED) {
        /* Angled flat upper lid, RoboEyes-style, on any eye shape. */
        ulid_slope = mirror * 900;
        if (lid_cut_style) {
            e->ulid_base_q4 += (e->hh_q4 * 2) / 5;
        } else {
            e->ulid_base_q4 = -e->hh_q4 + (e->hh_q4 * 2 * 35) / 100;
        }
    } else if (mood == FRE_MOOD_ALERT) {
        ulid_slope = -mirror * 250;
        if (!lid_cut_style) {
            e->ulid_base_q4 = -e->hh_q4 + 8;
        }
    } else if (mood == FRE_MOOD_HAPPY) {
        llid_bend = -700;
        e->llid_base_q4 = e->hh_q4 - (e->hh_q4 * 2) / 5;
    }
    /*
     * The shared expression adapter uses this channel even on the
     * brow-less Cozmo/Vector profiles: positive lifts the inner lid
     * (worry/appeal), negative presses it down (focus/anger).
     */
    if (lid_cut_style) {
        ulid_slope +=
            mirror * (rig->brow_tilt_q8[which] * 700) / 256;
    }
    e->ulid_slope_q12 =
        ulid_slope + rig->upper_lid_slope_q12[which];
    e->ulid_bend_q12 = rig->upper_lid_bend_q12[which];
    e->llid_slope_q12 = rig->lower_lid_slope_q12[which];
    e->llid_bend_q12 =
        llid_bend + rig->lower_lid_bend_q12[which];

    /* Iris block with parallax: the iris travels farther than the eye
     * body, which reads as depth behind a cornea. */
    int32_t it = st->travel_x + st->iris_travel_extra;
    e->iris_cx_q4 = (rig->gaze_x_q8 * it * FRE_Q4) / 256 - gx_q4;
    e->iris_cy_q4 = (rig->gaze_y_q8 *
        (st->travel_y + st->iris_travel_extra) * FRE_Q4) / 256 - gy_q4;
    e->iris_r_q4 = st->iris_r * FRE_Q4;
    int32_t dil = 192 + (rig->pupil_q8 / 2);
    if (st->iris_kind == FRE_IRIS_SLIT) {
        /* Cat slit: arousal opens the slit toward an almond. */
        dil = 64 + rig->pupil_q8;
    }
    e->pupil_r_q4 = (st->pupil_r * FRE_Q4 * dil) >> 8;
    /* Specular highlight: mostly pinned to the light, not the gaze. */
    e->high_cx_q4 = -(st->eye_hw * FRE_Q4) / 4 + e->iris_cx_q4 / 4;
    e->high_cy_q4 = -(st->eye_hh * FRE_Q4) / 4 + e->iris_cy_q4 / 4;
    e->high_r_q4 = 0;
    if (st->iris_kind == FRE_IRIS_FULL) {
        e->high_r_q4 = fre_max((st->iris_r * FRE_Q4) / 4, 20);
    } else if (st->iris_kind == FRE_IRIS_GLINT) {
        e->high_r_q4 = (st->eye_hw * FRE_Q4) / 4;
    } else if (st->iris_kind == FRE_IRIS_SLIT) {
        e->high_r_q4 = 28;
    }

    e->color = st->eye_color;
    e->edge_color = st->eye_edge_color;
    e->iris_color = st->iris_color;
    e->pupil_color = st->pupil_color;
    e->highlight_color = st->highlight_color;
    e->iris_kind = st->iris_kind;
    e->glow_alpha = st->glow_alpha;
    e->glow_range_q4 = st->glow_range * FRE_Q4;
}

static void fre_draw_closed_line(
    fre_canvas_t *canvas,
    const fre_profile_def_t *def,
    const fre_eye_draw_t *e,
    int which)
{
    int32_t mirror = (which == 0) ? -1 : 1;
    /* Tired-looking shallow arc where the lids meet. */
    fre_draw_capsule(canvas, e->cx_q4, e->cy_q4,
        (e->hw_q4 * 9) / 10, mirror * (e->ulid_slope_q12 / 3), 300,
        22, def->style.eye_color);
}

static void fre_draw_brow(
    fre_canvas_t *canvas,
    const fre_profile_def_t *def,
    const fre_rig_t *rig,
    const fre_face_frame_t *face,
    int which)
{
    const fre_style_t *st = &def->style;
    int32_t mirror = (which == 0) ? -1 : 1;
    int32_t lift_q4 = (rig->brow_raise_q8[which] * 6 * FRE_Q4) / 256;
    int32_t gx_q4 = (rig->gaze_x_q8 * st->travel_x * FRE_Q4) / 512;
    int32_t gy_q4 = (rig->gaze_y_q8 * st->travel_y * FRE_Q4) / 512;
    int32_t dx = mirror * (st->eye_gap * FRE_Q4) / 2 + gx_q4;
    int32_t dy = -(st->brow_dy * FRE_Q4) - lift_q4 + gy_q4;
    int32_t cx, cy;
    fre_face_point(face, dx, dy, &cx, &cy);
    /* brow_tilt > 0 lifts the inner end (worry); < 0 knits (anger). */
    int32_t slope = mirror * (rig->brow_tilt_q8[which] * 1400) / 256;
    /* Raised brows arch more; lowered brows flatten. */
    int32_t arch = 500 + (rig->brow_raise_q8[which] * 500) / 256;
    fre_draw_capsule(canvas, cx, cy, st->brow_len * FRE_Q4, slope,
        arch, st->brow_th * FRE_Q4, st->brow_color);
}

static void fre_draw_mouth(
    fre_canvas_t *canvas,
    const fre_profile_def_t *def,
    const fre_keyframe_t *kf,
    const fre_face_frame_t *face)
{
    const fre_style_t *st = &def->style;
    if (st->mouth_kind == FRE_MOUTH_NONE) {
        return;
    }
    int32_t cx, cy;
    fre_face_point(face, 0,
        (st->mouth_cy - 60) * FRE_Q4, &cx, &cy);
    int32_t width_q8 = 192 + ((int32_t)kf->mouth_width) / 2;
    int32_t half_len = (st->mouth_hw * FRE_Q4 * width_q8) / 320;
    int32_t open_px_q4 = ((int32_t)kf->mouth_open * st->mouth_hh *
        FRE_Q4) / 255;
    int32_t press = (int32_t)kf->mouth_press;
    int32_t th = 20 + open_px_q4 / 2 - (press * 10) / 255;
    /* Round mouths pull the corners in. */
    int32_t round_pull = ((int32_t)kf->mouth_round - 128) / 4;
    if (round_pull > 0) {
        half_len -= (half_len * round_pull) / 256;
        th += (th * round_pull) / 384;
    }
    /*
     * Resting mouths keep a faint smile and speech flattens it. Pressed
     * lips progressively reverse the curve, so concern, skepticism, and
     * determination do not inherit the same cheerful mouth silhouette.
     */
    int32_t arch = -420 +
        ((int32_t)kf->mouth_open * 320) / 255 +
        ((int32_t)kf->mouth_press * 3);
    fre_draw_capsule(canvas, cx, cy, fre_max(half_len, 30), 0, arch,
        fre_max(th, 14), st->mouth_color != 0
            ? st->mouth_color : st->eye_color);
}

static void fre_draw_robot_display(
    fre_canvas_t *canvas, const fre_profile_def_t *def)
{
    fre_eye_draw_t panel;
    memset(&panel, 0, sizeof(panel));
    panel.cx_q4 = 80 * FRE_Q4;
    panel.cy_q4 = 59 * FRE_Q4;
    panel.hw_q4 = 73 * FRE_Q4;
    panel.hh_q4 = 47 * FRE_Q4;
    for (int corner = 0; corner < 4; ++corner) {
        panel.r_q4[corner] = 15 * FRE_Q4;
    }
    panel.rot_cos_q14 = FRE_Q14;
    panel.ulid_base_q4 = -panel.hh_q4 * 2;
    panel.llid_base_q4 = panel.hh_q4 * 2;
    panel.color = fre_blend565(
        def->style.bg_top, def->style.eye_edge_color, 9U);
    panel.edge_color = panel.color;
    panel.iris_kind = FRE_IRIS_NONE;
    fre_draw_eye(canvas, &panel);

    panel.hw_q4 = 68 * FRE_Q4;
    panel.hh_q4 = 42 * FRE_Q4;
    for (int corner = 0; corner < 4; ++corner) {
        panel.r_q4[corner] = 12 * FRE_Q4;
    }
    panel.ulid_base_q4 = -panel.hh_q4 * 2;
    panel.llid_base_q4 = panel.hh_q4 * 2;
    panel.color = def->style.bg_top;
    panel.edge_color = panel.color;
    fre_draw_eye(canvas, &panel);
}

/* LED-matrix presentation: sample the eye field at cell centers. */
static void fre_render_dot_matrix(
    fre_canvas_t *canvas,
    const fre_profile_def_t *def,
    const fre_eye_draw_t eyes[2],
    int eye_count)
{
    const int32_t cell = def->style.pixel_grid;
    const uint16_t bg = def->style.bg_top;
    /* Dim resting grid so the panel itself reads as hardware. */
    const uint16_t grid_dim = fre_blend565(bg, def->style.eye_color, 3);
    for (int32_t cy = 0; cy < FRE_FRAME_HEIGHT / cell; ++cy) {
        for (int32_t cx = 0; cx < FRE_FRAME_WIDTH / cell; ++cx) {
            int32_t px_q4 = (cx * cell + cell / 2) * FRE_Q4;
            int32_t py_q4 = (cy * cell + cell / 2) * FRE_Q4;
            int32_t a = 0;
            for (int i = 0; i < eye_count; ++i) {
                a = fre_max(a, fre_eye_alpha_at(&eyes[i], px_q4, py_q4));
            }
            uint16_t color;
            if (a <= 2) {
                if (((cx + cy) & 1) != 0) {
                    continue;
                }
                color = grid_dim;
            } else {
                color = fre_blend565(bg, def->style.eye_color,
                    (uint32_t)fre_clamp(a, 6, 32));
            }
            fre_draw_disc(canvas,
                px_q4, py_q4, (cell * FRE_Q4) / 2 - 12, color, 32);
        }
    }
}

bool fre_render_frame(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    if (def == NULL || keyframe == NULL || rgb565 == NULL) {
        return false;
    }
    if (pixel_capacity < (size_t)FRE_FRAME_PIXEL_COUNT) {
        return false;
    }
    fre_rig_t rig;
    if (!fre_behavior_solve(profile, keyframe, sample_clock, &rig)) {
        return false;
    }
    return fre_render_resolved_frame(
        profile, keyframe, &rig, rgb565, pixel_capacity);
}

bool fre_render_resolved_frame(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    const fre_rig_t *rig,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    const fre_profile_def_t *def = fre_profile_def(profile);
    if (def == NULL || keyframe == NULL || rig == NULL || rgb565 == NULL) {
        return false;
    }
    if (pixel_capacity < (size_t)FRE_FRAME_PIXEL_COUNT) {
        return false;
    }
    fre_canvas_t canvas = { rgb565 };
    fre_fill_gradient(&canvas, def->style.bg_top, def->style.bg_bottom);
    if (profile == FRE_PROFILE_VECTOR_ROUNDED ||
        profile == FRE_PROFILE_COZMO_CUBIC) {
        fre_draw_robot_display(&canvas, def);
    }

    fre_face_frame_t face;
    face.cx_q4 = 80 * FRE_Q4;
    face.cy_q4 = def->style.eye_cy * FRE_Q4 + rig->breath_y_q8 / 16;
    /* Small-angle tilt: sin ~= angle, cos from one Pythagorean step. */
    int32_t tilt_sin = (rig->tilt_mdeg * 286) / 1000;
    face.sin_q14 = fre_clamp(tilt_sin, -4000, 4000);
    face.cos_q14 = FRE_Q14 - (int32_t)(((int64_t)face.sin_q14 *
        face.sin_q14) >> 15);

    int eye_count = def->style.single_eye ? 1 : 2;
    fre_eye_draw_t eyes[2];
    for (int i = 0; i < eye_count; ++i) {
        int which = def->style.single_eye ? 0 : i;
        fre_build_eye(def, rig, &face, which, &eyes[i]);
    }

    if (def->style.pixel_grid != 0) {
        fre_render_dot_matrix(&canvas, def, eyes, eye_count);
    } else {
        for (int i = 0; i < eye_count; ++i) {
            int which = def->style.single_eye ? 0 : i;
            bool closed = rig->openness_q8[which] < 20 &&
                def->style.closed_line != 0;
            if (closed) {
                fre_draw_closed_line(&canvas, def, &eyes[i], which);
            } else {
                fre_draw_eye(&canvas, &eyes[i]);
            }
        }
    }

    if (def->style.has_brow != 0) {
        for (int i = 0; i < 2; ++i) {
            fre_draw_brow(&canvas, def, rig, &face, i);
        }
    }
    fre_draw_mouth(&canvas, def, keyframe, &face);
    return true;
}
