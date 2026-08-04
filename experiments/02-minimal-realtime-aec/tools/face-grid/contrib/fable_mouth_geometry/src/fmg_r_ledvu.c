#include "fmg_internal.h"

/*
 * LED VU mouth: a KITT-style voicebox — three columns of red bar
 * segments extending symmetrically up and down from a center line, the
 * middle column taller than its flanks. Column heights follow mouth
 * openness (sides weighted by width), rounding pulls the columns
 * together, and idle mode runs a slow scanner sweep homage. Eyes are
 * 5x3 amber LED clusters with a punched pupil; everything sits on a
 * dark bezel with corner screws.
 */

#define VU_BG FMG_RGB565(8, 8, 10)
#define BEZEL FMG_RGB565(44, 44, 50)
#define BEZEL_EDGE FMG_RGB565(70, 70, 78)
#define SEG_OFF FMG_RGB565(58, 12, 10)
#define SEG_ON FMG_RGB565(248, 44, 30)
#define SEG_HOT FMG_RGB565(255, 120, 60)
#define AMBER FMG_RGB565(255, 176, 32)
#define AMBER_DIM FMG_RGB565(96, 66, 20)

static void fmg_vu_segment(
    uint16_t *px, int32_t cx, int32_t cy, uint16_t color, bool hot)
{
    fmg_fill_round_rect(px, cx - 7, cy - 2, 14, 5, 2, color);
    if (hot) {
        fmg_fill_rect(px, cx - 4, cy - 1, 8, 2, SEG_HOT);
    }
}

void fmg_render_ledvu(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    fmg_fill(px, VU_BG);
    fmg_fill_round_rect(px, 6, 6, 148, 108, 8, BEZEL);
    fmg_fill_round_rect(px, 9, 9, 142, 102, 6, VU_BG);
    /* corner screws */
    fmg_fill_ellipse(px, 13, 13, 2, 2, BEZEL_EDGE);
    fmg_fill_ellipse(px, 147, 13, 2, 2, BEZEL_EDGE);
    fmg_fill_ellipse(px, 13, 107, 2, 2, BEZEL_EDGE);
    fmg_fill_ellipse(px, 147, 107, 2, 2, BEZEL_EDGE);

    /* amber LED cluster eyes, 5x3, pupil = unlit hole */
    int32_t pdx = fmg_clampi(
        (int32_t)kf->look_x * 2 / 96 + (idle.gaze_dx_q8 >> 9), -2, 2);
    int32_t pdy = fmg_clampi((int32_t)kf->look_y / 96, -1, 1);
    for (int side = 0; side < 2; side++) {
        int32_t ex = (side == 0 ? 50 : 110);
        int32_t ey = 38;
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t rows_on = (lid * 3 + 200) >> 8; /* 0..3 */
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        int32_t brow_dy = fmg_clampi(brow >> 9, -2, 2);
        for (int32_t r = -1; r <= 1; r++) {
            for (int32_t c = -2; c <= 2; c++) {
                bool lit = (r + 2) <= rows_on;
                bool pupil = lit && r == pdy && (c == pdx || c == pdx + 1);
                uint16_t color = !lit ? AMBER_DIM
                                 : (pupil ? AMBER_DIM : AMBER);
                fmg_fill_ellipse(px, ex + c * 6, ey + r * 6 + brow_dy, 2, 2,
                                 color);
            }
        }
    }

    /* voicebox columns */
    int32_t spacing = 24 - ((mouth.round_q8 * 7) >> 8) +
                      ((mouth.width_q8 - 128) * 5 / 128);
    spacing = fmg_clampi(spacing, 16, 30);
    int32_t cy = 86;
    int32_t side_gain = 100 + ((mouth.width_q8 * 100) >> 8); /* 100..200 */

    /* idle scanner sweep when silent */
    int32_t scan = -1;
    if (!mouth.speaking && mouth.open_q8 < 16) {
        uint32_t sp = clock % 128000; /* every 8 s */
        if (sp < 24000) {
            int32_t ph = (int32_t)(sp * 2 / 3000); /* 0..15 */
            scan = ph < 8 ? ph : 15 - ph; /* 0..7 out and back */
        }
    }

    for (int col = -1; col <= 1; col++) {
        int32_t cx = 80 + col * spacing;
        int32_t max_segs = col == 0 ? 5 : 4; /* per half, mirrored */
        int32_t level_q8 = mouth.open_q8;
        if (col != 0) {
            level_q8 = (level_q8 * side_gain) / 200;
        }
        if (mouth.press_q8 > 150) {
            level_q8 = 0;
        }
        int32_t lit = (level_q8 * max_segs + 128) >> 8;
        if (mouth.speaking && level_q8 > 32) {
            /* deterministic per-column shimmer */
            uint32_t h = fmg_hash((clock >> 10) ^ (uint32_t)(col + 7));
            lit += (int32_t)(h % 3U) - 1;
        }
        lit = fmg_clampi(lit, mouth.speaking ? 1 : 0, max_segs);
        for (int32_t i = 0; i < max_segs; i++) {
            bool on = i < lit;
            if (scan >= 0) {
                /* sweep runs one segment behind on the flanks */
                on = col == 0 ? i == scan : (scan >= 1 && i == scan - 1);
            }
            uint16_t color = on ? SEG_ON : SEG_OFF;
            bool hot = on && i == lit - 1 && scan < 0;
            /* center line segment plus mirrored up/down stack */
            if (i == 0) {
                fmg_vu_segment(px, cx, cy, on ? SEG_ON : SEG_OFF, hot);
            } else {
                fmg_vu_segment(px, cx, cy - i * 7, color, hot);
                fmg_vu_segment(px, cx, cy + i * 7, color, false);
            }
        }
    }

    /* breathing power LED */
    int32_t pulse = 128 + (idle.breath_q8 / 3);
    fmg_fill_ellipse(px, 140, 100, 2, 2,
                     fmg_blend565(VU_BG, SEG_ON, fmg_clampi(pulse, 40, 256)));
}
