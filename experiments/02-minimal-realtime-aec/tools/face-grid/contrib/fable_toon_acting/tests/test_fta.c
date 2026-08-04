/*
 * fable_toon_acting — native test suite.
 *
 * Sections:
 *   1. ABI + argument rejection
 *   2. purity, full coverage, guard bands
 *   3. hard no-clip proof under extreme-input sweeps
 *   4. blink behavior (closes fully, reopens slowly and smoothly)
 *   5. pupil hard clamp + bilateral aperture floor
 *   6. acting-quality mirror of firmware-ws/tests/face_render_quality.c
 *      (same base key, cues, ROI, thresholds — must PASS, not warn)
 *   7. viseme distinctness + coarticulation continuity
 *   8. speech phases, robustness, area conservation
 *   9. golden hashes (regen with --write-golden)
 *
 * Flags: --dump-hashes (print per-case hash table and exit; used for
 * the native-vs-wasm byte identity diff), --write-golden.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_stage.h"
#include "fta.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, ...) \
    do { \
        ++g_checks; \
        if (!(cond)) { \
            ++g_failures; \
            printf("FAIL %s:%d: ", __func__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

enum {
    SAMPLE_RATE = 16000,
    FRAMES_PER_SECOND = 30,
    SAMPLES_PER_FRAME = 533,
    EXPRESSION_CLOCK = SAMPLE_RATE * 7 + 211,
    GUARD_PIXELS = 64,
    MOTION_FRAME_COUNT = 180,
};

static uint16_t arena[GUARD_PIXELS + FTA_PIXEL_COUNT + GUARD_PIXELS];
static uint16_t *const frame = arena + GUARD_PIXELS;
static uint16_t scratch_arena[GUARD_PIXELS + FTA_PIXEL_COUNT + GUARD_PIXELS];
static uint16_t *const scratch = scratch_arena + GUARD_PIXELS;
static uint16_t expression_frames[FACE_EXPRESSION_COUNT][FTA_PIXEL_COUNT];
static uint16_t previous_frame[FTA_PIXEL_COUNT];

/* ---- shared fixtures --------------------------------------------------- */

/* mirrors firmware-ws/tests/face_render_quality.c base_render_key() */
static face_render_key_t base_render_key(void)
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
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 1U;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t expression_cue(face_expression_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100U + (uint16_t)expression);
    cue.expression = (uint8_t)expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.blend_mode = FACE_STAGE_BLEND_REPLACE;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static face_render_key_t idle_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_width = 128U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.viseme = 14U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = 255U;
    key.affect_arousal = 72U;
    key.attention = 192U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static uint32_t crc_frame(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (int i = 0; i < FTA_PIXEL_COUNT; ++i) {
        hash ^= (uint32_t)(pixels[i] & 0xffU);
        hash *= 16777619U;
        hash ^= (uint32_t)(pixels[i] >> 8);
        hash *= 16777619U;
    }
    return hash;
}

static void fill_guards(uint16_t *base)
{
    for (int i = 0; i < GUARD_PIXELS; ++i) {
        base[i] = 0xa5a5U;
        base[GUARD_PIXELS + FTA_PIXEL_COUNT + i] = 0xa5a5U;
    }
}

static int guards_intact(const uint16_t *base)
{
    for (int i = 0; i < GUARD_PIXELS; ++i) {
        if (base[i] != 0xa5a5U ||
            base[GUARD_PIXELS + FTA_PIXEL_COUNT + i] != 0xa5a5U) {
            return 0;
        }
    }
    return 1;
}

/* ---- 1. ABI ------------------------------------------------------------ */

static void test_abi(void)
{
    CHECK(fta_profile_count() == 3U, "three profiles");
    CHECK(sizeof(face_render_key_t) == 40U, "renderer IR is 40 bytes");
    CHECK(sizeof(face_stage_cue_t) == 32U, "stage cue is 32 bytes");
    CHECK(sizeof(fta_info_t) == 16U, "info struct is 16 bytes");
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        CHECK(fta_profile_slug(profile) != NULL, "slug %zu", p);
        CHECK(fta_profile_name(profile) != NULL, "name %zu", p);
        fta_info_t info;
        CHECK(fta_profile_info(profile, &info), "info %zu", p);
        CHECK(
            info.width == 160U && info.height == 120U &&
                info.framebuffer_bytes == FTA_FRAME_BYTES,
            "info geometry %zu", p);
        CHECK(
            info.estimated_ops_per_pixel > 0U &&
                info.estimated_ops_per_pixel < 40U,
            "ops estimate sane %zu", p);
    }
    const face_render_key_t key = base_render_key();
    CHECK(
        !fta_render_frame(
            (fta_profile_t)99, &key, 0U, frame, FTA_PIXEL_COUNT),
        "reject bad profile");
    CHECK(
        !fta_render_frame(FTA_PROFILE_TOON_BEAN, NULL, 0U, frame,
                          FTA_PIXEL_COUNT),
        "reject NULL key");
    CHECK(
        !fta_render_frame(FTA_PROFILE_TOON_BEAN, &key, 0U, NULL,
                          FTA_PIXEL_COUNT),
        "reject NULL buffer");
    CHECK(
        !fta_render_frame(FTA_PROFILE_TOON_BEAN, &key, 0U, frame,
                          FTA_PIXEL_COUNT - 1U),
        "reject short buffer");
    fta_rig_t rig;
    CHECK(!fta_solve((fta_profile_t)77, &key, 0U, &rig), "solve bad profile");
    CHECK(fta_solve(FTA_PROFILE_TOON_INK, &key, 123U, &rig), "solve ok");
}

/* ---- 2. purity --------------------------------------------------------- */

static void test_purity(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        face_render_key_t key = base_render_key();
        fill_guards(arena);
        for (int i = 0; i < FTA_PIXEL_COUNT; ++i) {
            frame[i] = 0xdeadU;
        }
        CHECK(
            fta_render_frame(profile, &key, 77777U, frame,
                             FTA_PIXEL_COUNT),
            "render %zu", p);
        const uint32_t first = crc_frame(frame);
        int untouched = 0;
        for (int i = 0; i < FTA_PIXEL_COUNT; ++i) {
            untouched += frame[i] == 0xdeadU;
        }
        CHECK(
            untouched == 0, "%s writes every pixel (left %d)",
            fta_profile_slug(profile), untouched);
        CHECK(guards_intact(arena), "guards intact %zu", p);

        /* interleave other renders, then re-render on a new prefill */
        face_render_key_t other = idle_key();
        fta_render_frame(
            (fta_profile_t)((p + 1U) % fta_profile_count()), &other,
            999999U, scratch, FTA_PIXEL_COUNT);
        fta_render_frame(profile, &other, 31U, scratch, FTA_PIXEL_COUNT);
        for (int i = 0; i < FTA_PIXEL_COUNT; ++i) {
            frame[i] = 0x2152U;
        }
        CHECK(
            fta_render_frame(profile, &key, 77777U, frame,
                             FTA_PIXEL_COUNT),
            "re-render %zu", p);
        CHECK(
            crc_frame(frame) == first,
            "%s is a pure function of (key, clock)",
            fta_profile_slug(profile));
    }
}

/* ---- 3. hard no-clip --------------------------------------------------- */

/*
 * Every feature must stay on the plate and the plate inside the safe
 * area: with a uniform background fill, any pixel outside the plate
 * bounding box (plus AA margin) must equal the background exactly.
 */
static void check_no_clip(
    fta_profile_t profile, const face_render_key_t *key, uint32_t clock,
    const char *label)
{
    fta_rig_t rig;
    if (!fta_solve(profile, key, clock, &rig) ||
        !fta_render_frame(profile, key, clock, frame, FTA_PIXEL_COUNT)) {
        CHECK(false, "solve/render %s", label);
        return;
    }
    /* the sheared plate reaches |shear| * half_h beyond its box */
    const int32_t shear_reach =
        (abs((int)rig.shear_q12) *
         (((int)rig.plate_bottom_q4 - (int)rig.plate_top_q4) / 2)) >> 12;
    const int32_t left = ((rig.plate_left_q4 - shear_reach) >> 4) - 2;
    const int32_t right = ((rig.plate_right_q4 + shear_reach) >> 4) + 2;
    const int32_t top = (rig.plate_top_q4 >> 4) - 2;
    const int32_t bottom = (rig.plate_bottom_q4 >> 4) + 2;
    CHECK(
        left >= 1 && right < FTA_FRAME_WIDTH - 1 && top >= 1 &&
            bottom < FTA_FRAME_HEIGHT - 1,
        "%s plate inside safe area (%d %d %d %d)", label, left, top,
        right, bottom);
    const uint16_t background = frame[0];
    int leaks = 0;
    for (int y = 0; y < FTA_FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FTA_FRAME_WIDTH; ++x) {
            if (x >= left && x <= right && y >= top && y <= bottom) {
                continue;
            }
            if (frame[y * FTA_FRAME_WIDTH + x] != background) {
                ++leaks;
            }
        }
    }
    CHECK(leaks == 0, "%s: %d pixels leak outside the plate", label, leaks);
}

static void test_no_clip(void)
{
    static const int8_t EXTREMES[3] = {-127, 0, 127};
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        /* every expression at full weight, mid-speech */
        for (int e = 0; e < FACE_EXPRESSION_COUNT; ++e) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)e);
            face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
            char label[64];
            snprintf(label, sizeof(label), "p%zu expr%d", p, e);
            check_no_clip(profile, &key, EXPRESSION_CLOCK, label);
        }
        /* pose extremes: yaw/pitch/roll/lean/gaze all pushed together */
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) {
                face_render_key_t key = base_render_key();
                key.head_yaw = EXTREMES[a];
                key.head_pitch = EXTREMES[b];
                key.head_roll = EXTREMES[(a + b) % 3];
                key.body_lean_x = EXTREMES[b];
                key.body_lean_y = EXTREMES[a];
                key.controls.look_x = EXTREMES[a];
                key.controls.look_y = EXTREMES[b];
                key.controls.mouth_open = 255U;
                key.controls.mouth_width = 255U;
                key.mouth_corner_left = EXTREMES[a];
                key.mouth_corner_right = EXTREMES[b];
                key.affect_valence = EXTREMES[(a + 2 * b) % 3];
                key.cheek = 255U;
                key.tongue = 255U;
                key.controls.mouth_teeth = 255U;
                char label[64];
                snprintf(label, sizeof(label), "p%zu pose%d%d", p, a, b);
                check_no_clip(profile, &key, 55555U, label);
            }
        }
        /* all-maxed adversarial key */
        face_render_key_t wild;
        memset(&wild, 0xff, sizeof(wild));
        wild.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
        char label[64];
        snprintf(label, sizeof(label), "p%zu wild", p);
        check_no_clip(profile, &wild, 4294967295U, label);
    }
}

/* ---- 4. blink behavior -------------------------------------------------- */

static void test_blink(void)
{
    /* two idle minutes at 30 fps */
    const int frames = 2 * 60 * FRAMES_PER_SECOND;
    const face_render_key_t key = idle_key();
    int blink_count = 0;
    int min_openness = 256;
    int in_blink = 0;
    int close_frames = 0;
    int open_frames = 0;
    int reopen_slower = 0;
    int completed = 0;
    int previous_openness = -1;
    int max_step = 0;
    int falling = 0;
    for (int f = 0; f < frames; ++f) {
        fta_rig_t rig;
        fta_solve(
            FTA_PROFILE_TOON_BEAN, &key,
            (uint32_t)f * SAMPLES_PER_FRAME, &rig);
        const int openness = rig.eye[0].openness_q8;
        if (openness < min_openness) {
            min_openness = openness;
        }
        if (previous_openness >= 0) {
            const int step = openness - previous_openness;
            if (abs(step) > max_step) {
                max_step = abs(step);
            }
        }
        if (!in_blink && openness < 120) {
            in_blink = 1;
            ++blink_count;
            close_frames = 0;
            open_frames = 0;
            falling = 1;
        }
        if (in_blink) {
            if (falling && openness > 40) {
                ++close_frames;
            } else if (falling && openness <= 40) {
                falling = 0;
            } else if (!falling && openness < 200) {
                ++open_frames;
            } else if (!falling && openness >= 200) {
                in_blink = 0;
                ++completed;
                /* reopening lasts ~2-3x the down phase (VanderWerf
                 * 2003); allow >= so equal counts at 30 fps pass */
                reopen_slower += open_frames >= close_frames;
            }
        }
        previous_openness = openness;
    }
    /* idle blink rate: cadence 3.25 s -> ~37 over two minutes */
    CHECK(
        blink_count >= 24 && blink_count <= 52,
        "idle blink count over 2 min: %d", blink_count);
    CHECK(min_openness <= 16, "blinks close fully (min %d)", min_openness);
    CHECK(completed >= blink_count - 1, "blinks complete (%d/%d)",
          completed, blink_count);
    CHECK(
        reopen_slower * 10 >= completed * 8,
        "reopen slower than close (%d of %d)", reopen_slower, completed);
    /* smooth: no single 33 ms step may teleport the lids */
    CHECK(max_step <= 150, "lid motion smooth (max step %d)", max_step);
}

/* ---- 5. pupil clamp + aperture floor ------------------------------------ */

static void test_pupil_and_aperture(void)
{
    static const int8_t SWEEP[5] = {-127, -64, 0, 64, 127};
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        for (int gx = 0; gx < 5; ++gx) {
            for (int gy = 0; gy < 5; ++gy) {
                face_render_key_t key = base_render_key();
                key.controls.look_x = SWEEP[gx];
                key.controls.look_y = SWEEP[gy];
                key.eye_left_squint = (uint8_t)(gx * 50);
                key.eye_right_squint = (uint8_t)(gy * 50);
                fta_rig_t rig;
                fta_solve((fta_profile_t)p, &key, 91919U, &rig);
                for (int side = 0; side < 2; ++side) {
                    const fta_eye_t *eye = &rig.eye[side];
                    CHECK(
                        eye->pupil_x_q4 - (eye->iris_r_q4 * 3) / 4 >=
                                eye->center_x_q4 - eye->half_w_q4 - 8 &&
                            eye->pupil_x_q4 + (eye->iris_r_q4 * 3) / 4 <=
                                eye->center_x_q4 + eye->half_w_q4 + 8,
                        "pupil x clamped p%zu g%d%d s%d", p, gx, gy, side);
                    if (eye->openness_q8 > 32U) {
                        CHECK(
                            eye->pupil_y_q4 >= eye->lid_top_q4 &&
                                eye->pupil_y_q4 <= eye->lid_bottom_q4,
                            "pupil y within lids p%zu g%d%d s%d", p, gx,
                            gy, side);
                    }
                }
            }
        }
        /* bilateral aperture floor: every expression keeps both eyes
         * visible at the atlas clock */
        for (int e = 0; e < FACE_EXPRESSION_COUNT; ++e) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)e);
            face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
            fta_rig_t rig;
            fta_solve((fta_profile_t)p, &key, EXPRESSION_CLOCK, &rig);
            CHECK(
                rig.eye[0].openness_q8 >= 20U &&
                    rig.eye[1].openness_q8 >= 20U,
                "expression %d keeps both eyes visible on p%zu "
                "(%u/%u)",
                e, p, rig.eye[0].openness_q8, rig.eye[1].openness_q8);
        }
    }
}

/* ---- 6. acting-quality mirror ------------------------------------------- */

typedef struct {
    double roi_mean_delta;
    double roi_changed_fraction;
} frame_difference_t;

static uint8_t expand5(uint16_t pixel, int shift)
{
    const uint8_t value = (uint8_t)((pixel >> shift) & 0x1fU);
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(uint16_t pixel)
{
    const uint8_t value = (uint8_t)((pixel >> 5) & 0x3fU);
    return (uint8_t)((value << 2) | (value >> 4));
}

static frame_difference_t compare_frames(
    const uint16_t *first, const uint16_t *second)
{
    uint64_t roi_delta = 0U;
    size_t roi_changed = 0U;
    size_t roi_pixels = 0U;
    for (int y = 0; y < FTA_FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FTA_FRAME_WIDTH; ++x) {
            if (x < 16 || x >= FTA_FRAME_WIDTH - 16 || y < 10 ||
                y >= FTA_FRAME_HEIGHT - 10) {
                continue;
            }
            const uint16_t a = first[y * FTA_FRAME_WIDTH + x];
            const uint16_t b = second[y * FTA_FRAME_WIDTH + x];
            const uint32_t delta =
                (uint32_t)abs((int)expand5(a, 11) - (int)expand5(b, 11)) +
                (uint32_t)abs((int)expand6(a) - (int)expand6(b)) +
                (uint32_t)abs((int)expand5(a, 0) - (int)expand5(b, 0));
            roi_delta += delta;
            roi_changed += a != b;
            ++roi_pixels;
        }
    }
    frame_difference_t difference;
    difference.roi_mean_delta =
        (double)roi_delta / ((double)roi_pixels * 3.0 * 255.0);
    difference.roi_changed_fraction =
        (double)roi_changed / (double)roi_pixels;
    return difference;
}

static void test_expression_quality(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (int e = 0; e < FACE_EXPRESSION_COUNT; ++e) {
            face_render_key_t key = base_render_key();
            const face_stage_cue_t cue =
                expression_cue((face_expression_t)e);
            CHECK(
                face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key),
                "cue applies %d", e);
            CHECK(
                fta_render_frame(
                    profile, &key, EXPRESSION_CLOCK,
                    expression_frames[e], FTA_PIXEL_COUNT),
                "expression render %d", e);
            hashes[e] = crc_frame(expression_frames[e]);
        }
        uint32_t distinct = 0U;
        for (int e = 0; e < FACE_EXPRESSION_COUNT; ++e) {
            int first_occurrence = 1;
            for (int other = 0; other < e; ++other) {
                first_occurrence &= hashes[other] != hashes[e];
            }
            distinct += (uint32_t)first_occurrence;
        }
        uint32_t clear = 0U;
        for (int e = 1; e < FACE_EXPRESSION_COUNT; ++e) {
            const frame_difference_t difference = compare_frames(
                expression_frames[0], expression_frames[e]);
            if (difference.roi_mean_delta >= 0.008 &&
                difference.roi_changed_fraction >= 0.018) {
                ++clear;
            }
        }
        uint32_t weak_pairs = 0U;
        double pair_sum = 0.0;
        uint32_t pair_count = 0U;
        for (int e = 0; e < FACE_EXPRESSION_COUNT; ++e) {
            for (int other = e + 1; other < FACE_EXPRESSION_COUNT;
                 ++other) {
                const frame_difference_t difference = compare_frames(
                    expression_frames[e], expression_frames[other]);
                pair_sum += difference.roi_mean_delta;
                ++pair_count;
                if (difference.roi_mean_delta < 0.006 ||
                    difference.roi_changed_fraction < 0.015) {
                    ++weak_pairs;
                }
            }
        }
        const double mean_pair = pair_sum / (double)pair_count;
        /* firmware quality gate PASS conditions (not merely no-fail) */
        CHECK(
            distinct >= 9U, "%s: distinct hashes %u >= 9",
            fta_profile_slug(profile), distinct);
        CHECK(
            clear >= 8U, "%s: clear non-neutral %u >= 8",
            fta_profile_slug(profile), clear);
        CHECK(
            mean_pair >= 0.010, "%s: mean pair roi delta %.4f >= 0.010",
            fta_profile_slug(profile), mean_pair);
        printf(
            "  quality %s: distinct=%u clear=%u/10 weak=%u/55 "
            "mean_pair=%.4f\n",
            fta_profile_slug(profile), distinct, clear, weak_pairs,
            mean_pair);
    }
}

/* mirrors motion_render_key() + motion_stage_cue() from the firmware rig */
static uint8_t triangle_u8(uint32_t frame_index, uint32_t period)
{
    const uint32_t phase = frame_index % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)((phase * 255U) / half);
    }
    return (uint8_t)(255U - ((phase - half) * 255U) / (period - half));
}

static face_render_key_t motion_render_key(uint32_t frame_index)
{
    face_render_key_t key = base_render_key();
    const uint8_t jaw_wave = triangle_u8(frame_index + 7U, 42U);
    const uint8_t form_wave = triangle_u8(frame_index + 19U, 74U);
    const uint8_t gaze_wave = triangle_u8(frame_index + 11U, 120U);
    key.controls.mouth_open =
        (uint8_t)(24U + (uint16_t)jaw_wave * 204U / 255U);
    key.controls.mouth_width =
        (uint8_t)(104U + (uint16_t)form_wave * 104U / 255U);
    key.controls.mouth_round =
        (uint8_t)(218U - (uint16_t)form_wave * 174U / 255U);
    key.controls.mouth_teeth =
        (uint8_t)(24U + (uint16_t)jaw_wave * 104U / 255U);
    key.controls.look_x = (int8_t)((int32_t)gaze_wave / 3 - 42);
    key.controls.look_y =
        (int8_t)((int32_t)triangle_u8(frame_index + 37U, 156U) / 6 - 21);
    key.audio_level = (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_blend = form_wave;
    key.head_roll =
        (int8_t)((int32_t)triangle_u8(frame_index + 13U, 180U) / 12 - 10);
    return key;
}

static void test_motion_quality(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        face_stage_cue_t cue = expression_cue(FACE_EXPRESSION_JOY);
        cue.start_sample = SAMPLE_RATE;
        cue.attack_samples = SAMPLE_RATE;
        cue.hold_samples = SAMPLE_RATE * 2U;
        cue.release_samples = SAMPLE_RATE;
        cue.flags = 0U;
        cue.gesture = FACE_GESTURE_NOD;
        cue.intensity = 238U;

        double deltas[MOTION_FRAME_COUNT - 1];
        double changed[MOTION_FRAME_COUNT - 1];
        uint32_t frozen = 0U;
        for (uint32_t f = 0; f < MOTION_FRAME_COUNT; ++f) {
            const uint32_t clock = (uint32_t)(
                ((uint64_t)f * SAMPLE_RATE) / FRAMES_PER_SECOND);
            face_render_key_t key = motion_render_key(f);
            (void)face_stage_cue_apply(&cue, clock, &key);
            CHECK(
                fta_render_frame(
                    profile, &key, clock, frame, FTA_PIXEL_COUNT),
                "motion frame %u", f);
            if (f > 0U) {
                const frame_difference_t difference =
                    compare_frames(previous_frame, frame);
                deltas[f - 1U] = difference.roi_mean_delta;
                changed[f - 1U] = difference.roi_changed_fraction;
                frozen += difference.roi_changed_fraction == 0.0;
            }
            memcpy(previous_frame, frame, sizeof(previous_frame));
        }
        /* jump threshold: max(median * 7 + 0.008, 0.035) */
        double sorted[MOTION_FRAME_COUNT - 1];
        memcpy(sorted, deltas, sizeof(sorted));
        for (int i = 1; i < MOTION_FRAME_COUNT - 1; ++i) {
            const double value = sorted[i];
            int j = i - 1;
            while (j >= 0 && sorted[j] > value) {
                sorted[j + 1] = sorted[j];
                --j;
            }
            sorted[j + 1] = value;
        }
        const double median = sorted[(MOTION_FRAME_COUNT - 1) / 2];
        double threshold = median * 7.0 + 0.008;
        if (threshold < 0.035) {
            threshold = 0.035;
        }
        uint32_t jumps = 0U;
        double max_delta = 0.0;
        for (int i = 0; i < MOTION_FRAME_COUNT - 1; ++i) {
            if (deltas[i] > threshold && changed[i] >= 0.08) {
                ++jumps;
            }
            if (deltas[i] > max_delta) {
                max_delta = deltas[i];
            }
        }
        CHECK(jumps == 0U, "%s: zero abrupt jumps (got %u, thr %.4f)",
              fta_profile_slug(profile), jumps, threshold);
        CHECK(
            max_delta < 0.14, "%s: max roi delta %.4f < 0.14",
            fta_profile_slug(profile), max_delta);
        CHECK(
            frozen < 30U, "%s: frozen pairs %u < 30",
            fta_profile_slug(profile), frozen);
        printf(
            "  motion %s: jumps=%u max=%.4f median=%.5f frozen=%u\n",
            fta_profile_slug(profile), jumps, max_delta, median, frozen);
    }
}

/* ---- 7. visemes --------------------------------------------------------- */

static const uint8_t VISEME_SHAPES[15][5] = {
    {236, 205, 24, 0, 18},  {155, 246, 0, 0, 128}, {102, 255, 0, 0, 155},
    {214, 112, 255, 0, 16}, {112, 82, 244, 0, 10}, {12, 164, 18, 255, 0},
    {66, 224, 0, 0, 210},   {88, 190, 0, 0, 235},  {82, 194, 0, 0, 164},
    {38, 198, 0, 176, 235}, {120, 181, 20, 0, 78}, {72, 202, 0, 0, 142},
    {104, 148, 94, 0, 72},  {86, 158, 46, 34, 184}, {0, 110, 30, 0, 0},
};

static face_render_key_t viseme_key(int viseme)
{
    face_render_key_t key = idle_key();
    key.controls.mouth_open = VISEME_SHAPES[viseme][0];
    key.controls.mouth_width = VISEME_SHAPES[viseme][1];
    key.controls.mouth_round = VISEME_SHAPES[viseme][2];
    key.controls.mouth_press = VISEME_SHAPES[viseme][3];
    key.controls.mouth_teeth = VISEME_SHAPES[viseme][4];
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = (uint8_t)viseme;
    key.viseme_weight = 255U;
    key.audio_level = 170U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 120U;
    return key;
}

static void test_visemes(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        uint32_t hashes[15];
        for (int v = 0; v < 15; ++v) {
            face_render_key_t key = viseme_key(v);
            CHECK(
                fta_render_frame(
                    (fta_profile_t)p, &key, 88011U, frame,
                    FTA_PIXEL_COUNT),
                "viseme render %d", v);
            hashes[v] = crc_frame(frame);
        }
        int collisions = 0;
        for (int a = 0; a < 15; ++a) {
            for (int b = a + 1; b < 15; ++b) {
                collisions += hashes[a] == hashes[b];
            }
        }
        CHECK(collisions == 0, "p%zu: all 15 visemes distinct", p);

        /* coarticulation: sweeping viseme_blend must move smoothly */
        face_render_key_t key = viseme_key(0);
        key.viseme_secondary = 3U; /* AA -> O */
        double max_delta = 0.0;
        int rendered = 0;
        for (int blend = 0; blend <= 255; blend += 8) {
            key.viseme_blend = (uint8_t)blend;
            CHECK(
                fta_render_frame(
                    (fta_profile_t)p, &key, 88011U, frame,
                    FTA_PIXEL_COUNT),
                "blend render %d", blend);
            if (rendered) {
                const frame_difference_t difference =
                    compare_frames(previous_frame, frame);
                if (difference.roi_mean_delta > max_delta) {
                    max_delta = difference.roi_mean_delta;
                }
            }
            memcpy(previous_frame, frame, sizeof(previous_frame));
            rendered = 1;
        }
        CHECK(
            max_delta < 0.020,
            "p%zu: coarticulation continuous (max step %.4f)", p,
            max_delta);
    }
}

/* ---- 8. phases, robustness, conservation -------------------------------- */

static void test_phases_and_robustness(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        face_render_key_t key = base_render_key();
        key.speech_phase = FACE_SPEECH_ACTIVE;
        fta_render_frame((fta_profile_t)p, &key, 40000U, frame,
                         FTA_PIXEL_COUNT);
        const uint32_t active = crc_frame(frame);
        key.speech_phase = FACE_SPEECH_STARTING;
        fta_render_frame((fta_profile_t)p, &key, 40000U, frame,
                         FTA_PIXEL_COUNT);
        const uint32_t starting = crc_frame(frame);
        key.speech_phase = FACE_SPEECH_ENDING;
        fta_render_frame((fta_profile_t)p, &key, 40000U, frame,
                         FTA_PIXEL_COUNT);
        const uint32_t ending = crc_frame(frame);
        CHECK(
            starting != active && ending != active && starting != ending,
            "p%zu speech phases read differently", p);

        /* clock robustness: reversal, zero, max */
        key = idle_key();
        CHECK(
            fta_render_frame((fta_profile_t)p, &key, 100U, frame,
                             FTA_PIXEL_COUNT) &&
                fta_render_frame((fta_profile_t)p, &key, 5U, frame,
                                 FTA_PIXEL_COUNT) &&
                fta_render_frame((fta_profile_t)p, &key, 0U, frame,
                                 FTA_PIXEL_COUNT) &&
                fta_render_frame((fta_profile_t)p, &key, 4294967295U,
                                 frame, FTA_PIXEL_COUNT),
            "p%zu survives clock excursions", p);

        /* unknown schema/viseme-set degrade gracefully */
        key = base_render_key();
        key.schema_version = 0U;
        key.viseme_set = FACE_VISEME_SET_CUSTOM;
        key.viseme = 200U;
        CHECK(
            fta_render_frame((fta_profile_t)p, &key, 123U, frame,
                             FTA_PIXEL_COUNT),
            "p%zu tolerates unknown vocabularies", p);

        /* area conservation: scale_x * scale_y within 2% of unity */
        key = base_render_key();
        key.body_lean_y = 127;
        fta_rig_t rig;
        fta_solve((fta_profile_t)p, &key, 70000U, &rig);
        const int32_t product =
            (int32_t)rig.scale_x_q8 * (int32_t)rig.scale_y_q8;
        CHECK(
            product > 65536 * 98 / 100 && product < 65536 * 102 / 100,
            "p%zu squash conserves area (%d)", p, product);
        CHECK(rig.scale_y_q8 > 256, "p%zu lean-in stretches (%d)",
              p, rig.scale_y_q8);
    }
}

/* ---- 9. goldens --------------------------------------------------------- */

typedef struct {
    const char *slug;
    uint32_t kase;
    uint32_t hash;
} golden_t;

static const golden_t GOLDENS[] = {
#define FTA_GOLDEN(slug_, case_, hash_) {slug_, case_, hash_},
#include "golden_hashes.inc"
#undef FTA_GOLDEN
    {NULL, 0U, 0U},
};

static uint32_t golden_case_hash(size_t p, uint32_t kase)
{
    face_render_key_t key;
    uint32_t clock;
    if (kase < FACE_EXPRESSION_COUNT) {
        key = base_render_key();
        const face_stage_cue_t cue =
            expression_cue((face_expression_t)kase);
        face_stage_cue_apply(&cue, EXPRESSION_CLOCK, &key);
        clock = EXPRESSION_CLOCK;
    } else if (kase < FACE_EXPRESSION_COUNT + 15U) {
        key = viseme_key((int)(kase - FACE_EXPRESSION_COUNT));
        clock = 88011U;
    } else {
        key = idle_key();
        clock = (kase - FACE_EXPRESSION_COUNT - 15U) * 40033U + 7U;
    }
    fta_render_frame((fta_profile_t)p, &key, clock, frame,
                     FTA_PIXEL_COUNT);
    return crc_frame(frame);
}

enum {
    GOLDEN_CASES = FACE_EXPRESSION_COUNT + 15 + 6,
};

static void dump_hashes(void)
{
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        for (uint32_t kase = 0; kase < GOLDEN_CASES; ++kase) {
            printf(
                "%s %02u %08" PRIx32 "\n",
                fta_profile_slug((fta_profile_t)p), kase,
                golden_case_hash(p, kase));
        }
    }
}

static void write_golden(void)
{
    printf("/* generated by test_fta --write-golden; do not edit */\n");
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        for (uint32_t kase = 0; kase < GOLDEN_CASES; ++kase) {
            printf(
                "FTA_GOLDEN(\"%s\", %uU, 0x%08" PRIx32 "U)\n",
                fta_profile_slug((fta_profile_t)p), kase,
                golden_case_hash(p, kase));
        }
    }
}

static void test_goldens(void)
{
    if (GOLDENS[0].slug == NULL) {
        printf("  NOTE: golden_hashes.inc is empty; run "
               "`make regen-golden`.\n");
        return;
    }
    for (const golden_t *golden = GOLDENS; golden->slug != NULL;
         ++golden) {
        fta_profile_t profile = FTA_PROFILE_COUNT;
        for (size_t p = 0; p < fta_profile_count(); ++p) {
            if (strcmp(fta_profile_slug((fta_profile_t)p),
                       golden->slug) == 0) {
                profile = (fta_profile_t)p;
            }
        }
        CHECK(profile != FTA_PROFILE_COUNT, "golden slug %s",
              golden->slug);
        if (profile == FTA_PROFILE_COUNT) {
            continue;
        }
        const uint32_t hash =
            golden_case_hash((size_t)profile, golden->kase);
        CHECK(
            hash == golden->hash,
            "golden %s case %u: %08" PRIx32 " != %08" PRIx32,
            golden->slug, golden->kase, hash, golden->hash);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--dump-hashes") == 0) {
        dump_hashes();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--write-golden") == 0) {
        write_golden();
        return 0;
    }
    test_abi();
    test_purity();
    test_no_clip();
    test_blink();
    test_pupil_and_aperture();
    test_expression_quality();
    test_motion_quality();
    test_visemes();
    test_phases_and_robustness();
    test_goldens();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
