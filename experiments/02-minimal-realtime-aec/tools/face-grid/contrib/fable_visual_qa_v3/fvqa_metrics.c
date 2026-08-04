#include "fvqa_metrics.h"

uint32_t fvqa_popcount32(uint32_t value)
{
    value = value - ((value >> 1) & 0x55555555U);
    value = (value & 0x33333333U) + ((value >> 2) & 0x33333333U);
    value = (value + (value >> 4)) & 0x0F0F0F0FU;
    return (value * 0x01010101U) >> 24;
}

void fvqa_luma_from_rgb565(
    const uint16_t *frame, uint8_t *luma, size_t pixel_count)
{
    for (size_t index = 0; index < pixel_count; index++) {
        const uint16_t pixel = frame[index];
        const uint32_t r5 = (pixel >> 11) & 0x1FU;
        const uint32_t g6 = (pixel >> 5) & 0x3FU;
        const uint32_t b5 = pixel & 0x1FU;
        const uint32_t r = (r5 << 3) | (r5 >> 2);
        const uint32_t g = (g6 << 2) | (g6 >> 4);
        const uint32_t b = (b5 << 3) | (b5 >> 2);
        luma[index] = (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
    }
}

uint8_t fvqa_background_mode(const uint8_t *luma)
{
    uint16_t histogram[256] = {0};
    for (int x = 0; x < FVQA_WIDTH; x++) {
        histogram[luma[x]]++;
        histogram[luma[(FVQA_HEIGHT - 1) * FVQA_WIDTH + x]]++;
    }
    for (int y = 1; y < FVQA_HEIGHT - 1; y++) {
        histogram[luma[y * FVQA_WIDTH]]++;
        histogram[luma[y * FVQA_WIDTH + FVQA_WIDTH - 1]]++;
    }
    int mode = 0;
    for (int value = 1; value < 256; value++) {
        if (histogram[value] > histogram[mode]) {
            mode = value;
        }
    }
    return (uint8_t)mode;
}

void fvqa_mask_build(
    const uint8_t *luma, uint8_t background_mode, uint8_t *mask)
{
    for (int index = 0; index < FVQA_PIXELS; index++) {
        const int delta = (int)luma[index] - (int)background_mode;
        const int magnitude = delta < 0 ? -delta : delta;
        mask[index] = magnitude >= FVQA_MASK_MARGIN ? 1U : 0U;
    }
}

static uint16_t side_stats(
    const uint8_t *mask,
    int start,
    int stride,
    int count,
    uint16_t *max_run)
{
    uint16_t lit = 0;
    uint16_t run = 0;
    for (int step = 0; step < count; step++) {
        if (mask[start + step * stride] != 0U) {
            lit++;
            run++;
            if (run > *max_run) {
                *max_run = run;
            }
        } else {
            run = 0;
        }
    }
    return lit;
}

void fvqa_border_contact(
    const uint8_t *mask, fvqa_border_contact_t *out)
{
    out->max_run = 0;
    out->top = side_stats(mask, 0, 1, FVQA_WIDTH, &out->max_run);
    out->bottom = side_stats(
        mask, (FVQA_HEIGHT - 1) * FVQA_WIDTH, 1, FVQA_WIDTH,
        &out->max_run);
    out->left = side_stats(
        mask, 0, FVQA_WIDTH, FVQA_HEIGHT, &out->max_run);
    out->right = side_stats(
        mask, FVQA_WIDTH - 1, FVQA_WIDTH, FVQA_HEIGHT,
        &out->max_run);
    out->total = (uint16_t)(
        out->top + out->bottom + out->left + out->right);
}

/*
 * Iterative 4-connected flood fill restricted to rows [row_top, row_bottom).
 * Labels start at 1. Returns the filled area.
 */
static int flood_fill(
    const uint8_t *mask,
    fvqa_cc_scratch_t *scratch,
    int seed,
    int16_t label,
    int row_top,
    int row_bottom)
{
    int depth = 0;
    int area = 0;
    scratch->labels[seed] = label;
    scratch->stack[depth++] = seed;
    while (depth > 0) {
        const int index = (int)scratch->stack[--depth];
        area++;
        const int x = index % FVQA_WIDTH;
        const int y = index / FVQA_WIDTH;
        const int neighbors[4] = {
            x > 0 ? index - 1 : -1,
            x < FVQA_WIDTH - 1 ? index + 1 : -1,
            y > row_top ? index - FVQA_WIDTH : -1,
            y < row_bottom - 1 ? index + FVQA_WIDTH : -1,
        };
        for (int n = 0; n < 4; n++) {
            const int next = neighbors[n];
            if (next >= 0 && mask[next] != 0U &&
                scratch->labels[next] == 0) {
                scratch->labels[next] = label;
                scratch->stack[depth++] = next;
            }
        }
    }
    return area;
}

static int label_region(
    const uint8_t *mask,
    fvqa_cc_scratch_t *scratch,
    int row_top,
    int row_bottom,
    int16_t *largest_label,
    int *largest_area)
{
    for (int y = row_top; y < row_bottom; y++) {
        for (int x = 0; x < FVQA_WIDTH; x++) {
            scratch->labels[y * FVQA_WIDTH + x] = 0;
        }
    }
    int16_t label = 0;
    int filtered = 0;
    *largest_label = 0;
    *largest_area = 0;
    for (int y = row_top; y < row_bottom; y++) {
        for (int x = 0; x < FVQA_WIDTH; x++) {
            const int index = y * FVQA_WIDTH + x;
            if (mask[index] == 0U || scratch->labels[index] != 0) {
                continue;
            }
            label++;
            const int area = flood_fill(
                mask, scratch, index, label, row_top, row_bottom);
            if (area >= FVQA_MIN_COMPONENT_AREA) {
                filtered++;
            }
            if (area > *largest_area) {
                *largest_area = area;
                *largest_label = label;
            }
        }
    }
    return filtered;
}

int fvqa_component_count(
    const uint8_t *mask, fvqa_cc_scratch_t *scratch)
{
    int16_t largest_label;
    int largest_area;
    return label_region(
        mask, scratch, 0, FVQA_HEIGHT, &largest_label, &largest_area);
}

void fvqa_mouth_report(
    const uint8_t *mask,
    fvqa_cc_scratch_t *scratch,
    fvqa_mouth_report_t *out)
{
    int16_t largest_label;
    int largest_area;
    const int count = label_region(
        mask, scratch, FVQA_MOUTH_BAND_TOP, FVQA_MOUTH_BAND_BOTTOM,
        &largest_label, &largest_area);
    out->band_components = (uint8_t)(count > 255 ? 255 : count);
    out->largest_area =
        (uint16_t)(largest_area > 65535 ? 65535 : largest_area);
    out->corners_detached = false;
    if (largest_area < FVQA_MIN_COMPONENT_AREA) {
        return;
    }
    int extreme_left = -1;
    int extreme_right = -1;
    for (int y = FVQA_MOUTH_BAND_TOP; y < FVQA_MOUTH_BAND_BOTTOM; y++) {
        for (int x = 0; x < FVQA_WIDTH; x++) {
            const int index = y * FVQA_WIDTH + x;
            if (mask[index] == 0U) {
                continue;
            }
            if (extreme_left < 0 || x < extreme_left % FVQA_WIDTH) {
                extreme_left = index;
            }
            if (extreme_right < 0 || x > extreme_right % FVQA_WIDTH) {
                extreme_right = index;
            }
        }
    }
    if (extreme_left < 0 || extreme_right < 0) {
        return;
    }
    /* Ignore speck extremes: only flag when the detached extreme belongs
     * to a component of visible size. */
    const int16_t left_label = scratch->labels[extreme_left];
    const int16_t right_label = scratch->labels[extreme_right];
    bool detached = false;
    if (left_label != largest_label || right_label != largest_label) {
        /* Only flag when the detached extreme belongs to a component of
         * visible size, so single-pixel specks never count as corners. */
        const int16_t suspects[2] = {left_label, right_label};
        for (int s = 0; s < 2; s++) {
            if (suspects[s] == largest_label) {
                continue;
            }
            int area = 0;
            for (int y = FVQA_MOUTH_BAND_TOP;
                 y < FVQA_MOUTH_BAND_BOTTOM; y++) {
                for (int x = 0; x < FVQA_WIDTH; x++) {
                    if (scratch->labels[y * FVQA_WIDTH + x] ==
                        suspects[s]) {
                        area++;
                    }
                }
            }
            if (area >= FVQA_MIN_COMPONENT_AREA) {
                detached = true;
            }
        }
    }
    out->corners_detached = detached;
}

uint32_t fvqa_band_delta_centi(
    const uint8_t *luma_a,
    const uint8_t *luma_b,
    int band_top,
    int band_bottom)
{
    uint32_t sum = 0;
    for (int y = band_top; y < band_bottom; y++) {
        const int row = y * FVQA_WIDTH;
        for (int x = 0; x < FVQA_WIDTH; x++) {
            const int delta =
                (int)luma_a[row + x] - (int)luma_b[row + x];
            sum += (uint32_t)(delta < 0 ? -delta : delta);
        }
    }
    const uint32_t pixels =
        (uint32_t)(band_bottom - band_top) * FVQA_WIDTH;
    return pixels == 0U ? 0U : (sum * 100U) / pixels;
}

void fvqa_box_downscale4(const uint8_t *luma, uint8_t *small)
{
    for (int sy = 0; sy < FVQA_SMALL_HEIGHT; sy++) {
        for (int sx = 0; sx < FVQA_SMALL_WIDTH; sx++) {
            uint32_t sum = 0;
            for (int dy = 0; dy < 4; dy++) {
                const int row = (sy * 4 + dy) * FVQA_WIDTH + sx * 4;
                sum += (uint32_t)luma[row] + luma[row + 1] +
                       luma[row + 2] + luma[row + 3];
            }
            small[sy * FVQA_SMALL_WIDTH + sx] = (uint8_t)(sum / 16U);
        }
    }
}

void fvqa_edge_signature(const uint8_t *luma, uint32_t *signature)
{
    /* Max-pooled gradient magnitude per 4x4 cell. */
    uint16_t pooled[FVQA_SMALL_PIXELS];
    for (int index = 0; index < FVQA_SMALL_PIXELS; index++) {
        pooled[index] = 0;
    }
    for (int y = 1; y < FVQA_HEIGHT - 1; y++) {
        for (int x = 1; x < FVQA_WIDTH - 1; x++) {
            const int index = y * FVQA_WIDTH + x;
            int horizontal =
                (int)luma[index + 1] - (int)luma[index - 1];
            int vertical = (int)luma[index + FVQA_WIDTH] -
                           (int)luma[index - FVQA_WIDTH];
            if (horizontal < 0) {
                horizontal = -horizontal;
            }
            if (vertical < 0) {
                vertical = -vertical;
            }
            const uint16_t magnitude =
                (uint16_t)(horizontal + vertical);
            const int cell =
                (y / 4) * FVQA_SMALL_WIDTH + (x / 4);
            if (magnitude > pooled[cell]) {
                pooled[cell] = magnitude;
            }
        }
    }
    /* Rank threshold: keep the top quarter of cells with strength > 3,
     * found via a histogram so no sorting or allocation is needed. */
    uint16_t histogram[511] = {0};
    int nonzero = 0;
    for (int index = 0; index < FVQA_SMALL_PIXELS; index++) {
        if (pooled[index] > 3U) {
            histogram[pooled[index]]++;
            nonzero++;
        }
    }
    const int keep = nonzero / 4;
    int threshold = 4;
    int seen = 0;
    for (int value = 510; value >= 4; value--) {
        seen += histogram[value];
        if (seen >= keep) {
            threshold = value;
            break;
        }
    }
    for (int word = 0; word < FVQA_SIG_WORDS; word++) {
        signature[word] = 0;
    }
    if (keep == 0) {
        return;
    }
    for (int index = 0; index < FVQA_SMALL_PIXELS; index++) {
        if (pooled[index] > 3U && pooled[index] >= threshold) {
            signature[index / 32] |= 1U << (index % 32);
        }
    }
}

uint32_t fvqa_signature_distance_permille(
    const uint32_t *a, const uint32_t *b)
{
    uint32_t union_bits = 0;
    uint32_t symmetric_difference = 0;
    for (int word = 0; word < FVQA_SIG_WORDS; word++) {
        union_bits += fvqa_popcount32(a[word] | b[word]);
        symmetric_difference += fvqa_popcount32(a[word] ^ b[word]);
    }
    if (union_bits == 0U) {
        return 0U;
    }
    return (symmetric_difference * 1000U) / union_bits;
}
