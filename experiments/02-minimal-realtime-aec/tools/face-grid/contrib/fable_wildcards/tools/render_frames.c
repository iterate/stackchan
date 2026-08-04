#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wildcard_face.h"

/*
 * Frame dumper for previews and visual iteration.
 *
 *   render_frames <slug|index> <script> <frame_count> <fps> <out_dir>
 *
 * Scripts drive the keyframe deterministically:
 *   idle    - neutral face, silence: shows the idle rig
 *   speech  - a scripted pseudo-speech phrase cycling visemes with pauses
 *   sweep   - slow parameter sweeps that exercise every keyframe byte
 * Frames are written as binary PPM (P6) named frame_%04d.ppm.
 */

static void keyframe_for(const char *script, uint32_t clock, wc_keyframe_t *kf) {
    memset(kf, 0, sizeof *kf);
    kf->eye_left_open = 255;
    kf->eye_right_open = 255;
    kf->mouth_width = 128;

    if (strcmp(script, "idle") == 0) {
        return;
    }

    uint32_t ms = clock / 16u;
    if (strcmp(script, "speech") == 0) {
        /* pseudo-phrase: bursts of visemes with pauses, 4 s loop */
        uint32_t t = ms % 4000u;
        struct seg { uint32_t until; uint8_t open, width, round, teeth; };
        static const struct seg phrase[] = {
            { 120, 40, 128, 30, 0 },    /* attack */
            { 300, 190, 150, 20, 40 },  /* AH */
            { 450, 90, 220, 10, 160 },  /* EE */
            { 640, 210, 90, 200, 0 },   /* OH */
            { 800, 60, 200, 15, 210 },  /* SS */
            { 950, 170, 130, 60, 30 },  /* AH */
            { 1100, 20, 128, 40, 0 },   /* closure */
            { 1350, 150, 110, 180, 10 },/* OO */
            { 1500, 80, 230, 10, 190 }, /* IH */
            { 1700, 200, 140, 30, 60 }, /* AH */
            { 1900, 0, 128, 0, 0 },     /* pause */
            { 2200, 120, 160, 20, 90 },
            { 2400, 220, 120, 150, 0 },
            { 2600, 70, 210, 10, 200 },
            { 2850, 180, 140, 90, 20 },
            { 3000, 0, 128, 0, 0 },
            { 4000, 0, 128, 0, 0 },     /* long listen pause */
        };
        for (size_t i = 0; i < sizeof phrase / sizeof phrase[0]; ++i) {
            if (t < phrase[i].until) {
                kf->mouth_open = phrase[i].open;
                kf->mouth_width = phrase[i].width;
                kf->mouth_round = phrase[i].round;
                kf->mouth_teeth = phrase[i].teeth;
                if (phrase[i].open || t < 3000u) {
                    kf->flags |= WC_KEYFRAME_FLAG_SPEAKING;
                }
                break;
            }
        }
        /* wobble inside each viseme so nothing freezes */
        if (kf->mouth_open) {
            int32_t w = (int32_t)(ms % 90u);
            int32_t dip = w < 45 ? w : 90 - w;
            int32_t open = kf->mouth_open - dip;
            kf->mouth_open = (uint8_t)(open < 0 ? 0 : open);
        }
        return;
    }

    if (strcmp(script, "sweep") == 0) {
        uint32_t t = ms % 12000u;
        if (t < 2000u) {
            kf->mouth_open = (uint8_t)(t * 255u / 2000u);
            kf->flags |= WC_KEYFRAME_FLAG_SPEAKING;
        } else if (t < 4000u) {
            kf->mouth_open = 160;
            kf->mouth_width = (uint8_t)((t - 2000u) * 255u / 2000u);
            kf->flags |= WC_KEYFRAME_FLAG_SPEAKING;
        } else if (t < 6000u) {
            kf->mouth_open = 160;
            kf->mouth_round = (uint8_t)((t - 4000u) * 255u / 2000u);
            kf->flags |= WC_KEYFRAME_FLAG_SPEAKING;
        } else if (t < 8000u) {
            kf->mouth_open = 120;
            kf->mouth_teeth = (uint8_t)((t - 6000u) * 255u / 2000u);
            kf->flags |= WC_KEYFRAME_FLAG_SPEAKING;
        } else if (t < 10000u) {
            int32_t p = (int32_t)(t - 8000u);
            kf->brow = (int8_t)(p < 1000 ? (p * 254 / 1000) - 127
                                         : 127 - ((p - 1000) * 254 / 1000));
        } else {
            int32_t p = (int32_t)(t - 10000u);
            kf->look_x = (int8_t)(p < 1000 ? (p * 254 / 1000) - 127
                                           : 127 - ((p - 1000) * 254 / 1000));
            kf->look_y = (int8_t)(kf->look_x / 2);
        }
        return;
    }

    fprintf(stderr, "unknown script '%s'\n", script);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
                "usage: %s <slug|index> <idle|speech|sweep> <frames> <fps> <out_dir>\n",
                argv[0]);
        return 2;
    }
    int profile = -1;
    for (int i = 0; i < (int)wc_profile_count(); ++i) {
        if (strcmp(argv[1], wc_profile_slug((wc_profile_t)i)) == 0) {
            profile = i;
        }
    }
    if (profile < 0) {
        profile = atoi(argv[1]);
    }
    if (profile < 0 || profile >= (int)wc_profile_count()) {
        fprintf(stderr, "unknown profile '%s'\n", argv[1]);
        return 2;
    }
    const char *script = argv[2];
    int frames = atoi(argv[3]);
    int fps = atoi(argv[4]);
    const char *out_dir = argv[5];

    static uint16_t fb[WC_FACE_PIXEL_COUNT];
    static uint8_t rgb[WC_FACE_PIXEL_COUNT * 3];

    for (int f = 0; f < frames; ++f) {
        uint32_t clock = (uint32_t)((uint64_t)f * WC_SAMPLE_RATE_HZ / (uint32_t)fps);
        wc_keyframe_t kf;
        keyframe_for(script, clock, &kf);
        if (!wc_render_frame((wc_profile_t)profile, &kf, clock, fb,
                             WC_FACE_PIXEL_COUNT)) {
            fprintf(stderr, "render failed\n");
            return 1;
        }
        for (int i = 0; i < WC_FACE_PIXEL_COUNT; ++i) {
            uint16_t p = fb[i];
            uint32_t r = (p >> 11) & 31u, g = (p >> 5) & 63u, b = p & 31u;
            rgb[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
            rgb[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            rgb[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
        }
        char path[512];
        snprintf(path, sizeof path, "%s/frame_%04d.ppm", out_dir, f);
        FILE *fp = fopen(path, "wb");
        if (!fp) {
            perror(path);
            return 1;
        }
        fprintf(fp, "P6\n%d %d\n255\n", WC_FACE_WIDTH, WC_FACE_HEIGHT);
        fwrite(rgb, 1, sizeof rgb, fp);
        fclose(fp);
    }
    return 0;
}
