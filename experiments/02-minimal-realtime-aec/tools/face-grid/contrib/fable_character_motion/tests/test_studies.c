#include "../src/fable_studies.h"

#include <stdlib.h>

#include "test_support.h"

enum { MS = FABLE_SAMPLES_PER_MS };

static uint16_t frame_a[FABLE_STUDY_PIXELS];
static uint16_t frame_b[FABLE_STUDY_PIXELS];

static fable_keyframe_t speaking_kf(void)
{
    fable_keyframe_t kf;
    memset(&kf, 0, sizeof(kf));
    kf.mouth_open = 150;
    kf.mouth_width = 180;
    kf.mouth_round = 90;
    kf.mouth_teeth = 140;
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.look_x = 30;
    kf.expression = FABLE_ACTIVITY_SPEAKING;
    kf.flags = FABLE_KEYFRAME_FLAG_SPEAKING;
    return kf;
}

static void test_contract(void)
{
    const fable_keyframe_t kf = speaking_kf();
    CHECK(!fable_study_render(FABLE_STUDY_CURIOUS_SCOUT, &kf, 0, NULL,
                              FABLE_STUDY_PIXELS),
          "NULL buffer must fail");
    CHECK(!fable_study_render(FABLE_STUDY_CURIOUS_SCOUT, &kf, 0, frame_a,
                              FABLE_STUDY_PIXELS - 1),
          "short buffer must fail");
    CHECK(!fable_study_render(FABLE_STUDY_COUNT, &kf, 0, frame_a,
                              FABLE_STUDY_PIXELS),
          "invalid study must fail");
    CHECK(fable_study_render(FABLE_STUDY_CURIOUS_SCOUT, NULL, 0, frame_a,
                             FABLE_STUDY_PIXELS),
          "NULL keyframe must render idle");
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        CHECK(fable_study_slug((fable_study_t)s) != NULL, "slug %u", s);
        CHECK(fable_study_name((fable_study_t)s) != NULL, "name %u", s);
        CHECK(fable_study_persona((fable_study_t)s) != NULL,
              "persona %u", s);
    }
}

static void test_determinism_and_coverage(void)
{
    const fable_keyframe_t kf = speaking_kf();
    const uint32_t clocks[] = { 0U, 16000U, 913337U, 16000U * 613U };
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        for (size_t ci = 0; ci < sizeof(clocks) / sizeof(clocks[0]);
             ci++) {
            /* Different canaries: identical output proves every pixel
               is written and nothing depends on buffer history. */
            for (size_t i = 0; i < (size_t)FABLE_STUDY_PIXELS; i++) {
                frame_a[i] = 0xdead;
                frame_b[i] = 0x1234;
            }
            CHECK(fable_study_render((fable_study_t)s, &kf, clocks[ci],
                                     frame_a, FABLE_STUDY_PIXELS),
                  "render a");
            CHECK(fable_study_render((fable_study_t)s, &kf, clocks[ci],
                                     frame_b, FABLE_STUDY_PIXELS),
                  "render b");
            CHECK(memcmp(frame_a, frame_b, sizeof(frame_a)) == 0,
                  "study %u clock %u depends on buffer history", s,
                  clocks[ci]);
            size_t canaries = 0;
            for (size_t i = 0; i < (size_t)FABLE_STUDY_PIXELS; i++) {
                if (frame_a[i] == 0xdead) {
                    canaries++;
                }
            }
            /* 0xdead is a legal color, but a fully painted frame can
               only contain it where the palette really produces it;
               the gradient backgrounds never do. */
            CHECK(canaries == 0,
                  "study %u clock %u left %zu unwritten pixels", s,
                  clocks[ci], canaries);
        }
    }
}

static void test_liveliness(void)
{
    /* A face must never freeze: over 20 s of silent idle no stretch of
       a second may render identical frames. */
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        uint32_t run = 0;
        uint32_t max_run = 0;
        uint32_t distinct = 0;
        fable_study_render((fable_study_t)s, NULL, 0, frame_a,
                           FABLE_STUDY_PIXELS);
        for (uint32_t f = 1; f < 600U; f++) {
            const uint32_t clock = f * 533U; /* ~30 fps on 16 kHz */
            fable_study_render((fable_study_t)s, NULL, clock, frame_b,
                               FABLE_STUDY_PIXELS);
            if (memcmp(frame_a, frame_b, sizeof(frame_a)) == 0) {
                run++;
                if (run > max_run) {
                    max_run = run;
                }
            } else {
                run = 0;
                distinct++;
            }
            memcpy(frame_a, frame_b, sizeof(frame_a));
        }
        CHECK(max_run < 30U,
              "study %s froze for %u frames",
              fable_study_slug((fable_study_t)s), max_run);
        /* Alive means never frozen (bound above) plus steady texture:
           2-3 changes/s reads alive even in 2-frame retro idles, so
           demand at least ~6 visible changes per second here. */
        CHECK(distinct > 120U, "study %s barely moves: %u changes",
              fable_study_slug((fable_study_t)s), distinct);
    }
}

static void test_studies_differ(void)
{
    const fable_keyframe_t kf = speaking_kf();
    uint32_t crcs[FABLE_STUDY_COUNT];
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        fable_study_render((fable_study_t)s, &kf, 123456U, frame_a,
                           FABLE_STUDY_PIXELS);
        crcs[s] = fable_crc32(0, frame_a, sizeof(frame_a));
        for (uint32_t o = 0; o < s; o++) {
            CHECK(crcs[o] != crcs[s], "studies %u and %u identical", o,
                  s);
        }
    }
}

/*
 * Golden frames: CRC-32 of the raw little-endian RGB565 buffer. These
 * pin byte-identical output across host, firmware, and wasm builds (all
 * little-endian, arithmetic-shift platforms). Regenerate with
 * FABLE_GOLDEN_UPDATE=1 after an intentional visual change.
 */
typedef struct {
    uint32_t clock;
    int with_kf;
} golden_point_t;

static const golden_point_t GOLDEN_POINTS[] = {
    { 0U, 0 },
    { 8000U, 0 },
    { 16000U * 7U + 123U, 1 },
    { 16000U * 61U, 1 },
    { 16000U * 601U + 7777U, 0 },
};

enum {
    GOLDEN_POINT_COUNT =
        (int)(sizeof(GOLDEN_POINTS) / sizeof(GOLDEN_POINTS[0])),
};

static const uint32_t GOLDEN_CRCS[FABLE_STUDY_COUNT]
                                 [GOLDEN_POINT_COUNT] = {
    { 0xae6bf744U, 0xca36ea6fU, 0x19c2da82U, 0x7e83ef38U,
      0xa4d9b0acU },
    { 0xa75dc4f2U, 0x4894f7e4U, 0x954bc680U, 0x6866f998U,
      0x94b2361aU },
    { 0x9b01a476U, 0xf388052cU, 0x753f6fd8U, 0xe4652211U,
      0x1bfccf11U },
    { 0xfac1f840U, 0xda237c13U, 0x1a338826U, 0x97028c79U,
      0x519d4ef7U },
    { 0x2757662eU, 0xbd0ab31fU, 0x527bb9c0U, 0xe3fca126U,
      0x3fd18459U },
};

static void test_golden(void)
{
    const fable_keyframe_t kf = speaking_kf();
    const int update = getenv("FABLE_GOLDEN_UPDATE") != NULL;

    if (update) {
        printf("static const uint32_t GOLDEN_CRCS[FABLE_STUDY_COUNT]\n"
               "                                 [GOLDEN_POINT_COUNT]"
               " = {\n");
    }
    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        if (update) {
            printf("    {");
        }
        for (int gi = 0; gi < GOLDEN_POINT_COUNT; gi++) {
            const golden_point_t *pt = &GOLDEN_POINTS[gi];
            fable_study_render((fable_study_t)s,
                               pt->with_kf ? &kf : NULL, pt->clock,
                               frame_a, FABLE_STUDY_PIXELS);
            const uint32_t crc =
                fable_crc32(0, frame_a, sizeof(frame_a));
            if (update) {
                printf(" 0x%08xU,", crc);
            } else {
                CHECK(crc == GOLDEN_CRCS[s][gi],
                      "golden mismatch study %u point %d: 0x%08x want "
                      "0x%08x",
                      s, gi, crc, GOLDEN_CRCS[s][gi]);
            }
        }
        if (update) {
            printf(" },\n");
        }
    }
    if (update) {
        printf("};\n");
    }
}

int main(void)
{
    if (!fable_shift_is_arithmetic()) {
        printf("FAIL: platform lacks arithmetic right shift\n");
        return 1;
    }
    test_contract();
    test_determinism_and_coverage();
    test_liveliness();
    test_studies_differ();
    test_golden();
    return fable_test_finish("test_studies");
}
