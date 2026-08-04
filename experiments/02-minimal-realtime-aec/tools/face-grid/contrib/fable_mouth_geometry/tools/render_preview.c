#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fmg.h"

/*
 * Renders a review montage: one row per profile, one column per scenario,
 * written as a binary PPM (convert with `sips -s format png` on macOS).
 * Also emits a 48-frame animation strip per profile when given a second
 * argument, for eyeballing idle motion and coarticulated speech.
 */

typedef struct {
    const char *name;
    fmg_keyframe_t kf;
    uint32_t clock;
} scenario_t;

static const scenario_t s_scenarios[] = {
    {"idle", {0, 110, 30, 0, 0, 255, 255, 0, 0, 0, 0, 0}, 30000},
    {"aa", {236, 205, 24, 0, 18, 255, 255, 0, 0, 0, 3, 1}, 51200},
    {"oh", {214, 112, 255, 0, 16, 255, 255, 0, 0, 0, 3, 1}, 72000},
    {"ee", {155, 246, 0, 0, 128, 255, 255, 0, 0, 10, 3, 1}, 90000},
    {"mbp", {12, 164, 18, 255, 0, 255, 255, 0, 0, 0, 3, 1}, 111000},
    {"fv", {38, 198, 0, 176, 235, 255, 255, 0, 0, 0, 3, 1}, 130000},
    {"ln", {80, 198, 6, 0, 145, 255, 255, 0, 0, 0, 3, 1}, 150000},
    {"blink", {0, 110, 30, 0, 0, 40, 40, 0, 0, 0, 0, 2}, 170000},
    {"think", {0, 110, 30, 0, 0, 255, 255, 45, -30, 24, 2, 0}, 200000},
};

enum {
    SCENARIO_COUNT = (int)(sizeof(s_scenarios) / sizeof(s_scenarios[0]))
};

static void put_rgb565(FILE *f, const uint16_t *frame)
{
    for (int i = 0; i < FMG_PIXEL_COUNT; i++) {
        uint16_t c = frame[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) * 255 + 15) / 31),
            (uint8_t)((((c >> 5) & 63) * 255 + 31) / 63),
            (uint8_t)(((c & 31) * 255 + 15) / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
}

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : "preview";
    bool anim = argc > 2;
    static uint16_t frame[FMG_PIXEL_COUNT];
    int profiles = (int)fmg_profile_count();

    /* montage: assemble row-major into one big RGB buffer */
    int mw = FMG_WIDTH * SCENARIO_COUNT;
    int mh = FMG_HEIGHT * profiles;
    uint8_t *montage = malloc((size_t)mw * mh * 3);
    if (montage == NULL) {
        return 1;
    }
    for (int p = 0; p < profiles; p++) {
        for (int s = 0; s < SCENARIO_COUNT; s++) {
            fmg_render_frame((fmg_profile_t)p, &s_scenarios[s].kf,
                             s_scenarios[s].clock, frame, FMG_PIXEL_COUNT);
            for (int y = 0; y < FMG_HEIGHT; y++) {
                for (int x = 0; x < FMG_WIDTH; x++) {
                    uint16_t c = frame[y * FMG_WIDTH + x];
                    size_t o = (((size_t)(p * FMG_HEIGHT + y) * mw) +
                                (size_t)(s * FMG_WIDTH + x)) * 3;
                    montage[o] = (uint8_t)(((c >> 11) * 255 + 15) / 31);
                    montage[o + 1] =
                        (uint8_t)((((c >> 5) & 63) * 255 + 31) / 63);
                    montage[o + 2] = (uint8_t)(((c & 31) * 255 + 15) / 31);
                }
            }
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/montage.ppm", out_dir);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(montage);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", mw, mh);
    fwrite(montage, 3, (size_t)mw * mh, f);
    fclose(f);
    free(montage);
    printf("wrote %s (%dx%d)\n", path, mw, mh);

    if (!anim) {
        return 0;
    }
    /* 48-frame speaking+idle animation per profile */
    for (int p = 0; p < profiles; p++) {
        for (int fr = 0; fr < 48; fr++) {
            uint32_t clock = (uint32_t)fr * 533U + 20000U;
            /* sweep through vowels then fall silent */
            const scenario_t *sc = &s_scenarios[1 + (fr / 6) % 6];
            fmg_keyframe_t kf = fr < 36 ? sc->kf : s_scenarios[0].kf;
            fmg_render_frame((fmg_profile_t)p, &kf, clock, frame,
                             FMG_PIXEL_COUNT);
            snprintf(path, sizeof(path), "%s/%s_%02d.ppm", out_dir,
                     fmg_profile_slug((fmg_profile_t)p), fr);
            f = fopen(path, "wb");
            if (f == NULL) {
                return 1;
            }
            fprintf(f, "P6\n%d %d\n255\n", FMG_WIDTH, FMG_HEIGHT);
            put_rgb565(f, frame);
            fclose(f);
        }
    }
    printf("wrote animation strips to %s/\n", out_dir);
    return 0;
}
