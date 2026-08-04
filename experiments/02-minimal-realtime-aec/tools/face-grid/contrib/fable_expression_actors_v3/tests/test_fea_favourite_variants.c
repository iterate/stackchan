/*
 * Run the mature FEA quality suite against the isolated favourite-variant
 * API.  Including the original harness keeps both packs on precisely the
 * same purity, expression-separability, temporal, blink, viseme, 40-byte IR
 * and adversarial-fuzz gates without copying their implementation.
 */

#include <time.h>

#include "fea_favourite_variants.h"

#define fea_profile_t fea_favourite_profile_t
#define FEA_PROFILE_COUNT FEA_FAVOURITE_PROFILE_COUNT
#define FEA_PROFILE_MOCHI_CAT FEA_FAVOURITE_LANTERN_BLOOM
#define FEA_PROFILE_EMOTE_STICKER FEA_FAVOURITE_POP_BURST
#define fea_profile_count fea_favourite_profile_count
#define fea_profile_slug fea_favourite_profile_slug
#define fea_profile_name fea_favourite_profile_name
#define fea_profile_info fea_favourite_profile_info
#define fea_probe fea_favourite_probe
#define fea_render_frame fea_favourite_render_frame
#define main fea_original_suite_main
#include "test_fea.c"
#undef main

static double favourite_now_seconds(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void test_favourite_metadata(void)
{
    for (unsigned profile = 0; profile < FEA_FAVOURITE_PROFILE_COUNT;
         ++profile) {
        fea_favourite_lineage_t lineage;
        const char *thesis = fea_favourite_profile_thesis(
            (fea_favourite_profile_t)profile);
        CHECK(thesis != NULL && strlen(thesis) >= 40U,
              "profile %u has a substantive thesis", profile);
        CHECK(fea_favourite_profile_lineage(
                  (fea_favourite_profile_t)profile, &lineage),
              "profile %u lineage", profile);
        CHECK((unsigned)lineage <=
                  FEA_FAVOURITE_LINEAGE_EMOTE_STICKER,
              "profile %u valid lineage", profile);
        for (unsigned earlier = 0; earlier < profile; ++earlier) {
            CHECK(strcmp(
                      fea_favourite_profile_slug(
                          (fea_favourite_profile_t)profile),
                      fea_favourite_profile_slug(
                          (fea_favourite_profile_t)earlier)) != 0,
                  "profile %u slug unique", profile);
        }
    }
    CHECK(fea_favourite_profile_thesis(
              (fea_favourite_profile_t)FEA_FAVOURITE_PROFILE_COUNT) ==
              NULL,
          "thesis rejects invalid profile");
    CHECK(!fea_favourite_profile_lineage(
              FEA_FAVOURITE_LANTERN_BLOOM, NULL),
          "lineage rejects NULL output");
}

static void test_favourite_all_visemes(void)
{
    const uint32_t clock = SAMPLE_RATE * 4U + 71U;
    for (unsigned profile = 0; profile < FEA_FAVOURITE_PROFILE_COUNT;
         ++profile) {
        uint32_t hashes[15];
        unsigned distinct = 0U;
        for (uint8_t viseme = 0; viseme < 15U; ++viseme) {
            face_render_key_t key = base_key();
            key.viseme = viseme;
            key.viseme_weight = 255U;
            key.viseme_blend = 0U;
            CHECK(fea_favourite_render_frame(
                      (fea_favourite_profile_t)profile, &key, clock,
                      frame_a(), FEA_PIXEL_COUNT),
                  "%s: viseme %u render",
                  fea_favourite_profile_slug(
                      (fea_favourite_profile_t)profile),
                  viseme);
            hashes[viseme] = frame_hash(frame_a());
            bool first = true;
            for (unsigned earlier = 0; earlier < viseme; ++earlier) {
                first &= hashes[earlier] != hashes[viseme];
            }
            distinct += first;
        }
        /*
         * OVR15 deliberately shares several articulatory families; demand
         * at least nine raster-distinct outcomes while the canonical five
         * are checked pairwise by test_viseme_articulation().
         */
        CHECK(distinct >= 9U, "%s: %u/15 raster-distinct visemes",
              fea_favourite_profile_slug(
                  (fea_favourite_profile_t)profile),
              distinct);
    }
}

static void test_favourite_render_budget(void)
{
    enum { BENCH_FRAMES = 120 };
    volatile uint32_t checksum = 0U;
    for (unsigned profile = 0; profile < FEA_FAVOURITE_PROFILE_COUNT;
         ++profile) {
        const double start = favourite_now_seconds();
        for (uint32_t frame = 0; frame < BENCH_FRAMES; ++frame) {
            const uint32_t clock =
                (uint32_t)((uint64_t)frame * SAMPLE_RATE / 30U);
            face_render_key_t key = motion_key(frame);
            const face_stage_cue_t cue = motion_cue();
            (void)face_stage_cue_apply(&cue, clock, &key);
            CHECK(fea_favourite_render_frame(
                      (fea_favourite_profile_t)profile, &key, clock,
                      frame_a(), FEA_PIXEL_COUNT),
                  "benchmark render %u/%u", profile, frame);
            checksum ^= frame_a()[(frame * 251U) % FEA_PIXEL_COUNT];
        }
        const double elapsed = favourite_now_seconds() - start;
        const double milliseconds =
            elapsed * 1000.0 / (double)BENCH_FRAMES;
        CHECK(milliseconds < (1000.0 / 30.0),
              "%s: %.3f ms/frame exceeds 30 fps",
              fea_favourite_profile_slug(
                  (fea_favourite_profile_t)profile),
              milliseconds);
        printf("  %-29s %7.3f ms/frame  %8.1f fps\n",
               fea_favourite_profile_slug(
                   (fea_favourite_profile_t)profile),
               milliseconds, 1000.0 / milliseconds);
    }
    CHECK(checksum != 0xffffffffU, "benchmark checksum consumed");
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--dump-hashes") == 0) {
        print_case_table(false);
        return 0;
    }

    printf("fable favourite variants: isolated quality suite\n");
    test_abi();
    test_favourite_metadata();
    test_purity_and_coverage();
    test_expression_separability();
    test_temporal_smoothness();
    test_blink_kinematics();
    test_corner_parenting();
    test_acting_curve();
    test_viseme_articulation();
    test_favourite_all_visemes();
    test_all_forty_bytes_live();
    test_adversarial_fuzz();
    test_favourite_render_budget();

    if (checks_failed != 0U) {
        printf("FAILED: %" PRIu32 "/%" PRIu32 " checks\n",
               checks_failed, checks_run);
        return 1;
    }
    printf("PASS: %" PRIu32 " checks\n", checks_run);
    return 0;
}
