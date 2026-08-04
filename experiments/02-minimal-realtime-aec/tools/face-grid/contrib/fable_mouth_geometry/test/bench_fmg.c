#include <stdio.h>
#include <time.h>

#include "../src/fmg.h"

/*
 * Host benchmark: renders each profile for a few seconds of animated
 * keyframes and reports us/frame. Host numbers are only a proxy for the
 * ESP32-S3 — see README.md for the derating discussion — but the
 * *relative* cost of the profiles carries over well because everything
 * is plain integer arithmetic.
 */

enum { FRAMES = 600 };

int main(void)
{
    static uint16_t frame[FMG_PIXEL_COUNT];
    printf("%-22s %10s %10s\n", "profile", "us/frame", "fps(host)");
    for (int p = 0; p < (int)fmg_profile_count(); p++) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (uint32_t f = 0; f < FRAMES; f++) {
            fmg_keyframe_t kf = {
                (uint8_t)((f * 7) & 0xFF),  (uint8_t)(110 + (f % 80)),
                (uint8_t)((f * 3) & 0xFF),  (uint8_t)(f % 5 == 0 ? 200 : 0),
                (uint8_t)((f * 5) & 0xFF),  255,
                255,                        (int8_t)((f % 60) - 30),
                (int8_t)((f % 40) - 20),    0,
                3,                          1,
            };
            fmg_render_frame((fmg_profile_t)p, &kf, f * 533U, frame,
                             FMG_PIXEL_COUNT);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 +
                     (double)(t1.tv_nsec - t0.tv_nsec) / 1e3) /
                    FRAMES;
        printf("%-22s %10.1f %10.0f\n", fmg_profile_slug((fmg_profile_t)p),
               us, 1e6 / us);
    }
    return 0;
}
