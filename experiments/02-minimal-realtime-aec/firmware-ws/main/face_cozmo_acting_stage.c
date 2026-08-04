#include "face_cozmo_acting_internal.h"
#include "face_stage.h"

#include <string.h>

/*
 * The stager: profile definitions, the eleven-emotion acting table, and
 * the fold of the complete 40-byte IR into a screen-space pose.
 *
 * Layer order (later layers ride on earlier ones, never replace them):
 *   1. profile base geometry and palette;
 *   2. activity posture (controls.expression is idle/listening/thinking/
 *      speaking — never an emotion);
 *   3. the director's autonomous performance (blinks, darts, breath, acts);
 *   4. the authored stage emotion (stage_expression x expression_weight);
 *   5. dense facial-action bytes (lids, brows, cheek, corners, affect,
 *      attention, head yaw/pitch/roll, body lean);
 *   6. speech articulation (level, viseme vocabulary, phase);
 *   7. the containment governor, which analytically guarantees that no
 *      lit pixel can reach the frame border.
 */

#define FCA_RGB(r, g, b)                                                  \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) |                          \
                (((uint16_t)(g) & 0xFCU) << 3) |                          \
                (((uint16_t)(b)) >> 3)))

/* ---- profiles --------------------------------------------------------- */

static const fca_profile_def_t FCA_PROFILES[FACE_COZMO_ACTING_PROFILE_COUNT] = {
    [FACE_COZMO_ACTING_STAGE] = {
        .slug = "cozmo-acting-stage",
        .name = "Cozmo Acting · Stage",
        .eye_cy = 57, .eye_gap = 46, .eye_hw = 20, .eye_hh = 26,
        .corner_px = { 9, 9, 8, 8 },
        .travel_x = 9, .travel_y = 7,
        .bloom_px = 5, .hotspot_q8 = 118, .sheen_q8 = 66,
        .has_mouth = 1, .mouth_cy = 100, .mouth_hw = 15,
        .mouth_verve = 110,
        .direction = {
            .blink_period_ms = { 4200, 5200, 3400, 2600 },
            .avert_period_ms = { 3600, 7800, 6200, 4200 },
            .avert_prob_pct = { 62, 40, 86, 68 },
            .wander_q8 = { 176, 92, 198, 136 },
            .breath_period_ms = 4300, .breath_amp_q4 = 20,
            .micro_gain_q8 = 96, .act_every_s = 9,
            .reopen_overshoot_q8 = 26, .blink_asym_ms = 6,
            .saccade_verve_q8 = 120, .drowsy = 0,
        },
        .palette = {
            .body = FCA_RGB(0x18, 0xE2, 0xEC),
            .core = FCA_RGB(0xDC, 0xFF, 0xFF),
            .rim = FCA_RGB(0x0B, 0x7C, 0x86),
            .seam = FCA_RGB(0x06, 0x34, 0x3A),
            .floor_glow = FCA_RGB(0x04, 0x14, 0x18),
            .mouth = FCA_RGB(0x8E, 0xF2, 0xF8),
            .warm_body = FCA_RGB(0x3C, 0xF0, 0xB4),
            .cool_body = FCA_RGB(0x2E, 0x7C, 0xFF),
        },
        .seed = 0xC02A5701U,
    },
    [FACE_COZMO_ACTING_EMBER] = {
        .slug = "cozmo-acting-ember",
        .name = "Cozmo Acting · Ember",
        .eye_cy = 56, .eye_gap = 48, .eye_hw = 19, .eye_hh = 24,
        .corner_px = { 7, 7, 10, 10 },
        .travel_x = 8, .travel_y = 6,
        .bloom_px = 6, .hotspot_q8 = 96, .sheen_q8 = 84,
        .has_mouth = 1, .mouth_cy = 99, .mouth_hw = 14,
        .mouth_verve = 96,
        .direction = {
            .blink_period_ms = { 5200, 6200, 4200, 3400 },
            .avert_period_ms = { 4400, 8600, 6800, 5000 },
            .avert_prob_pct = { 55, 34, 80, 58 },
            .wander_q8 = { 140, 80, 172, 112 },
            .breath_period_ms = 4900, .breath_amp_q4 = 26,
            .micro_gain_q8 = 64, .act_every_s = 12,
            .reopen_overshoot_q8 = 14, .blink_asym_ms = 9,
            .saccade_verve_q8 = 70, .drowsy = 1,
        },
        .palette = {
            .body = FCA_RGB(0xFF, 0xB0, 0x1E),
            .core = FCA_RGB(0xFF, 0xF2, 0xC8),
            .rim = FCA_RGB(0x8A, 0x5A, 0x08),
            .seam = FCA_RGB(0x3A, 0x26, 0x04),
            .floor_glow = FCA_RGB(0x18, 0x0E, 0x02),
            .mouth = FCA_RGB(0xFF, 0xD8, 0x96),
            .warm_body = FCA_RGB(0xFF, 0x7E, 0x3C),
            .cool_body = FCA_RGB(0xD8, 0xB2, 0x68),
        },
        .seed = 0xE3B3E201U,
    },
    [FACE_COZMO_ACTING_CHATTER] = {
        .slug = "cozmo-acting-chatter",
        .name = "Cozmo Acting · Chatter",
        .eye_cy = 47, .eye_gap = 47, .eye_hw = 17, .eye_hh = 20,
        .corner_px = { 8, 8, 8, 8 },
        .travel_x = 8, .travel_y = 5,
        .bloom_px = 4, .hotspot_q8 = 110, .sheen_q8 = 60,
        .has_mouth = 1, .mouth_cy = 92, .mouth_hw = 27,
        .mouth_verve = 235,
        .direction = {
            .blink_period_ms = { 3800, 4600, 3200, 2200 },
            .avert_period_ms = { 3300, 6600, 5600, 3600 },
            .avert_prob_pct = { 64, 44, 84, 72 },
            .wander_q8 = { 168, 96, 182, 148 },
            .breath_period_ms = 3900, .breath_amp_q4 = 16,
            .micro_gain_q8 = 104, .act_every_s = 8,
            .reopen_overshoot_q8 = 30, .blink_asym_ms = 5,
            .saccade_verve_q8 = 140, .drowsy = 0,
        },
        .palette = {
            .body = FCA_RGB(0x46, 0xF0, 0xA0),
            .core = FCA_RGB(0xE6, 0xFF, 0xF2),
            .rim = FCA_RGB(0x1A, 0x8A, 0x52),
            .seam = FCA_RGB(0x06, 0x30, 0x1C),
            .floor_glow = FCA_RGB(0x03, 0x14, 0x0A),
            .mouth = FCA_RGB(0x52, 0xF0, 0xA8),
            .warm_body = FCA_RGB(0xB0, 0xF0, 0x46),
            .cool_body = FCA_RGB(0x46, 0xD0, 0xF0),
        },
        .seed = 0x37A77E03U,
    },
    [FACE_COZMO_ACTING_NOVA] = {
        .slug = "cozmo-acting-nova",
        .name = "Cozmo Acting · Nova",
        .eye_cy = 57, .eye_gap = 45, .eye_hw = 21, .eye_hh = 27,
        .corner_px = { 12, 12, 10, 10 },
        .travel_x = 10, .travel_y = 8,
        .bloom_px = 7, .hotspot_q8 = 132, .sheen_q8 = 52,
        .has_mouth = 1, .mouth_cy = 100, .mouth_hw = 15,
        .mouth_verve = 110,
        .direction = {
            .blink_period_ms = { 3400, 4400, 3000, 2300 },
            .avert_period_ms = { 3000, 6200, 5200, 3600 },
            .avert_prob_pct = { 70, 46, 88, 74 },
            .wander_q8 = { 208, 112, 220, 168 },
            .breath_period_ms = 3600, .breath_amp_q4 = 14,
            .micro_gain_q8 = 128, .act_every_s = 7,
            .reopen_overshoot_q8 = 40, .blink_asym_ms = 4,
            .saccade_verve_q8 = 190, .drowsy = 0,
        },
        .palette = {
            .body = FCA_RGB(0xA4, 0x6C, 0xFF),
            .core = FCA_RGB(0xF0, 0xE6, 0xFF),
            .rim = FCA_RGB(0x5A, 0x2C, 0xB4),
            .seam = FCA_RGB(0x22, 0x10, 0x3F),
            .floor_glow = FCA_RGB(0x0C, 0x05, 0x18),
            .mouth = FCA_RGB(0xDE, 0xC8, 0xFF),
            .warm_body = FCA_RGB(0xFF, 0x6C, 0xD8),
            .cool_body = FCA_RGB(0x6C, 0x8C, 0xFF),
        },
        .seed = 0x9B0BA904U,
    },
};

const fca_profile_def_t *fca_profile(face_cozmo_acting_profile_t profile)
{
    if ((int)profile < 0 || profile >= FACE_COZMO_ACTING_PROFILE_COUNT) {
        return NULL;
    }
    return &FCA_PROFILES[profile];
}

size_t face_cozmo_acting_profile_count(void)
{
    return FACE_COZMO_ACTING_PROFILE_COUNT;
}

const char *face_cozmo_acting_profile_slug(
    face_cozmo_acting_profile_t profile)
{
    const fca_profile_def_t *def = fca_profile(profile);
    return def == NULL ? NULL : def->slug;
}

const char *face_cozmo_acting_profile_name(
    face_cozmo_acting_profile_t profile)
{
    const fca_profile_def_t *def = fca_profile(profile);
    return def == NULL ? NULL : def->name;
}

/* ---- the acting table ------------------------------------------------- */

/*
 * Silhouette-first emotion targets. Local eye x is positive toward the
 * temple (outward); the painter mirrors the right eye. Positive upper
 * slope therefore always means "outer end of the lid falls", which reads
 * as sad/soft, and negative means "inner end falls", which reads as
 * determined/cross. All values are deltas from the neutral pose and are
 * scaled by expression_weight before application.
 */
typedef struct {
    int16_t aperture_q8;      /* multiplier target, 256 == unchanged */
    int16_t upper_drop_q8;    /* extra upper-lid drop, Q8 of full height */
    int16_t upper_slope_q12;
    int16_t upper_bend_q12;
    int16_t lower_raise_q8;
    int16_t lower_slope_q12;
    int16_t lower_bend_q12;   /* positive arcs upward (happy crescent) */
    int16_t scale_x_q8;       /* multiplier target */
    int16_t scale_y_q8;
    int16_t taper_q8;         /* + narrows the top of the eye */
    int16_t round_q8;         /* corner radius multiplier target */
    int16_t sharp_inner_q8;   /* shrinks the inner-top corner radius */
    int16_t gaze_x_q8;
    int16_t gaze_y_q8;
    int16_t tilt_mdeg;
    int16_t pose_dy_q4;       /* whole-face lift(-) / sag(+) */
    int16_t hotspot_size_q8;
    int16_t hotspot_gain_q8;
    int16_t emissive_q8;
    int16_t grade_q8;         /* warm(+) / cool(-) palette grade */
    int16_t bounce_amp_q4;    /* periodic vertical bounce */
    uint16_t bounce_period_ms;
    int16_t shiver_mdeg;      /* tiny fast roll shimmer (excited) */
    int16_t mouth_curve_q8;   /* CHATTER acting */
    int16_t mouth_open_q8;
    int16_t mouth_wide_q8;
} fca_emotion_t;

static const fca_emotion_t FCA_EMOTIONS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        .aperture_q8 = 256, .scale_x_q8 = 256, .scale_y_q8 = 256,
        .round_q8 = 256,
    },
    [FACE_EXPRESSION_WARM] = {
        .aperture_q8 = 244, .upper_drop_q8 = 22,
        .upper_slope_q12 = 60, .upper_bend_q12 = 120,
        .lower_raise_q8 = 74, .lower_bend_q12 = 620,
        .scale_x_q8 = 260, .scale_y_q8 = 250,
        .round_q8 = 288, .tilt_mdeg = 700,
        .hotspot_size_q8 = 20, .hotspot_gain_q8 = 16,
        .emissive_q8 = 14, .grade_q8 = 80,
        .mouth_curve_q8 = 120, .mouth_open_q8 = 22, .mouth_wide_q8 = 40,
    },
    [FACE_EXPRESSION_JOY] = {
        .aperture_q8 = 236, .upper_drop_q8 = 12,
        .lower_raise_q8 = 132, .lower_bend_q12 = 1050,
        .scale_x_q8 = 268, .scale_y_q8 = 242,
        .round_q8 = 336, .pose_dy_q4 = -10,
        .hotspot_size_q8 = 26, .hotspot_gain_q8 = 40,
        .emissive_q8 = 36, .grade_q8 = 48,
        .bounce_amp_q4 = 20, .bounce_period_ms = 640,
        .mouth_curve_q8 = 200, .mouth_open_q8 = 92, .mouth_wide_q8 = 76,
    },
    [FACE_EXPRESSION_CONCERN] = {
        .aperture_q8 = 240, .upper_drop_q8 = 46,
        .upper_slope_q12 = 300, .upper_bend_q12 = 260,
        .lower_raise_q8 = 26,
        .scale_x_q8 = 240, .scale_y_q8 = 252,
        .round_q8 = 236, .taper_q8 = 30,
        .gaze_y_q8 = 16, .tilt_mdeg = -900, .pose_dy_q4 = 4,
        .hotspot_size_q8 = -16, .hotspot_gain_q8 = -10,
        .emissive_q8 = -14, .grade_q8 = -70,
        .mouth_curve_q8 = -110, .mouth_open_q8 = 14,
    },
    [FACE_EXPRESSION_SURPRISE] = {
        .aperture_q8 = 272,
        .scale_x_q8 = 274, .scale_y_q8 = 294,
        .round_q8 = 356, .pose_dy_q4 = -14,
        .gaze_y_q8 = -12,
        .hotspot_size_q8 = 62, .hotspot_gain_q8 = 30,
        .emissive_q8 = 30, .grade_q8 = -12,
        .mouth_curve_q8 = -20, .mouth_open_q8 = 196, .mouth_wide_q8 = -60,
    },
    [FACE_EXPRESSION_THOUGHTFUL] = {
        .aperture_q8 = 244, .upper_drop_q8 = 30,
        .upper_slope_q12 = -80, .upper_bend_q12 = 140,
        .lower_raise_q8 = 20,
        .scale_x_q8 = 252, .scale_y_q8 = 252,
        .round_q8 = 244, .taper_q8 = 18,
        .gaze_x_q8 = -74, .gaze_y_q8 = -96,
        .tilt_mdeg = -2600, .pose_dy_q4 = -2,
        .hotspot_size_q8 = -18, .hotspot_gain_q8 = -8,
        .emissive_q8 = -10, .grade_q8 = -24,
        .mouth_curve_q8 = -40, .mouth_wide_q8 = -40,
    },
    [FACE_EXPRESSION_SKEPTICAL] = {
        .aperture_q8 = 246, .upper_drop_q8 = 26,
        .upper_slope_q12 = -140,
        .lower_raise_q8 = 30,
        .scale_x_q8 = 258, .scale_y_q8 = 246,
        .round_q8 = 232, .taper_q8 = 22,
        .gaze_x_q8 = 58, .tilt_mdeg = 2100,
        .hotspot_size_q8 = -22, .hotspot_gain_q8 = -6,
        .emissive_q8 = -6, .grade_q8 = -46,
        .mouth_curve_q8 = -140, .mouth_open_q8 = 8, .mouth_wide_q8 = -50,
    },
    [FACE_EXPRESSION_DETERMINED] = {
        .aperture_q8 = 228, .upper_drop_q8 = 46,
        .upper_slope_q12 = -340,
        .lower_raise_q8 = 44, .lower_bend_q12 = 60,
        .scale_x_q8 = 276, .scale_y_q8 = 232,
        .round_q8 = 216, .taper_q8 = 38, .sharp_inner_q8 = 170,
        .hotspot_size_q8 = -22, .hotspot_gain_q8 = 28,
        .emissive_q8 = 30, .grade_q8 = 8,
        .mouth_curve_q8 = -40, .mouth_wide_q8 = 66,
    },
    [FACE_EXPRESSION_SLEEPY] = {
        .aperture_q8 = 168, .upper_drop_q8 = 118,
        .upper_slope_q12 = 200, .upper_bend_q12 = 420,
        .lower_raise_q8 = 34,
        .scale_x_q8 = 246, .scale_y_q8 = 224,
        .round_q8 = 268, .gaze_y_q8 = 58,
        .tilt_mdeg = 1900, .pose_dy_q4 = 8,
        .hotspot_size_q8 = 30, .hotspot_gain_q8 = -56,
        .emissive_q8 = -66, .grade_q8 = -30,
        .mouth_curve_q8 = -20, .mouth_open_q8 = 10, .mouth_wide_q8 = -60,
    },
    [FACE_EXPRESSION_EXCITED] = {
        .aperture_q8 = 262,
        .lower_raise_q8 = 66, .lower_bend_q12 = 560,
        .scale_x_q8 = 284, .scale_y_q8 = 272,
        .round_q8 = 320, .pose_dy_q4 = -8,
        .gaze_y_q8 = -14,
        .hotspot_size_q8 = 52, .hotspot_gain_q8 = 58,
        .emissive_q8 = 54, .grade_q8 = 96,
        .bounce_amp_q4 = 28, .bounce_period_ms = 430,
        .shiver_mdeg = 320,
        .mouth_curve_q8 = 190, .mouth_open_q8 = 140, .mouth_wide_q8 = 120,
    },
    [FACE_EXPRESSION_EMBARRASSED] = {
        .aperture_q8 = 216, .upper_drop_q8 = 62,
        .upper_slope_q12 = 260, .upper_bend_q12 = 200,
        .lower_raise_q8 = 84, .lower_bend_q12 = 420,
        .scale_x_q8 = 240, .scale_y_q8 = 242,
        .round_q8 = 276, .gaze_x_q8 = 96, .gaze_y_q8 = 86,
        .tilt_mdeg = 3400, .pose_dy_q4 = 5,
        .hotspot_size_q8 = 18, .hotspot_gain_q8 = -18,
        .emissive_q8 = -8, .grade_q8 = 128,
        .mouth_curve_q8 = 60, .mouth_open_q8 = 10, .mouth_wide_q8 = -30,
    },
};

/* Per-eye asymmetry (index 0 viewer-left), applied with the same weight. */
typedef struct {
    uint8_t expression;
    int16_t aperture_mul_q8[2]; /* 256 == unchanged */
    int16_t extra_drop_q8[2];
    int16_t extra_slope_q12[2];
} fca_asym_t;

/*
 * Asymmetry is an accent, never an amputation: the multipliers keep the
 * narrower eye at well over half the mass of its partner, so every pose
 * stays a bilateral pair (the containment/lid rig separately enforces a
 * visible band in each eye).
 */
static const fca_asym_t FCA_ASYMS[] = {
    { FACE_EXPRESSION_THOUGHTFUL,
      { 256, 230 }, { 0, 20 }, { -40, 96 } },
    { FACE_EXPRESSION_SKEPTICAL,
      { 272, 208 }, { -22, 36 }, { -110, 40 } },
    { FACE_EXPRESSION_EMBARRASSED,
      { 240, 260 }, { 18, 0 }, { 80, 0 } },
};

/* ---- viseme vocabulary ------------------------------------------------ */

/* Mouth articulation target: jaw, width, roundness, teeth (0..255). */
typedef struct {
    uint8_t jaw;
    uint8_t wide;
    uint8_t round;
    uint8_t teeth;
} fca_mouth_shape_t;

static const fca_mouth_shape_t FCA_OVR15[15] = {
    [FACE_VISEME_AA] = { 205, 150, 92, 90 },
    [FACE_VISEME_E] = { 150, 195, 60, 122 },
    [FACE_VISEME_I] = { 112, 205, 52, 150 },
    [FACE_VISEME_O] = { 172, 108, 224, 40 },
    [FACE_VISEME_U] = { 122, 88, 244, 28 },
    [FACE_VISEME_PP] = { 10, 142, 70, 0 },
    [FACE_VISEME_SS] = { 58, 214, 42, 224 },
    [FACE_VISEME_TH] = { 82, 182, 62, 158 },
    [FACE_VISEME_DD] = { 92, 172, 68, 140 },
    [FACE_VISEME_FF] = { 62, 192, 58, 182 },
    [FACE_VISEME_KK] = { 112, 152, 82, 70 },
    [FACE_VISEME_NN] = { 72, 162, 70, 92 },
    [FACE_VISEME_RR] = { 102, 140, 152, 62 },
    [FACE_VISEME_CH] = { 92, 172, 122, 112 },
    [FACE_VISEME_SIL] = { 8, 150, 70, 0 },
};

static const uint8_t FCA_VRM5_TO_OVR[5] = {
    FACE_VISEME_AA, FACE_VISEME_I, FACE_VISEME_U,
    FACE_VISEME_E, FACE_VISEME_O,
};

static const uint8_t FCA_PRESTON9_TO_OVR[9] = {
    FACE_VISEME_AA, FACE_VISEME_E, FACE_VISEME_O,
    FACE_VISEME_U, FACE_VISEME_PP, FACE_VISEME_FF,
    FACE_VISEME_NN, FACE_VISEME_U, FACE_VISEME_SIL,
};

static const uint8_t FCA_MS22_TO_OVR[22] = {
    FACE_VISEME_SIL, FACE_VISEME_E, FACE_VISEME_AA, FACE_VISEME_O,
    FACE_VISEME_E, FACE_VISEME_RR, FACE_VISEME_I, FACE_VISEME_U,
    FACE_VISEME_O, FACE_VISEME_AA, FACE_VISEME_O, FACE_VISEME_AA,
    FACE_VISEME_KK, FACE_VISEME_RR, FACE_VISEME_NN, FACE_VISEME_SS,
    FACE_VISEME_CH, FACE_VISEME_TH, FACE_VISEME_FF, FACE_VISEME_DD,
    FACE_VISEME_KK, FACE_VISEME_PP,
};

/* Resolve one viseme id in the record's declared vocabulary; false when
 * the vocabulary or id is unknown (callers then keep controls only). */
static bool fca_viseme_shape(
    uint8_t viseme_set, uint8_t viseme, fca_mouth_shape_t *shape)
{
    uint8_t ovr = 0xFFU;
    switch (viseme_set) {
    case FACE_VISEME_SET_OVR15:
        if (viseme < 15U) {
            ovr = viseme;
        }
        break;
    case FACE_VISEME_SET_VRM5:
        if (viseme < 5U) {
            ovr = FCA_VRM5_TO_OVR[viseme];
        }
        break;
    case FACE_VISEME_SET_PRESTON9:
        if (viseme < 9U) {
            ovr = FCA_PRESTON9_TO_OVR[viseme];
        }
        break;
    case FACE_VISEME_SET_MICROSOFT22:
        if (viseme < 22U) {
            ovr = FCA_MS22_TO_OVR[viseme];
        }
        break;
    default:
        break;
    }
    if (ovr >= 15U) {
        return false;
    }
    *shape = FCA_OVR15[ovr];
    return true;
}

/* True when the viseme narrows into a sibilant/fricative hiss. */
static bool fca_viseme_sibilant(uint8_t viseme_set, uint8_t viseme)
{
    fca_mouth_shape_t shape;
    if (!fca_viseme_shape(viseme_set, viseme, &shape)) {
        return false;
    }
    return shape.teeth >= 150U;
}

/* ---- working pose ------------------------------------------------------ */

typedef struct {
    /* Continuous channels before screen-space projection. */
    int32_t aperture_q8[2];
    int32_t upper_drop_q8[2];
    int32_t upper_slope_q12[2];
    int32_t upper_bend_q12[2];
    int32_t lower_raise_q8[2];
    int32_t lower_slope_q12[2];
    int32_t lower_bend_q12[2];
    int32_t scale_x_q8;
    int32_t scale_y_q8;
    int32_t eye_scale_x_q8[2]; /* per-eye perspective on top of pair scale */
    int32_t taper_q8;
    int32_t round_q8;
    int32_t sharp_inner_q8;
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    int32_t lid_gaze_y_q8;
    int32_t pair_dx_q4;
    int32_t pair_dy_q4;
    int32_t roll_mdeg;
    int32_t vergence_q4;
    int32_t hotspot_size_q8;
    int32_t hotspot_gain_q8;
    int32_t emissive_q8;
    int32_t grade_q8;
    /* Mouth channels. jaw/wide carry the PCM-articulated mouth;
     * the *_emote pair carries authored emotion so a compact accent
     * mouth can act without chattering to the audio envelope. */
    int32_t jaw_q8;
    int32_t wide_q8;
    int32_t jaw_emote_q8;
    int32_t wide_emote_q8;
    int32_t emphasis_q8;
    int32_t round_mouth_q8;
    int32_t teeth_q8;
    int32_t tongue_q8;
    int32_t press_q8;
    int32_t curve_q8;
    int32_t curve_asym_q8;
} fca_channels_t;

static int32_t fca_toward(int32_t base, int32_t target, int32_t w_q8)
{
    return base + (int32_t)fca_sar64((int64_t)(target - base) * w_q8, 8);
}

static void fca_channels_init(
    const fca_profile_def_t *def,
    const fca_score_t *score,
    fca_channels_t *ch)
{
    memset(ch, 0, sizeof(*ch));
    ch->aperture_q8[0] = score->blink_q8[0];
    ch->aperture_q8[1] = score->blink_q8[1];
    ch->scale_x_q8 = score->dart_stretch_q8;
    /* Preserve area during dart stretch: y compensates half of x. */
    ch->scale_y_q8 = 256 - (score->dart_stretch_q8 - 256) / 2;
    ch->eye_scale_x_q8[0] = 256;
    ch->eye_scale_x_q8[1] = 256;
    ch->round_q8 = 256;
    ch->gaze_x_q8 = score->gaze_x_q8;
    ch->gaze_y_q8 = score->gaze_y_q8;
    ch->lid_gaze_y_q8 = score->lid_gaze_y_q8;
    ch->pair_dy_q4 = score->breath_y_q4;
    ch->roll_mdeg = score->act_roll_mdeg;
    ch->hotspot_size_q8 = def->hotspot_q8;
    ch->hotspot_gain_q8 = 150;
    ch->emissive_q8 = 256;
    /* The director's squint act closes from below. */
    ch->lower_raise_q8[0] = (score->act_squint_q8 * 96) >> 8;
    ch->lower_raise_q8[1] = ch->lower_raise_q8[0];
    ch->aperture_q8[0] -= (score->act_squint_q8 * 92) >> 8;
    ch->aperture_q8[1] -= (score->act_squint_q8 * 92) >> 8;
    /* Mouth baseline: relaxed. */
    ch->wide_q8 = 150;
    ch->round_mouth_q8 = 70;
}

static void fca_apply_activity(
    uint8_t activity, uint8_t drowsy, int32_t arousal, fca_channels_t *ch)
{
    switch (activity) {
    case FACE_ACTIVITY_LISTENING:
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 266) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 266) >> 8;
        ch->gaze_y_q8 -= 10;
        ch->hotspot_gain_q8 += 14;
        break;
    case FACE_ACTIVITY_THINKING:
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 240) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 240) >> 8;
        ch->hotspot_size_q8 -= 10;
        break;
    case FACE_ACTIVITY_SPEAKING:
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 262) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 262) >> 8;
        ch->emissive_q8 += 10;
        break;
    default:
        if (drowsy != 0U && arousal < 100) {
            ch->upper_drop_q8[0] += 30;
            ch->upper_drop_q8[1] += 30;
            ch->aperture_q8[0] = (ch->aperture_q8[0] * 236) >> 8;
            ch->aperture_q8[1] = (ch->aperture_q8[1] * 236) >> 8;
        }
        break;
    }
}

static void fca_apply_emotion(
    const face_render_key_t *key,
    uint32_t t_ms,
    fca_channels_t *ch)
{
    if (key->schema_version < FACE_RENDER_KEY_SCHEMA_VERSION) {
        return;
    }
    const uint8_t expression = key->stage_expression;
    if (expression >= FACE_EXPRESSION_COUNT) {
        return; /* FACE_EXPRESSION_CUSTOM rides on dense bytes alone. */
    }
    const int32_t w = key->expression_weight;
    if (w == 0) {
        return;
    }
    const fca_emotion_t *e = &FCA_EMOTIONS[expression];

    for (int eye = 0; eye < 2; ++eye) {
        ch->aperture_q8[eye] = (int32_t)fca_sar64(
            (int64_t)ch->aperture_q8[eye] *
                fca_toward(256, e->aperture_q8, w), 8);
        ch->upper_drop_q8[eye] +=
            (int32_t)fca_sar64((int64_t)e->upper_drop_q8 * w, 8);
        ch->upper_slope_q12[eye] +=
            (int32_t)fca_sar64((int64_t)e->upper_slope_q12 * w, 8);
        ch->upper_bend_q12[eye] +=
            (int32_t)fca_sar64((int64_t)e->upper_bend_q12 * w, 8);
        ch->lower_raise_q8[eye] +=
            (int32_t)fca_sar64((int64_t)e->lower_raise_q8 * w, 8);
        ch->lower_slope_q12[eye] +=
            (int32_t)fca_sar64((int64_t)e->lower_slope_q12 * w, 8);
        ch->lower_bend_q12[eye] +=
            (int32_t)fca_sar64((int64_t)e->lower_bend_q12 * w, 8);
    }
    ch->scale_x_q8 = (int32_t)fca_sar64(
        (int64_t)ch->scale_x_q8 * fca_toward(256, e->scale_x_q8, w), 8);
    ch->scale_y_q8 = (int32_t)fca_sar64(
        (int64_t)ch->scale_y_q8 * fca_toward(256, e->scale_y_q8, w), 8);
    ch->taper_q8 += (int32_t)fca_sar64((int64_t)e->taper_q8 * w, 8);
    ch->round_q8 = fca_toward(ch->round_q8, e->round_q8, w);
    ch->sharp_inner_q8 +=
        (int32_t)fca_sar64((int64_t)e->sharp_inner_q8 * w, 8);
    ch->gaze_x_q8 += (int32_t)fca_sar64((int64_t)e->gaze_x_q8 * w, 8);
    ch->gaze_y_q8 += (int32_t)fca_sar64((int64_t)e->gaze_y_q8 * w, 8);
    ch->lid_gaze_y_q8 += (int32_t)fca_sar64((int64_t)e->gaze_y_q8 * w, 8);
    ch->roll_mdeg += (int32_t)fca_sar64((int64_t)e->tilt_mdeg * w, 8);
    ch->pair_dy_q4 += (int32_t)fca_sar64((int64_t)e->pose_dy_q4 * w, 8);
    ch->hotspot_size_q8 +=
        (int32_t)fca_sar64((int64_t)e->hotspot_size_q8 * w, 8);
    ch->hotspot_gain_q8 +=
        (int32_t)fca_sar64((int64_t)e->hotspot_gain_q8 * w, 8);
    ch->emissive_q8 += (int32_t)fca_sar64((int64_t)e->emissive_q8 * w, 8);
    ch->grade_q8 += (int32_t)fca_sar64((int64_t)e->grade_q8 * w, 8);
    ch->curve_q8 += (int32_t)fca_sar64((int64_t)e->mouth_curve_q8 * w, 8);
    ch->jaw_emote_q8 +=
        (int32_t)fca_sar64((int64_t)e->mouth_open_q8 * w, 8);
    ch->wide_emote_q8 +=
        (int32_t)fca_sar64((int64_t)e->mouth_wide_q8 * w, 8);

    if (e->bounce_amp_q4 != 0 && e->bounce_period_ms != 0U) {
        const int32_t wave =
            fca_sin_q14(fca_turn16(t_ms, e->bounce_period_ms));
        /* Full-wave rectified bounce: a hop, not a sway. */
        const int32_t hop = fca_abs(wave);
        ch->pair_dy_q4 -= (int32_t)fca_sar64(
            (int64_t)hop * e->bounce_amp_q4 * w, 14 + 8);
    }
    if (e->shiver_mdeg != 0) {
        const int32_t wave = fca_sin_q14(fca_turn16(t_ms, 190U));
        ch->roll_mdeg += (int32_t)fca_sar64(
            (int64_t)wave * e->shiver_mdeg * w, 14 + 8);
    }

    for (size_t i = 0; i < sizeof(FCA_ASYMS) / sizeof(FCA_ASYMS[0]); ++i) {
        if (FCA_ASYMS[i].expression != expression) {
            continue;
        }
        for (int eye = 0; eye < 2; ++eye) {
            ch->aperture_q8[eye] = (int32_t)fca_sar64(
                (int64_t)ch->aperture_q8[eye] *
                    fca_toward(256, FCA_ASYMS[i].aperture_mul_q8[eye], w),
                8);
            ch->upper_drop_q8[eye] += (int32_t)fca_sar64(
                (int64_t)FCA_ASYMS[i].extra_drop_q8[eye] * w, 8);
            ch->upper_slope_q12[eye] += (int32_t)fca_sar64(
                (int64_t)FCA_ASYMS[i].extra_slope_q12[eye] * w, 8);
        }
        break;
    }
}

static void fca_apply_dense_actions(
    const face_render_key_t *key, fca_channels_t *ch)
{
    const bool extended =
        key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION;

    /* Authored per-eye lids: 238 is the fixtures' canonical open. */
    const int32_t open_auth[2] = {
        key->controls.eye_left_open,
        key->controls.eye_right_open,
    };
    for (int eye = 0; eye < 2; ++eye) {
        const int32_t mul =
            fca_clamp((open_auth[eye] * 276) / 238, 0, 288);
        ch->aperture_q8[eye] = (int32_t)fca_sar64(
            (int64_t)ch->aperture_q8[eye] * mul, 8);
    }
    if ((key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 20) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 20) >> 8;
    }

    /* Legacy single brow byte: raise lifts the lids, lower knits them. */
    const int32_t brow = key->controls.brow;
    if (brow > 0) {
        ch->upper_drop_q8[0] -= (brow * 66) / 127;
        ch->upper_drop_q8[1] -= (brow * 66) / 127;
        ch->aperture_q8[0] += (brow * 22) / 127;
        ch->aperture_q8[1] += (brow * 22) / 127;
    } else if (brow < 0) {
        ch->upper_drop_q8[0] += (-brow * 80) / 127;
        ch->upper_drop_q8[1] += (-brow * 80) / 127;
        ch->upper_slope_q12[0] += (brow * 120) / 127;
        ch->upper_slope_q12[1] += (brow * 120) / 127;
    }

    /* Authored gaze from the stable prefix. */
    ch->gaze_x_q8 += (int32_t)key->controls.look_x * 2;
    ch->gaze_y_q8 += (int32_t)key->controls.look_y * 2;
    ch->lid_gaze_y_q8 += (int32_t)key->controls.look_y * 2;

    if (!extended) {
        return;
    }

    /* Independent squints ride the lower lids up and compress apertures. */
    const int32_t squint[2] = {
        key->eye_left_squint, key->eye_right_squint,
    };
    for (int eye = 0; eye < 2; ++eye) {
        ch->aperture_q8[eye] = (int32_t)fca_sar64(
            (int64_t)ch->aperture_q8[eye] *
                (256 - (squint[eye] * 168) / 255), 8);
        ch->lower_raise_q8[eye] += (squint[eye] * 90) / 255;
    }

    /* Brow actions land on lid slope for a brow-less silhouette. */
    const int32_t inner = key->brow_inner;
    ch->upper_slope_q12[0] += inner * 3;
    ch->upper_slope_q12[1] += inner * 3;
    if (inner > 0) {
        ch->upper_drop_q8[0] -= inner / 4;
        ch->upper_drop_q8[1] -= inner / 4;
    } else {
        ch->upper_drop_q8[0] += (-inner) / 3;
        ch->upper_drop_q8[1] += (-inner) / 3;
    }
    const int32_t outer[2] = {
        key->brow_outer_left, key->brow_outer_right,
    };
    for (int eye = 0; eye < 2; ++eye) {
        ch->upper_slope_q12[eye] -= outer[eye] * 2;
        ch->upper_drop_q8[eye] -= outer[eye] / 4;
    }

    /* Cheeks push the lower lids into an arc and warm the grade. */
    const int32_t cheek = key->cheek;
    ch->lower_raise_q8[0] += (cheek * 120) / 255;
    ch->lower_raise_q8[1] += (cheek * 120) / 255;
    ch->lower_bend_q12[0] += cheek * 3;
    ch->lower_bend_q12[1] += cheek * 3;
    ch->grade_q8 += cheek / 3;

    /* Mouth corners: smiling eyes everywhere, mouth curve on CHATTER. */
    const int32_t corner_l = key->mouth_corner_left;
    const int32_t corner_r = key->mouth_corner_right;
    ch->lower_bend_q12[0] += corner_l;
    ch->lower_bend_q12[1] += corner_r;
    ch->curve_q8 += (corner_l + corner_r) / 2;
    ch->curve_asym_q8 = (corner_r - corner_l) / 2;

    /* Affect: valence grades color and leans the lids; arousal boosts
     * light and pace (pace is already the director's business). */
    const int32_t valence = key->affect_valence;
    ch->grade_q8 += (valence * 3) / 4;
    if (valence > 0) {
        ch->lower_bend_q12[0] += valence;
        ch->lower_bend_q12[1] += valence;
    } else if (valence < 0) {
        if (key->affect_arousal > 150U) {
            ch->upper_slope_q12[0] += valence; /* cross lean */
            ch->upper_slope_q12[1] += valence;
        } else {
            ch->upper_slope_q12[0] -= valence / 2; /* sad lean */
            ch->upper_slope_q12[1] -= valence / 2;
        }
    }
    const int32_t arousal = key->affect_arousal;
    ch->emissive_q8 += (arousal - 128) / 3;
    ch->hotspot_gain_q8 += (arousal - 128) / 5;

    /* Attention: vergence toward the shared point and a tighter core. */
    const int32_t attention = key->attention;
    if (attention > 140) {
        ch->vergence_q4 = fca_min(19, ((attention - 140) * 19) / 115);
    }
    ch->hotspot_size_q8 -= (attention - 128) / 8;
    ch->hotspot_gain_q8 += (attention - 128) / 6;

    /* Head pose and body lean move the whole stage. */
    const int32_t yaw = key->head_yaw;
    const int32_t pitch = key->head_pitch;
    ch->pair_dx_q4 += (yaw * 144) / 127;
    ch->pair_dy_q4 += (pitch * 112) / 127;
    ch->lid_gaze_y_q8 += pitch / 2;
    if (pitch > 0) {
        ch->aperture_q8[0] -= pitch / 4;
        ch->aperture_q8[1] -= pitch / 4;
    }
    if (yaw > 0) {
        ch->eye_scale_x_q8[0] -= (yaw * 34) / 127;
    } else if (yaw < 0) {
        ch->eye_scale_x_q8[1] -= (-yaw * 34) / 127;
    }
    ch->roll_mdeg += key->head_roll * 70;

    const int32_t lean_x = key->body_lean_x;
    const int32_t lean_y = key->body_lean_y;
    ch->pair_dx_q4 += (lean_x * 40) / 127;
    ch->roll_mdeg += lean_x * 18;
    if (lean_y > 0) {
        /* Leaning in: closer, so slightly larger and higher. */
        ch->scale_x_q8 += (lean_y * 20) / 127;
        ch->scale_y_q8 += (lean_y * 20) / 127;
        ch->pair_dy_q4 -= (lean_y * 24) / 127;
    } else if (lean_y < 0) {
        ch->scale_x_q8 += (lean_y * 14) / 127;
        ch->scale_y_q8 += (lean_y * 14) / 127;
        ch->pair_dy_q4 -= (lean_y * 16) / 127;
    }
}

static void fca_apply_speech(
    const fca_profile_def_t *def,
    const face_render_key_t *key,
    const fca_score_t *score,
    fca_channels_t *ch)
{
    const bool extended =
        key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION;
    const uint8_t phase =
        extended ? key->speech_phase : (uint8_t)FACE_SPEECH_IDLE;

    if (phase == FACE_SPEECH_STARTING) {
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 268) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 268) >> 8;
        ch->gaze_x_q8 -= (ch->gaze_x_q8 * 154) / 256;
        ch->gaze_y_q8 -= (ch->gaze_y_q8 * 154) / 256;
        ch->emissive_q8 += 12;
    } else if (phase == FACE_SPEECH_ENDING) {
        ch->aperture_q8[0] = (ch->aperture_q8[0] * 248) >> 8;
        ch->aperture_q8[1] = (ch->aperture_q8[1] * 248) >> 8;
        ch->emissive_q8 -= 8;
    }

    /* Instantaneous emphasis: light and one or two pixels of presence. */
    const int32_t emphasis = score->emphasis_q8;
    ch->emissive_q8 += emphasis / 5;
    ch->hotspot_gain_q8 += emphasis / 6;
    ch->scale_y_q8 += emphasis / 28;

    /* Sibilants narrow the eyes a whisper. */
    const uint8_t viseme_set =
        extended ? key->viseme_set : (uint8_t)FACE_VISEME_SET_OVR15;
    if (key->viseme_weight > 60U &&
        fca_viseme_sibilant(viseme_set, key->viseme)) {
        const int32_t nick = ((int32_t)key->viseme_weight - 60) / 6;
        ch->aperture_q8[0] -= nick;
        ch->aperture_q8[1] -= nick;
    }

    /* Mouth articulation, resolved for every profile. */
    ch->emphasis_q8 = emphasis;
    ch->jaw_q8 += ((int32_t)key->controls.mouth_open * 256) / 255;
    ch->wide_q8 = ((int32_t)key->controls.mouth_width * 256) / 255 +
                  ch->wide_emote_q8;
    ch->round_mouth_q8 = ((int32_t)key->controls.mouth_round * 256) / 255;
    ch->press_q8 = ((int32_t)key->controls.mouth_press * 256) / 255;
    ch->teeth_q8 = ((int32_t)key->controls.mouth_teeth * 256) / 255;
    ch->tongue_q8 = extended ? ((int32_t)key->tongue * 256) / 255 : 0;

    fca_mouth_shape_t primary;
    if (key->viseme_weight > 0U &&
        fca_viseme_shape(viseme_set, key->viseme, &primary)) {
        fca_mouth_shape_t blended = primary;
        fca_mouth_shape_t secondary;
        const uint8_t vb = extended ? key->viseme_blend : 0U;
        if (vb > 0U &&
            fca_viseme_shape(
                viseme_set,
                extended ? key->viseme_secondary : (uint8_t)0xFFU,
                &secondary)) {
            blended.jaw = (uint8_t)(
                ((uint32_t)primary.jaw * (255U - vb) +
                 (uint32_t)secondary.jaw * vb) / 255U);
            blended.wide = (uint8_t)(
                ((uint32_t)primary.wide * (255U - vb) +
                 (uint32_t)secondary.wide * vb) / 255U);
            blended.round = (uint8_t)(
                ((uint32_t)primary.round * (255U - vb) +
                 (uint32_t)secondary.round * vb) / 255U);
            blended.teeth = (uint8_t)(
                ((uint32_t)primary.teeth * (255U - vb) +
                 (uint32_t)secondary.teeth * vb) / 255U);
        }
        const int32_t vw = key->viseme_weight;
        /* The viseme reshapes width/roundness and gates the jaw. */
        ch->wide_q8 = fca_toward(ch->wide_q8, blended.wide, vw);
        ch->round_mouth_q8 =
            fca_toward(ch->round_mouth_q8, blended.round, vw);
        ch->teeth_q8 = fca_toward(ch->teeth_q8, blended.teeth, vw);
        const int32_t viseme_jaw =
            (int32_t)fca_sar64(
                (int64_t)blended.jaw *
                    fca_max((int32_t)key->audio_level, 84), 8);
        ch->jaw_q8 = fca_toward(ch->jaw_q8, viseme_jaw, vw);
    }
    (void)def;
}

/* ---- screen-space projection and containment --------------------------- */

typedef struct {
    int32_t cos_q14;
    int32_t sin_q14;
} fca_rot_t;

static fca_rot_t fca_rot_from_mdeg(int32_t mdeg)
{
    /* 360000 mdeg == one turn of 65536. */
    const int32_t wrapped = mdeg % 360000;
    const uint32_t turn =
        (uint32_t)(((int64_t)(wrapped + 360000) << 16) / 360000);
    fca_rot_t rot;
    rot.cos_q14 = fca_cos_q14(turn);
    rot.sin_q14 = fca_sin_q14(turn);
    return rot;
}

static void fca_project(
    const fca_profile_def_t *def,
    const fca_channels_t *ch,
    face_cozmo_acting_pose_t *pose)
{
    const int32_t scale_x = fca_clamp(ch->scale_x_q8, 176, 320);
    const int32_t scale_y = fca_clamp(ch->scale_y_q8, 168, 320);
    const int32_t roll = fca_clamp(ch->roll_mdeg, -16000, 16000);
    const int32_t gaze_x = fca_clamp(ch->gaze_x_q8, -256, 256);
    const int32_t gaze_y = fca_clamp(ch->gaze_y_q8, -256, 256);

    const int32_t gaze_dx_q4 =
        (gaze_x * def->travel_x * FCA_Q4) / 256;
    const int32_t gaze_dy_q4 =
        (gaze_y * def->travel_y * FCA_Q4) / 256;

    const int32_t cx_q4 =
        (FACE_COZMO_ACTING_WIDTH / 2) * FCA_Q4 +
        fca_clamp(ch->pair_dx_q4, -160, 160) + gaze_dx_q4;
    const int32_t cy_q4 =
        def->eye_cy * FCA_Q4 +
        fca_clamp(ch->pair_dy_q4, -128, 128) + gaze_dy_q4;

    const fca_rot_t rot = fca_rot_from_mdeg(roll);
    const int32_t half_gap_q4 = (def->eye_gap * FCA_Q4) / 2;

    pose->roll_mdeg = roll;
    pose->gaze_x_q8 = gaze_x;
    pose->gaze_y_q8 = gaze_y;

    for (int eye = 0; eye < 2; ++eye) {
        const int32_t side = eye == 0 ? -1 : 1;
        /* Rotate the eye offset around the pair center. */
        const int32_t off_x = side * half_gap_q4 - side * ch->vergence_q4;
        const int32_t ex = (int32_t)fca_sar64(
            (int64_t)rot.cos_q14 * off_x, 14);
        const int32_t ey = (int32_t)fca_sar64(
            (int64_t)rot.sin_q14 * off_x, 14);
        pose->eye_cx_q4[eye] = cx_q4 + ex;
        pose->eye_cy_q4[eye] = cy_q4 + ey;

        const int32_t sx = (int32_t)fca_sar64(
            (int64_t)scale_x *
                fca_clamp(ch->eye_scale_x_q8[eye], 192, 288), 8);
        pose->eye_hw_q4[eye] = (int32_t)fca_sar64(
            (int64_t)def->eye_hw * FCA_Q4 * sx, 8);
        /*
         * Gaze-coupled squash and stretch (the classic Cozmo/Vector
         * recipe): looking up stretches the eyes, looking down squashes
         * them, and a sideways look makes the leading eye taller than
         * the trailing one.
         */
        const int32_t stretch_v = 256 - (gaze_y * 20) / 256;
        const int32_t lead_h = side * (gaze_x * 12) / 256;
        pose->eye_hh_q4[eye] = (int32_t)fca_sar64(
            (int64_t)def->eye_hh * FCA_Q4 * scale_y, 8);
        pose->eye_hh_q4[eye] = (int32_t)fca_sar64(
            (int64_t)pose->eye_hh_q4[eye] *
                fca_clamp(stretch_v + lead_h, 208, 300), 8);

        const int32_t round_mul = fca_clamp(ch->round_q8, 96, 420);
        const int32_t max_r_q4 =
            fca_min(pose->eye_hw_q4[eye], pose->eye_hh_q4[eye]) - 8;
        for (int corner = 0; corner < 4; ++corner) {
            int32_t r_q4 = (int32_t)fca_sar64(
                (int64_t)def->corner_px[corner] * FCA_Q4 * round_mul, 8);
            /* corner 0/1 are the top pair; the inner-top corner can be
             * sharpened for the determined V. Inner is TL for the right
             * eye and TR for the left eye in screen space; in local
             * outward-x the inner corner is always index 0. */
            if (corner == 0 && ch->sharp_inner_q8 > 0) {
                r_q4 = (int32_t)fca_sar64(
                    (int64_t)r_q4 *
                        fca_max(64, 256 - ch->sharp_inner_q8), 8);
            }
            pose->corner_q4[eye][corner] =
                fca_clamp(r_q4, 4, fca_max(max_r_q4, 4));
        }
        pose->taper_q8[eye] = fca_clamp(ch->taper_q8, -96, 96);

        const int32_t ap = fca_clamp(ch->aperture_q8[eye], 0, 288);
        pose->aperture_q8[eye] = ap;
        /* The closing deficit splits 72/28 between upper and lower lids,
         * mirroring how human lids meet below the pupil line. */
        const int32_t deficit = fca_max(0, 256 - ap);
        int32_t upper = (deficit * 184) / 256 +
            fca_clamp(ch->upper_drop_q8[eye], -64, 256);
        int32_t lower = (deficit * 72) / 256 +
            fca_clamp(ch->lower_raise_q8[eye], -48, 256);
        /* Lids track vertical gaze: looking down lowers the curtain. */
        upper += (ch->lid_gaze_y_q8 * 30) / 256;
        lower -= (ch->lid_gaze_y_q8 * 12) / 256;
        /*
         * Minimum visible eye mass: while the eye is logically open
         * (not blinking or authored shut), the two lids together may
         * cover at most 232/256 of the eye, so every stage expression
         * keeps a readable emissive band in both eyes. Blinks and
         * authored closures pass through and are drawn as a luminous
         * seam instead of a void.
         */
        if (ap > 40) {
            const int32_t total = upper + lower;
            if (total > 224) {
                upper = (upper * 224) / total;
                lower = (lower * 224) / total;
            }
        }
        pose->upper_drop_q8[eye] = fca_clamp(upper, -64, 300);
        pose->lower_raise_q8[eye] = fca_clamp(lower, -64, 300);
        pose->upper_slope_q12[eye] =
            fca_clamp(ch->upper_slope_q12[eye], -1600, 1600);
        pose->upper_bend_q12[eye] =
            fca_clamp(ch->upper_bend_q12[eye], -1600, 1600);
        pose->lower_slope_q12[eye] =
            fca_clamp(ch->lower_slope_q12[eye], -1600, 1600);
        pose->lower_bend_q12[eye] =
            fca_clamp(ch->lower_bend_q12[eye], -1600, 1600);
    }

    /* The pupil-like core never fully disappears: a floor on the gain
     * keeps a readable focus point in every expression. */
    pose->hotspot_size_q8 = fca_clamp(ch->hotspot_size_q8, 48, 220);
    pose->hotspot_gain_q8 = fca_clamp(ch->hotspot_gain_q8, 72, 256);
    pose->emissive_q8 = fca_clamp(ch->emissive_q8, 96, 320);
    pose->grade_q8 = fca_clamp(ch->grade_q8, -256, 256);
    pose->bloom_q4 = def->bloom_px * FCA_Q4;

    /* Mouth pose. */
    if (def->has_mouth != 0U) {
        const int32_t wide = fca_clamp(ch->wide_q8, 40, 300);
        const int32_t round_m = fca_clamp(ch->round_mouth_q8, 0, 256);
        const int32_t press = fca_clamp(ch->press_q8, 0, 256);
        int32_t jaw = fca_clamp(ch->jaw_q8, 0, 300);
        jaw = (jaw * (256 - (press * 176) / 256)) >> 8;
        /*
         * Roundness trades width for openness, and the profile's verve
         * decides how far articulation may go: Chatter performs full
         * viseme shapes, the other profiles keep a compact accent
         * mouth that still smiles, frowns, gasps and presses.
         */
        const int32_t verve = def->mouth_verve;
        if (verve < 200) {
            /* The accent mouth ignores the raw PCM jaw and performs
             * the authored emotion plus a whisper of speech energy. */
            jaw = fca_clamp(
                ch->jaw_emote_q8 + ch->emphasis_q8 / 4, 0, 300);
            jaw = (jaw * (256 - (press * 176) / 256)) >> 8;
        } else {
            jaw = fca_clamp(jaw + ch->jaw_emote_q8 / 2, 0, 300);
            /* A raised tongue keeps the articulating mouth ajar. */
            jaw = fca_clamp(jaw + ch->tongue_q8 / 6, 0, 300);
        }
        const int32_t hw_q4 = (int32_t)fca_sar64(
            (int64_t)def->mouth_hw * FCA_Q4 *
                (140 + (wide * 140) / 256 - (round_m * 70) / 256), 8);
        int32_t open_q4 = (int32_t)fca_sar64(
            (int64_t)jaw * (14 + (round_m * 12) / 256) * FCA_Q4 * verve,
            8 + 8);
        const int32_t open_cap = (int32_t)fca_sar64(
            (int64_t)hw_q4 * (verve >= 200 ? 230 : 110), 8);
        open_q4 = fca_min(open_q4, open_cap);
        pose->mouth_cx_q4 = cx_q4 +
            (int32_t)fca_sar64((int64_t)ch->curve_asym_q8 * FCA_Q4, 6);
        pose->mouth_cy_q4 =
            def->mouth_cy * FCA_Q4 +
            fca_clamp(ch->pair_dy_q4, -128, 128) / 2 +
            gaze_dy_q4 / 3;
        pose->mouth_hw_q4 = fca_max(hw_q4, 6 * FCA_Q4);
        pose->mouth_open_q4 = open_q4;
        pose->mouth_curve_q8 = fca_clamp(ch->curve_q8, -256, 256);
        pose->mouth_round_q8 = round_m;
        pose->mouth_teeth_q8 = fca_clamp(ch->teeth_q8, 0, 256);
    } else {
        pose->mouth_cx_q4 = 0;
        pose->mouth_cy_q4 = 0;
        pose->mouth_hw_q4 = 0;
        pose->mouth_open_q4 = 0;
        pose->mouth_curve_q8 = 0;
        pose->mouth_round_q8 = 0;
        pose->mouth_teeth_q8 = 0;
    }
}

/*
 * Containment governor. Every reachable extremity — rotated eye corners,
 * bloom reach, reopen overshoot, the mouth capsule — is bounded by an
 * analytic AABB per eye. If the pose would leak past the safe rectangle
 * the whole stage is first translated, then uniformly shrunk. This is
 * what makes "no clipping, ever" a property instead of a hope.
 */
static void fca_contain(
    const fca_profile_def_t *def, face_cozmo_acting_pose_t *pose)
{
    const int32_t safe_lo_x =
        (FACE_COZMO_ACTING_SAFE_MARGIN + 1) * FCA_Q4;
    const int32_t safe_hi_x =
        (FACE_COZMO_ACTING_WIDTH - FACE_COZMO_ACTING_SAFE_MARGIN - 1) *
        FCA_Q4;
    const int32_t safe_lo_y =
        (FACE_COZMO_ACTING_SAFE_MARGIN + 1) * FCA_Q4;
    const int32_t safe_hi_y =
        (FACE_COZMO_ACTING_HEIGHT - FACE_COZMO_ACTING_SAFE_MARGIN - 1) *
        FCA_Q4;

    const fca_rot_t rot = fca_rot_from_mdeg(pose->roll_mdeg);
    const int32_t abs_cos = fca_abs(rot.cos_q14);
    const int32_t abs_sin = fca_abs(rot.sin_q14);
    /* Reopen overshoot can push lids past the rest silhouette a hair;
     * one extra pixel plus the AA ramp covers it. */
    const int32_t slack_q4 = pose->bloom_q4 + FCA_Q4 + 12;

    pose->governor_engaged = 0U;

    for (int pass = 0; pass < 2; ++pass) {
        int32_t worst_dx = 0;
        int32_t worst_dy = 0;
        for (int eye = 0; eye < 2; ++eye) {
            const int32_t ex = (int32_t)fca_sar64(
                (int64_t)pose->eye_hw_q4[eye] * abs_cos +
                    (int64_t)pose->eye_hh_q4[eye] * abs_sin, 14) +
                slack_q4;
            const int32_t ey = (int32_t)fca_sar64(
                (int64_t)pose->eye_hw_q4[eye] * abs_sin +
                    (int64_t)pose->eye_hh_q4[eye] * abs_cos, 14) +
                slack_q4;
            const int32_t lo_x = pose->eye_cx_q4[eye] - ex;
            const int32_t hi_x = pose->eye_cx_q4[eye] + ex;
            const int32_t lo_y = pose->eye_cy_q4[eye] - ey;
            const int32_t hi_y = pose->eye_cy_q4[eye] + ey;
            if (lo_x < safe_lo_x) {
                worst_dx = fca_max(worst_dx, safe_lo_x - lo_x);
            }
            if (hi_x > safe_hi_x) {
                worst_dx = fca_min(worst_dx, safe_hi_x - hi_x);
            }
            if (lo_y < safe_lo_y) {
                worst_dy = fca_max(worst_dy, safe_lo_y - lo_y);
            }
            if (hi_y > safe_hi_y) {
                worst_dy = fca_min(worst_dy, safe_hi_y - hi_y);
            }
        }
        if (def->has_mouth != 0U && pose->mouth_hw_q4 > 0) {
            /* The painter's exact reach: lip stroke + additive glow
             * along x, and opening + corner arch + stroke + glow
             * along y. The mouth has no bloom halo, so the eye slack
             * does not apply here. */
            const int32_t arch_q12 =
                fca_abs(-(pose->mouth_curve_q8 * 5) / 2);
            const int32_t arch_rise_q4 = (int32_t)fca_sar64(
                (int64_t)arch_q12 * pose->mouth_hw_q4 *
                    pose->mouth_hw_q4, 20);
            const int32_t mex = pose->mouth_hw_q4 + 96;
            const int32_t mey =
                pose->mouth_open_q4 + arch_rise_q4 + 104;
            if (pose->mouth_cx_q4 - mex < safe_lo_x) {
                worst_dx = fca_max(
                    worst_dx, safe_lo_x - (pose->mouth_cx_q4 - mex));
            }
            if (pose->mouth_cx_q4 + mex > safe_hi_x) {
                worst_dx = fca_min(
                    worst_dx, safe_hi_x - (pose->mouth_cx_q4 + mex));
            }
            if (pose->mouth_cy_q4 - mey < safe_lo_y) {
                worst_dy = fca_max(
                    worst_dy, safe_lo_y - (pose->mouth_cy_q4 - mey));
            }
            if (pose->mouth_cy_q4 + mey > safe_hi_y) {
                worst_dy = fca_min(
                    worst_dy, safe_hi_y - (pose->mouth_cy_q4 + mey));
            }
        }

        if (worst_dx == 0 && worst_dy == 0) {
            return;
        }
        pose->governor_engaged = 1U;
        for (int eye = 0; eye < 2; ++eye) {
            pose->eye_cx_q4[eye] += worst_dx;
            pose->eye_cy_q4[eye] += worst_dy;
        }
        pose->mouth_cx_q4 += worst_dx;
        pose->mouth_cy_q4 += worst_dy;

        if (pass == 1) {
            break;
        }
    }

    /*
     * Still out after translating (opposite edges both violated, i.e.
     * the stage is simply too large): shrink everything toward the
     * frame center until the widest extent fits.
     */
    int32_t need_num = 256;
    for (int eye = 0; eye < 2; ++eye) {
        const int32_t ex = (int32_t)fca_sar64(
            (int64_t)pose->eye_hw_q4[eye] * abs_cos +
                (int64_t)pose->eye_hh_q4[eye] * abs_sin, 14) + slack_q4;
        const int32_t ey = (int32_t)fca_sar64(
            (int64_t)pose->eye_hw_q4[eye] * abs_sin +
                (int64_t)pose->eye_hh_q4[eye] * abs_cos, 14) + slack_q4;
        const int32_t span_x = fca_max(
            fca_abs(pose->eye_cx_q4[eye] - (safe_lo_x + safe_hi_x) / 2) +
                ex,
            1);
        const int32_t span_y = fca_max(
            fca_abs(pose->eye_cy_q4[eye] - (safe_lo_y + safe_hi_y) / 2) +
                ey,
            1);
        const int32_t allow_x = (safe_hi_x - safe_lo_x) / 2;
        const int32_t allow_y = (safe_hi_y - safe_lo_y) / 2;
        if (span_x > allow_x) {
            need_num = fca_min(need_num, (allow_x * 256) / span_x);
        }
        if (span_y > allow_y) {
            need_num = fca_min(need_num, (allow_y * 256) / span_y);
        }
    }
    if (need_num >= 256) {
        return;
    }
    pose->governor_engaged = 1U;
    const int32_t mid_x = (safe_lo_x + safe_hi_x) / 2;
    const int32_t mid_y = (safe_lo_y + safe_hi_y) / 2;
    for (int eye = 0; eye < 2; ++eye) {
        pose->eye_cx_q4[eye] = mid_x + (int32_t)fca_sar64(
            (int64_t)(pose->eye_cx_q4[eye] - mid_x) * need_num, 8);
        pose->eye_cy_q4[eye] = mid_y + (int32_t)fca_sar64(
            (int64_t)(pose->eye_cy_q4[eye] - mid_y) * need_num, 8);
        pose->eye_hw_q4[eye] = (int32_t)fca_sar64(
            (int64_t)pose->eye_hw_q4[eye] * need_num, 8);
        pose->eye_hh_q4[eye] = (int32_t)fca_sar64(
            (int64_t)pose->eye_hh_q4[eye] * need_num, 8);
        for (int corner = 0; corner < 4; ++corner) {
            pose->corner_q4[eye][corner] = fca_max(
                4,
                (int32_t)fca_sar64(
                    (int64_t)pose->corner_q4[eye][corner] * need_num, 8));
        }
    }
    pose->mouth_cx_q4 = mid_x + (int32_t)fca_sar64(
        (int64_t)(pose->mouth_cx_q4 - mid_x) * need_num, 8);
    pose->mouth_cy_q4 = mid_y + (int32_t)fca_sar64(
        (int64_t)(pose->mouth_cy_q4 - mid_y) * need_num, 8);
    pose->mouth_hw_q4 = (int32_t)fca_sar64(
        (int64_t)pose->mouth_hw_q4 * need_num, 8);
    pose->mouth_open_q4 = (int32_t)fca_sar64(
        (int64_t)pose->mouth_open_q4 * need_num, 8);
}

/* ---- public entry points ---------------------------------------------- */

bool face_cozmo_acting_resolve(
    face_cozmo_acting_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_cozmo_acting_pose_t *pose)
{
    const fca_profile_def_t *def = fca_profile(profile);
    if (def == NULL || render_key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));

    fca_score_t score;
    fca_direct(def, render_key, sample_clock, &score);

    fca_channels_t ch;
    fca_channels_init(def, &score, &ch);

    const uint8_t activity = (uint8_t)(render_key->controls.expression & 3U);
    fca_apply_activity(
        activity, def->direction.drowsy,
        (int32_t)render_key->affect_arousal, &ch);
    fca_apply_emotion(render_key, fca_ms(sample_clock), &ch);
    fca_apply_dense_actions(render_key, &ch);
    fca_apply_speech(def, render_key, &score, &ch);

    fca_project(def, &ch, pose);
    fca_contain(def, pose);

    pose->activity = activity;
    pose->stage_expression =
        render_key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION
            ? render_key->stage_expression
            : (uint8_t)FACE_EXPRESSION_NEUTRAL;
    pose->blink_state = score.blink_state;
    pose->act_id = score.act_id;
    pose->saccade_active = score.saccade_active;
    return true;
}

bool face_cozmo_acting_render_resolved(
    face_cozmo_acting_profile_t profile,
    const face_cozmo_acting_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    const fca_profile_def_t *def = fca_profile(profile);
    if (def == NULL || pose == NULL || rgb565 == NULL ||
        pixel_capacity < (size_t)FACE_COZMO_ACTING_PIXEL_COUNT) {
        return false;
    }
    fca_paint(def, pose, rgb565);
    return true;
}

bool face_cozmo_acting_render(
    face_cozmo_acting_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_cozmo_acting_pose_t pose;
    return face_cozmo_acting_resolve(
               profile, render_key, sample_clock, &pose) &&
           face_cozmo_acting_render_resolved(
               profile, &pose, rgb565, pixel_capacity);
}
