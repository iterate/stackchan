#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "face_viseme.h"

enum {
    SAMPLE_RATE = 16000,
    WINDOW_SAMPLES = SAMPLE_RATE / 100,
    SIMULATED_SECONDS = 10 * 60,
};

static double seconds_between(const struct timespec *start,
                              const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static uint8_t *load_model(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes != NULL);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    size_t model_size;
    uint8_t *model = load_model(argv[1], &model_size);
    face_viseme_config_t config = FACE_VISEME_DEFAULT_CONFIG;
    config.model_data = model;
    config.model_size = model_size;

    void *state = calloc(1, FACE_ALGORITHM_VISEME.state_size);
    assert(state != NULL);
    face_driver_t driver;
    assert(face_driver_init(
        &driver, &FACE_ALGORITHM_VISEME,
        state, FACE_ALGORITHM_VISEME.state_size,
        SAMPLE_RATE, &config, sizeof(config)));

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
    const double realtime_factor = SIMULATED_SECONDS / elapsed;
    printf(
        "{\"algorithm\":\"viseme\",\"cpu_percent_at_realtime\":%.6f,"
        "\"elapsed_seconds\":%.6f,\"model_bytes\":%zu,"
        "\"realtime_factor\":%.1f,\"simulated_audio_seconds\":%d,"
        "\"state_bytes\":%zu,\"terminal_samples\":%" PRIu32 "}\n",
        elapsed / SIMULATED_SECONDS * 100.0,
        elapsed,
        model_size,
        realtime_factor,
        SIMULATED_SECONDS,
        FACE_ALGORITHM_VISEME.state_size,
        pose.playout_samples);

    free(state);
    free(model);
    return pose.playout_samples ==
                   (uint32_t)(SIMULATED_SECONDS * SAMPLE_RATE)
               ? 0
               : 1;
}
