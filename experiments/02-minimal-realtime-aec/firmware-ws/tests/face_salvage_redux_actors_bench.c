#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_pose.h"
#include "face_salvage_redux_actors.h"
#include "face_stage.h"

enum {
    SR_BENCH_FRAMES_PER_STYLE = 600,
};

static uint64_t sr_bench_now_ns(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) {
        return 0U;
    }
    return (uint64_t)value.tv_sec * 1000000000ULL +
        (uint64_t)value.tv_nsec;
}

static face_render_key_t sr_bench_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 128U;
    key.controls.mouth_width = 178U;
    key.controls.mouth_round = 60U;
    key.controls.mouth_teeth = 110U;
    key.controls.eye_left_open = 235U;
    key.controls.eye_right_open = 238U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = FACE_PHONEME_NONE;
    key.viseme_weight = 225U;
    key.audio_level = 140U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 60U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.tongue = 90U;
    key.cheek = 36U;
    key.affect_arousal = 140U;
    key.expression_weight = 255U;
    key.attention = 214U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t pixels[FACE_SALVAGE_REDUX_PIXEL_COUNT];
    face_render_key_t key = sr_bench_key();
    uint32_t checksum = 2166136261U;
    const uint64_t start = sr_bench_now_ns();
    for (size_t style = 0U;
         style < FACE_SALVAGE_REDUX_COUNT;
         ++style) {
        for (uint32_t frame = 0U;
             frame < SR_BENCH_FRAMES_PER_STYLE;
             ++frame) {
            key.viseme = (uint8_t)(frame % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((frame + 1U) % FACE_VISEME_COUNT);
            key.viseme_blend = (uint8_t)(frame * 31U);
            key.audio_level = (uint8_t)(64U + frame % 176U);
            key.stage_expression =
                (uint8_t)(frame % FACE_EXPRESSION_COUNT);
            key.controls.look_x =
                (int8_t)((int)(frame % 41U) - 20);
            key.controls.look_y =
                (int8_t)((int)(frame % 29U) - 14);
            key.body_lean_x =
                (int8_t)((int)(frame % 37U) - 18);
            if (!face_salvage_redux_render(
                    (face_salvage_redux_style_t)style,
                    &key,
                    16000U + frame * 533U,
                    pixels,
                    FACE_SALVAGE_REDUX_PIXEL_COUNT)) {
                return 1;
            }
            checksum ^= pixels[
                (frame * 7919U) % FACE_SALVAGE_REDUX_PIXEL_COUNT];
            checksum *= 16777619U;
        }
    }
    const uint64_t elapsed = sr_bench_now_ns() - start;
    const uint64_t rendered =
        (uint64_t)FACE_SALVAGE_REDUX_COUNT *
        SR_BENCH_FRAMES_PER_STYLE;
    const uint64_t ns_per_frame =
        rendered == 0U ? 0U : elapsed / rendered;
    const uint64_t frames_per_second =
        ns_per_frame == 0U ? 0U : 1000000000ULL / ns_per_frame;
    printf(
        "actors=%d frames=%llu elapsed_ns=%llu ns_per_frame=%llu "
        "frames_per_second=%llu checksum=%08x\n",
        FACE_SALVAGE_REDUX_COUNT,
        (unsigned long long)rendered,
        (unsigned long long)elapsed,
        (unsigned long long)ns_per_frame,
        (unsigned long long)frames_per_second,
        checksum);
    return 0;
}
