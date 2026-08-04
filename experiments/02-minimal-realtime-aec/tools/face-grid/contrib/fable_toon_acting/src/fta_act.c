/*
 * fable_toon_acting — acting solver.
 *
 * Layered like the robot-eyes family (behavior -> authored emotion ->
 * dense actions -> clamp): a deterministic life layer (blinks, saccades,
 * drift, breathing) hashed from the sample clock, the generic stage action
 * channels, an eleven-way expression accent layer gated on
 * stage_expression and shaped by the acting response curve, JALI-style
 * jaw/lip articulation with viseme accents, speech-phase poses, and a
 * whole-face pose transform with area-conserving squash and stretch.
 * Every output is clamped before the rasterizer sees it.
 */

#include "face_stage.h"
#include "fta_internal.h"

/* ---- expression accents ----------------------------------------------- */

/*
 * Hand-authored per-emotion offsets, in the spirit of the visual-review
 * direction: "emotion table = small integer offsets ... with per-emotion
 * hand authoring — no full-face warp". These add the tells the generic
 * channels cannot separate at 160x120: pupil dilation, gaze aversion,
 * blush, sparkle, posture, breath and blink character.
 */
static const fta_accent_t ACCENTS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 128, 0},
    [FACE_EXPRESSION_WARM] =
        {16, 16, 12, 0, 0, 0, 3, 6, 6, 0, -5, 10, 0, 30, 0, 0, 120, 8},
    [FACE_EXPRESSION_JOY] =
        {22, 46, 14, -8, 0, 0, 7, 14, 20, 8, 10, 14, 0, 90, 0, 0, 140, 40},
    [FACE_EXPRESSION_CONCERN] =
        {12, 0, 26, 10, 0, 8, 5, -12, -10, -6, -8, 0, 0, 0, 12, 0, 90, 0},
    [FACE_EXPRESSION_SURPRISE] =
        {-30, -12, 0, -44, 0, 0, 16, 2, -8, 26, 18, 0, 0, 20, 0, 110, 40, 0},
    [FACE_EXPRESSION_THOUGHTFUL] =
        {10, 8, -6, 0, 30, -34, 5, 3, -8, -5, 0, 0, 0, 0, 0, 0, 110, 0},
    [FACE_EXPRESSION_SKEPTICAL] =
        {14, 10, -12, -10, 38, 0, 0, -8, -6, -8, -3, 0, 0, 0, 0, 0, 118, 0},
    [FACE_EXPRESSION_DETERMINED] =
        {20, 20, -22, -18, 0, 0, -8, -6, 12, -30, 8, 0, 0, 24, 0, 0, 90, 0},
    [FACE_EXPRESSION_SLEEPY] =
        {64, 18, 18, 8, 0, 26, -10, -2, -8, -5, -16, 0, 0, 0, 0, 24, 200, 0},
    [FACE_EXPRESSION_EXCITED] =
        {-20, 18, 8, 16, 0, 0, 12, 10, 14, 10, 16, 12, 0, 170, 0, 0, 170, 70},
    [FACE_EXPRESSION_EMBARRASSED] =
        {24, 16, 16, 6, 20, 24, 3, 5, -12, -5, -8, 32, 26, 12, 10, 0, 140, 0},
};

static const fta_accent_t NEUTRAL_ACCENT = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 128, 0,
};

const fta_accent_t *fta_accent_for(uint8_t stage_expression)
{
    if (stage_expression < FACE_EXPRESSION_COUNT) {
        return &ACCENTS[stage_expression];
    }
    return &NEUTRAL_ACCENT;
}

/* ---- deterministic life ----------------------------------------------- */

enum {
    /*
     * Human blink kinematics: down phase 92 +- 17 ms, ~50 ms closed,
     * reopening 242 +- 55 ms (about 2.6x the close). The asymmetry is
     * what keeps a blink from reading as mechanical.
     */
    BLINK_PRE_SAMPLES = 800,    /* 50 ms anticipation dip */
    BLINK_CLOSE_SAMPLES = 1500, /* 94 ms down phase */
    BLINK_HOLD_SAMPLES = 800,   /* 50 ms closed */
    BLINK_OPEN_SAMPLES = 3900,  /* 244 ms slower reopen */
    BLINK_TOTAL_SAMPLES =
        BLINK_CLOSE_SAMPLES + BLINK_HOLD_SAMPLES + BLINK_OPEN_SAMPLES,
    /* resting adults blink ~17/min; conversation nearly doubles it */
    BLINK_BASE_CADENCE = 52000, /* 3.25 s between blink slots */
    SACCADE_SLOT_SAMPLES = 28000,   /* 1.75 s fixations */
    SACCADE_FLIGHT_SAMPLES = 640,   /* 40 ms flight */
};

/* envelope of a single blink event, 0..256 closedness */
static int32_t blink_event_q8(int32_t t)
{
    if (t < -BLINK_PRE_SAMPLES || t >= BLINK_TOTAL_SAMPLES) {
        return 0;
    }
    if (t < 0) {
        /* pre-blink anticipation: lids dip ~9% before the close */
        return ((t + BLINK_PRE_SAMPLES) * 24) / BLINK_PRE_SAMPLES;
    }
    if (t < BLINK_CLOSE_SAMPLES) {
        const uint8_t q = (uint8_t)((t * 255) / BLINK_CLOSE_SAMPLES);
        return ((int32_t)fta_smooth_u8(q) * 256) / 255;
    }
    if (t < BLINK_CLOSE_SAMPLES + BLINK_HOLD_SAMPLES) {
        return 256;
    }
    const int32_t open_t = t - BLINK_CLOSE_SAMPLES - BLINK_HOLD_SAMPLES;
    const int32_t q = (open_t * 256) / BLINK_OPEN_SAMPLES;
    /* fast-out reopen: lid velocity peaks early, then decelerates */
    return 256 - ((q * (512 - q)) >> 8);
}

/*
 * Scheduled closedness 0..256 at this clock. Cadence stretches with the
 * expression accent (a surprised face stares, a sleepy face barely
 * reopens); ~15% of slots blink twice (human double-blink habit).
 */
static int32_t blink_closed_q8(
    uint32_t sample_clock, uint32_t salt, uint32_t cadence)
{
    if (cadence < BLINK_TOTAL_SAMPLES + BLINK_PRE_SAMPLES + 8000U) {
        cadence = BLINK_TOTAL_SAMPLES + BLINK_PRE_SAMPLES + 8000U;
    }
    int32_t closed = 0;
    /* the previous slot's blink may spill into this one; check both */
    for (uint32_t back = 0U; back < 2U; ++back) {
        const uint32_t slot = sample_clock / cadence - back;
        if (back > sample_clock / cadence) {
            break;
        }
        const uint32_t latest_start =
            cadence - (uint32_t)BLINK_TOTAL_SAMPLES - 6400U;
        const uint32_t start =
            (uint32_t)BLINK_PRE_SAMPLES +
            ((uint32_t)fta_noise_u8(slot, salt, 0U) * latest_start) / 255U;
        const int64_t t64 =
            (int64_t)sample_clock - (int64_t)slot * cadence - (int64_t)start;
        const int32_t t = (int32_t)fta_clamp_i32(
            (int32_t)fta_max_i32((int32_t)fta_min_i32((int32_t)t64, 1 << 30),
                                 -(1 << 30)),
            -(1 << 30), 1 << 30);
        closed = fta_max_i32(closed, blink_event_q8(t));
        if (fta_noise_u8(slot, salt, 2U) < 38U) {
            closed = fta_max_i32(closed, blink_event_q8(t - 6400));
        }
    }
    return closed;
}

/* fixation offset for a saccade slot, Q4 pixels, roughly +-1.6 px */
static void fixation_offset(
    uint32_t slot, uint32_t salt, int32_t *x_q4, int32_t *y_q4)
{
    *x_q4 = ((int32_t)fta_noise_u8(slot, salt, 4U) - 128) * 26 / 128;
    *y_q4 = ((int32_t)fta_noise_u8(slot, salt, 5U) - 128) * 18 / 128;
}

/*
 * Conjugate saccade/drift layer: held fixations punctuated by brief
 * flights with a slight overshoot, plus slow smooth-pursuit drift.
 */
static void saccade_offsets(
    uint32_t sample_clock, uint32_t salt, int32_t wander_gain_q8,
    int32_t *x_q4, int32_t *y_q4)
{
    const uint32_t slot = sample_clock / SACCADE_SLOT_SAMPLES;
    const uint32_t t_in = sample_clock % SACCADE_SLOT_SAMPLES;
    int32_t current_x;
    int32_t current_y;
    fixation_offset(slot, salt, &current_x, &current_y);
    if (t_in < SACCADE_FLIGHT_SAMPLES && slot > 0U) {
        int32_t previous_x;
        int32_t previous_y;
        fixation_offset(slot - 1U, salt, &previous_x, &previous_y);
        /*
         * Real saccades are ballistic: brief acceleration, hard landing,
         * no overshoot (an overshooting gaze reads as a nervous tic).
         */
        const int32_t q = (int32_t)(
            (t_in * 255U) / (uint32_t)SACCADE_FLIGHT_SAMPLES);
        const int32_t progress =
            ((int32_t)fta_smooth_u8((uint8_t)fta_clamp_i32(q, 0, 255)) *
             256) /
            255;
        current_x = previous_x + ((current_x - previous_x) * progress) / 256;
        current_y = previous_y + ((current_y - previous_y) * progress) / 256;
    }
    /* slow conjugate drift, two incommensurate lanes */
    const int32_t drift_x =
        fta_wave_q8(sample_clock / 3U + (salt & 0xffffU)) * 6 / 256;
    const int32_t drift_y =
        fta_wave_q8(sample_clock / 5U + ((salt >> 8) & 0xffffU)) * 5 / 256;
    *x_q4 = ((current_x + drift_x) * wander_gain_q8) / 256;
    *y_q4 = ((current_y + drift_y) * wander_gain_q8) / 256;
}

/* ---- viseme lip accents ----------------------------------------------- */

/*
 * Micro-adjustments layered over the animator-authored mouth channels.
 * The controls prefix stays authoritative (the animator already smooths
 * the viseme shape table into it); these add the identity details a
 * geometric mouth can voice: funnel rounding for O/U, teeth emphasis for
 * the sibilants, tongue for TH/NN/RR, press for the bilabials.
 */
typedef struct {
    int8_t round_add_q8;
    int8_t press_add_q8;
    int8_t teeth_add_q8;
    int8_t tongue_add_q8;
    int8_t width_bias_q8;
} viseme_accent_t;

static const viseme_accent_t VISEME_ACCENTS[15] = {
    [0] = {0, 0, 0, 8, 16},      /* AA: tall open */
    [1] = {-10, 0, 24, 0, 24},   /* E: spread, teeth line */
    [2] = {-16, 0, 32, 0, 32},   /* I: widest spread */
    [3] = {70, 0, -16, 0, -36},  /* O: funnel */
    [4] = {88, 8, -24, 0, -52},  /* U: tight funnel */
    [5] = {0, 64, -24, 0, -8},   /* PP: bilabial press */
    [6] = {-12, 0, 48, 0, 20},   /* SS: teeth showcase */
    [7] = {-6, 0, 40, 28, 8},    /* TH: tongue at teeth */
    [8] = {-6, 0, 28, 16, 8},    /* DD: alveolar tap */
    [9] = {-4, 40, 44, 0, 4},    /* FF: lip under teeth */
    [10] = {6, 0, 8, 0, -4},     /* KK: back closure */
    [11] = {-4, 0, 20, 20, 4},   /* NN: tongue visible */
    [12] = {24, 0, 8, 18, -12},  /* RR: slight funnel */
    [13] = {10, 10, 30, 0, -6},  /* CH: pursed sibilant */
    [14] = {4, 0, -8, 0, -6},    /* SIL: relaxed */
};

static void viseme_accent_lookup(
    const face_render_key_t *key, viseme_accent_t *out)
{
    out->round_add_q8 = 0;
    out->press_add_q8 = 0;
    out->teeth_add_q8 = 0;
    out->tongue_add_q8 = 0;
    out->width_bias_q8 = 0;
    if (key->viseme_set != FACE_VISEME_SET_OVR15 || key->viseme >= 15U) {
        return;
    }
    const viseme_accent_t *primary = &VISEME_ACCENTS[key->viseme];
    const viseme_accent_t *secondary = primary;
    int32_t blend = 0;
    if (key->viseme_secondary < 15U) {
        secondary = &VISEME_ACCENTS[key->viseme_secondary];
        blend = key->viseme_blend;
    }
    const int32_t weight = key->viseme_weight;
#define FTA_MIX_ACCENT(field) \
    (int8_t)((((int32_t)primary->field + \
               (((int32_t)secondary->field - primary->field) * blend) / \
                   255) * \
              weight) / \
             255)
    out->round_add_q8 = FTA_MIX_ACCENT(round_add_q8);
    out->press_add_q8 = FTA_MIX_ACCENT(press_add_q8);
    out->teeth_add_q8 = FTA_MIX_ACCENT(teeth_add_q8);
    out->tongue_add_q8 = FTA_MIX_ACCENT(tongue_add_q8);
    out->width_bias_q8 = FTA_MIX_ACCENT(width_bias_q8);
#undef FTA_MIX_ACCENT
}

/* ---- solver ----------------------------------------------------------- */

typedef struct {
    int32_t origin_x_q4;
    int32_t origin_y_q4;
    int32_t scale_x_q8;
    int32_t scale_y_q8;
    int32_t shear_q12;
} pose_t;

static int32_t scale_x(const pose_t *pose, int32_t offset_q4)
{
    return (offset_q4 * pose->scale_x_q8) / 256;
}

static int32_t scale_y(const pose_t *pose, int32_t offset_q4)
{
    return (offset_q4 * pose->scale_y_q8) / 256;
}

/* place a rig-space offset (from face center) into screen space */
static void place(
    const pose_t *pose, int32_t dx_q4, int32_t dy_q4,
    int32_t *x_q4, int32_t *y_q4)
{
    const int32_t sy = scale_y(pose, dy_q4);
    *y_q4 = pose->origin_y_q4 + sy;
    *x_q4 = pose->origin_x_q4 + scale_x(pose, dx_q4) +
            ((pose->shear_q12 * sy) >> 12);
}

void fta_solve_rig(
    const fta_style_t *style,
    const face_render_key_t *key,
    uint32_t sample_clock,
    fta_rig_t *rig)
{
    const face_keyframe_t *controls = &key->controls;
    const fta_accent_t *accent = fta_accent_for(key->stage_expression);
    const int32_t acting = fta_acting_q8(key->expression_weight);
    const int32_t acting_mag = fta_acting_mag_q8(key->expression_weight);
    const int32_t accent_gain = style->accent_gain_q8;
    /* signed accent scale (anticipation dip allowed) */
    const int32_t act = (acting * accent_gain) / 256;
    /* magnitude accent scale (alphas, sparkle: never negative) */
    const int32_t mag = (acting_mag * accent_gain) / 256;

    const uint8_t activity =
        controls->expression <= FACE_ACTIVITY_SPEAKING
            ? controls->expression
            : (uint8_t)FACE_ACTIVITY_IDLE;
    const bool speaking =
        (controls->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        key->speech_phase == FACE_SPEECH_ACTIVE;
    /* strong authored emotion fades the activity poses */
    const int32_t activity_gain =
        256 - ((int32_t)key->expression_weight * 160) / 255;

    /* ---- breathing ---- */
    /*
     * Resting adults breathe 12-20/min; arousal speeds it up. A real
     * breath is asymmetric (inhale ~40% of the cycle, exhale ~60%), so
     * the phase is warped before the wave lookup. Surprise holds the
     * breath entirely (accent breath gain ~0).
     */
    const int32_t arousal = key->affect_arousal;
    const int32_t bpm_q8 = (9 * 256) + (arousal * 14 * 256) / 255;
    const uint32_t breath_period = (uint32_t)fta_clamp_i32(
        (int32_t)((16000LL * 60 * 256) / fta_max_i32(bpm_q8, 1)),
        30000, 110000);
    uint32_t breath_phase =
        (uint32_t)(((uint64_t)sample_clock << 16) / breath_period);
    const uint32_t breath_turn = breath_phase & 0xffffU;
    if (breath_turn < 26214U) {
        breath_phase = (breath_phase & ~0xffffU) +
                       (breath_turn * 32768U) / 26214U;
    } else {
        breath_phase = (breath_phase & ~0xffffU) + 32768U +
                       ((breath_turn - 26214U) * 32768U) / 39322U;
    }
    const int32_t breath_gain = accent->breath_q8;
    const int32_t breath_amp_q4 = (19 * breath_gain) / 128;
    const int32_t breath_wave = fta_wave_q8(breath_phase);
    const int32_t plate_breath = (breath_wave * breath_amp_q4) / 256;
    const int32_t feature_breath =
        (fta_wave_q8(breath_phase - 5461U) * breath_amp_q4 * 3) / (256 * 4);
    /* breath also lives in scale: +-1.5% of face height at full gain */
    const int32_t breath_stretch_q8 =
        (breath_wave * ((4 * breath_gain) / 128)) / 256;

    /* ---- idle bounce (joyful/excited energy) ---- */
    const uint32_t bounce_phase =
        (uint32_t)(((uint64_t)sample_clock << 16) / 7273U); /* ~2.2 Hz */
    const int32_t bounce_q4 =
        (fta_wave_q8(bounce_phase) * ((int32_t)accent->bob_q8 * mag) / 256) /
        (256 * 10);

    /* ---- speech energy ---- */
    const int32_t audio = key->audio_level;
    int32_t speech_bob_q4 = 0;
    int32_t speech_brow_q4 = 0;
    if (speaking) {
        const uint32_t bob_phase =
            (uint32_t)(((uint64_t)sample_clock << 16) / 5000U); /* 3.2 Hz */
        speech_bob_q4 = (fta_wave_q8(bob_phase) * (audio * 12 / 255)) / 256;
        if (audio > 150) {
            speech_brow_q4 = ((audio - 150) * 20) / 105;
        }
    }

    /* ---- speech phase poses ---- */
    int32_t phase_lid_q8 = 0;      /* negative widens */
    int32_t phase_brow_q4 = 0;
    int32_t phase_jaw_q8 = 0;
    int32_t phase_press_q8 = 0;
    int32_t phase_stretch_q8 = 0;
    int32_t phase_corner_gain_q8 = 256;
    if (key->speech_phase == FACE_SPEECH_STARTING) {
        /* inhale: brows and lids up, lips press before the jaw opens */
        phase_lid_q8 = -20;
        phase_brow_q4 = 24;
        phase_jaw_q8 = 10;
        phase_press_q8 = 38;
        phase_stretch_q8 = 4;
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        /* exhale and settle: everything eases toward rest */
        phase_lid_q8 = 12;
        phase_stretch_q8 = -3;
        phase_jaw_q8 = -20;
        phase_corner_gain_q8 = 230;
    }

    /* ---- activity poses (fade under strong stage emotion) ---- */
    int32_t activity_lid_q8 = 0;
    int32_t activity_brow_q4 = 0;
    int32_t activity_gaze_x_q4 = 0;
    int32_t activity_gaze_y_q4 = 0;
    int32_t activity_press_q8 = 0;
    if (activity == FACE_ACTIVITY_LISTENING) {
        activity_lid_q8 = -10;
        activity_brow_q4 = 12;
        activity_gaze_y_q4 = -8;
    } else if (activity == FACE_ACTIVITY_THINKING) {
        const int32_t wander =
            fta_wave_q8(sample_clock / 7U + (style->salt & 0x7fffU));
        activity_gaze_x_q4 = 20 + (wander * 8) / 256;
        activity_gaze_y_q4 = -24 + (wander * 4) / 256;
        activity_brow_q4 = 6;
        activity_press_q8 = 24;
    }
    activity_lid_q8 = (activity_lid_q8 * activity_gain) / 256;
    activity_brow_q4 = (activity_brow_q4 * activity_gain) / 256;
    activity_gaze_x_q4 = (activity_gaze_x_q4 * activity_gain) / 256;
    activity_gaze_y_q4 = (activity_gaze_y_q4 * activity_gain) / 256;
    activity_press_q8 = (activity_press_q8 * activity_gain) / 256;

    /* ---- whole-face pose ---- */
    const int32_t motion = style->motion_gain_q8;
    pose_t pose;
    int32_t stretch_q8 =
        ((int32_t)accent->stretch_q8 * act) / 256 + phase_stretch_q8 +
        ((int32_t)key->body_lean_y) / 6;
    /* subtle jaw-driven stretch: a dropped jaw pulls the face longer */
    stretch_q8 += ((int32_t)controls->mouth_open * 6) / 255;
    stretch_q8 = fta_clamp_i32((stretch_q8 * motion) / 256, -30, 30);
    stretch_q8 += breath_stretch_q8;
    pose.scale_y_q8 = 256 + stretch_q8;
    /* exact 2D area conservation: scale_x = 1 / scale_y */
    pose.scale_x_q8 = (65536 + pose.scale_y_q8 / 2) / pose.scale_y_q8;
    pose.shear_q12 = fta_clamp_i32(
        ((int32_t)key->head_roll * 9 +
         (int32_t)key->body_lean_x * 3) *
            motion / 256,
        -900, 900);
    pose.origin_x_q4 = (FTA_FRAME_WIDTH / 2) * FTA_Q4 +
                       (((int32_t)key->head_yaw * 72) / 127 +
                        ((int32_t)key->body_lean_x * 32) / 127) *
                           motion / 256;
    pose.origin_y_q4 =
        58 * FTA_Q4 +
        (((int32_t)key->head_pitch * 56) / 127 -
         ((int32_t)key->body_lean_y * 12) / 127) *
            motion / 256 +
        plate_breath + bounce_q4 + speech_bob_q4;
    /* stretch pivots at the plate bottom so the face stays planted */
    pose.origin_y_q4 -= (style->plate_half_h_q4 * stretch_q8) / 256;

    rig->origin_x_q4 = (int16_t)pose.origin_x_q4;
    rig->origin_y_q4 = (int16_t)pose.origin_y_q4;
    rig->scale_x_q8 = (int16_t)pose.scale_x_q8;
    rig->scale_y_q8 = (int16_t)pose.scale_y_q8;
    rig->shear_q12 = (int16_t)pose.shear_q12;

    /* ---- plate ---- */
    const int32_t plate_half_w = scale_x(&pose, style->plate_half_w_q4);
    const int32_t plate_half_h = scale_y(&pose, style->plate_half_h_q4);
    /* the plate trails the features slightly for 2.5D parallax */
    const int32_t plate_dx =
        -(((int32_t)key->head_yaw * 18) / 127) * motion / 256;
    rig->plate_left_q4 =
        (int16_t)(pose.origin_x_q4 + plate_dx - plate_half_w);
    rig->plate_right_q4 =
        (int16_t)(pose.origin_x_q4 + plate_dx + plate_half_w);
    rig->plate_top_q4 = (int16_t)(pose.origin_y_q4 - plate_half_h);
    rig->plate_bottom_q4 = (int16_t)(pose.origin_y_q4 + plate_half_h);
    rig->plate_radius_q4 = (int16_t)fta_min_i32(
        style->plate_radius_q4, fta_min_i32(plate_half_w, plate_half_h));

    /* ---- embarrassed display sequence (after Keltner 1995) ---- */
    /*
     * Embarrassment is a timed display, not a static pose: gaze drops,
     * then the smile is fought down (lip press, corners retreat), then
     * it re-emerges. A 5 s deterministic cycle with two smoothed
     * "smile control" beats reproduces the read without state.
     */
    int32_t emb_press_q8 = 0;
    int32_t emb_corner_gain_q8 = 256;
    int32_t emb_gaze_y_q4 = 0;
    if (key->stage_expression == FACE_EXPRESSION_EMBARRASSED && mag > 64) {
        const uint32_t cycle = sample_clock % 80000U;
        if (cycle > 9600U) {
            emb_gaze_y_q4 = (14 * mag) / 256;
        }
        static const uint32_t beat_start[2] = {25600U, 60800U};
        static const uint32_t beat_end[2] = {38400U, 70400U};
        int32_t beat_q8 = 0;
        for (uint32_t beat = 0U; beat < 2U; ++beat) {
            if (cycle > beat_start[beat] && cycle < beat_end[beat]) {
                const int32_t rise = (int32_t)fta_min_i32(
                    (int32_t)((cycle - beat_start[beat]) * 256U / 3200U),
                    256);
                const int32_t fall = (int32_t)fta_min_i32(
                    (int32_t)((beat_end[beat] - cycle) * 256U / 3200U),
                    256);
                beat_q8 =
                    fta_max_i32(beat_q8, fta_min_i32(rise, fall));
            }
        }
        emb_press_q8 = (((56 * beat_q8) / 256) * mag) / 256;
        emb_corner_gain_q8 = 256 - (((102 * beat_q8) / 256) * mag) / 256;
    }

    /* ---- gaze ---- */
    int32_t wander_gain_q8 = 256;
    if (key->attention > 200U) {
        wander_gain_q8 = 128;
    } else if (key->attention < 100U) {
        wander_gain_q8 = 384;
    }
    int32_t wander_x_q4;
    int32_t wander_y_q4;
    saccade_offsets(
        sample_clock, style->salt, wander_gain_q8,
        &wander_x_q4, &wander_y_q4);
    const int32_t gaze_x_q8 =
        ((int32_t)controls->look_x * 256) / 127;
    const int32_t gaze_y_q8 =
        ((int32_t)controls->look_y * 256) / 127;
    const int32_t accent_gaze_x =
        ((int32_t)accent->gaze_x_q4 * act * 3) / 256;
    const int32_t accent_gaze_y =
        ((int32_t)accent->gaze_y_q4 * act * 3) / 256;
    const int32_t yaw_parallax_q4 = ((int32_t)key->head_yaw * 26) / 127;

    /* ---- blink ---- */
    /*
     * The cadence is deliberately constant per style: the quality atlas
     * renders all eleven expressions at one frozen clock, and a cadence
     * that varied per expression would catch some columns mid-blink,
     * collapsing them into accidental near-duplicates of sleepy.
     */
    const uint32_t cadence = BLINK_BASE_CADENCE;
    int32_t blink = blink_closed_q8(sample_clock, style->salt, cadence);
    if ((controls->flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
        blink = fta_max_i32(blink, 218);
    }

    /* ---- eyes ---- */
    const int32_t eye_open[2] = {
        controls->eye_left_open, controls->eye_right_open,
    };
    const int32_t eye_squint[2] = {
        key->eye_left_squint, key->eye_right_squint,
    };
    /* surprise/excited widen the eye itself, not just the lids */
    const int32_t widen_q8 =
        accent->lid_top_q8 < 0
            ? (-(int32_t)accent->lid_top_q8 * mag) / 256
            : 0;
    for (int32_t side = 0; side < 2; ++side) {
        fta_eye_t *eye = &rig->eye[side];
        const int32_t mirror = side == 0 ? -1 : 1;
        int32_t ex;
        int32_t ey;
        place(
            &pose,
            mirror * style->eye_offset_x_q4 +
                (gaze_x_q8 * 10) / 256, /* eyes trail gaze slightly */
            style->eye_offset_y_q4 + (gaze_y_q8 * 7) / 256 +
                (feature_breath - plate_breath),
            &ex, &ey);
        int32_t half_w = scale_x(&pose, style->eye_half_w_q4);
        const int32_t half_h =
            scale_y(&pose, style->eye_half_h_q4) * (256 + widen_q8) / 256;
        eye->center_x_q4 = (int16_t)ex;
        eye->center_y_q4 = (int16_t)ey;

        /* lid closedness: u = upper travel, v = lower raise, Q8 of eye */
        const int32_t closing = 256 - (eye_open[side] * 256) / 255;
        int32_t u = (closing * 179) / 256;
        int32_t v = (closing * 77) / 256;
        int32_t extra_u =
            (eye_squint[side] * 40) / 255 +
            fta_max_i32(((int32_t)accent->lid_top_q8 * act * 3) / 512, 0) +
            phase_lid_q8 + activity_lid_q8;
        int32_t extra_v =
            (eye_squint[side] * 96) / 255 +
            ((int32_t)accent->lid_bottom_q8 * act * 3) / 512;
        /*
         * Bilateral-visibility floor: expression squints may narrow an
         * eye but never erase it. Only the animator's eye_open channel
         * or a blink may take the aperture below ~9%.
         */
        const int32_t budget = 232 - u - v;
        const int32_t positive_extra =
            fta_max_i32(extra_u, 0) + fta_max_i32(extra_v, 0);
        if (budget <= 0) {
            extra_u = fta_min_i32(extra_u, 0);
            extra_v = fta_min_i32(extra_v, 0);
        } else if (positive_extra > budget) {
            if (extra_u > 0) {
                extra_u = (extra_u * budget) / positive_extra;
            }
            if (extra_v > 0) {
                extra_v = (extra_v * budget) / positive_extra;
            }
        }
        u = fta_clamp_i32(u + extra_u, 0, 256);
        v = fta_clamp_i32(v + extra_v, 0, 200);
        /* blink drives both lids toward a low meet line (65% down) */
        u += ((166 - u) * blink) / 256;
        v += ((90 - v) * blink) / 256;
        /* blink squash: the eye box widens as the lids slam shut */
        const int32_t blink_squash =
            blink > 200 ? ((blink - 200) * 23) / 56 : 0;
        half_w = (half_w * (256 + blink_squash)) / 256;
        if (u + v > 256) {
            /* keep the aperture from inverting; upper lid wins */
            v = 256 - u;
            if (v < 0) {
                v = 0;
                u = 256;
            }
        }
        eye->half_w_q4 = (int16_t)half_w;
        eye->half_h_q4 = (int16_t)half_h;
        const int32_t span = half_h * 2;
        eye->lid_top_q4 = (int16_t)(ey - half_h + (span * u) / 256);
        eye->lid_bottom_q4 = (int16_t)(ey + half_h - (span * v) / 256);
        eye->openness_q8 = (uint8_t)fta_clamp_i32(256 - u - v, 0, 255);

        /* lid tilt: accent tilt mirrored per eye + shared roll shear */
        const int32_t tilt =
            ((int32_t)accent->lid_tilt_q8 * act) / 256;
        const int32_t roll_slope = pose.shear_q12 / 6;
        eye->lid_top_slope_q12 =
            (int16_t)fta_clamp_i32(
                mirror * tilt * 5 + roll_slope, -1024, 1024);
        eye->lid_bottom_slope_q12 =
            (int16_t)fta_clamp_i32(
                mirror * tilt * 2 + roll_slope, -1024, 1024);

        /* pupil + iris */
        int32_t iris_r = scale_y(&pose, style->iris_r_q4);
        int32_t pupil_r = scale_y(&pose, style->pupil_r_q4);
        const int32_t pupil_bias_q8 =
            ((int32_t)accent->pupil_scale_q8 * act * 3) / 256 +
            ((arousal - 128) * 32) / 255;
        pupil_r = (pupil_r * (256 + pupil_bias_q8)) / 256;
        iris_r = (iris_r * (256 + pupil_bias_q8 / 2)) / 256;
        pupil_r = fta_max_i32(pupil_r, 2 * FTA_Q4 / 2);
        iris_r = fta_max_i32(iris_r, pupil_r + FTA_Q4 / 2);

        const int32_t travel_x =
            ((half_w - (iris_r * 3) / 4) * style->gaze_travel_q8) / 256;
        const int32_t travel_y =
            ((half_h - (iris_r * 2) / 4) * style->gaze_travel_q8) / 256;
        int32_t px = ex + (gaze_x_q8 * travel_x) / 256 + wander_x_q4 +
                     accent_gaze_x + activity_gaze_x_q4 + yaw_parallax_q4;
        int32_t py = ey + (gaze_y_q8 * travel_y) / 256 + wander_y_q4 +
                     accent_gaze_y + activity_gaze_y_q4 + emb_gaze_y_q4;
        /* hard pupil clamp: the iris may tuck 25% under an edge, never
         * escape; review rule "pupil with hard clamp inside lid" */
        const int32_t max_dx = fta_max_i32(half_w - (iris_r * 3) / 4, 0);
        px = fta_clamp_i32(px, ex - max_dx, ex + max_dx);
        const int32_t top_limit = eye->lid_top_q4 + (iris_r * 1) / 2;
        const int32_t bottom_limit = eye->lid_bottom_q4 - (iris_r * 1) / 2;
        if (top_limit <= bottom_limit) {
            py = fta_clamp_i32(py, top_limit, bottom_limit);
        } else {
            py = (eye->lid_top_q4 + eye->lid_bottom_q4) / 2;
        }
        eye->pupil_x_q4 = (int16_t)px;
        eye->pupil_y_q4 = (int16_t)py;
        eye->pupil_r_q4 = (int16_t)pupil_r;
        eye->iris_r_q4 = (int16_t)iris_r;
        eye->sparkle = (uint8_t)fta_clamp_i32(
            ((int32_t)accent->sparkle * mag) / 256, 0, 255);
    }

    /* ---- brows ---- */
    /* brows get deliberately high gain: at 120 px the brow angle is the
     * single strongest emotion carrier */
    const int32_t brow_lift_common =
        ((int32_t)controls->brow * 96) / 127 +
        ((int32_t)accent->brow_lift_q4 * act) / 64 +
        phase_brow_q4 + activity_brow_q4 + speech_brow_q4;
    const int32_t inner_lift = ((int32_t)key->brow_inner * 150) / 127;
    const int32_t outer_lift_left =
        ((int32_t)key->brow_outer_left * 130) / 127;
    const int32_t outer_lift_right =
        ((int32_t)key->brow_outer_right * 130) / 127;
    for (int32_t side = 0; side < 2; ++side) {
        fta_brow_t *brow = &rig->brow[side];
        const int32_t mirror = side == 0 ? -1 : 1;
        const int32_t outer_lift =
            side == 0 ? outer_lift_left : outer_lift_right;
        const int32_t rest_dy = style->eye_offset_y_q4 -
                                style->eye_half_h_q4 - style->brow_gap_q4;
        int32_t ix;
        int32_t iy;
        int32_t ox;
        int32_t oy;
        place(
            &pose,
            mirror * (style->eye_offset_x_q4 - style->brow_half_w_q4 + 8),
            rest_dy, &ix, &iy);
        place(
            &pose,
            mirror * (style->eye_offset_x_q4 + style->brow_half_w_q4),
            rest_dy, &ox, &oy);
        /* lifts are screen-space Q4; raising means smaller y */
        iy -= inner_lift + brow_lift_common;
        oy -= outer_lift + brow_lift_common;
        brow->inner_x_q4 = (int16_t)ix;
        brow->inner_y_q4 = (int16_t)iy;
        brow->outer_x_q4 = (int16_t)ox;
        brow->outer_y_q4 = (int16_t)oy;
        brow->thickness_q4 = (int16_t)style->brow_thickness_q4;
    }

    /* ---- mouth ---- */
    viseme_accent_t viseme;
    viseme_accent_lookup(key, &viseme);
    fta_mouth_t *mouth = &rig->mouth;
    int32_t mx;
    int32_t my;
    place(
        &pose, 0,
        style->mouth_offset_y_q4 + (feature_breath - plate_breath) / 2,
        &mx, &my);
    const int32_t round_q8 = fta_clamp_i32(
        ((int32_t)controls->mouth_round * 256) / 255 +
            ((int32_t)viseme.round_add_q8 * 2) +
            ((int32_t)accent->mouth_round_q8 * act) / 256,
        0, 256);
    const int32_t press_q8 = fta_clamp_i32(
        ((int32_t)controls->mouth_press * 256) / 255 +
            ((int32_t)viseme.press_add_q8 * 2) + activity_press_q8 +
            phase_press_q8 + emb_press_q8,
        0, 256);
    int32_t half_w =
        (scale_x(&pose, style->mouth_half_w_q4) *
         (102 + ((int32_t)controls->mouth_width * 154) / 255)) /
        256;
    half_w = (half_w * (256 + ((int32_t)accent->mouth_width_q8 * act) / 128 +
                        (int32_t)viseme.width_bias_q8)) /
             256;
    half_w = (half_w * (256 - (round_q8 * 118) / 256)) / 256;
    int32_t open_q4 =
        (scale_y(&pose, style->mouth_max_open_q4) *
         (((int32_t)controls->mouth_open * 256) / 255 +
          ((int32_t)accent->jaw_q8 * act * 5) / 512 + phase_jaw_q8)) /
        256;
    open_q4 = (open_q4 * (256 - (press_q8 * 230) / 256)) / 256;
    open_q4 = fta_clamp_i32(
        open_q4, 0, scale_y(&pose, style->mouth_max_open_q4));
    /* rounding ovals the aperture upward as well */
    const int32_t corner_gain =
        ((phase_corner_gain_q8 * (256 - (round_q8 * 140) / 256)) / 256) *
        emb_corner_gain_q8 / 256;
    const int32_t corner_left =
        -(((int32_t)key->mouth_corner_left * 120) / 127) * corner_gain /
        256;
    const int32_t corner_right =
        -(((int32_t)key->mouth_corner_right * 120) / 127) * corner_gain /
        256;
    const int32_t valence_curve =
        ((int32_t)key->affect_valence * 40) / 127;
    const int32_t accent_curve =
        ((int32_t)accent->mouth_curve_q4 * act) / 64;
    int32_t curve_q4 = valence_curve + accent_curve;
    curve_q4 = (curve_q4 * (256 - (press_q8 * 180) / 256)) / 256;
    curve_q4 = (curve_q4 * (256 - (round_q8 * 200) / 256)) / 256;

    mouth->center_x_q4 = (int16_t)mx;
    mouth->center_y_q4 = (int16_t)my;
    mouth->half_w_q4 = (int16_t)fta_max_i32(half_w, 6 * FTA_Q4 / 2);
    mouth->open_q4 = (int16_t)open_q4;
    mouth->corner_left_q4 = (int16_t)corner_left;
    mouth->corner_right_q4 = (int16_t)corner_right;
    mouth->curve_q4 = (int16_t)fta_clamp_i32(curve_q4, -88, 88);
    mouth->round_q8 = (uint8_t)round_q8;
    mouth->press_q8 = (uint8_t)press_q8;
    mouth->lip_q4 = (int16_t)(style->lip_thickness_q4 +
                              (round_q8 * 8) / 256 + (press_q8 * 6) / 256);
    const int32_t teeth_room = (open_q4 * 230) / 256;
    mouth->teeth_q4 = (int16_t)fta_clamp_i32(
        (((int32_t)controls->mouth_teeth * teeth_room) / 255) +
            ((int32_t)viseme.teeth_add_q8 * teeth_room) / 256,
        0, teeth_room);
    const int32_t tongue_room = (open_q4 * 179) / 256;
    mouth->tongue_q4 = (int16_t)fta_clamp_i32(
        (((int32_t)key->tongue * tongue_room) / 255) +
            ((int32_t)viseme.tongue_add_q8 * tongue_room) / 128,
        0, tongue_room);

    /* ---- blush, band, sweat ---- */
    const int32_t cheek_alpha = ((int32_t)key->cheek * 26) / 255;
    const int32_t accent_blush = ((int32_t)accent->blush * mag) / 256;
    for (int32_t side = 0; side < 2; ++side) {
        fta_blush_t *blush = &rig->blush[side];
        const int32_t mirror = side == 0 ? -1 : 1;
        int32_t bx;
        int32_t by;
        place(
            &pose, mirror * style->blush_offset_x_q4,
            style->blush_offset_y_q4, &bx, &by);
        blush->center_x_q4 = (int16_t)bx;
        blush->center_y_q4 = (int16_t)by;
        blush->half_w_q4 = (int16_t)scale_x(&pose, style->blush_half_w_q4);
        blush->half_h_q4 = (int16_t)scale_y(&pose, style->blush_half_h_q4);
        blush->alpha =
            (uint8_t)fta_clamp_i32(cheek_alpha + accent_blush, 0, 32);
    }
    rig->blush_band_alpha = (uint8_t)fta_clamp_i32(
        ((int32_t)accent->blush_band * mag) / 256, 0, 32);
    /* the band always sits below both eye masses, never through them */
    rig->blush_band_y_q4 = (int16_t)fta_max_i32(
        fta_max_i32(rig->eye[0].lid_bottom_q4, rig->eye[1].lid_bottom_q4) +
            3 * FTA_Q4,
        (rig->blush[0].center_y_q4 + rig->blush[1].center_y_q4) / 2);
    rig->sweat_alpha = (uint8_t)fta_clamp_i32(
        ((int32_t)accent->sweat * mag) / 256, 0, 32);
    /* the droplet slides down slowly and respawns */
    rig->sweat_y_q4 =
        (int16_t)((sample_clock / 640U) % 160U);

    rig->stage_expression = key->stage_expression;
    rig->expression_weight = key->expression_weight;

    /* ---- final safety clamps (hard no-clip guarantee) ---- */
    /* the roll shear slides plate rows sideways by up to
     * |shear| * half_h; reserve that reach so no sheared row can leave
     * the safe area */
    const int32_t shear_reach =
        (fta_abs_i32(pose.shear_q12) *
         ((rig->plate_bottom_q4 - rig->plate_top_q4) / 2)) >> 12;
    const int32_t safe_left = 4 * FTA_Q4 + shear_reach;
    const int32_t safe_right = (FTA_FRAME_WIDTH - 4) * FTA_Q4 - shear_reach;
    const int32_t safe_top = 4 * FTA_Q4;
    const int32_t safe_bottom = (FTA_FRAME_HEIGHT - 4) * FTA_Q4;
    rig->plate_left_q4 = (int16_t)fta_max_i32(rig->plate_left_q4, safe_left);
    rig->plate_right_q4 =
        (int16_t)fta_min_i32(rig->plate_right_q4, safe_right);
    rig->plate_top_q4 = (int16_t)fta_max_i32(rig->plate_top_q4, safe_top);
    rig->plate_bottom_q4 =
        (int16_t)fta_min_i32(rig->plate_bottom_q4, safe_bottom);
    for (int32_t side = 0; side < 2; ++side) {
        fta_eye_t *eye = &rig->eye[side];
        const int32_t margin = 2 * FTA_Q4;
        eye->center_x_q4 = (int16_t)fta_clamp_i32(
            eye->center_x_q4,
            rig->plate_left_q4 + eye->half_w_q4 + margin,
            rig->plate_right_q4 - eye->half_w_q4 - margin);
        eye->center_y_q4 = (int16_t)fta_clamp_i32(
            eye->center_y_q4,
            rig->plate_top_q4 + eye->half_h_q4 + margin,
            rig->plate_bottom_q4 - eye->half_h_q4 - margin);
        fta_brow_t *brow = &rig->brow[side];
        brow->inner_y_q4 = (int16_t)fta_max_i32(
            brow->inner_y_q4, rig->plate_top_q4 + 2 * FTA_Q4);
        brow->outer_y_q4 = (int16_t)fta_max_i32(
            brow->outer_y_q4, rig->plate_top_q4 + 2 * FTA_Q4);
        brow->inner_x_q4 = (int16_t)fta_clamp_i32(
            brow->inner_x_q4, rig->plate_left_q4 + FTA_Q4,
            rig->plate_right_q4 - FTA_Q4);
        brow->outer_x_q4 = (int16_t)fta_clamp_i32(
            brow->outer_x_q4, rig->plate_left_q4 + FTA_Q4,
            rig->plate_right_q4 - FTA_Q4);
    }
    fta_mouth_t *m = &rig->mouth;
    const int32_t mouth_reach =
        m->open_q4 * 2 + m->lip_q4 + fta_abs_i32(m->curve_q4) +
        fta_max_i32(
            fta_abs_i32(m->corner_left_q4), fta_abs_i32(m->corner_right_q4));
    if (m->center_y_q4 + mouth_reach >
        rig->plate_bottom_q4 - 3 * FTA_Q4) {
        m->center_y_q4 = (int16_t)(
            rig->plate_bottom_q4 - 3 * FTA_Q4 - mouth_reach);
    }
    m->half_w_q4 = (int16_t)fta_min_i32(
        m->half_w_q4,
        fta_min_i32(
            m->center_x_q4 - rig->plate_left_q4 - 3 * FTA_Q4,
            rig->plate_right_q4 - 3 * FTA_Q4 - m->center_x_q4));
}
