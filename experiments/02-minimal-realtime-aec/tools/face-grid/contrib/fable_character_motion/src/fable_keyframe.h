#pragma once

#include <stdint.h>

/*
 * Standalone mirror of the stable 12-byte semantic keyframe defined by
 * firmware-ws/main/face_keyframe.h. This contribution must build without
 * reaching outside its directory, so the wire structure is re-declared here
 * byte-for-byte. A static assert keeps the ABI honest; integration can swap
 * this header for the firmware one without touching any other file.
 */
typedef struct {
    uint8_t mouth_open;
    uint8_t mouth_width;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t mouth_teeth;
    uint8_t eye_left_open;
    uint8_t eye_right_open;
    int8_t look_x;
    int8_t look_y;
    int8_t brow;
    uint8_t expression;
    uint8_t flags;
} fable_keyframe_t;

enum {
    FABLE_KEYFRAME_FLAG_SPEAKING = 1U << 0,
    FABLE_KEYFRAME_FLAG_BLINKING = 1U << 1,
    FABLE_KEYFRAME_BYTES = 12,
};

/*
 * face_keyframe_from_pose() stores face_activity_t in `expression`, so the
 * motion engine can stage distinct idle / listening / thinking / speaking
 * behavior. Unknown values fall back to IDLE (or SPEAKING when the flag says
 * so), which keeps the engine safe against future expression payloads.
 */
enum {
    FABLE_ACTIVITY_IDLE = 0,
    FABLE_ACTIVITY_LISTENING = 1,
    FABLE_ACTIVITY_THINKING = 2,
    FABLE_ACTIVITY_SPEAKING = 3,
};

_Static_assert(
    sizeof(fable_keyframe_t) == FABLE_KEYFRAME_BYTES,
    "fable keyframe mirror must stay exactly 12 bytes");
