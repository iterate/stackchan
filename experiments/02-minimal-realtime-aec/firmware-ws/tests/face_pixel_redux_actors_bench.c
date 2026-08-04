#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_pixel_redux_actors.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    FRAMES_PER_ACTOR = 1200,
};

static face_render_key_t benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 132U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 48U;
    key.controls.mouth_teeth = 128U;
    key.controls.eye_left_open = 220U;
    key.controls.eye_right_open = 216U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 8U;
    key.viseme_weight = 230U;
    key.audio_level = 148U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 40U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.tongue = 82U;
    key.cheek = 36U;
    key.affect_arousal = 148U;
    key.expression_weight = 255U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t pixels[FACE_PIXEL_REDUX_PIXEL_COUNT];
    uint32_t checksum = 2166136261U;
    double slowest_fps = 1000000000.0;
    for (size_t raw = 0U; raw < FACE_PIXEL_REDUX_ACTOR_COUNT; ++raw) {
        face_render_key_t key = benchmark_key();
        const clock_t start = clock();
        assert(start != (clock_t)-1);
        for (uint32_t frame = 0U; frame < FRAMES_PER_ACTOR; ++frame) {
            key.viseme = (uint8_t)(frame % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((frame + 1U) % FACE_VISEME_COUNT);
            key.viseme_blend = (uint8_t)(frame * 37U);
            key.audio_level = (uint8_t)(frame * 29U);
            key.controls.look_x =
                (int8_t)((int)(frame % 101U) - 50);
            key.controls.look_y =
                (int8_t)((int)(frame % 73U) - 36);
            key.stage_expression =
                (uint8_t)(frame % FACE_EXPRESSION_COUNT);
            assert(face_pixel_redux_actor_render(
                (face_pixel_redux_actor_t)raw,
                &key,
                frame * 533U,
                pixels,
                FACE_PIXEL_REDUX_PIXEL_COUNT));
            checksum ^= pixels[(frame * 977U) %
                FACE_PIXEL_REDUX_PIXEL_COUNT];
            checksum *= 16777619U;
        }
        const clock_t end = clock();
        assert(end != (clock_t)-1);
        const double elapsed =
            (double)(end - start) / (double)CLOCKS_PER_SEC;
        const double fps = FRAMES_PER_ACTOR / elapsed;
        if (fps < slowest_fps) {
            slowest_fps = fps;
        }
        printf(
            "%s frames=%d cpu_ms=%.3f cpu_fps=%.2f\n",
            face_pixel_redux_actor_slug(
                (face_pixel_redux_actor_t)raw),
            FRAMES_PER_ACTOR,
            elapsed * 1000.0,
            fps);
    }
    printf(
        "slowest_cpu_fps=%.2f target_fps=30 checksum=%08x\n",
        slowest_fps,
        checksum);
    return slowest_fps > 30.0 ? 0 : 1;
}
