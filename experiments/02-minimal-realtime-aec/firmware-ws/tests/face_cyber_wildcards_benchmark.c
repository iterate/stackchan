#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "face_cyber_wildcards.h"
#include "face_pose.h"
#include "face_stage.h"

enum {
    FRAMES_PER_PROFILE = 480,
};

static uint64_t now_ns(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) {
        return 0U;
    }
    return (uint64_t)value.tv_sec * 1000000000ULL +
        (uint64_t)value.tv_nsec;
}

static face_render_key_t benchmark_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 128U;
    key.controls.mouth_width = 176U;
    key.controls.mouth_round = 58U;
    key.controls.mouth_teeth = 120U;
    key.controls.eye_left_open = 238U;
    key.controls.eye_right_open = 240U;
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
    key.affect_arousal = 140U;
    key.expression_weight = 255U;
    key.attention = 214U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = FACE_EXPRESSION_WARM;
    return key;
}

int main(void)
{
    uint16_t pixels[FACE_CYBER_WILDCARD_PIXEL_COUNT];
    face_render_key_t key = benchmark_key();
    uint32_t checksum = 2166136261U;
    uint64_t per_profile[FACE_CYBER_WILDCARD_COUNT] = {0U};
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        const uint64_t start = now_ns();
        for (uint32_t frame = 0U; frame < FRAMES_PER_PROFILE; ++frame) {
            key.viseme = (uint8_t)(frame % FACE_VISEME_COUNT);
            key.viseme_secondary =
                (uint8_t)((frame + 1U) % FACE_VISEME_COUNT);
            key.viseme_blend = (uint8_t)(frame * 31U);
            key.audio_level = (uint8_t)(64U + frame % 176U);
            key.stage_expression =
                (uint8_t)(frame % FACE_EXPRESSION_COUNT);
            if (!face_cyber_wildcard_render(
                    (face_cyber_wildcard_profile_t)raw,
                    &key,
                    16000U + frame * 533U,
                    pixels,
                    FACE_CYBER_WILDCARD_PIXEL_COUNT)) {
                return 1;
            }
            checksum ^= pixels[
                (frame * 7919U) % FACE_CYBER_WILDCARD_PIXEL_COUNT];
            checksum *= 16777619U;
        }
        per_profile[raw] =
            (now_ns() - start) / FRAMES_PER_PROFILE;
        printf(
            "%s ns_per_frame=%llu fps=%llu\n",
            face_cyber_wildcard_slug(
                (face_cyber_wildcard_profile_t)raw),
            (unsigned long long)per_profile[raw],
            (unsigned long long)(
                per_profile[raw] == 0U
                ? 0U
                : 1000000000ULL / per_profile[raw]));
    }
    uint64_t worst = 0U;
    for (size_t raw = 0U; raw < FACE_CYBER_WILDCARD_COUNT; ++raw) {
        if (per_profile[raw] > worst) {
            worst = per_profile[raw];
        }
    }
    printf(
        "profiles=3 frames_per_profile=%d worst_ns=%llu checksum=%08x\n",
        FRAMES_PER_PROFILE,
        (unsigned long long)worst,
        checksum);
    return 0;
}
