#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_robot_eyes.h"
#include "face_stage.h"

enum {
    GUARD_WORDS = 12,
    TEMPORAL_FPS = 30,
    TEMPORAL_SECONDS = 20,
    /*
     * At the production 30 fps cadence, autonomous eye motion must not
     * traverse more than 3/8 of the full aperture in one rendered frame.
     * Larger steps turn a blink into a two-frame cut on the large
     * Vector/Cozmo eye silhouettes.
     */
    MAX_AUTONOMOUS_APERTURE_STEP_Q8 = 96,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_ROBOT_EYES_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < FACE_ROBOT_EYES_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static void initialise_guard(guarded_frame_t *frame)
{
    memset(frame, 0xa5, sizeof(*frame));
    for (size_t index = 0; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0xface1000U + (uint32_t)index;
        frame->after[index] = 0xc0de2000U + (uint32_t)index;
    }
}

static void assert_guard_unchanged(const guarded_frame_t *frame)
{
    for (size_t index = 0; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0xface1000U + index);
        assert(frame->after[index] == 0xc0de2000U + index);
    }
}

static face_render_key_t baseline_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 132U;
    key.controls.mouth_width = 156U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_press = 18U;
    key.controls.mouth_teeth = 72U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 198U;
    key.audio_level = 116U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 54U;
    key.speech_phase = FACE_SPEECH_IDLE;
    key.affect_arousal = 128U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_NEUTRAL;
    key.expression_weight = 255U;
    return key;
}

static size_t differing_pixels(
    const uint16_t *first, const uint16_t *second)
{
    size_t changed = 0U;
    for (size_t index = 0; index < FACE_ROBOT_EYES_PIXEL_COUNT; ++index) {
        changed += first[index] != second[index];
    }
    return changed;
}

static void test_metadata_and_rejection(void)
{
    assert(face_robot_eyes_profile_count() ==
        FACE_ROBOT_EYES_PROFILE_COUNT);
    for (size_t raw = 0U; raw < face_robot_eyes_profile_count(); ++raw) {
        const face_robot_eyes_profile_t profile =
            (face_robot_eyes_profile_t)raw;
        const char *slug = face_robot_eyes_profile_slug(profile);
        const char *name = face_robot_eyes_profile_name(profile);
        assert(slug != NULL && slug[0] != '\0');
        assert(name != NULL && name[0] != '\0');
        for (size_t earlier = 0U; earlier < raw; ++earlier) {
            assert(strcmp(
                slug,
                face_robot_eyes_profile_slug(
                    (face_robot_eyes_profile_t)earlier)) != 0);
        }
    }
    assert(face_robot_eyes_profile_slug(
        (face_robot_eyes_profile_t)-1) == NULL);
    assert(face_robot_eyes_profile_name(
        FACE_ROBOT_EYES_PROFILE_COUNT) == NULL);

    const face_render_key_t key = baseline_key();
    face_robot_eyes_pose_t pose;
    uint16_t pixels[FACE_ROBOT_EYES_PIXEL_COUNT];
    assert(!face_robot_eyes_resolve(
        FACE_ROBOT_EYES_PROFILE_COUNT, &key, 0U, &pose));
    assert(!face_robot_eyes_resolve(
        FACE_ROBOT_EYES_COZMO_CUBIC, NULL, 0U, &pose));
    assert(!face_robot_eyes_resolve(
        FACE_ROBOT_EYES_COZMO_CUBIC, &key, 0U, NULL));
    assert(!face_robot_eyes_render(
        FACE_ROBOT_EYES_COZMO_CUBIC,
        &key,
        0U,
        pixels,
        FACE_ROBOT_EYES_PIXEL_COUNT - 1U));
}

static void test_activity_and_emotion_are_independent(void)
{
    face_render_key_t key = baseline_key();
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.stage_expression = FACE_EXPRESSION_JOY;
    key.expression_weight = 255U;

    face_robot_eyes_pose_t pose;
    assert(face_robot_eyes_resolve(
        FACE_ROBOT_EYES_COZMO_CUBIC,
        &key,
        16000U + 211U,
        &pose));
    assert(pose.activity == FACE_ACTIVITY_LISTENING);
    assert(pose.stage_expression == FACE_EXPRESSION_JOY);
    assert(pose.source.controls.expression == FACE_ACTIVITY_LISTENING);
    assert(pose.source.stage_expression == FACE_EXPRESSION_JOY);
    assert(memcmp(&pose.source, &key, sizeof(key)) == 0);

    /*
     * Schema v1's final byte was reserved. It must not accidentally become
     * an emotion when an old capture happens to contain a small value.
     */
    key.schema_version = 1U;
    key.stage_expression = FACE_EXPRESSION_DETERMINED;
    assert(face_robot_eyes_resolve(
        FACE_ROBOT_EYES_COZMO_CUBIC,
        &key,
        16000U + 211U,
        &pose));
    assert(pose.activity == FACE_ACTIVITY_LISTENING);
    assert(pose.stage_expression == FACE_EXPRESSION_NEUTRAL);
}

static void test_expression_separability_and_safe_area(void)
{
    enum {
        SAFE_MARGIN_X = 5,
        SAFE_MARGIN_Y = 5,
    };
    guarded_frame_t frames[FACE_EXPRESSION_COUNT];
    const uint32_t fixed_clock = 16000U + 211U;
    size_t smallest_neutral_delta = FACE_ROBOT_EYES_PIXEL_COUNT;

    for (size_t raw = 0U; raw < face_robot_eyes_profile_count(); ++raw) {
        const face_robot_eyes_profile_t profile =
            (face_robot_eyes_profile_t)raw;
        uint32_t hashes[FACE_EXPRESSION_COUNT];
        for (uint8_t expression = FACE_EXPRESSION_NEUTRAL;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = baseline_key();
            key.stage_expression = expression;
            key.expression_weight = 255U;
            initialise_guard(&frames[expression]);
            assert(face_robot_eyes_render(
                profile,
                &key,
                fixed_clock,
                frames[expression].pixels,
                FACE_ROBOT_EYES_PIXEL_COUNT));
            assert_guard_unchanged(&frames[expression]);
            hashes[expression] =
                frame_hash(frames[expression].pixels);
        }

        for (size_t first = 0U; first < FACE_EXPRESSION_COUNT; ++first) {
            for (size_t second = first + 1U;
                 second < FACE_EXPRESSION_COUNT;
                 ++second) {
                assert(hashes[first] != hashes[second]);
            }
        }
        for (size_t expression = 1U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            const size_t delta = differing_pixels(
                frames[FACE_EXPRESSION_NEUTRAL].pixels,
                frames[expression].pixels);
            if (delta < smallest_neutral_delta) {
                smallest_neutral_delta = delta;
            }
            assert(delta >= 20U);

            /*
             * The outer five-pixel frame is a hard visual safe area. A
             * canary catches memory overwrite; this catches aesthetic
             * clipping caused by an expression moving geometry off-screen.
             */
            for (size_t y = 0U; y < FACE_ROBOT_EYES_HEIGHT; ++y) {
                for (size_t x = 0U; x < FACE_ROBOT_EYES_WIDTH; ++x) {
                    if (x >= SAFE_MARGIN_X &&
                        x < FACE_ROBOT_EYES_WIDTH - SAFE_MARGIN_X &&
                        y >= SAFE_MARGIN_Y &&
                        y < FACE_ROBOT_EYES_HEIGHT - SAFE_MARGIN_Y) {
                        continue;
                    }
                    const size_t index =
                        y * FACE_ROBOT_EYES_WIDTH + x;
                    assert(frames[expression].pixels[index] ==
                        frames[FACE_EXPRESSION_NEUTRAL].pixels[index]);
                }
            }
        }
    }
    printf(
        "robot-eyes expression separability: min neutral delta=%zu px\n",
        smallest_neutral_delta);
}

static void test_pcm_articulation_survives_stage_direction(void)
{
    face_render_key_t quiet = baseline_key();
    quiet.controls.expression = FACE_ACTIVITY_SPEAKING;
    quiet.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    quiet.speech_phase = FACE_SPEECH_ACTIVE;
    quiet.controls.mouth_open = 24U;
    quiet.audio_level = 18U;
    quiet.viseme_weight = 32U;
    quiet.stage_expression = FACE_EXPRESSION_JOY;

    face_render_key_t loud = quiet;
    loud.controls.mouth_open = 238U;
    loud.controls.mouth_width = 224U;
    loud.audio_level = 236U;
    loud.viseme = FACE_VISEME_O;
    loud.viseme_weight = 250U;

    face_robot_eyes_pose_t quiet_pose;
    face_robot_eyes_pose_t loud_pose;
    assert(face_robot_eyes_resolve(
        FACE_ROBOT_EYES_M5_MANGA,
        &quiet,
        16000U * 3U,
        &quiet_pose));
    assert(face_robot_eyes_resolve(
        FACE_ROBOT_EYES_M5_MANGA,
        &loud,
        16000U * 3U,
        &loud_pose));
    assert(quiet_pose.activity == FACE_ACTIVITY_SPEAKING);
    assert(loud_pose.activity == FACE_ACTIVITY_SPEAKING);
    assert(quiet_pose.stage_expression == FACE_EXPRESSION_JOY);
    assert(loud_pose.stage_expression == FACE_EXPRESSION_JOY);
    assert(quiet_pose.source.controls.mouth_open == 24U);
    assert(loud_pose.source.controls.mouth_open == 238U);
    assert(quiet_pose.resolved_controls.mouth_open !=
        loud_pose.resolved_controls.mouth_open);
    assert(quiet_pose.scale_y_q8 != loud_pose.scale_y_q8);

    uint16_t quiet_pixels[FACE_ROBOT_EYES_PIXEL_COUNT];
    uint16_t loud_pixels[FACE_ROBOT_EYES_PIXEL_COUNT];
    assert(face_robot_eyes_render_resolved(
        FACE_ROBOT_EYES_M5_MANGA,
        &quiet_pose,
        quiet_pixels,
        FACE_ROBOT_EYES_PIXEL_COUNT));
    assert(face_robot_eyes_render_resolved(
        FACE_ROBOT_EYES_M5_MANGA,
        &loud_pose,
        loud_pixels,
        FACE_ROBOT_EYES_PIXEL_COUNT));
    assert(differing_pixels(quiet_pixels, loud_pixels) >= 40U);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void test_extreme_ir_fuzz_and_determinism(void)
{
    enum {
        EXTREME_SAFE_MARGIN = 3,
    };
    guarded_frame_t first;
    guarded_frame_t second;
    uint16_t background_reference[FACE_ROBOT_EYES_PIXEL_COUNT];
    uint32_t random = 0x51a7c0deU;
    for (size_t raw = 0U; raw < face_robot_eyes_profile_count(); ++raw) {
        const face_robot_eyes_profile_t profile =
            (face_robot_eyes_profile_t)raw;
        const face_render_key_t reference_key = baseline_key();
        assert(face_robot_eyes_render(
            profile,
            &reference_key,
            0U,
            background_reference,
            FACE_ROBOT_EYES_PIXEL_COUNT));
        for (size_t iteration = 0U; iteration < 128U; ++iteration) {
            face_render_key_t key;
            uint8_t *bytes = (uint8_t *)&key;
            for (size_t index = 0U; index < sizeof(key); ++index) {
                bytes[index] = (uint8_t)(next_random(&random) >> 24);
            }
            const uint32_t sample_clock = next_random(&random);
            initialise_guard(&first);
            initialise_guard(&second);
            assert(face_robot_eyes_render(
                profile,
                &key,
                sample_clock,
                first.pixels,
                FACE_ROBOT_EYES_PIXEL_COUNT));
            assert(face_robot_eyes_render(
                profile,
                &key,
                sample_clock,
                second.pixels,
                FACE_ROBOT_EYES_PIXEL_COUNT));
            assert_guard_unchanged(&first);
            assert_guard_unchanged(&second);
            assert(memcmp(
                first.pixels,
                second.pixels,
                sizeof(first.pixels)) == 0);
            for (size_t y = 0U; y < FACE_ROBOT_EYES_HEIGHT; ++y) {
                for (size_t x = 0U; x < FACE_ROBOT_EYES_WIDTH; ++x) {
                    if (x >= EXTREME_SAFE_MARGIN &&
                        x < FACE_ROBOT_EYES_WIDTH - EXTREME_SAFE_MARGIN &&
                        y >= EXTREME_SAFE_MARGIN &&
                        y < FACE_ROBOT_EYES_HEIGHT - EXTREME_SAFE_MARGIN) {
                        continue;
                    }
                    const size_t index =
                        y * FACE_ROBOT_EYES_WIDTH + x;
                    assert(first.pixels[index] ==
                        background_reference[index]);
                }
            }
        }
    }
}

static void test_temporal_smoothing(void)
{
    uint16_t previous[FACE_ROBOT_EYES_PIXEL_COUNT];
    uint16_t current[FACE_ROBOT_EYES_PIXEL_COUNT];
    size_t worst_changed = 0U;
    int32_t worst_gaze_step = 0;
    int32_t worst_open_step = 0;

    for (size_t raw = 0U; raw < face_robot_eyes_profile_count(); ++raw) {
        const face_robot_eyes_profile_t profile =
            (face_robot_eyes_profile_t)raw;
        face_render_key_t key = baseline_key();
        key.stage_expression = FACE_EXPRESSION_WARM;
        key.controls.expression = FACE_ACTIVITY_LISTENING;
        face_robot_eyes_pose_t prior_pose;
        assert(face_robot_eyes_resolve(profile, &key, 0U, &prior_pose));
        assert(face_robot_eyes_render_resolved(
            profile,
            &prior_pose,
            previous,
            FACE_ROBOT_EYES_PIXEL_COUNT));

        for (uint32_t frame = 1U;
             frame < TEMPORAL_FPS * TEMPORAL_SECONDS;
             ++frame) {
            const uint32_t clock =
                (uint32_t)(((uint64_t)frame * 16000U) / TEMPORAL_FPS);
            face_robot_eyes_pose_t pose;
            assert(face_robot_eyes_resolve(profile, &key, clock, &pose));
            assert(face_robot_eyes_render_resolved(
                profile,
                &pose,
                current,
                FACE_ROBOT_EYES_PIXEL_COUNT));
            const size_t changed = differing_pixels(previous, current);
            if (changed > worst_changed) {
                worst_changed = changed;
            }
            const int32_t gaze_step =
                pose.gaze_x_q8 > prior_pose.gaze_x_q8
                    ? pose.gaze_x_q8 - prior_pose.gaze_x_q8
                    : prior_pose.gaze_x_q8 - pose.gaze_x_q8;
            if (gaze_step > worst_gaze_step) {
                worst_gaze_step = gaze_step;
            }
            for (int eye = 0; eye < 2; ++eye) {
                const int32_t open_step =
                    pose.openness_q8[eye] >
                            prior_pose.openness_q8[eye]
                        ? pose.openness_q8[eye] -
                              prior_pose.openness_q8[eye]
                        : prior_pose.openness_q8[eye] -
                              pose.openness_q8[eye];
                if (open_step > worst_open_step) {
                    worst_open_step = open_step;
                }
            }
            assert(changed < FACE_ROBOT_EYES_PIXEL_COUNT * 2U / 5U);
            assert(gaze_step <= 240);
            assert(worst_open_step <= MAX_AUTONOMOUS_APERTURE_STEP_Q8);
            memcpy(previous, current, sizeof(previous));
            prior_pose = pose;
        }
    }
    printf(
        "robot-eyes temporal: worst=%zu px, gaze-step=%ld, "
        "open-step=%ld @ %d fps\n",
        worst_changed,
        (long)worst_gaze_step,
        (long)worst_open_step,
        TEMPORAL_FPS);
}

static void benchmark(void)
{
    enum {
        BENCHMARK_FRAMES = 1200,
    };
    face_render_key_t key = baseline_key();
    uint16_t pixels[FACE_ROBOT_EYES_PIXEL_COUNT];
    volatile uint32_t sink = 0U;
    const clock_t started = clock();
    for (uint32_t frame = 0U; frame < BENCHMARK_FRAMES; ++frame) {
        const face_robot_eyes_profile_t profile =
            (face_robot_eyes_profile_t)(
                frame % FACE_ROBOT_EYES_PROFILE_COUNT);
        assert(face_robot_eyes_render(
            profile,
            &key,
            frame * (16000U / 30U),
            pixels,
            FACE_ROBOT_EYES_PIXEL_COUNT));
        sink ^= pixels[(frame * 131U) % FACE_ROBOT_EYES_PIXEL_COUNT];
    }
    const clock_t elapsed = clock() - started;
    const double us_per_frame =
        (double)elapsed * 1000000.0 /
        ((double)CLOCKS_PER_SEC * BENCHMARK_FRAMES);
    printf(
        "robot-eyes benchmark: %.1f us/frame "
        "(%d frames, sink=%lu)\n",
        us_per_frame,
        BENCHMARK_FRAMES,
        (unsigned long)sink);
    assert(us_per_frame < 5000.0);
}

int main(void)
{
    assert(sizeof(face_render_key_t) == FACE_RENDER_KEY_BYTES);
    test_metadata_and_rejection();
    test_activity_and_emotion_are_independent();
    test_expression_separability_and_safe_area();
    test_pcm_articulation_survives_stage_direction();
    test_extreme_ir_fuzz_and_determinism();
    test_temporal_smoothing();
    benchmark();
    puts("face_robot_eyes_test: PASS");
    return 0;
}
