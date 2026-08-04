/*
 * Frame inspection tool for the fable_robot_eyes contribution.
 *
 *   fre_dump grid <out.ppm> [ms] [expression]   4x4 sheet of all profiles
 *   fre_dump anim <slug> <outdir> <frames> <fps> [start_ms] [expression]
 *   fre_dump strip <slug> <out.ppm> <t0> <t1> <cols> [expression]
 *   fre_dump hash                                FNV-1a of a fixed matrix
 *   fre_dump bench                               frame timing per profile
 *
 * expression: 0 idle, 1 listening, 2 thinking, 3 speaking.
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../fable_robot_eyes.h"

static fre_keyframe_t fre_neutral_keyframe(int expression)
{
    fre_keyframe_t kf;
    memset(&kf, 0, sizeof kf);
    kf.mouth_width = 128;
    kf.mouth_round = 128;
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.expression = (uint8_t)expression;
    if (expression == 3) {
        kf.flags = FRE_KEYFRAME_FLAG_SPEAKING;
        kf.mouth_open = 140;
    }
    return kf;
}

static void fre_write_ppm(
    const char *path, const uint16_t *pix, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        uint16_t p = pix[i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((p >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (unsigned char)(((p >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (unsigned char)((p & 0x1F) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int fre_find_profile(const char *slug)
{
    for (size_t i = 0; i < fre_profile_count(); ++i) {
        if (strcmp(fre_profile_slug((fre_profile_t)i), slug) == 0) {
            return (int)i;
        }
    }
    /* Also accept a bare index. */
    char *end = NULL;
    long v = strtol(slug, &end, 10);
    if (end != NULL && *end == '\0' && v >= 0 &&
        (size_t)v < fre_profile_count()) {
        return (int)v;
    }
    fprintf(stderr, "unknown profile %s\n", slug);
    exit(1);
}

static uint32_t fre_fnv1a(
    uint32_t h, const unsigned char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 16777619U;
    }
    return h;
}

static uint16_t frame[FRE_FRAME_PIXEL_COUNT];

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fre_dump grid|anim|strip|hash|bench ...\n");
        return 1;
    }
    if (strcmp(argv[1], "grid") == 0) {
        const char *out = argc > 2 ? argv[2] : "grid.ppm";
        uint32_t ms = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10)
                               : 1500U;
        int expr = argc > 4 ? atoi(argv[4]) : 0;
        fre_keyframe_t kf = fre_neutral_keyframe(expr);
        int cols = 4, rows = 4;
        int w = cols * FRE_FRAME_WIDTH, h = rows * FRE_FRAME_HEIGHT;
        uint16_t *sheet = calloc((size_t)w * h, sizeof(uint16_t));
        for (size_t p = 0; p < fre_profile_count(); ++p) {
            fre_render_frame((fre_profile_t)p, &kf, ms * 16U, frame,
                FRE_FRAME_PIXEL_COUNT);
            int ox = ((int)p % cols) * FRE_FRAME_WIDTH;
            int oy = ((int)p / cols) * FRE_FRAME_HEIGHT;
            for (int y = 0; y < FRE_FRAME_HEIGHT; ++y) {
                memcpy(sheet + (size_t)(oy + y) * w + ox,
                    frame + (size_t)y * FRE_FRAME_WIDTH,
                    FRE_FRAME_WIDTH * sizeof(uint16_t));
            }
        }
        fre_write_ppm(out, sheet, w, h);
        free(sheet);
        printf("wrote %s at t=%ums expr=%d\n", out, ms, expr);
        return 0;
    }
    if (strcmp(argv[1], "anim") == 0) {
        if (argc < 6) {
            fprintf(stderr,
                "usage: fre_dump anim <slug> <outdir> <frames> <fps> "
                "[start_ms] [expr]\n");
            return 1;
        }
        int prof = fre_find_profile(argv[2]);
        const char *dir = argv[3];
        int frames = atoi(argv[4]);
        int fps = atoi(argv[5]);
        uint32_t start_ms = argc > 6
            ? (uint32_t)strtoul(argv[6], NULL, 10) : 0U;
        int expr = argc > 7 ? atoi(argv[7]) : 0;
        fre_keyframe_t kf = fre_neutral_keyframe(expr);
        char path[512];
        for (int i = 0; i < frames; ++i) {
            uint32_t ms = start_ms + (uint32_t)(i * 1000 / fps);
            fre_render_frame((fre_profile_t)prof, &kf, ms * 16U, frame,
                FRE_FRAME_PIXEL_COUNT);
            snprintf(path, sizeof path, "%s/f%05d.ppm", dir, i);
            fre_write_ppm(path, frame, FRE_FRAME_WIDTH, FRE_FRAME_HEIGHT);
        }
        printf("wrote %d frames to %s\n", frames, dir);
        return 0;
    }
    if (strcmp(argv[1], "strip") == 0) {
        if (argc < 7) {
            fprintf(stderr,
                "usage: fre_dump strip <slug> <out.ppm> <t0> <t1> <cols> "
                "[expr]\n");
            return 1;
        }
        int prof = fre_find_profile(argv[2]);
        const char *out = argv[3];
        uint32_t t0 = (uint32_t)strtoul(argv[4], NULL, 10);
        uint32_t t1 = (uint32_t)strtoul(argv[5], NULL, 10);
        int cols = atoi(argv[6]);
        int expr = argc > 7 ? atoi(argv[7]) : 0;
        fre_keyframe_t kf = fre_neutral_keyframe(expr);
        int w = cols * FRE_FRAME_WIDTH;
        uint16_t *sheet = calloc(
            (size_t)w * FRE_FRAME_HEIGHT, sizeof(uint16_t));
        for (int i = 0; i < cols; ++i) {
            uint32_t ms = t0 + (t1 - t0) * (uint32_t)i /
                (uint32_t)(cols > 1 ? cols - 1 : 1);
            fre_render_frame((fre_profile_t)prof, &kf, ms * 16U, frame,
                FRE_FRAME_PIXEL_COUNT);
            for (int y = 0; y < FRE_FRAME_HEIGHT; ++y) {
                memcpy(sheet + (size_t)y * w + i * FRE_FRAME_WIDTH,
                    frame + (size_t)y * FRE_FRAME_WIDTH,
                    FRE_FRAME_WIDTH * sizeof(uint16_t));
            }
        }
        fre_write_ppm(out, sheet, w, FRE_FRAME_HEIGHT);
        free(sheet);
        printf("wrote %s\n", out);
        return 0;
    }
    if (strcmp(argv[1], "hash") == 0) {
        /* Deterministic matrix: profiles x expressions x timestamps. */
        static const uint32_t times_ms[] = {
            0, 137, 1500, 3210, 9999, 45123, 120000,
        };
        for (size_t p = 0; p < fre_profile_count(); ++p) {
            for (int expr = 0; expr < 4; ++expr) {
                fre_keyframe_t kf = fre_neutral_keyframe(expr);
                uint32_t h = 2166136261U;
                for (size_t t = 0;
                     t < sizeof times_ms / sizeof times_ms[0]; ++t) {
                    fre_render_frame((fre_profile_t)p, &kf,
                        times_ms[t] * 16U, frame, FRE_FRAME_PIXEL_COUNT);
                    h = fre_fnv1a(h, (const unsigned char *)frame,
                        sizeof frame);
                }
                printf("%s expr%d %08x\n",
                    fre_profile_slug((fre_profile_t)p), expr, h);
            }
        }
        return 0;
    }
    if (strcmp(argv[1], "bench") == 0) {
        fre_keyframe_t kf = fre_neutral_keyframe(0);
        for (size_t p = 0; p < fre_profile_count(); ++p) {
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            const int n = 300;
            for (int i = 0; i < n; ++i) {
                fre_render_frame((fre_profile_t)p, &kf,
                    (uint32_t)i * 533U, frame, FRE_FRAME_PIXEL_COUNT);
            }
            clock_gettime(CLOCK_MONOTONIC, &b);
            double us = ((double)(b.tv_sec - a.tv_sec) * 1e9 +
                (double)(b.tv_nsec - a.tv_nsec)) / 1e3 / n;
            printf("%-24s %8.1f us/frame\n",
                fre_profile_slug((fre_profile_t)p), us);
        }
        return 0;
    }
    fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 1;
}
