#include "wc_common.h"

/*
 * Wayang lamp: shadow-puppet theatre on a cotton screen.
 *
 * A warm oil lamp behind the screen builds the light field (a per-frame
 * radial LUT with deterministic flame flicker); the puppet is a perforated
 * leather silhouette in front of it. The jaw is a hinged flap the way a real
 * rod puppet's is: opening the mouth drops the whole chin trapezoid, and the
 * gap that opens between the upper lip and the jaw lets raw lamp light pour
 * out — the voice is literally light escaping the mouth. Eyes are punched
 * almond holes with a floating silhouette pupil, brows are punched slits,
 * and the cheeks, forehead and crown carry hash-seeded but permanent
 * filigree punchwork like tooled leather. The puppet sways on its rod with
 * breathing and follow-through from the shared rig; the leather passes a few
 * percent of the light, so the silhouette glows faintly red-brown.
 */

enum {
    WC_WY_LAMP_X = 80,
    WC_WY_LAMP_Y = 56,
};

/* silhouette opacity 0..256 at (dx, dy) relative to the face center */
static int32_t wc_wy_sil(const wc_rig_t *rig, int32_t dx, int32_t dy,
                         int32_t drop, int32_t *mouth_gap) {
    *mouth_gap = 0;

    /* face oval plus crown; early reject far outside */
    if (dx < -58 || dx > 58 || dy < -62 || dy > 66) {
        return 0;
    }
    static const int32_t inv_fa2 = (1 << 18) / (42 * 42);
    static const int32_t inv_fb2 = (1 << 18) / (48 * 48);
    int32_t fq = wc_ellipse_q10(dx, dy, inv_fa2, inv_fb2);
    int32_t sil = 0;

    /* jaw zone: hinged flap */
    if (dy > 17 && dx > -26 && dx < 26) {
        int32_t jy = dy - 17;
        if (jy < drop) {
            if (fq < 900) {
                *mouth_gap = 256;
            }
            /* hanging teeth comb at the upper lip */
            if (rig->mouth_teeth > 100 && jy < 4 && ((dx >> 2) & 1) == 0 &&
                wc_abs32(dx) < 18) {
                return 256;
            }
            return 0;
        }
        int32_t jj = jy - drop; /* coordinate inside the dropped jaw */
        int32_t half = 24 - jj / 2;
        if (jj < 30 && wc_abs32(dx) < half) {
            sil = 256;
            /* chin filigree dot */
            if (jj > 8 && ((wc_hash2((uint32_t)((dx + 200) >> 3),
                                     (uint32_t)(jj >> 3) + 77u) &
                            7u) == 3u)) {
                int32_t lx = (dx + 200) & 7;
                int32_t ly = jj & 7;
                if ((lx - 4) * (lx - 4) + (ly - 4) * (ly - 4) < 3) {
                    sil = 30;
                }
            }
            return sil;
        }
        /* outside the flap but inside the oval: cheeks continue */
        if (fq >= 1024) {
            return 0;
        }
    }

    if (fq < 1024) {
        sil = 256;
    } else if (fq < 1150) {
        sil = ((1150 - fq) << 8) / 126; /* soft leather edge */
    }

    /* crown: scalloped band above the forehead with punched merlons */
    if (dy < -24 && dy > -62) {
        int32_t tri = wc_abs32(((dx * 9 + 2048) & 63) - 32); /* 0..32 */
        int32_t limit = 1500 + (tri < 9 ? 360 : 0);
        if (fq >= 1024 && fq < limit) {
            sil = wc_max32(sil, 256);
            /* crown jewel holes at the merlon centers */
            if (tri < 4 && fq > 1150 && fq < 1420) {
                sil = 20;
            }
        }
    }

    /* shoulders */
    if (dy > 40) {
        int32_t half = 26 + (dy - 40) * 3;
        if (wc_abs32(dx) < half) {
            sil = wc_max32(sil, wc_min32(256, (dy - 40) * 64));
        }
    }

    if (sil <= 0) {
        return 0;
    }

    /* punched features (holes in the leather) */
    /* eyes: almond holes, lid squashes them shut */
    for (int e = 0; e < 2; ++e) {
        int32_t ex = e == 0 ? -WC_EYE_DX + 6 : WC_EYE_DX - 6;
        int32_t lid = e == 0 ? rig->lid_l : rig->lid_r;
        int32_t edx = dx - ex;
        int32_t edy = dy - (-14);
        if (wc_abs32(edx) > 15 || wc_abs32(edy) > 10) {
            continue;
        }
        int32_t b = wc_max32(1, 7 * lid >> 8);
        int32_t q = wc_ellipse_q10(edx, edy, (1 << 18) / (11 * 11),
                                   (1 << 18) / (b * b));
        if (q < 1024) {
            int32_t hole = wc_cov_from_q10(q, 180);
            /* floating pupil */
            int32_t pdx = edx - (rig->gaze_x * 4 >> 8);
            int32_t pdy = edy - (rig->gaze_y * 3 >> 8);
            if (pdx * pdx + pdy * pdy < 3 * 3 && b > 3) {
                return 256;
            }
            sil = wc_min32(sil, 256 - hole);
        }
        /* brow slit above the eye */
        int32_t tilt = (e == 0 ? -rig->brow_q8 : rig->brow_q8);
        int32_t by = -26 - (rig->brow_q8 * 6 >> 8) + (edx * tilt >> 10) +
                     (edx * edx / 20);
        if (wc_abs32(edx) < 10 && wc_abs32(dy - by) < 2) {
            sil = wc_min32(sil, 40);
        }
    }

    /* nostril punches */
    if (dy >= 8 && dy <= 10 && (wc_abs32(dx - 5) < 2 || wc_abs32(dx + 5) < 2)) {
        sil = wc_min32(sil, 60);
    }

    /* forehead + cheek filigree: permanent hash-seeded punchwork */
    if ((dy > -34 && dy < -22) ||
        (dy > -4 && dy < 16 && wc_abs32(dx) > 16 && wc_abs32(dx) < 38)) {
        uint32_t cx = (uint32_t)(dx + 200) / 6u;
        uint32_t cy = (uint32_t)(dy + 200) / 6u;
        if ((wc_hash2(cx, cy * 3u + 11u) & 3u) == 1u) {
            int32_t lx = (dx + 200) % 6 - 3;
            int32_t ly = (dy + 200) % 6 - 3;
            if (lx * lx + ly * ly < 4) {
                sil = wc_min32(sil, 26);
            }
        }
    }

    return sil;
}

void wc_render_wayang_lamp(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)kf;
    /* lamp LUT indexed by r*r >> 6, with flame flicker */
    uint16_t lamp[176];
    int32_t flick = 15200 + (rig->flick_q14 >> 3) +
                    ((wc_sin_q14(clock * 27u) + wc_sin_q14(clock * 41u + 9000u)) >> 5);
    for (int32_t i = 0; i < 176; ++i) {
        /* falloff: bright core, warm mids, dark corners */
        int32_t d = i << 6; /* r^2 */
        int32_t bright = (int32_t)((11000L << 14) / (11000 + d * 3));
        bright = bright * flick >> 14;
        int32_t r = 255 * bright >> 14;
        /* green and blue fall off faster than red: warm core, ember edges */
        int32_t g = (210 * bright >> 14) * (12000 + (bright >> 2)) >> 14;
        int32_t b = (130 * bright >> 14) * bright >> 14;
        lamp[i] = wc_rgb565((uint32_t)wc_clamp32(r, 10, 255),
                            (uint32_t)wc_clamp32(g, 6, 255),
                            (uint32_t)wc_clamp32(b, 4, 255));
    }

    int32_t head_x = rig->head_x_q8 * 2 >> 8;
    int32_t head_y = (rig->head_y_q8 >> 8) + (rig->breath_q14 >> 13);
    int32_t tilt = rig->head_x_q8 / 3; /* rod sway shear, Q8 per row */
    int32_t drop = 1 + ((int32_t)rig->mouth_open * 15 >> 8);
    drop = drop * (256 - (int32_t)rig->mouth_press / 2) >> 8;

    int32_t glow_boost = (int32_t)rig->energy * 3;

    for (int32_t y = 0; y < WC_FACE_HEIGHT; ++y) {
        uint16_t *row = fb + (size_t)y * WC_FACE_WIDTH;
        int32_t ly = y - WC_WY_LAMP_Y;
        int32_t sway = (y - 40) * tilt >> 11;
        for (int32_t x = 0; x < WC_FACE_WIDTH; ++x) {
            int32_t lx = x - WC_WY_LAMP_X;
            uint32_t r2 = (uint32_t)(lx * lx + ly * ly);
            uint16_t light = lamp[r2 >> 6];

            /* cotton screen weave */
            if (((x + y) & 3) == 0) {
                light = wc_mix565(light, wc_rgb565(0, 0, 0), 20);
            }

            int32_t dx = x - WC_CENTER_X - head_x - sway;
            int32_t dy = y - 62 - head_y;
            int32_t gap;
            int32_t sil = wc_wy_sil(rig, dx, dy, drop, &gap);

            uint16_t out;
            if (gap && sil == 0) {
                /* voice pours out: over-bright warm core in the mouth gap */
                out = wc_mix565(light, wc_rgb565(255, 240, 200),
                                (uint32_t)wc_clamp32(170 + glow_boost, 0, 256));
            } else if (sil > 0) {
                /* leather passes ~7% and tints red-brown */
                uint16_t leather = wc_mix565(
                    wc_rgb565(24, 9, 5), light, 18);
                out = wc_mix565(light, leather, (uint32_t)sil);
            } else {
                out = light;
            }
            row[x] = out;
        }
    }
}
