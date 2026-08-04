#include <stdio.h>
#include <string.h>

#include "cyber_face.h"

/*
 * Writes 24-bit BMP previews for every profile at a handful of
 * expressive moments, plus an index.html contact sheet, into a
 * directory given as argv[1] (default "preview"). Development aid
 * only; never part of the renderer build.
 */

typedef struct {
    const char *label;
    cyber_keyframe_t keyframe;
    uint32_t sample_clock;
} preview_case_t;

static const preview_case_t cases[] = {
    { "idle",
      { 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0 },
      2u * 16000u + 5000u },
    { "blink",
      { 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0,
        CYBER_KEYFRAME_FLAG_BLINKING },
      5u * 16000u },
    { "speak-aa",
      { 210, 190, 30, 0, 0, 255, 255, 0, 0, 10, 0,
        CYBER_KEYFRAME_FLAG_SPEAKING },
      3u * 16000u + 2100u },
    { "speak-oo",
      { 170, 60, 240, 0, 0, 255, 255, 0, 0, 0, 0,
        CYBER_KEYFRAME_FLAG_SPEAKING },
      9u * 16000u + 400u },
    { "happy",
      { 40, 140, 0, 0, 0, 255, 255, 25, -15, 60, 1, 0 },
      13u * 16000u + 900u },
    { "angry-glitch",
      { 90, 200, 0, 40, 130, 255, 255, -40, 10, -110, 3,
        CYBER_KEYFRAME_FLAG_SPEAKING },
      /* Chosen to land inside a glitch window (bucket 2, ~6.1 s). */
      98u * 16000u / 16u },
};

enum { CASE_COUNT = (int)(sizeof cases / sizeof cases[0]) };

static cyber_face_ctx_t ctx;
static uint16_t frame[CYBER_FACE_PIXEL_COUNT];

static int write_bmp(const char *path, const uint16_t *pixels)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    int width = CYBER_FACE_WIDTH;
    int height = CYBER_FACE_HEIGHT;
    int row_bytes = (width * 3 + 3) & ~3;
    int data_bytes = row_bytes * height;
    unsigned char header[54];
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    unsigned file_size = 54u + (unsigned)data_bytes;
    header[2] = (unsigned char)(file_size);
    header[3] = (unsigned char)(file_size >> 8);
    header[4] = (unsigned char)(file_size >> 16);
    header[5] = (unsigned char)(file_size >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (unsigned char)(width);
    header[19] = (unsigned char)(width >> 8);
    header[22] = (unsigned char)(height);
    header[23] = (unsigned char)(height >> 8);
    header[26] = 1;
    header[28] = 24;
    header[34] = (unsigned char)(data_bytes);
    header[35] = (unsigned char)(data_bytes >> 8);
    header[36] = (unsigned char)(data_bytes >> 16);
    fwrite(header, 1, sizeof header, fp);
    unsigned char row[CYBER_FACE_WIDTH * 3 + 4];
    for (int y = height - 1; y >= 0; --y) {
        memset(row, 0, sizeof row);
        for (int x = 0; x < width; ++x) {
            uint16_t c = pixels[y * width + x];
            unsigned r5 = (c >> 11) & 31u;
            unsigned g6 = (c >> 5) & 63u;
            unsigned b5 = c & 31u;
            /* Standard RGB565 expansion with bit replication. */
            row[x * 3 + 0] = (unsigned char)((b5 << 3) | (b5 >> 2));
            row[x * 3 + 1] = (unsigned char)((g6 << 2) | (g6 >> 4));
            row[x * 3 + 2] = (unsigned char)((r5 << 3) | (r5 >> 2));
        }
        fwrite(row, 1, (size_t)row_bytes, fp);
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "preview";
    cyber_face_init(&ctx);

    char path[512];
    snprintf(path, sizeof path, "%s/index.html", dir);
    FILE *html = fopen(path, "w");
    if (html == NULL) {
        fprintf(stderr, "cannot open %s (create the directory first)\n",
                path);
        return 1;
    }
    fprintf(html,
            "<!doctype html><meta charset=\"utf-8\">"
            "<title>fable_cyber_shaders previews</title>"
            "<style>body{background:#0b0d12;color:#dfe7ef;"
            "font:14px system-ui}table{border-collapse:collapse}"
            "td,th{padding:4px 6px;text-align:center}"
            "img{width:240px;height:180px;image-rendering:pixelated;"
            "background:#000;border:1px solid #223}</style>"
            "<h1>fable_cyber_shaders \xe2\x80\x94 11 profiles \xc3\x97 "
            "%d moments</h1><table>\n",
            CASE_COUNT);
    fprintf(html, "<tr><th></th>");
    for (int c = 0; c < CASE_COUNT; ++c) {
        fprintf(html, "<th>%s</th>", cases[c].label);
    }
    fprintf(html, "</tr>\n");

    for (int p = 0; p < (int)cyber_face_profile_count(); ++p) {
        const char *slug = cyber_face_profile_slug((cyber_profile_t)p);
        fprintf(html, "<tr><th>%s</th>", slug);
        for (int c = 0; c < CASE_COUNT; ++c) {
            if (!cyber_face_render(&ctx, (cyber_profile_t)p,
                                   &cases[c].keyframe,
                                   cases[c].sample_clock, frame,
                                   CYBER_FACE_PIXEL_COUNT)) {
                fprintf(stderr, "render failed %s/%s\n", slug,
                        cases[c].label);
                fclose(html);
                return 1;
            }
            snprintf(path, sizeof path, "%s/%s-%s.bmp", dir, slug,
                     cases[c].label);
            if (write_bmp(path, frame) != 0) {
                fprintf(stderr, "cannot write %s\n", path);
                fclose(html);
                return 1;
            }
            fprintf(html, "<td><img src=\"%s-%s.bmp\" alt=\"%s %s\">"
                          "</td>",
                    slug, cases[c].label, slug, cases[c].label);
        }
        fprintf(html, "</tr>\n");
    }
    fprintf(html, "</table>\n");
    fclose(html);
    printf("previews written to %s/\n", dir);
    return 0;
}
