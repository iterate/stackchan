#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_abstract_redux.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    AR_BENCH_FRAMES = 900,
};

static face_render_key_t ar_bench_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 182U;
    key.controls.mouth_width = 174U;
    key.controls.mouth_round = 46U;
    key.controls.mouth_press = 12U;
    key.controls.mouth_teeth = 128U;
    key.controls.eye_left_open = 224U;
    key.controls.eye_right_open = 216U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_weight = 230U;
    key.viseme_blend = 44U;
    key.audio_level = 142U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.tongue = 78U;
    key.cheek = 42U;
    key.affect_arousal = 152U;
    key.expression_weight = 255U;
    key.attention = 218U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t pixels[FACE_ABSTRACT_REDUX_PIXEL_COUNT];
    uint32_t checksum = 2166136261U;
    double slowest_fps = 1000000000.0;
    for (size_t raw = 0U; raw < FACE_ABSTRACT_REDUX_COUNT; ++raw) {
        face_render_key_t key = ar_bench_key();
        const clock_t start = clock();
        assert(start != (clock_t)-1);
        for (uint32_t frame = 0U;
             frame < AR_BENCH_FRAMES;
             ++frame) {
            key.viseme =
                (uint8_t)(frame % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((frame + 1U) % FACE_VISEME_COUNT);
            key.viseme_blend = (uint8_t)(frame * 37U);
            key.audio_level = (uint8_t)(frame * 53U);
            key.controls.look_x =
                (int8_t)((int)(frame % 101U) - 50);
            key.controls.look_y =
                (int8_t)((int)(frame % 73U) - 36);
            key.stage_expression =
                (uint8_t)(frame % FACE_EXPRESSION_COUNT);
            assert(face_abstract_redux_render(
                (face_abstract_redux_style_t)raw,
                &key,
                frame * 533U,
                pixels,
                FACE_ABSTRACT_REDUX_PIXEL_COUNT));
            checksum ^= pixels[
                (frame * 977U) %
                FACE_ABSTRACT_REDUX_PIXEL_COUNT];
            checksum *= 16777619U;
        }
        const clock_t end = clock();
        assert(end != (clock_t)-1);
        const double elapsed =
            (double)(end - start) / (double)CLOCKS_PER_SEC;
        const double fps = AR_BENCH_FRAMES / elapsed;
        if (fps < slowest_fps) {
            slowest_fps = fps;
        }
        face_abstract_redux_info_t info;
        assert(face_abstract_redux_info(
            (face_abstract_redux_style_t)raw, &info));
        printf(
            "legacy %u %-28s %9.1f fps (%7.3f ms/frame)\n",
            (unsigned)info.legacy_profile_id,
            face_abstract_redux_slug(
                (face_abstract_redux_style_t)raw),
            fps,
            1000.0 / fps);
    }
    printf(
        "abstract redux benchmark: slowest %.1f fps, "
        "target 30 fps, checksum %08x\n",
        slowest_fps,
        checksum);
    return slowest_fps > 30.0 ? 0 : 1;
}
