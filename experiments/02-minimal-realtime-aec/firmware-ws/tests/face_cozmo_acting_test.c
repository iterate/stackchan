#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_cozmo_acting.h"
#include "face_stage.h"

/*
 * Acceptance tests for the face_cozmo_acting module.
 *
 * These encode the review bar as hard gates, not vibes:
 *   1.  strict containment: nothing but pure background in the outer
 *       frame ring, across every expression, extreme pose, and a large
 *       all-byte fuzz sweep;
 *   2.  bilateral eye mass: every one of the eleven stage expressions
 *       must light both eye zones with real pixel mass AND a bright
 *       emissive band or seam — no stub, cyclops, or blank cells;
 *   3.  expression separability using the production QA ROI metric;
 *   4.  30 fps temporal smoothness with no abrupt jumps and bounded
 *       autonomous aperture steps (no two-frame blink cuts);
 *   5.  semantic layering: activity is never emotion, v1 records leave
 *       the extended bytes inert, and each consumed IR field visibly
 *       changes the frame;
 *   6.  bit-exact determinism of the full render path.
 */

enum {
    W = FACE_COZMO_ACTING_WIDTH,
    H = FACE_COZMO_ACTING_HEIGHT,
    PIXELS = FACE_COZMO_ACTING_PIXEL_COUNT,
    SAFE = FACE_COZMO_ACTING_SAFE_MARGIN,
    SAMPLE_RATE = 16000,
    FPS = 30,
    FIXED_CLOCK = SAMPLE_RATE * 7 + 211,
    GUARD_WORDS = 12,
    FUZZ_ITERATIONS = 4000,
    MOTION_FRAMES = 240,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[PIXELS];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static guarded_frame_t g_frame;
static uint16_t g_expression_frames[FACE_EXPRESSION_COUNT][PIXELS];
static uint16_t g_previous[PIXELS];
static uint16_t g_current[PIXELS];

static uint32_t g_checks;

static void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", what);
        abort();
    }
}

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < (size_t)PIXELS; ++i) {
        hash ^= pixels[i];
        hash *= 16777619U;
    }
    return hash;
}

static void guard_init(guarded_frame_t *frame)
{
    memset(frame, 0xA5, sizeof(*frame));
    for (size_t i = 0; i < GUARD_WORDS; ++i) {
        frame->before[i] = 0xFACE1000U + (uint32_t)i;
        frame->after[i] = 0xC0DE2000U + (uint32_t)i;
    }
}

static void guard_check(const guarded_frame_t *frame)
{
    for (size_t i = 0; i < GUARD_WORDS; ++i) {
        check(frame->before[i] == 0xFACE1000U + i, "front guard intact");
        check(frame->after[i] == 0xC0DE2000U + i, "rear guard intact");
    }
}

static uint8_t red8(uint16_t p)
{
    const uint8_t v = (uint8_t)((p >> 11) & 0x1FU);
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint8_t green8(uint16_t p)
{
    const uint8_t v = (uint8_t)((p >> 5) & 0x3FU);
    return (uint8_t)((v << 2) | (v >> 4));
}

static uint8_t blue8(uint16_t p)
{
    const uint8_t v = (uint8_t)(p & 0x1FU);
    return (uint8_t)((v << 3) | (v >> 2));
}

static uint32_t luma(uint16_t p)
{
    return (uint32_t)red8(p) + green8(p) + blue8(p);
}

/* Any energy at all: catches lid shells and dim sleepy bands. */
static bool pixel_lit(uint16_t p)
{
    return luma(p) >= 18U;
}

/* Clearly emissive: the luminous band/seam the reviews demand. */
static bool pixel_bright(uint16_t p)
{
    return luma(p) >= 240U;
}

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 152U;
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 76U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_render_key_t expression_key(face_expression_t expression)
{
    face_render_key_t key = base_key();
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    check(
        face_stage_cue_apply(&cue, FIXED_CLOCK, &key),
        "stage cue applies");
    return key;
}

static void render_checked(
    face_cozmo_acting_profile_t profile,
    const face_render_key_t *key,
    uint32_t clock,
    uint16_t *pixels)
{
    guard_init(&g_frame);
    check(
        face_cozmo_acting_render(
            profile, key, clock, g_frame.pixels, PIXELS),
        "render succeeds");
    guard_check(&g_frame);
    memcpy(pixels, g_frame.pixels, sizeof(g_frame.pixels));
}

/* The outer SAFE-pixel ring must be exactly black, always. */
static void check_ring_black(const uint16_t *pixels, const char *what)
{
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool in_ring =
                x < SAFE || x >= W - SAFE || y < SAFE || y >= H - SAFE;
            if (!in_ring) {
                continue;
            }
            if (pixels[y * W + x] != 0x0000U) {
                fprintf(
                    stderr, "lit ring pixel at (%d,%d) in %s\n",
                    x, y, what);
                abort();
            }
        }
    }
    ++g_checks;
}

/* ---- 1. metadata and rejection ---------------------------------------- */

static void test_metadata(void)
{
    check(
        face_cozmo_acting_profile_count() ==
            FACE_COZMO_ACTING_PROFILE_COUNT,
        "profile count");
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        const char *slug = face_cozmo_acting_profile_slug(profile);
        const char *name = face_cozmo_acting_profile_name(profile);
        check(slug != NULL && slug[0] != '\0', "slug present");
        check(name != NULL && name[0] != '\0', "name present");
        for (size_t earlier = 0; earlier < raw; ++earlier) {
            check(
                strcmp(
                    slug,
                    face_cozmo_acting_profile_slug(
                        (face_cozmo_acting_profile_t)earlier)) != 0,
                "slug unique");
        }
    }
    check(
        face_cozmo_acting_profile_slug(
            (face_cozmo_acting_profile_t)-1) == NULL,
        "negative profile rejected");
    check(
        face_cozmo_acting_profile_name(
            FACE_COZMO_ACTING_PROFILE_COUNT) == NULL,
        "overflow profile rejected");

    const face_render_key_t key = base_key();
    face_cozmo_acting_pose_t pose;
    uint16_t tiny[16];
    check(
        !face_cozmo_acting_resolve(
            FACE_COZMO_ACTING_PROFILE_COUNT, &key, 0, &pose),
        "resolve rejects bad profile");
    check(
        !face_cozmo_acting_resolve(
            FACE_COZMO_ACTING_STAGE, NULL, 0, &pose),
        "resolve rejects NULL key");
    check(
        !face_cozmo_acting_resolve(
            FACE_COZMO_ACTING_STAGE, &key, 0, NULL),
        "resolve rejects NULL pose");
    check(
        !face_cozmo_acting_render(
            FACE_COZMO_ACTING_STAGE, &key, 0, tiny, 16),
        "render rejects small buffer");
    check(
        !face_cozmo_acting_render(
            FACE_COZMO_ACTING_STAGE, &key, 0, NULL, PIXELS),
        "render rejects NULL buffer");
}

/* ---- 2. determinism ---------------------------------------------------- */

static void test_determinism(void)
{
    const face_render_key_t key = expression_key(FACE_EXPRESSION_JOY);
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        render_checked(profile, &key, FIXED_CLOCK, g_previous);
        render_checked(profile, &key, FIXED_CLOCK, g_current);
        check(
            memcmp(g_previous, g_current, sizeof(g_previous)) == 0,
            "same inputs are pixel-identical");

        face_cozmo_acting_pose_t pose;
        check(
            face_cozmo_acting_resolve(
                profile, &key, FIXED_CLOCK, &pose),
            "resolve succeeds");
        guard_init(&g_frame);
        check(
            face_cozmo_acting_render_resolved(
                profile, &pose, g_frame.pixels, PIXELS),
            "render_resolved succeeds");
        guard_check(&g_frame);
        check(
            memcmp(g_frame.pixels, g_previous, sizeof(g_previous)) == 0,
            "resolve+render_resolved matches render");

        render_checked(
            profile, &key, FIXED_CLOCK + SAMPLE_RATE * 3U, g_current);
        check(
            memcmp(g_previous, g_current, sizeof(g_previous)) != 0,
            "the clock alone creates life");
    }
}

/* ---- 3. bilateral eye mass and readable emissive geometry ------------- */

static void measure_side(
    const uint16_t *pixels,
    int x0,
    int x1,
    uint32_t *lit,
    uint32_t *bright)
{
    *lit = 0;
    *bright = 0;
    /* The eye theater: above the mouth zone. */
    for (int y = 8; y < 92; ++y) {
        for (int x = x0; x < x1; ++x) {
            const uint16_t p = pixels[y * W + x];
            *lit += pixel_lit(p);
            *bright += pixel_bright(p);
        }
    }
}

static void test_bilateral_mass(void)
{
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        for (size_t expression = 0;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key =
                expression_key((face_expression_t)expression);
            render_checked(profile, &key, FIXED_CLOCK, g_current);
            check_ring_black(
                g_current, face_cozmo_acting_profile_slug(profile));

            uint32_t lit_left;
            uint32_t bright_left;
            uint32_t lit_right;
            uint32_t bright_right;
            measure_side(g_current, SAFE, W / 2, &lit_left, &bright_left);
            measure_side(
                g_current, W / 2, W - SAFE, &lit_right, &bright_right);

            char what[128];
            snprintf(
                what, sizeof(what), "%s expr %zu bilateral mass "
                "(L %u/%u, R %u/%u)",
                face_cozmo_acting_profile_slug(profile), expression,
                lit_left, bright_left, lit_right, bright_right);
            /* Both sides must carry substantial lit area... */
            check(lit_left >= 260 && lit_right >= 260, what);
            /* ...and a clearly emissive band/seam, never a void. */
            check(bright_left >= 40 && bright_right >= 40, what);
            /* Asymmetry is bounded: the smaller eye keeps at least a
             * third of its partner's lit mass. */
            const uint32_t lo =
                lit_left < lit_right ? lit_left : lit_right;
            const uint32_t hi =
                lit_left < lit_right ? lit_right : lit_left;
            check(lo * 3 >= hi, what);

            face_cozmo_acting_pose_t pose;
            check(
                face_cozmo_acting_resolve(
                    profile, &key, FIXED_CLOCK, &pose),
                "resolve for canonical fixture");
            check(
                pose.governor_engaged == 0U,
                "canonical fixtures fit without the governor");
        }
    }
}

/* ---- 4. expression separability (production QA metric) ---------------- */

typedef struct {
    double roi_delta;
    double roi_changed;
} roi_diff_t;

static roi_diff_t roi_compare(const uint16_t *a, const uint16_t *b)
{
    uint64_t delta = 0;
    size_t changed = 0;
    size_t count = 0;
    for (int y = 10; y < H - 10; ++y) {
        for (int x = 16; x < W - 16; ++x) {
            const uint16_t pa = a[y * W + x];
            const uint16_t pb = b[y * W + x];
            delta += (uint64_t)(
                abs((int)red8(pa) - red8(pb)) +
                abs((int)green8(pa) - green8(pb)) +
                abs((int)blue8(pa) - blue8(pb)));
            changed += pa != pb;
            ++count;
        }
    }
    roi_diff_t diff;
    diff.roi_delta = (double)delta / ((double)count * 3.0 * 255.0);
    diff.roi_changed = (double)changed / (double)count;
    return diff;
}

static void test_separability(void)
{
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (size_t expression = 0;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key =
                expression_key((face_expression_t)expression);
            render_checked(
                profile, &key, FIXED_CLOCK,
                g_expression_frames[expression]);
            hashes[expression] =
                frame_hash(g_expression_frames[expression]);
        }
        for (size_t a = 0; a < FACE_EXPRESSION_COUNT; ++a) {
            for (size_t b = a + 1; b < FACE_EXPRESSION_COUNT; ++b) {
                check(hashes[a] != hashes[b], "all expressions distinct");
            }
        }
        uint32_t clear = 0;
        double mean_pair = 0.0;
        uint32_t pairs = 0;
        double min_nonneutral = 1.0;
        for (size_t a = 0; a < FACE_EXPRESSION_COUNT; ++a) {
            if (a != FACE_EXPRESSION_NEUTRAL) {
                const roi_diff_t d = roi_compare(
                    g_expression_frames[FACE_EXPRESSION_NEUTRAL],
                    g_expression_frames[a]);
                if (d.roi_delta >= 0.008 && d.roi_changed >= 0.018) {
                    ++clear;
                }
                if (d.roi_delta < min_nonneutral) {
                    min_nonneutral = d.roi_delta;
                }
            }
            for (size_t b = a + 1; b < FACE_EXPRESSION_COUNT; ++b) {
                const roi_diff_t d = roi_compare(
                    g_expression_frames[a], g_expression_frames[b]);
                mean_pair += d.roi_delta;
                ++pairs;
            }
        }
        mean_pair /= (double)pairs;
        printf(
            "  %-22s separability: clear %u/10, mean pair ROI "
            "%.4f, min non-neutral %.4f\n",
            face_cozmo_acting_profile_slug(profile), clear, mean_pair,
            min_nonneutral);
        check(clear == 10, "all ten emotions clearly non-neutral");
        check(mean_pair >= 0.012, "mean pairwise ROI distance");
    }
}

/* ---- 5. 30 fps temporal smoothness ------------------------------------ */

static face_render_key_t motion_key(uint32_t frame)
{
    face_render_key_t key = base_key();
    const uint32_t jaw = (frame * 21U + 7U) % 255U;
    const uint32_t form = (frame * 9U + 19U) % 255U;
    key.controls.mouth_open = (uint8_t)(24U + (jaw * 204U) / 255U);
    key.controls.mouth_width = (uint8_t)(104U + (form * 104U) / 255U);
    key.audio_level = (uint8_t)(18U + (jaw * 202U) / 255U);
    key.viseme_blend = (uint8_t)form;
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = 7;
    cue.expression = FACE_EXPRESSION_JOY;
    cue.start_sample = SAMPLE_RATE;
    cue.attack_samples = SAMPLE_RATE;
    cue.hold_samples = SAMPLE_RATE * 2U;
    cue.release_samples = SAMPLE_RATE;
    cue.gesture = FACE_GESTURE_NOD;
    cue.gaze_target = FACE_GAZE_USER;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 238U;
    const uint32_t clock =
        (uint32_t)(((uint64_t)frame * SAMPLE_RATE) / FPS);
    (void)face_stage_cue_apply(&cue, clock, &key);
    return key;
}

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a;
    const double y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void test_motion(void)
{
    static double deltas[MOTION_FRAMES - 1];
    static double changed[MOTION_FRAMES - 1];
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        face_render_key_t key = motion_key(0);
        render_checked(profile, &key, 0, g_previous);
        double max_delta = 0.0;
        for (uint32_t frame = 1; frame < MOTION_FRAMES; ++frame) {
            const uint32_t clock =
                (uint32_t)(((uint64_t)frame * SAMPLE_RATE) / FPS);
            key = motion_key(frame);
            render_checked(profile, &key, clock, g_current);
            check_ring_black(g_current, "motion frame ring");
            const roi_diff_t d = roi_compare(g_previous, g_current);
            deltas[frame - 1] = d.roi_delta;
            changed[frame - 1] = d.roi_changed;
            if (d.roi_delta > max_delta) {
                max_delta = d.roi_delta;
            }
            memcpy(g_previous, g_current, sizeof(g_previous));
        }
        static double sorted[MOTION_FRAMES - 1];
        memcpy(sorted, deltas, sizeof(sorted));
        qsort(
            sorted, MOTION_FRAMES - 1, sizeof(sorted[0]),
            compare_double);
        const double median = sorted[(MOTION_FRAMES - 1) / 2];
        double jump_threshold = median * 7.0 + 0.008;
        if (jump_threshold < 0.035) {
            jump_threshold = 0.035;
        }
        uint32_t jumps = 0;
        uint32_t frozen = 0;
        for (size_t i = 0; i < MOTION_FRAMES - 1; ++i) {
            if (deltas[i] > jump_threshold && changed[i] >= 0.08) {
                ++jumps;
            }
            frozen += changed[i] == 0.0;
        }
        printf(
            "  %-22s motion: median %.4f, max %.4f, jumps %u, "
            "frozen %u/%u\n",
            face_cozmo_acting_profile_slug(profile), median, max_delta,
            jumps, frozen, MOTION_FRAMES - 1);
        check(jumps == 0, "no abrupt 30 fps jumps");
        check(max_delta < 0.14, "peak frame delta bounded");
        check(frozen < 30, "the face never freezes");
    }
}

/* Autonomous aperture/gaze step bounds at 30 fps (blink kinematics). */
static void test_pose_step_bounds(void)
{
    const face_render_key_t key = base_key();
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        face_cozmo_acting_pose_t previous;
        check(
            face_cozmo_acting_resolve(profile, &key, 0, &previous),
            "resolve frame 0");
        for (uint32_t frame = 1; frame < FPS * 30U; ++frame) {
            const uint32_t clock =
                (uint32_t)(((uint64_t)frame * SAMPLE_RATE) / FPS);
            face_cozmo_acting_pose_t pose;
            check(
                face_cozmo_acting_resolve(profile, &key, clock, &pose),
                "resolve frame");
            for (int eye = 0; eye < 2; ++eye) {
                const int32_t step =
                    pose.aperture_q8[eye] - previous.aperture_q8[eye];
                check(
                    step <= 124 && step >= -124,
                    "autonomous aperture step under 124 Q8 per frame");
            }
            const int32_t gaze_step_x =
                pose.gaze_x_q8 - previous.gaze_x_q8;
            const int32_t gaze_step_y =
                pose.gaze_y_q8 - previous.gaze_y_q8;
            check(
                gaze_step_x <= 132 && gaze_step_x >= -132 &&
                    gaze_step_y <= 132 && gaze_step_y >= -132,
                "autonomous gaze step bounded");
            previous = pose;
        }
    }
}

/* ---- 6. semantic layering ---------------------------------------------- */

static void test_layering(void)
{
    /* A v1 record must leave every extended byte inert. */
    face_render_key_t v1 = base_key();
    v1.schema_version = 1U;
    face_render_key_t v1_emotional = v1;
    v1_emotional.stage_expression = FACE_EXPRESSION_JOY;
    v1_emotional.expression_weight = 255U;
    v1_emotional.cheek = 255U;
    v1_emotional.brow_inner = 127;
    v1_emotional.head_roll = 90;
    v1_emotional.body_lean_y = 100;
    render_checked(
        FACE_COZMO_ACTING_STAGE, &v1, FIXED_CLOCK, g_previous);
    render_checked(
        FACE_COZMO_ACTING_STAGE, &v1_emotional, FIXED_CLOCK, g_current);
    check(
        memcmp(g_previous, g_current, sizeof(g_previous)) == 0,
        "schema v1 ignores extended action bytes");

    /* Activity is not emotion: same stage emotion, different activity
     * byte, must both render and differ (the byte is consumed as
     * posture), and an out-of-range stage_expression is inert. */
    face_render_key_t idle = expression_key(FACE_EXPRESSION_WARM);
    idle.controls.expression = FACE_ACTIVITY_IDLE;
    idle.controls.flags = 0U;
    idle.speech_phase = FACE_SPEECH_IDLE;
    face_render_key_t speaking = idle;
    speaking.controls.expression = FACE_ACTIVITY_SPEAKING;
    render_checked(
        FACE_COZMO_ACTING_STAGE, &idle, FIXED_CLOCK, g_previous);
    render_checked(
        FACE_COZMO_ACTING_STAGE, &speaking, FIXED_CLOCK, g_current);
    check(
        memcmp(g_previous, g_current, sizeof(g_previous)) != 0,
        "activity byte changes posture");

    face_render_key_t custom = base_key();
    custom.stage_expression = FACE_EXPRESSION_CUSTOM;
    custom.expression_weight = 255U;
    face_render_key_t neutral = base_key();
    neutral.stage_expression = FACE_EXPRESSION_NEUTRAL;
    neutral.expression_weight = 255U;
    render_checked(
        FACE_COZMO_ACTING_STAGE, &custom, FIXED_CLOCK, g_previous);
    render_checked(
        FACE_COZMO_ACTING_STAGE, &neutral, FIXED_CLOCK, g_current);
    check(
        memcmp(g_previous, g_current, sizeof(g_previous)) == 0,
        "CUSTOM emotion falls back to dense bytes only");
}

/* Every consumed field group must visibly change some profile's frame. */
static void test_field_consumption(void)
{
    /* One indexed case per consumed field group. */
    static const char *const NAMES[] = {
        "mouth_open", "mouth_width", "mouth_round", "mouth_press",
        "mouth_teeth", "eye_left_open", "eye_right_open", "look_x",
        "look_y", "brow", "flags", "viseme", "viseme_weight",
        "audio_level", "viseme_set", "viseme_secondary", "viseme_blend",
        "speech_phase", "mouth_corner_left", "mouth_corner_right",
        "tongue", "cheek", "eye_left_squint", "eye_right_squint",
        "brow_inner", "brow_outer_left", "brow_outer_right",
        "head_roll", "affect_valence", "affect_arousal", "head_yaw",
        "head_pitch", "body_lean_x", "body_lean_y", "expression_weight",
        "attention", "stage_expression",
    };
    enum { CASE_COUNT = sizeof(NAMES) / sizeof(NAMES[0]) };

    for (size_t index = 0; index < CASE_COUNT; ++index) {
        face_render_key_t key = base_key();
        /* A mid-weight authored emotion so weight changes matter. */
        key.stage_expression = FACE_EXPRESSION_WARM;
        key.expression_weight = 128U;
        face_render_key_t changed = key;
        face_cozmo_acting_profile_t profile = FACE_COZMO_ACTING_CHATTER;
        switch (index) {
        case 0: changed.controls.mouth_open = 255U; break;
        case 1: changed.controls.mouth_width = 30U; break;
        case 2: changed.controls.mouth_round = 240U; break;
        case 3: changed.controls.mouth_press = 220U; break;
        case 4: changed.controls.mouth_teeth = 250U; break;
        case 5:
            changed.controls.eye_left_open = 40U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 6:
            changed.controls.eye_right_open = 40U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 7:
            changed.controls.look_x = 100;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 8:
            changed.controls.look_y = 100;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 9:
            changed.controls.brow = -120;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 10:
            changed.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 11: changed.viseme = FACE_VISEME_U; break;
        case 12: changed.viseme_weight = 10U; break;
        case 13: changed.audio_level = 255U; break;
        case 14:
            changed.viseme_set = FACE_VISEME_SET_VRM5;
            changed.viseme = 4U; /* VRM O against OVR AA */
            break;
        case 15:
            changed.viseme_secondary = FACE_VISEME_U;
            changed.viseme_blend = 200U;
            break;
        case 16: changed.viseme_blend = 220U; break;
        case 17:
            changed.speech_phase = FACE_SPEECH_STARTING;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 18: changed.mouth_corner_left = -110; break;
        case 19: changed.mouth_corner_right = 110; break;
        case 20: changed.tongue = 255U; break;
        case 21:
            changed.cheek = 230U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 22:
            changed.eye_left_squint = 230U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 23:
            changed.eye_right_squint = 230U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 24:
            changed.brow_inner = 120;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 25:
            changed.brow_outer_left = -120;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 26:
            changed.brow_outer_right = 120;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 27:
            changed.head_roll = 100;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 28:
            changed.affect_valence = -120;
            profile = FACE_COZMO_ACTING_NOVA;
            break;
        case 29:
            changed.affect_arousal = 250U;
            profile = FACE_COZMO_ACTING_NOVA;
            break;
        case 30:
            changed.head_yaw = 110;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 31:
            changed.head_pitch = 110;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 32:
            changed.body_lean_x = 110;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 33:
            changed.body_lean_y = 110;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 34:
            changed.expression_weight = 255U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 35:
            changed.attention = 10U;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        case 36:
            changed.stage_expression = FACE_EXPRESSION_SLEEPY;
            profile = FACE_COZMO_ACTING_STAGE;
            break;
        default:
            break;
        }
        render_checked(profile, &key, FIXED_CLOCK, g_previous);
        render_checked(profile, &changed, FIXED_CLOCK, g_current);
        if (memcmp(g_previous, g_current, sizeof(g_previous)) == 0) {
            fprintf(
                stderr, "FAILED: field %s is not consumed\n",
                NAMES[index]);
            abort();
        }
        ++g_checks;
    }
}

/* ---- 7. extreme poses and all-byte fuzzing ----------------------------- */

static uint32_t xorshift(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void test_extremes_and_fuzz(void)
{
    /* Deliberate worst cases: every pose control pinned to a corner. */
    static const int8_t EXTREME[5] = { -127, -64, 0, 64, 127 };
    for (size_t raw = 0; raw < face_cozmo_acting_profile_count(); ++raw) {
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)raw;
        for (size_t a = 0; a < 5; ++a) {
            for (size_t b = 0; b < 5; ++b) {
                face_render_key_t key =
                    expression_key(FACE_EXPRESSION_EXCITED);
                key.head_yaw = EXTREME[a];
                key.head_pitch = EXTREME[b];
                key.head_roll = EXTREME[(a + b) % 5];
                key.body_lean_x = EXTREME[(a + 2 * b) % 5];
                key.body_lean_y = EXTREME[(2 * a + b) % 5];
                key.controls.look_x = EXTREME[b];
                key.controls.look_y = EXTREME[a];
                render_checked(
                    profile, &key, FIXED_CLOCK + (uint32_t)(a * 977 + b),
                    g_current);
                check_ring_black(g_current, "extreme pose ring");
            }
        }
    }

    /* All-byte fuzz: any 40-byte record must render safely inside the
     * frame with guards intact. */
    uint32_t state = 0xC02A5EEDU;
    for (uint32_t iteration = 0; iteration < FUZZ_ITERATIONS;
         ++iteration) {
        face_render_key_t key;
        uint8_t *bytes = (uint8_t *)&key;
        for (size_t i = 0; i < sizeof(key); ++i) {
            bytes[i] = (uint8_t)(xorshift(&state) & 0xFFU);
        }
        const face_cozmo_acting_profile_t profile =
            (face_cozmo_acting_profile_t)(
                xorshift(&state) %
                (uint32_t)face_cozmo_acting_profile_count());
        const uint32_t clock = xorshift(&state);
        guard_init(&g_frame);
        check(
            face_cozmo_acting_render(
                profile, &key, clock, g_frame.pixels, PIXELS),
            "fuzz render succeeds");
        guard_check(&g_frame);
        check_ring_black(g_frame.pixels, "fuzz ring");
    }
}

int main(void)
{
    printf("face_cozmo_acting acceptance tests\n");
    test_metadata();
    printf("  metadata and rejection ok\n");
    test_determinism();
    printf("  determinism and composition ok\n");
    test_bilateral_mass();
    printf("  bilateral mass, emissive bands, containment ok\n");
    test_separability();
    test_motion();
    test_pose_step_bounds();
    printf("  pose step bounds ok\n");
    test_layering();
    printf("  semantic layering ok\n");
    test_field_consumption();
    printf("  all consumed IR fields verified\n");
    test_extremes_and_fuzz();
    printf("  extreme poses and %u-record fuzz ok\n", FUZZ_ITERATIONS);
    printf("face_cozmo_acting_test: PASS (%" PRIu32 " checks)\n",
           g_checks);
    return 0;
}
