#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "face_spectral.h"

enum {
    SAMPLE_RATE = 16000,
    WINDOW_SAMPLES = 160,
    SIMULATED_SECONDS = 10 * 60,
};

static double seconds_between(const struct timespec *start,
                              const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

int main(void)
{
    face_spectral_state_t state;
    face_driver_t driver;
    if (!face_driver_init(
            &driver, &FACE_ALGORITHM_SPECTRAL,
            &state, sizeof(state), SAMPLE_RATE, NULL, 0)) {
        return 1;
    }
    int16_t window[WINDOW_SAMPLES];
    for (size_t index = 0; index < WINDOW_SAMPLES; ++index) {
        const int32_t pseudo_voice =
            (int32_t)((index * 7919U + 1237U) & 0xffffU) - 32768;
        window[index] = (int16_t)(pseudo_voice / 3);
    }
    const uint64_t windows =
        (uint64_t)SIMULATED_SECONDS * SAMPLE_RATE / WINDOW_SAMPLES;

    struct timespec started;
    struct timespec finished;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (uint64_t index = 0; index < windows; ++index) {
        face_driver_push_pcm(&driver, window, WINDOW_SAMPLES);
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &finished);
    face_pose_t pose;
    face_driver_snapshot(&driver, &pose);

    const double elapsed = seconds_between(&started, &finished);
    printf(
        "{\"algorithm\":\"spectral\",\"cpu_percent_at_realtime\":%.6f,"
        "\"elapsed_seconds\":%.6f,\"model_bytes\":0,"
        "\"realtime_factor\":%.1f,\"simulated_audio_seconds\":%d,"
        "\"state_bytes\":%zu,\"terminal_samples\":%" PRIu32 "}\n",
        elapsed / SIMULATED_SECONDS * 100.0,
        elapsed,
        SIMULATED_SECONDS / elapsed,
        SIMULATED_SECONDS,
        FACE_ALGORITHM_SPECTRAL.state_size,
        pose.playout_samples);
    return pose.playout_samples ==
                   (uint32_t)(SIMULATED_SECONDS * SAMPLE_RATE)
               ? 0
               : 1;
}
