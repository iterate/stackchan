#include "wc_common.h"

/*
 * Ferro pool: a ferrofluid creature in a lit dish.
 *
 * The pool is a glossy black blob whose rim radius is a function of angle:
 * slow meniscus harmonics idle it, and voice raises a Rosensweig spike crown
 * (sharpened |sin|^4 lobes; spike count follows mouth width, height follows
 * mouth open, and rounded vowels gather everything into one central Taylor
 * cone). The interior grows the classic hexagonal Rosensweig lattice as the
 * interference of three plane waves at 120 degrees, exactly the pattern real
 * ferrofluid forms above critical magnetization — lattice contrast rides the
 * voice energy and the phases drift so the fluid never sits still. The eyes
 * are two satellite droplets whose specular catchlight doubles as the pupil
 * (the highlight tracks gaze); blinking sinks them into the pool. Everything
 * is integer: one atan2 and one isqrt per rim-band pixel only.
 */

enum {
    WC_FP_CX = 80,
    WC_FP_CY = 76,
};

void wc_render_ferro_pool(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)kf;
    /* dish light LUT by r^2 >> 6 */
    uint16_t dish[200];
    for (int32_t i = 0; i < 200; ++i) {
        int32_t d = i * 3 >> 1;
        int32_t r = 206 - d - (d >> 2);
        int32_t g = 212 - d;
        int32_t b = 222 - d + (d >> 3);
        dish[i] = wc_rgb565((uint32_t)wc_clamp32(r, 60, 255),
                            (uint32_t)wc_clamp32(g, 66, 255),
                            (uint32_t)wc_clamp32(b, 84, 255));
    }

    int32_t energy = rig->energy;
    int32_t base_r = 31 + (rig->breath_q14 * 3 >> 13) + (energy * 7 >> 8);
    uint32_t spikes = 5u + ((uint32_t)rig->mouth_width * 10u >> 8);
    int32_t spike_h = 3 + (energy * 13 >> 8);
    int32_t cone = rig->mouth_round > 140 ? (rig->mouth_round - 140) : 0;

    uint32_t ph_a = clock * 5u;
    uint32_t ph_b = (uint32_t)(-(int32_t)(clock * 3u));
    uint32_t ph_c = clock * 8u;

    /* hex lattice wave vectors: ~9 px wavelength, finer with sibilants */
    int32_t k = 6600 + ((int32_t)rig->mouth_teeth << 4);
    uint32_t lat_p1 = clock * 6u, lat_p2 = clock * 4u, lat_p3 = (uint32_t)(-(int32_t)(clock * 5u));

    int32_t rmax = base_r + spike_h + 8;
    int32_t rmin = base_r - 10;
    if (rmin < 4) {
        rmin = 4;
    }

    /* droplet eyes */
    int32_t drop_cx[2], drop_cy[2], drop_a[2], drop_b[2], drop_alpha[2];
    for (int e = 0; e < 2; ++e) {
        int32_t lid = e == 0 ? rig->lid_l : rig->lid_r;
        int32_t sink = (255 - lid) * 9 >> 8;
        drop_cx[e] = WC_CENTER_X + (e == 0 ? -WC_EYE_DX + 8 : WC_EYE_DX - 8) +
                     (rig->head_x_q8 >> 8) + (rig->gaze_x * 3 >> 8);
        drop_cy[e] = WC_FP_CY - base_r - 4 + (rig->head_y_q8 >> 9) +
                     (rig->gaze_y * 2 >> 8) + sink;
        drop_a[e] = 8;
        drop_b[e] = wc_max32(2, 8 - (sink >> 1));
        drop_alpha[e] = lid < 40 ? lid * 6 : 256;
    }

    for (int32_t y = 0; y < WC_FACE_HEIGHT; ++y) {
        uint16_t *row = fb + (size_t)y * WC_FACE_WIDTH;
        for (int32_t x = 0; x < WC_FACE_WIDTH; ++x) {
            int32_t vx = x - 80;
            int32_t vy = y - 60;
            uint32_t vr2 = (uint32_t)(vx * vx + vy * vy) >> 6;
            uint16_t out = dish[vr2 < 200u ? vr2 : 199u];

            int32_t dx = x - WC_FP_CX - (rig->head_x_q8 >> 9);
            int32_t dy = y - WC_FP_CY - (rig->head_y_q8 >> 10);
            int32_t r2 = dx * dx + dy * dy;

            /* contact shadow just outside the pool */
            if (r2 > rmax * rmax && r2 < (rmax + 5) * (rmax + 5) && dy > 8) {
                int32_t t = (rmax + 5) * (rmax + 5) - r2;
                out = wc_mix565(out, wc_rgb565(88, 92, 104),
                                (uint32_t)wc_clamp32(t / 8, 0, 70));
            }

            int32_t cov = 0;
            int32_t rim_dq4 = -999; /* Q4 px distance past the local rim */
            if (r2 <= rmin * rmin) {
                cov = 256;
            } else if (r2 < rmax * rmax) {
                uint32_t th = wc_atan2_u16(dy, dx);
                /* rim radius in Q4 for a sub-pixel soft edge */
                int32_t rr_q4 = base_r * 16 +
                                ((wc_sin_q14(th * 2u + ph_a) * 32) >> 14) +
                                ((wc_sin_q14(th * 3u + ph_b) * 32) >> 14) +
                                ((wc_sin_q14(th * 5u + ph_c) * 16) >> 14);
                /* Rosensweig crown: sharpened |sin|^4 lobes */
                int32_t s = wc_abs32(wc_sin_q14(th * spikes >> 1));
                int32_t s2 = s * s >> 14;
                int32_t s4 = s2 * s2 >> 14;
                rr_q4 += spike_h * 16 * s4 >> 14;
                int32_t r_q4 = (int32_t)wc_isqrt32((uint32_t)r2 << 8);
                rim_dq4 = r_q4 - rr_q4;
                cov = wc_clamp32(((24 - rim_dq4) * 256) / 48, 0, 256);
            }

            if (cov) {
                /* glossy black fluid */
                int32_t rC = 15, gC = 13, bC = 20;
                /* central Taylor cone for rounded vowels */
                if (cone && r2 < 12 * 12) {
                    int32_t h = (144 - r2) * cone >> 8;
                    rC += h >> 4;
                    gC += h >> 4;
                    bC += h >> 3;
                    if (r2 < 2 * 2 && cone > 60) {
                        rC = 170;
                        gC = 180;
                        bC = 205; /* cone tip catchlight */
                    }
                }
                /* hexagonal Rosensweig lattice, three waves at 120 deg */
                if (energy > 24 && r2 < (rmin + 4) * (rmin + 4)) {
                    int32_t p1 = wc_sin_q14((uint32_t)(dx * k) + lat_p1);
                    int32_t p2 = wc_sin_q14(
                        (uint32_t)((-dx * k >> 1) + (dy * k * 227 >> 8)) + lat_p2);
                    int32_t p3 = wc_sin_q14(
                        (uint32_t)((-dx * k >> 1) - (dy * k * 227 >> 8)) + lat_p3);
                    int32_t lat = (p1 + p2 + p3) * energy >> 8; /* Q14-ish */
                    if (lat > 9800) {
                        /* spike flank rises into a specular tip */
                        int32_t t = wc_min32(lat - 9800, 6000);
                        rC += t * 95 >> 13;
                        gC += t * 105 >> 13;
                        bC += t * 135 >> 13;
                    } else if (lat < -8000) {
                        /* valley sheen */
                        bC += (-lat - 8000) >> 9;
                    }
                }
                /* window reflection streak upper-left */
                int32_t wx = dx * 2 + 30, wy = dy * 3 + 58;
                int32_t w2 = wx * wx + wy * wy;
                if (w2 < 500) {
                    int32_t t = (500 - w2) >> 2;
                    rC += t;
                    gC += t;
                    bC += t + (t >> 2);
                }
                uint16_t fluid =
                    wc_rgb565((uint32_t)wc_clamp32(rC, 0, 255),
                              (uint32_t)wc_clamp32(gC, 0, 255),
                              (uint32_t)wc_clamp32(bC, 0, 255));
                /* rim meniscus highlight on the lit side */
                if (rim_dq4 > -64 && rim_dq4 != -999 && dx + dy < 0) {
                    fluid = wc_mix565(fluid, wc_rgb565(150, 158, 178), 120);
                }
                out = wc_mix565(out, fluid, (uint32_t)cov);
            }

            /* droplet eyes over everything */
            for (int e = 0; e < 2; ++e) {
                if (drop_alpha[e] <= 0) {
                    continue;
                }
                int32_t edx = x - drop_cx[e];
                int32_t edy = y - drop_cy[e];
                if (wc_abs32(edx) > 11 || wc_abs32(edy) > 11) {
                    continue;
                }
                int32_t q = wc_ellipse_q10(
                    edx, edy, wc_inv_sq_q18(drop_a[e]), wc_inv_sq_q18(drop_b[e]));
                uint32_t dcov = wc_cov_from_q10(q, 200);
                if (!dcov) {
                    continue;
                }
                dcov = dcov * (uint32_t)drop_alpha[e] >> 8;
                uint16_t drop = wc_rgb565(14, 12, 19);
                /* catchlight doubles as pupil: tracks gaze */
                int32_t hx = edx - (rig->gaze_x * 3 >> 8) + 2;
                int32_t hy = edy - (rig->gaze_y * 2 >> 8) + 2;
                int32_t h2 = hx * hx + hy * hy;
                if (h2 < 6) {
                    drop = wc_rgb565(235, 240, 252);
                } else if (edy > drop_b[e] - 3 && q > 500) {
                    drop = wc_rgb565(70, 80, 110); /* bounce light */
                }
                out = wc_mix565(out, drop, dcov);
            }
            row[x] = out;
        }
    }
}
