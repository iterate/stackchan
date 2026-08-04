#include "fmg_internal.h"

/*
 * JALI-style jaw/lip polygon mouth (Edwards et al., SIGGRAPH 2016). The
 * mouth is rebuilt every frame from the two aggregate axes the paper
 * identifies — JA (jaw drop, gated by bilabial closure) and LI (lip
 * articulation) — as four quadratic bezier envelopes evaluated per column.
 * Upper teeth stay pinned to the skull while lower teeth ride the jaw,
 * the anatomy detail that makes polygon mouths read as real speech.
 */

#define SKIN_HI FMG_RGB565(242, 208, 178)
#define SKIN_LO FMG_RGB565(216, 172, 140)
#define SKIN_SHADE FMG_RGB565(198, 150, 120)
#define LIP FMG_RGB565(186, 96, 88)
#define LIP_DARK FMG_RGB565(140, 62, 60)
#define MOUTH_IN FMG_RGB565(66, 24, 26)
#define TEETH FMG_RGB565(248, 244, 230)
#define TEETH_SEAM FMG_RGB565(212, 202, 184)
#define TONGUE FMG_RGB565(214, 110, 110)
#define IRIS FMG_RGB565(96, 66, 40)

static void fmg_jali_eye(
    uint16_t *px, int32_t cx, int32_t cy, int32_t lid_q8, int32_t look_dx,
    int32_t look_dy, uint16_t skin)
{
    int32_t ry = (9 * fmg_clampi(lid_q8, 0, 280)) >> 8;
    if (ry <= 1) {
        fmg_fill_rect(px, cx - 10, cy - 1, 21, 2, FMG_RGB565(96, 62, 44));
        return;
    }
    fmg_fill_ellipse(px, cx, cy, 12, ry + 1, FMG_RGB565(120, 84, 60));
    fmg_fill_ellipse(px, cx, cy, 11, ry, FMG_RGB565(250, 248, 242));
    int32_t pdx = fmg_clampi(look_dx, -6, 6);
    int32_t pdy = fmg_clampi(look_dy, -(ry - 1), ry - 1);
    int32_t ir = ry < 5 ? ry : 5;
    fmg_fill_ellipse(px, cx + pdx, cy + pdy, 5, ir, IRIS);
    fmg_fill_ellipse(px, cx + pdx, cy + pdy, 2, ir < 2 ? ir : 2,
                     FMG_RGB565(24, 16, 12));
    fmg_pixel(px, cx + pdx - 1, cy + pdy - 1, FMG_RGB565(255, 255, 255));
    /* upper lid casts a soft shadow */
    fmg_fill_rect_blend(px, cx - 11, cy - ry, 23, 2, skin, 90);
}

void fmg_render_jali(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;

    /* vertical skin gradient */
    for (int32_t y = 0; y < FMG_HEIGHT; y++) {
        uint16_t c = fmg_blend565(SKIN_HI, SKIN_LO, y * 256 / FMG_HEIGHT);
        fmg_hline(px, 0, FMG_WIDTH - 1, y, c);
    }

    /* cheek shading */
    fmg_fill_ellipse_blend(px, 30 + sway, 78 + breath, 12, 8, SKIN_SHADE, 70);
    fmg_fill_ellipse_blend(px, 130 + sway, 78 + breath, 12, 8, SKIN_SHADE,
                           70);

    int32_t look_dx = (int32_t)kf->look_x * 7 / 128 + (idle.gaze_dx_q8 >> 8);
    int32_t look_dy = (int32_t)kf->look_y * 5 / 128 + (idle.gaze_dy_q8 >> 8);
    int32_t eye_y = 44 + breath;
    fmg_jali_eye(px, 52 + sway, eye_y, idle.lid_l_q8, look_dx, look_dy,
                 SKIN_LO);
    fmg_jali_eye(px, 108 + sway, eye_y, idle.lid_r_q8, look_dx, look_dy,
                 SKIN_LO);

    /* tapered brows */
    int32_t bl = eye_y - 16 + (idle.brow_l_q8 >> 8);
    int32_t br = eye_y - 16 + (idle.brow_r_q8 >> 8);
    for (int i = 0; i < 3; i++) {
        fmg_line(px, 40 + sway, bl + 2 + i, 56 + sway, bl + i,
                 FMG_RGB565(110, 74, 50));
        fmg_line(px, 56 + sway, bl + i, 64 + sway, bl + 2 + i,
                 FMG_RGB565(110, 74, 50));
        fmg_line(px, 96 + sway, br + 2 + i, 104 + sway, br + i,
                 FMG_RGB565(110, 74, 50));
        fmg_line(px, 104 + sway, br + i, 120 + sway, br + 2 + i,
                 FMG_RGB565(110, 74, 50));
    }

    /* nose */
    fmg_fill_ellipse_blend(px, 80 + sway, 64 + breath, 6, 5, SKIN_SHADE, 120);
    fmg_fill_ellipse(px, 76 + sway, 66 + breath, 1, 1, LIP_DARK);
    fmg_fill_ellipse(px, 84 + sway, 66 + breath, 1, 1, LIP_DARK);

    /* ---- the JALI mouth ---- */
    int32_t jaw = mouth.jaw_q8;
    int32_t lip = mouth.lip_q8;
    int32_t mx = 80 + sway;
    int32_t my = 86 + breath + ((jaw * 3) >> 8);

    int32_t w = 20 + ((mouth.width_q8 * 16) >> 8) - ((mouth.round_q8 * 12) >> 8);
    w = fmg_clampi(w, 11, 38);
    /* smiling corners lift, puckered corners drop */
    int32_t corner_dy = -((mouth.width_q8 - 128) * 5 / 128) +
                        ((mouth.round_q8 * 3) >> 8);
    int32_t corner_y = my + corner_dy;

    int32_t up_th = 4 + ((lip * 3) >> 8);
    int32_t lo_th = 5 + ((lip * 4) >> 8);
    int32_t open_up = (jaw * 7) >> 8;
    int32_t open_dn = 2 + ((jaw * 24) >> 8);

    int32_t teeth_up = mouth.teeth_q8 > 30 ? 3 + ((mouth.teeth_q8 * 5) >> 8)
                                           : 0;
    int32_t teeth_dn = mouth.teeth_q8 > 90 ? 2 + ((mouth.teeth_q8 * 3) >> 8)
                                           : 0;
    bool tongue_up = mouth.vis == FMG_VIS_LN;
    bool tongue_low = jaw > 90 && mouth.teeth_q8 < 60 && !tongue_up;

    /* philtrum */
    fmg_vspan_blend(px, mx, my - up_th - open_up - 7, my - up_th - open_up - 3,
                    SKIN_SHADE, 130);

    for (int32_t x = mx - w; x <= mx + w; x++) {
        int32_t t = (x - (mx - w)) * 256 / (2 * w);
        int32_t y_ot = fmg_qbez_q4((corner_y - 1) << 4,
                                   (my - 2 - open_up - up_th) << 4,
                                   (corner_y - 1) << 4, t) >> 4;
        int32_t y_it = fmg_qbez_q4(corner_y << 4, (my - 1 - open_up) << 4,
                                   corner_y << 4, t) >> 4;
        int32_t y_ib = fmg_qbez_q4(corner_y << 4, (my + open_dn) << 4,
                                   corner_y << 4, t) >> 4;
        int32_t y_ob = fmg_qbez_q4((corner_y + 1) << 4,
                                   (my + open_dn + lo_th) << 4,
                                   (corner_y + 1) << 4, t) >> 4;
        if (y_it > y_ib) {
            y_it = y_ib = (y_it + y_ib) / 2;
        }
        /* lips */
        fmg_vspan(px, x, y_ot, y_it, LIP);
        fmg_vspan(px, x, y_ot, y_ot + (y_it - y_ot) / 3, LIP_DARK);
        fmg_vspan(px, x, y_ib, y_ob, LIP);
        fmg_vspan(px, x, y_ob - (y_ob - y_ib) / 4, y_ob, LIP_DARK);
        /* interior with anatomy */
        if (y_ib > y_it + 1) {
            fmg_vspan(px, x, y_it + 1, y_ib - 1, MOUTH_IN);
            if (teeth_up > 0) {
                int32_t tb = y_it + teeth_up;
                if (tb > y_ib - 1) {
                    tb = y_ib - 1;
                }
                uint16_t tc = ((x - mx + 40) % 6 == 0) ? TEETH_SEAM : TEETH;
                fmg_vspan(px, x, y_it + 1, tb, tc);
            }
            if (teeth_dn > 0 && y_ib - teeth_dn > y_it + teeth_up + 1) {
                uint16_t tc = ((x - mx + 43) % 6 == 0) ? TEETH_SEAM : TEETH;
                fmg_vspan(px, x, y_ib - teeth_dn, y_ib - 1, tc);
            }
        }
        /* outline */
        fmg_pixel_blend(px, x, y_ot - 1, LIP_DARK, 120);
        fmg_pixel_blend(px, x, y_ob + 1, LIP_DARK, 120);
    }

    /* tongue: raised for L/N, resting hump for open vowels */
    if (tongue_up && open_dn > 6) {
        fmg_fill_ellipse(px, mx, my + 1, (w * 2) / 5, 3 + ((jaw * 3) >> 8),
                         TONGUE);
    } else if (tongue_low && open_dn > 8) {
        fmg_fill_ellipse(px, mx, my + open_dn - 3, (w * 3) / 5,
                         2 + ((jaw * 4) >> 8), TONGUE);
    }

    /* chin crease follows the jaw */
    fmg_fill_ellipse_blend(px, mx, my + open_dn + lo_th + 7, 10, 3,
                           SKIN_SHADE, 110);

    /* nasolabial folds appear on wide smiles */
    if (mouth.width_q8 > 190) {
        fmg_line(px, mx - w - 3, my - 10, mx - w + 1, corner_y - 2,
                 SKIN_SHADE);
        fmg_line(px, mx + w + 3, my - 10, mx + w - 1, corner_y - 2,
                 SKIN_SHADE);
    }
}
