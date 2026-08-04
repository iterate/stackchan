/*
 * fable_toon_acting — host timing benchmark (-O3).
 *
 * Animates a speaking key over the canonical 533-samples-per-frame
 * clock and reports us/frame per profile plus solver-only time.
 * Numbers are host wall-clock; the README derates them for ESP32-S3.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fta.h"

enum {
    WARMUP_FRAMES = 30,
    BENCH_FRAMES = 600,
};

static uint16_t frame[FTA_PIXEL_COUNT];

static face_render_key_t animated_key(uint32_t frame_index)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = (uint8_t)((frame_index * 11U) % 255U);
    key.controls.mouth_width = (uint8_t)(120U + (frame_index * 7U) % 100U);
    key.controls.mouth_round = (uint8_t)((frame_index * 5U) % 200U);
    key.controls.mouth_teeth = (uint8_t)((frame_index * 3U) % 160U);
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = (int8_t)((int32_t)(frame_index % 100U) - 50);
    key.controls.look_y = (int8_t)((int32_t)(frame_index % 60U) - 30);
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = (uint8_t)(frame_index % 15U);
    key.viseme_weight = 220U;
    key.viseme_set = 0U;
    key.viseme_secondary = (uint8_t)((frame_index + 4U) % 15U);
    key.viseme_blend = (uint8_t)((frame_index * 9U) % 255U);
    key.audio_level = (uint8_t)((frame_index * 13U) % 255U);
    key.speech_phase = 2U;
    key.mouth_corner_left = (int8_t)((int32_t)(frame_index % 120U) - 60);
    key.mouth_corner_right = (int8_t)((int32_t)(frame_index % 90U) - 45);
    key.cheek = (uint8_t)((frame_index * 2U) % 255U);
    key.head_roll = (int8_t)((int32_t)(frame_index % 40U) - 20);
    key.head_yaw = (int8_t)((int32_t)(frame_index % 80U) - 40);
    key.affect_arousal = 140U;
    key.attention = 200U;
    key.stage_expression = (uint8_t)((frame_index / 60U) % 11U);
    key.expression_weight = 230U;
    key.schema_version = 2U;
    return key;
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(void)
{
    uint32_t checksum = 0U;
    printf("fable_toon_acting benchmark (%d frames per profile)\n",
           BENCH_FRAMES);
    double total_us = 0.0;
    for (size_t p = 0; p < fta_profile_count(); ++p) {
        const fta_profile_t profile = (fta_profile_t)p;
        for (uint32_t f = 0; f < WARMUP_FRAMES; ++f) {
            const face_render_key_t key = animated_key(f);
            fta_render_frame(profile, &key, f * 533U, frame,
                             FTA_PIXEL_COUNT);
        }
        const double start = now_seconds();
        for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
            const face_render_key_t key = animated_key(f);
            fta_render_frame(profile, &key, f * 533U, frame,
                             FTA_PIXEL_COUNT);
            checksum += frame[(f * 977U) % FTA_PIXEL_COUNT];
        }
        const double elapsed = now_seconds() - start;
        const double us_per_frame = elapsed * 1e6 / BENCH_FRAMES;
        total_us += us_per_frame;
        printf(
            "  %-12s %8.1f us/frame  %7.0f fps(host)\n",
            fta_profile_slug(profile), us_per_frame,
            1e6 / us_per_frame);

        /* solver-only cost */
        fta_rig_t rig;
        const double solve_start = now_seconds();
        for (uint32_t f = 0; f < BENCH_FRAMES; ++f) {
            const face_render_key_t key = animated_key(f);
            fta_solve(profile, &key, f * 533U, &rig);
            checksum += (uint32_t)rig.eye[0].pupil_x_q4;
        }
        const double solve_elapsed = now_seconds() - solve_start;
        printf(
        "  %-12s %8.2f us/solve\n",
            "", solve_elapsed * 1e6 / BENCH_FRAMES);
    }
    printf(
        "  matrix: %.1f us for all %zu profiles per frame "
        "(%0.0f matrix fps host)\n",
        total_us, fta_profile_count(), 1e6 / total_us);
    printf("  checksum %08x\n", checksum);
    printf(
        "  context bytes: %d, framebuffer bytes: %d\n",
        FTA_CONTEXT_BYTES, FTA_FRAME_BYTES);
    return checksum == 0U ? 1 : 0;
}
