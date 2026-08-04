#include "wc_common.h"

/*
 * Halftone press: a vintage comic panel straight off a two-plate press.
 *
 * The face is an analytic luminance field that never reaches the screen
 * directly: it is printed. The black plate is an amplitude-modulated dot
 * screen at the classic 45 degrees (rotation done exactly with u=x+y,
 * v=x-y), the red spot-color plate runs at ~15 degrees via an incremental
 * DDA, and the red plate is deliberately misregistered by a slowly drifting
 * offset like a worn press. Feature outlines are drawn as ink with a
 * thick-thin brush weight, highlights knock the dots out, and when the brows
 * slam down (or the analyzer reports a shout) the panel grows manga-style
 * radial emphasis lines outside the face oval.
 */

enum {
    WC_HT_P = 7,            /* black screen period in rotated units */
    WC_HT_P_Q8 = 7 * 256,   /* red screen period, Q8 */
};

typedef struct {
    int32_t face_cx, face_cy, inv_fa2, inv_fb2;
    int32_t eye_cx[2], eye_cy, inv_ea2, inv_eb2[2];
    int32_t iris_cx[2], iris_cy;
    int32_t mouth_cx, mouth_cy, inv_ma2, inv_mb2, mh;
    int32_t brow_y[2], brow_tilt[2];
    int32_t lid_top[2];
    int32_t hair_y;
    int32_t emphasis;
} wc_ht_geo_t;

/* black-plate luminance 0 (ink) .. 255 (paper) plus red-plate darkness */
static void wc_ht_field(const wc_ht_geo_t *g, const wc_rig_t *rig,
                        int32_t x, int32_t y, int32_t *lum, int32_t *red) {
    int32_t L = 250;
    int32_t R = 0;

    int32_t fdx = x - g->face_cx;
    int32_t fdy = y - g->face_cy;
    int32_t fq = 4096;
    if (wc_abs32(fdx) < 70 && wc_abs32(fdy) < 74) {
        fq = wc_ellipse_q10(fdx, fdy, g->inv_fa2, g->inv_fb2);
    }

    if (fq < 1024) {
        /* face interior: soft downward shading */
        L = 233 - (fdy > 24 ? (fdy - 24) : 0) - (fq > 820 ? (fq - 820) / 14 : 0);

        /* helmet bob hair: solid ink above the fringe zigzag */
        int32_t fringe = g->hair_y + (wc_abs32(((x * 5) & 31) - 16) >> 2);
        if (y < fringe) {
            L = 30;
        }

        /* cheeks on the red plate */
        for (int e = 0; e < 2; ++e) {
            int32_t dx = x - g->eye_cx[e];
            int32_t dy = y - (g->eye_cy + 18);
            int32_t d2 = dx * dx + dy * dy;
            if (d2 < 9 * 9) {
                R = wc_max32(R, (81 - d2) * 90 / 81);
            }
        }

        /* eyes */
        for (int e = 0; e < 2; ++e) {
            int32_t dx = x - g->eye_cx[e];
            int32_t dy = y - g->eye_cy;
            if (wc_abs32(dx) > 16 || wc_abs32(dy) > 12) {
                continue;
            }
            int32_t q = wc_ellipse_q10(dx, dy, g->inv_ea2, g->inv_eb2[e]);
            if (q < 1024) {
                L = 252; /* sclera */
                int32_t idx = x - g->iris_cx[e];
                int32_t idy = y - g->iris_cy;
                int32_t d2 = idx * idx + idy * idy;
                if (d2 < 5 * 5) {
                    L = d2 < 2 * 2 ? 6 : 60; /* pupil in iris */
                    if (idx == -1 && idy == -1) {
                        L = 250; /* catchlight */
                    }
                }
                /* upper lash line hugging the lid */
                if (dy < g->lid_top[e] + 1) {
                    L = 18;
                }
            } else if (q < 1300) {
                L = wc_min32(L, 24); /* eye outline */
            }
        }

        /* brows: thick strokes */
        for (int e = 0; e < 2; ++e) {
            int32_t dx = x - g->eye_cx[e];
            if (wc_abs32(dx) > 12) {
                continue;
            }
            int32_t by = g->brow_y[e] + (dx * g->brow_tilt[e] >> 10) +
                         (dx * dx / 26);
            int32_t d = y - by;
            if (d >= -2 && d <= 1 + (wc_abs32(dx) < 6)) {
                L = 16;
            }
        }

        /* mouth */
        int32_t mdx = x - g->mouth_cx;
        int32_t mdy = y - g->mouth_cy;
        if (wc_abs32(mdx) < 42 && wc_abs32(mdy) < 26) {
            int32_t q = wc_ellipse_q10(mdx, mdy, g->inv_ma2, g->inv_mb2);
            if (q < 1024) {
                if (g->mh > 8) {
                    L = 96; /* open interior: heavy screen, red shows through */
                    R = wc_max32(R, 150);
                    if (rig->mouth_teeth > 90 && mdy < -(g->mh >> 2)) {
                        L = 248; /* teeth band */
                        R = 0;
                    }
                    if (mdy > (g->mh >> 2)) {
                        R = 230; /* tongue */
                        L = wc_min32(L, 120);
                    }
                } else {
                    L = 26; /* closed line mouth */
                    R = wc_max32(R, 60);
                }
            } else if (q < 1024 + 200 + (mdy > 0 ? mdy * 14 : 0)) {
                L = 22; /* lip outline, heavier under the lower lip */
                R = wc_max32(R, 90);
            }
        }

        /* nose hook */
        if (x >= g->face_cx - 1 && x <= g->face_cx + 3 &&
            y >= g->face_cy + 4 && y <= g->face_cy + 10 &&
            (x - g->face_cx + 1) >= (g->face_cy + 10 - y)) {
            L = 30;
        }
    } else {
        /* face outline: brush weight thickens toward the chin */
        int32_t band = 130 + (fdy > 0 ? fdy * 5 : 0);
        if (fq < 1024 + band) {
            L = 20;
        } else if (g->emphasis) {
            /* manga emphasis lines outside the oval */
            int32_t adx = wc_abs32(fdx), ady = wc_abs32(fdy);
            int32_t den = adx + ady;
            if (den > 0) {
                /* diamond angle 0..4096 per quadrant fold */
                int32_t t = (fdy >= 0)
                                ? (fdx >= 0 ? (ady << 10) / den
                                            : 2048 - ((ady << 10) / den))
                                : (fdx < 0 ? 2048 + ((ady << 10) / den)
                                           : 4096 - ((ady << 10) / den));
                int32_t s = (t * 7) & 1023;
                int32_t jitter =
                    (int32_t)(wc_hash_u32((uint32_t)(t * 7 >> 10)) & 127u);
                if (s < 90 && fq > 1500 + jitter * 8) {
                    L = 28;
                }
            }
        }
    }
    *lum = wc_clamp32(L, 0, 255);
    *red = wc_clamp32(R, 0, 255);
}

void wc_render_halftone_press(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)kf;
    wc_ht_geo_t g;
    g.face_cx = WC_CENTER_X + (rig->head_x_q8 >> 8);
    g.face_cy = 62 + (rig->head_y_q8 >> 8);
    g.inv_fa2 = wc_inv_sq_q18(52);
    g.inv_fb2 = wc_inv_sq_q18(56);
    g.eye_cy = g.face_cy - 15;
    g.iris_cy = g.eye_cy + (rig->gaze_y * 4 >> 8);
    g.inv_ea2 = wc_inv_sq_q18(11);
    for (int e = 0; e < 2; ++e) {
        g.eye_cx[e] = g.face_cx + (e == 0 ? -24 : 24);
        int32_t lid = e == 0 ? rig->lid_l : rig->lid_r;
        int32_t b = wc_max32(1, 8 * lid >> 8);
        g.inv_eb2[e] = wc_inv_sq_q18(b);
        g.lid_top[e] = -b + 1;
        g.iris_cx[e] = g.eye_cx[e] + (rig->gaze_x * 5 >> 8);
        g.brow_y[e] = g.eye_cy - 12 - (rig->brow_q8 * 6 >> 8);
        g.brow_tilt[e] = e == 0 ? -rig->brow_q8 : rig->brow_q8;
    }
    int32_t mw = 15 + ((int32_t)rig->mouth_width * 13 >> 8);
    mw -= (int32_t)rig->mouth_round * (mw / 3) >> 8;
    g.mh = 2 + ((int32_t)rig->mouth_open * 20 >> 8);
    g.mh = g.mh * (256 - (int32_t)rig->mouth_press) >> 8;
    if (g.mh < 2) {
        g.mh = 2;
    }
    g.mouth_cx = g.face_cx;
    g.mouth_cy = g.face_cy + 27;
    g.inv_ma2 = wc_inv_sq_q18(mw);
    g.inv_mb2 = wc_inv_sq_q18(g.mh);
    g.hair_y = g.face_cy - 34 + (rig->brow_q8 >> 6);
    g.emphasis = (rig->brow_q8 < -280) || (rig->energy > 200);

    /* red plate misregistration drifts like a worn press */
    int32_t mis_x = (wc_sin_q14(clock / 96u) * 3) >> 13;
    int32_t mis_y = (wc_sin_q14(clock / 150u + 21000u) * 2) >> 13;

    /* red screen at ~15 degrees: cos 248/256, sin 66/256 */
    for (int32_t y = 0; y < WC_FACE_HEIGHT; ++y) {
        uint16_t *row = fb + (size_t)y * WC_FACE_WIDTH;
        int32_t u2 = y * 66;        /* Q8 rotated coords at x=0 */
        int32_t v2 = y * 248;
        for (int32_t x = 0; x < WC_FACE_WIDTH; ++x, u2 += 248, v2 -= 66) {
            int32_t lum, red;
            wc_ht_field(&g, rig, x, y, &lum, &red);

            /* paper */
            uint32_t fleck = wc_hash2((uint32_t)x, (uint32_t)y * 3u) & 7u;
            int32_t pr = 243 - (int32_t)fleck;
            int32_t pg = 236 - (int32_t)fleck;
            int32_t pb = 220 - (int32_t)(fleck << 1);

            /* red plate: AM dots, fully covered above darkness 200 */
            if (red > 4) {
                int32_t ru = u2 + (x - mis_x) * 0 + mis_x * 251; /* offset */
                int32_t rv = v2 + mis_y * 251;
                int32_t su = ((ru % WC_HT_P_Q8) + WC_HT_P_Q8) % WC_HT_P_Q8 - 896;
                int32_t sv = ((rv % WC_HT_P_Q8) + WC_HT_P_Q8) % WC_HT_P_Q8 - 896;
                int32_t d2 = (su >> 4) * (su >> 4) + (sv >> 4) * (sv >> 4);
                int32_t rmax = red >= 200 ? 99999 : (red * 26) >> 8 << 8;
                if (d2 < rmax) {
                    pr = pr * 214 >> 8;
                    pg = pg * 62 >> 8;
                    pb = pb * 72 >> 8;
                }
            }

            /* black plate: exact 45-degree screen on u=x+y, v=x-y */
            int32_t dark = 255 - lum;
            if (dark > 20) {
                int32_t u = x + y;
                int32_t v = x - y + 4096; /* keep positive */
                int32_t su = u % WC_HT_P - 3;
                int32_t sv = v % WC_HT_P - 3;
                int32_t d2 = su * su + sv * sv;
                int32_t rmax = dark >= 235 ? 99 : (dark * 24) >> 8;
                if (d2 <= rmax) {
                    pr = 30;
                    pg = 26;
                    pb = 25;
                }
            }
            row[x] = wc_rgb565((uint32_t)pr, (uint32_t)pg, (uint32_t)pb);
        }
    }
}
