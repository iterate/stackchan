#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_stage.h"
#include "fea.h"

/*
 * Strict native suite for fable_expression_actors_v3.
 *
 *   ./test_fea                 run everything
 *   ./test_fea --dump-hashes   print the golden case table
 *   ./test_fea --write-golden  print golden_hashes.inc content
 *
 * The renderer itself is integer-only; this harness uses doubles for
 * metrics, mirroring firmware-ws/tests/face_render_quality.c.
 */

enum {
    SAMPLE_RATE = 16000,
    GUARD_PIXELS = 512,
    MOTION_FRAMES = 180,
    FUZZ_KEYS = 1500,
};

static uint32_t checks_run = 0;
static uint32_t checks_failed = 0;

#define CHECK(condition, ...)                                          \
    do {                                                               \
        ++checks_run;                                                  \
        if (!(condition)) {                                            \
            ++checks_failed;                                           \
            printf("FAIL %s:%d  ", __func__, __LINE__);                \
            printf(__VA_ARGS__);                                       \
            printf("\n");                                              \
        }                                                              \
    } while (0)

static uint16_t buffer_a[GUARD_PIXELS + FEA_PIXEL_COUNT + GUARD_PIXELS];
static uint16_t buffer_b[GUARD_PIXELS + FEA_PIXEL_COUNT + GUARD_PIXELS];
static uint16_t
    emotion_frames[FACE_EXPRESSION_COUNT][FEA_PIXEL_COUNT];

static uint16_t *frame_a(void) { return buffer_a + GUARD_PIXELS; }
static uint16_t *frame_b(void) { return buffer_b + GUARD_PIXELS; }

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < (size_t)FEA_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t splitmix32(uint32_t *state)
{
    uint32_t x = (*state += 0x9e3779b9U);
    x ^= x >> 16;
    x *= 0x21f0aaadU;
    x ^= x >> 15;
    x *= 0x735a2d97U;
    x ^= x >> 15;
    return x;
}

/* ------------------------------------------------ shared scenario keys */

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
    key.controls.expression = 3U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 0U;
    key.viseme_weight = 220U;
    key.audio_level = 154U;
    key.viseme_secondary = 1U;
    key.viseme_blend = 38U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 96U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_stage_cue_t emotion_cue(uint8_t expression)
{
    face_stage_cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.cue_id = (uint16_t)(100U + expression);
    cue.expression = expression;
    cue.gaze_target = FACE_GAZE_USER;
    cue.easing = FACE_STAGE_EASE_SMOOTHSTEP;
    cue.intensity = 255U;
    cue.flags = FACE_STAGE_FLAG_HOLD_FINAL;
    return cue;
}

static uint8_t triangle_u8(uint32_t frame, uint32_t period)
{
    const uint32_t phase = frame % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

static face_render_key_t motion_key(uint32_t frame)
{
    face_render_key_t key = base_key();
    const uint8_t jaw_wave = triangle_u8(frame + 7U, 42U);
    const uint8_t form_wave = triangle_u8(frame + 19U, 74U);
    const uint8_t gaze_wave = triangle_u8(frame + 11U, 120U);
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
        (int8_t)((int32_t)triangle_u8(frame + 37U, 156U) / 6 - 21);
    key.audio_level =
        (uint8_t)(18U + (uint16_t)jaw_wave * 202U / 255U);
    key.viseme_blend = form_wave;
    key.head_roll =
        (int8_t)((int32_t)triangle_u8(frame + 13U, 180U) / 12 - 10);
    return key;
}

static face_stage_cue_t motion_cue(void)
{
    face_stage_cue_t cue = emotion_cue(FACE_EXPRESSION_JOY);
    cue.start_sample = SAMPLE_RATE;
    cue.attack_samples = SAMPLE_RATE;
    cue.hold_samples = SAMPLE_RATE * 2U;
    cue.release_samples = SAMPLE_RATE;
    cue.flags = 0U;
    cue.gesture = FACE_GESTURE_NOD;
    cue.intensity = 238U;
    return cue;
}

static void render_emotion(
    fea_profile_t profile, uint8_t emotion, uint32_t clock,
    uint16_t *pixels)
{
    face_render_key_t key = base_key();
    const face_stage_cue_t cue = emotion_cue(emotion);
    (void)face_stage_cue_apply(&cue, clock, &key);
    CHECK(fea_render_frame(profile, &key, clock, pixels,
                           FEA_PIXEL_COUNT),
          "emotion render %u", emotion);
}

static void render_motion(
    fea_profile_t profile, uint32_t frame, uint16_t *pixels)
{
    const uint32_t clock =
        (uint32_t)((uint64_t)frame * SAMPLE_RATE / 30U);
    face_render_key_t key = motion_key(frame);
    const face_stage_cue_t cue = motion_cue();
    (void)face_stage_cue_apply(&cue, clock, &key);
    CHECK(fea_render_frame(profile, &key, clock, pixels,
                           FEA_PIXEL_COUNT),
          "motion render %u", frame);
}

/* -------------------------------------------------- frame comparison */

typedef struct {
    double roi_mean_delta;
    double roi_changed_fraction;
} frame_diff_t;

static uint8_t chan_r(uint16_t p) { return (uint8_t)((p >> 11) & 31); }
static uint8_t chan_g(uint16_t p) { return (uint8_t)((p >> 5) & 63); }
static uint8_t chan_b(uint16_t p) { return (uint8_t)(p & 31); }

static frame_diff_t compare_frames(
    const uint16_t *first, const uint16_t *second)
{
    uint64_t delta = 0U;
    size_t changed = 0U;
    size_t roi_pixels = 0U;
    for (size_t index = 0; index < (size_t)FEA_PIXEL_COUNT; ++index) {
        const size_t x = index % FEA_FRAME_WIDTH;
        const size_t y = index / FEA_FRAME_WIDTH;
        if (x < 16U || x >= FEA_FRAME_WIDTH - 16U || y < 10U ||
            y >= FEA_FRAME_HEIGHT - 10U) {
            continue;
        }
        ++roi_pixels;
        const uint16_t a = first[index];
        const uint16_t b = second[index];
        delta += (uint64_t)(abs((int)(chan_r(a) << 3) -
                                (int)(chan_r(b) << 3)) +
                            abs((int)(chan_g(a) << 2) -
                                (int)(chan_g(b) << 2)) +
                            abs((int)(chan_b(a) << 3) -
                                (int)(chan_b(b) << 3)));
        changed += a != b;
    }
    frame_diff_t diff;
    diff.roi_mean_delta =
        (double)delta / ((double)roi_pixels * 3.0 * 255.0);
    diff.roi_changed_fraction =
        (double)changed / (double)roi_pixels;
    return diff;
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* ------------------------------------------------------------- tests */

static void test_abi(void)
{
    CHECK(sizeof(face_render_key_t) == 40, "render key must be 40 B");
    CHECK(sizeof(face_stage_cue_t) == 32, "stage cue must be 32 B");
    CHECK(sizeof(fea_info_t) == 16, "info must be 16 B");
    CHECK(fea_profile_count() == FEA_PROFILE_COUNT, "profile count");
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        CHECK(fea_profile_slug((fea_profile_t)profile) != NULL,
              "slug %u", profile);
        CHECK(fea_profile_name((fea_profile_t)profile) != NULL,
              "name %u", profile);
        fea_info_t info;
        CHECK(fea_profile_info((fea_profile_t)profile, &info),
              "info %u", profile);
        CHECK(info.width == FEA_FRAME_WIDTH &&
                  info.height == FEA_FRAME_HEIGHT,
              "info dimensions %u", profile);
        CHECK(info.framebuffer_bytes == FEA_FRAME_BYTES,
              "info bytes %u", profile);
    }
    CHECK(fea_profile_slug((fea_profile_t)FEA_PROFILE_COUNT) == NULL,
          "slug out of range");
    fea_info_t info;
    CHECK(!fea_profile_info((fea_profile_t)99, &info), "info range");

    const face_render_key_t key = base_key();
    fea_probe_t probe;
    CHECK(!fea_render_frame(
              (fea_profile_t)FEA_PROFILE_COUNT, &key, 0U, frame_a(),
              FEA_PIXEL_COUNT),
          "render bad profile");
    CHECK(!fea_render_frame(FEA_PROFILE_MOCHI_CAT, NULL, 0U, frame_a(),
                            FEA_PIXEL_COUNT),
          "render NULL key");
    CHECK(!fea_render_frame(FEA_PROFILE_MOCHI_CAT, &key, 0U, NULL,
                            FEA_PIXEL_COUNT),
          "render NULL buffer");
    CHECK(!fea_render_frame(FEA_PROFILE_MOCHI_CAT, &key, 0U, frame_a(),
                            FEA_PIXEL_COUNT - 1),
          "render short buffer");
    CHECK(!fea_probe((fea_profile_t)77, &key, 0U, &probe),
          "probe bad profile");
    CHECK(!fea_probe(FEA_PROFILE_MOCHI_CAT, NULL, 0U, &probe),
          "probe NULL key");
    CHECK(!fea_probe(FEA_PROFILE_MOCHI_CAT, &key, 0U, NULL),
          "probe NULL out");
}

static void test_purity_and_coverage(void)
{
    const face_render_key_t key = base_key();
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        for (size_t index = 0;
             index < sizeof(buffer_a) / sizeof(buffer_a[0]); ++index) {
            buffer_a[index] = 0xAAAAU;
            buffer_b[index] = 0x5555U;
        }
        CHECK(fea_render_frame(
                  (fea_profile_t)profile, &key, 12345U, frame_a(),
                  FEA_PIXEL_COUNT),
              "render a %u", profile);
        CHECK(fea_render_frame(
                  (fea_profile_t)profile, &key, 12345U, frame_b(),
                  FEA_PIXEL_COUNT),
              "render b %u", profile);
        CHECK(memcmp(frame_a(), frame_b(),
                     FEA_FRAME_BYTES) == 0,
              "prefill independence %u (every pixel written)", profile);
        bool guards_intact = true;
        for (size_t index = 0; index < GUARD_PIXELS; ++index) {
            guards_intact &= buffer_a[index] == 0xAAAAU;
            guards_intact &=
                buffer_a[GUARD_PIXELS + FEA_PIXEL_COUNT + index] ==
                0xAAAAU;
            guards_intact &= buffer_b[index] == 0x5555U;
            guards_intact &=
                buffer_b[GUARD_PIXELS + FEA_PIXEL_COUNT + index] ==
                0x5555U;
        }
        CHECK(guards_intact, "guard bands %u", profile);
        /* determinism across repeated calls */
        CHECK(fea_render_frame(
                  (fea_profile_t)profile, &key, 12345U, frame_b(),
                  FEA_PIXEL_COUNT),
              "render c %u", profile);
        CHECK(memcmp(frame_a(), frame_b(), FEA_FRAME_BYTES) == 0,
              "repeat determinism %u", profile);
    }
}

static void test_expression_separability(void)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        for (uint8_t emotion = 0; emotion < FACE_EXPRESSION_COUNT;
             ++emotion) {
            render_emotion(
                (fea_profile_t)profile, emotion, fixed_clock,
                emotion_frames[emotion]);
        }
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        uint32_t distinct = 0U;
        for (unsigned emotion = 0; emotion < FACE_EXPRESSION_COUNT;
             ++emotion) {
            hashes[emotion] = frame_hash(emotion_frames[emotion]);
            bool first_occurrence = true;
            for (unsigned earlier = 0; earlier < emotion; ++earlier) {
                if (hashes[earlier] == hashes[emotion]) {
                    first_occurrence = false;
                }
            }
            distinct += first_occurrence;
        }
        CHECK(distinct == FACE_EXPRESSION_COUNT,
              "%s: %u/11 distinct emotion frames",
              fea_profile_slug((fea_profile_t)profile), distinct);

        uint32_t clear = 0U;
        for (unsigned emotion = 1; emotion < FACE_EXPRESSION_COUNT;
             ++emotion) {
            const frame_diff_t diff = compare_frames(
                emotion_frames[0], emotion_frames[emotion]);
            if (diff.roi_mean_delta >= 0.008 &&
                diff.roi_changed_fraction >= 0.018) {
                ++clear;
            }
        }
        CHECK(clear == FACE_EXPRESSION_COUNT - 1,
              "%s: %u/10 clearly non-neutral",
              fea_profile_slug((fea_profile_t)profile), clear);

        uint32_t weak_pairs = 0U;
        double pair_sum = 0.0;
        uint32_t pair_count = 0U;
        for (unsigned first = 0; first < FACE_EXPRESSION_COUNT;
             ++first) {
            for (unsigned second = first + 1U;
                 second < FACE_EXPRESSION_COUNT; ++second) {
                const frame_diff_t diff = compare_frames(
                    emotion_frames[first], emotion_frames[second]);
                pair_sum += diff.roi_mean_delta;
                ++pair_count;
                if (diff.roi_mean_delta < 0.006 ||
                    diff.roi_changed_fraction < 0.015) {
                    ++weak_pairs;
                }
            }
        }
        CHECK(weak_pairs == 0U, "%s: %u weak pairs",
              fea_profile_slug((fea_profile_t)profile), weak_pairs);
        const double mean_pair = pair_sum / (double)pair_count;
        CHECK(mean_pair >= 0.010, "%s: mean pair delta %.4f",
              fea_profile_slug((fea_profile_t)profile), mean_pair);
        printf("  %-18s mean pair ROI delta %.4f, weak %u/55\n",
               fea_profile_slug((fea_profile_t)profile), mean_pair,
               weak_pairs);
    }
}

static void test_temporal_smoothness(void)
{
    static double deltas[MOTION_FRAMES - 1];
    static double changed[MOTION_FRAMES - 1];
    static double sorted[MOTION_FRAMES - 1];
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        render_motion((fea_profile_t)profile, 0U, frame_a());
        uint32_t frozen = 0U;
        double max_delta = 0.0;
        for (uint32_t frame = 1U; frame < MOTION_FRAMES; ++frame) {
            render_motion((fea_profile_t)profile, frame, frame_b());
            const frame_diff_t diff =
                compare_frames(frame_a(), frame_b());
            deltas[frame - 1U] = diff.roi_mean_delta;
            changed[frame - 1U] = diff.roi_changed_fraction;
            frozen += diff.roi_changed_fraction == 0.0;
            if (diff.roi_mean_delta > max_delta) {
                max_delta = diff.roi_mean_delta;
            }
            memcpy(frame_a(), frame_b(), FEA_FRAME_BYTES);
        }
        memcpy(sorted, deltas, sizeof(sorted));
        qsort(sorted, MOTION_FRAMES - 1, sizeof(double),
              compare_double);
        const double median = sorted[(MOTION_FRAMES - 1) / 2];
        double jump_threshold = median * 7.0 + 0.008;
        if (jump_threshold < 0.035) {
            jump_threshold = 0.035;
        }
        uint32_t jumps = 0U;
        for (unsigned index = 0; index < MOTION_FRAMES - 1; ++index) {
            if (deltas[index] > jump_threshold &&
                changed[index] >= 0.08) {
                ++jumps;
            }
        }
        CHECK(jumps == 0U, "%s: %u abrupt jumps",
              fea_profile_slug((fea_profile_t)profile), jumps);
        CHECK(max_delta < 0.14, "%s: max delta %.4f",
              fea_profile_slug((fea_profile_t)profile), max_delta);
        CHECK(frozen < 30U, "%s: %u frozen pairs",
              fea_profile_slug((fea_profile_t)profile), frozen);
        printf("  %-18s median %.4f max %.4f frozen %u\n",
               fea_profile_slug((fea_profile_t)profile), median,
               max_delta, frozen);
    }
}

static void test_blink_kinematics(void)
{
    /* idle key, no cue: watch the aperture over 20 s */
    face_render_key_t key = base_key();
    key.controls.expression = 0U;      /* idle */
    key.controls.flags = 0U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.viseme_weight = 0U;
    enum { STEP = 160, STEPS = 20 * SAMPLE_RATE / 160 };
    int32_t previous = 256;
    uint32_t blink_count = 0U;
    uint32_t full_closures = 0U;
    uint32_t close_start = 0U;
    uint32_t closed_at = 0U;
    uint32_t reopened_at = 0U;
    bool measured = false;
    bool in_blink = false;
    for (uint32_t step = 0; step < STEPS; ++step) {
        const uint32_t clock = step * STEP;
        fea_probe_t probe;
        CHECK(fea_probe(FEA_PROFILE_EMOTE_STICKER, &key, clock, &probe),
              "probe blink");
        const int32_t aperture = probe.eye_open_q8[0];
        if (!in_blink && aperture < 128 && previous >= 128) {
            in_blink = true;
            ++blink_count;
            close_start = clock;
            closed_at = 0U;
        }
        if (in_blink && aperture < 40 && closed_at == 0U) {
            closed_at = clock;
            ++full_closures;
        }
        if (in_blink && aperture >= 200) {
            reopened_at = clock;
            in_blink = false;
            if (!measured && closed_at > close_start) {
                const uint32_t close_time = closed_at - close_start;
                const uint32_t reopen_time = reopened_at - closed_at;
                CHECK(reopen_time > close_time,
                      "reopen (%u) must be slower than close (%u)",
                      reopen_time, close_time);
                measured = true;
            }
        }
        previous = aperture;
    }
    CHECK(blink_count >= 3U && blink_count <= 9U,
          "idle blinks in 20 s: %u", blink_count);
    CHECK(full_closures >= 2U, "full closures: %u", full_closures);
    CHECK(measured, "blink asymmetry was measured");

    /* the BLINKING flag must force lid motion */
    face_render_key_t flagged = base_key();
    flagged.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
    fea_probe_t probe;
    CHECK(fea_probe(FEA_PROFILE_EMOTE_STICKER, &flagged,
                    1900U, &probe),
          "probe forced blink");
    CHECK(probe.eye_open_q8[0] < 120, "BLINKING flag closes the eye");
}

static void test_corner_parenting(void)
{
    const uint32_t clock = SAMPLE_RATE * 3U;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        fea_probe_t neutral;
        face_render_key_t key = base_key();
        CHECK(fea_probe((fea_profile_t)profile, &key, clock, &neutral),
              "probe neutral");
        if (neutral.has_mouth == 0U) {
            continue;
        }
        /* raising the left corner must raise the left lip endpoint and
         * leave the right endpoint essentially alone */
        key.mouth_corner_left = 100;
        fea_probe_t raised;
        CHECK(fea_probe((fea_profile_t)profile, &key, clock, &raised),
              "probe raised");
        CHECK(raised.corner_y_q4[0] < neutral.corner_y_q4[0] - 24,
              "%s: left corner rises (%d -> %d)",
              fea_profile_slug((fea_profile_t)profile),
              neutral.corner_y_q4[0], raised.corner_y_q4[0]);
        CHECK(abs(raised.corner_y_q4[1] - neutral.corner_y_q4[1]) <=
                  raised.jaw_q4 / 4 + 24,
              "%s: right corner mostly unaffected",
              fea_profile_slug((fea_profile_t)profile));
        key.mouth_corner_left = -100;
        fea_probe_t lowered;
        CHECK(fea_probe((fea_profile_t)profile, &key, clock, &lowered),
              "probe lowered");
        CHECK(lowered.corner_y_q4[0] > neutral.corner_y_q4[0] + 24,
              "%s: left corner drops",
              fea_profile_slug((fea_profile_t)profile));

        /* parenting: a head-pose shift must carry the corners with the
         * mouth center, exactly */
        key = base_key();
        key.head_yaw = 90;
        fea_probe_t shifted;
        CHECK(fea_probe((fea_profile_t)profile, &key, clock, &shifted),
              "probe shifted");
        const int32_t mouth_dx =
            shifted.mouth_cx_q4 - neutral.mouth_cx_q4;
        CHECK(mouth_dx > 24, "%s: yaw moves the mouth",
              fea_profile_slug((fea_profile_t)profile));
        const int32_t corner_dx =
            shifted.corner_x_q4[0] - neutral.corner_x_q4[0];
        CHECK(abs(corner_dx - mouth_dx) <= 8,
              "%s: corners travel with the mouth (%d vs %d)",
              fea_profile_slug((fea_profile_t)profile), corner_dx,
              mouth_dx);
    }
}

static void test_acting_curve(void)
{
    const uint32_t clock = SAMPLE_RATE * 5U;
    face_render_key_t key = base_key();
    key.stage_expression = FACE_EXPRESSION_JOY;
    int16_t act_at[256];
    for (unsigned weight = 0; weight < 256; ++weight) {
        key.expression_weight = (uint8_t)weight;
        fea_probe_t probe;
        CHECK(fea_probe(FEA_PROFILE_MOCHI_CAT, &key, clock, &probe),
              "probe acting %u", weight);
        act_at[weight] = probe.act_q8;
    }
    CHECK(act_at[0] == 0, "acting starts at zero");
    int16_t dip = 0;
    for (unsigned weight = 1; weight < 48; ++weight) {
        if (act_at[weight] < dip) {
            dip = act_at[weight];
        }
    }
    CHECK(dip < -8, "anticipation dip present (%d)", dip);
    CHECK(act_at[128] > 64, "mid-attack rise (%d)", act_at[128]);
    int16_t peak = 0;
    for (unsigned weight = 160; weight < 250; ++weight) {
        if (act_at[weight] > peak) {
            peak = act_at[weight];
        }
    }
    CHECK(peak > 262, "overshoot present (%d)", peak);
    CHECK(act_at[255] == 256, "exact settle (%d)", act_at[255]);
}

static void test_viseme_articulation(void)
{
    const uint32_t clock = SAMPLE_RATE * 4U + 71U;
    static const uint8_t shapes[5] = { 0, 3, 5, 6, 2 }; /* AA O PP SS I */
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        uint32_t hashes[5];
        int32_t jaws[5];
        face_render_key_t neutral_key = base_key();
        fea_probe_t neutral_probe;
        CHECK(fea_probe((fea_profile_t)profile, &neutral_key, clock,
                        &neutral_probe),
              "viseme neutral probe");
        for (unsigned index = 0; index < 5; ++index) {
            face_render_key_t key = base_key();
            key.viseme = shapes[index];
            key.viseme_weight = 255U;
            key.viseme_blend = 0U;
            CHECK(fea_render_frame(
                      (fea_profile_t)profile, &key, clock, frame_a(),
                      FEA_PIXEL_COUNT),
                  "viseme render");
            hashes[index] = frame_hash(frame_a());
            fea_probe_t probe;
            CHECK(fea_probe((fea_profile_t)profile, &key, clock,
                            &probe),
                  "viseme probe");
            jaws[index] = probe.jaw_q4;
        }
        for (unsigned first = 0; first < 5; ++first) {
            for (unsigned second = first + 1U; second < 5; ++second) {
                CHECK(hashes[first] != hashes[second],
                      "%s: visemes %u vs %u identical",
                      fea_profile_slug((fea_profile_t)profile),
                      shapes[first], shapes[second]);
            }
        }
        if (neutral_probe.has_mouth != 0U) {
            CHECK(jaws[0] > jaws[2] + 32,
                  "%s: AA jaw far wider than PP (%d vs %d)",
                  fea_profile_slug((fea_profile_t)profile), jaws[0],
                  jaws[2]);
            CHECK(jaws[2] < 96, "%s: PP nearly closed (%d)",
                  fea_profile_slug((fea_profile_t)profile),
                  jaws[2]);
        } else {
            /* eye-only: AA must still stretch the light vs PP */
            CHECK(jaws[0] > jaws[2],
                  "%s: AA light taller than PP (%d vs %d)",
                  fea_profile_slug((fea_profile_t)profile), jaws[0],
                  jaws[2]);
        }
    }
    /* vocabulary sets: the same index must map differently */
    face_render_key_t key = base_key();
    key.viseme = 2U;                    /* OVR15 I vs VRM5 U */
    key.viseme_weight = 255U;
    CHECK(fea_render_frame(FEA_PROFILE_EMOTE_STICKER, &key, clock,
                           frame_a(), FEA_PIXEL_COUNT),
          "set render a");
    key.viseme_set = 1U;                /* FACE_VISEME_SET_VRM5 */
    CHECK(fea_render_frame(FEA_PROFILE_EMOTE_STICKER, &key, clock,
                           frame_b(), FEA_PIXEL_COUNT),
          "set render b");
    CHECK(frame_hash(frame_a()) != frame_hash(frame_b()),
          "viseme_set changes the mapping");
}

static void test_all_forty_bytes_live(void)
{
    /* every byte of the IR must be able to change some pixel for every
     * profile: perturb each byte across several values and clocks */
    const uint32_t clocks[2] = { SAMPLE_RATE * 2U + 17U,
                                 SAMPLE_RATE * 9U + 401U };
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        unsigned live = 0U;
        for (unsigned byte_index = 0; byte_index < 40U; ++byte_index) {
            face_render_key_t key = base_key();
            key.stage_expression = FACE_EXPRESSION_WARM;
            key.expression_weight = 200U;
            key.cheek = 60U;
            key.tongue = 90U;
            key.mouth_corner_left = 20;
            key.mouth_corner_right = -20;
            bool changed = false;
            for (unsigned clock_index = 0;
                 clock_index < 2U && !changed; ++clock_index) {
                CHECK(fea_render_frame(
                          (fea_profile_t)profile, &key,
                          clocks[clock_index], frame_a(),
                          FEA_PIXEL_COUNT),
                      "live base render");
                const uint32_t reference = frame_hash(frame_a());
                static const uint8_t probes[3] = { 0x00U, 0x7fU,
                                                   0xffU };
                for (unsigned value_index = 0;
                     value_index < 3U && !changed; ++value_index) {
                    face_render_key_t mutated = key;
                    uint8_t *raw = (uint8_t *)&mutated;
                    if (raw[byte_index] == probes[value_index]) {
                        continue;
                    }
                    raw[byte_index] = probes[value_index];
                    CHECK(fea_render_frame(
                              (fea_profile_t)profile, &mutated,
                              clocks[clock_index], frame_b(),
                              FEA_PIXEL_COUNT),
                          "live mutated render");
                    changed = frame_hash(frame_b()) != reference;
                }
            }
            if (changed) {
                ++live;
            } else {
                printf("  %-18s byte %u is dead\n",
                       fea_profile_slug((fea_profile_t)profile),
                       byte_index);
            }
        }
        CHECK(live == 40U, "%s: %u/40 IR bytes live",
              fea_profile_slug((fea_profile_t)profile), live);
    }
}

static void check_probe_bounds(
    const fea_probe_t *probe, const char *slug, unsigned tag)
{
    CHECK(probe->extent_left_q4 >= 0 &&
              probe->extent_right_q4 <= (FEA_FRAME_WIDTH << 4) &&
              probe->extent_top_q4 >= 0 &&
              probe->extent_bottom_q4 <= (FEA_FRAME_HEIGHT << 4),
          "%s[%u]: extent inside frame (%d %d %d %d)", slug, tag,
          probe->extent_left_q4, probe->extent_top_q4,
          probe->extent_right_q4, probe->extent_bottom_q4);
    for (int side = 0; side < 2; ++side) {
        CHECK(probe->pupil_x_q4[side] >= 0 &&
                  probe->pupil_x_q4[side] <= (FEA_FRAME_WIDTH << 4) &&
                  probe->pupil_y_q4[side] >= 0 &&
                  probe->pupil_y_q4[side] <= (FEA_FRAME_HEIGHT << 4),
              "%s[%u]: pupil inside frame", slug, tag);
        CHECK(probe->eye_open_q8[side] >= 0 &&
                  probe->eye_open_q8[side] <= 256,
              "%s[%u]: aperture in range", slug, tag);
    }
    if (probe->has_mouth != 0U) {
        for (int side = 0; side < 2; ++side) {
            CHECK(probe->corner_x_q4[side] >= (2 << 4) &&
                      probe->corner_x_q4[side] <=
                          ((FEA_FRAME_WIDTH - 2) << 4) &&
                      probe->corner_y_q4[side] >= (2 << 4) &&
                      probe->corner_y_q4[side] <=
                          ((FEA_FRAME_HEIGHT - 2) << 4),
                  "%s[%u]: mouth corner on screen", slug, tag);
        }
    }
}

static void test_adversarial_fuzz(void)
{
    uint32_t rng = 0xC0FFEEU;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        const char *slug = fea_profile_slug((fea_profile_t)profile);
        /* structured extremes first */
        static const uint8_t patterns[4] = { 0x00U, 0xffU, 0x80U,
                                             0x7fU };
        for (unsigned index = 0; index < 4U; ++index) {
            face_render_key_t key;
            memset(&key, patterns[index], sizeof(key));
            for (size_t g = 0;
                 g < sizeof(buffer_a) / sizeof(buffer_a[0]); ++g) {
                buffer_a[g] = 0x1234U;
            }
            CHECK(fea_render_frame(
                      (fea_profile_t)profile, &key, 0xfffffff0U,
                      frame_a(), FEA_PIXEL_COUNT),
                  "%s: extreme pattern %u renders", slug, index);
            bool guards_intact = true;
            for (size_t g = 0; g < GUARD_PIXELS; ++g) {
                guards_intact &= buffer_a[g] == 0x1234U;
                guards_intact &=
                    buffer_a[GUARD_PIXELS + FEA_PIXEL_COUNT + g] ==
                    0x1234U;
            }
            CHECK(guards_intact, "%s: extreme guards %u", slug, index);
            fea_probe_t probe;
            CHECK(fea_probe((fea_profile_t)profile, &key, 0xfffffff0U,
                            &probe),
                  "%s: extreme probe %u", slug, index);
            check_probe_bounds(&probe, slug, index);
        }
        /* randomized keys */
        for (unsigned trial = 0; trial < FUZZ_KEYS / 5U; ++trial) {
            face_render_key_t key;
            uint8_t *raw = (uint8_t *)&key;
            for (unsigned byte_index = 0; byte_index < sizeof(key);
                 ++byte_index) {
                raw[byte_index] = (uint8_t)splitmix32(&rng);
            }
            const uint32_t clock = splitmix32(&rng);
            CHECK(fea_render_frame(
                      (fea_profile_t)profile, &key, clock, frame_a(),
                      FEA_PIXEL_COUNT),
                  "%s: fuzz %u renders", slug, trial);
            CHECK(fea_render_frame(
                      (fea_profile_t)profile, &key, clock, frame_b(),
                      FEA_PIXEL_COUNT),
                  "%s: fuzz %u repeat", slug, trial);
            CHECK(memcmp(frame_a(), frame_b(), FEA_FRAME_BYTES) == 0,
                  "%s: fuzz %u deterministic", slug, trial);
            fea_probe_t probe;
            CHECK(fea_probe((fea_profile_t)profile, &key, clock,
                            &probe),
                  "%s: fuzz %u probe", slug, trial);
            check_probe_bounds(&probe, slug, 1000U + trial);
        }
    }
}

/* ------------------------------------------------------------ golden */

typedef struct {
    const char *slug;
    const char *label;
    uint32_t hash;
} golden_case_t;

static const golden_case_t GOLDEN[] = {
#include "golden_hashes.inc"
};

static uint32_t compute_case(
    unsigned profile, unsigned case_index)
{
    const uint32_t fixed_clock = SAMPLE_RATE * 7U + 211U;
    if (case_index < FACE_EXPRESSION_COUNT) {
        render_emotion(
            (fea_profile_t)profile, (uint8_t)case_index, fixed_clock,
            frame_a());
        return frame_hash(frame_a());
    }
    if (case_index < FACE_EXPRESSION_COUNT + 4U) {
        const uint32_t frame =
            30U + 25U * (case_index - FACE_EXPRESSION_COUNT);
        render_motion((fea_profile_t)profile, frame, frame_a());
        return frame_hash(frame_a());
    }
    face_render_key_t key;
    memset(&key, case_index == FACE_EXPRESSION_COUNT + 4U ? 0x00 : 0xff,
           sizeof(key));
    (void)fea_render_frame(
        (fea_profile_t)profile, &key, 0xfffffff0U, frame_a(),
        FEA_PIXEL_COUNT);
    return frame_hash(frame_a());
}

enum { CASES_PER_PROFILE = FACE_EXPRESSION_COUNT + 6 };

static void print_case_table(bool as_include)
{
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        for (unsigned case_index = 0; case_index < CASES_PER_PROFILE;
             ++case_index) {
            char label[32];
            if (case_index < FACE_EXPRESSION_COUNT) {
                snprintf(label, sizeof(label), "expr%02u", case_index);
            } else if (case_index < FACE_EXPRESSION_COUNT + 4U) {
                snprintf(label, sizeof(label), "frame%03u",
                         30U + 25U * (case_index -
                                      FACE_EXPRESSION_COUNT));
            } else {
                snprintf(label, sizeof(label), "%s",
                         case_index == FACE_EXPRESSION_COUNT + 4U
                             ? "all00" : "allff");
            }
            const uint32_t hash = compute_case(profile, case_index);
            if (as_include) {
                printf("{ \"%s\", \"%s\", 0x%08" PRIx32 "U },\n",
                       fea_profile_slug((fea_profile_t)profile), label,
                       hash);
            } else {
                printf("%s %s %08" PRIx32 "\n",
                       fea_profile_slug((fea_profile_t)profile), label,
                       hash);
            }
        }
    }
}

static void test_golden(void)
{
    const size_t count = sizeof(GOLDEN) / sizeof(GOLDEN[0]);
    if (count == 0U) {
        printf("  (no golden hashes frozen yet)\n");
        return;
    }
    CHECK(count == FEA_PROFILE_COUNT * CASES_PER_PROFILE,
          "golden table size %zu", count);
    size_t case_cursor = 0U;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        for (unsigned case_index = 0; case_index < CASES_PER_PROFILE;
             ++case_index) {
            if (case_cursor >= count) {
                return;
            }
            const uint32_t hash = compute_case(profile, case_index);
            CHECK(hash == GOLDEN[case_cursor].hash,
                  "golden %s/%s: %08" PRIx32 " != %08" PRIx32,
                  GOLDEN[case_cursor].slug, GOLDEN[case_cursor].label,
                  hash, GOLDEN[case_cursor].hash);
            ++case_cursor;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--dump-hashes") == 0) {
        print_case_table(false);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--write-golden") == 0) {
        print_case_table(true);
        return 0;
    }
    printf("fable_expression_actors_v3 test suite\n");
    test_abi();
    test_purity_and_coverage();
    printf("expression separability:\n");
    test_expression_separability();
    printf("temporal smoothness:\n");
    test_temporal_smoothness();
    test_blink_kinematics();
    test_corner_parenting();
    test_acting_curve();
    test_viseme_articulation();
    test_all_forty_bytes_live();
    test_adversarial_fuzz();
    test_golden();
    printf("%" PRIu32 " checks, %" PRIu32 " failures\n", checks_run,
           checks_failed);
    return checks_failed == 0U ? 0 : 1;
}
