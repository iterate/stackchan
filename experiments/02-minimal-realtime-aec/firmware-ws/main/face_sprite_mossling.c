#include "face_sprite_mossling.h"

#include "face_performance.h"
#include "face_sprite_mossling_generated.h"
#include "face_sprite_sheet.h"

static const face_performance_profile_t MOSSLING_PERFORMANCE = {
    .seed = 0x4d4f5353U,
    .motion_gain = 236U,
    .gaze_gain = 224U,
    .expression_gain = 216U,
    .blink_gain = 236U,
};

bool face_sprite_mossling_render(
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    static face_sprite_player_t validated_player;
    static bool ready;
    if (render_key == NULL) {
        return false;
    }
    if (!ready) {
        if (!face_sprite_player_init(
                &validated_player,
                &face_sprite_pocket_mossling_authored_atlas)) {
            return false;
        }
        ready = true;
    }

    face_render_key_t animated = *render_key;
    /*
     * A freshly-created browser/device pose is zero-filled.  Treat that as
     * "eyes open" unless the stream explicitly says it is blinking; otherwise
     * the first visible Mossling frame looks asleep until PCM arrives.
     */
    if (animated.controls.eye_left_open == 0U &&
        animated.controls.eye_right_open == 0U &&
        (animated.controls.flags & FACE_KEYFRAME_FLAG_BLINKING) == 0U) {
        animated.controls.eye_left_open = 255U;
        animated.controls.eye_right_open = 255U;
    }
    if (!face_performance_apply(
            &MOSSLING_PERFORMANCE,
            sample_clock,
            &animated,
            NULL)) {
        return false;
    }
    return face_sprite_render_snapshot(
        &validated_player,
        &animated,
        sample_clock,
        rgb565,
        pixel_capacity);
}
