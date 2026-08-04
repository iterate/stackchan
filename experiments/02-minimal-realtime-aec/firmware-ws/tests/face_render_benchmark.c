#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_render.h"

enum {
    MATRIX_FRAMES = 120,
    SAMPLE_RATE = 16000,
    FRAMES_PER_SECOND = 30,
};

static double seconds_between(
    const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

int main(void)
{
    uint16_t framebuffer[FACE_RENDER_PIXEL_COUNT];
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 190U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 82U;
    key.controls.mouth_teeth = 86U;
    key.controls.eye_left_open = 244U;
    key.controls.eye_right_open = 232U;
    key.controls.look_x = -9;
    key.controls.look_y = 6;
    key.controls.brow = 20;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 0U;
    key.viseme_weight = 230U;
    key.audio_level = 184U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 20;
    key.mouth_corner_right = 20;
    key.brow_inner = 10;
    key.affect_valence = 24;
    key.affect_arousal = 184U;
    key.head_yaw = -6;
    key.head_pitch = 4;
    key.expression_weight = 220U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;

    struct timespec started;
    struct timespec finished;
    uint32_t checksum = 0U;
    double worst_profile_seconds = 0.0;
    size_t worst_profile = 0U;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    for (size_t profile = 0;
         profile < face_render_profile_count(); ++profile) {
        struct timespec profile_started;
        struct timespec profile_finished;
        (void)clock_gettime(CLOCK_MONOTONIC, &profile_started);
        for (uint32_t frame = 0U; frame < MATRIX_FRAMES; ++frame) {
            key.controls.mouth_open =
                (uint8_t)(70U + (frame * 17U + profile * 11U) % 180U);
            key.viseme =
                (uint8_t)((frame / 4U + profile) % FACE_VISEME_COUNT);
            if (!face_render_frame(
                    (face_render_profile_t)profile, &key,
                    frame * SAMPLE_RATE / FRAMES_PER_SECOND,
                    framebuffer, FACE_RENDER_PIXEL_COUNT)) {
                return 1;
            }
            checksum ^= framebuffer[
                (frame * 977U + (uint32_t)profile * 89U) %
                FACE_RENDER_PIXEL_COUNT];
        }
        (void)clock_gettime(CLOCK_MONOTONIC, &profile_finished);
        const double profile_seconds =
            seconds_between(&profile_started, &profile_finished);
        if (profile_seconds > worst_profile_seconds) {
            worst_profile_seconds = profile_seconds;
            worst_profile = profile;
        }
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &finished);

    const double elapsed = seconds_between(&started, &finished);
    const double matrix_frame_seconds = elapsed / MATRIX_FRAMES;
    const double matrix_fps = 1.0 / matrix_frame_seconds;
    const double worst_profile_us =
        worst_profile_seconds * 1000000.0 / MATRIX_FRAMES;
    printf(
        "{\"checksum\":%" PRIu32 ",\"framebuffer_bytes\":%d,"
        "\"matrix_fps\":%.2f,\"matrix_frame_ms\":%.3f,"
        "\"profiles\":%zu,\"renderer_ir_bytes\":%zu,"
        "\"worst_profile\":\"%s\",\"worst_profile_us\":%.2f}\n",
        checksum,
        FACE_RENDER_FRAME_BYTES,
        matrix_fps,
        matrix_frame_seconds * 1000.0,
        face_render_profile_count(),
        sizeof(face_render_key_t),
        face_render_profile_slug((face_render_profile_t)worst_profile),
        worst_profile_us);
    return checksum == UINT32_MAX ? 1 : 0;
}
