#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_stage.h"
#include "fea.h"

/*
 * -O3 host timing. The pack convention derates host-to-ESP32-S3 by a
 * deliberately pessimistic 100x; verify with esp_timer before shipping
 * device fps claims.
 */

enum {
    SAMPLE_RATE = 16000,
    FRAMES = 600,
};

static uint16_t frame[FEA_PIXEL_COUNT];

static uint8_t triangle_u8(uint32_t f, uint32_t period)
{
    const uint32_t phase = f % period;
    const uint32_t half = period / 2U;
    if (phase < half) {
        return (uint8_t)(phase * 255U / half);
    }
    return (uint8_t)((period - phase) * 255U / half);
}

static face_render_key_t bench_key(uint32_t f)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = triangle_u8(f + 7U, 42U);
    key.controls.mouth_width = 168U;
    key.controls.mouth_round = triangle_u8(f + 3U, 61U);
    key.controls.mouth_teeth = 96U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = 3U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = (uint8_t)(f % 15U);
    key.viseme_weight = 220U;
    key.audio_level = triangle_u8(f, 37U);
    key.speech_phase = 2U;
    key.stage_expression = (uint8_t)(f / 55U % 11U);
    key.expression_weight = 255U;
    key.cheek = 90U;
    key.affect_arousal = 128U;
    key.attention = 220U;
    key.schema_version = 2U;
    return key;
}

int main(void)
{
    volatile uint32_t sink = 0U;
    for (unsigned profile = 0; profile < FEA_PROFILE_COUNT; ++profile) {
        /* warm up */
        for (uint32_t f = 0; f < 30U; ++f) {
            const face_render_key_t key = bench_key(f);
            (void)fea_render_frame(
                (fea_profile_t)profile, &key,
                f * SAMPLE_RATE / 30U, frame, FEA_PIXEL_COUNT);
        }
        struct timespec begin;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        for (uint32_t f = 0; f < FRAMES; ++f) {
            const face_render_key_t key = bench_key(f);
            (void)fea_render_frame(
                (fea_profile_t)profile, &key,
                f * SAMPLE_RATE / 30U, frame, FEA_PIXEL_COUNT);
            sink += frame[f % FEA_PIXEL_COUNT];
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        const double elapsed_us =
            ((double)end.tv_sec - (double)begin.tv_sec) * 1e6 +
            ((double)end.tv_nsec - (double)begin.tv_nsec) / 1e3;
        const double per_frame = elapsed_us / FRAMES;
        printf(
            "%-20s %8.1f us/frame  host %7.0f fps  "
            "100x-derated ESP32 %6.2f ms (budget 33.3)\n",
            fea_profile_slug((fea_profile_t)profile), per_frame,
            1e6 / per_frame, per_frame / 10.0);
    }
    (void)sink;
    return 0;
}
