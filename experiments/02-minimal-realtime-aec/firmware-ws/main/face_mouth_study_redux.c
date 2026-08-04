#include "face_mouth_study_redux.h"

#include <string.h>

#include "face_pose.h"

enum {
    MSR_EXPRESSION_COUNT = 11,
    MSR_EXPR_NEUTRAL = 0,
    MSR_EXPR_WARM = 1,
    MSR_EXPR_JOY = 2,
    MSR_EXPR_CONCERN = 3,
    MSR_EXPR_SURPRISE = 4,
    MSR_EXPR_THOUGHTFUL = 5,
    MSR_EXPR_SKEPTICAL = 6,
    MSR_EXPR_DETERMINED = 7,
    MSR_EXPR_SLEEPY = 8,
    MSR_EXPR_EXCITED = 9,
    MSR_EXPR_EMBARRASSED = 10,
};

#define MSR_RGB565(red, green, blue)                                    \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8) |                      \
                (((uint16_t)(green) & 0xfcU) << 3) |                    \
                ((uint16_t)(blue) >> 3)))

typedef struct {
    uint16_t *pixels;
} msr_canvas_t;

typedef struct {
    int8_t eye_left;
    int8_t eye_right;
    int8_t brow_left;
    int8_t brow_right;
    int8_t slant_left;
    int8_t slant_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t smile_left;
    int8_t smile_right;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t round;
    int8_t jaw;
    uint8_t cheek;
} msr_expression_t;

typedef struct {
    const char *slug;
    const char *name;
    int8_t left_eye_x;
    int8_t right_eye_x;
    int8_t eye_y;
    int8_t mouth_y;
    int8_t base_width;
    int8_t eye_radius_x;
    int8_t eye_radius_y;
} msr_style_t;

typedef struct {
    int16_t width;
    int16_t opening;
    int16_t round;
    int16_t press;
    int16_t teeth;
    int16_t tongue;
} msr_viseme_target_t;

static const msr_expression_t MSR_EXPRESSIONS[MSR_EXPRESSION_COUNT] = {
    [MSR_EXPR_NEUTRAL] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16,
    },
    [MSR_EXPR_WARM] = {
        -1, -1, 2, 2, -1, 1, 0, 0, 5, 5, -1, 3, 0, 0, 118,
    },
    [MSR_EXPR_JOY] = {
        -4, -4, 3, 3, -2, 2, 0, -1, 9, 9, 3, 7, 0, 2, 225,
    },
    [MSR_EXPR_CONCERN] = {
        -1, 0, 6, 6, 5, -5, -2, 1, -6, -5, -1, -2, 0, -1, 82,
    },
    [MSR_EXPR_SURPRISE] = {
        5, 5, 8, 8, -1, 1, 0, -2, 0, 0, 11, -8, 8, 7, 12,
    },
    [MSR_EXPR_THOUGHTFUL] = {
        -3, 1, 4, 0, 3, 0, -6, -3, -3, 1, -2, -4, 2, 0, 45,
    },
    [MSR_EXPR_SKEPTICAL] = {
        -5, 1, 7, -3, 5, -4, 5, 0, -4, 3, -2, 1, 0, 0, 52,
    },
    [MSR_EXPR_DETERMINED] = {
        -3, -3, -4, -4, -5, 5, 0, 1, -1, -1, 1, 6, 0, 2, 66,
    },
    [MSR_EXPR_SLEEPY] = {
        -7, -7, -3, -3, 0, 0, -3, 4, 2, 2, 1, -3, 3, 1, 28,
    },
    [MSR_EXPR_EXCITED] = {
        4, 4, 7, 7, -2, 2, 0, -2, 8, 8, 9, 8, 2, 6, 170,
    },
    [MSR_EXPR_EMBARRASSED] = {
        -2, -5, 3, 5, 2, -3, 5, 2, 5, 1, -1, 0, 1, 0, 255,
    },
};

static const msr_style_t MSR_STYLES[FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT] = {
    {
        "preston-sprites-redux",
        "Pip Preston cel rig",
        53, 107, 43, 82, 49, 13, 10,
    },
    {
        "polygon-jali-redux",
        "Jali articulated polygon rig",
        51, 109, 42, 82, 51, 14, 9,
    },
    {
        "bezier-ribbon-redux",
        "Ruby ribbon cabaret rig",
        50, 110, 42, 82, 43, 17, 12,
    },
    {
        "teeth-tongue-redux",
        "Munch teeth and tongue rig",
        50, 110, 40, 81, 55, 14, 10,
    },
    {
        "led-vu-mouth-redux",
        "Glyph-15 categorical viseme rig",
        52, 108, 42, 83, 50, 14, 8,
    },
    {
        "origami-mask-redux",
        "Ori folded-paper fox rig",
        51, 109, 43, 83, 47, 14, 9,
    },
};

static int32_t msr_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t msr_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t msr_mix(int32_t from, int32_t to, uint8_t weight)
{
    return from + ((to - from) * (int32_t)weight + 127) / 255;
}

static int32_t msr_smoothstep_q8(int32_t value)
{
    const int32_t t = msr_clamp(value, 0, 256);
    return (t * t * (768 - 2 * t) + 32768) / 65536;
}

static int32_t msr_ease_signed(
    int32_t value,
    int32_t input_limit,
    int32_t output_limit)
{
    const int32_t sign = value < 0 ? -1 : 1;
    const int32_t magnitude = msr_clamp(
        msr_abs(value), 0, input_limit);
    return sign *
        msr_smoothstep_q8(magnitude * 256 / input_limit) *
        output_limit / 256;
}

static bool msr_valid_profile(face_mouth_study_redux_profile_t profile)
{
    return (int)profile >= FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID &&
        (int)profile <= FACE_MOUTH_STUDY_REDUX_LAST_LEGACY_ID;
}

static size_t msr_style_index(face_mouth_study_redux_profile_t profile)
{
    return (size_t)(
        (int)profile - FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID);
}

static uint32_t msr_key_signature(const face_render_key_t *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < sizeof(*key); ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

size_t face_mouth_study_redux_profile_count(void)
{
    return FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT;
}

bool face_mouth_study_redux_profile_from_index(
    size_t index,
    face_mouth_study_redux_profile_t *profile)
{
    if (index >= FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT ||
        profile == NULL) {
        return false;
    }
    *profile = (face_mouth_study_redux_profile_t)(
        FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID + (int)index);
    return true;
}

bool face_mouth_study_redux_profile_from_legacy_id(
    uint8_t legacy_id,
    face_mouth_study_redux_profile_t *profile)
{
    if (legacy_id < FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID ||
        legacy_id > FACE_MOUTH_STUDY_REDUX_LAST_LEGACY_ID ||
        profile == NULL) {
        return false;
    }
    *profile = (face_mouth_study_redux_profile_t)legacy_id;
    return true;
}

const char *face_mouth_study_redux_profile_slug(
    face_mouth_study_redux_profile_t profile)
{
    return msr_valid_profile(profile)
        ? MSR_STYLES[msr_style_index(profile)].slug
        : NULL;
}

const char *face_mouth_study_redux_profile_name(
    face_mouth_study_redux_profile_t profile)
{
    return msr_valid_profile(profile)
        ? MSR_STYLES[msr_style_index(profile)].name
        : NULL;
}

static uint8_t msr_decode_viseme(uint8_t set, uint8_t raw)
{
    if (raw == FACE_VISEME_NONE) {
        return FACE_VISEME_SIL;
    }
    if (set == FACE_VISEME_SET_VRM5) {
        static const uint8_t VRM5[5] = {
            FACE_VISEME_AA,
            FACE_VISEME_E,
            FACE_VISEME_I,
            FACE_VISEME_O,
            FACE_VISEME_U,
        };
        return VRM5[raw % 5U];
    }
    if (set == FACE_VISEME_SET_PRESTON9) {
        static const uint8_t PRESTON9[9] = {
            FACE_VISEME_SIL,
            FACE_VISEME_PP,
            FACE_VISEME_FF,
            FACE_VISEME_TH,
            FACE_VISEME_E,
            FACE_VISEME_AA,
            FACE_VISEME_O,
            FACE_VISEME_U,
            FACE_VISEME_SS,
        };
        return PRESTON9[raw % 9U];
    }
    return raw < FACE_VISEME_COUNT
        ? raw
        : (uint8_t)(raw % FACE_VISEME_COUNT);
}

static msr_viseme_target_t msr_viseme_target(uint8_t viseme)
{
    static const msr_viseme_target_t TARGETS[FACE_VISEME_COUNT] = {
        [FACE_VISEME_AA] = {7, 22, 8, 0, 108, 42},
        [FACE_VISEME_E] = {13, 10, 0, 0, 195, 25},
        [FACE_VISEME_I] = {15, 7, 0, 0, 185, 20},
        [FACE_VISEME_O] = {-9, 19, 178, 0, 70, 36},
        [FACE_VISEME_U] = {-12, 13, 230, 0, 48, 30},
        [FACE_VISEME_PP] = {2, 1, 5, 250, 0, 0},
        [FACE_VISEME_SS] = {12, 5, 0, 72, 225, 16},
        [FACE_VISEME_TH] = {3, 8, 20, 36, 205, 245},
        [FACE_VISEME_DD] = {6, 9, 8, 22, 190, 118},
        [FACE_VISEME_FF] = {8, 4, 0, 92, 255, 18},
        [FACE_VISEME_KK] = {5, 14, 12, 0, 105, 82},
        [FACE_VISEME_NN] = {8, 7, 5, 15, 180, 188},
        [FACE_VISEME_RR] = {-2, 11, 105, 0, 92, 76},
        [FACE_VISEME_CH] = {10, 8, 12, 44, 210, 40},
        [FACE_VISEME_SIL] = {0, 2, 0, 92, 25, 0},
    };
    return TARGETS[
        viseme < FACE_VISEME_COUNT ? viseme : FACE_VISEME_SIL];
}

static msr_viseme_target_t msr_blend_target(
    msr_viseme_target_t first,
    msr_viseme_target_t second,
    uint8_t weight)
{
    return (msr_viseme_target_t){
        .width = (int16_t)msr_mix(first.width, second.width, weight),
        .opening =
            (int16_t)msr_mix(first.opening, second.opening, weight),
        .round = (int16_t)msr_mix(first.round, second.round, weight),
        .press = (int16_t)msr_mix(first.press, second.press, weight),
        .teeth = (int16_t)msr_mix(first.teeth, second.teeth, weight),
        .tongue =
            (int16_t)msr_mix(first.tongue, second.tongue, weight),
    };
}

static uint8_t msr_blink_q8(size_t style_index, uint32_t time_ms)
{
    const uint32_t period = 3100U + (uint32_t)style_index * 337U;
    const uint32_t phase =
        (time_ms + 727U + (uint32_t)style_index * 449U) % period;
    const uint32_t span = 330U;
    if (phase + span < period) {
        return 255U;
    }
    const uint32_t blink_time = phase + span - period;
    if (blink_time < 135U) {
        return (uint8_t)(
            255 - msr_smoothstep_q8(
                      (int32_t)(blink_time * 256U / 135U)));
    }
    if (blink_time < 165U) {
        return 0U;
    }
    return (uint8_t)msr_smoothstep_q8(
        (int32_t)((blink_time - 165U) * 256U / 165U));
}

bool face_mouth_study_redux_resolve(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *key,
    uint32_t sample_clock,
    face_mouth_study_redux_pose_t *pose)
{
    if (!msr_valid_profile(profile) || key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    pose->source = *key;
    pose->input_signature = msr_key_signature(key);

    const size_t style_index = msr_style_index(profile);
    const msr_style_t *style = &MSR_STYLES[style_index];
    const uint8_t expression =
        key->stage_expression < MSR_EXPRESSION_COUNT
            ? key->stage_expression
            : MSR_EXPR_NEUTRAL;
    const uint8_t expression_weight =
        key->stage_expression < MSR_EXPRESSION_COUNT
            ? key->expression_weight
            : 0U;
    const msr_expression_t *emotion = &MSR_EXPRESSIONS[expression];

    pose->face_center_x = 80;
    pose->face_center_y = 59;
    pose->left_eye_x = style->left_eye_x;
    pose->right_eye_x = style->right_eye_x;
    const bool fixed_facet_sockets =
        profile == FACE_MOUTH_STUDY_REDUX_JALI ||
        profile == FACE_MOUTH_STUDY_REDUX_ORIGAMI;
    pose->eye_y = fixed_facet_sockets
        ? style->eye_y
        : (int16_t)msr_clamp(
              style->eye_y + key->head_pitch / 42,
              style->eye_y - 3,
              style->eye_y + 3);
    pose->gaze_x = (int16_t)msr_clamp(
        key->controls.look_x / 15 +
            key->head_yaw / 34 +
            key->body_lean_x / 58 +
            msr_mix(0, emotion->gaze_x, expression_weight),
        -9,
        9);
    pose->gaze_y = (int16_t)msr_clamp(
        key->controls.look_y / 18 +
            key->head_pitch / 36 +
            key->body_lean_y / 64 +
            msr_mix(0, emotion->gaze_y, expression_weight),
        -6,
        6);
    if (profile == FACE_MOUTH_STUDY_REDUX_JALI) {
        pose->gaze_x = (int16_t)msr_ease_signed(
            pose->gaze_x, 9, 5);
        pose->gaze_y = (int16_t)msr_ease_signed(
            pose->gaze_y, 6, 3);
    }

    pose->blink_q8 =
        (key->controls.flags & FACE_KEYFRAME_FLAG_BLINKING) != 0U
            ? 0U
            : msr_blink_q8(style_index, sample_clock / 16U);
    const uint8_t requested_open[2] = {
        key->controls.eye_left_open,
        key->controls.eye_right_open,
    };
    const uint8_t squint[2] = {
        key->eye_left_squint,
        key->eye_right_squint,
    };
    const int8_t eye_emotion[2] = {
        emotion->eye_left,
        emotion->eye_right,
    };
    const int8_t brow_emotion[2] = {
        emotion->brow_left,
        emotion->brow_right,
    };
    const int8_t slant_emotion[2] = {
        emotion->slant_left,
        emotion->slant_right,
    };
    const int8_t outer_brow[2] = {
        key->brow_outer_left,
        key->brow_outer_right,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        int32_t openness =
            4 + requested_open[eye] * 13 / 255 +
            msr_mix(0, eye_emotion[eye], expression_weight) -
            squint[eye] * 7 / 255;
        openness = 2 + (openness - 2) * pose->blink_q8 / 255;
        pose->eye_open[eye] =
            (int16_t)msr_clamp(openness, 2, 21);
        pose->brow_raise[eye] = (int16_t)msr_clamp(
            key->controls.brow / 22 +
                key->brow_inner / 30 +
                outer_brow[eye] / 38 +
                msr_mix(0, brow_emotion[eye], expression_weight),
            -7,
            11);
        pose->brow_slant[eye] = (int16_t)msr_clamp(
            (eye == 0U ? key->brow_inner - outer_brow[eye]
                       : outer_brow[eye] - key->brow_inner) /
                    22 +
                msr_mix(0, slant_emotion[eye], expression_weight) +
                (eye == 0U ? key->head_roll : -key->head_roll) / 32,
            -9,
            9);
    }

    const uint8_t primary =
        msr_decode_viseme(key->viseme_set, key->viseme);
    msr_viseme_target_t target = msr_viseme_target(primary);
    if (key->viseme_secondary != FACE_VISEME_NONE &&
        key->viseme_blend > 0U) {
        const uint8_t secondary = msr_decode_viseme(
            key->viseme_set, key->viseme_secondary);
        target = msr_blend_target(
            target,
            msr_viseme_target(secondary),
            key->viseme_blend);
    }
    target = msr_blend_target(
        msr_viseme_target(FACE_VISEME_SIL),
        target,
        key->viseme_weight);

    const bool speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U;
    const int32_t audio = speaking ? key->audio_level : 0;
    const int32_t speech_drive =
        speaking
            ? msr_smoothstep_q8(
                  ((int32_t)key->viseme_weight * 2 +
                   key->controls.mouth_open + audio) *
                      64 /
                      255)
            : 0;
    pose->speech_drive_q8 =
        (uint8_t)msr_clamp(speech_drive, 0, 255);
    pose->anticipation_q8 =
        key->speech_phase == FACE_SPEECH_STARTING
            ? pose->speech_drive_q8
            : 0U;
    pose->settle_q8 =
        key->speech_phase == FACE_SPEECH_ENDING
            ? (uint8_t)(255U - pose->speech_drive_q8)
            : 0U;
    /*
     * The LED actor is a phonetic display, not an amplitude meter.  Audio
     * energy may animate its eyes and chassis, but identical visemes must
     * retain an identical mouth silhouette at quiet and loud levels.
     */
    const int32_t audio_open =
        profile == FACE_MOUTH_STUDY_REDUX_LED_VU
            ? 0
            : audio / 22;
    int32_t resolved_open =
        1 + key->controls.mouth_open * 15 / 255 +
        target.opening +
        audio_open +
        msr_mix(0, emotion->mouth_open, expression_weight);
    int32_t resolved_width =
        style->base_width +
        ((int32_t)key->controls.mouth_width - 128) / 7 +
        target.width +
        msr_mix(0, emotion->mouth_width, expression_weight);
    int32_t round =
        key->controls.mouth_round * 3 / 5 +
        target.round * 2 / 3 +
        msr_mix(0, emotion->round * 5, expression_weight);
    int32_t press =
        key->controls.mouth_press * 3 / 5 +
        target.press * 2 / 3;
    resolved_open =
        resolved_open * (300 - msr_clamp(press, 0, 255)) / 300;
    resolved_width -= round / 18;

    if (key->speech_phase == FACE_SPEECH_STARTING) {
        resolved_width += 3 + pose->anticipation_q8 / 64;
        resolved_open =
            resolved_open *
                (128 + pose->anticipation_q8 / 2) /
            256;
        pose->brow_raise[0] =
            (int16_t)msr_clamp(
                pose->brow_raise[0] +
                    1 + pose->anticipation_q8 / 80,
                -7,
                11);
        pose->brow_raise[1] =
            (int16_t)msr_clamp(
                pose->brow_raise[1] +
                    1 + pose->anticipation_q8 / 96,
                -7,
                11);
        pose->eye_open[0] =
            (int16_t)msr_clamp(
                pose->eye_open[0] +
                    1 + pose->anticipation_q8 / 160,
                2,
                21);
        pose->eye_open[1] =
            (int16_t)msr_clamp(
                pose->eye_open[1] +
                    pose->anticipation_q8 / 192,
                2,
                21);
    } else if (key->speech_phase == FACE_SPEECH_ENDING) {
        resolved_open =
            resolved_open *
                (256 - pose->settle_q8 / 2) /
            256;
        resolved_width -= 1 + pose->settle_q8 / 80;
        pose->gaze_y = (int16_t)msr_clamp(
            pose->gaze_y + 1 + pose->settle_q8 / 160,
            -6,
            6);
    }

    if (speaking) {
        pose->eye_open[0] = (int16_t)msr_clamp(
            pose->eye_open[0] + pose->speech_drive_q8 / 144,
            2,
            21);
        pose->eye_open[1] = (int16_t)msr_clamp(
            pose->eye_open[1] + pose->speech_drive_q8 / 176,
            2,
            21);
        pose->brow_raise[0] = (int16_t)msr_clamp(
            pose->brow_raise[0] + pose->speech_drive_q8 / 170,
            -7,
            11);
        pose->brow_raise[1] = (int16_t)msr_clamp(
            pose->brow_raise[1] + pose->speech_drive_q8 / 210,
            -7,
            11);
    }
    if (profile == FACE_MOUTH_STUDY_REDUX_PRESTON) {
        /*
         * Pip's face acting is deliberately low-frequency: stage emotion and
         * speech phase move lids, brows, and gaze, while raw PCM never jitters
         * the pupils.  This gives the pixel actor Cozmo-like whole-face intent
         * without inventing temporal state.
         */
        const int32_t pip_one =
            msr_mix(0, 1, expression_weight);
        const int32_t pip_two =
            msr_mix(0, 2, expression_weight);
        const int32_t pip_three =
            msr_mix(0, 3, expression_weight);
        const int32_t pip_four =
            msr_mix(0, 4, expression_weight);
        switch (expression) {
        case MSR_EXPR_WARM:
            pose->gaze_y = (int16_t)msr_clamp(
                pose->gaze_y + pip_one, -6, 6);
            pose->brow_raise[0] = (int16_t)msr_clamp(
                pose->brow_raise[0] + pip_one, -7, 11);
            pose->brow_raise[1] = (int16_t)msr_clamp(
                pose->brow_raise[1] + pip_one, -7, 11);
            break;
        case MSR_EXPR_JOY:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] - pip_two, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] - pip_two, 2, 21);
            pose->gaze_y = (int16_t)msr_clamp(
                pose->gaze_y + pip_one, -6, 6);
            break;
        case MSR_EXPR_CONCERN:
            pose->gaze_x = (int16_t)msr_clamp(
                pose->gaze_x - pip_one, -9, 9);
            pose->brow_slant[0] = (int16_t)msr_clamp(
                pose->brow_slant[0] + pip_two, -9, 9);
            pose->brow_slant[1] = (int16_t)msr_clamp(
                pose->brow_slant[1] - pip_two, -9, 9);
            break;
        case MSR_EXPR_SURPRISE:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] + pip_three, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] + pip_three, 2, 21);
            pose->brow_raise[0] = (int16_t)msr_clamp(
                pose->brow_raise[0] + pip_two, -7, 11);
            pose->brow_raise[1] = (int16_t)msr_clamp(
                pose->brow_raise[1] + pip_two, -7, 11);
            break;
        case MSR_EXPR_THOUGHTFUL:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] - pip_two, 2, 21);
            pose->gaze_x = (int16_t)msr_clamp(
                pose->gaze_x - pip_one, -9, 9);
            break;
        case MSR_EXPR_SKEPTICAL:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] - pip_three, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] + pip_one, 2, 21);
            pose->brow_raise[0] = (int16_t)msr_clamp(
                pose->brow_raise[0] + pip_two, -7, 11);
            pose->brow_raise[1] = (int16_t)msr_clamp(
                pose->brow_raise[1] - pip_one, -7, 11);
            break;
        case MSR_EXPR_DETERMINED:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] - pip_two, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] - pip_two, 2, 21);
            pose->brow_slant[0] = (int16_t)msr_clamp(
                pose->brow_slant[0] - pip_two, -9, 9);
            pose->brow_slant[1] = (int16_t)msr_clamp(
                pose->brow_slant[1] + pip_two, -9, 9);
            break;
        case MSR_EXPR_SLEEPY:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] - pip_four, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] - pip_four, 2, 21);
            pose->gaze_y = (int16_t)msr_clamp(
                pose->gaze_y + pip_two, -6, 6);
            break;
        case MSR_EXPR_EXCITED:
            pose->eye_open[0] = (int16_t)msr_clamp(
                pose->eye_open[0] + pip_three, 2, 21);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] + pip_two, 2, 21);
            pose->brow_raise[0] = (int16_t)msr_clamp(
                pose->brow_raise[0] + pip_two, -7, 11);
            pose->brow_raise[1] = (int16_t)msr_clamp(
                pose->brow_raise[1] + pip_two, -7, 11);
            break;
        case MSR_EXPR_EMBARRASSED:
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] - pip_two, 2, 21);
            pose->gaze_x = (int16_t)msr_clamp(
                pose->gaze_x + pip_one, -9, 9);
            break;
        default:
            break;
        }
        if (key->speech_phase == FACE_SPEECH_STARTING) {
            pose->gaze_x = (int16_t)msr_clamp(
                pose->gaze_x - 1, -9, 9);
            pose->brow_raise[0] = (int16_t)msr_clamp(
                pose->brow_raise[0] + 1, -7, 11);
        } else if (key->speech_phase == FACE_SPEECH_ENDING) {
            pose->gaze_y = (int16_t)msr_clamp(
                pose->gaze_y + 1, -6, 6);
            pose->eye_open[1] = (int16_t)msr_clamp(
                pose->eye_open[1] - 1, 2, 21);
        }
    } else if (
        profile == FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE &&
        key->speech_phase == FACE_SPEECH_STARTING) {
        pose->eye_open[0] = (int16_t)msr_clamp(
            pose->eye_open[0] +
                1 + pose->anticipation_q8 / 128,
            2,
            21);
        pose->brow_raise[0] = (int16_t)msr_clamp(
            pose->brow_raise[0] +
                1 + pose->anticipation_q8 / 96,
            -7,
            11);
        pose->brow_raise[1] = (int16_t)msr_clamp(
            pose->brow_raise[1] +
                pose->anticipation_q8 / 170,
            -7,
            11);
        pose->gaze_x = (int16_t)msr_clamp(
            pose->gaze_x - 1, -9, 9);
    } else if (
        profile == FACE_MOUTH_STUDY_REDUX_LED_VU &&
        speaking) {
        pose->eye_open[0] = (int16_t)msr_clamp(
            pose->eye_open[0] +
                1 + pose->speech_drive_q8 / 112,
            2,
            21);
        pose->eye_open[1] = (int16_t)msr_clamp(
            pose->eye_open[1] +
                pose->speech_drive_q8 / 160,
            2,
            21);
        pose->brow_raise[0] = (int16_t)msr_clamp(
            pose->brow_raise[0] +
                pose->speech_drive_q8 / 112,
            -7,
            11);
    }

    const bool fixed_fold_rig =
        profile == FACE_MOUTH_STUDY_REDUX_JALI ||
        profile == FACE_MOUTH_STUDY_REDUX_ORIGAMI;
    pose->mouth_center_x = fixed_fold_rig
        ? 80
        : (int16_t)msr_clamp(
              80 + key->head_yaw / 34 +
                  key->body_lean_x / 54 +
                  (key->mouth_corner_right -
                   key->mouth_corner_left) /
                      34,
              75,
              85);
    pose->mouth_center_y = fixed_fold_rig
        ? style->mouth_y
        : (int16_t)msr_clamp(
              style->mouth_y +
                  key->head_pitch / 45 +
                  key->body_lean_y / 60,
              style->mouth_y - 3,
              style->mouth_y + 4);
    if (profile == FACE_MOUTH_STUDY_REDUX_RIBBON) {
        const int32_t eased_aperture =
            msr_smoothstep_q8(
                msr_clamp(resolved_open - 2, 0, 32) * 8);
        pose->mouth_open = (int16_t)msr_clamp(
            2 + eased_aperture * 21 / 256,
            2,
            23);
    } else {
        pose->mouth_open =
            (int16_t)msr_clamp(resolved_open, 2, 34);
    }
    const int32_t minimum_width =
        pose->mouth_open + 16 > pose->mouth_open * 3 / 2
            ? pose->mouth_open + 16
            : pose->mouth_open * 3 / 2;
    pose->mouth_width = (int16_t)msr_clamp(
        resolved_width < minimum_width ? minimum_width : resolved_width,
        27,
        72);
    pose->mouth_round_q8 =
        (int16_t)msr_clamp(round, 0, 255);
    pose->lip_press_q8 =
        (int16_t)msr_clamp(press, 0, 255);

    const int32_t smile_left =
        msr_mix(0, emotion->smile_left, expression_weight) +
        key->mouth_corner_left / 20 +
        key->affect_valence / 25;
    const int32_t smile_right =
        msr_mix(0, emotion->smile_right, expression_weight) +
        key->mouth_corner_right / 20 +
        key->affect_valence / 25;
    pose->corner_y[0] =
        (int16_t)msr_clamp(-smile_left, -11, 11);
    pose->corner_y[1] =
        (int16_t)msr_clamp(-smile_right, -11, 11);
    if (profile == FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE &&
        key->speech_phase == FACE_SPEECH_ENDING) {
        pose->jaw_skew =
            (int16_t)(1 + pose->settle_q8 * 4 / 255);
        pose->mouth_center_x = (int16_t)msr_clamp(
            pose->mouth_center_x + pose->jaw_skew,
            75,
            87);
        pose->corner_y[0] = (int16_t)msr_clamp(
            pose->corner_y[0] -
                pose->settle_q8 * 2 / 255,
            -11,
            11);
        pose->corner_y[1] = (int16_t)msr_clamp(
            pose->corner_y[1] +
                pose->settle_q8 * 3 / 255,
            -11,
            11);
    }
    /*
     * Extreme/downturned inputs must still leave room for both lip strokes.
     * Constrain the aperture in face space instead of relying on raster
     * clipping at the bottom edge.
     */
    const int32_t average_corner =
        (pose->corner_y[0] + pose->corner_y[1]) / 2;
    pose->mouth_open = (int16_t)msr_clamp(
        pose->mouth_open,
        2,
        msr_clamp(
            114 - pose->mouth_center_y - average_corner +
                pose->mouth_round_q8 / 90,
            2,
            34));
    pose->jaw_drop = (int16_t)msr_clamp(
        pose->mouth_open * 2 / 3 +
            msr_mix(0, emotion->jaw, expression_weight) +
            audio / 64,
        2,
        25);
    pose->cheek_lift = (int16_t)msr_clamp(
        msr_mix(
            key->cheek / 28,
            emotion->cheek / 24,
            expression_weight) +
            (smile_left + smile_right) / 5 -
            pose->jaw_drop / 8 +
            pose->anticipation_q8 / 72,
        -4,
        12);
    if (profile == FACE_MOUTH_STUDY_REDUX_JALI) {
        pose->mouth_anchor_x[0] = 53;
        pose->mouth_anchor_x[1] = 107;
    } else if (profile == FACE_MOUTH_STUDY_REDUX_ORIGAMI) {
        pose->mouth_anchor_x[0] = 55;
        pose->mouth_anchor_x[1] = 105;
    } else {
        pose->mouth_anchor_x[0] =
            (int16_t)(
                pose->mouth_center_x - pose->mouth_width / 2);
        pose->mouth_anchor_x[1] =
            (int16_t)(
                pose->mouth_center_x + pose->mouth_width / 2);
    }
    pose->mouth_anchor_y[0] =
        (int16_t)(
            pose->mouth_center_y + pose->corner_y[0]);
    pose->mouth_anchor_y[1] =
        (int16_t)(
            pose->mouth_center_y + pose->corner_y[1]);
    for (size_t side = 0U; side < 2U; ++side) {
        if (fixed_fold_rig) {
            pose->cheek_x[side] =
                pose->mouth_anchor_x[side];
            pose->cheek_y[side] =
                pose->mouth_anchor_y[side];
        } else {
            const int32_t direction = side == 0U ? -1 : 1;
            pose->cheek_x[side] = (int16_t)(
                pose->mouth_center_x +
                direction * (pose->mouth_width / 2 + 8));
            pose->cheek_y[side] = (int16_t)(
                pose->mouth_center_y +
                pose->corner_y[side] -
                7 - pose->cheek_lift / 2);
        }
    }
    pose->teeth_q8 = (int16_t)msr_clamp(
        msr_mix(
            key->controls.mouth_teeth,
            target.teeth,
            key->viseme_weight),
        0,
        255);
    pose->tongue_q8 = (int16_t)msr_clamp(
        msr_mix(key->tongue, target.tongue, key->viseme_weight),
        0,
        255);
    pose->tongue_x = (int16_t)msr_clamp(
        (key->viseme == FACE_VISEME_TH ? 2 : 0) +
            (key->mouth_corner_right - key->mouth_corner_left) / 42,
        -5,
        5);
    pose->expression = expression;
    pose->expression_weight = expression_weight;
    pose->speech_phase = key->speech_phase;
    pose->viseme_class = primary;
    pose->viseme_set = key->viseme_set;
    pose->attention = key->attention;
    pose->speech_energy = (uint8_t)audio;
    const int32_t led_envelope_input = msr_clamp(
        (pose->mouth_open - 2) * 12 +
            pose->mouth_round_q8 / 5 +
            pose->teeth_q8 / 8,
        0,
        255);
    pose->led_envelope_q8 =
        (uint8_t)msr_clamp(
            msr_smoothstep_q8(led_envelope_input),
            0,
            255);
    return true;
}

static uint16_t msr_blend565(
    uint16_t from,
    uint16_t to,
    uint32_t alpha32)
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

static void msr_put(
    msr_canvas_t *canvas,
    int32_t x,
    int32_t y,
    uint16_t color,
    uint32_t alpha32)
{
    if ((unsigned)x >= FACE_MOUTH_STUDY_REDUX_WIDTH ||
        (unsigned)y >= FACE_MOUTH_STUDY_REDUX_HEIGHT) {
        return;
    }
    uint16_t *pixel =
        &canvas->pixels[
            (size_t)y * FACE_MOUTH_STUDY_REDUX_WIDTH + (size_t)x];
    *pixel = msr_blend565(*pixel, color, alpha32);
}

static void msr_gradient(
    msr_canvas_t *canvas,
    uint16_t top,
    uint16_t bottom)
{
    for (int32_t y = 0; y < FACE_MOUTH_STUDY_REDUX_HEIGHT; ++y) {
        const uint16_t color = msr_blend565(
            top,
            bottom,
            (uint32_t)y * 32U /
                (FACE_MOUTH_STUDY_REDUX_HEIGHT - 1U));
        for (int32_t x = 0; x < FACE_MOUTH_STUDY_REDUX_WIDTH; ++x) {
            canvas->pixels[
                (size_t)y * FACE_MOUTH_STUDY_REDUX_WIDTH +
                (size_t)x] = color;
        }
    }
}

static void msr_rect(
    msr_canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color,
    uint32_t alpha32)
{
    const int32_t left =
        msr_clamp(x, 0, FACE_MOUTH_STUDY_REDUX_WIDTH);
    const int32_t right =
        msr_clamp(x + width, 0, FACE_MOUTH_STUDY_REDUX_WIDTH);
    const int32_t top =
        msr_clamp(y, 0, FACE_MOUTH_STUDY_REDUX_HEIGHT);
    const int32_t bottom =
        msr_clamp(y + height, 0, FACE_MOUTH_STUDY_REDUX_HEIGHT);
    for (int32_t yy = top; yy < bottom; ++yy) {
        for (int32_t xx = left; xx < right; ++xx) {
            msr_put(canvas, xx, yy, color, alpha32);
        }
    }
}

static void msr_disc(
    msr_canvas_t *canvas,
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
                msr_put(canvas, x, y, color, alpha32);
            } else if (distance <= outer) {
                msr_put(canvas, x, y, color, alpha32 / 2U);
            }
        }
    }
}

static void msr_ellipse(
    msr_canvas_t *canvas,
    int32_t cx,
    int32_t cy,
    int32_t rx,
    int32_t ry,
    uint16_t color,
    uint32_t alpha32)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    const int64_t limit =
        (int64_t)rx * rx * ry * ry;
    for (int32_t y = cy - ry; y <= cy + ry; ++y) {
        for (int32_t x = cx - rx; x <= cx + rx; ++x) {
            const int32_t dx = x - cx;
            const int32_t dy = y - cy;
            if ((int64_t)dx * dx * ry * ry +
                    (int64_t)dy * dy * rx * rx <=
                limit) {
                msr_put(canvas, x, y, color, alpha32);
            }
        }
    }
}

static void msr_line(
    msr_canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color,
    uint32_t alpha32)
{
    const int32_t dx = msr_abs(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -msr_abs(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        msr_disc(
            canvas, x0, y0,
            thickness > 1 ? thickness / 2 : 1,
            color, alpha32);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void msr_curve(
    msr_canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t control_x,
    int32_t control_y,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color,
    uint32_t alpha32)
{
    int32_t previous_x = x0;
    int32_t previous_y = y0;
    for (int32_t step = 1; step <= 16; ++step) {
        const int32_t inverse = 16 - step;
        const int32_t x =
            (inverse * inverse * x0 +
             2 * inverse * step * control_x +
             step * step * x1 + 128) /
            256;
        const int32_t y =
            (inverse * inverse * y0 +
             2 * inverse * step * control_y +
             step * step * y1 + 128) /
            256;
        msr_line(
            canvas,
            previous_x,
            previous_y,
            x,
            y,
            thickness,
            color,
            alpha32);
        previous_x = x;
        previous_y = y;
    }
}

static int64_t msr_edge(
    int32_t ax,
    int32_t ay,
    int32_t bx,
    int32_t by,
    int32_t px,
    int32_t py)
{
    return (int64_t)(px - ax) * (by - ay) -
        (int64_t)(py - ay) * (bx - ax);
}

static void msr_triangle(
    msr_canvas_t *canvas,
    int32_t ax,
    int32_t ay,
    int32_t bx,
    int32_t by,
    int32_t cx,
    int32_t cy,
    uint16_t color,
    uint32_t alpha32)
{
    const int32_t left =
        msr_clamp(
            ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
            0,
            FACE_MOUTH_STUDY_REDUX_WIDTH - 1);
    const int32_t right =
        msr_clamp(
            ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
            0,
            FACE_MOUTH_STUDY_REDUX_WIDTH - 1);
    const int32_t top =
        msr_clamp(
            ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
            0,
            FACE_MOUTH_STUDY_REDUX_HEIGHT - 1);
    const int32_t bottom =
        msr_clamp(
            ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
            0,
            FACE_MOUTH_STUDY_REDUX_HEIGHT - 1);
    const int64_t orientation = msr_edge(ax, ay, bx, by, cx, cy);
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            const int64_t first = msr_edge(ax, ay, bx, by, x, y);
            const int64_t second = msr_edge(bx, by, cx, cy, x, y);
            const int64_t third = msr_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 &&
                 first >= 0 && second >= 0 && third >= 0) ||
                (orientation < 0 &&
                 first <= 0 && second <= 0 && third <= 0)) {
                msr_put(canvas, x, y, color, alpha32);
            }
        }
    }
}

static void msr_quad(
    msr_canvas_t *canvas,
    int32_t ax,
    int32_t ay,
    int32_t bx,
    int32_t by,
    int32_t cx,
    int32_t cy,
    int32_t dx,
    int32_t dy,
    uint16_t color,
    uint32_t alpha32)
{
    msr_triangle(
        canvas, ax, ay, bx, by, cx, cy, color, alpha32);
    msr_triangle(
        canvas, ax, ay, cx, cy, dx, dy, color, alpha32);
}

static void msr_draw_brows(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t color,
    int32_t thickness,
    bool blocky)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t y =
            pose->eye_y - 17 - pose->brow_raise[eye];
        const int32_t slant = pose->brow_slant[eye];
        if (blocky) {
            const int32_t left_y = y - slant / 2;
            const int32_t right_y = y + slant / 2;
            msr_line(
                canvas,
                centers[eye] - 14,
                left_y,
                centers[eye] + 14,
                right_y,
                thickness,
                color,
                32U);
        } else {
            msr_curve(
                canvas,
                centers[eye] - 15,
                y - slant / 2,
                centers[eye],
                y - 3,
                centers[eye] + 15,
                y + slant / 2,
                thickness,
                color,
                32U);
        }
    }
}

static void msr_draw_eyes(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t outline,
    bool angular,
    bool pixel)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t open = pose->eye_open[eye];
        const int32_t rx = style->eye_radius_x;
        const int32_t ry =
            msr_clamp(
                style->eye_radius_y * open / 16,
                2,
                style->eye_radius_y + 4);
        if (pixel) {
            msr_rect(
                canvas,
                centers[eye] - rx - 2,
                pose->eye_y - ry - 2,
                (rx + 2) * 2,
                (ry + 2) * 2,
                outline,
                32U);
            msr_rect(
                canvas,
                centers[eye] - rx,
                pose->eye_y - ry,
                rx * 2,
                ry * 2,
                sclera,
                32U);
        } else if (angular) {
            msr_quad(
                canvas,
                centers[eye] - rx - 2,
                pose->eye_y,
                centers[eye],
                pose->eye_y - ry - 3,
                centers[eye] + rx + 2,
                pose->eye_y,
                centers[eye],
                pose->eye_y + ry + 3,
                outline,
                32U);
            msr_quad(
                canvas,
                centers[eye] - rx,
                pose->eye_y,
                centers[eye],
                pose->eye_y - ry,
                centers[eye] + rx,
                pose->eye_y,
                centers[eye],
                pose->eye_y + ry,
                sclera,
                32U);
        } else {
            msr_ellipse(
                canvas,
                centers[eye],
                pose->eye_y,
                rx + 2,
                ry + 2,
                outline,
                32U);
            msr_ellipse(
                canvas,
                centers[eye],
                pose->eye_y,
                rx,
                ry,
                sclera,
                32U);
        }
        const int32_t iris_y =
            msr_clamp(
                pose->eye_y + pose->gaze_y,
                pose->eye_y - ry + 3,
                pose->eye_y + ry - 3);
        const int32_t iris_x =
            centers[eye] + pose->gaze_x;
        const int32_t iris_ry = msr_clamp(ry - 2, 2, 6);
        if (open > 3) {
            if (pixel) {
                msr_rect(
                    canvas,
                    iris_x - 3,
                    iris_y - iris_ry,
                    7,
                    iris_ry * 2,
                    iris,
                    32U);
                msr_rect(
                    canvas,
                    iris_x - 1,
                    iris_y - msr_clamp(iris_ry - 1, 1, 5),
                    3,
                    msr_clamp(iris_ry - 1, 1, 5) * 2,
                    pupil,
                    32U);
            } else {
                msr_ellipse(
                    canvas, iris_x, iris_y,
                    5, iris_ry, iris, 32U);
                msr_ellipse(
                    canvas, iris_x, iris_y,
                    2, msr_clamp(iris_ry - 1, 1, 5),
                    pupil, 32U);
                msr_disc(
                    canvas, iris_x - 1, iris_y - 2,
                    1, MSR_RGB565(255, 255, 240), 24U);
            }
        } else {
            msr_curve(
                canvas,
                centers[eye] - rx,
                pose->eye_y,
                centers[eye],
                pose->eye_y + 2,
                centers[eye] + rx,
                pose->eye_y,
                2,
                outline,
                32U);
        }
    }
}

static void msr_draw_pip_eyes(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t outline,
    uint16_t skin)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t cx = centers[eye];
        const int32_t rx = style->eye_radius_x;
        const int32_t half_open = msr_clamp(
            2 + pose->eye_open[eye] * 8 / 21,
            2,
            10);
        const int32_t tilt = msr_clamp(
            pose->brow_slant[eye] / 3,
            -3,
            3);
        /*
         * Keep a fixed socket and move two lids through it.  Changing the
         * white rectangle's total size made the old eyes look like unrelated
         * sprites and let pupils escape during squints.
         */
        msr_rect(
            canvas,
            cx - rx - 3,
            pose->eye_y - 13,
            (rx + 3) * 2,
            26,
            outline,
            32U);
        msr_quad(
            canvas,
            cx - rx,
            pose->eye_y - half_open - tilt,
            cx + rx,
            pose->eye_y - half_open + tilt,
            cx + rx,
            pose->eye_y + half_open + tilt,
            cx - rx,
            pose->eye_y + half_open - tilt,
            sclera,
            32U);

        const int32_t iris_half_y = msr_clamp(
            half_open - 1,
            1,
            6);
        const int32_t iris_x = msr_clamp(
            cx + pose->gaze_x,
            cx - rx + 4,
            cx + rx - 4);
        const int32_t iris_y = msr_clamp(
            pose->eye_y + pose->gaze_y,
            pose->eye_y - half_open + iris_half_y,
            pose->eye_y + half_open - iris_half_y);
        if (half_open > 2) {
            msr_rect(
                canvas,
                iris_x - 4,
                iris_y - iris_half_y,
                8,
                iris_half_y * 2 + 1,
                iris,
                32U);
            msr_rect(
                canvas,
                iris_x - 1,
                iris_y - iris_half_y,
                3,
                iris_half_y * 2 + 1,
                pupil,
                32U);
            msr_rect(
                canvas,
                iris_x - 1,
                iris_y - iris_half_y + 1,
                2,
                2,
                MSR_RGB565(255, 255, 232),
                32U);
        } else {
            msr_rect(
                canvas,
                cx - rx,
                pose->eye_y - 1,
                rx * 2,
                3,
                outline,
                32U);
        }

        /*
         * Lids are face-colored masks parented to the fixed socket.  They
         * create strong, attached expression silhouettes at 40x30.
         */
        const int32_t top_lid_y =
            pose->eye_y - half_open - tilt;
        const int32_t bottom_lid_y =
            pose->eye_y + half_open - tilt;
        msr_triangle(
            canvas,
            cx - rx,
            pose->eye_y - 12,
            cx + rx,
            pose->eye_y - 12,
            cx - rx,
            top_lid_y,
            skin,
            32U);
        msr_triangle(
            canvas,
            cx + rx,
            pose->eye_y - 12,
            cx + rx,
            top_lid_y + tilt * 2,
            cx - rx,
            top_lid_y,
            skin,
            32U);
        msr_triangle(
            canvas,
            cx - rx,
            pose->eye_y + 12,
            cx - rx,
            bottom_lid_y,
            cx + rx,
            pose->eye_y + 12,
            skin,
            32U);
        msr_triangle(
            canvas,
            cx + rx,
            pose->eye_y + 12,
            cx - rx,
            bottom_lid_y,
            cx + rx,
            bottom_lid_y + tilt * 2,
            skin,
            32U);
        msr_line(
            canvas,
            cx - rx,
            top_lid_y,
            cx + rx,
            top_lid_y + tilt * 2,
            2,
            outline,
            32U);
        msr_line(
            canvas,
            cx - rx,
            bottom_lid_y,
            cx + rx,
            bottom_lid_y + tilt * 2,
            1,
            outline,
            22U);
    }
}

static void msr_draw_pip_brows(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t color)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t y = msr_clamp(
            pose->eye_y - 14 -
                pose->brow_raise[eye] * 2 / 3,
            23,
            33);
        const int32_t slant = msr_clamp(
            pose->brow_slant[eye] / 2,
            -4,
            4);
        const int32_t left_y = y - slant;
        const int32_t right_y = y + slant;
        msr_line(
            canvas,
            centers[eye] - 14,
            left_y,
            centers[eye] + 14,
            right_y,
            4,
            color,
            32U);
        /*
         * A short inner cap makes concern/determination readable even after
         * the exact 4:1 contact-scale sample.
         */
        const int32_t inner_x =
            eye == 0U ? centers[eye] + 11 : centers[eye] - 11;
        const int32_t inner_y =
            eye == 0U ? right_y : left_y;
        msr_rect(
            canvas,
            inner_x - 2,
            inner_y - 2,
            5,
            4,
            color,
            32U);
    }
}

static void msr_draw_jali_eyes(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t outline)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t socket_rx = style->eye_radius_x + 2;
        const int32_t socket_ry = style->eye_radius_y + 3;
        const int32_t aperture_ry = msr_clamp(
            style->eye_radius_y * pose->eye_open[eye] / 16,
            2,
            style->eye_radius_y);
        msr_quad(
            canvas,
            centers[eye] - socket_rx,
            pose->eye_y,
            centers[eye],
            pose->eye_y - socket_ry,
            centers[eye] + socket_rx,
            pose->eye_y,
            centers[eye],
            pose->eye_y + socket_ry,
            outline,
            32U);
        msr_quad(
            canvas,
            centers[eye] - style->eye_radius_x,
            pose->eye_y,
            centers[eye],
            pose->eye_y - aperture_ry,
            centers[eye] + style->eye_radius_x,
            pose->eye_y,
            centers[eye],
            pose->eye_y + aperture_ry,
            sclera,
            32U);
        const int32_t gaze_x =
            msr_clamp(pose->gaze_x, -5, 5);
        const int32_t gaze_y =
            msr_clamp(pose->gaze_y, -2, 2);
        const int32_t iris_ry =
            msr_clamp(aperture_ry - 2, 1, 4);
        const int32_t iris_rx =
            msr_clamp(
                style->eye_radius_x -
                    msr_abs(gaze_y) * 2 - 7,
                2,
                4);
        msr_ellipse(
            canvas,
            centers[eye] + gaze_x,
            pose->eye_y + gaze_y,
            iris_rx,
            iris_ry,
            iris,
            32U);
        msr_ellipse(
            canvas,
            centers[eye] + gaze_x,
            pose->eye_y + gaze_y,
            1,
            msr_clamp(iris_ry - 1, 1, 3),
            pupil,
            32U);
        msr_put(
            canvas,
            centers[eye] + gaze_x - 1,
            pose->eye_y + gaze_y - 1,
            MSR_RGB565(255, 255, 240),
            24U);
    }
}

static void msr_draw_led_eyes(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t cyan,
    uint16_t hot,
    uint16_t dim)
{
    const int32_t centers[2] = {
        pose->left_eye_x,
        pose->right_eye_x,
    };
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t focus_column = msr_clamp(
            pose->gaze_x / 4,
            -1,
            1);
        const int32_t focus_row = msr_clamp(
            pose->gaze_y / 4,
            -1,
            1);
        const int32_t active_half_rows = msr_clamp(
            (pose->eye_open[eye] + 2) / 7,
            1,
            2);
        msr_rect(
            canvas,
            centers[eye] - 14,
            pose->eye_y - 10,
            28,
            20,
            MSR_RGB565(4, 35, 39),
            32U);
        for (int32_t row = -2; row <= 2; ++row) {
            for (int32_t column = -2; column <= 2; ++column) {
                const bool active =
                    msr_abs(row) < active_half_rows &&
                    msr_abs(column - focus_column) <= 1;
                uint16_t color = active ? cyan : dim;
                if (active &&
                    column == focus_column &&
                    row == focus_row) {
                    color = hot;
                }
                msr_rect(
                    canvas,
                    centers[eye] + column * 5 - 2,
                    pose->eye_y + row * 4 - 1,
                    4,
                    3,
                    color,
                    32U);
            }
        }
    }
}

static void msr_draw_cheeks(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t color,
    bool angular)
{
    for (size_t side = 0U; side < 2U; ++side) {
        if (angular) {
            msr_triangle(
                canvas,
                pose->cheek_x[side] - 5,
                pose->cheek_y[side] + 2,
                pose->cheek_x[side],
                pose->cheek_y[side] - 3 - pose->cheek_lift / 3,
                pose->cheek_x[side] + 5,
                pose->cheek_y[side] + 2,
                color,
                23U);
        } else {
            msr_ellipse(
                canvas,
                pose->cheek_x[side],
                pose->cheek_y[side],
                5 + pose->cheek_lift / 5,
                3,
                color,
                19U);
        }
    }
}

static void msr_mouth_points(
    const face_mouth_study_redux_pose_t *pose,
    int32_t *left_x,
    int32_t *left_y,
    int32_t *right_x,
    int32_t *right_y,
    int32_t *upper_y,
    int32_t *lower_y)
{
    *left_x = pose->mouth_anchor_x[0];
    *right_x = pose->mouth_anchor_x[1];
    *left_y = pose->mouth_anchor_y[0];
    *right_y = pose->mouth_anchor_y[1];
    const int32_t average_corner = (*left_y + *right_y) / 2;
    *upper_y =
        pose->mouth_center_y -
        msr_clamp(pose->mouth_open / 4, 1, 8);
    *lower_y =
        average_corner + pose->mouth_open -
        pose->mouth_round_q8 / 90;
}

static void msr_draw_continuous_mouth(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue,
    int32_t lip_thickness,
    bool angular)
{
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    int32_t upper_y;
    int32_t lower_y;
    msr_mouth_points(
        pose,
        &left_x,
        &left_y,
        &right_x,
        &right_y,
        &upper_y,
        &lower_y);
    const int32_t center_x = pose->mouth_center_x;
    const int32_t opening =
        msr_clamp(lower_y - upper_y, 1, 38);
    if (angular) {
        const int32_t anchor_half =
            msr_clamp((right_x - left_x) / 2 - 3, 3, 34);
        const int32_t inner_half = msr_clamp(
            pose->mouth_width / 2 - 4,
            3,
            anchor_half);
        const int32_t top_half = msr_clamp(
            inner_half * 3 / 4,
            3,
            inner_half);
        const int32_t bottom_half = msr_clamp(
            inner_half * 2 / 3,
            3,
            inner_half);
        /*
         * Use two attached trapezoids rather than a diamond with a
         * one-pixel bottom apex.  The apex survived 4:1 contact reduction
         * after its neck disappeared, so the JALI mouth read as a detached
         * chin shard.  This stays deliberately faceted while giving both
         * lip rails a flat, connected silhouette.
         */
        msr_quad(
            canvas,
            left_x - 2,
            left_y,
            center_x - top_half,
            upper_y - lip_thickness,
            center_x + top_half,
            upper_y - lip_thickness,
            right_x + 2,
            right_y,
            lip,
            32U);
        msr_quad(
            canvas,
            left_x - 2,
            left_y,
            right_x + 2,
            right_y,
            center_x + bottom_half + lip_thickness,
            lower_y + 1,
            center_x - bottom_half - lip_thickness,
            lower_y + 1,
            lip,
            32U);
        msr_quad(
            canvas,
            center_x - top_half,
            upper_y + 1,
            center_x + top_half,
            upper_y + 1,
            center_x + bottom_half,
            lower_y - 1,
            center_x - bottom_half,
            lower_y - 1,
            cavity,
            32U);
    } else {
        msr_curve(
            canvas,
            left_x,
            left_y,
            center_x,
            upper_y - lip_thickness / 2,
            right_x,
            right_y,
            lip_thickness + 3,
            lip,
            32U);
        msr_curve(
            canvas,
            left_x,
            left_y,
            center_x,
            lower_y + lip_thickness / 2,
            right_x,
            right_y,
            lip_thickness + 3,
            lip,
            32U);
        for (int32_t y = upper_y + 1;
             y < lower_y;
             ++y) {
            const int32_t vertical =
                y <= (upper_y + lower_y) / 2
                    ? y - upper_y
                    : lower_y - y;
            const int32_t half =
                pose->mouth_width / 2 -
                msr_clamp(
                    (opening / 2 - vertical) *
                        pose->mouth_width /
                        msr_clamp(opening * 2, 2, 76),
                    0,
                    pose->mouth_width / 3);
            msr_rect(
                canvas,
                center_x - half + 2,
                y,
                half * 2 - 3,
                1,
                cavity,
                32U);
        }
    }

    const int32_t interior_width =
        msr_clamp(pose->mouth_width - 9, 5, 65);
    const int32_t teeth_height = msr_clamp(
        opening * pose->teeth_q8 / 900,
        0,
        opening / 2);
    if (teeth_height > 0) {
        if (angular) {
            msr_quad(
                canvas,
                center_x - interior_width / 2,
                upper_y + 1,
                center_x + interior_width / 2,
                upper_y + 1,
                center_x + interior_width / 2 - 3,
                upper_y + 1 + teeth_height,
                center_x - interior_width / 2 + 3,
                upper_y + 1 + teeth_height,
                teeth,
                32U);
        } else {
            msr_rect(
                canvas,
                center_x - interior_width / 2,
                upper_y + 1,
                interior_width,
                teeth_height,
                teeth,
                32U);
        }
    }
    const int32_t tongue_height =
        msr_clamp(opening * pose->tongue_q8 / 1050, 0, opening / 2);
    if (tongue_height > 0) {
        const int32_t tongue_width =
            msr_clamp(interior_width * 2 / 3, 5, 38);
        if (angular) {
            msr_triangle(
                canvas,
                center_x + pose->tongue_x - tongue_width / 2,
                lower_y - 1,
                center_x + pose->tongue_x + tongue_width / 2,
                lower_y - 1,
                center_x + pose->tongue_x,
                lower_y - tongue_height - 2,
                tongue,
                32U);
        } else {
            msr_ellipse(
                canvas,
                center_x + pose->tongue_x,
                lower_y - tongue_height / 2,
                tongue_width / 2,
                msr_clamp(tongue_height, 1, 10),
                tongue,
                32U);
        }
    }
    msr_disc(canvas, left_x, left_y, lip_thickness, lip, 32U);
    msr_disc(canvas, right_x, right_y, lip_thickness, lip, 32U);
}

static void msr_draw_pip_mouth(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    int32_t upper_y;
    int32_t lower_y;
    msr_mouth_points(
        pose,
        &left_x,
        &left_y,
        &right_x,
        &right_y,
        &upper_y,
        &lower_y);
    const int32_t center_x = pose->mouth_center_x;
    const int32_t opening = msr_clamp(
        lower_y - upper_y,
        1,
        34);
    const int32_t interior_half = msr_clamp(
        pose->mouth_width / 2 - 3,
        5,
        31);

    /*
     * One continuous cavity and two continuous lip rails replace the old
     * tooth/no-tooth sprite topology.  The silhouette still follows every
     * viseme, but dental detail cross-fades over a stable patch of pixels.
     */
    msr_curve(
        canvas,
        left_x,
        left_y,
        center_x,
        upper_y - 1,
        right_x,
        right_y,
        5,
        lip,
        32U);
    msr_curve(
        canvas,
        left_x,
        left_y,
        center_x,
        lower_y + 1,
        right_x,
        right_y,
        5,
        lip,
        32U);
    for (int32_t y = upper_y + 1; y < lower_y; ++y) {
        const int32_t distance_to_edge =
            y - upper_y < lower_y - y
                ? y - upper_y
                : lower_y - y;
        const int32_t taper = msr_clamp(
            (opening / 2 - distance_to_edge) *
                interior_half /
                msr_clamp(opening, 2, 34),
            0,
            interior_half / 3);
        const int32_t half = interior_half - taper;
        msr_rect(
            canvas,
            center_x - half,
            y,
            half * 2 + 1,
            1,
            cavity,
            32U);
    }

    const int32_t tooth_alpha = msr_clamp(
        ((int32_t)pose->teeth_q8 - 72) * 22 / 183,
        0,
        22);
    if (opening >= 4 && tooth_alpha > 0) {
        const int32_t tooth_half = msr_clamp(
            interior_half * 3 / 4,
            4,
            23);
        /*
         * Fixed two-pixel coverage is intentional.  Varying tooth height by
         * one integer pixel was the visible white strobe in the old actor.
         */
        msr_rect(
            canvas,
            center_x - tooth_half,
            upper_y + 1,
            tooth_half * 2 + 1,
            2,
            teeth,
            (uint32_t)tooth_alpha);
    }

    const int32_t tongue_alpha = msr_clamp(
        ((int32_t)pose->tongue_q8 - 64) * 20 / 191,
        0,
        20);
    if (opening >= 6 && tongue_alpha > 0) {
        const int32_t tongue_half = msr_clamp(
            interior_half / 2,
            3,
            14);
        msr_rect(
            canvas,
            center_x + pose->tongue_x - tongue_half,
            lower_y - 3,
            tongue_half * 2 + 1,
            2,
            tongue,
            (uint32_t)tongue_alpha);
    }
    msr_rect(
        canvas,
        left_x - 2,
        left_y - 2,
        5,
        5,
        lip,
        32U);
    msr_rect(
        canvas,
        right_x - 2,
        right_y - 2,
        5,
        5,
        lip,
        32U);
}

static void msr_render_preston(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t outline = MSR_RGB565(44, 31, 42);
    const uint16_t skin = MSR_RGB565(247, 181, 105);
    const uint16_t skin_dark = MSR_RGB565(204, 99, 70);
    const uint16_t hair = MSR_RGB565(87, 48, 44);
    const uint16_t shirt = MSR_RGB565(40, 105, 150);
    msr_gradient(
        canvas,
        MSR_RGB565(37, 92, 100),
        MSR_RGB565(18, 49, 67));
    msr_rect(canvas, 28, 105, 104, 15, shirt, 32U);
    msr_rect(
        canvas, 38, 15, 84,
        92 + pose->jaw_drop / 2, outline, 32U);
    msr_rect(
        canvas, 42, 18, 76,
        86 + pose->jaw_drop / 2, skin, 32U);
    msr_rect(canvas, 31, 43, 12, 27, outline, 32U);
    msr_rect(canvas, 34, 47, 9, 20, skin_dark, 32U);
    msr_rect(canvas, 117, 43, 12, 27, outline, 32U);
    msr_rect(canvas, 117, 47, 9, 20, skin_dark, 32U);
    msr_rect(canvas, 39, 14, 82, 13, hair, 32U);
    msr_rect(canvas, 48, 9, 16, 9, hair, 32U);
    msr_rect(canvas, 92, 9, 20, 9, hair, 32U);
    msr_rect(canvas, 75, 22, 9, 7, skin, 32U);
    msr_draw_pip_eyes(
        canvas,
        style,
        pose,
        MSR_RGB565(255, 250, 220),
        MSR_RGB565(34, 141, 153),
        outline,
        outline,
        skin);
    msr_draw_pip_brows(canvas, pose, outline);
    msr_rect(
        canvas,
        pose->face_center_x - 2,
        55,
        4,
        13,
        skin_dark,
        32U);
    msr_draw_cheeks(
        canvas, pose, MSR_RGB565(237, 92, 101), false);
    /*
     * Preston is a deliberately chunky pixel actor, but the shared
     * high-gain speech pose can otherwise turn AA/O into a beard-sized
     * cavity.  Compress only this renderer's visual jaw while retaining
     * the full resolved pose for eyes, cheeks, timing, and diagnostics.
     */
    face_mouth_study_redux_pose_t mouth_pose = *pose;
    mouth_pose.mouth_open = (int16_t)msr_clamp(
        2 + (pose->mouth_open - 2) * 9 / 16,
        2,
        20);
    mouth_pose.mouth_width = (int16_t)msr_clamp(
        pose->mouth_width * 4 / 5,
        22,
        56);
    const int32_t mouth_half = mouth_pose.mouth_width / 2;
    mouth_pose.mouth_anchor_x[0] =
        (int16_t)(mouth_pose.mouth_center_x - mouth_half);
    mouth_pose.mouth_anchor_x[1] =
        (int16_t)(mouth_pose.mouth_center_x + mouth_half);
    msr_draw_pip_mouth(
        canvas,
        &mouth_pose,
        MSR_RGB565(179, 39, 66),
        outline,
        MSR_RGB565(255, 243, 208),
        MSR_RGB565(235, 93, 116));
    msr_rect(
        canvas,
        42,
        106 + pose->jaw_drop / 5,
        76,
        3,
        outline,
        18U);
}

static void msr_render_jali(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t ink = MSR_RGB565(20, 19, 48);
    const uint16_t violet = MSR_RGB565(91, 57, 132);
    const uint16_t violet_light = MSR_RGB565(143, 80, 163);
    const uint16_t cyan = MSR_RGB565(54, 232, 225);
    const uint16_t magenta = MSR_RGB565(247, 83, 166);
    msr_gradient(
        canvas,
        MSR_RGB565(27, 24, 68),
        MSR_RGB565(11, 14, 39));
    /*
     * A flat faceted jaw keeps the mask silhouette coherent at 40x30.
     * The former diamond ended in a four-native-pixel apex that survived
     * reduction as a detached purple tie beneath the mouth.
     */
    msr_quad(
        canvas, 80, 5, 137, 42, 128, 103, 80, 103, violet, 32U);
    msr_quad(
        canvas, 80, 5, 23, 42, 32, 103, 80, 103,
        violet_light, 32U);
    msr_triangle(
        canvas, 23, 42, 80, 58, 32, 103, MSR_RGB565(70, 46, 112), 32U);
    msr_triangle(
        canvas, 137, 42, 128, 103, 80, 58, MSR_RGB565(109, 55, 119), 32U);
    /*
     * Keep the faceted split as material detail, but never run it through
     * the eyes, nose, mouth, and chin.  The old full-height seam survived
     * downsampling as a broken clip through the face.
     */
    msr_line(canvas, 80, 7, 80, 29, 1, ink, 14U);
    msr_draw_jali_eyes(
        canvas,
        style,
        pose,
        MSR_RGB565(235, 245, 238),
        cyan,
        ink,
        ink);
    msr_draw_brows(canvas, pose, magenta, 3, true);
    msr_triangle(
        canvas,
        76,
        54,
        84,
        54,
        80,
        68,
        MSR_RGB565(192, 90, 158),
        32U);
    msr_draw_cheeks(canvas, pose, magenta, true);
    /* Keep the flat lower mask uninterrupted behind the mouth. */
    face_mouth_study_redux_pose_t mouth_pose = *pose;
    mouth_pose.mouth_width = (int16_t)msr_clamp(
        pose->mouth_width + 2, 24, 56);
    mouth_pose.mouth_open = (int16_t)msr_clamp(
        2 + (pose->mouth_open - 2) * 3 / 4,
        2,
        24);
    if (pose->viseme_class == FACE_VISEME_SS) {
        /* Sustained sibilant: long, thin teeth bank. */
        mouth_pose.mouth_width = (int16_t)msr_clamp(
            mouth_pose.mouth_width + 4,
            24,
            56);
        mouth_pose.mouth_open = (int16_t)msr_clamp(
            mouth_pose.mouth_open - 1,
            2,
            24);
    } else if (pose->viseme_class == FACE_VISEME_FF) {
        /* Labiodental contact: visibly shorter lower-lip span. */
        mouth_pose.mouth_width = (int16_t)msr_clamp(
            mouth_pose.mouth_width - 4,
            24,
            56);
        mouth_pose.mouth_open = (int16_t)msr_clamp(
            mouth_pose.mouth_open - 1,
            2,
            24);
    } else if (pose->viseme_class == FACE_VISEME_CH) {
        /*
         * CH is a compact affricate closure, not the long horizontal I
         * shape.  The generic resolved values are close enough to quantise
         * to the same polygon, so preserve that phonetic distinction here.
         */
        mouth_pose.mouth_width = (int16_t)msr_clamp(
            mouth_pose.mouth_width - 5,
            24,
            56);
        mouth_pose.mouth_open = (int16_t)msr_clamp(
            mouth_pose.mouth_open + 2,
            2,
            24);
        mouth_pose.teeth_q8 = (int16_t)msr_clamp(
            mouth_pose.teeth_q8 + 32,
            0,
            255);
    }
    /*
     * Keep the angular lip inside the jaw plate.  Wide fixed anchors made
     * the lip corners survive the 4:1 contact reduction as two unrelated
     * magenta pixels, rather than as part of one mouth.
     */
    const int32_t mouth_anchor_half = msr_clamp(
        mouth_pose.mouth_width / 2, 18, 23);
    mouth_pose.mouth_anchor_x[0] =
        (int16_t)(mouth_pose.mouth_center_x - mouth_anchor_half);
    mouth_pose.mouth_anchor_x[1] =
        (int16_t)(mouth_pose.mouth_center_x + mouth_anchor_half);
    msr_draw_continuous_mouth(
        canvas,
        &mouth_pose,
        magenta,
        ink,
        MSR_RGB565(248, 230, 215),
        MSR_RGB565(251, 116, 151),
        4,
        true);
}

static void msr_render_ribbon(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t ink = MSR_RGB565(57, 28, 49);
    const uint16_t skin = MSR_RGB565(250, 194, 162);
    const uint16_t hair = MSR_RGB565(100, 31, 65);
    const uint16_t blue = MSR_RGB565(40, 126, 178);
    const uint16_t rose = MSR_RGB565(220, 53, 108);
    msr_gradient(
        canvas,
        MSR_RGB565(122, 35, 100),
        MSR_RGB565(42, 17, 65));
    msr_triangle(
        canvas, 0, 22, 35, 8, 24, 120, MSR_RGB565(67, 22, 77), 32U);
    msr_triangle(
        canvas, 160, 22, 125, 8, 136, 120, MSR_RGB565(67, 22, 77), 32U);
    msr_ellipse(canvas, 80, 59, 51, 55, hair, 32U);
    msr_ellipse(
        canvas, 80, 61 + pose->jaw_drop / 7,
        43, 50 + pose->jaw_drop / 7, skin, 32U);
    msr_curve(
        canvas, 42, 25, 80, 6, 118, 25, 8, hair, 32U);
    msr_draw_eyes(
        canvas,
        style,
        pose,
        MSR_RGB565(255, 252, 232),
        MSR_RGB565(61, 149, 152),
        ink,
        ink,
        false,
        false);
    msr_draw_brows(canvas, pose, ink, 3, false);
    msr_curve(canvas, 76, 54, 80, 65, 84, 54, 1, ink, 25U);
    msr_draw_cheeks(
        canvas, pose, MSR_RGB565(241, 97, 126), false);
    /*
     * Ruby's former mouth occupied most of the lower face in every state.
     * Preserve the cabaret lip shape while reserving enough face for the
     * eyes and brows to carry emotion at contact scale.
     */
    face_mouth_study_redux_pose_t mouth_pose = *pose;
    mouth_pose.mouth_width = (int16_t)msr_clamp(
        pose->mouth_width * 4 / 5, 20, 48);
    mouth_pose.mouth_open = (int16_t)msr_clamp(
        pose->mouth_open * 3 / 4, 1, 24);
    mouth_pose.mouth_anchor_x[0] =
        (int16_t)(mouth_pose.mouth_center_x - mouth_pose.mouth_width / 2);
    mouth_pose.mouth_anchor_x[1] =
        (int16_t)(mouth_pose.mouth_center_x + mouth_pose.mouth_width / 2);
    msr_draw_continuous_mouth(
        canvas,
        &mouth_pose,
        rose,
        ink,
        MSR_RGB565(255, 239, 222),
        MSR_RGB565(242, 109, 138),
        2,
        false);
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    int32_t upper_y;
    int32_t lower_y;
    msr_mouth_points(
        &mouth_pose,
        &left_x,
        &left_y,
        &right_x,
        &right_y,
        &upper_y,
        &lower_y);
    msr_curve(
        canvas,
        left_x - 2,
        left_y - 1,
        mouth_pose.mouth_center_x,
        upper_y - 4,
        right_x + 2,
        right_y - 1,
        2,
        blue,
        20U);
}

static void msr_draw_monster_teeth(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t tooth)
{
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    int32_t upper_y;
    int32_t lower_y;
    msr_mouth_points(
        pose,
        &left_x,
        &left_y,
        &right_x,
        &right_y,
        &upper_y,
        &lower_y);
    const int32_t available =
        msr_clamp(lower_y - upper_y - 2, 0, 30);
    const int32_t tooth_height =
        available * pose->teeth_q8 / 900;
    for (int32_t tooth_index = -3;
         tooth_index <= 3 && tooth_height > 0;
         ++tooth_index) {
        const int32_t x =
            pose->mouth_center_x +
            tooth_index * pose->mouth_width / 8;
        msr_triangle(
            canvas,
            x - 3,
            upper_y + 1,
            x + 3,
            upper_y + 1,
            x,
            upper_y + 1 + tooth_height,
            tooth,
            32U);
    }
}

static void msr_render_teeth(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t outline = MSR_RGB565(29, 54, 54);
    const uint16_t skin = MSR_RGB565(89, 190, 132);
    const uint16_t skin_dark = MSR_RGB565(48, 139, 111);
    const uint16_t cavity = MSR_RGB565(65, 15, 37);
    const uint16_t tooth = MSR_RGB565(255, 237, 176);
    msr_gradient(
        canvas,
        MSR_RGB565(197, 235, 204),
        MSR_RGB565(118, 188, 160));
    msr_triangle(canvas, 38, 20, 48, 3, 57, 24, skin_dark, 32U);
    msr_triangle(canvas, 122, 20, 112, 3, 103, 24, skin_dark, 32U);
    msr_ellipse(canvas, 80, 60, 60, 55, outline, 32U);
    msr_ellipse(
        canvas, 80, 60 + pose->jaw_drop / 6,
        56, 51 + pose->jaw_drop / 6, skin, 32U);
    msr_ellipse(canvas, 22, 57, 14, 21, skin_dark, 32U);
    msr_ellipse(canvas, 138, 57, 14, 21, skin_dark, 32U);
    msr_draw_eyes(
        canvas,
        style,
        pose,
        MSR_RGB565(255, 242, 190),
        MSR_RGB565(248, 92, 55),
        outline,
        outline,
        false,
        false);
    msr_draw_brows(canvas, pose, outline, 3, false);
    msr_rect(canvas, 76, 54, 3, 4, outline, 32U);
    msr_rect(canvas, 82, 54, 3, 4, outline, 32U);
    msr_draw_cheeks(
        canvas, pose, MSR_RGB565(244, 105, 115), false);
    msr_draw_continuous_mouth(
        canvas,
        pose,
        MSR_RGB565(34, 76, 65),
        cavity,
        tooth,
        MSR_RGB565(230, 77, 114),
        3,
        false);
    msr_draw_monster_teeth(canvas, pose, tooth);
    msr_curve(
        canvas,
        48,
        104,
        80 + pose->jaw_skew,
        110 + pose->jaw_drop / 4,
        112,
        104,
        2,
        skin_dark,
        24U);
}

static uint16_t msr_led_viseme_mask(uint8_t viseme, size_t row)
{
    /*
     * Five rows of eleven addressable blocks.  These are deliberately
     * phonetic glyphs, not samples from an amplitude envelope.
     */
    static const uint16_t MASKS[FACE_VISEME_COUNT][5] = {
        [FACE_VISEME_AA] = {
            0x1fcU, 0x3feU, 0x7ffU, 0x3feU, 0x1fcU,
        },
        [FACE_VISEME_E] = {
            0x000U, 0x7ffU, 0x7ffU, 0x3feU, 0x000U,
        },
        [FACE_VISEME_I] = {
            0x000U, 0x7ffU, 0x3feU, 0x000U, 0x000U,
        },
        [FACE_VISEME_O] = {
            0x0f8U, 0x1fcU, 0x1fcU, 0x1fcU, 0x0f8U,
        },
        [FACE_VISEME_U] = {
            0x070U, 0x070U, 0x070U, 0x0f8U, 0x1fcU,
        },
        [FACE_VISEME_PP] = {
            0x000U, 0x1fcU, 0x3feU, 0x1fcU, 0x000U,
        },
        [FACE_VISEME_SS] = {
            0x401U, 0x7ffU, 0x7ffU, 0x000U, 0x000U,
        },
        [FACE_VISEME_TH] = {
            0x1fcU, 0x3feU, 0x1fcU, 0x070U, 0x000U,
        },
        [FACE_VISEME_DD] = {
            0x1fcU, 0x3feU, 0x0f8U, 0x000U, 0x020U,
        },
        [FACE_VISEME_FF] = {
            0x3feU, 0x7ffU, 0x1fcU, 0x000U, 0x000U,
        },
        [FACE_VISEME_KK] = {
            0x1fcU, 0x3feU, 0x1fcU, 0x060U, 0x040U,
        },
        [FACE_VISEME_NN] = {
            0x1fcU, 0x1fcU, 0x070U, 0x000U, 0x010U,
        },
        [FACE_VISEME_RR] = {
            0x070U, 0x0f8U, 0x1fcU, 0x0f8U, 0x070U,
        },
        [FACE_VISEME_CH] = {
            0x3feU, 0x7ffU, 0x3feU, 0x1fcU, 0x000U,
        },
        [FACE_VISEME_SIL] = {
            0x000U, 0x000U, 0x1fcU, 0x000U, 0x000U,
        },
    };
    const uint8_t resolved =
        viseme < FACE_VISEME_COUNT ? viseme : FACE_VISEME_SIL;
    return MASKS[resolved][row < 5U ? row : 2U];
}

static uint16_t msr_led_viseme_color(
    uint8_t viseme,
    size_t row,
    uint16_t cyan)
{
    if (viseme == FACE_VISEME_TH && row >= 3U) {
        return MSR_RGB565(255, 91, 103);
    }
    if ((viseme == FACE_VISEME_E ||
         viseme == FACE_VISEME_I ||
         viseme == FACE_VISEME_SS ||
         viseme == FACE_VISEME_DD ||
         viseme == FACE_VISEME_FF ||
         viseme == FACE_VISEME_NN) &&
        row <= 2U) {
        return MSR_RGB565(255, 218, 91);
    }
    if (viseme == FACE_VISEME_O ||
        viseme == FACE_VISEME_U ||
        viseme == FACE_VISEME_RR) {
        return MSR_RGB565(69, 218, 255);
    }
    if (viseme == FACE_VISEME_KK ||
        viseme == FACE_VISEME_CH) {
        return MSR_RGB565(103, 244, 133);
    }
    return cyan;
}

static void msr_draw_led_viseme(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t cyan,
    uint16_t dim)
{
    const int32_t columns = 11;
    const int32_t rows = 5;
    const int32_t left =
        pose->mouth_center_x - columns * 3;
    const int32_t top =
        pose->mouth_center_y - rows * 5 / 2;
    const uint8_t primary = pose->viseme_class;
    const bool has_secondary =
        pose->source.viseme_secondary != FACE_VISEME_NONE &&
        pose->source.viseme_blend > 0U;
    const uint8_t secondary = has_secondary
        ? msr_decode_viseme(
              pose->viseme_set,
              pose->source.viseme_secondary)
        : primary;
    const int32_t speech_weight =
        pose->source.viseme_weight;
    const int32_t secondary_weight = has_secondary
        ? speech_weight * pose->source.viseme_blend / 255
        : 0;
    const int32_t primary_weight =
        speech_weight - secondary_weight;
    const int32_t silence_weight =
        255 - speech_weight;

    for (int32_t row = 0; row < rows; ++row) {
        const uint16_t silence_mask =
            msr_led_viseme_mask(FACE_VISEME_SIL, (size_t)row);
        const uint16_t primary_mask =
            msr_led_viseme_mask(primary, (size_t)row);
        const uint16_t secondary_mask =
            msr_led_viseme_mask(secondary, (size_t)row);
        for (int32_t column = 0; column < columns; ++column) {
            const uint16_t bit =
                (uint16_t)(1U << (unsigned)(columns - 1 - column));
            int32_t weight = 0;
            if ((silence_mask & bit) != 0U) {
                weight += silence_weight;
            }
            if ((primary_mask & bit) != 0U) {
                weight += primary_weight;
            }
            if ((secondary_mask & bit) != 0U) {
                weight += secondary_weight;
            }
            weight = msr_clamp(weight, 0, 255);
            const int32_t corner = column < columns / 2
                ? pose->corner_y[0]
                : pose->corner_y[1];
            const int32_t skew =
                corner *
                msr_abs(column * 2 - (columns - 1)) /
                (columns * 2);
            const int32_t x = left + column * 6;
            const int32_t y = top + row * 5 + skew;
            /* A barely-there hardware matrix, never a competing VU graph. */
            msr_rect(canvas, x, y, 4, 3, dim, 9U);
            if (weight > 0) {
                const bool secondary_dominates =
                    secondary_weight > primary_weight &&
                    (secondary_mask & bit) != 0U;
                const uint8_t color_viseme = secondary_dominates
                    ? secondary
                    : primary;
                msr_rect(
                    canvas,
                    x,
                    y,
                    4,
                    3,
                    msr_led_viseme_color(
                        color_viseme,
                        (size_t)row,
                        cyan),
                    (uint32_t)(7 + weight * 25 / 255));
            }
        }
    }
}

static void msr_render_led(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t black = MSR_RGB565(2, 8, 12);
    const uint16_t chassis = MSR_RGB565(74, 125, 137);
    const uint16_t chassis_dark = MSR_RGB565(31, 68, 82);
    const uint16_t cyan = MSR_RGB565(20, 228, 209);
    const uint16_t dim = MSR_RGB565(8, 55, 54);
    msr_gradient(
        canvas,
        MSR_RGB565(11, 31, 42),
        MSR_RGB565(3, 10, 18));
    msr_rect(canvas, 22, 12, 116, 100, chassis_dark, 32U);
    msr_rect(canvas, 27, 17, 106, 90, chassis, 32U);
    msr_rect(canvas, 33, 24, 94, 77, black, 32U);
    msr_rect(canvas, 73, 8, 14, 9, chassis, 32U);
    (void)style;
    msr_draw_led_eyes(
        canvas,
        pose,
        cyan,
        MSR_RGB565(225, 255, 224),
        dim);
    msr_draw_brows(canvas, pose, cyan, 3, true);
    for (size_t side = 0U; side < 2U; ++side) {
        msr_rect(
            canvas,
            pose->cheek_x[side] - 4,
            pose->cheek_y[side] - 2,
            8,
            4,
            pose->cheek_lift > 3
                ? MSR_RGB565(255, 176, 49)
                : dim,
            32U);
    }
    msr_draw_led_viseme(canvas, pose, cyan, dim);
    msr_line(
        canvas,
        40,
        108 + pose->jaw_drop / 8,
        120,
        108 + pose->jaw_drop / 8,
        2,
        chassis,
        32U);
}

typedef struct {
    int16_t half_width;
    int16_t opening;
} msr_origami_viseme_t;

static msr_origami_viseme_t msr_origami_viseme(uint8_t viseme)
{
    static const msr_origami_viseme_t VISEMES[FACE_VISEME_COUNT] = {
        [FACE_VISEME_AA] = {25, 27},
        [FACE_VISEME_E] = {29, 12},
        [FACE_VISEME_I] = {31, 7},
        [FACE_VISEME_O] = {14, 25},
        [FACE_VISEME_U] = {10, 17},
        [FACE_VISEME_PP] = {28, 3},
        [FACE_VISEME_SS] = {31, 9},
        [FACE_VISEME_TH] = {19, 14},
        [FACE_VISEME_DD] = {22, 12},
        [FACE_VISEME_FF] = {29, 6},
        [FACE_VISEME_KK] = {23, 19},
        [FACE_VISEME_NN] = {24, 9},
        [FACE_VISEME_RR] = {16, 16},
        [FACE_VISEME_CH] = {27, 16},
        [FACE_VISEME_SIL] = {24, 2},
    };
    return VISEMES[
        viseme < FACE_VISEME_COUNT ? viseme : FACE_VISEME_SIL];
}

static msr_origami_viseme_t msr_blend_origami_viseme(
    msr_origami_viseme_t first,
    msr_origami_viseme_t second,
    uint8_t weight)
{
    return (msr_origami_viseme_t){
        .half_width =
            (int16_t)msr_mix(first.half_width, second.half_width, weight),
        .opening =
            (int16_t)msr_mix(first.opening, second.opening, weight),
    };
}

static void msr_draw_origami_mouth(
    msr_canvas_t *canvas,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t lip,
    uint16_t cavity,
    uint16_t paper,
    uint16_t light,
    uint16_t shadow,
    uint16_t teeth,
    uint16_t tongue)
{
    const uint8_t primary = pose->viseme_class;
    const bool has_secondary =
        pose->source.viseme_secondary != FACE_VISEME_NONE &&
        pose->source.viseme_blend > 0U;
    const uint8_t secondary = has_secondary
        ? msr_decode_viseme(
              pose->viseme_set,
              pose->source.viseme_secondary)
        : primary;
    msr_origami_viseme_t shape = msr_origami_viseme(primary);
    if (has_secondary) {
        shape = msr_blend_origami_viseme(
            shape,
            msr_origami_viseme(secondary),
            pose->source.viseme_blend);
    }
    shape = msr_blend_origami_viseme(
        msr_origami_viseme(FACE_VISEME_SIL),
        shape,
        pose->source.viseme_weight);
    const uint8_t dominant =
        has_secondary && pose->source.viseme_blend >= 128U
            ? secondary
            : primary;
    const int32_t center_x = pose->mouth_center_x;
    const int32_t center_y = pose->mouth_center_y;
    const int32_t half = msr_clamp(
        (shape.half_width * 3 + pose->mouth_width / 2) / 4,
        8,
        31);
    const int32_t opening = msr_clamp(
        (shape.opening * 3 + pose->mouth_open) / 4,
        2,
        29);
    const int32_t top_y =
        center_y - msr_clamp(opening / 3, 1, 10);
    const int32_t bottom_y =
        center_y + msr_clamp(opening * 2 / 3, 1, 20);
    const int32_t left_x = pose->mouth_anchor_x[0];
    const int32_t right_x = pose->mouth_anchor_x[1];
    const int32_t left_y = pose->mouth_anchor_y[0];
    const int32_t right_y = pose->mouth_anchor_y[1];
    const int32_t inner_left_y =
        center_y + pose->corner_y[0] * half / 25;
    const int32_t inner_right_y =
        center_y + pose->corner_y[1] * half / 25;

    if (opening <= 4) {
        /*
         * Even a closed mouth is one connected fold from cheek anchor to
         * cheek anchor.  PP is a double press; SIL is a single relaxed seam.
         */
        msr_line(
            canvas,
            left_x,
            left_y,
            center_x,
            center_y,
            dominant == FACE_VISEME_PP ? 4 : 3,
            lip,
            32U);
        msr_line(
            canvas,
            center_x,
            center_y,
            right_x,
            right_y,
            dominant == FACE_VISEME_PP ? 4 : 3,
            lip,
            32U);
        if (dominant == FACE_VISEME_PP) {
            msr_line(
                canvas,
                left_x + 6,
                left_y + 3,
                right_x - 6,
                right_y + 3,
                2,
                shadow,
                24U);
        }
        return;
    }

    /*
     * The outer lip is a single anchored kite.  All interior folds are
     * contained by or touch that kite, so changing a viseme cannot create a
     * detached chin or floating mouth shard.
     */
    msr_quad(
        canvas,
        left_x,
        left_y,
        center_x,
        top_y - 3,
        right_x,
        right_y,
        center_x,
        bottom_y + 3,
        lip,
        32U);
    msr_triangle(
        canvas,
        left_x,
        left_y,
        center_x - half,
        inner_left_y,
        center_x,
        top_y - 3,
        light,
        18U);
    msr_triangle(
        canvas,
        right_x,
        right_y,
        center_x,
        top_y - 3,
        center_x + half,
        inner_right_y,
        shadow,
        18U);
    msr_quad(
        canvas,
        center_x - half,
        inner_left_y,
        center_x,
        top_y,
        center_x + half,
        inner_right_y,
        center_x,
        bottom_y,
        cavity,
        32U);

    msr_line(
        canvas,
        left_x,
        left_y,
        center_x - half,
        inner_left_y,
        1,
        paper,
        24U);
    msr_line(
        canvas,
        center_x + half,
        inner_right_y,
        right_x,
        right_y,
        1,
        paper,
        20U);

    const int32_t dental_alpha = msr_clamp(
        ((int32_t)pose->teeth_q8 - 56) * 26 / 199,
        0,
        26);
    if (dental_alpha > 0 &&
        dominant != FACE_VISEME_AA &&
        dominant != FACE_VISEME_O &&
        dominant != FACE_VISEME_U &&
        dominant != FACE_VISEME_RR) {
        const int32_t dental_half = msr_clamp(
            half - 3,
            4,
            25);
        msr_quad(
            canvas,
            center_x - dental_half,
            top_y + 1,
            center_x + dental_half,
            top_y + 1,
            center_x + dental_half - 3,
            top_y + 4,
            center_x - dental_half + 3,
            top_y + 4,
            teeth,
            (uint32_t)dental_alpha);
    }
    const int32_t tongue_alpha = msr_clamp(
        ((int32_t)pose->tongue_q8 - 72) * 26 / 183,
        0,
        26);
    if (tongue_alpha > 0 &&
        (dominant == FACE_VISEME_TH ||
         dominant == FACE_VISEME_DD ||
         dominant == FACE_VISEME_NN)) {
        const int32_t tongue_half = msr_clamp(
            half / 2,
            3,
            11);
        msr_triangle(
            canvas,
            center_x - tongue_half,
            bottom_y - 1,
            center_x + tongue_half,
            bottom_y - 1,
            center_x + pose->tongue_x,
            bottom_y - msr_clamp(opening / 3, 3, 8),
            tongue,
            (uint32_t)tongue_alpha);
    }

    /*
     * Attached crease accents distinguish consonants with similar aperture
     * without adding free-floating decorative pixels.
     */
    if (dominant == FACE_VISEME_KK) {
        msr_line(
            canvas,
            center_x,
            top_y,
            center_x + half,
            inner_right_y,
            2,
            paper,
            25U);
    } else if (dominant == FACE_VISEME_CH) {
        msr_line(
            canvas,
            center_x - half,
            inner_left_y,
            center_x,
            bottom_y,
            2,
            paper,
            23U);
        msr_line(
            canvas,
            center_x,
            bottom_y,
            center_x + half,
            inner_right_y,
            2,
            shadow,
            23U);
    } else if (dominant == FACE_VISEME_RR) {
        msr_line(
            canvas,
            center_x,
            top_y,
            center_x,
            bottom_y,
            1,
            tongue,
            22U);
    } else if (dominant == FACE_VISEME_SS) {
        msr_line(
            canvas,
            center_x - half,
            inner_left_y,
            center_x + half,
            inner_right_y,
            2,
            teeth,
            20U);
    }
}

static void msr_render_origami(
    msr_canvas_t *canvas,
    const msr_style_t *style,
    const face_mouth_study_redux_pose_t *pose)
{
    const uint16_t ink = MSR_RGB565(49, 42, 56);
    const uint16_t paper = MSR_RGB565(244, 158, 119);
    const uint16_t light = MSR_RGB565(255, 202, 159);
    const uint16_t shadow = MSR_RGB565(171, 83, 91);
    const uint16_t pale = MSR_RGB565(255, 226, 185);
    msr_gradient(
        canvas,
        MSR_RGB565(41, 52, 72),
        MSR_RGB565(22, 31, 49));
    msr_triangle(canvas, 28, 39, 43, 4, 69, 32, shadow, 32U);
    msr_triangle(canvas, 132, 39, 117, 4, 91, 32, shadow, 32U);
    msr_triangle(canvas, 80, 8, 135, 42, 80, 114, paper, 32U);
    msr_triangle(canvas, 80, 8, 80, 114, 25, 42, light, 32U);
    msr_triangle(canvas, 25, 42, 80, 59, 38, 101, pale, 24U);
    msr_triangle(canvas, 135, 42, 122, 101, 80, 59, shadow, 19U);
    msr_draw_eyes(
        canvas,
        style,
        pose,
        pale,
        MSR_RGB565(73, 204, 196),
        ink,
        ink,
        true,
        false);
    msr_draw_brows(canvas, pose, shadow, 3, true);
    msr_triangle(canvas, 75, 55, 85, 55, 80, 65, ink, 32U);
    msr_draw_cheeks(
        canvas, pose, MSR_RGB565(232, 90, 105), true);
    msr_draw_origami_mouth(
        canvas,
        pose,
        shadow,
        ink,
        paper,
        light,
        shadow,
        pale,
        MSR_RGB565(225, 92, 117));
    const int32_t jaw_y = 112;
    msr_triangle(canvas, 42, 101, 80, jaw_y, 80, 86, light, 15U);
    msr_triangle(canvas, 118, 101, 80, jaw_y, 80, 86, shadow, 15U);
    msr_line(canvas, 80, 8, 80, 114, 1, ink, 14U);
    msr_line(canvas, 25, 42, 80, 59, 1, ink, 12U);
    msr_line(canvas, 135, 42, 80, 59, 1, ink, 12U);
    msr_line(
        canvas,
        38,
        101,
        pose->mouth_anchor_x[0],
        pose->mouth_anchor_y[0],
        1,
        ink,
        12U);
    msr_line(
        canvas,
        122,
        101,
        pose->mouth_anchor_x[1],
        pose->mouth_anchor_y[1],
        1,
        ink,
        12U);
}

static void msr_landmarks(
    face_mouth_study_redux_profile_t profile,
    const face_mouth_study_redux_pose_t *pose,
    face_mouth_study_redux_landmarks_t *landmarks)
{
    const msr_style_t *style = &MSR_STYLES[msr_style_index(profile)];
    landmarks->face = (face_mouth_study_redux_bounds_t){
        18, 4, 124, 112,
    };
    landmarks->left_eye = (face_mouth_study_redux_bounds_t){
        (uint16_t)(pose->left_eye_x - style->eye_radius_x - 5),
        (uint16_t)(pose->eye_y - style->eye_radius_y - 10),
        (uint16_t)(style->eye_radius_x * 2 + 10),
        (uint16_t)(style->eye_radius_y * 2 + 20),
    };
    landmarks->right_eye = landmarks->left_eye;
    landmarks->right_eye.x =
        (uint16_t)(pose->right_eye_x - style->eye_radius_x - 5);
    const int32_t mouth_x =
        pose->mouth_center_x - pose->mouth_width / 2 - 7;
    const int32_t mouth_y =
        pose->mouth_center_y - 12;
    landmarks->mouth = (face_mouth_study_redux_bounds_t){
        (uint16_t)msr_clamp(
            mouth_x, 0, FACE_MOUTH_STUDY_REDUX_WIDTH - 1),
        (uint16_t)msr_clamp(
            mouth_y, 0, FACE_MOUTH_STUDY_REDUX_HEIGHT - 1),
        (uint16_t)msr_clamp(
            pose->mouth_width + 14,
            1,
            FACE_MOUTH_STUDY_REDUX_WIDTH - mouth_x),
        (uint16_t)msr_clamp(
            pose->mouth_open + 27,
            1,
            FACE_MOUTH_STUDY_REDUX_HEIGHT - mouth_y - 2),
    };
    const int32_t jaw_y = msr_clamp(
        pose->mouth_center_y + 7,
        0,
        FACE_MOUTH_STUDY_REDUX_HEIGHT - 2);
    landmarks->jaw = (face_mouth_study_redux_bounds_t){
        38,
        (uint16_t)jaw_y,
        84,
        (uint16_t)msr_clamp(
            pose->jaw_drop + 22,
            1,
            FACE_MOUTH_STUDY_REDUX_HEIGHT - jaw_y - 1),
    };
}

bool face_mouth_study_redux_render_resolved(
    face_mouth_study_redux_profile_t profile,
    const face_mouth_study_redux_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_study_redux_landmarks_t *landmarks)
{
    if (!msr_valid_profile(profile) ||
        pose == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT) {
        return false;
    }
    msr_canvas_t canvas = {.pixels = rgb565};
    const msr_style_t *style = &MSR_STYLES[msr_style_index(profile)];
    switch (profile) {
    case FACE_MOUTH_STUDY_REDUX_PRESTON:
        msr_render_preston(&canvas, style, pose);
        break;
    case FACE_MOUTH_STUDY_REDUX_JALI:
        msr_render_jali(&canvas, style, pose);
        break;
    case FACE_MOUTH_STUDY_REDUX_RIBBON:
        msr_render_ribbon(&canvas, style, pose);
        break;
    case FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE:
        msr_render_teeth(&canvas, style, pose);
        break;
    case FACE_MOUTH_STUDY_REDUX_LED_VU:
        msr_render_led(&canvas, style, pose);
        break;
    case FACE_MOUTH_STUDY_REDUX_ORIGAMI:
        msr_render_origami(&canvas, style, pose);
        break;
    default:
        return false;
    }
    if (landmarks != NULL) {
        msr_landmarks(profile, pose, landmarks);
    }
    return true;
}

bool face_mouth_study_redux_render_checked(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity,
    face_mouth_study_redux_landmarks_t *landmarks)
{
    face_mouth_study_redux_pose_t pose;
    return face_mouth_study_redux_resolve(
               profile, render_key, sample_clock, &pose) &&
        face_mouth_study_redux_render_resolved(
               profile,
               &pose,
               rgb565,
               pixel_capacity,
               landmarks);
}

bool face_mouth_study_redux_render(
    face_mouth_study_redux_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    return face_mouth_study_redux_render_checked(
        profile,
        render_key,
        sample_clock,
        rgb565,
        pixel_capacity,
        NULL);
}
