#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_eye_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    ESR_BENCH_FRAMES_PER_PROFILE = 720,
};

static face_render_key_t esr_benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 235U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 205U;
    key.audio_level = 116U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.cheek = 18U;
    key.affect_arousal = 126U;
    key.expression_weight = 255U;
    key.attention = 196U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t frame[FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
    volatile uint32_t checksum = 0U;
    double slowest_fps = 1.0e30;
    double total_seconds = 0.0;
    size_t total_frames = 0U;

    for (size_t raw = 0U;
         raw < FACE_EYE_STUDY_REDUX_PROFILE_COUNT;
         ++raw) {
        const face_eye_study_redux_profile_t profile =
            (face_eye_study_redux_profile_t)(
                FACE_EYE_STUDY_REDUX_FIRST_LEGACY_ID + raw);
        face_render_key_t key = esr_benchmark_key();
        const clock_t start = clock();
        for (size_t rendered = 0U;
             rendered < ESR_BENCH_FRAMES_PER_PROFILE;
             ++rendered) {
            const int32_t triangle =
                (int32_t)(rendered % 128U) -
                (int32_t)((rendered / 128U) % 2U) * 127;
            key.controls.look_x = (int8_t)(triangle - 32);
            key.controls.look_y = (int8_t)(32 - triangle / 2);
            key.controls.eye_left_open =
                (uint8_t)(178U + rendered % 77U);
            key.controls.eye_right_open =
                (uint8_t)(184U + (rendered * 3U) % 71U);
            key.audio_level =
                (uint8_t)(24U + (rendered * 17U) % 232U);
            key.expression_weight =
                (uint8_t)(96U + rendered % 160U);
            key.stage_expression =
                (uint8_t)(rendered % FACE_EXPRESSION_COUNT);
            key.speech_phase =
                (uint8_t)(rendered % 4U);
            if (!face_eye_study_redux_render(
                    profile, &key, (uint32_t)(rendered * 533U),
                    frame, FACE_EYE_STUDY_REDUX_PIXEL_COUNT)) {
                fprintf(stderr, "benchmark render failed\n");
                return 1;
            }
            checksum ^= frame[
                (rendered * 7919U) %
                FACE_EYE_STUDY_REDUX_PIXEL_COUNT];
        }
        const double seconds =
            (double)(clock() - start) / CLOCKS_PER_SEC;
        const double fps =
            ESR_BENCH_FRAMES_PER_PROFILE /
            (seconds > 0.0 ? seconds : 1.0e-9);
        if (fps < slowest_fps) {
            slowest_fps = fps;
        }
        total_seconds += seconds;
        total_frames += ESR_BENCH_FRAMES_PER_PROFILE;
        printf(
            "legacy %u %-17s %8.1f fps (%7.3f ms/frame)\n",
            (unsigned)profile,
            face_eye_study_redux_profile_slug(profile),
            fps,
            1000.0 / fps);
    }
    const double aggregate_fps =
        total_frames / (total_seconds > 0.0 ? total_seconds : 1.0e-9);
    printf(
        "eye-study benchmark: aggregate %.1f fps, slowest %.1f fps, "
        "target 30 fps, checksum %u\n",
        aggregate_fps, slowest_fps, checksum);
    if (slowest_fps <= 30.0) {
        fprintf(stderr, "30 fps budget failed\n");
        return 1;
    }
    return 0;
}
