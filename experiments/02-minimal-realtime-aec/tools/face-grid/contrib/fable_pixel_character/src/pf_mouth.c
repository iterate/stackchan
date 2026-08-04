#include "pf_internal.h"

/*
 * Shared mouth logic.
 *
 * pf_mouth_classify() reduces the five keyframe mouth channels to one of
 * ten discrete shapes. The shape list follows the Hanna-Barbera lineage
 * six-basic/three-extended convention documented by Rhubarb Lip Sync
 * (shape *names* only; every drawing here is original), plus one extra
 * "barely open" transition shape that reads well at pixel sizes.
 *
 * pf_draw_lips() is the continuous scanline system for higher-colour
 * styles: lip curves are integer parabolas whose extents interpolate from
 * the keyframe channels, so openness/width/roundness animate smoothly.
 */

pf_mouth_shape_t pf_mouth_classify(const face_keyframe_t *k) {
    int open = k->mouth_open;
    int width = k->mouth_width;
    int round = k->mouth_round;
    int press = k->mouth_press;
    int teeth = k->mouth_teeth;

    if (press > 150 && open < 60) {
        return PF_SHAPE_MBP;
    }
    if (teeth > 150 && press > 90 && open < 110) {
        return PF_SHAPE_FV;
    }
    if (open < 28) {
        if (teeth > 110) {
            return PF_SHAPE_SS;
        }
        return PF_SHAPE_REST;
    }
    if (round > 140) {
        return open > 130 ? PF_SHAPE_OH : PF_SHAPE_OO;
    }
    if (open > 180) {
        return PF_SHAPE_AA;
    }
    if (width > 160 && open < 110) {
        return PF_SHAPE_EE;
    }
    if (open > 96) {
        return PF_SHAPE_EH;
    }
    return PF_SHAPE_SMALL;
}

void pf_draw_lips(pf_surface_t *s, const pf_lips_t *p,
                  const face_keyframe_t *k) {
    /* Width shrinks when the mouth rounds, grows with mouth_width. */
    int w = p->half_width;
    w = w + ((w * ((int)k->mouth_width - 128)) >> 9);
    w = w - ((w * (int)k->mouth_round) >> 9);
    w = pf_maxi(w, 3);

    int open = ((int)k->mouth_open * p->open_px) >> 8;
    int press = (int)k->mouth_press;
    int teeth = (int)k->mouth_teeth;
    int round = (int)k->mouth_round;

    /* Pressed lips flatten the opening. */
    open = pf_maxi(open - ((press * p->open_px) >> 9), 0);

    int w2 = w * w;
    for (int dx = -w; dx <= w; ++dx) {
        int x = p->cx + dx;
        /* Parabolic arch: 256 at centre, 0 at corners. */
        int arch = (int)(((int32_t)(w2 - dx * dx) * 256) / w2);
        /* Rounding makes the arch fuller (closer to an ellipse). */
        if (round > 96) {
            int boost = pf_lerp(arch, 256 - ((256 - arch) * arch >> 8),
                                pf_clampi((round - 96) * 2, 0, 256));
            arch = pf_clampi(boost, 0, 256);
        }
        int gap = (open * arch) >> 8;
        int y_top = p->cy - gap / 2;
        int y_bot = p->cy + (gap + 1) / 2;

        if (gap <= 0) {
            /* Closed: a single dark seam plus lip body below. */
            pf_px(s, x, p->cy, p->lip_dark);
            if (press > 100) {
                pf_px(s, x, p->cy - 1, p->lip_mid);
            }
            pf_px(s, x, p->cy + 1, p->lip_mid);
            continue;
        }

        /* Cavity, then teeth curtain from the top, tongue from the bottom. */
        pf_vline(s, x, y_top, y_bot, p->cavity);
        if (teeth > 60 && gap > 2) {
            int rows = 1 + ((teeth - 60) * (gap / 2)) / 196;
            pf_vline(s, x, y_top, y_top + pf_mini(rows, gap / 2), p->teeth);
        }
        if (gap > 5 && (dx * dx) < (w2 * 2) / 5) {
            int tongue_rows = gap / 4;
            pf_vline(s, x, y_bot - tongue_rows, y_bot, p->tongue);
        }
        /* Lip rims. */
        pf_px(s, x, y_top - 1, p->lip_dark);
        pf_px(s, x, y_bot + 1, p->lip_dark);
        pf_px(s, x, y_bot + 2, p->lip_mid);
    }
    /* Mouth corners. */
    pf_px(s, p->cx - w - 1, p->cy, p->lip_dark);
    pf_px(s, p->cx + w + 1, p->cy, p->lip_dark);
}
