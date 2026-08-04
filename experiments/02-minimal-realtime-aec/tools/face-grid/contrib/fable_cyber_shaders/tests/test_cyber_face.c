#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyber_face.h"

/*
 * Deterministic test suite. Runs natively and under WebAssembly (node);
 * `--dump-hashes` prints the golden table so the two builds can be
 * diffed byte for byte.
 */

static int failures;

#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

enum { GUARD_PIXELS = 64, GUARD_VALUE = 0xA5C3 };

typedef struct {
    uint16_t front_guard[GUARD_PIXELS];
    uint16_t frame[CYBER_FACE_PIXEL_COUNT];
    uint16_t back_guard[GUARD_PIXELS];
} guarded_frame_t;

static void reset_frame(guarded_frame_t *frame)
{
    for (int i = 0; i < GUARD_PIXELS; ++i) {
        frame->front_guard[i] = GUARD_VALUE;
        frame->back_guard[i] = GUARD_VALUE;
    }
    memset(frame->frame, 0, sizeof frame->frame);
}

static int guards_ok(const guarded_frame_t *frame)
{
    for (int i = 0; i < GUARD_PIXELS; ++i) {
        if (frame->front_guard[i] != GUARD_VALUE ||
            frame->back_guard[i] != GUARD_VALUE) {
            return 0;
        }
    }
    return 1;
}

static uint64_t fnv1a64_frame(const uint16_t *pixels)
{
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < CYBER_FACE_PIXEL_COUNT; ++i) {
        hash ^= (uint64_t)(pixels[i] & 0xFFu);
        hash *= 1099511628211ull;
        hash ^= (uint64_t)(pixels[i] >> 8);
        hash *= 1099511628211ull;
    }
    return hash;
}

/* ---- golden cases ------------------------------------------------- */

typedef struct {
    const char *label;
    cyber_keyframe_t keyframe;
    uint32_t sample_clock;
} golden_case_t;

static const golden_case_t golden_cases[] = {
    { "idle-start",
      { 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0 },
      0u },
    { "idle-7s",
      { 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0 },
      7u * 16000u + 123u },
    { "speaking-aa",
      { 200, 180, 40, 0, 60, 255, 255, 10, -6, 20, 0,
        CYBER_KEYFRAME_FLAG_SPEAKING },
      3u * 16000u + 7777u },
    { "blink-look-angry",
      { 30, 90, 0, 120, 0, 255, 255, -90, 60, -100, 3,
        CYBER_KEYFRAME_FLAG_BLINKING },
      11u * 16000u },
    { "surprised-round",
      { 240, 60, 230, 0, 0, 255, 255, 40, -40, 110, 4,
        CYBER_KEYFRAME_FLAG_SPEAKING },
      21u * 16000u + 4444u },
};

enum {
    GOLDEN_CASE_COUNT =
        (int)(sizeof golden_cases / sizeof golden_cases[0]),
};

typedef struct {
    uint8_t profile;
    uint8_t case_index;
    uint64_t hash;
} golden_entry_t;

#define CYBER_GOLDEN(profile, case_index, hash) \
    { (uint8_t)(profile), (uint8_t)(case_index), (hash) },
static const golden_entry_t golden_expected[] = {
#include "golden_hashes.inc"
};
#undef CYBER_GOLDEN

enum {
    GOLDEN_EXPECTED_COUNT =
        (int)(sizeof golden_expected / sizeof golden_expected[0]),
};

/* ---- tests --------------------------------------------------------- */

static cyber_face_ctx_t ctx_a;
static cyber_face_ctx_t ctx_b;
static guarded_frame_t frame_a;
static guarded_frame_t frame_b;

static void test_metadata(void)
{
    CHECK(cyber_face_profile_count() == CYBER_PROFILE_COUNT,
          "profile count");
    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        cyber_face_info_t info;
        const char *slug = cyber_face_profile_slug((cyber_profile_t)p);
        const char *name = cyber_face_profile_name((cyber_profile_t)p);
        CHECK(slug != NULL && slug[0] != '\0', "slug %d", p);
        CHECK(name != NULL && name[0] != '\0', "name %d", p);
        CHECK(cyber_face_profile_info((cyber_profile_t)p, &info),
              "info %d", p);
        CHECK(info.width == CYBER_FACE_WIDTH &&
                  info.height == CYBER_FACE_HEIGHT,
              "info dims %d", p);
        CHECK(info.framebuffer_bytes == CYBER_FACE_FRAME_BYTES,
              "info framebuffer %d", p);
        CHECK(info.family == 4, "info family %d", p);
        CHECK(info.estimated_ops_per_pixel > 0, "info ops %d", p);
        for (int q = 0; q < p; ++q) {
            CHECK(strcmp(slug,
                         cyber_face_profile_slug((cyber_profile_t)q)) !=
                      0,
                  "slug %d duplicates %d", p, q);
        }
    }
    CHECK(!cyber_face_profile_info(CYBER_PROFILE_COUNT, NULL),
          "info rejects bad profile");
    CHECK(sizeof(cyber_keyframe_t) == CYBER_KEYFRAME_BYTES,
          "keyframe ABI size");
}

static void test_argument_validation(void)
{
    cyber_keyframe_t kf;
    memset(&kf, 0, sizeof kf);
    reset_frame(&frame_a);
    CHECK(!cyber_face_render(NULL, CYBER_PROFILE_NEON_SDF_CYAN, &kf, 0,
                             frame_a.frame, CYBER_FACE_PIXEL_COUNT),
          "NULL ctx rejected");
    CHECK(!cyber_face_render(&ctx_a, CYBER_PROFILE_NEON_SDF_CYAN, NULL,
                             0, frame_a.frame, CYBER_FACE_PIXEL_COUNT),
          "NULL keyframe rejected");
    CHECK(!cyber_face_render(&ctx_a, CYBER_PROFILE_NEON_SDF_CYAN, &kf, 0,
                             NULL, CYBER_FACE_PIXEL_COUNT),
          "NULL frame rejected");
    CHECK(!cyber_face_render(&ctx_a, CYBER_PROFILE_COUNT, &kf, 0,
                             frame_a.frame, CYBER_FACE_PIXEL_COUNT),
          "bad profile rejected");
    CHECK(!cyber_face_render(&ctx_a, CYBER_PROFILE_NEON_SDF_CYAN, &kf, 0,
                             frame_a.frame, CYBER_FACE_PIXEL_COUNT - 1),
          "small capacity rejected");
    cyber_face_ctx_t *uninit = calloc(1, sizeof *uninit);
    CHECK(uninit != NULL, "calloc scratch ctx");
    if (uninit != NULL) {
        CHECK(!cyber_face_render(uninit, CYBER_PROFILE_NEON_SDF_CYAN,
                                 &kf, 0, frame_a.frame,
                                 CYBER_FACE_PIXEL_COUNT),
              "uninitialised ctx rejected");
        free(uninit);
    }
}

static void test_determinism_and_guards(void)
{
    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        for (int c = 0; c < GOLDEN_CASE_COUNT; ++c) {
            reset_frame(&frame_a);
            reset_frame(&frame_b);
            CHECK(cyber_face_render(&ctx_a, (cyber_profile_t)p,
                                    &golden_cases[c].keyframe,
                                    golden_cases[c].sample_clock,
                                    frame_a.frame,
                                    CYBER_FACE_PIXEL_COUNT),
                  "render a p%d c%d", p, c);
            CHECK(cyber_face_render(&ctx_b, (cyber_profile_t)p,
                                    &golden_cases[c].keyframe,
                                    golden_cases[c].sample_clock,
                                    frame_b.frame,
                                    CYBER_FACE_PIXEL_COUNT),
                  "render b p%d c%d", p, c);
            CHECK(memcmp(frame_a.frame, frame_b.frame,
                         sizeof frame_a.frame) == 0,
                  "deterministic across contexts p%d c%d", p, c);
            CHECK(guards_ok(&frame_a) && guards_ok(&frame_b),
                  "guard bands p%d c%d", p, c);
        }
    }
}

static void test_idle_motion_and_blink(void)
{
    cyber_keyframe_t idle;
    memset(&idle, 0, sizeof idle);
    idle.eye_left_open = 255;
    idle.eye_right_open = 255;

    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        reset_frame(&frame_a);
        reset_frame(&frame_b);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &idle, 0,
                          frame_a.frame, CYBER_FACE_PIXEL_COUNT);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &idle,
                          16000u * 8u / 5u, frame_b.frame,
                          CYBER_FACE_PIXEL_COUNT);
        CHECK(memcmp(frame_a.frame, frame_b.frame,
                     sizeof frame_a.frame) != 0,
              "idle motion animates p%d", p);
    }

    /* The blink flag must visibly change the frame. */
    cyber_keyframe_t blink = idle;
    blink.flags = CYBER_KEYFRAME_FLAG_BLINKING;
    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        if (p == CYBER_PROFILE_PALETTE_PLASMA) {
            /* Eyes are a plasma mask; blink still applies but weakly. */
        }
        reset_frame(&frame_a);
        reset_frame(&frame_b);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &idle, 40000u,
                          frame_a.frame, CYBER_FACE_PIXEL_COUNT);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &blink, 40000u,
                          frame_b.frame, CYBER_FACE_PIXEL_COUNT);
        CHECK(memcmp(frame_a.frame, frame_b.frame,
                     sizeof frame_a.frame) != 0,
              "blink flag changes frame p%d", p);
    }

    /* Mouth motion: speaking wide-open differs from closed. */
    cyber_keyframe_t open_mouth = idle;
    open_mouth.mouth_open = 230;
    open_mouth.mouth_width = 200;
    open_mouth.flags = CYBER_KEYFRAME_FLAG_SPEAKING;
    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        reset_frame(&frame_a);
        reset_frame(&frame_b);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &idle, 80000u,
                          frame_a.frame, CYBER_FACE_PIXEL_COUNT);
        cyber_face_render(&ctx_a, (cyber_profile_t)p, &open_mouth,
                          80000u, frame_b.frame,
                          CYBER_FACE_PIXEL_COUNT);
        CHECK(memcmp(frame_a.frame, frame_b.frame,
                     sizeof frame_a.frame) != 0,
              "speech changes frame p%d", p);
    }
}

static uint32_t lcg_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_fuzz(void)
{
    uint32_t rng = 0xC0FFEEu;
    for (int iteration = 0; iteration < 400; ++iteration) {
        cyber_keyframe_t kf;
        uint8_t *bytes = (uint8_t *)&kf;
        for (size_t i = 0; i < sizeof kf; ++i) {
            bytes[i] = (uint8_t)(lcg_next(&rng) >> 24);
        }
        uint32_t clock = lcg_next(&rng);
        int p = (int)(lcg_next(&rng) % CYBER_PROFILE_COUNT);
        reset_frame(&frame_a);
        CHECK(cyber_face_render(&ctx_a, (cyber_profile_t)p, &kf, clock,
                                frame_a.frame, CYBER_FACE_PIXEL_COUNT),
              "fuzz render %d", iteration);
        CHECK(guards_ok(&frame_a), "fuzz guards %d p%d clock %u",
              iteration, p, clock);
    }
}

static void run_goldens(int dump)
{
    if (dump) {
        printf("/* Generated by test_cyber_face --dump-hashes. */\n");
        printf("CYBER_GOLDEN(255, 255, 0u)\n");
    }
    int checked = 0;
    for (int p = 0; p < CYBER_PROFILE_COUNT; ++p) {
        for (int c = 0; c < GOLDEN_CASE_COUNT; ++c) {
            reset_frame(&frame_a);
            cyber_face_render(&ctx_a, (cyber_profile_t)p,
                              &golden_cases[c].keyframe,
                              golden_cases[c].sample_clock,
                              frame_a.frame, CYBER_FACE_PIXEL_COUNT);
            uint64_t hash = fnv1a64_frame(frame_a.frame);
            if (dump) {
                printf("CYBER_GOLDEN(%d, %d, 0x%016llxull)\n", p, c,
                       (unsigned long long)hash);
                continue;
            }
            for (int e = 0; e < GOLDEN_EXPECTED_COUNT; ++e) {
                if (golden_expected[e].profile == p &&
                    golden_expected[e].case_index == c) {
                    CHECK(golden_expected[e].hash == hash,
                          "golden hash %s/%s: got 0x%016llx",
                          cyber_face_profile_slug((cyber_profile_t)p),
                          golden_cases[c].label,
                          (unsigned long long)hash);
                    ++checked;
                }
            }
        }
    }
    if (!dump) {
        if (GOLDEN_EXPECTED_COUNT <= 1) {
            printf("NOTE: golden_hashes.inc is empty; run "
                   "`make regen-golden`.\n");
        } else {
            CHECK(checked ==
                      CYBER_PROFILE_COUNT * GOLDEN_CASE_COUNT,
                  "golden table covers every profile/case (%d)",
                  checked);
            printf("golden hashes verified: %d\n", checked);
        }
    }
}

int main(int argc, char **argv)
{
    int dump = argc > 1 && strcmp(argv[1], "--dump-hashes") == 0;
    cyber_face_init(&ctx_a);
    cyber_face_init(&ctx_b);

    if (dump) {
        run_goldens(1);
        return 0;
    }

    test_metadata();
    test_argument_validation();
    test_determinism_and_guards();
    test_idle_motion_and_blink();
    test_fuzz();
    run_goldens(0);

    if (failures == 0) {
        printf("OK: all cyber face tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
