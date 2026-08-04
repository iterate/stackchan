#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "face_animator.h"

enum {
    SAMPLE_RATE = 16000,
    WINDOW_SAMPLES = SAMPLE_RATE / 100,
    SIMULATED_SECONDS = 4 * 60 * 60,
};

static double seconds_between(const struct timespec *start,
                              const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

int main(void)
{
    face_animator_t animator;
    face_animator_state_t state;
    int16_t window[WINDOW_SAMPLES];
    const uint64_t windows =
        (uint64_t)SIMULATED_SECONDS * SAMPLE_RATE / WINDOW_SAMPLES;
    const uint64_t samples = windows * WINDOW_SAMPLES;

    for (size_t index = 0; index < WINDOW_SAMPLES; ++index) {
        const int32_t pseudo_voice =
            (int32_t)((index * 7919U + 1237U) & 0xffffU) - 32768;
        window[index] = (int16_t)(pseudo_voice / 3);
    }

    face_animator_init(&animator, SAMPLE_RATE);
    struct timespec started;
    struct timespec finished;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (uint64_t index = 0; index < windows; ++index) {
        face_animator_push_pcm(
            &animator, window, sizeof(window) / sizeof(window[0]));
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &finished);
    face_animator_snapshot(&animator, &state);

    const double elapsed = seconds_between(&started, &finished);
    const double realtime_factor = SIMULATED_SECONDS / elapsed;
    const double cpu_percent = elapsed / SIMULATED_SECONDS * 100.0;
    const double nanoseconds_per_sample =
        elapsed * 1000000000.0 / (double)samples;

    printf(
        "{\"animator_bytes\":%zu,\"cpu_percent_at_realtime\":%.9f,"
        "\"elapsed_seconds\":%.6f,\"nanoseconds_per_sample\":%.3f,"
        "\"realtime_factor\":%.1f,\"simulated_audio_seconds\":%d,"
        "\"state_bytes\":%zu,\"terminal_frame\":%" PRIu32 "}\n",
        sizeof(animator),
        cpu_percent,
        elapsed,
        nanoseconds_per_sample,
        realtime_factor,
        SIMULATED_SECONDS,
        sizeof(state),
        state.frame_index);
    return state.frame_index == windows ? 0 : 1;
}
