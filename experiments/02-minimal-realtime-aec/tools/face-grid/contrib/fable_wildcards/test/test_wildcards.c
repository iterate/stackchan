#include <stdio.h>
#include <string.h>

#include "wildcard_face.h"

#ifdef WC_BENCH
#include <time.h>
#endif

/*
 * Native and WebAssembly test suite. Every check is deterministic; the
 * golden CRCs below were frozen from the native arm64 build and must match
 * on x86, on the ESP32-S3 and under emscripten (integer-only renderers make
 * this a hard byte-identity requirement, verified by `make wasm-check`).
 */

enum { GUARD = 64 };
static uint16_t arena[GUARD + WC_FACE_PIXEL_COUNT + GUARD];
static uint16_t *const fb = arena + GUARD;
static uint16_t scratch[WC_FACE_PIXEL_COUNT];

static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            ++failures;                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static uint32_t crc32_frame(const uint16_t *px) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < WC_FACE_PIXEL_COUNT; ++i) {
        crc ^= px[i];
        for (int b = 0; b < 16; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void fill_guards(void) {
    for (int i = 0; i < GUARD; ++i) {
        arena[i] = 0xA5A5u;
        arena[GUARD + WC_FACE_PIXEL_COUNT + i] = 0xA5A5u;
    }
}

static void check_guards(const char *what) {
    for (int i = 0; i < GUARD; ++i) {
        CHECK(arena[i] == 0xA5A5u, "%s wrote before the framebuffer", what);
        CHECK(arena[GUARD + WC_FACE_PIXEL_COUNT + i] == 0xA5A5u,
              "%s wrote past the framebuffer", what);
        if (arena[i] != 0xA5A5u ||
            arena[GUARD + WC_FACE_PIXEL_COUNT + i] != 0xA5A5u) {
            return;
        }
    }
}

static wc_keyframe_t kf_neutral(void) {
    wc_keyframe_t kf;
    memset(&kf, 0, sizeof kf);
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.mouth_width = 128;
    return kf;
}

static wc_keyframe_t kf_speaking(void) {
    wc_keyframe_t kf = kf_neutral();
    kf.mouth_open = 180;
    kf.mouth_width = 200;
    kf.mouth_round = 40;
    kf.mouth_teeth = 150;
    kf.look_x = 30;
    kf.look_y = -12;
    kf.brow = 25;
    kf.flags = WC_KEYFRAME_FLAG_SPEAKING;
    return kf;
}

static wc_keyframe_t kf_extreme(void) {
    wc_keyframe_t kf;
    memset(&kf, 0xFF, sizeof kf);
    kf.look_x = -128;
    kf.look_y = 127;
    kf.brow = -128;
    return kf;
}

/* Frozen from the native build; regenerate with -DWC_PRINT_GOLDENS. */
typedef struct {
    uint32_t clock;
    uint32_t crc[WC_PROFILE_COUNT];
} wc_golden_t;

static const wc_golden_t GOLDENS[3] = {
    { 0u, { 0xC31B2839u, 0x20C8AB28u, 0x18E4A0CAu, 0x8E993EAEu, 0x6C0ED0DDu,
            0x6E08F72Du, 0x3C7D35F1u } },
    { 48000u, { 0x8518F1DAu, 0xAA19809Bu, 0xF6552783u, 0x1583E150u,
                0xA1D1808Cu, 0x4440737Fu, 0xE8872019u } },
    { 1234567u, { 0x1287343Eu, 0xBCA63676u, 0x2C5292A4u, 0xC921C493u,
                  0x76F8BA5Bu, 0x5282BDAEu, 0x75754C82u } },
};

static void test_contract(void) {
    wc_keyframe_t kf = kf_neutral();
    CHECK(!wc_render_frame(WC_PROFILE_COUNT, &kf, 0, fb, WC_FACE_PIXEL_COUNT),
          "bad profile must fail");
    CHECK(!wc_render_frame(WC_PROFILE_SCOPE_BEAM, NULL, 0, fb,
                           WC_FACE_PIXEL_COUNT),
          "NULL keyframe must fail");
    CHECK(!wc_render_frame(WC_PROFILE_SCOPE_BEAM, &kf, 0, NULL,
                           WC_FACE_PIXEL_COUNT),
          "NULL buffer must fail");
    CHECK(!wc_render_frame(WC_PROFILE_SCOPE_BEAM, &kf, 0, fb,
                           WC_FACE_PIXEL_COUNT - 1),
          "short buffer must fail");
    CHECK(wc_profile_count() == WC_PROFILE_COUNT, "profile count");
    for (int p = 0; p < WC_PROFILE_COUNT; ++p) {
        wc_render_info_t info;
        CHECK(wc_profile_info((wc_profile_t)p, &info), "info %d", p);
        CHECK(info.width == WC_FACE_WIDTH && info.height == WC_FACE_HEIGHT,
              "info dimensions %d", p);
        CHECK(wc_profile_slug((wc_profile_t)p)[0] != '\0', "slug %d", p);
        CHECK(wc_profile_name((wc_profile_t)p)[0] != '\0', "name %d", p);
    }
}

static void test_determinism_and_purity(void) {
    wc_keyframe_t speak = kf_speaking();
    wc_keyframe_t neutral = kf_neutral();
    for (int p = 0; p < WC_PROFILE_COUNT; ++p) {
        fill_guards();
        /* two different prefill patterns: identical output proves every
         * pixel is written, not inherited */
        for (size_t i = 0; i < WC_FACE_PIXEL_COUNT; ++i) {
            fb[i] = 0xDEADu;
        }
        CHECK(wc_render_frame((wc_profile_t)p, &speak, 77777u, fb,
                              WC_FACE_PIXEL_COUNT),
              "render %d", p);
        uint32_t first = crc32_frame(fb);

        /* interleave other renders to prove statelessness */
        wc_render_frame((wc_profile_t)((p + 3) % WC_PROFILE_COUNT), &neutral,
                        123u, scratch, WC_FACE_PIXEL_COUNT);
        wc_render_frame((wc_profile_t)p, &neutral, 999999u, scratch,
                        WC_FACE_PIXEL_COUNT);

        for (size_t i = 0; i < WC_FACE_PIXEL_COUNT; ++i) {
            fb[i] = 0x2152u;
        }
        CHECK(wc_render_frame((wc_profile_t)p, &speak, 77777u, fb,
                              WC_FACE_PIXEL_COUNT),
              "re-render %d", p);
        CHECK(crc32_frame(fb) == first,
              "%s not a pure function of (keyframe, clock)",
              wc_profile_slug((wc_profile_t)p));
        check_guards(wc_profile_slug((wc_profile_t)p));
    }
}

static void test_goldens(void) {
    const wc_keyframe_t kfs[3] = { kf_neutral(), kf_speaking(), kf_extreme() };
    int print = 0;
#ifdef WC_PRINT_GOLDENS
    print = 1;
#endif
    for (int g = 0; g < 3; ++g) {
        if (print) {
            printf("    { %uu, { ", GOLDENS[g].clock);
        }
        for (int p = 0; p < WC_PROFILE_COUNT; ++p) {
            fill_guards();
            CHECK(wc_render_frame((wc_profile_t)p, &kfs[g], GOLDENS[g].clock,
                                  fb, WC_FACE_PIXEL_COUNT),
                  "golden render %d/%d", g, p);
            uint32_t crc = crc32_frame(fb);
            if (print) {
                printf("0x%08Xu, ", crc);
            } else {
                CHECK(crc == GOLDENS[g].crc[p],
                      "golden mismatch %s case %d: got 0x%08X want 0x%08X",
                      wc_profile_slug((wc_profile_t)p), g, crc,
                      GOLDENS[g].crc[p]);
            }
            check_guards("golden");
        }
        if (print) {
            printf("} },\n");
        }
    }
}

static void test_fuzz(void) {
    uint32_t seed = 0x1234ABCDu;
    for (int it = 0; it < 120; ++it) {
        wc_keyframe_t kf;
        uint8_t *raw = (uint8_t *)&kf;
        for (size_t i = 0; i < sizeof kf; ++i) {
            seed = seed * 1664525u + 1013904223u;
            raw[i] = (uint8_t)(seed >> 24);
        }
        seed = seed * 1664525u + 1013904223u;
        uint32_t clock = seed;
        wc_profile_t p = (wc_profile_t)(it % WC_PROFILE_COUNT);
        fill_guards();
        CHECK(wc_render_frame(p, &kf, clock, fb, WC_FACE_PIXEL_COUNT),
              "fuzz render %d", it);
        uint32_t first = crc32_frame(fb);
        CHECK(wc_render_frame(p, &kf, clock, fb, WC_FACE_PIXEL_COUNT),
              "fuzz re-render %d", it);
        CHECK(crc32_frame(fb) == first, "fuzz determinism %d", it);
        check_guards("fuzz");
    }
}

#ifdef WC_BENCH
static void bench(void) {
    wc_keyframe_t kf = kf_speaking();
    printf("host benchmark, 300 frames per profile\n");
    for (int p = 0; p < WC_PROFILE_COUNT; ++p) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int f = 0; f < 300; ++f) {
            kf.mouth_open = (uint8_t)(f * 7);
            wc_render_frame((wc_profile_t)p, &kf, (uint32_t)f * 533u, fb,
                            WC_FACE_PIXEL_COUNT);
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = ((double)(b.tv_sec - a.tv_sec) * 1e3 +
                     (double)(b.tv_nsec - a.tv_nsec) / 1e6) / 300.0;
        printf("  %-22s %7.3f ms/frame  (%6.0f fps host)\n",
               wc_profile_slug((wc_profile_t)p), ms, 1000.0 / ms);
    }
}
#endif

int main(void) {
    test_contract();
    test_determinism_and_purity();
    test_goldens();
    test_fuzz();
#ifdef WC_BENCH
    bench();
#endif
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all wildcard renderer tests passed\n");
    return 0;
}
