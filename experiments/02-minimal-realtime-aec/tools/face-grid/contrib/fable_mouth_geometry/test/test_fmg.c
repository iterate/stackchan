#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fmg_internal.h"

/*
 * Host-side test suite. Run with --update-golden to regenerate
 * golden_crc.inc after an intentional visual change; the same binary
 * compiled to WebAssembly must produce the identical CRC table, which is
 * the byte-identity contract for the browser lab.
 */

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_failures++;                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

_Static_assert(sizeof(fmg_keyframe_t) == 12, "keyframe ABI must be 12 bytes");
_Static_assert(sizeof(fmg_info_t) == 16, "info ABI must be 16 bytes");

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

enum { GUARD = 256 };

typedef struct {
    uint16_t pre[GUARD];
    uint16_t frame[FMG_PIXEL_COUNT];
    uint16_t post[GUARD];
} guarded_frame_t;

static void guard_init(guarded_frame_t *g)
{
    for (int i = 0; i < GUARD; i++) {
        g->pre[i] = 0xA55A;
        g->post[i] = 0xA55A;
    }
    for (int i = 0; i < FMG_PIXEL_COUNT; i++) {
        g->frame[i] = 0x1234;
    }
}

static bool guard_ok(const guarded_frame_t *g)
{
    for (int i = 0; i < GUARD; i++) {
        if (g->pre[i] != 0xA55A || g->post[i] != 0xA55A) {
            return false;
        }
    }
    return true;
}

/* scenario set shared with the preview tool and the golden table */
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

static const uint32_t s_golden[FMG_PROFILE_COUNT][SCENARIO_COUNT] = {
#include "golden_crc.inc"
};

static void test_registry(void)
{
    CHECK(fmg_profile_count() == FMG_PROFILE_COUNT, "profile count");
    for (int p = 0; p < FMG_PROFILE_COUNT; p++) {
        fmg_info_t info;
        CHECK(fmg_profile_slug((fmg_profile_t)p) != NULL, "slug %d", p);
        CHECK(fmg_profile_name((fmg_profile_t)p) != NULL, "name %d", p);
        CHECK(fmg_profile_info((fmg_profile_t)p, &info), "info %d", p);
        CHECK(info.width == FMG_WIDTH && info.height == FMG_HEIGHT,
              "info dims %d", p);
        CHECK(info.framebuffer_bytes == FMG_FRAME_BYTES, "info bytes %d", p);
        CHECK(info.family == 3, "info family %d", p);
    }
    CHECK(fmg_profile_slug(FMG_PROFILE_COUNT) == NULL, "slug oob");
    CHECK(!fmg_profile_info(FMG_PROFILE_COUNT, NULL), "info oob");
}

static void test_arg_validation(void)
{
    static guarded_frame_t g;
    guard_init(&g);
    fmg_keyframe_t kf = s_scenarios[0].kf;
    CHECK(!fmg_render_frame(FMG_PROFILE_COUNT, &kf, 0, g.frame,
                            FMG_PIXEL_COUNT),
          "bad profile accepted");
    CHECK(!fmg_render_frame(FMG_PROFILE_PRESTON_SPRITES, NULL, 0, g.frame,
                            FMG_PIXEL_COUNT),
          "NULL keyframe accepted");
    CHECK(!fmg_render_frame(FMG_PROFILE_PRESTON_SPRITES, &kf, 0, NULL,
                            FMG_PIXEL_COUNT),
          "NULL buffer accepted");
    CHECK(!fmg_render_frame(FMG_PROFILE_PRESTON_SPRITES, &kf, 0, g.frame,
                            FMG_PIXEL_COUNT - 1),
          "short buffer accepted");
}

static void test_determinism_and_coverage(void)
{
    static guarded_frame_t a, b;
    for (int p = 0; p < FMG_PROFILE_COUNT; p++) {
        for (int s = 0; s < SCENARIO_COUNT; s++) {
            guard_init(&a);
            guard_init(&b);
            CHECK(fmg_render_frame((fmg_profile_t)p, &s_scenarios[s].kf,
                                   s_scenarios[s].clock, a.frame,
                                   FMG_PIXEL_COUNT),
                  "render %d/%d", p, s);
            fmg_render_frame((fmg_profile_t)p, &s_scenarios[s].kf,
                             s_scenarios[s].clock, b.frame, FMG_PIXEL_COUNT);
            CHECK(memcmp(a.frame, b.frame, FMG_FRAME_BYTES) == 0,
                  "nondeterministic %s/%s", fmg_profile_slug(p),
                  s_scenarios[s].name);
            CHECK(guard_ok(&a), "guard tripped %s/%s", fmg_profile_slug(p),
                  s_scenarios[s].name);
            int untouched = 0;
            for (int i = 0; i < FMG_PIXEL_COUNT; i++) {
                if (a.frame[i] == 0x1234) {
                    untouched++;
                }
            }
            /* every profile paints a full background */
            CHECK(untouched == 0, "%s/%s left %d unpainted px",
                  fmg_profile_slug(p), s_scenarios[s].name, untouched);
        }
    }
}

static void test_fuzz(void)
{
    static guarded_frame_t g;
    static const uint32_t clocks[] = {0, 1, 15999, 16000, 123456789,
                                      4294967290U};
    for (int p = 0; p < FMG_PROFILE_COUNT; p++) {
        for (uint32_t i = 0; i < 220; i++) {
            uint32_t h = fmg_hash(i * 2654435761U + (uint32_t)p);
            fmg_keyframe_t kf = {
                (uint8_t)h,
                (uint8_t)(h >> 8),
                (uint8_t)(h >> 16),
                (uint8_t)(h >> 24),
                (uint8_t)fmg_hash(h),
                (uint8_t)(fmg_hash(h) >> 8),
                (uint8_t)(fmg_hash(h) >> 16),
                (int8_t)(fmg_hash(h) >> 24),
                (int8_t)fmg_hash(h + 1),
                (int8_t)(fmg_hash(h + 1) >> 8),
                (uint8_t)((fmg_hash(h + 1) >> 16) & 3),
                (uint8_t)((fmg_hash(h + 1) >> 24) & 3),
            };
            guard_init(&g);
            uint32_t clock = clocks[i % 6] + i;
            CHECK(fmg_render_frame((fmg_profile_t)p, &kf, clock, g.frame,
                                   FMG_PIXEL_COUNT),
                  "fuzz render failed p=%d i=%u", p, i);
            if (!guard_ok(&g)) {
                CHECK(false, "fuzz guard tripped p=%d i=%u", p, i);
                return;
            }
        }
    }
}

static void test_idle_motion(void)
{
    fmg_keyframe_t kf = s_scenarios[0].kf;
    fmg_idle_t idle;
    int32_t lid_min = INT32_MAX, lid_max = INT32_MIN;
    int32_t gaze_min = INT32_MAX, gaze_max = INT32_MIN;
    int32_t brow_min = INT32_MAX, brow_max = INT32_MIN;
    /* one minute of idle at 60 fps */
    for (uint32_t f = 0; f < 3600; f++) {
        fmg_idle_compute(&kf, f * 267U, &idle);
        if (idle.lid_l_q8 < lid_min) {
            lid_min = idle.lid_l_q8;
        }
        if (idle.lid_l_q8 > lid_max) {
            lid_max = idle.lid_l_q8;
        }
        if (idle.gaze_dx_q8 < gaze_min) {
            gaze_min = idle.gaze_dx_q8;
        }
        if (idle.gaze_dx_q8 > gaze_max) {
            gaze_max = idle.gaze_dx_q8;
        }
        if (idle.brow_l_q8 < brow_min) {
            brow_min = idle.brow_l_q8;
        }
        if (idle.brow_l_q8 > brow_max) {
            brow_max = idle.brow_l_q8;
        }
    }
    CHECK(lid_min < 40, "blinks close the lids (min %d)", lid_min);
    CHECK(lid_max >= 256, "lids fully open between blinks (max %d)", lid_max);
    CHECK(gaze_max - gaze_min > 2 * 256,
          "saccades move the gaze (range %d)", gaze_max - gaze_min);
    CHECK(brow_max - brow_min > 128, "brows are alive (range %d)",
          brow_max - brow_min);
    /* blink gating multiplies, never replaces, commanded eye openness */
    kf.eye_left_open = 0;
    for (uint32_t f = 0; f < 600; f++) {
        fmg_idle_compute(&kf, f * 267U, &idle);
        CHECK(idle.lid_l_q8 <= 0, "commanded shut eye stayed shut");
        if (idle.lid_l_q8 > 0) {
            return;
        }
    }
}

static void test_viseme_classifier(void)
{
    /* each anchor pose must classify as itself */
    static const struct {
        fmg_vis_t vis;
        uint8_t open, width, round, press, teeth;
    } anchors[] = {
        {FMG_VIS_AA, 236, 205, 24, 0, 18},
        {FMG_VIS_EE, 155, 246, 0, 0, 128},
        {FMG_VIS_IH, 102, 255, 0, 0, 155},
        {FMG_VIS_OH, 214, 112, 255, 0, 16},
        {FMG_VIS_UU, 112, 82, 244, 0, 10},
        {FMG_VIS_MBP, 12, 164, 18, 255, 0},
        {FMG_VIS_SS, 70, 210, 0, 0, 220},
        {FMG_VIS_FV, 38, 198, 0, 176, 235},
        {FMG_VIS_LN, 80, 198, 6, 0, 145},
    };
    for (size_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i++) {
        fmg_keyframe_t kf = {anchors[i].open, anchors[i].width,
                             anchors[i].round, anchors[i].press,
                             anchors[i].teeth, 255, 255, 0, 0, 0, 3, 1};
        fmg_mouth_t m;
        fmg_mouth_compute(&kf, &m);
        CHECK(m.vis == anchors[i].vis, "anchor %zu classified as %d", i,
              (int)m.vis);
    }
    /* silence maps to rest */
    fmg_keyframe_t rest = {0, 110, 30, 0, 0, 255, 255, 0, 0, 0, 0, 0};
    fmg_mouth_t m;
    fmg_mouth_compute(&rest, &m);
    CHECK(m.vis == FMG_VIS_REST, "rest classified as %d", (int)m.vis);
    /* JALI dominance: full press crushes the jaw axis */
    fmg_keyframe_t pressed = {255, 128, 0, 255, 0, 255, 255, 0, 0, 0, 3, 1};
    fmg_mouth_compute(&pressed, &m);
    CHECK(m.jaw_q8 < 64, "press dominates jaw (jaw=%d)", m.jaw_q8);
}

static void test_coart(void)
{
    fmg_coart_t c1, c2;
    fmg_coart_reset(&c1);
    fmg_coart_reset(&c2);
    fmg_keyframe_t in = {0, 110, 30, 0, 0, 255, 255, 0, 0, 0, 3, 1};
    fmg_keyframe_t out1, out2;
    /* jaw opens gradually toward the target, never overshooting */
    in.mouth_open = 240;
    uint8_t prev = 0;
    for (int f = 1; f <= 60; f++) {
        fmg_coart_apply(&c1, &in, (uint32_t)f * 533U, &out1);
        CHECK(out1.mouth_open >= prev, "coart open is monotone");
        CHECK(out1.mouth_open <= 240, "coart open overshoot");
        prev = out1.mouth_open;
    }
    CHECK(prev > 200, "coart converged (got %d)", prev);
    /* determinism: identical call sequences agree */
    fmg_coart_reset(&c1);
    for (int f = 1; f <= 30; f++) {
        fmg_coart_apply(&c1, &in, (uint32_t)f * 533U, &out1);
        fmg_coart_apply(&c2, &in, (uint32_t)f * 533U, &out2);
    }
    CHECK(memcmp(&out1, &out2, sizeof(out1)) == 0, "coart determinism");
    /* bilabial dominance: press collapses the smoothed jaw */
    fmg_coart_reset(&c1);
    in.mouth_open = 255;
    in.mouth_press = 255;
    for (int f = 1; f <= 90; f++) {
        fmg_coart_apply(&c1, &in, (uint32_t)f * 533U, &out1);
    }
    CHECK(out1.mouth_open < 48, "press dominance in coart (open=%d)",
          out1.mouth_open);
    /* clock going backwards must not divide by zero or explode */
    fmg_coart_apply(&c1, &in, 10, &out1);
    fmg_coart_apply(&c1, &in, 5, &out1);
    CHECK(true, "coart survived clock reversal");
}

static void run_golden(bool update)
{
    static uint16_t frame[FMG_PIXEL_COUNT];
    if (update) {
        FILE *f = fopen("test/golden_crc.inc", "w");
        if (f == NULL) {
            printf("cannot write golden_crc.inc\n");
            g_failures++;
            return;
        }
        fprintf(f, "/* Generated by test_fmg --update-golden. */\n");
        for (int p = 0; p < FMG_PROFILE_COUNT; p++) {
            fprintf(f, "{");
            for (int s = 0; s < SCENARIO_COUNT; s++) {
                fmg_render_frame((fmg_profile_t)p, &s_scenarios[s].kf,
                                 s_scenarios[s].clock, frame,
                                 FMG_PIXEL_COUNT);
                uint32_t crc = crc32_update(0, (const uint8_t *)frame,
                                            FMG_FRAME_BYTES);
                fprintf(f, "0x%08XU,%s", crc,
                        s == SCENARIO_COUNT - 1 ? "" : " ");
            }
            fprintf(f, "},\n");
        }
        fclose(f);
        printf("golden_crc.inc regenerated\n");
        return;
    }
    for (int p = 0; p < FMG_PROFILE_COUNT; p++) {
        for (int s = 0; s < SCENARIO_COUNT; s++) {
            fmg_render_frame((fmg_profile_t)p, &s_scenarios[s].kf,
                             s_scenarios[s].clock, frame, FMG_PIXEL_COUNT);
            uint32_t crc =
                crc32_update(0, (const uint8_t *)frame, FMG_FRAME_BYTES);
            CHECK(crc == s_golden[p][s],
                  "golden mismatch %s/%s: got 0x%08X want 0x%08X",
                  fmg_profile_slug(p), s_scenarios[s].name, crc,
                  s_golden[p][s]);
        }
    }
}

int main(int argc, char **argv)
{
    bool update = argc > 1 && strcmp(argv[1], "--update-golden") == 0;
    test_registry();
    test_arg_validation();
    test_determinism_and_coverage();
    test_fuzz();
    test_idle_motion();
    test_viseme_classifier();
    test_coart();
    run_golden(update);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
