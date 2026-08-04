/*
 * Unit tests for the fvqa metric core. Synthetic frames only; no renderer
 * sources are linked, so this compiles and runs in well under a second.
 */
#include "fvqa_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition)                                                 \
    do {                                                                 \
        checks++;                                                        \
        if (!(condition)) {                                              \
            failures++;                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                    #condition);                                         \
        }                                                                \
    } while (0)

static uint8_t luma_a[FVQA_PIXELS];
static uint8_t luma_b[FVQA_PIXELS];
static uint8_t mask[FVQA_PIXELS];
static fvqa_cc_scratch_t scratch;

static void clear(uint8_t *buffer, uint8_t value)
{
    memset(buffer, value, FVQA_PIXELS);
}

static void fill_rect(
    uint8_t *buffer, int x0, int y0, int x1, int y1, uint8_t value)
{
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            buffer[y * FVQA_WIDTH + x] = value;
        }
    }
}

static void test_luma_conversion(void)
{
    uint16_t frame[4] = {
        0x0000, /* black */
        0xFFFF, /* white */
        0xF800, /* pure red */
        0x07E0, /* pure green */
    };
    uint8_t luma[4];
    fvqa_luma_from_rgb565(frame, luma, 4);
    CHECK(luma[0] == 0);
    CHECK(luma[1] == 255);
    /* red: 77*255/256 = 76 */
    CHECK(luma[2] == 76);
    /* green: 150*255/256 = 149 */
    CHECK(luma[3] == 149);
}

static void test_background_and_mask(void)
{
    clear(luma_a, 10);
    fill_rect(luma_a, 40, 30, 120, 90, 200);
    CHECK(fvqa_background_mode(luma_a) == 10);
    fvqa_mask_build(luma_a, 10, mask);
    CHECK(mask[35 * FVQA_WIDTH + 80] == 1);
    CHECK(mask[0] == 0);
    /* Bright background, dark face: mask must still find the face. */
    clear(luma_a, 220);
    fill_rect(luma_a, 40, 30, 120, 90, 40);
    CHECK(fvqa_background_mode(luma_a) == 220);
    fvqa_mask_build(luma_a, 220, mask);
    CHECK(mask[35 * FVQA_WIDTH + 80] == 1);
    CHECK(mask[0] == 0);
    /* Values inside the margin stay background. */
    clear(luma_a, 100);
    fill_rect(luma_a, 0, 0, 8, 8, 100 + FVQA_MASK_MARGIN - 1);
    fvqa_mask_build(luma_a, 100, mask);
    CHECK(mask[0] == 0);
}

static void test_border_contact(void)
{
    clear(luma_a, 0);
    /* Square flush against the left edge, spanning rows 50..70. */
    fill_rect(luma_a, 0, 50, 30, 70, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_border_contact_t contact;
    fvqa_border_contact(mask, &contact);
    CHECK(contact.left == 20);
    CHECK(contact.right == 0);
    CHECK(contact.top == 0);
    CHECK(contact.bottom == 0);
    CHECK(contact.max_run == 20);
    CHECK(contact.total == 20);
    /* A centered square touches nothing. */
    clear(luma_a, 0);
    fill_rect(luma_a, 60, 40, 100, 80, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_border_contact(mask, &contact);
    CHECK(contact.total == 0);
    /* Full-bleed frame touches everything. */
    clear(luma_a, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_border_contact(mask, &contact);
    CHECK(contact.top == FVQA_WIDTH);
    CHECK(contact.bottom == FVQA_WIDTH);
    CHECK(contact.left == FVQA_HEIGHT);
    CHECK(contact.right == FVQA_HEIGHT);
}

static void test_component_count(void)
{
    clear(luma_a, 0);
    fill_rect(luma_a, 20, 20, 50, 50, 255);
    fill_rect(luma_a, 90, 20, 120, 50, 255);
    fvqa_mask_build(luma_a, 0, mask);
    CHECK(fvqa_component_count(mask, &scratch) == 2);
    /* Bridge the two squares: one component. */
    fill_rect(luma_a, 50, 30, 90, 34, 255);
    fvqa_mask_build(luma_a, 0, mask);
    CHECK(fvqa_component_count(mask, &scratch) == 1);
    /* Specks below the area floor are ignored. */
    clear(luma_a, 0);
    fill_rect(luma_a, 20, 20, 22, 22, 255); /* 4 px < floor of 5 */
    fvqa_mask_build(luma_a, 0, mask);
    CHECK(fvqa_component_count(mask, &scratch) == 0);
    fill_rect(luma_a, 40, 40, 45, 41, 255); /* exactly 5 px */
    fvqa_mask_build(luma_a, 0, mask);
    CHECK(fvqa_component_count(mask, &scratch) == 1);
    /* Empty mask. */
    clear(luma_a, 0);
    fvqa_mask_build(luma_a, 0, mask);
    CHECK(fvqa_component_count(mask, &scratch) == 0);
}

static void test_mouth_report(void)
{
    fvqa_mouth_report_t report;
    /* Connected smile: single bar across the mouth band. */
    clear(luma_a, 0);
    fill_rect(luma_a, 40, 84, 120, 96, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_mouth_report(mask, &scratch, &report);
    CHECK(report.band_components == 1);
    CHECK(!report.corners_detached);
    CHECK(report.largest_area == 80 * 12);
    /* Detached corners: bar plus two floating corner blobs. */
    fill_rect(luma_a, 24, 80, 32, 88, 255);
    fill_rect(luma_a, 128, 80, 136, 88, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_mouth_report(mask, &scratch, &report);
    CHECK(report.band_components == 3);
    CHECK(report.corners_detached);
    /* A single off-corner speck must not flag. */
    clear(luma_a, 0);
    fill_rect(luma_a, 40, 84, 120, 96, 255);
    fill_rect(luma_a, 20, 90, 21, 91, 255); /* 1 px speck */
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_mouth_report(mask, &scratch, &report);
    CHECK(!report.corners_detached);
    /* Eyes-only renderer: empty band is calm, not detached. */
    clear(luma_a, 0);
    fill_rect(luma_a, 30, 20, 70, 50, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_mouth_report(mask, &scratch, &report);
    CHECK(report.band_components == 0);
    CHECK(!report.corners_detached);
    /* Mouth spanning the band boundary still counts inside the band. */
    clear(luma_a, 0);
    fill_rect(luma_a, 60, 60, 100, 90, 255);
    fvqa_mask_build(luma_a, 0, mask);
    fvqa_mouth_report(mask, &scratch, &report);
    CHECK(report.band_components == 1);
}

static void test_band_delta(void)
{
    clear(luma_a, 0);
    clear(luma_b, 0);
    /* Change confined to the eye band. */
    fill_rect(luma_b, 40, 20, 80, 40, 200);
    const uint32_t eye = fvqa_band_delta_centi(
        luma_a, luma_b, FVQA_EYE_BAND_TOP, FVQA_EYE_BAND_BOTTOM);
    const uint32_t mouth = fvqa_band_delta_centi(
        luma_a, luma_b, FVQA_MOUTH_BAND_TOP, FVQA_MOUTH_BAND_BOTTOM);
    CHECK(eye > 0);
    CHECK(mouth == 0);
    /* Exact value: 40x20 px of delta 200 in a 52-row band. */
    const uint32_t expected =
        (40U * 20U * 200U * 100U) /
        ((FVQA_EYE_BAND_BOTTOM - FVQA_EYE_BAND_TOP) * FVQA_WIDTH);
    CHECK(eye == expected);
    CHECK(fvqa_band_delta_centi(luma_a, luma_a, 0, FVQA_HEIGHT) == 0);
}

static void test_downscale(void)
{
    clear(luma_a, 37);
    uint8_t small[FVQA_SMALL_PIXELS];
    fvqa_box_downscale4(luma_a, small);
    for (int index = 0; index < FVQA_SMALL_PIXELS; index++) {
        CHECK(small[index] == 37);
        if (small[index] != 37) {
            return;
        }
    }
    /* A 4x4 block of 255 in a black frame averages to 255 in its cell. */
    clear(luma_a, 0);
    fill_rect(luma_a, 8, 8, 12, 12, 255);
    fvqa_box_downscale4(luma_a, small);
    CHECK(small[2 * FVQA_SMALL_WIDTH + 2] == 255);
    CHECK(small[0] == 0);
}

static void test_signature_palette_invariance(void)
{
    uint32_t sig_a[FVQA_SIG_WORDS];
    uint32_t sig_b[FVQA_SIG_WORDS];
    /* Face-like blobs. */
    clear(luma_a, 12);
    fill_rect(luma_a, 30, 24, 66, 60, 190);
    fill_rect(luma_a, 94, 24, 130, 60, 190);
    fill_rect(luma_a, 50, 80, 110, 100, 190);
    /* Palette swap: same geometry, remapped tones. */
    for (int index = 0; index < FVQA_PIXELS; index++) {
        luma_b[index] = luma_a[index] == 12 ? 60U : 230U;
    }
    fvqa_edge_signature(luma_a, sig_a);
    fvqa_edge_signature(luma_b, sig_b);
    CHECK(fvqa_signature_distance_permille(sig_a, sig_b) < 150);
    /* Different geometry: circles moved and resized. */
    clear(luma_b, 12);
    fill_rect(luma_b, 10, 10, 80, 40, 190);
    fill_rect(luma_b, 100, 70, 150, 110, 190);
    fvqa_edge_signature(luma_b, sig_b);
    CHECK(fvqa_signature_distance_permille(sig_a, sig_b) > 500);
    /* Identical frames: zero distance. */
    fvqa_edge_signature(luma_a, sig_b);
    CHECK(fvqa_signature_distance_permille(sig_a, sig_b) == 0);
    /* Blank frames have empty signatures and zero distance. */
    clear(luma_a, 90);
    clear(luma_b, 90);
    fvqa_edge_signature(luma_a, sig_a);
    fvqa_edge_signature(luma_b, sig_b);
    CHECK(fvqa_signature_distance_permille(sig_a, sig_b) == 0);
}

static void test_popcount(void)
{
    CHECK(fvqa_popcount32(0) == 0);
    CHECK(fvqa_popcount32(0xFFFFFFFFU) == 32);
    CHECK(fvqa_popcount32(0x80000001U) == 2);
    CHECK(fvqa_popcount32(0x0F0F0F0FU) == 16);
}

static void test_determinism(void)
{
    clear(luma_a, 5);
    fill_rect(luma_a, 33, 21, 127, 99, 180);
    fill_rect(luma_a, 60, 60, 100, 80, 20);
    uint32_t sig_a[FVQA_SIG_WORDS];
    uint32_t sig_b[FVQA_SIG_WORDS];
    fvqa_edge_signature(luma_a, sig_a);
    fvqa_edge_signature(luma_a, sig_b);
    CHECK(memcmp(sig_a, sig_b, sizeof(sig_a)) == 0);
    fvqa_mask_build(luma_a, fvqa_background_mode(luma_a), mask);
    const int first = fvqa_component_count(mask, &scratch);
    const int second = fvqa_component_count(mask, &scratch);
    CHECK(first == second);
}

int main(void)
{
    test_luma_conversion();
    test_background_and_mask();
    test_border_contact();
    test_component_count();
    test_mouth_report();
    test_band_delta();
    test_downscale();
    test_signature_palette_invariance();
    test_popcount();
    test_determinism();
    if (failures != 0) {
        fprintf(stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("fvqa_test: all %d checks passed\n", checks);
    return 0;
}
