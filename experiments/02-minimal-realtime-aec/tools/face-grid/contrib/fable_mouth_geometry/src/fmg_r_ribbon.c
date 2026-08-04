#include "fmg_internal.h"

/*
 * Bezier ribbon lips: stage-show glossy lips floating on a dark set —
 * the mouth *is* the face. The upper lip is two quadratic ribbons meeting
 * in a cupid's bow, the lower lip one full-width ribbon; both are shaded
 * along their thickness per column and finished with a specular sweep,
 * teeth glint, and corner dimples. Two minimal crescent eyes keep the
 * idle life (blink, gaze, brow) present.
 */

#define STAGE_BG FMG_RGB565(18, 12, 22)
#define SPOT FMG_RGB565(38, 26, 44)
#define LIP_LIGHT FMG_RGB565(244, 96, 120)
#define LIP_MID FMG_RGB565(198, 54, 84)
#define LIP_DEEP FMG_RGB565(126, 24, 52)
#define MOUTH_IN FMG_RGB565(44, 10, 22)
#define TEETH FMG_RGB565(250, 246, 240)
#define GLINT FMG_RGB565(255, 236, 244)

static void fmg_ribbon_eye(
    uint16_t *px, int32_t cx, int32_t cy, int32_t lid_q8, int32_t look_dx,
    int32_t brow_q8)
{
    int32_t by = cy - 8 + (brow_q8 >> 8);
    /* crescent: bright arc whose opening follows the lid */
    int32_t ry = 2 + ((6 * fmg_clampi(lid_q8, 0, 256)) >> 8);
    for (int32_t dy = -ry; dy <= 0; dy++) {
        int32_t half =
            (int32_t)(9 * fmg_isqrt((uint32_t)(ry * ry - dy * dy))) /
            (ry > 0 ? ry : 1);
        fmg_pixel_blend(px, cx - half + look_dx / 2, cy + dy, GLINT, 200);
        fmg_pixel_blend(px, cx + half + look_dx / 2, cy + dy, GLINT, 200);
        if (dy == -ry) {
            fmg_hline_blend(px, cx - half + look_dx / 2,
                            cx + half + look_dx / 2, cy + dy, GLINT, 180);
        }
    }
    fmg_hline_blend(px, cx - 6, cx + 6, by, LIP_LIGHT, 90);
}

void fmg_render_ribbon(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;

    fmg_fill(px, STAGE_BG);
    fmg_fill_ellipse_blend(px, 80 + sway, 74 + breath, 66, 44, SPOT, 150);

    int32_t look_dx = (int32_t)kf->look_x * 6 / 128 + (idle.gaze_dx_q8 >> 8);
    fmg_ribbon_eye(px, 56 + sway, 30 + breath, idle.lid_l_q8, look_dx,
                   idle.brow_l_q8);
    fmg_ribbon_eye(px, 104 + sway, 30 + breath, idle.lid_r_q8, look_dx,
                   idle.brow_r_q8);

    int32_t jaw = mouth.jaw_q8;
    int32_t mx = 80 + sway;
    int32_t my = 76 + breath;

    int32_t w = 42 + ((mouth.width_q8 * 10) >> 8) - ((mouth.round_q8 * 18) >> 8);
    w = fmg_clampi(w, 20, 54);
    int32_t corner_dy = -((mouth.width_q8 - 128) * 6 / 128) +
                        ((mouth.round_q8 * 4) >> 8);
    int32_t cy_l = my + corner_dy;
    int32_t up_th = 9 + ((mouth.lip_q8 * 4) >> 8);
    int32_t lo_th = 13 + ((mouth.round_q8 * 5) >> 8);
    int32_t gap = (jaw * 30) >> 8;
    int32_t bow_dip = my - 4; /* cupid's bow center dip */
    int32_t peak = my + 1 - up_th - ((mouth.round_q8 * 5) >> 8);

    for (int32_t x = mx - w; x <= mx + w; x++) {
        bool left = x < mx;
        int32_t span = w;
        int32_t t = left ? (x - (mx - w)) * 256 / span
                         : (x - mx) * 256 / span;
        /* upper outer: corner -> peak -> center dip (mirrored halves) */
        int32_t y_ot = left
            ? fmg_qbez_q4((cy_l - 2) << 4, peak << 4, bow_dip << 4, t) >> 4
            : fmg_qbez_q4(bow_dip << 4, peak << 4, (cy_l - 2) << 4, t) >> 4;
        int32_t y_it = left
            ? fmg_qbez_q4(cy_l << 4, (my + 1) << 4, (my + 2) << 4, t) >> 4
            : fmg_qbez_q4((my + 2) << 4, (my + 1) << 4, cy_l << 4, t) >> 4;
        /* lower curves span the full width with the mirrored parameter */
        int32_t tm = (x - (mx - w)) * 256 / (2 * w);
        int32_t y_ib = fmg_qbez_q4(cy_l << 4, (my + 2 + gap) << 4,
                                   cy_l << 4, tm) >> 4;
        int32_t y_ob = fmg_qbez_q4((cy_l + 2) << 4,
                                   (my + 4 + gap + lo_th) << 4,
                                   (cy_l + 2) << 4, tm) >> 4;
        if (y_it > y_ib) {
            y_it = y_ib = (y_it + y_ib) / 2;
        }
        /* upper ribbon shaded along thickness */
        int32_t th_u = y_it - y_ot;
        for (int32_t y = y_ot; y <= y_it; y++) {
            int32_t f = th_u > 0 ? (y - y_ot) * 256 / th_u : 0;
            fmg_pixel(px, x, y, fmg_blend565(LIP_DEEP, LIP_LIGHT, f));
        }
        /* interior + teeth glint */
        if (y_ib > y_it + 1) {
            fmg_vspan(px, x, y_it + 1, y_ib - 1, MOUTH_IN);
            if (mouth.teeth_q8 > 40) {
                int32_t tb = y_it + 1 + ((mouth.teeth_q8 * 5) >> 8);
                if (tb > y_ib - 1) {
                    tb = y_ib - 1;
                }
                fmg_vspan_blend(px, x, y_it + 1, tb, TEETH,
                                ((x - mx + 60) % 7 == 0) ? 140 : 230);
            }
        }
        /* lower ribbon: light top rolling to deep bottom */
        int32_t th_l = y_ob - y_ib;
        for (int32_t y = y_ib; y <= y_ob; y++) {
            int32_t f = th_l > 0 ? (y - y_ib) * 256 / th_l : 0;
            uint16_t c = f < 96 ? fmg_blend565(LIP_LIGHT, LIP_MID, f * 2)
                                : fmg_blend565(LIP_MID, LIP_DEEP,
                                               (f - 96) * 256 / 160);
            fmg_pixel(px, x, y, c);
        }
        /* specular sweep on the lower lip */
        int32_t rel = x - (mx - w / 3);
        if (rel >= 0 && rel < w / 2 && th_l > 5) {
            fmg_pixel_blend(px, x, y_ib + th_l / 4, GLINT, 190);
            fmg_pixel_blend(px, x, y_ib + th_l / 4 + 1, GLINT, 110);
        }
    }

    /* corner dimples */
    fmg_fill_ellipse_blend(px, mx - w - 2, cy_l, 2, 2, MOUTH_IN, 160);
    fmg_fill_ellipse_blend(px, mx + w + 2, cy_l, 2, 2, MOUTH_IN, 160);

    /* sparkle that twinkles deterministically while speaking */
    if (mouth.speaking || mouth.round_q8 > 140) {
        int32_t tw = fmg_sin_q14((uint16_t)(clock * 3)) >> 8; /* -64..64 */
        int32_t a = 150 + tw * 3 / 2;
        int32_t sx_ = mx - w / 2;
        int32_t sy_ = my + 6 + gap / 3;
        fmg_pixel_blend(px, sx_, sy_, GLINT, a);
        fmg_pixel_blend(px, sx_ - 2, sy_, GLINT, a / 2);
        fmg_pixel_blend(px, sx_ + 2, sy_, GLINT, a / 2);
        fmg_pixel_blend(px, sx_, sy_ - 2, GLINT, a / 2);
        fmg_pixel_blend(px, sx_, sy_ + 2, GLINT, a / 2);
    }
}
