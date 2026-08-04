#include "../src/fable_studies.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Contact-sheet renderer for visual review: one PPM per study, frames
 * left to right, plus a keyframe strip that sweeps mouth and activity
 * states. Usage:
 *
 *   render_ppm <out_dir> [frames] [step_ms]
 *
 * Defaults: 8 frames, 700 ms apart, starting a little into the clock so
 * blinks and acts are likely to appear on the sheet.
 */

enum {
    W = FABLE_STUDY_WIDTH,
    H = FABLE_STUDY_HEIGHT,
};

static uint16_t frame[FABLE_STUDY_PIXELS];

static void frame_into_sheet(uint8_t *rgb, int cols, int col)
{
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint16_t c = frame[y * W + x];
            const int r5 = (c >> 11) & 0x1f;
            const int g6 = (c >> 5) & 0x3f;
            const int b5 = c & 0x1f;
            uint8_t *dst =
                rgb + 3 * ((size_t)y * (size_t)(cols * W) +
                           (size_t)(col * W + x));
            dst[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            dst[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            dst[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
}

static int write_ppm(const char *path, const uint8_t *rgb, int cols)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", cols * W, H);
    fwrite(rgb, 1, (size_t)cols * W * H * 3, f);
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : "out";
    const int frames = argc > 2 ? atoi(argv[2]) : 8;
    const uint32_t step_ms = argc > 3 ? (uint32_t)atoi(argv[3]) : 700U;
    if (frames < 1 || frames > 64) {
        fprintf(stderr, "frames must be 1..64\n");
        return 1;
    }

    uint8_t *rgb = malloc((size_t)frames * W * H * 3);
    if (rgb == NULL) {
        return 1;
    }
    char path[512];
    int rc = 0;

    /* Idle reels: each study breathing, blinking, acting on its own. */
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        for (int f = 0; f < frames; f++) {
            const uint32_t clock =
                1000U * 16U + (uint32_t)f * step_ms * 16U;
            fable_study_render((fable_study_t)s, NULL, clock, frame,
                               FABLE_STUDY_PIXELS);
            frame_into_sheet(rgb, frames, f);
        }
        snprintf(path, sizeof(path), "%s/idle_%s.ppm", out_dir,
                 fable_study_slug((fable_study_t)s));
        rc |= write_ppm(path, rgb, frames);
    }

    /* Dialogue strip: mouth sweep + the four activities, per study. */
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        for (int f = 0; f < frames; f++) {
            fable_keyframe_t kf;
            memset(&kf, 0, sizeof(kf));
            kf.eye_left_open = 255;
            kf.eye_right_open = 255;
            const int half = frames / 2 > 0 ? frames / 2 : 1;
            if (f < half) {
                kf.expression = FABLE_ACTIVITY_SPEAKING;
                kf.flags = FABLE_KEYFRAME_FLAG_SPEAKING;
                kf.mouth_open = (uint8_t)(40 + (215 * f) / half);
                kf.mouth_width = (uint8_t)(120 + (100 * f) / half);
                kf.mouth_round = (uint8_t)((f & 1) != 0 ? 200 : 40);
                kf.mouth_teeth = (uint8_t)((f & 1) != 0 ? 0 : 160);
            } else {
                kf.expression =
                    (uint8_t)((f - half) % 4U); /* idle..speaking */
                kf.mouth_open = kf.expression == 3 ? 120 : 0;
                kf.flags = kf.expression == 3
                               ? FABLE_KEYFRAME_FLAG_SPEAKING
                               : 0U;
            }
            const uint32_t clock =
                4000U * 16U + (uint32_t)f * step_ms * 16U;
            fable_study_render((fable_study_t)s, &kf, clock, frame,
                               FABLE_STUDY_PIXELS);
            frame_into_sheet(rgb, frames, f);
        }
        snprintf(path, sizeof(path), "%s/dialog_%s.ppm", out_dir,
                 fable_study_slug((fable_study_t)s));
        rc |= write_ppm(path, rgb, frames);
    }

    free(rgb);
    return rc;
}
