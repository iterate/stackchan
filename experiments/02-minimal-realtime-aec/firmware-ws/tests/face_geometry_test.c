#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "face_geometry.h"

static void rest_geometry_is_visible_and_inside_the_screen(void)
{
    const face_animator_state_t state = {
        .eye_open = 255,
    };
    face_geometry_t geometry;

    face_geometry_from_state(&state, 320, 240, &geometry);

    assert(geometry.left_eye_x >= 0);
    assert(geometry.left_eye_y >= 0);
    assert(geometry.right_eye_x + geometry.eye_width <= 320);
    assert(geometry.left_eye_y + geometry.eye_height <= 240);
    assert(geometry.mouth_x >= 0);
    assert(geometry.mouth_y >= 0);
    assert(geometry.mouth_x + geometry.mouth_width <= 320);
    assert(geometry.mouth_y + geometry.mouth_height <= 240);
    assert(geometry.eye_height >= 4);
    assert(geometry.mouth_height >= 4);
}

static void pcm_features_have_distinct_visible_geometry(void)
{
    const face_animator_state_t rest = {
        .eye_open = 255,
    };
    const face_animator_state_t vowel = {
        .mouth_open = 220,
        .mouth_width = 220,
        .eye_open = 255,
        .gaze_x = -8,
        .gaze_y = 5,
        .speaking = true,
    };
    const face_animator_state_t fricative = {
        .mouth_open = 220,
        .mouth_width = 112,
        .eye_open = 64,
        .gaze_x = 8,
        .gaze_y = -5,
        .speaking = true,
    };
    face_geometry_t rest_geometry;
    face_geometry_t vowel_geometry;
    face_geometry_t fricative_geometry;

    face_geometry_from_state(&rest, 320, 240, &rest_geometry);
    face_geometry_from_state(&vowel, 320, 240, &vowel_geometry);
    face_geometry_from_state(
        &fricative, 320, 240, &fricative_geometry);

    assert(vowel_geometry.mouth_height > rest_geometry.mouth_height);
    assert(vowel_geometry.mouth_width > fricative_geometry.mouth_width);
    assert(fricative_geometry.eye_height < vowel_geometry.eye_height);
    assert(vowel_geometry.pupil_offset_x <
           fricative_geometry.pupil_offset_x);
    assert(vowel_geometry.pupil_offset_y >
           fricative_geometry.pupil_offset_y);
}

static void strong_speech_opens_the_face_as_a_whole(void)
{
    const face_animator_state_t rest = {
        .eye_open = 255,
    };
    const face_animator_state_t strong_speech = {
        .mouth_open = 255,
        .mouth_width = 255,
        .eye_open = 255,
        .speaking = true,
    };
    face_geometry_t rest_geometry;
    face_geometry_t speaking_geometry;

    face_geometry_from_state(&rest, 320, 240, &rest_geometry);
    face_geometry_from_state(
        &strong_speech, 320, 240, &speaking_geometry);

    assert(speaking_geometry.mouth_width >= 120);
    assert(speaking_geometry.mouth_height >= 60);
    assert(speaking_geometry.eye_height < rest_geometry.eye_height);
    assert(speaking_geometry.left_eye_y < rest_geometry.left_eye_y);
    assert(speaking_geometry.mouth_y +
               (int16_t)(speaking_geometry.mouth_height / 2) >
           rest_geometry.mouth_y +
               (int16_t)(rest_geometry.mouth_height / 2));
}

int main(void)
{
    rest_geometry_is_visible_and_inside_the_screen();
    pcm_features_have_distinct_visible_geometry();
    strong_speech_opens_the_face_as_a_whole();
    puts("face_geometry_test: PASS");
    return 0;
}
