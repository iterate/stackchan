#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pixel_face.h"
#include "png_write.h"

/*
 * Review-image generator. For each profile it renders a pose sheet (eight
 * canonical keyframes) and an idle time-lapse sheet (eight clock stops with
 * a neutral keyframe), each tile scaled 2x nearest for easy inspection.
 */

enum { TILE_W = PIXEL_FACE_WIDTH * 2, TILE_H = PIXEL_FACE_HEIGHT * 2 };

typedef struct {
    const char *label;
    face_keyframe_t k;
    uint32_t clock;
} pose_t;

#define SPEAK FACE_KEYFRAME_FLAG_SPEAKING

static const pose_t poses[] = {
    { "idle", { 0, 128, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0 }, 5600 },
    { "aa", { 230, 150, 30, 0, 40, 255, 255, 0, 0, 10, 0, SPEAK }, 21000 },
    { "oo", { 120, 90, 230, 0, 0, 255, 255, 0, 0, 0, 0, SPEAK }, 37000 },
    { "ee", { 80, 235, 20, 0, 200, 255, 255, 0, 0, 20, 0, SPEAK }, 52000 },
    { "mbp", { 20, 128, 40, 235, 0, 255, 255, 0, 0, -10, 0, SPEAK }, 69000 },
    { "gaze", { 0, 128, 0, 0, 0, 255, 255, -110, 30, 0, 0, 0 }, 83000 },
    { "brow", { 40, 128, 0, 0, 0, 255, 255, 60, -40, 110, 0, SPEAK },
      99000 },
    { "blink", { 0, 128, 0, 0, 0, 255, 255, 0, 0, 0, 0,
                 FACE_KEYFRAME_FLAG_BLINKING }, 113000 },
};

enum { POSE_COUNT = (int)(sizeof(poses) / sizeof(poses[0])) };

static void rgb565_to_tile(const uint16_t *frame, uint8_t *sheet,
                           int sheet_w, int tile_col, int tile_row) {
    for (int y = 0; y < TILE_H; ++y) {
        for (int x = 0; x < TILE_W; ++x) {
            uint16_t c =
                frame[(y / 2) * PIXEL_FACE_WIDTH + (x / 2)];
            uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t)((c & 0x1F) << 3);
            r |= r >> 5;
            g |= g >> 6;
            b |= b >> 5;
            size_t off =
                ((size_t)(tile_row * TILE_H + y) * (size_t)sheet_w +
                 (size_t)(tile_col * TILE_W + x)) * 3;
            sheet[off] = r;
            sheet[off + 1] = g;
            sheet[off + 2] = b;
        }
    }
}

int main(int argc, char **argv) {
    const char *out_dir = argc > 1 ? argv[1] : "out";
    static uint16_t frame[PIXEL_FACE_PIXEL_COUNT];

    int cols = 4, rows = 2;
    int sheet_w = cols * TILE_W;
    int sheet_h = rows * TILE_H;
    uint8_t *sheet = malloc((size_t)sheet_w * (size_t)sheet_h * 3);
    if (!sheet) {
        return 1;
    }

    char path[512];
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        const char *slug = pixel_face_profile_slug((pixel_face_profile_t)p);

        /* Pose sheet. */
        for (int i = 0; i < POSE_COUNT; ++i) {
            if (!pixel_face_render((pixel_face_profile_t)p, &poses[i].k,
                                   poses[i].clock, frame,
                                   PIXEL_FACE_PIXEL_COUNT)) {
                fprintf(stderr, "render failed: %s %s\n", slug,
                        poses[i].label);
                return 1;
            }
            rgb565_to_tile(frame, sheet, sheet_w, i % cols, i / cols);
        }
        snprintf(path, sizeof(path), "%s/%s_poses.png", out_dir, slug);
        if (png_write_rgb(path, sheet, sheet_w, sheet_h) != 0) {
            fprintf(stderr, "png write failed: %s\n", path);
            return 1;
        }

        /* Idle time-lapse sheet: neutral keyframe, 0.45 s steps. */
        face_keyframe_t idle = { 0, 128, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0 };
        for (int i = 0; i < POSE_COUNT; ++i) {
            uint32_t clock = (uint32_t)i * 7200U; /* 0.45 s per step */
            if (!pixel_face_render((pixel_face_profile_t)p, &idle, clock,
                                   frame, PIXEL_FACE_PIXEL_COUNT)) {
                return 1;
            }
            rgb565_to_tile(frame, sheet, sheet_w, i % cols, i / cols);
        }
        snprintf(path, sizeof(path), "%s/%s_idle.png", out_dir, slug);
        if (png_write_rgb(path, sheet, sheet_w, sheet_h) != 0) {
            return 1;
        }
        printf("%s: wrote pose + idle sheets\n", slug);
    }

    /* Overview: every profile at the speaking AA pose. */
    {
        int ocols = 4;
        int orows =
            ((int)pixel_face_profile_count() + ocols - 1) / ocols;
        int ow = ocols * TILE_W;
        int oh = orows * TILE_H;
        uint8_t *osheet = calloc((size_t)ow * (size_t)oh, 3);
        if (!osheet) {
            free(sheet);
            return 1;
        }
        for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
            pixel_face_render((pixel_face_profile_t)p, &poses[1].k,
                              poses[1].clock, frame,
                              PIXEL_FACE_PIXEL_COUNT);
            for (int y = 0; y < TILE_H; ++y) {
                for (int x = 0; x < TILE_W; ++x) {
                    uint16_t c = frame[(y / 2) * PIXEL_FACE_WIDTH + x / 2];
                    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
                    uint8_t b = (uint8_t)((c & 0x1F) << 3);
                    r |= r >> 5;
                    g |= g >> 6;
                    b |= b >> 5;
                    size_t off =
                        (((size_t)(p / (size_t)ocols) * TILE_H + (size_t)y) *
                             (size_t)ow +
                         ((size_t)(p % (size_t)ocols) * TILE_W + (size_t)x)) *
                        3;
                    osheet[off] = r;
                    osheet[off + 1] = g;
                    osheet[off + 2] = b;
                }
            }
        }
        snprintf(path, sizeof(path), "%s/overview.png", out_dir);
        png_write_rgb(path, osheet, ow, oh);
        free(osheet);
    }

    free(sheet);
    return 0;
}
