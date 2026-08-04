#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pixel_pack.h"
#include "face_stage.h"

static uint16_t frame[FACE_PIXEL_PACK_PIXEL_COUNT];

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < FACE_PIXEL_PACK_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static face_render_key_t signature_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 138U;
    key.controls.mouth_width = 164U;
    key.controls.mouth_round = 48U;
    key.controls.mouth_teeth = 92U;
    key.controls.eye_left_open = 236U;
    key.controls.eye_right_open = 242U;
    key.controls.look_x = -18;
    key.controls.look_y = 8;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 44U;
    key.viseme_weight = 218U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 152U;
    key.attention = 230U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

int main(void)
{
    uint32_t aggregate = 2166136261U;
    for (size_t profile = 0U;
         profile < FACE_PIXEL_PACK_PROFILE_COUNT;
         ++profile) {
        for (uint8_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            for (uint8_t articulation = 0U;
                 articulation < 4U;
                 ++articulation) {
                face_render_key_t key = signature_key();
                key.stage_expression = expression;
                key.expression_weight = 255U;
                key.controls.mouth_open =
                    (uint8_t)(articulation * 72U);
                key.controls.mouth_round =
                    (uint8_t)(articulation * 61U);
                key.viseme =
                    (uint8_t)(
                        (expression + articulation * 3U) %
                        FACE_VISEME_COUNT);
                const uint32_t clock =
                    8191U +
                    (uint32_t)expression * 997U +
                    (uint32_t)articulation * 4093U;
                assert(face_pixel_pack_render(
                    (face_pixel_pack_profile_t)profile,
                    &key, clock, frame,
                    FACE_PIXEL_PACK_PIXEL_COUNT));
                const uint32_t hash = frame_hash(frame);
                aggregate ^= hash;
                aggregate *= 16777619U;
                printf(
                    "%zu %u %u %08" PRIx32 "\n",
                    profile, expression, articulation, hash);
            }
        }
    }
    printf("aggregate %08" PRIx32 "\n", aggregate);
    return 0;
}
