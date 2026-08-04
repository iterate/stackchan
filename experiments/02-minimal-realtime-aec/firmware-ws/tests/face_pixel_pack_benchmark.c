#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_pixel_pack.h"
#include "face_stage.h"

enum {
    BENCHMARK_FRAMES = 1200,
    MAX_HOST_NS_PER_FRAME = 2000000,
};

static uint16_t frame[FACE_PIXEL_PACK_PIXEL_COUNT];

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec timestamp;
    assert(clock_gettime(CLOCK_MONOTONIC, &timestamp) == 0);
    return (uint64_t)timestamp.tv_sec * 1000000000ULL +
           (uint64_t)timestamp.tv_nsec;
}

static face_render_key_t benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 120U;
    key.controls.mouth_width = 156U;
    key.controls.mouth_round = 48U;
    key.controls.mouth_teeth = 82U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 238U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme = FACE_VISEME_AA;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_weight = 224U;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.stage_expression = FACE_EXPRESSION_WARM;
    key.expression_weight = 255U;
    key.affect_arousal = 160U;
    key.attention = 240U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

int main(void)
{
    uint64_t total_nanoseconds = 0U;
    uint64_t slowest_nanoseconds = 0U;
    for (size_t profile_index = 0U;
         profile_index < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile_index) {
        face_render_key_t key = benchmark_key();
        const face_pixel_pack_profile_t profile =
            (face_pixel_pack_profile_t)profile_index;
        const uint64_t started = monotonic_nanoseconds();
        for (uint32_t index = 0U; index < BENCHMARK_FRAMES; ++index) {
            key.controls.mouth_open =
                (uint8_t)(36U + (index * 37U) % 196U);
            key.controls.mouth_round =
                (uint8_t)((index * 17U) & 0xffU);
            key.viseme =
                (uint8_t)(index % FACE_VISEME_COUNT);
            key.stage_expression =
                (uint8_t)(index % FACE_EXPRESSION_COUNT);
            assert(face_pixel_pack_render(
                profile, &key, index * 533U,
                frame, FACE_PIXEL_PACK_PIXEL_COUNT));
        }
        const uint64_t elapsed =
            monotonic_nanoseconds() - started;
        const uint64_t nanoseconds_per_frame =
            elapsed / BENCHMARK_FRAMES;
        total_nanoseconds += elapsed;
        if (nanoseconds_per_frame > slowest_nanoseconds) {
            slowest_nanoseconds = nanoseconds_per_frame;
        }
        printf(
            "%s: %" PRIu64 " ns/frame, %" PRIu64 " fps\n",
            face_pixel_pack_profile_slug(profile),
            nanoseconds_per_frame,
            nanoseconds_per_frame == 0U
                ? 0U
                : 1000000000ULL / nanoseconds_per_frame);
        assert(nanoseconds_per_frame <= MAX_HOST_NS_PER_FRAME);
    }
    printf(
        "face_pixel_pack_benchmark: PASS "
        "(mean=%" PRIu64 " ns/frame slowest=%" PRIu64
        " ns/frame context=%d frame=%d bytes)\n",
        total_nanoseconds /
            (BENCHMARK_FRAMES * FACE_PIXEL_PACK_PROFILE_COUNT),
        slowest_nanoseconds,
        FACE_PIXEL_PACK_CONTEXT_BYTES,
        FACE_PIXEL_PACK_FRAME_BYTES);
    return 0;
}
