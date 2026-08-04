#include "scenario.h"

#include <string.h>

static uint8_t triangle_wave(uint32_t tick, uint32_t period)
{
    const uint32_t phase = tick % period;
    const uint32_t half = period / 2u;
    if (phase <= half) {
        return (uint8_t)((phase * 255u) / (half == 0 ? 1u : half));
    }
    return (uint8_t)(
        ((period - phase) * 255u) / (period - half));
}

uint32_t scenario_clock(uint32_t frame_index)
{
    /* 16000 samples per second at 30 fps. Integer division is fine:
     * both harnesses use this exact formula. */
    return (frame_index * 16000u) / 30u;
}

void scenario_keyframe(uint32_t frame_index, face_keyframe_t *keyframe)
{
    memset(keyframe, 0, sizeof(*keyframe));
    keyframe->eye_left_open = 255;
    keyframe->eye_right_open = 255;

    if (frame_index < 60u) {
        /* Quiet idle: engine-driven blinks, saccades, idle acts. */
        return;
    }
    if (frame_index < 150u) {
        /* Energetic speech with sweeping articulation. */
        const uint32_t t = frame_index - 60u;
        keyframe->flags = FACE_KEYFRAME_FLAG_SPEAKING;
        keyframe->mouth_open = triangle_wave(t * 5u, 44u);
        keyframe->mouth_width = triangle_wave(t * 3u + 9u, 60u);
        keyframe->mouth_round = triangle_wave(t * 2u + 30u, 90u);
        keyframe->mouth_teeth = triangle_wave(t * 7u + 100u, 210u);
        keyframe->mouth_press = triangle_wave(t * 11u, 260u);
        keyframe->brow = (int8_t)(triangle_wave(t, 80u) / 4u);
        return;
    }
    if (frame_index < 180u) {
        /* Sudden pause: exercises hold-then-close debounce. */
        return;
    }
    if (frame_index < 240u) {
        /* Rounded speech plus a gaze sweep and raised brows. */
        const uint32_t t = frame_index - 180u;
        keyframe->flags = FACE_KEYFRAME_FLAG_SPEAKING;
        keyframe->mouth_open = triangle_wave(t * 6u + 20u, 52u);
        keyframe->mouth_round = 200;
        keyframe->mouth_width = 60;
        keyframe->look_x = (int8_t)(
            (int32_t)triangle_wave(t * 2u, 120u) / 2 - 63);
        keyframe->look_y = (int8_t)(
            (int32_t)triangle_wave(t * 3u + 40u, 90u) / 4 - 31);
        keyframe->brow = 90;
        return;
    }
    if (frame_index < 270u) {
        /* Commanded blink and an alternate expression bank. */
        const uint32_t t = frame_index - 240u;
        keyframe->expression = 1;
        if (t >= 5u && t < 20u) {
            keyframe->flags |= FACE_KEYFRAME_FLAG_BLINKING;
        }
        keyframe->mouth_open = t < 15u ? 40u : 0u;
        keyframe->mouth_width = 180;
        return;
    }
    /* Wind-down idle with a half-lidded sleepy look. */
    keyframe->eye_left_open = 140;
    keyframe->eye_right_open = 140;
    keyframe->look_y = 30;
    keyframe->brow = -60;
}
