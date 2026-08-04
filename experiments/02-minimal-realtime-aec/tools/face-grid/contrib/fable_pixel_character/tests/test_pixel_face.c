#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pixel_face.h"

/*
 * Host test-suite for the pixel-character renderer family.
 *
 *   ./test_pixel_face                 run all checks against golden CRCs
 *   ./test_pixel_face --print-crcs    print CRC table (for WASM parity diff)
 *   ./test_pixel_face --write-golden <path>   regenerate golden_crcs.inc
 */

#define SPEAK FACE_KEYFRAME_FLAG_SPEAKING

typedef struct {
    const char *label;
    face_keyframe_t k;
    uint32_t clock;
} pose_t;

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

static const uint32_t golden_crcs[][POSE_COUNT] = {
#include "golden_crcs.inc"
};

enum {
    GOLDEN_ROWS =
        (int)(sizeof(golden_crcs) / sizeof(golden_crcs[0])),
};

static int failures = 0;

static void check(int ok, const char *what) {
    if (!ok) {
        ++failures;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static uint32_t crc32_buf(const void *data, size_t len) {
    static uint32_t table[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int b = 0; b < 8; ++b) {
                c = (c & 1U) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = 1;
    }
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ p[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

static uint16_t frame_a[PIXEL_FACE_PIXEL_COUNT + 8];
static uint16_t frame_b[PIXEL_FACE_PIXEL_COUNT + 8];

static uint32_t render_crc(pixel_face_profile_t p, const pose_t *pose) {
    if (!pixel_face_render(p, &pose->k, pose->clock, frame_a,
                           PIXEL_FACE_PIXEL_COUNT)) {
        return 0;
    }
    /* CRC over the little-endian byte view: matches the wire/WASM bytes. */
    uint8_t bytes[PIXEL_FACE_FRAME_BYTES];
    for (int i = 0; i < PIXEL_FACE_PIXEL_COUNT; ++i) {
        bytes[i * 2] = (uint8_t)(frame_a[i] & 0xFFU);
        bytes[i * 2 + 1] = (uint8_t)(frame_a[i] >> 8);
    }
    return crc32_buf(bytes, sizeof(bytes));
}

static void test_abi(void) {
    check(sizeof(face_keyframe_t) == FACE_KEYFRAME_BYTES,
          "face_keyframe_t must stay 12 bytes");
    check(pixel_face_profile_count() == PIXEL_FACE_PROFILE_COUNT,
          "profile count");
}

static void test_error_paths(void) {
    const face_keyframe_t k = poses[0].k;
    check(!pixel_face_render(PIXEL_FACE_PROFILE_COUNT, &k, 0, frame_a,
                             PIXEL_FACE_PIXEL_COUNT),
          "unknown profile rejected");
    check(!pixel_face_render(PIXEL_FACE_EGA_QUEST, 0, 0, frame_a,
                             PIXEL_FACE_PIXEL_COUNT),
          "NULL keyframe rejected");
    check(!pixel_face_render(PIXEL_FACE_EGA_QUEST, &k, 0, 0,
                             PIXEL_FACE_PIXEL_COUNT),
          "NULL buffer rejected");
    check(!pixel_face_render(PIXEL_FACE_EGA_QUEST, &k, 0, frame_a,
                             PIXEL_FACE_PIXEL_COUNT - 1),
          "short buffer rejected");
    check(pixel_face_profile_slug(PIXEL_FACE_PROFILE_COUNT) == 0,
          "slug out of range");
    check(pixel_face_profile_name((pixel_face_profile_t)-1) == 0,
          "name out of range");
}

static void test_metadata(void) {
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        pixel_face_profile_t prof = (pixel_face_profile_t)p;
        check(pixel_face_profile_slug(prof) != 0, "slug present");
        check(pixel_face_profile_name(prof) != 0, "name present");
        pixel_face_style_t style;
        check(pixel_face_profile_style(prof, &style), "style present");
        check(style.art_width * style.art_scale_x == PIXEL_FACE_WIDTH,
              "art width times scale covers the frame");
        check(style.art_height * style.art_scale_y == PIXEL_FACE_HEIGHT,
              "art height times scale covers the frame");
        check(style.palette_strategy != 0 && style.dither_strategy != 0,
              "strategy strings present");
    }
}

static void test_determinism_and_bounds(void) {
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        for (int i = 0; i < POSE_COUNT; ++i) {
            memset(frame_a, 0xA5, sizeof(frame_a));
            memset(frame_b, 0x5A, sizeof(frame_b));
            check(pixel_face_render((pixel_face_profile_t)p, &poses[i].k,
                                    poses[i].clock, frame_a,
                                    PIXEL_FACE_PIXEL_COUNT),
                  "render succeeds");
            check(pixel_face_render((pixel_face_profile_t)p, &poses[i].k,
                                    poses[i].clock, frame_b,
                                    PIXEL_FACE_PIXEL_COUNT),
                  "second render succeeds");
            check(memcmp(frame_a, frame_b,
                         PIXEL_FACE_FRAME_BYTES) == 0,
                  "same inputs give identical frames");
            for (int g = 0; g < 8; ++g) {
                check(frame_a[PIXEL_FACE_PIXEL_COUNT + g] == 0xA5A5,
                      "no write past pixel_capacity");
            }
        }
    }
}

static void test_full_coverage(void) {
    /* A pixel is only "unwritten" if it survives two different canaries. */
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        memset(frame_a, 0xAD, PIXEL_FACE_FRAME_BYTES);
        memset(frame_b, 0x2B, PIXEL_FACE_FRAME_BYTES);
        pixel_face_render((pixel_face_profile_t)p, &poses[1].k,
                          poses[1].clock, frame_a, PIXEL_FACE_PIXEL_COUNT);
        pixel_face_render((pixel_face_profile_t)p, &poses[1].k,
                          poses[1].clock, frame_b, PIXEL_FACE_PIXEL_COUNT);
        int unwritten = 0;
        for (int i = 0; i < PIXEL_FACE_PIXEL_COUNT; ++i) {
            if (frame_a[i] == 0xADAD && frame_b[i] == 0x2B2B) {
                ++unwritten;
            }
        }
        check(unwritten == 0, "every pixel written");
    }
}

static void test_reactivity(void) {
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        pixel_face_profile_t prof = (pixel_face_profile_t)p;
        face_keyframe_t a = poses[0].k;
        face_keyframe_t b = poses[0].k;

        a.flags = b.flags = SPEAK;
        a.mouth_open = 0;
        b.mouth_open = 255;
        uint32_t ca = 0, cb = 0;
        pose_t pa = { "m0", a, 40000 };
        pose_t pb = { "m1", b, 40000 };
        ca = render_crc(prof, &pa);
        cb = render_crc(prof, &pb);
        check(ca != cb, "mouth_open changes the frame");

        a = b = poses[0].k;
        a.eye_left_open = a.eye_right_open = 255;
        b.eye_left_open = b.eye_right_open = 0;
        pa.k = a;
        pb.k = b;
        check(render_crc(prof, &pa) != render_crc(prof, &pb),
              "eye openness changes the frame");

        a = b = poses[0].k;
        a.look_x = -100;
        b.look_x = 100;
        pa.k = a;
        pb.k = b;
        check(render_crc(prof, &pa) != render_crc(prof, &pb),
              "gaze changes the frame");

        /* Idle acting: sample 6.4 s at 200 ms steps — every blink window
         * (~260 ms) must be hit — and expect at least three distinct
         * frames from blinks, saccades, breathing, or ambient motion. */
        uint32_t seen[32];
        int distinct = 0;
        for (int i = 0; i < 32; ++i) {
            pose_t t = { "t", poses[0].k, (uint32_t)i * 3200U };
            uint32_t crc = render_crc(prof, &t);
            int is_new = 1;
            for (int j = 0; j < distinct; ++j) {
                if (seen[j] == crc) {
                    is_new = 0;
                    break;
                }
            }
            if (is_new) {
                seen[distinct++] = crc;
            }
        }
        if (distinct < 3) {
            ++failures;
            fprintf(stderr,
                    "FAIL: %s: only %d distinct idle frames in 6.4 s\n",
                    pixel_face_profile_slug(prof), distinct);
        }
    }
}

static void test_golden(void) {
    if (GOLDEN_ROWS != (int)pixel_face_profile_count()) {
        ++failures;
        fprintf(stderr,
                "FAIL: golden table has %d rows for %d profiles "
                "(run --write-golden)\n",
                GOLDEN_ROWS, (int)pixel_face_profile_count());
        return;
    }
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        for (int i = 0; i < POSE_COUNT; ++i) {
            uint32_t crc = render_crc((pixel_face_profile_t)p, &poses[i]);
            if (crc != golden_crcs[p][i]) {
                ++failures;
                fprintf(stderr,
                        "FAIL: golden CRC %s/%s: got %08x want %08x\n",
                        pixel_face_profile_slug((pixel_face_profile_t)p),
                        poses[i].label, crc, golden_crcs[p][i]);
            }
        }
    }
}

static void print_crcs(void) {
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        for (int i = 0; i < POSE_COUNT; ++i) {
            printf("%s %s %08x\n",
                   pixel_face_profile_slug((pixel_face_profile_t)p),
                   poses[i].label,
                   render_crc((pixel_face_profile_t)p, &poses[i]));
        }
    }
}

static int write_golden(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fprintf(f, "/* Generated by test_pixel_face --write-golden. */\n");
    for (size_t p = 0; p < pixel_face_profile_count(); ++p) {
        fprintf(f, "{ /* %s */\n ",
                pixel_face_profile_slug((pixel_face_profile_t)p));
        for (int i = 0; i < POSE_COUNT; ++i) {
            fprintf(f, " 0x%08xU,",
                    render_crc((pixel_face_profile_t)p, &poses[i]));
        }
        fprintf(f, "\n},\n");
    }
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--print-crcs") == 0) {
        print_crcs();
        return 0;
    }
    if (argc > 2 && strcmp(argv[1], "--write-golden") == 0) {
        return write_golden(argv[2]);
    }

    test_abi();
    test_error_paths();
    test_metadata();
    test_determinism_and_bounds();
    test_full_coverage();
    test_reactivity();
    test_golden();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all pixel_face tests passed (%d profiles, %d poses)\n",
           (int)pixel_face_profile_count(), POSE_COUNT);
    return 0;
}
