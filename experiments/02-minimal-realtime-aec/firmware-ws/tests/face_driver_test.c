#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_animator.h"
#include "face_driver.h"

static void invalid_driver_is_a_safe_neutral_face(void)
{
    face_pose_t pose;
    memset(&pose, 0x5a, sizeof(pose));
    face_driver_snapshot(NULL, &pose);
    assert(pose.eye_open == UINT8_MAX);
    assert(pose.viseme == FACE_VISEME_NONE);
    assert(pose.phoneme == FACE_PHONEME_NONE);
}

static void envelope_dispatches_through_the_generic_driver(void)
{
    face_animator_t state;
    face_driver_t driver;
    assert(face_driver_init(
        &driver, &FACE_ALGORITHM_ENVELOPE,
        &state, sizeof(state), 16000, NULL, 0));
    assert(strcmp(face_driver_name(&driver), "envelope") == 0);
    assert(face_driver_state_size(&driver) == sizeof(state));

    const face_stream_event_t response_started = {
        .type = FACE_STREAM_ASSISTANT_RESPONSE_STARTED,
        .received_audio_samples = 0,
        .dispatch_playout_samples = 0,
    };
    face_driver_push_event(&driver, &response_started);
    face_pose_t pose;
    face_driver_snapshot(&driver, &pose);
    assert(pose.activity == FACE_ACTIVITY_THINKING);

    int16_t pcm[160];
    for (size_t index = 0; index < 160; ++index) {
        pcm[index] = index % 20 < 10 ? 8000 : -8000;
    }
    face_driver_push_pcm(&driver, pcm, 160);
    face_driver_snapshot(&driver, &pose);
    assert(pose.playout_samples == 160);
    assert(pose.activity == FACE_ACTIVITY_SPEAKING);
}

static void invalid_storage_and_config_are_rejected(void)
{
    face_driver_t driver;
    face_animator_t state;
    assert(!face_driver_init(
        &driver, &FACE_ALGORITHM_ENVELOPE,
        &state, sizeof(state) - 1, 16000, NULL, 0));
    assert(!face_driver_init(
        &driver, &FACE_ALGORITHM_ENVELOPE,
        &state, sizeof(state), 16000, &state, sizeof(state)));
}

int main(void)
{
    invalid_driver_is_a_safe_neutral_face();
    envelope_dispatches_through_the_generic_driver();
    invalid_storage_and_config_are_rejected();
    puts("face_driver_test: PASS");
    return 0;
}
