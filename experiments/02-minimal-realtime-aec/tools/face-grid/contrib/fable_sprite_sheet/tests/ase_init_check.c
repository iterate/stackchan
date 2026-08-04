#include <stdio.h>
#include <string.h>

#include "sprite_face.h"

#include "ase_hero_atlas.h"

/*
 * Smoke check for the Aseprite ingestion path: the synthetic export
 * converted by tools/test_converter.py must pass full engine
 * validation and render.
 */

static uint16_t frame_buffer[SPRITE_FACE_PIXEL_COUNT];

int main(void)
{
    sprite_face_t face;
    if (!sprite_face_init(&face, &ase_hero_atlas)) {
        printf("FAIL: aseprite-converted atlas rejected by engine\n");
        return 1;
    }
    face_keyframe_t keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.eye_left_open = 255;
    keyframe.eye_right_open = 255;
    keyframe.mouth_open = 200;
    keyframe.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    for (uint32_t frame = 0; frame < 90u; ++frame) {
        if (!sprite_face_render(
                &face, &keyframe, frame * 533u, frame_buffer,
                SPRITE_FACE_PIXEL_COUNT)) {
            printf("FAIL: aseprite-converted atlas render error\n");
            return 1;
        }
    }
    printf("aseprite-mode atlas: engine validation + render OK\n");
    return 0;
}
