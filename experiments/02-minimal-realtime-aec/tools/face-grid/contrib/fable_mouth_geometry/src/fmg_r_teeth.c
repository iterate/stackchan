#include "fmg_internal.h"

/*
 * Teeth & tongue closeup: an extreme "talkie closeup" where the mouth
 * interior fills most of the frame. Correct dental occlusion — the upper
 * tooth row hangs from the skull and never moves, the lower row rides
 * the jaw — plus gum lines, per-tooth rounding, a grooved tongue that
 * lifts for L/N, a throat gradient, and a uvula that swings on wide
 * vowels. Two squinting eyes peek from the top edge for idle life.
 */

#define SKIN FMG_RGB565(230, 184, 152)
#define SKIN_DK FMG_RGB565(206, 156, 126)
#define LIP FMG_RGB565(176, 88, 82)
#define LIP_DK FMG_RGB565(128, 56, 56)
#define GUM FMG_RGB565(196, 96, 96)
#define TOOTH FMG_RGB565(250, 246, 232)
#define TOOTH_SHADE FMG_RGB565(222, 212, 190)
#define THROAT FMG_RGB565(52, 16, 20)
#define THROAT_DEEP FMG_RGB565(28, 8, 12)
#define TONGUE FMG_RGB565(216, 112, 112)
#define TONGUE_DK FMG_RGB565(178, 82, 88)

void fmg_render_teeth(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;
    int32_t jaw = mouth.jaw_q8;

    fmg_fill(px, SKIN);

    /* squinting eyes at the top edge keep the idle alive */
    for (int side = 0; side < 2; side++) {
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t ecx = (side == 0 ? 34 : 126) + sway;
        int32_t ry = 1 + ((3 * fmg_clampi(lid, 0, 256)) >> 8);
        fmg_fill_ellipse(px, ecx, 8 + breath, 9, ry, FMG_RGB565(64, 40, 32));
        if (ry > 2) {
            fmg_fill_ellipse(px, ecx + (idle.gaze_dx_q8 >> 9), 8 + breath, 3,
                             ry - 1, FMG_RGB565(20, 12, 10));
        }
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        fmg_fill_rect(px, ecx - 8, 1 + breath + (brow >> 9), 17, 2,
                      FMG_RGB565(120, 80, 56));
    }

    int32_t mx = 80 + sway;
    int32_t my = 64 + breath;
    int32_t w = 62 + ((mouth.width_q8 - 128) * 8 / 128) -
                ((mouth.round_q8 * 22) >> 8);
    w = fmg_clampi(w, 34, 72);
    int32_t gap_up = 3 + ((jaw * 22) >> 8);
    int32_t gap_dn = 3 + ((jaw * 28) >> 8);
    int32_t lip_up = 11 + ((mouth.lip_q8 * 5) >> 8);
    int32_t lip_dn = 13 + ((mouth.lip_q8 * 6) >> 8);
    if (mouth.press_q8 > 150) {
        gap_up = 1;
        gap_dn = 1;
    }

    int32_t teeth_len_up = 9 + ((mouth.teeth_q8 * 7) >> 8);
    int32_t teeth_len_dn = 7 + ((mouth.teeth_q8 * 5) >> 8);
    if (jaw < 30 && mouth.teeth_q8 < 60) {
        teeth_len_up = 0; /* resting lips sit together, no teeth slit */
        teeth_len_dn = 0;
    }
    bool tongue_up = mouth.vis == FMG_VIS_LN;

    for (int32_t x = mx - w; x <= mx + w; x++) {
        int32_t t = (x - (mx - w)) * 256 / (2 * w);
        int32_t y_it = fmg_qbez_q4(my << 4, (my - gap_up) << 4, my << 4,
                                   t) >> 4;
        int32_t y_ib = fmg_qbez_q4(my << 4, (my + gap_dn) << 4, my << 4,
                                   t) >> 4;
        int32_t y_ot = fmg_qbez_q4((my - 4) << 4,
                                   (my - gap_up - lip_up) << 4,
                                   (my - 4) << 4, t) >> 4;
        int32_t y_ob = fmg_qbez_q4((my + 4) << 4,
                                   (my + gap_dn + lip_dn) << 4,
                                   (my + 4) << 4, t) >> 4;
        /* skin above/below is the base fill; lips: */
        fmg_vspan(px, x, y_ot, y_it, LIP);
        fmg_vspan(px, x, y_ot, y_ot + (y_it - y_ot) / 3, LIP_DK);
        fmg_vspan(px, x, y_ib, y_ob, LIP);
        fmg_vspan(px, x, y_ob - (y_ob - y_ib) / 4, y_ob, LIP_DK);
        fmg_pixel_blend(px, x, y_ot - 1, SKIN_DK, 160);
        fmg_pixel_blend(px, x, y_ob + 1, SKIN_DK, 160);

        if (y_ib <= y_it + 1) {
            continue; /* mouth shut at this column */
        }
        /* throat gradient: deeper toward the vertical center */
        for (int32_t y = y_it + 1; y <= y_ib - 1; y++) {
            int32_t depth = (y - y_it) * 256 / (y_ib - y_it);
            int32_t center = depth > 128 ? 256 - depth : depth;
            fmg_pixel(px, x, y,
                      fmg_blend565(THROAT, THROAT_DEEP, center * 2));
        }
        /* upper teeth: pinned to the skull (y_it), gums first */
        if (teeth_len_up > 0) {
            int32_t gum_b = y_it + 3;
            int32_t tooth_b = y_it + 3 + teeth_len_up;
            /* per-tooth rounding: shorten near tooth edges */
            int32_t phase = (x - mx + 77) % 14;
            int32_t edge = phase < 2 ? 3 : (phase > 11 ? 3 : 0);
            if (phase == 0 || phase == 13) {
                edge = 5;
            }
            if (gum_b > y_ib - 1) {
                gum_b = y_ib - 1;
            }
            if (tooth_b - edge > y_ib - 1) {
                tooth_b = y_ib - 1 + edge;
            }
            fmg_vspan(px, x, y_it + 1, gum_b, GUM);
            if (tooth_b - edge > gum_b) {
                uint16_t tc = phase == 0 || phase == 13 ? TOOTH_SHADE : TOOTH;
                fmg_vspan(px, x, gum_b, tooth_b - edge, tc);
            }
        }
        /* lower teeth: ride the jaw (y_ib) */
        if (teeth_len_dn > 0 && jaw > 40) {
            int32_t gum_t = y_ib - 3;
            int32_t tooth_t = y_ib - 3 - teeth_len_dn;
            int32_t phase = (x - mx + 70) % 12;
            int32_t edge = phase < 2 || phase > 9 ? 3 : 0;
            if (gum_t < y_it + 1) {
                gum_t = y_it + 1;
            }
            if (tooth_t + edge < y_it + 1) {
                tooth_t = y_it + 1 - edge;
            }
            fmg_vspan(px, x, gum_t, y_ib - 1, GUM);
            if (gum_t > tooth_t + edge) {
                uint16_t tc = phase == 0 || phase == 11 ? TOOTH_SHADE : TOOTH;
                fmg_vspan(px, x, tooth_t + edge, gum_t, tc);
            }
        }
    }

    /* tongue drawn over the interior, clipped by construction */
    if (gap_dn > 8) {
        int32_t tr_y = tongue_up ? my - gap_up / 2
                                 : my + gap_dn - 4 - teeth_len_dn / 2;
        int32_t trx = (w * 5) / 8;
        int32_t try_ = 4 + ((jaw * 8) >> 8);
        fmg_fill_ellipse(px, mx, tr_y + try_ / 2, trx, try_, TONGUE);
        fmg_fill_ellipse_blend(px, mx, tr_y + try_ / 2, trx - 6,
                               try_ > 3 ? try_ - 3 : 1, TONGUE_DK, 90);
        /* center groove */
        fmg_vspan_blend(px, mx, tr_y, tr_y + try_, TONGUE_DK, 150);
        fmg_fill_ellipse_blend(px, mx - trx / 2, tr_y + 1, 5, 2,
                               FMG_RGB565(244, 168, 168), 130);
    }

    /* uvula swings gently on wide-open vowels */
    if (jaw > 190 && !tongue_up) {
        int32_t swing = fmg_sin_q14((uint16_t)(clock * 2)) * 3 >> 14;
        fmg_fill_ellipse(px, mx + swing, my - gap_up + 7, 3, 5,
                         FMG_RGB565(150, 60, 66));
    }

    /* philtrum + chin dimple anchor the closeup */
    fmg_vspan_blend(px, mx, my - gap_up - lip_up - 12, my - gap_up - lip_up - 4,
                    SKIN_DK, 120);
    fmg_fill_ellipse_blend(px, mx, my + gap_dn + lip_dn + 8, 12, 4, SKIN_DK,
                           110);
}
