#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_spectral.h"

enum {
    SAMPLE_RATE = 16000,
    WINDOW_SAMPLES = 320,
    TEST_WINDOWS = 6,
    TEST_SAMPLES = WINDOW_SAMPLES * TEST_WINDOWS,
};

_Static_assert(
    sizeof(face_spectral_state_t) <= 128,
    "streaming spectral analysis must stay tiny");

static void generate_formants(
    int16_t samples[TEST_SAMPLES], double first_hz, double second_hz)
{
    const double pi = 3.14159265358979323846;
    for (size_t index = 0; index < TEST_SAMPLES; ++index) {
        const double first =
            6500.0 * sin(2.0 * pi * first_hz * index / SAMPLE_RATE);
        const double second =
            6500.0 * sin(2.0 * pi * second_hz * index / SAMPLE_RATE);
        samples[index] = (int16_t)lround(first + second);
    }
}

static face_pose_t analyse(double first_hz, double second_hz)
{
    int16_t samples[TEST_SAMPLES];
    generate_formants(samples, first_hz, second_hz);
    face_spectral_state_t state;
    face_driver_t driver;
    assert(face_driver_init(
        &driver, &FACE_ALGORITHM_SPECTRAL,
        &state, sizeof(state), SAMPLE_RATE, NULL, 0));
    face_driver_push_pcm(&driver, samples, TEST_SAMPLES);
    face_pose_t pose;
    memset(&pose, 0, sizeof(pose));
    face_driver_snapshot(&driver, &pose);
    return pose;
}

static void silence_is_closed(void)
{
    const int16_t silence[WINDOW_SAMPLES * 2] = {0};
    face_spectral_state_t state;
    face_driver_t driver;
    assert(face_driver_init(
        &driver, &FACE_ALGORITHM_SPECTRAL,
        &state, sizeof(state), SAMPLE_RATE, NULL, 0));
    face_driver_push_pcm(
        &driver, silence, sizeof(silence) / sizeof(silence[0]));
    face_pose_t pose;
    face_driver_snapshot(&driver, &pose);
    assert(pose.viseme == FACE_VISEME_SIL);
    assert(pose.mouth_open == 0);
    assert(!pose.speaking);
}

static void formant_regions_produce_five_vowels(void)
{
    const face_pose_t aa = analyse(800, 1100);
    const face_pose_t e = analyse(550, 1600);
    const face_pose_t i = analyse(350, 2400);
    const face_pose_t o = analyse(550, 1100);
    const face_pose_t u = analyse(350, 1100);

    assert(aa.viseme == FACE_VISEME_AA);
    assert(e.viseme == FACE_VISEME_E);
    assert(i.viseme == FACE_VISEME_I);
    assert(o.viseme == FACE_VISEME_O);
    assert(u.viseme == FACE_VISEME_U);
    assert(aa.mouth_open > i.mouth_open);
    assert(i.mouth_width > u.mouth_width);
    assert(u.mouth_round > i.mouth_round);
    assert(aa.confidence > 100);
}

static void high_band_produces_a_fricative(void)
{
    const face_pose_t sibilant = analyse(3200, 3200);
    assert(sibilant.viseme == FACE_VISEME_SS);
    assert(sibilant.mouth_teeth > 100);
}

static void packet_boundaries_cannot_change_the_result(void)
{
    int16_t samples[TEST_SAMPLES + 17];
    for (size_t index = 0;
         index < sizeof(samples) / sizeof(samples[0]); ++index) {
        const int32_t value =
            (int32_t)((index * 7919U + 1237U) & 0xffffU) - 32768;
        samples[index] = (int16_t)(value / 3);
    }
    face_spectral_state_t contiguous_state;
    face_spectral_state_t fragmented_state;
    face_driver_t contiguous;
    face_driver_t fragmented;
    assert(face_driver_init(
        &contiguous, &FACE_ALGORITHM_SPECTRAL,
        &contiguous_state, sizeof(contiguous_state),
        SAMPLE_RATE, NULL, 0));
    assert(face_driver_init(
        &fragmented, &FACE_ALGORITHM_SPECTRAL,
        &fragmented_state, sizeof(fragmented_state),
        SAMPLE_RATE, NULL, 0));
    face_driver_push_pcm(
        &contiguous, samples,
        sizeof(samples) / sizeof(samples[0]));

    static const size_t chunks[] = {
        1, 37, 3, 159, 320, 11, 503, 2, 697,
    };
    size_t offset = 0;
    size_t chunk = 0;
    while (offset < sizeof(samples) / sizeof(samples[0])) {
        size_t count =
            chunks[chunk % (sizeof(chunks) / sizeof(chunks[0]))];
        const size_t remaining =
            sizeof(samples) / sizeof(samples[0]) - offset;
        if (count > remaining) {
            count = remaining;
        }
        face_driver_push_pcm(
            &fragmented, samples + offset, count);
        offset += count;
        chunk += 1;
    }

    face_pose_t first;
    face_pose_t second;
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    face_driver_snapshot(&contiguous, &first);
    face_driver_snapshot(&fragmented, &second);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(first.playout_samples ==
           sizeof(samples) / sizeof(samples[0]));
}

static void rejects_non_sixteen_kilohertz_input(void)
{
    face_spectral_state_t state;
    face_driver_t driver;
    assert(!face_driver_init(
        &driver, &FACE_ALGORITHM_SPECTRAL,
        &state, sizeof(state), 24000, NULL, 0));
}

int main(void)
{
    silence_is_closed();
    formant_regions_produce_five_vowels();
    high_band_produces_a_fricative();
    packet_boundaries_cannot_change_the_result();
    rejects_non_sixteen_kilohertz_input();
    puts("face_spectral_test: PASS");
    return 0;
}

