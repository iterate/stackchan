#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_viseme.h"

enum {
    SAMPLE_RATE = 16000,
    GOLDEN_SAMPLES = 512,
};

static const float GOLDEN_FEATURES[FACE_VISEME_FEATURE_COUNT] = {
    1.0f,
    0.9999417066574097f,
    -0.999945342540741f,
    -0.9999997615814209f,
    0.7099394202232361f,
    1.0f,
    1.0f,
    1.0f,
    -1.0f,
    -1.0f,
    -1.0f,
    -1.0f,
};

static void build_golden_pcm(int16_t samples[GOLDEN_SAMPLES])
{
    const double pi = 3.14159265358979323846;
    for (size_t index = 0; index < GOLDEN_SAMPLES; ++index) {
        const double carrier =
            9200.0 * sin(2.0 * pi * 173.0 * index / SAMPLE_RATE) +
            5100.0 * sin(2.0 * pi * 731.0 * index / SAMPLE_RATE);
        const double envelope =
            0.35 + 0.65 * (double)index / (GOLDEN_SAMPLES - 1);
        samples[index] = (int16_t)lround(carrier * envelope);
    }
}

static void feature_front_end_matches_upstream_headaudio(void)
{
    int16_t samples[GOLDEN_SAMPLES];
    float actual[FACE_VISEME_FEATURE_COUNT];
    build_golden_pcm(samples);

    assert(face_viseme_test_extract_features(
        samples, GOLDEN_SAMPLES, 150, actual));
    float maximum_error = 0.0f;
    for (size_t index = 0; index < FACE_VISEME_FEATURE_COUNT; ++index) {
        const float error = fabsf(actual[index] - GOLDEN_FEATURES[index]);
        if (error > maximum_error) {
            maximum_error = error;
        }
        if (error > 1e-4f) {
            fprintf(
                stderr,
                "feature %zu mismatch: actual %.9f expected %.9f "
                "(error %.9g)\n",
                index, actual[index], GOLDEN_FEATURES[index], error);
        }
    }
    assert(maximum_error <= 1e-4f);
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

static void snapshot_equal(const face_pose_t *left,
                           const face_pose_t *right)
{
    assert(memcmp(left, right, sizeof(*left)) == 0);
}

static void classifier_is_packet_boundary_invariant(const char *model_path)
{
    size_t model_size;
    uint8_t *model = load_model(model_path, &model_size);
    assert(face_viseme_model_valid(model, model_size));
    assert(face_viseme_model_record_count(model_size) == 39);

    face_viseme_config_t config = FACE_VISEME_DEFAULT_CONFIG;
    config.model_data = model;
    config.model_size = model_size;

    void *first_state = calloc(1, FACE_ALGORITHM_VISEME.state_size);
    void *second_state = calloc(1, FACE_ALGORITHM_VISEME.state_size);
    assert(first_state != NULL);
    assert(second_state != NULL);
    face_driver_t contiguous;
    face_driver_t fragmented;
    assert(face_driver_init(
        &contiguous, &FACE_ALGORITHM_VISEME,
        first_state, FACE_ALGORITHM_VISEME.state_size,
        SAMPLE_RATE, &config, sizeof(config)));
    assert(face_driver_init(
        &fragmented, &FACE_ALGORITHM_VISEME,
        second_state, FACE_ALGORITHM_VISEME.state_size,
        SAMPLE_RATE, &config, sizeof(config)));

    int16_t pcm[4097];
    for (size_t index = 0; index < sizeof(pcm) / sizeof(pcm[0]); ++index) {
        const int32_t value =
            (int32_t)((index * 7919U + 1237U) & 0xffffU) - 32768;
        pcm[index] = (int16_t)(value / 3);
    }
    face_driver_push_pcm(
        &contiguous, pcm, sizeof(pcm) / sizeof(pcm[0]));

    static const size_t chunks[] = {
        1, 37, 3, 159, 320, 11, 503, 2, 697,
    };
    size_t offset = 0;
    size_t chunk_index = 0;
    while (offset < sizeof(pcm) / sizeof(pcm[0])) {
        size_t count =
            chunks[chunk_index %
                   (sizeof(chunks) / sizeof(chunks[0]))];
        const size_t remaining =
            sizeof(pcm) / sizeof(pcm[0]) - offset;
        if (count > remaining) {
            count = remaining;
        }
        face_driver_push_pcm(&fragmented, pcm + offset, count);
        offset += count;
        chunk_index += 1;
    }

    face_pose_t contiguous_pose;
    face_pose_t fragmented_pose;
    face_driver_snapshot(&contiguous, &contiguous_pose);
    face_driver_snapshot(&fragmented, &fragmented_pose);
    snapshot_equal(&contiguous_pose, &fragmented_pose);
    assert(contiguous_pose.frame_index > 0);
    assert(contiguous_pose.playout_samples ==
           sizeof(pcm) / sizeof(pcm[0]));

    free(second_state);
    free(first_state);
    free(model);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    feature_front_end_matches_upstream_headaudio();
    classifier_is_packet_boundary_invariant(argv[1]);
    puts("face_viseme_test: PASS");
    return 0;
}
