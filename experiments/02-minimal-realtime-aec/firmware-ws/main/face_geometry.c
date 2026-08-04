#include "face_geometry.h"

#include <stddef.h>
#include <string.h>

static uint16_t percentage(uint16_t value, uint16_t percent)
{
    return (uint16_t)(((uint32_t)value * percent) / 100U);
}

static uint16_t at_least(uint16_t value, uint16_t minimum)
{
    return value < minimum ? minimum : value;
}

static uint16_t at_most(uint16_t value, uint16_t maximum)
{
    return value > maximum ? maximum : value;
}

static int16_t centred_position(uint16_t centre, uint16_t size,
                                uint16_t limit)
{
    int32_t position = (int32_t)centre - (int32_t)size / 2;
    if (position < 0) {
        return 0;
    }
    if ((uint32_t)position + size > limit) {
        return (int16_t)(limit > size ? limit - size : 0);
    }
    return (int16_t)position;
}

static int16_t scale_gaze(int8_t gaze, uint16_t maximum)
{
    int32_t scaled = (int32_t)gaze * maximum / 8;
    if (scaled > maximum) {
        scaled = maximum;
    } else if (scaled < -(int32_t)maximum) {
        scaled = -(int32_t)maximum;
    }
    return (int16_t)scaled;
}

void face_geometry_from_state(const face_animator_state_t *state,
                              uint16_t display_width,
                              uint16_t display_height,
                              face_geometry_t *geometry)
{
    if (state == NULL || geometry == NULL || display_width == 0 ||
        display_height == 0) {
        return;
    }
    memset(geometry, 0, sizeof(*geometry));

    const uint16_t eye_width = at_most(
        at_least(percentage(display_width, 16), 4), display_width);
    const uint16_t maximum_eye_height = at_most(
        at_least(percentage(display_height, 18), 4), display_height);
    const uint16_t unsquinted_eye_height =
        (uint16_t)(4U +
                   (uint32_t)state->eye_open *
                       (maximum_eye_height - 4U) / UINT8_MAX);
    /*
     * A strong syllable subtly lifts and squints the eyes. This makes PCM
     * animate the expression as a whole without adding state or a second
     * animation clock.
     */
    const uint16_t maximum_speech_squint =
        percentage(maximum_eye_height, 16);
    const uint16_t speech_squint =
        (uint16_t)((uint32_t)state->mouth_open *
                   maximum_speech_squint / UINT8_MAX);
    const uint16_t eye_height =
        unsquinted_eye_height > 4U + speech_squint
            ? (uint16_t)(unsquinted_eye_height - speech_squint)
            : 4U;
    const uint16_t maximum_expression_lift =
        percentage(display_height, 2);
    const uint16_t expression_lift =
        (uint16_t)((uint32_t)state->mouth_open *
                   maximum_expression_lift / UINT8_MAX);
    const uint16_t base_eye_centre_y = percentage(display_height, 38);
    const uint16_t eye_centre_y =
        base_eye_centre_y > expression_lift
            ? (uint16_t)(base_eye_centre_y - expression_lift)
            : 0;
    const uint16_t left_eye_centre_x = percentage(display_width, 30);
    const uint16_t right_eye_centre_x = percentage(display_width, 70);

    geometry->eye_width = eye_width;
    geometry->eye_height = eye_height;
    geometry->left_eye_x = centred_position(
        left_eye_centre_x, eye_width, display_width);
    geometry->left_eye_y = centred_position(
        eye_centre_y, eye_height, display_height);
    geometry->right_eye_x = centred_position(
        right_eye_centre_x, eye_width, display_width);
    geometry->right_eye_y = geometry->left_eye_y;

    const uint16_t base_pupil_size =
        at_least(percentage(display_width, 4), 2);
    geometry->pupil_size = eye_height > 4
                               ? at_most(base_pupil_size, eye_height - 2)
                               : 0;
    const uint16_t maximum_pupil_x =
        eye_width > geometry->pupil_size
            ? (eye_width - geometry->pupil_size) / 3
            : 0;
    const uint16_t maximum_pupil_y =
        eye_height > geometry->pupil_size
            ? (eye_height - geometry->pupil_size) / 3
            : 0;
    geometry->pupil_offset_x =
        scale_gaze(state->gaze_x, maximum_pupil_x);
    geometry->pupil_offset_y =
        scale_gaze(state->gaze_y, maximum_pupil_y);

    const uint16_t minimum_mouth_width =
        at_most(at_least(percentage(display_width, 15), 4), display_width);
    const uint16_t extra_mouth_width = percentage(display_width, 26);
    geometry->mouth_width = at_most(
        (uint16_t)(minimum_mouth_width +
                   (uint32_t)state->mouth_width * extra_mouth_width /
                       UINT8_MAX),
        display_width);
    const uint16_t maximum_extra_mouth_height =
        percentage(display_height, 25);
    geometry->mouth_height = at_most(
        (uint16_t)(4U +
                   (uint32_t)state->mouth_open *
                       maximum_extra_mouth_height / UINT8_MAX),
        display_height);
    geometry->mouth_x = centred_position(
        display_width / 2, geometry->mouth_width, display_width);
    const uint16_t base_mouth_centre_y =
        percentage(display_height, 68);
    const uint16_t mouth_centre_y = at_most(
        (uint16_t)(base_mouth_centre_y + expression_lift),
        display_height);
    geometry->mouth_y = centred_position(
        mouth_centre_y, geometry->mouth_height, display_height);
}
