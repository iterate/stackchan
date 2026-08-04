#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_mouth_actors.h"

enum {
    BENCHMARK_FRAMES = 600,
    SAMPLE_RATE = 16000,
    FRAMES_PER_SECOND = 30,
};

static double seconds_between(
    const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static face_render_key_t benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 170U;
    key.controls.mouth_width = 160U;
    key.controls.mouth_round = 54U;
    key.controls.mouth_teeth = 138U;
    key.controls.eye_left_open = 244U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.viseme_weight = 230U;
    key.audio_level = 170U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 14;
    key.mouth_corner_right = 18;
    key.tongue = 96U;
    key.cheek = 54U;
    key.affect_valence = 22;
    key.affect_arousal = 174U;
    key.expression_weight = 255U;
    key.attention = 230U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = 9U;
    return key;
}

int main(void)
{
    uint16_t framebuffer[FACE_MOUTH_ACTORS_PIXEL_COUNT];
    face_render_key_t key = benchmark_key();
    uint32_t checksum = 2166136261U;
    double worst_us = 0.0;
    size_t worst_profile = 0U;
    struct timespec total_start;
    struct timespec total_end;
    (void)clock_gettime(CLOCK_MONOTONIC, &total_start);
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        struct timespec started;
        struct timespec finished;
        (void)clock_gettime(CLOCK_MONOTONIC, &started);
        for (uint32_t frame = 0U;
             frame < BENCHMARK_FRAMES;
             ++frame) {
            key.controls.mouth_open =
                (uint8_t)(48U + (frame * 17U + profile * 13U) % 208U);
            key.controls.mouth_round =
                (uint8_t)((frame * 31U + profile * 19U) & 255U);
            key.viseme =
                (uint8_t)((frame / 3U + profile) % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((key.viseme + 1U) % FACE_VISEME_COUNT);
            key.viseme_blend = (uint8_t)((frame * 23U) & 255U);
            key.audio_level =
                (uint8_t)((frame * 29U + profile * 41U) & 255U);
            key.stage_expression =
                (uint8_t)((frame / 19U + profile) % 11U);
            if (!face_mouth_actors_render(
                    (face_mouth_actor_profile_t)profile,
                    &key,
                    frame * SAMPLE_RATE / FRAMES_PER_SECOND,
                    framebuffer,
                    FACE_MOUTH_ACTORS_PIXEL_COUNT)) {
                return 1;
            }
            const uint16_t sample = framebuffer[
                (frame * 977U + (uint32_t)profile * 137U) %
                FACE_MOUTH_ACTORS_PIXEL_COUNT];
            checksum ^= sample;
            checksum *= 16777619U;
        }
        (void)clock_gettime(CLOCK_MONOTONIC, &finished);
        const double profile_us =
            seconds_between(&started, &finished) * 1000000.0 /
            BENCHMARK_FRAMES;
        if (profile_us > worst_us) {
            worst_us = profile_us;
            worst_profile = profile;
        }
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &total_end);
    const double total_seconds =
        seconds_between(&total_start, &total_end);
    const double average_us =
        total_seconds * 1000000.0 /
        (BENCHMARK_FRAMES * FACE_MOUTH_ACTOR_COUNT);
    printf(
        "{\"average_us\":%.2f,\"checksum\":%" PRIu32
        ",\"framebuffer_bytes\":%d,\"frames\":%d,"
        "\"profiles\":%d,\"renderer_ir_bytes\":%zu,"
        "\"worst_profile\":\"%s\",\"worst_us\":%.2f}\n",
        average_us,
        checksum,
        FACE_MOUTH_ACTORS_FRAME_BYTES,
        BENCHMARK_FRAMES,
        FACE_MOUTH_ACTOR_COUNT,
        sizeof(face_render_key_t),
        face_mouth_actors_profile_slug(
            (face_mouth_actor_profile_t)worst_profile),
        worst_us);
    return checksum == UINT32_MAX ? 1 : 0;
}
