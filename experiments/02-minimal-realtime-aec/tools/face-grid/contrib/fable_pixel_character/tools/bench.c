#include <stdio.h>
#include <time.h>

#include "pixel_face.h"

/*
 * Host benchmark: renders 240 frames per profile with a sweeping keyframe
 * and reports ms/frame. Host numbers only bound the shape of the cost —
 * the ESP32-S3 budget check must be repeated on-device — but any profile
 * that cannot hold 30 fps on a laptop is disqualified immediately.
 */

static uint16_t frame[PIXEL_FACE_PIXEL_COUNT];

int main(void) {
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int frames = 240;
        for (int i = 0; i < frames; ++i) {
            face_keyframe_t k = {
                (uint8_t)((i * 7) & 0xFF),
                (uint8_t)(128 + ((i * 3) & 0x7F)),
                (uint8_t)((i * 11) & 0xFF),
                0,
                (uint8_t)((i * 5) & 0xFF),
                255,
                255,
                (int8_t)(((i * 13) & 0xFF) - 128),
                0,
                (int8_t)(((i * 17) & 0x7F) - 64),
                0,
                FACE_KEYFRAME_FLAG_SPEAKING,
            };
            pixel_face_render((pixel_face_profile_t)p, &k,
                              (uint32_t)i * 533U, frame,
                              PIXEL_FACE_PIXEL_COUNT);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                    (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
        printf("%-18s %7.3f ms/frame (%6.0f fps host)\n",
               pixel_face_profile_slug((pixel_face_profile_t)p),
               ms / frames, frames * 1000.0 / ms);
    }
    return 0;
}
