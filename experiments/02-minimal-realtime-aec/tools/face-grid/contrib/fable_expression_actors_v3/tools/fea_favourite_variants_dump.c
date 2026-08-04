/*
 * Reuse the original FEA dump/contact-sheet machinery through the isolated
 * favourite-variant API.  The runner renames its generic v4 files to an
 * unambiguous artist-pass prefix after generation.
 */

#include "fea_favourite_variants.h"

#define fea_profile_t fea_favourite_profile_t
#define FEA_PROFILE_COUNT FEA_FAVOURITE_PROFILE_COUNT
#define fea_profile_slug fea_favourite_profile_slug
#define fea_profile_name fea_favourite_profile_name
#define fea_probe fea_favourite_probe
#define fea_render_frame fea_favourite_render_frame
#define main fea_favourite_dump_base_main
#include "fea_dump.c"
#undef main

enum {
    FV_BLINK_TOTAL_SAMPLES = 6208,
    FV_BLINK_CELLS = 16,
};

static void favourite_render_blink_frame(
    fea_favourite_profile_t profile, int cell, uint16_t *pixels)
{
    const uint32_t clock =
        (uint32_t)cell * FV_BLINK_TOTAL_SAMPLES /
        (FV_BLINK_CELLS - 1U);
    face_render_key_t key = base_key();
    key.controls.flags |= FACE_KEYFRAME_FLAG_BLINKING;
    key.controls.mouth_open =
        (uint8_t)(94U + (uint32_t)cell * 92U /
                           (FV_BLINK_CELLS - 1U));
    key.audio_level =
        (uint8_t)(110U + (uint32_t)cell * 70U /
                            (FV_BLINK_CELLS - 1U));
    if (!fea_favourite_render_frame(
            profile, &key, clock, pixels, FEA_PIXEL_COUNT)) {
        fprintf(stderr, "blink render failed\n");
        exit(1);
    }
}

static void favourite_cmd_blink_sheets(const char *dir)
{
    char path[1024];
    for (unsigned profile = 0;
         profile < FEA_FAVOURITE_PROFILE_COUNT; ++profile) {
        for (int exact40 = 0; exact40 < 2; ++exact40) {
            const int cell_w =
                exact40 != 0 ? 160 : FEA_FRAME_WIDTH;
            const int cell_h =
                exact40 != 0 ? 120 : FEA_FRAME_HEIGHT;
            const int columns = 4;
            const int rows = 4;
            const int width = columns * (cell_w + 2) + 2;
            const int height =
                rows * (cell_h + LABEL_H + 2) + 2;
            sheet_t sheet = sheet_new(width, height);
            for (int cell = 0; cell < FV_BLINK_CELLS; ++cell) {
                const int column = cell % columns;
                const int row = cell / columns;
                const int x0 = 2 + column * (cell_w + 2);
                const int y0 =
                    2 + row * (cell_h + LABEL_H + 2);
                const uint32_t clock =
                    (uint32_t)cell * FV_BLINK_TOTAL_SAMPLES /
                    (FV_BLINK_CELLS - 1U);
                const char *phase =
                    clock < 1504U ? "CLOSE"
                    : clock < 2304U ? "HOLD" : "OPEN";
                char label[32];
                snprintf(
                    label, sizeof(label), "B%02d %s", cell, phase);
                sheet_text(&sheet, x0 + 2, y0 + 1, label);
                favourite_render_blink_frame(
                    (fea_favourite_profile_t)profile, cell,
                    frame_buffer);
                if (exact40 != 0) {
                    sheet_blit_nn40(
                        &sheet, x0, y0 + LABEL_H, frame_buffer, 4);
                } else {
                    sheet_blit(
                        &sheet, x0, y0 + LABEL_H, frame_buffer);
                }
            }
            snprintf(
                path, sizeof(path),
                exact40 != 0
                    ? "%s/favourite-artist-pass__temporal-blink__%s__16f__40x30-nn__x4.ppm"
                    : "%s/favourite-artist-pass__temporal-blink__%s__16f__native.ppm",
                dir,
                fea_favourite_profile_slug(
                    (fea_favourite_profile_t)profile) + 4);
            sheet_write(&sheet, path);
            free(sheet.rgb);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "sheets") == 0) {
        cmd_sheets(argv[2]);
        favourite_cmd_blink_sheets(argv[2]);
        return 0;
    }
    return fea_favourite_dump_base_main(argc, argv);
}
