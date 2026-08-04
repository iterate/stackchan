#include "face_robot_eyes.h"

#include <string.h>

#include "face_robot_eyes_core.h"
#include "face_stage.h"

/*
 * Clean-room Fable behavior/rasterizer integration. The core implementation
 * is kept in separate translation units so this adapter remains the sole
 * place where the legacy 12-byte activity ABI meets the 40-byte performance
 * IR. In particular, controls.expression is never read as an emotion.
 */

typedef struct {
    uint16_t open_scale_q8[2];
    int16_t brow_raise_q8[2];
    int16_t brow_tilt_q8[2];
    int16_t upper_lid_slope_q12[2];
    int16_t upper_lid_bend_q12[2];
    int16_t lower_lid_slope_q12[2];
    int16_t lower_lid_bend_q12[2];
    int16_t gaze_add_q8[2];
    uint16_t eye_scale_q8[2];
    int16_t tilt_mdeg;
    int16_t pupil_add_q8;
    int16_t arousal_add_q8;
} face_robot_expression_target_t;

/*
 * Eye acting presets. These are intentionally silhouette-first: expressions
 * remain readable on the brow-less Cozmo and Vector profiles through lids,
 * asymmetry, gaze, squash/stretch, and pupil posture.
 */
static const face_robot_expression_target_t
    EXPRESSION_TARGETS[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] = {
            {256, 256}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
            {0, 0}, {0, 0}, {256, 256}, 0, 0, 0,
        },
        [FACE_EXPRESSION_WARM] = {
            {224, 224}, {24, 24}, {28, 28}, {0, 0}, {0, 0}, {0, 0},
            {-260, -260}, {0, 10}, {264, 250}, -300, 8, 12,
        },
        [FACE_EXPRESSION_JOY] = {
            {158, 158}, {30, 30}, {10, 10}, {0, 0}, {0, 0}, {0, 0},
            {-1420, -1420}, {0, -8}, {276, 234}, 0, 14, 56,
        },
        [FACE_EXPRESSION_CONCERN] = {
            {246, 238}, {68, 62}, {146, 138}, {-420, 420},
            {0, 0}, {0, 0}, {96, 96}, {-18, 12},
            {250, 266}, -500, 4, 20,
        },
        [FACE_EXPRESSION_SURPRISE] = {
            {280, 280}, {166, 166}, {52, 52}, {0, 0}, {0, 0}, {0, 0},
            {0, 0}, {0, -18}, {238, 284}, 0, 42, 110,
        },
        [FACE_EXPRESSION_THOUGHTFUL] = {
            {226, 202}, {54, 30}, {92, -18}, {-180, 260},
            {0, 0}, {0, 0}, {0, 40}, {-72, -52},
            {252, 252}, -2600, -4, 6,
        },
        [FACE_EXPRESSION_SKEPTICAL] = {
            {218, 176}, {28, -14}, {82, -104}, {160, -380},
            {0, 0}, {0, 0}, {0, 42}, {48, -4},
            {264, 246}, -1800, -6, 16,
        },
        [FACE_EXPRESSION_DETERMINED] = {
            {194, 194}, {-72, -72}, {-174, -174}, {820, -820},
            {120, 120}, {0, 0}, {0, 0}, {0, -14},
            {276, 236}, 0, -12, 72,
        },
        [FACE_EXPRESSION_SLEEPY] = {
            {132, 124}, {-28, -34}, {18, 8}, {0, 0},
            {0, 0}, {0, 0}, {0, 0}, {14, 42},
            {248, 236}, 1000, -38, -68,
        },
        [FACE_EXPRESSION_EXCITED] = {
            {278, 272}, {154, 142}, {46, 38}, {0, 0}, {0, 0}, {0, 0},
            {-480, -480}, {-16, -22}, {286, 278}, 1200, 54, 120,
        },
        [FACE_EXPRESSION_EMBARRASSED] = {
            {176, 194}, {66, 46}, {124, 72}, {-180, 180},
            {0, 0}, {0, 0}, {-320, -260}, {76, 42},
            {256, 246}, 2200, 12, 34,
        },
    };

static const fre_profile_t PROFILE_MAP[FACE_ROBOT_EYES_PROFILE_COUNT] = {
    [FACE_ROBOT_EYES_VECTOR_ROUNDED] = FRE_PROFILE_VECTOR_ROUNDED,
    [FACE_ROBOT_EYES_COZMO_CUBIC] = FRE_PROFILE_COZMO_CUBIC,
    [FACE_ROBOT_EYES_BROW_DIALOGUE] = FRE_PROFILE_BROW_DIALOGUE,
    [FACE_ROBOT_EYES_SLEEP_WAKE] = FRE_PROFILE_SLEEP_WAKE,
    [FACE_ROBOT_EYES_IRIS_PARALLAX] = FRE_PROFILE_IRIS_PARALLAX,
    [FACE_ROBOT_EYES_CAT_OPTICS] = FRE_PROFILE_CAT_OPTICS,
    [FACE_ROBOT_EYES_M5_MANGA] = FRE_PROFILE_M5_AVATAR_MANGA,
};

static const char *const PROFILE_SLUGS[FACE_ROBOT_EYES_PROFILE_COUNT] = {
    [FACE_ROBOT_EYES_VECTOR_ROUNDED] = "robot_rig_vector_rounded",
    [FACE_ROBOT_EYES_COZMO_CUBIC] = "robot_rig_cozmo_cubic",
    [FACE_ROBOT_EYES_BROW_DIALOGUE] = "robot_rig_brow_dialogue",
    [FACE_ROBOT_EYES_SLEEP_WAKE] = "robot_rig_sleep_wake",
    [FACE_ROBOT_EYES_IRIS_PARALLAX] = "robot_rig_iris_parallax",
    [FACE_ROBOT_EYES_CAT_OPTICS] = "robot_rig_cat_optics",
    [FACE_ROBOT_EYES_M5_MANGA] = "robot_rig_m5_manga",
};

static const char *const PROFILE_NAMES[FACE_ROBOT_EYES_PROFILE_COUNT] = {
    [FACE_ROBOT_EYES_VECTOR_ROUNDED] = "Robot Rig · Vector Rounded",
    [FACE_ROBOT_EYES_COZMO_CUBIC] = "Robot Rig · Cozmo Cubic",
    [FACE_ROBOT_EYES_BROW_DIALOGUE] = "Robot Rig · Brow Dialogue",
    [FACE_ROBOT_EYES_SLEEP_WAKE] = "Robot Rig · Sleep / Wake",
    [FACE_ROBOT_EYES_IRIS_PARALLAX] = "Robot Rig · Iris Parallax",
    [FACE_ROBOT_EYES_CAT_OPTICS] = "Robot Rig · Cat Optics",
    [FACE_ROBOT_EYES_M5_MANGA] = "Robot Rig · M5 Manga",
};

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t weighted_i32(int32_t value, uint8_t weight)
{
    return (int32_t)((int64_t)value * weight / 255);
}

static int32_t scale_by_target(
    int32_t current, int32_t target_q8, uint8_t weight)
{
    const int32_t scale =
        256 + weighted_i32(target_q8 - 256, weight);
    return (int32_t)((int64_t)current * scale / 256);
}

static uint8_t blend_u8(uint8_t current, uint8_t target, uint8_t weight)
{
    return (uint8_t)clamp_i32(
        (int32_t)current +
            ((int32_t)target - current) * weight / 255,
        0,
        255);
}

static void floor_u8(uint8_t *value, uint8_t target, uint8_t weight)
{
    if (*value < target) {
        *value = blend_u8(*value, target, weight);
    }
}

static bool map_profile(
    face_robot_eyes_profile_t profile, fre_profile_t *mapped)
{
    if ((int)profile < 0 || profile >= FACE_ROBOT_EYES_PROFILE_COUNT ||
        mapped == NULL) {
        return false;
    }
    *mapped = PROFILE_MAP[profile];
    return true;
}

static void resolve_expression_mouth(
    uint8_t expression,
    uint8_t weight,
    face_keyframe_t *controls)
{
    if (weight < 8U || controls == NULL) {
        return;
    }
    switch (expression) {
    case FACE_EXPRESSION_WARM:
        floor_u8(&controls->mouth_width, 188U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 64U, weight);
        floor_u8(&controls->mouth_open, 18U, weight);
        break;
    case FACE_EXPRESSION_JOY:
        floor_u8(&controls->mouth_width, 224U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 38U, weight);
        floor_u8(&controls->mouth_open, 62U, weight);
        floor_u8(&controls->mouth_teeth, 138U, weight);
        break;
    case FACE_EXPRESSION_CONCERN:
        controls->mouth_width =
            blend_u8(controls->mouth_width, 126U, weight);
        floor_u8(&controls->mouth_round, 92U, weight);
        floor_u8(&controls->mouth_press, 82U, weight);
        break;
    case FACE_EXPRESSION_SURPRISE:
        floor_u8(&controls->mouth_open, 196U, weight);
        controls->mouth_width =
            blend_u8(controls->mouth_width, 108U, weight);
        floor_u8(&controls->mouth_round, 216U, weight);
        controls->mouth_press =
            blend_u8(controls->mouth_press, 0U, weight);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        controls->mouth_width =
            blend_u8(controls->mouth_width, 142U, weight);
        floor_u8(&controls->mouth_round, 102U, weight);
        floor_u8(&controls->mouth_press, 98U, weight);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        controls->mouth_width =
            blend_u8(controls->mouth_width, 138U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 54U, weight);
        floor_u8(&controls->mouth_press, 126U, weight);
        break;
    case FACE_EXPRESSION_DETERMINED:
        controls->mouth_width =
            blend_u8(controls->mouth_width, 154U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 34U, weight);
        floor_u8(&controls->mouth_press, 182U, weight);
        break;
    case FACE_EXPRESSION_SLEEPY:
        controls->mouth_width =
            blend_u8(controls->mouth_width, 138U, weight);
        floor_u8(&controls->mouth_round, 76U, weight);
        break;
    case FACE_EXPRESSION_EXCITED:
        floor_u8(&controls->mouth_open, 176U, weight);
        floor_u8(&controls->mouth_width, 232U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 52U, weight);
        floor_u8(&controls->mouth_teeth, 184U, weight);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        floor_u8(&controls->mouth_width, 178U, weight);
        controls->mouth_round =
            blend_u8(controls->mouth_round, 78U, weight);
        floor_u8(&controls->mouth_open, 28U, weight);
        break;
    default:
        break;
    }
}

static void apply_named_expression(
    const face_robot_expression_target_t *target,
    uint8_t weight,
    fre_rig_t *rig)
{
    if (target == NULL || rig == NULL || weight == 0U) {
        return;
    }
    for (int eye = 0; eye < 2; ++eye) {
        rig->openness_q8[eye] = scale_by_target(
            rig->openness_q8[eye],
            target->open_scale_q8[eye],
            weight);
        rig->brow_raise_q8[eye] +=
            weighted_i32(target->brow_raise_q8[eye], weight);
        rig->brow_tilt_q8[eye] +=
            weighted_i32(target->brow_tilt_q8[eye], weight);
        rig->upper_lid_slope_q12[eye] +=
            weighted_i32(target->upper_lid_slope_q12[eye], weight);
        rig->upper_lid_bend_q12[eye] +=
            weighted_i32(target->upper_lid_bend_q12[eye], weight);
        rig->lower_lid_slope_q12[eye] +=
            weighted_i32(target->lower_lid_slope_q12[eye], weight);
        rig->lower_lid_bend_q12[eye] +=
            weighted_i32(target->lower_lid_bend_q12[eye], weight);
    }
    rig->gaze_x_q8 += weighted_i32(target->gaze_add_q8[0], weight);
    rig->gaze_y_q8 += weighted_i32(target->gaze_add_q8[1], weight);
    rig->lid_gaze_y_q8 +=
        weighted_i32(target->gaze_add_q8[1], weight);
    rig->scale_x_q8 = scale_by_target(
        rig->scale_x_q8, target->eye_scale_q8[0], weight);
    rig->scale_y_q8 = scale_by_target(
        rig->scale_y_q8, target->eye_scale_q8[1], weight);
    rig->tilt_mdeg += weighted_i32(target->tilt_mdeg, weight);
    rig->pupil_q8 += weighted_i32(target->pupil_add_q8, weight);
    rig->arousal_q8 += weighted_i32(target->arousal_add_q8, weight);
}

static void apply_dense_actions(
    const face_render_key_t *key, fre_rig_t *rig)
{
    const uint8_t squint[2] = {
        key->eye_left_squint,
        key->eye_right_squint,
    };
    const int8_t outer[2] = {
        key->brow_outer_left,
        key->brow_outer_right,
    };
    for (int eye = 0; eye < 2; ++eye) {
        const int32_t squint_scale =
            256 - ((int32_t)squint[eye] * 192) / 255;
        rig->openness_q8[eye] =
            (rig->openness_q8[eye] * squint_scale) >> 8;
        rig->brow_raise_q8[eye] +=
            (int32_t)key->brow_inner + outer[eye];
        rig->brow_tilt_q8[eye] +=
            (int32_t)key->brow_inner - outer[eye];
        rig->lower_lid_bend_q12[eye] -=
            (int32_t)key->cheek * 4;
    }

    const int32_t valence = key->affect_valence;
    if (valence > 0) {
        rig->lower_lid_bend_q12[0] -= valence * 3;
        rig->lower_lid_bend_q12[1] -= valence * 3;
    } else if (valence < 0) {
        rig->brow_tilt_q8[0] += valence;
        rig->brow_tilt_q8[1] += valence;
    }
    if (key->affect_arousal != 0U) {
        rig->arousal_q8 +=
            ((int32_t)key->affect_arousal - rig->arousal_q8) / 2;
    }
    if (key->attention != 0U) {
        rig->arousal_q8 +=
            ((int32_t)key->attention - rig->arousal_q8) / 3;
    }

    rig->gaze_x_q8 += (int32_t)key->head_yaw;
    rig->gaze_y_q8 += (int32_t)key->head_pitch;
    rig->lid_gaze_y_q8 += (int32_t)key->head_pitch;
    rig->tilt_mdeg += (int32_t)key->head_roll * 70 +
        (int32_t)key->body_lean_x * 22;
    rig->breath_y_q8 += (int32_t)key->body_lean_y * 2;

    /*
     * PCM stays in charge of speech timing. Energy and jaw motion only add
     * a small emphasis pulse to the eye performance; authored emotion never
     * substitutes a delayed or synthetic audio envelope.
     */
    const bool speaking =
        (key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        key->controls.expression == FACE_ACTIVITY_SPEAKING ||
        key->speech_phase == FACE_SPEECH_ACTIVE ||
        key->speech_phase == FACE_SPEECH_STARTING;
    if (speaking) {
        int32_t emphasis =
            ((int32_t)key->audio_level + key->controls.mouth_open) / 2;
        if (key->viseme_weight != 0U) {
            emphasis =
                (emphasis * (128 + key->viseme_weight / 2)) / 255;
        }
        rig->brow_raise_q8[0] += emphasis / 9;
        rig->brow_raise_q8[1] += emphasis / 9;
        rig->scale_y_q8 += emphasis / 22;
        rig->arousal_q8 += emphasis / 7;
    }
}

static void clamp_rig(fre_rig_t *rig)
{
    rig->gaze_x_q8 = clamp_i32(rig->gaze_x_q8, -256, 256);
    rig->gaze_y_q8 = clamp_i32(rig->gaze_y_q8, -256, 256);
    rig->lid_gaze_y_q8 =
        clamp_i32(rig->lid_gaze_y_q8, -256, 256);
    for (int eye = 0; eye < 2; ++eye) {
        rig->openness_q8[eye] =
            clamp_i32(rig->openness_q8[eye], 0, 280);
        rig->brow_raise_q8[eye] =
            clamp_i32(rig->brow_raise_q8[eye], -256, 256);
        rig->brow_tilt_q8[eye] =
            clamp_i32(rig->brow_tilt_q8[eye], -256, 256);
        rig->upper_lid_slope_q12[eye] = clamp_i32(
            rig->upper_lid_slope_q12[eye], -1800, 1800);
        rig->upper_lid_bend_q12[eye] = clamp_i32(
            rig->upper_lid_bend_q12[eye], -1800, 1800);
        rig->lower_lid_slope_q12[eye] = clamp_i32(
            rig->lower_lid_slope_q12[eye], -1800, 1800);
        rig->lower_lid_bend_q12[eye] = clamp_i32(
            rig->lower_lid_bend_q12[eye], -1800, 1800);
    }
    rig->scale_x_q8 = clamp_i32(rig->scale_x_q8, 184, 300);
    rig->scale_y_q8 = clamp_i32(rig->scale_y_q8, 176, 300);
    rig->breath_y_q8 = clamp_i32(rig->breath_y_q8, -640, 640);
    rig->tilt_mdeg = clamp_i32(rig->tilt_mdeg, -12000, 12000);
    rig->pupil_q8 = clamp_i32(rig->pupil_q8, 40, 230);
    rig->arousal_q8 = clamp_i32(rig->arousal_q8, 0, 256);
}

static void copy_rig_to_pose(
    const fre_rig_t *rig, face_robot_eyes_pose_t *pose)
{
    pose->gaze_x_q8 = rig->gaze_x_q8;
    pose->gaze_y_q8 = rig->gaze_y_q8;
    pose->lid_gaze_y_q8 = rig->lid_gaze_y_q8;
    for (int eye = 0; eye < 2; ++eye) {
        pose->openness_q8[eye] = rig->openness_q8[eye];
        pose->brow_raise_q8[eye] = rig->brow_raise_q8[eye];
        pose->brow_tilt_q8[eye] = rig->brow_tilt_q8[eye];
        pose->upper_lid_slope_q12[eye] =
            rig->upper_lid_slope_q12[eye];
        pose->upper_lid_bend_q12[eye] =
            rig->upper_lid_bend_q12[eye];
        pose->lower_lid_slope_q12[eye] =
            rig->lower_lid_slope_q12[eye];
        pose->lower_lid_bend_q12[eye] =
            rig->lower_lid_bend_q12[eye];
    }
    pose->scale_x_q8 = rig->scale_x_q8;
    pose->scale_y_q8 = rig->scale_y_q8;
    pose->breath_y_q8 = rig->breath_y_q8;
    pose->tilt_mdeg = rig->tilt_mdeg;
    pose->pupil_q8 = rig->pupil_q8;
    pose->arousal_q8 = rig->arousal_q8;
    pose->act_id = rig->act_id;
    pose->saccade_active = rig->saccade_active ? 1U : 0U;
}

static void copy_pose_to_rig(
    const face_robot_eyes_pose_t *pose, fre_rig_t *rig)
{
    memset(rig, 0, sizeof(*rig));
    rig->gaze_x_q8 = pose->gaze_x_q8;
    rig->gaze_y_q8 = pose->gaze_y_q8;
    rig->lid_gaze_y_q8 = pose->lid_gaze_y_q8;
    for (int eye = 0; eye < 2; ++eye) {
        rig->openness_q8[eye] = pose->openness_q8[eye];
        rig->brow_raise_q8[eye] = pose->brow_raise_q8[eye];
        rig->brow_tilt_q8[eye] = pose->brow_tilt_q8[eye];
        rig->upper_lid_slope_q12[eye] =
            pose->upper_lid_slope_q12[eye];
        rig->upper_lid_bend_q12[eye] =
            pose->upper_lid_bend_q12[eye];
        rig->lower_lid_slope_q12[eye] =
            pose->lower_lid_slope_q12[eye];
        rig->lower_lid_bend_q12[eye] =
            pose->lower_lid_bend_q12[eye];
    }
    rig->scale_x_q8 = pose->scale_x_q8;
    rig->scale_y_q8 = pose->scale_y_q8;
    rig->breath_y_q8 = pose->breath_y_q8;
    rig->tilt_mdeg = pose->tilt_mdeg;
    rig->pupil_q8 = pose->pupil_q8;
    rig->arousal_q8 = pose->arousal_q8;
    rig->act_id = pose->act_id;
    rig->saccade_active = pose->saccade_active != 0U;
}

size_t face_robot_eyes_profile_count(void)
{
    return FACE_ROBOT_EYES_PROFILE_COUNT;
}

const char *face_robot_eyes_profile_slug(face_robot_eyes_profile_t profile)
{
    if ((int)profile < 0 || profile >= FACE_ROBOT_EYES_PROFILE_COUNT) {
        return NULL;
    }
    return PROFILE_SLUGS[profile];
}

const char *face_robot_eyes_profile_name(face_robot_eyes_profile_t profile)
{
    if ((int)profile < 0 || profile >= FACE_ROBOT_EYES_PROFILE_COUNT) {
        return NULL;
    }
    return PROFILE_NAMES[profile];
}

bool face_robot_eyes_resolve(
    face_robot_eyes_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_robot_eyes_pose_t *pose)
{
    fre_profile_t mapped;
    if (!map_profile(profile, &mapped) ||
        render_key == NULL || pose == NULL) {
        return false;
    }

    memset(pose, 0, sizeof(*pose));
    pose->source = *render_key;
    pose->resolved_controls = render_key->controls;
    pose->activity = render_key->controls.expression;

    uint8_t expression = FACE_EXPRESSION_NEUTRAL;
    uint8_t weight = 0U;
    if (render_key->schema_version >= FACE_RENDER_KEY_SCHEMA_VERSION &&
        render_key->stage_expression < FACE_EXPRESSION_COUNT) {
        expression = render_key->stage_expression;
        weight = render_key->expression_weight;
    }
    pose->stage_expression = expression;

    const int32_t corner_average =
        ((int32_t)render_key->mouth_corner_left +
         render_key->mouth_corner_right) /
        2;
    pose->resolved_controls.mouth_width = (uint8_t)clamp_i32(
        (int32_t)pose->resolved_controls.mouth_width +
            corner_average / 2,
        0,
        255);
    pose->resolved_controls.mouth_round = (uint8_t)clamp_i32(
        (int32_t)pose->resolved_controls.mouth_round -
            corner_average / 3,
        0,
        255);
    resolve_expression_mouth(
        expression, weight, &pose->resolved_controls);

    fre_keyframe_t legacy;
    _Static_assert(
        sizeof(legacy) == sizeof(pose->resolved_controls),
        "robot-eyes legacy control prefix must remain 12 bytes");
    memcpy(&legacy, &pose->resolved_controls, sizeof(legacy));

    fre_rig_t rig;
    if (!fre_behavior_solve(mapped, &legacy, sample_clock, &rig)) {
        return false;
    }
    apply_named_expression(
        &EXPRESSION_TARGETS[expression], weight, &rig);
    apply_dense_actions(render_key, &rig);
    clamp_rig(&rig);
    copy_rig_to_pose(&rig, pose);
    return true;
}

bool face_robot_eyes_render_resolved(
    face_robot_eyes_profile_t profile,
    const face_robot_eyes_pose_t *pose,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    fre_profile_t mapped;
    if (!map_profile(profile, &mapped) ||
        pose == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_ROBOT_EYES_PIXEL_COUNT) {
        return false;
    }
    fre_keyframe_t legacy;
    memcpy(&legacy, &pose->resolved_controls, sizeof(legacy));
    fre_rig_t rig;
    copy_pose_to_rig(pose, &rig);
    return fre_render_resolved_frame(
        mapped, &legacy, &rig, rgb565, pixel_capacity);
}

bool face_robot_eyes_render(
    face_robot_eyes_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_robot_eyes_pose_t pose;
    return face_robot_eyes_resolve(
               profile, render_key, sample_clock, &pose) &&
        face_robot_eyes_render_resolved(
               profile, &pose, rgb565, pixel_capacity);
}
