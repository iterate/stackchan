#include "fea_internal.h"

#include "face_pose.h"

/*
 * Shared performance solver: 40-byte IR + 16 kHz sample clock -> dense
 * renderer-agnostic pose. One implementation drives all five actors so
 * acting quality is uniform; the actors differ in how the pose becomes
 * pixels, not in what the pose means.
 *
 * Layer order: schema gate -> controls prefix -> deterministic life
 * (blink, saccade, drift, breath) -> viseme articulation -> speech
 * phases -> emotion identity accents (scaled by the acting curve) ->
 * whole-face pose -> hard clamps.
 */

enum {
    FEA_SAMPLE_RATE = 16000,
};

/* ------------------------------------------------------ emotion accents */

typedef struct {
    int16_t ap;             /* aperture bias Q8 */
    int16_t ap_asym;        /* +left/-right aperture asymmetry Q8 */
    int16_t lolid;          /* lower-lid raise (smile squint) Q8 */
    int16_t lid_tilt;       /* upper lid tilt, + inner up (oblique) */
    int16_t brow_l, brow_r; /* raise bias Q8 */
    int16_t btilt_l, btilt_r;
    int16_t knit;
    int16_t pupil;          /* pupil scale bias Q8 */
    int16_t sparkle;        /* glint energy 0..255 */
    int16_t gaze_x, gaze_y; /* aversion bias Q8 */
    int16_t corner_l, corner_r;
    int16_t curve;
    int16_t jaw, width, press;
    int16_t round_override; /* surprise O-mouth, 0 none */
    int16_t cheek;
    int16_t oy_q4;          /* posture offset */
    int16_t roll_q12;
    int16_t stretch;        /* scale_y bias Q8 */
    int16_t breath_rate;    /* Q8, 256 nominal */
    int16_t breath_amp;     /* Q8 */
} fea_accent_t;

static const fea_accent_t ACCENTS[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 256, 256 },
    [FACE_EXPRESSION_WARM] = {
        -12, 0, 70, 0, 12, 12, 8, 8, 0, 20, 30, 0, 8, 20, 20, 40,
        0, -6, 0, 0, 40, 0, 0, 4, 230, 280 },
    [FACE_EXPRESSION_JOY] = {
        -20, 0, 150, 0, 25, 25, 12, 12, 0, 10, 90, 0, -6, 45, 45, 90,
        30, 10, 0, 0, 70, -48, 0, 12, 300, 220 },
    [FACE_EXPRESSION_CONCERN] = {
        -25, 0, 0, 90, 10, 10, 40, 40, 80, -10, 0, 30, 40, -35, -35,
        -60, 0, -25, 30, 0, 0, 32, 0, -6, 320, 140 },
    [FACE_EXPRESSION_SURPRISE] = {
        90, 0, 0, 0, 90, 90, 10, 10, 0, -70, 20, 0, 0, 0, 0, 0,
        110, -40, 0, 180, 0, -32, 0, 18, 256, 0 },
    [FACE_EXPRESSION_THOUGHTFUL] = {
        -35, 8, 10, 15, 55, -5, 30, 0, 20, 10, 0, -70, -80, -15, 5,
        -10, 0, -15, 50, 0, 0, 0, 30, 0, 200, 180 },
    [FACE_EXPRESSION_SKEPTICAL] = {
        -20, 10, 15, -20, -30, 75, -40, 0, 25, 0, 0, -60, 0, 10, -45,
        -30, 0, 10, 80, 0, 0, 0, -40, 0, 220, 160 },
    [FACE_EXPRESSION_DETERMINED] = {
        -30, 0, 40, -35, -55, -55, -60, -60, 90, -25, 15, 0, 0, -20,
        -20, -20, 0, 45, 110, 0, 0, 16, 0, 8, 280, 120 },
    [FACE_EXPRESSION_SLEEPY] = {
        -150, 22, 0, -12, -35, -30, 10, 10, 0, 25, 0, 10, 60, -10,
        -5, -15, 25, -30, 0, 0, 0, 48, 50, -14, 140, 320 },
    [FACE_EXPRESSION_EXCITED] = {
        55, 0, 40, 0, 65, 65, 25, 25, 0, 80, 200, 0, -10, 50, 50, 80,
        55, 25, 0, 0, 45, -48, 0, 22, 360, 200 },
    [FACE_EXPRESSION_EMBARRASSED] = {
        -55, 12, 55, 25, 15, 5, 20, 10, 30, -15, 0, 96, 96, 25, -10,
        25, 0, -35, 65, 0, 160, 48, 25, -8, 300, 150 },
};

/* ------------------------------------------------------ viseme tables */

typedef struct {
    uint8_t jaw, width, round, press, teeth, tongue;
} fea_viseme_row_t;

/* OVR15 absolute mouth targets (0..255 semantic space). */
static const fea_viseme_row_t OVR15[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = { 210, 170, 30, 0, 90, 60 },
    [FACE_VISEME_E] = { 130, 210, 10, 0, 140, 40 },
    [FACE_VISEME_I] = { 80, 235, 0, 0, 160, 30 },
    [FACE_VISEME_O] = { 160, 95, 220, 0, 30, 20 },
    [FACE_VISEME_U] = { 90, 60, 255, 10, 10, 10 },
    [FACE_VISEME_PP] = { 0, 150, 0, 230, 0, 0 },
    [FACE_VISEME_SS] = { 35, 205, 0, 60, 220, 30 },
    [FACE_VISEME_TH] = { 70, 185, 0, 20, 180, 230 },
    [FACE_VISEME_DD] = { 85, 190, 0, 30, 150, 120 },
    [FACE_VISEME_FF] = { 35, 175, 0, 180, 200, 0 },
    [FACE_VISEME_KK] = { 95, 175, 20, 10, 80, 0 },
    [FACE_VISEME_NN] = { 45, 180, 0, 90, 60, 90 },
    [FACE_VISEME_RR] = { 75, 140, 120, 20, 60, 50 },
    [FACE_VISEME_CH] = { 65, 150, 160, 70, 190, 0 },
    [FACE_VISEME_SIL] = { 0, 160, 0, 20, 0, 0 },
};

/* VRM5 (A I U E O) -> OVR15 */
static const uint8_t VRM5_MAP[5] = {
    FACE_VISEME_AA, FACE_VISEME_I, FACE_VISEME_U,
    FACE_VISEME_E, FACE_VISEME_O,
};

/* Preston-Blair 9 (AI, E, O, U, MBP, FV, L, WQ, etc/rest) -> OVR15 */
static const uint8_t PRESTON9_MAP[9] = {
    FACE_VISEME_AA, FACE_VISEME_E, FACE_VISEME_O, FACE_VISEME_U,
    FACE_VISEME_PP, FACE_VISEME_FF, FACE_VISEME_TH, FACE_VISEME_U,
    FACE_VISEME_SIL,
};

/*
 * Microsoft 22 -> OVR15 collapse. Index = Microsoft viseme id 0..21
 * (0 silence, 1-11 vowels/diphthongs, 12-21 consonant groups).
 */
static const uint8_t MS22_MAP[22] = {
    FACE_VISEME_SIL, FACE_VISEME_AA, FACE_VISEME_AA, FACE_VISEME_O,
    FACE_VISEME_E, FACE_VISEME_RR, FACE_VISEME_I, FACE_VISEME_U,
    FACE_VISEME_O, FACE_VISEME_AA, FACE_VISEME_O, FACE_VISEME_AA,
    FACE_VISEME_KK, FACE_VISEME_RR, FACE_VISEME_NN, FACE_VISEME_SS,
    FACE_VISEME_CH, FACE_VISEME_TH, FACE_VISEME_FF, FACE_VISEME_DD,
    FACE_VISEME_KK, FACE_VISEME_PP,
};

/* Map (viseme_set, viseme) to an OVR15 row; NULL when not mappable. */
static const fea_viseme_row_t *viseme_row(uint8_t set, uint8_t viseme)
{
    if (viseme == FACE_VISEME_NONE) {
        return NULL;
    }
    switch (set) {
    case FACE_VISEME_SET_OVR15:
        return viseme < FACE_VISEME_COUNT ? &OVR15[viseme] : NULL;
    case FACE_VISEME_SET_VRM5:
        return viseme < 5U ? &OVR15[VRM5_MAP[viseme]] : NULL;
    case FACE_VISEME_SET_PRESTON9:
        return viseme < 9U ? &OVR15[PRESTON9_MAP[viseme]] : NULL;
    case FACE_VISEME_SET_MICROSOFT22:
        return viseme < 22U ? &OVR15[MS22_MAP[viseme]] : NULL;
    default:
        return NULL;
    }
}

/* ------------------------------------------------------ life schedules */

/* Blink epoch lengths per activity, in samples. */
static const uint32_t BLINK_EPOCH[4] = {
    56000U,     /* idle      ~3.5 s  (~17/min) */
    75000U,     /* listening ~4.7 s */
    53000U,     /* thinking  ~3.3 s */
    34000U,     /* speaking  ~2.1 s  (~26/min + doubles) */
};

/* Gaze epoch lengths per activity, in samples. */
static const uint32_t GAZE_EPOCH[4] = {
    36000U, 48000U, 42000U, 22000U,
};

/* Eyes-Alive-style exponential saccade magnitudes, Q8 of full scale. */
static const uint8_t SACC_MAG_Q8[16] = {
    2, 7, 12, 17, 23, 30, 37, 45, 54, 64, 75, 90, 107, 131, 167, 245,
};

/* 8-way direction table (R, UR, U, UL, L, DL, D, DR), cumulative /256. */
static const uint8_t SACC_DIR_CDF[8] = {
    40, 57, 102, 121, 164, 184, 236, 255,
};
static const int8_t SACC_DIR_X[8] = { 4, 3, 0, -3, -4, -3, 0, 3 };
static const int8_t SACC_DIR_Y[8] = { 0, -3, -4, -3, 0, 3, 4, 3 };

/*
 * Blink closure at this clock: hashed offset inside fixed activity
 * epochs, 15 % double blinks, deterministic at any frame order.
 */
static int32_t blink_closure(
    uint32_t clock, uint8_t activity, bool forced)
{
    if (forced) {
        return fea_blink_wave_q8(clock % FEA_BLINK_TOTAL_SAMPLES);
    }
    const uint32_t epoch_len = BLINK_EPOCH[activity & 3U];
    const uint32_t epoch = clock / epoch_len;
    const uint32_t phase = clock - epoch * epoch_len;
    const uint32_t hash = fea_hash2(epoch, 0xB111CU);
    const uint32_t double_extra =
        (uint32_t)FEA_BLINK_TOTAL_SAMPLES + 1600U;
    const uint32_t span = epoch_len - FEA_BLINK_TOTAL_SAMPLES -
        double_extra - 800U;
    const uint32_t offset = 400U + (hash % span);
    int32_t closure = 0;
    if (phase >= offset) {
        closure = fea_blink_wave_q8(phase - offset);
    }
    if (((hash >> 12) & 0x7fU) < 18U) {      /* ~14 % doubles */
        const uint32_t second = offset + double_extra;
        if (phase >= second) {
            const int32_t again = fea_blink_wave_q8(phase - second);
            if (again > closure) {
                closure = again;
            }
        }
    }
    return closure;
}

/* Saccade wander offset for one epoch (before amplitude scaling). */
static void gaze_epoch_target(
    uint32_t epoch, uint8_t activity, int32_t *dx, int32_t *dy)
{
    const uint32_t hash = fea_hash2(epoch, 0x5ACCADEU);
    const uint32_t dir_roll = hash & 0xffU;
    uint32_t dir = 0U;
    while (dir < 7U && dir_roll > SACC_DIR_CDF[dir]) {
        ++dir;
    }
    const int32_t mag = SACC_MAG_Q8[(hash >> 8) & 15U];
    *dx = (SACC_DIR_X[dir] * mag) / 4;
    *dy = (SACC_DIR_Y[dir] * mag * 3) / 16;
    if (activity == FACE_ACTIVITY_THINKING) {
        *dy -= 28;                            /* aversion trends up */
    }
}

static int32_t scale_q8(int32_t value, int32_t gain_q8)
{
    return (value * gain_q8) >> 8;
}

static int32_t lerp_q8(int32_t from, int32_t to, int32_t t_q8)
{
    return from + (((to - from) * t_q8) >> 8);
}

/* ------------------------------------------------------------- solver */

void fea_solve(
    const face_render_key_t *key, uint32_t clock, fea_pose_t *pose)
{
    const face_keyframe_t *c = &key->controls;

    /* schema gate: extended bytes are trusted from v2 upward */
    const bool extended = key->schema_version >= 2U;
    pose->extended = extended ? 1U : 0U;

    const uint8_t activity =
        c->expression <= FACE_ACTIVITY_SPEAKING
            ? c->expression : (uint8_t)FACE_ACTIVITY_IDLE;
    pose->activity = activity;
    pose->speaking =
        (c->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ? 1U : 0U;

    uint8_t emotion = FACE_EXPRESSION_NEUTRAL;
    uint8_t weight = 0U;
    uint8_t speech_phase =
        pose->speaking ? (uint8_t)FACE_SPEECH_ACTIVE
                       : (uint8_t)FACE_SPEECH_IDLE;
    uint8_t audio = 0U;
    int8_t valence = 0;
    uint8_t arousal = 96U;
    uint8_t attention = 200U;
    if (extended) {
        if (key->stage_expression < FACE_EXPRESSION_COUNT) {
            emotion = key->stage_expression;
            weight = key->expression_weight;
        }
        if (key->speech_phase <= FACE_SPEECH_ENDING) {
            speech_phase = key->speech_phase;
        }
        audio = key->audio_level;
        valence = key->affect_valence;
        arousal = key->affect_arousal;
        attention = key->attention;
    }
    pose->emotion = emotion;
    pose->speech_phase = speech_phase;
    pose->audio_q8 = audio;
    pose->valence = valence;
    pose->arousal = arousal;
    pose->attention = attention;

    const int32_t act = fea_act_curve_q8(weight);
    pose->act_q8 = (int16_t)act;
    const fea_accent_t *accent = &ACCENTS[emotion];

    /* ---------------------------------------------------------- breath */
    const int32_t rate_q8 = fea_clamp_i32(
        (accent->breath_rate * (176 + ((int32_t)arousal * 160) / 255)) >>
            8,
        64, 640);
    const uint32_t breath_period =
        (60800U << 8) / (uint32_t)rate_q8;
    const uint32_t breath_phase =
        (uint32_t)(((uint64_t)(clock % breath_period) << 16) /
                   breath_period);
    int32_t breath_wave;
    if (breath_phase < 27525U) {              /* inhale 42 % */
        const int32_t t =
            (int32_t)((breath_phase << 8) / 27525U);
        breath_wave = -256 + 2 * fea_smoothstep_q8(t);
    } else {
        const int32_t t = (int32_t)
            (((breath_phase - 27525U) << 8) / (65536U - 27525U));
        breath_wave = 256 - 2 * fea_smoothstep_q8(t);
    }
    const int32_t breath_amp = lerp_q8(256, accent->breath_amp, act);
    pose->breath_q8 =
        (int16_t)fea_clamp_i32((breath_wave * breath_amp) >> 8,
                               -320, 320);

    /* ------------------------------------------------------------ eyes */
    const bool forced_blink =
        (c->flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U;
    const int32_t closure = blink_closure(clock, activity, forced_blink);
    pose->blink_active = closure > 0 ? 1U : 0U;

    const uint8_t squint_l = extended ? key->eye_left_squint : 0U;
    const uint8_t squint_r = extended ? key->eye_right_squint : 0U;
    for (int eye = 0; eye < 2; ++eye) {
        const uint8_t base =
            eye == 0 ? c->eye_left_open : c->eye_right_open;
        const uint8_t squint = eye == 0 ? squint_l : squint_r;
        int32_t aperture = ((int32_t)base * 256) / 255;
        aperture = (aperture * (256 - ((int32_t)squint * 180) / 255)) >> 8;
        aperture += scale_q8(accent->ap, act);
        aperture += scale_q8(eye == 0 ? accent->ap_asym
                                      : -accent->ap_asym, act);
        aperture += (pose->breath_q8 * 6) >> 8;
        if (speech_phase == FACE_SPEECH_STARTING) {
            aperture += 30;                    /* anticipation widen */
        } else if (speech_phase == FACE_SPEECH_ENDING) {
            aperture -= 10;                    /* settle relax */
        }
        aperture = (aperture * (256 - closure)) >> 8;
        aperture = fea_clamp_i32(aperture, 0, 256);
        pose->eye_open_q8[eye] = (int16_t)aperture;
        pose->lower_lid_q8[eye] = (int16_t)fea_clamp_i32(
            scale_q8(accent->lolid, act) +
                ((int32_t)(eye == 0 ? squint_l : squint_r) * 90) / 255,
            0, 256);
        pose->lid_tilt_q8[eye] =
            (int16_t)fea_clamp_i32(scale_q8(accent->lid_tilt, act),
                                   -256, 256);
    }
    /* bilateral aperture floor: only a blink or closed controls may
     * fully close both eyes */
    if (closure == 0 && c->eye_left_open > 40U &&
        c->eye_right_open > 40U) {
        if (pose->eye_open_q8[0] < 12 && pose->eye_open_q8[1] < 12) {
            pose->eye_open_q8[0] = 12;
            pose->eye_open_q8[1] = 12;
        }
    }

    /* ------------------------------------------------------------ gaze */
    const uint32_t gaze_epoch_len = GAZE_EPOCH[activity & 3U];
    const uint32_t gaze_epoch = clock / gaze_epoch_len;
    const uint32_t gaze_phase = clock - gaze_epoch * gaze_epoch_len;
    int32_t cur_dx;
    int32_t cur_dy;
    int32_t prev_dx = 0;
    int32_t prev_dy = 0;
    gaze_epoch_target(gaze_epoch, activity, &cur_dx, &cur_dy);
    if (gaze_epoch > 0U) {
        gaze_epoch_target(gaze_epoch - 1U, activity, &prev_dx, &prev_dy);
    }
    const int32_t wander_q8 =
        40 + ((255 - (int32_t)attention) * 216) / 255;
    cur_dx = (cur_dx * wander_q8) >> 8;
    cur_dy = (cur_dy * wander_q8) >> 8;
    prev_dx = (prev_dx * wander_q8) >> 8;
    prev_dy = (prev_dy * wander_q8) >> 8;
    const int32_t amp =
        fea_isqrt64((int64_t)cur_dx * cur_dx + (int64_t)cur_dy * cur_dy);
    const uint32_t saccade_len = 400U + (uint32_t)amp * 8U;
    int32_t sacc_x = cur_dx;
    int32_t sacc_y = cur_dy;
    if (gaze_phase < saccade_len) {
        const int32_t t = fea_smoothstep_q8(
            (int32_t)((gaze_phase << 8) / saccade_len));
        sacc_x = lerp_q8(prev_dx, cur_dx, t);
        sacc_y = lerp_q8(prev_dy, cur_dy, t);
    }
    /* slow fixation drift, two incommensurate periods */
    const int32_t drift_x =
        (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U / 27296U)) * 7) >>
        14;
    const int32_t drift_y =
        (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U / 46208U)) * 5) >>
        14;
    pose->gaze_x_q8 = (int16_t)fea_clamp_i32(
        ((int32_t)c->look_x * 2) + scale_q8(accent->gaze_x, act) +
            sacc_x + drift_x,
        -256, 256);
    pose->gaze_y_q8 = (int16_t)fea_clamp_i32(
        ((int32_t)c->look_y * 2) + scale_q8(accent->gaze_y, act) +
            sacc_y + drift_y,
        -256, 256);

    pose->pupil_scale_q8 = (int16_t)fea_clamp_i32(
        256 + scale_q8(accent->pupil, act) +
            (((int32_t)arousal - 128) * 40) / 255,
        140, 400);
    pose->sparkle = (uint8_t)fea_clamp_i32(
        scale_q8(accent->sparkle, act) + (int32_t)arousal / 8, 0, 255);

    /* ------------------------------------------------------------ brows */
    const int32_t brow_base = ((int32_t)c->brow * 256) / 127;
    const int32_t brow_inner =
        extended ? ((int32_t)key->brow_inner * 2) : 0;
    const int32_t brow_outer_l =
        extended ? ((int32_t)key->brow_outer_left * 2) : 0;
    const int32_t brow_outer_r =
        extended ? ((int32_t)key->brow_outer_right * 2) : 0;
    int32_t speech_brow = 0;
    if (speech_phase == FACE_SPEECH_STARTING) {
        speech_brow = 40;                      /* anticipation raise */
    } else if (speech_phase == FACE_SPEECH_ACTIVE) {
        speech_brow = audio > 190U ? ((int32_t)audio - 190) : 0;
    }
    pose->brow_raise_q8[0] = (int16_t)fea_clamp_i32(
        brow_base + (brow_inner + brow_outer_l) / 2 +
            scale_q8(accent->brow_l, act) + speech_brow,
        -256, 256);
    pose->brow_raise_q8[1] = (int16_t)fea_clamp_i32(
        brow_base + (brow_inner + brow_outer_r) / 2 +
            scale_q8(accent->brow_r, act) + speech_brow,
        -256, 256);
    /* + tilt == inner end up (oblique/concern); - == inner down */
    pose->brow_tilt_q8[0] = (int16_t)fea_clamp_i32(
        (brow_inner - brow_outer_l) / 2 + scale_q8(accent->btilt_l, act),
        -256, 256);
    pose->brow_tilt_q8[1] = (int16_t)fea_clamp_i32(
        (brow_inner - brow_outer_r) / 2 + scale_q8(accent->btilt_r, act),
        -256, 256);
    pose->brow_knit_q8 = (int16_t)fea_clamp_i32(
        (brow_inner > 0 ? brow_inner / 2 : 0) +
            scale_q8(accent->knit, act),
        0, 256);

    /* ------------------------------------------------------------ mouth */
    int32_t jaw = ((int32_t)c->mouth_open * 256) / 255;
    int32_t width = 128 + ((int32_t)c->mouth_width * 192) / 255;
    int32_t round = ((int32_t)c->mouth_round * 256) / 255;
    int32_t press = ((int32_t)c->mouth_press * 256) / 255;
    int32_t teeth = ((int32_t)c->mouth_teeth * 256) / 255;
    int32_t tongue = extended ? ((int32_t)key->tongue * 256) / 255 : 0;

    if (extended) {
        const fea_viseme_row_t *prim =
            viseme_row(key->viseme_set, key->viseme);
        if (prim != NULL && key->viseme_weight > 0U) {
            fea_viseme_row_t mix = *prim;
            const fea_viseme_row_t *sec =
                viseme_row(key->viseme_set, key->viseme_secondary);
            if (sec != NULL && key->viseme_blend > 0U) {
                const int32_t b = ((int32_t)key->viseme_blend * 256) / 255;
                mix.jaw = (uint8_t)lerp_q8(prim->jaw, sec->jaw, b);
                mix.width = (uint8_t)lerp_q8(prim->width, sec->width, b);
                mix.round = (uint8_t)lerp_q8(prim->round, sec->round, b);
                mix.press = (uint8_t)lerp_q8(prim->press, sec->press, b);
                mix.teeth = (uint8_t)lerp_q8(prim->teeth, sec->teeth, b);
                mix.tongue =
                    (uint8_t)lerp_q8(prim->tongue, sec->tongue, b);
            }
            const int32_t v_w = ((int32_t)key->viseme_weight * 256) / 255;
            /* JALI split: jaw keeps 40 % of the analyzed opening, lips
             * follow the viseme almost fully */
            const int32_t jaw_w = (v_w * 154) >> 8;
            const int32_t lip_w = (v_w * 230) >> 8;
            jaw = lerp_q8(jaw, ((int32_t)mix.jaw * 256) / 255, jaw_w);
            width = lerp_q8(
                width, 128 + ((int32_t)mix.width * 192) / 255, lip_w);
            round = lerp_q8(round, ((int32_t)mix.round * 256) / 255,
                            lip_w);
            press = lerp_q8(press, ((int32_t)mix.press * 256) / 255,
                            lip_w);
            teeth = lerp_q8(teeth, ((int32_t)mix.teeth * 256) / 255,
                            lip_w);
            const int32_t vtongue =
                (((int32_t)mix.tongue * 256) / 255 * v_w) >> 8;
            if (vtongue > tongue) {
                tongue = vtongue;
            }
            /* phoneme: sub-viseme micro-shape so articulation does not
             * look quantized */
            if (key->phoneme != FACE_PHONEME_NONE) {
                const uint32_t ph = fea_hash32(key->phoneme);
                width += (((int32_t)(ph & 31U) - 16) * v_w) >> 8;
                round += (((int32_t)((ph >> 5) & 31U) - 16) * v_w) >> 8;
            }
        }
    }

    /* speech phases shape the mouth around articulation */
    if (speech_phase == FACE_SPEECH_STARTING) {
        jaw = (jaw * 130) >> 8;                /* pre-utterance hold */
        press += 40;
    } else if (speech_phase == FACE_SPEECH_ACTIVE) {
        jaw += ((int32_t)audio * 30) / 255;
    } else if (speech_phase == FACE_SPEECH_ENDING) {
        jaw = (jaw * 200) >> 8;                /* settle */
        press = (press * 200) >> 8;
    }

    /* emotion identity */
    jaw += scale_q8(accent->jaw, act);
    width += scale_q8(accent->width, act);
    press += scale_q8(accent->press, act);
    if (accent->round_override > 0) {
        round = fea_clamp_i32(
            round + scale_q8(accent->round_override, act), round, 256);
    }

    /* bilabial closure: hard press crushes the opening (quadratic so
     * moderate press only soften it) */
    jaw = (jaw * (256 - ((press * press) >> 8))) >> 8;

    pose->jaw_q8 = (int16_t)fea_clamp_i32(jaw, 0, 320);
    pose->mouth_w_q8 = (int16_t)fea_clamp_i32(width, 96, 320);
    pose->round_q8 = (int16_t)fea_clamp_i32(round, 0, 256);
    pose->press_q8 = (int16_t)fea_clamp_i32(press, 0, 256);
    pose->teeth_q8 = (int16_t)fea_clamp_i32(teeth, 0, 256);
    pose->tongue_q8 = (int16_t)fea_clamp_i32(tongue, 0, 256);

    const int32_t corner_base_l =
        extended ? ((int32_t)key->mouth_corner_left * 2) : 0;
    const int32_t corner_base_r =
        extended ? ((int32_t)key->mouth_corner_right * 2) : 0;
    int32_t settle = 0;
    if (speech_phase == FACE_SPEECH_ENDING) {
        settle = 14;                           /* soft settle smile */
    }
    pose->corner_q8[0] = (int16_t)fea_clamp_i32(
        corner_base_l + scale_q8(accent->corner_l, act) +
            (int32_t)valence / 4 + settle,
        -300, 300);
    pose->corner_q8[1] = (int16_t)fea_clamp_i32(
        corner_base_r + scale_q8(accent->corner_r, act) +
            (int32_t)valence / 4 + settle,
        -300, 300);
    pose->curve_q8 = (int16_t)fea_clamp_i32(
        scale_q8(accent->curve, act) +
            (pose->corner_q8[0] + pose->corner_q8[1]) / 4 +
            (int32_t)valence / 3,
        -300, 300);
    pose->cheek_q8 = (int16_t)fea_clamp_i32(
        (extended ? ((int32_t)key->cheek * 256) / 255 : 0) +
            scale_q8(accent->cheek, act),
        0, 256);

    /* ---------------------------------------------------- whole-face */
    const int32_t yaw = extended ? key->head_yaw : 0;
    const int32_t pitch = extended ? key->head_pitch : 0;
    const int32_t roll = extended ? key->head_roll : 0;
    const int32_t lean_x = extended ? key->body_lean_x : 0;
    const int32_t lean_y = extended ? key->body_lean_y : 0;

    int32_t bob = 0;
    if (speech_phase == FACE_SPEECH_ACTIVE && pose->speaking != 0U) {
        /* energy-coupled nod at ~2.6 Hz, a few pixels at most */
        bob = (fea_sin_q14((uint32_t)((uint64_t)clock * 65536U / 6154U)) *
               ((int32_t)audio * 40 / 255)) >> 14;
    }
    pose->ox_q4 = (int16_t)fea_clamp_i32(
        (yaw * 64) / 127 + (lean_x * 40) / 127, -96, 96);
    pose->oy_q4 = (int16_t)fea_clamp_i32(
        (pitch * 48) / 127 + (lean_y * 40) / 127 +
            scale_q8(accent->oy_q4 * 16, act) / 16 + bob,
        -80, 80);
    pose->shear_q12 = (int16_t)fea_clamp_i32(
        roll * 2 + scale_q8(accent->roll_q12, act), -420, 420);

    int32_t stretch = 256 + scale_q8(accent->stretch, act) +
        ((pose->breath_q8 * 5) >> 8);
    if (speech_phase == FACE_SPEECH_STARTING) {
        stretch += 8;                          /* inhale */
    }
    stretch = fea_clamp_i32(stretch, 236, 288);
    pose->scale_y_q8 = (int16_t)stretch;
    pose->scale_x_q8 = (int16_t)(65536 / stretch);  /* area conserving */
}
