#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * fable_robot_eyes — standalone procedural robot-face renderer contribution.
 *
 * Sixteen eye-forward face profiles informed by Anki Cozmo/Vector
 * parameterized eyes, FluxGarage RoboEyes, m5stack-avatar/Stack-chan,
 * EVE-like minimal displays, Adafruit Uncanny Eyes, and the oculomotor and
 * social-gaze literature (blink kinematics, saccade main sequence,
 * microsaccades, lid-gaze coupling, conversational gaze aversion).
 *
 * Contract, mirrored from the firmware's face_render.h:
 *   - one 160x120 RGB565 frame per call into a caller-owned buffer;
 *   - a renderer is a pure function of (profile, keyframe, sample clock);
 *   - integer arithmetic only, no allocation, no retained state, so native
 *     and WebAssembly builds are byte-identical;
 *   - the 16 kHz sample clock alone drives blinks, saccades, brow motion,
 *     breathing, micro-fixational jitter, and rare idle acts.
 *
 * The 12-byte keyframe below is layout-identical to the firmware's
 * face_keyframe_t so the module can be grafted without a translation shim.
 */

enum {
    FRE_FRAME_WIDTH = 160,
    FRE_FRAME_HEIGHT = 120,
    FRE_FRAME_PIXEL_COUNT = FRE_FRAME_WIDTH * FRE_FRAME_HEIGHT,
    FRE_FRAME_BYTES = FRE_FRAME_PIXEL_COUNT * (int)sizeof(uint16_t),
    FRE_SAMPLE_RATE_HZ = 16000,
};

typedef struct {
    uint8_t mouth_open;
    uint8_t mouth_width;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t mouth_teeth;
    uint8_t eye_left_open;
    uint8_t eye_right_open;
    int8_t look_x;
    int8_t look_y;
    int8_t brow;
    uint8_t expression;
    uint8_t flags;
} fre_keyframe_t;

enum {
    FRE_KEYFRAME_FLAG_SPEAKING = 1U << 0,
    FRE_KEYFRAME_FLAG_BLINKING = 1U << 1,
    FRE_KEYFRAME_BYTES = 12,
};

_Static_assert(
    sizeof(fre_keyframe_t) == FRE_KEYFRAME_BYTES,
    "keyframe wire format must remain exactly 12 bytes");

/* keyframe.expression carries the firmware's face_activity_t. */
typedef enum {
    FRE_ACTIVITY_IDLE = 0,
    FRE_ACTIVITY_LISTENING = 1,
    FRE_ACTIVITY_THINKING = 2,
    FRE_ACTIVITY_SPEAKING = 3,
} fre_activity_t;

typedef enum {
    /* ROBOT family: complete faces in the parametric robot lineage. */
    FRE_PROFILE_VECTOR_ROUNDED = 0,
    FRE_PROFILE_COZMO_CUBIC,
    FRE_PROFILE_ROBOEYES_ALERT,
    FRE_PROFILE_ROBOEYES_SOFT,
    FRE_PROFILE_M5_AVATAR_CLASSIC,
    FRE_PROFILE_M5_AVATAR_MANGA,
    FRE_PROFILE_EVE_MINIMAL,
    FRE_PROFILE_JIBO_ORB,
    /* EYES family: studies that push one observed behavior to the front. */
    FRE_PROFILE_SACCADE_LAB,
    FRE_PROFILE_BROW_DIALOGUE,
    FRE_PROFILE_LID_ANTICIPATION,
    FRE_PROFILE_IRIS_PARALLAX,
    FRE_PROFILE_SLEEP_WAKE,
    FRE_PROFILE_CURIOUS_TILT,
    FRE_PROFILE_DOT_MATRIX_EYES,
    FRE_PROFILE_CAT_OPTICS,
    FRE_PROFILE_COUNT,
} fre_profile_t;

/* Families and mouth kinds reuse the firmware's face_render.h numbering. */
enum {
    FRE_FAMILY_ROBOT = 1,
    FRE_FAMILY_EYES = 2,
};

enum {
    FRE_MOUTH_NONE = 0,
    FRE_MOUTH_ELLIPSE = 2,
    FRE_MOUTH_LINE = 4,
};

enum {
    FRE_FLAG_PIXELATED = 1U << 0,
    FRE_FLAG_EYE_FOCUS = 1U << 2,
    FRE_FLAG_IDLE_MOTION = 1U << 5,
    FRE_FLAG_NO_MOUTH = 1U << 7,
};

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t work_width;
    uint16_t work_height;
    uint16_t framebuffer_bytes;
    uint8_t family;
    uint8_t mouth_kind;
    uint8_t flags;
    uint8_t reserved;
    uint16_t estimated_ops_per_pixel;
} fre_profile_info_t;

_Static_assert(
    sizeof(fre_profile_info_t) == 16,
    "renderer metadata ABI must remain exactly 16 bytes");

/* Idle acts the deterministic scheduler can run. Exposed for tests. */
enum {
    FRE_ACT_NONE = 0,
    FRE_ACT_GLANCE_ASIDE = 1,
    FRE_ACT_LOOK_UP_THINK = 2,
    FRE_ACT_SQUINT = 3,
    FRE_ACT_BROW_FLASH = 4,
    FRE_ACT_WINK = 5,
    FRE_ACT_DRIFT_REFOCUS = 6,
    FRE_ACT_SHIVER = 7,
    FRE_ACT_SLOW_BLINK = 8,
    FRE_ACT_TILT = 9,
};

/*
 * Resolved behavior rig: the stateless behavior engine's output for one
 * instant, before any pixels are drawn. Exposed so tests can assert on
 * blink kinematics, saccade timing, lid anticipation, and social gaze
 * without rasterizing, and so other renderers could be driven by the same
 * solver. Q8 means 256 == 1.0 of the named unit.
 */
typedef struct {
    /* Gaze offset of the eye pair, -256..256 of maximum travel. */
    int32_t gaze_x_q8;
    int32_t gaze_y_q8;
    /* Where the lids believe gaze is heading: leads gaze_y_q8 by the
     * profile's anticipation lookahead, so lids move before the eyes. */
    int32_t lid_gaze_y_q8;
    /* Per-eye aperture, 0..~280 (values above 256 are reopen overshoot).
     * Index 0 is the viewer-left eye. */
    int32_t openness_q8[2];
    /* Per-eye brow raise (+) / lower (-), roughly -256..256. */
    int32_t brow_raise_q8[2];
    /* Per-eye brow inner-end lift (+) / knit (-), roughly -256..256. */
    int32_t brow_tilt_q8[2];
    /*
     * Authored lid-shape offsets. These are zero in the autonomous solve
     * and are populated by the production 40-byte expression adapter.
     * They keep emotion in the eye silhouette for brow-less robot faces.
     */
    int32_t upper_lid_slope_q12[2];
    int32_t upper_lid_bend_q12[2];
    int32_t lower_lid_slope_q12[2];
    int32_t lower_lid_bend_q12[2];
    /* Eye-pair squash and stretch scales, 256 == neutral. */
    int32_t scale_x_q8;
    int32_t scale_y_q8;
    /* Whole-face breathing lift in Q8 pixels (positive moves down). */
    int32_t breath_y_q8;
    /* Whole-face tilt in milli-degrees, positive rotates clockwise. */
    int32_t tilt_mdeg;
    /* Pupil/iris dilation, 0..256 neutral 128 (also cat slit width). */
    int32_t pupil_q8;
    /* Attention level 0..256: drives pupil, posture, act gating. */
    int32_t arousal_q8;
    /* A saccade is in flight this instant (drives squash + evoked blinks). */
    bool saccade_active;
    /* Idle act currently running (FRE_ACT_*), FRE_ACT_NONE when quiet. */
    uint8_t act_id;
} fre_rig_t;

size_t fre_profile_count(void);
const char *fre_profile_slug(fre_profile_t profile);
const char *fre_profile_name(fre_profile_t profile);
const char *fre_profile_family_name(fre_profile_t profile);
bool fre_profile_info(fre_profile_t profile, fre_profile_info_t *info);

/*
 * Solve the behavior rig for one instant without rendering. `profile`
 * selects behavior tuning (a sleepy profile blinks differently from an
 * alert one); pixel style is not consulted.
 */
bool fre_behavior_solve(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    uint32_t sample_clock,
    fre_rig_t *rig);

/*
 * Rasterize an already-resolved rig. This production integration seam lets
 * the 40-byte face_render_key_t adapter add authored emotion and facial
 * actions after the autonomous oculomotor solve, without duplicating the
 * fixed-point rasterizer or reinterpreting the legacy activity byte.
 */
bool fre_render_resolved_frame(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    const fre_rig_t *rig,
    uint16_t *rgb565,
    size_t pixel_capacity);

/*
 * Render exactly FRE_FRAME_WIDTH x FRE_FRAME_HEIGHT RGB565 pixels.
 * `sample_clock` is in 16 kHz PCM samples, even for silent/idle previews.
 * Returns false on NULL arguments, unknown profile, or insufficient
 * `pixel_capacity` (must be >= FRE_FRAME_PIXEL_COUNT).
 */
bool fre_render_frame(
    fre_profile_t profile,
    const fre_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity);
