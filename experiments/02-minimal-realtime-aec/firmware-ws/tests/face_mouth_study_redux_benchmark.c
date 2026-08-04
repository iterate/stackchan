#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_mouth_study_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    MSR_BENCH_FRAMES_PER_PROFILE = 720,
};

static face_render_key_t msr_benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 88U;
    key.controls.mouth_width = 136U;
    key.controls.mouth_round = 22U;
    key.controls.mouth_teeth = 126U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 235U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 232U;
    key.audio_level = 110U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.cheek = 24U;
    key.affect_arousal = 136U;
    key.expression_weight = 255U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t frame[FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
    volatile uint32_t checksum = 0U;
    double slowest_fps = 1.0e30;
    double total_seconds = 0.0;
    size_t total_frames = 0U;

    for (size_t raw = 0U;
         raw < FACE_MOUTH_STUDY_REDUX_PROFILE_COUNT;
         ++raw) {
        const face_mouth_study_redux_profile_t profile =
            (face_mouth_study_redux_profile_t)(
                FACE_MOUTH_STUDY_REDUX_FIRST_LEGACY_ID + raw);
        face_render_key_t key = msr_benchmark_key();
        const clock_t start = clock();
        for (size_t rendered = 0U;
             rendered < MSR_BENCH_FRAMES_PER_PROFILE;
             ++rendered) {
            key.controls.mouth_open =
                (uint8_t)(30U + (rendered * 17U) % 226U);
            key.controls.mouth_width =
                (uint8_t)(74U + (rendered * 11U) % 182U);
            key.controls.mouth_round =
                (uint8_t)((rendered * 29U) % 256U);
            key.controls.mouth_press =
                (uint8_t)((rendered * 7U) % 256U);
            key.controls.mouth_teeth =
                (uint8_t)((rendered * 19U) % 256U);
            key.audio_level =
                (uint8_t)(18U + (rendered * 23U) % 238U);
            key.viseme = (uint8_t)(rendered % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((rendered + 3U) % FACE_VISEME_COUNT);
            key.viseme_blend =
                (uint8_t)((rendered * 31U) % 256U);
            key.stage_expression =
                (uint8_t)(rendered % FACE_EXPRESSION_COUNT);
            key.speech_phase = (uint8_t)(rendered % 4U);
            key.controls.look_x =
                (int8_t)((int32_t)(rendered % 127U) - 63);
            key.controls.look_y =
                (int8_t)(31 - (int32_t)(rendered % 63U));
            if (!face_mouth_study_redux_render(
                    profile, &key, (uint32_t)(rendered * 533U),
                    frame, FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT)) {
                fprintf(stderr, "benchmark render failed\n");
                return 1;
            }
            checksum ^= frame[
                (rendered * 7919U) %
                FACE_MOUTH_STUDY_REDUX_PIXEL_COUNT];
        }
        const double seconds =
            (double)(clock() - start) / CLOCKS_PER_SEC;
        const double fps =
            MSR_BENCH_FRAMES_PER_PROFILE /
            (seconds > 0.0 ? seconds : 1.0e-9);
        if (fps < slowest_fps) {
            slowest_fps = fps;
        }
        total_seconds += seconds;
        total_frames += MSR_BENCH_FRAMES_PER_PROFILE;
        printf(
            "legacy %u %-24s %8.1f fps (%7.3f ms/frame)\n",
            (unsigned)profile,
            face_mouth_study_redux_profile_slug(profile),
            fps,
            1000.0 / fps);
    }
    const double aggregate_fps =
        total_frames /
        (total_seconds > 0.0 ? total_seconds : 1.0e-9);
    printf(
        "mouth-study benchmark: aggregate %.1f fps, "
        "slowest %.1f fps, target 30 fps, checksum %u\n",
        aggregate_fps,
        slowest_fps,
        checksum);
    if (slowest_fps <= 30.0) {
        fprintf(stderr, "30 fps budget failed\n");
        return 1;
    }
    return 0;
}
