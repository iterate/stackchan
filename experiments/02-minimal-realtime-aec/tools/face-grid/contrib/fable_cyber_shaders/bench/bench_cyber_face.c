#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cyber_face.h"

/*
 * Host benchmark. Renders a realistic animated sequence per profile
 * (clock advancing 533 samples per frame, i.e. 30 fps, keyframes swept
 * through speech-like values) and reports the average frame cost.
 *
 * The absolute numbers are host numbers; the README derives the
 * ESP32-S3 plausibility argument from them together with the ops/pixel
 * estimates and the S3's published CoreMark ratio. Run on the device
 * for authoritative timing.
 */

enum { WARMUP_FRAMES = 30, BENCH_FRAMES = 600 };

static cyber_face_ctx_t ctx;
static uint16_t frame[CYBER_FACE_PIXEL_COUNT];

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void animate_keyframe(cyber_keyframe_t *kf, int i)
{
    memset(kf, 0, sizeof *kf);
    kf->eye_left_open = 255;
    kf->eye_right_open = 255;
    /* Triangle-wave "speech" so mouth geometry keeps changing. */
    int syllable = (i * 23) % 512;
    if (syllable > 255) {
        syllable = 511 - syllable;
    }
    kf->mouth_open = (uint8_t)syllable;
    kf->mouth_width = (uint8_t)(128 + ((i * 11) % 96));
    kf->mouth_round = (uint8_t)((i * 7) % 200);
    kf->look_x = (int8_t)(((i * 5) % 160) - 80);
    kf->look_y = (int8_t)(((i * 3) % 80) - 40);
    kf->flags = (i % 3) != 0 ? CYBER_KEYFRAME_FLAG_SPEAKING : 0;
}

int main(void)
{
    cyber_face_init(&ctx);
    uint64_t checksum = 0;

    printf("profile               avg us/frame   fps(host)  "
           "ns/out-px\n");
    printf("-------------------------------------------------------"
           "--\n");

    double total_us = 0.0;
    for (int p = 0; p < (int)cyber_face_profile_count(); ++p) {
        cyber_keyframe_t kf;
        for (int i = 0; i < WARMUP_FRAMES; ++i) {
            animate_keyframe(&kf, i);
            cyber_face_render(&ctx, (cyber_profile_t)p, &kf,
                              (uint32_t)i * 533u, frame,
                              CYBER_FACE_PIXEL_COUNT);
        }
        uint64_t start = now_ns();
        for (int i = 0; i < BENCH_FRAMES; ++i) {
            animate_keyframe(&kf, i);
            cyber_face_render(&ctx, (cyber_profile_t)p, &kf,
                              (uint32_t)i * 533u, frame,
                              CYBER_FACE_PIXEL_COUNT);
            checksum += frame[(i * 977) % CYBER_FACE_PIXEL_COUNT];
        }
        uint64_t elapsed = now_ns() - start;
        double us_per_frame =
            (double)elapsed / 1000.0 / (double)BENCH_FRAMES;
        double fps = 1000000.0 / us_per_frame;
        double ns_per_px = (double)elapsed / (double)BENCH_FRAMES /
                           (double)CYBER_FACE_PIXEL_COUNT;
        total_us += us_per_frame;
        printf("%-22s %10.1f %10.0f %10.2f\n",
               cyber_face_profile_slug((cyber_profile_t)p),
               us_per_frame, fps, ns_per_px);
    }
    printf("-------------------------------------------------------"
           "--\n");
    printf("mean over %d profiles: %.1f us/frame\n",
           (int)cyber_face_profile_count(),
           total_us / (double)cyber_face_profile_count());
    printf("ctx size: %zu bytes, frame: %d bytes (checksum %llu)\n",
           sizeof(cyber_face_ctx_t), CYBER_FACE_FRAME_BYTES,
           (unsigned long long)checksum);
    return 0;
}
