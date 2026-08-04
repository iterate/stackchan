#include "wc_common.h"

/*
 * Chladni voiceplate: cymatics as a face.
 *
 * A square-plate standing wave F = cos(n pi u) cos(m pi v) - cos(m pi u)
 * cos(n pi v) supplies the canonical Chladni nodal geometry. The mouth bytes
 * pick the resonance: rest hums at (1,3); an open unrounded vowel drives
 * (3,5); wide vowels drive (5,7); rounded vowels drive (2,6); sibilant frames
 * drive a fine (7,9) mesh. Sand settles where |F| is small, so the whole
 * plate literally re-figures with the voice. Near the eyes the plate is
 * "clamped": the field is pulled into concentric rings that squash with the
 * eyelid, with a sand-pile pupil at the clamp point. The mouth rim is an
 * attractor, so a sand outline traces the current mouth shape while the
 * interior antinode blows clear. Loud frames tighten the nodal lines and
 * make every grain shimmer by re-rolling the grain hash 31 times a second.
 */

static void wc_axis_tab(int16_t *tab, int32_t count, int32_t n, int32_t m,
                        int32_t stride_num) {
    /* tab[i*2+0] = cos(n pi u), tab[i*2+1] = cos(m pi u), u = i / count */
    for (int32_t i = 0; i < count; ++i) {
        uint32_t half = (uint32_t)(i * stride_num); /* half-turn units Q15 */
        tab[i * 2 + 0] = (int16_t)wc_cos_q14((uint32_t)n * half >> 1);
        tab[i * 2 + 1] = (int16_t)wc_cos_q14((uint32_t)m * half >> 1);
    }
}

void wc_render_chladni_sand(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)kf;
    int32_t open = rig->mouth_open;
    int32_t width = rig->mouth_width;
    int32_t round_ = rig->mouth_round;
    int32_t teeth = rig->mouth_teeth;

    /* choose the driven mode pair */
    int32_t n2 = 3, m2 = 5; /* A */
    if (teeth > 150 && open > 60) {
        n2 = 7;
        m2 = 9;
    } else if (round_ > 140) {
        n2 = 2;
        m2 = 6;
    } else if (width > 150) {
        n2 = 5;
        m2 = 7;
    }

    /* separable cosine tables: rest pair (1,3) and driven pair */
    int16_t colt_r[WC_FACE_WIDTH * 2], colt_d[WC_FACE_WIDTH * 2];
    int16_t rowt_r[WC_FACE_HEIGHT * 2], rowt_d[WC_FACE_HEIGHT * 2];
    /* half-turn across the plate: 32768/160 = 204.8 -> x*6554>>5 in Q15 */
    wc_axis_tab(colt_r, WC_FACE_WIDTH, 1, 3, 410);
    wc_axis_tab(colt_d, WC_FACE_WIDTH, n2, m2, 410);
    wc_axis_tab(rowt_r, WC_FACE_HEIGHT, 1, 3, 546);
    wc_axis_tab(rowt_d, WC_FACE_HEIGHT, n2, m2, 546);

    int32_t blend = open; /* 0 rest .. 255 driven */
    int32_t thresh = 1150 + ((255 - open) * 5);
    uint32_t grain_salt =
        rig->energy > 100 ? clock >> 9 : 7u; /* grains dance when loud */

    /* eye geometry */
    int32_t ex[2], lidv[2], inv_lid[2];
    ex[0] = WC_CENTER_X - WC_EYE_DX + (rig->gaze_x * 3 >> 8);
    ex[1] = WC_CENTER_X + WC_EYE_DX + (rig->gaze_x * 3 >> 8);
    int32_t ey = WC_EYE_Y + (rig->gaze_y * 2 >> 8);
    lidv[0] = wc_max32(20, rig->lid_l);
    lidv[1] = wc_max32(20, rig->lid_r);
    inv_lid[0] = (255 << 14) / lidv[0];
    inv_lid[1] = (255 << 14) / lidv[1];

    /* mouth attractor geometry */
    int32_t mw = 16 + (width * 14 >> 8);
    int32_t mh = 4 + (open * 16 >> 8);
    mw -= round_ * (mw / 3) >> 8;
    int32_t inv_mw2 = wc_inv_sq_q18(mw);
    int32_t inv_mh2 = wc_inv_sq_q18(mh);

    for (int32_t y = 0; y < WC_FACE_HEIGHT; ++y) {
        uint16_t *row = fb + (size_t)y * WC_FACE_WIDTH;
        int32_t cyr1 = rowt_r[y * 2], cyr3 = rowt_r[y * 2 + 1];
        int32_t cyd1 = rowt_d[y * 2], cyd3 = rowt_d[y * 2 + 1];
        for (int32_t x = 0; x < WC_FACE_WIDTH; ++x) {
            /* steel frame */
            if (x < 3 || x >= WC_FACE_WIDTH - 3 || y < 3 ||
                y >= WC_FACE_HEIGHT - 3) {
                int32_t streak =
                    (int32_t)(wc_hash2((uint32_t)x, (uint32_t)y / 3u) & 15u);
                int32_t v = 74 + streak - ((x + y) & 7);
                row[x] = wc_rgb565((uint32_t)v, (uint32_t)(v + 4),
                                   (uint32_t)(v + 9));
                continue;
            }

            int32_t fr = (colt_r[x * 2] * cyr3 - colt_r[x * 2 + 1] * cyr1) >> 14;
            int32_t fd = (colt_d[x * 2] * cyd3 - colt_d[x * 2 + 1] * cyd1) >> 14;
            int32_t f = ((256 - blend) * fr + blend * fd) >> 8;

            /* eye clamps: pull the field into lid-squashed rings */
            int32_t pupil = 0;
            for (int e = 0; e < 2; ++e) {
                int32_t dx = x - ex[e];
                int32_t dy = y - ey;
                if (dx < -20 || dx > 20 || dy < -18 || dy > 18) {
                    continue;
                }
                int32_t dye = dy * inv_lid[e] >> 14;
                int32_t r2 = dx * dx + dye * dye;
                if (r2 > 20 * 20) {
                    continue;
                }
                int32_t g = ((20 * 20 - r2) << 8) / (20 * 20); /* 0..256 */
                int32_t ring = wc_sin_q14((uint32_t)(r2 * 96));
                f = (f * (256 - g) + ring * g) >> 8;
                if (r2 < 5 * 5 && lidv[e] > 90) {
                    pupil = 256 - (r2 << 8) / (5 * 5);
                }
            }

            /* mouth rim attractor / interior antinode */
            int32_t dxm = x - WC_CENTER_X;
            int32_t dym = y - WC_MOUTH_Y;
            int32_t rim_boost = 0, clear_boost = 0;
            if (dxm > -48 && dxm < 48 && dym > -30 && dym < 30) {
                int32_t q = wc_ellipse_q10(dxm, dym, inv_mw2, inv_mh2);
                if (q < 800) {
                    clear_boost = (800 - q) / 2; /* blow the interior clean */
                } else if (q < 1500) {
                    int32_t d = wc_abs32(q - 1024);
                    if (d < 260) {
                        rim_boost = (260 - d); /* sand hugs the mouth rim */
                    }
                }
            }

            /* brow arcs as sand ridges */
            for (int e = 0; e < 2; ++e) {
                int32_t dx = x - ex[e];
                if (dx < -12 || dx > 12) {
                    continue;
                }
                int32_t tilt = (e == 0 ? -rig->brow_q8 : rig->brow_q8);
                int32_t by = ey - 15 - (rig->brow_q8 * 5 >> 8) +
                             (dx * tilt >> 10) + (dx * dx / 24);
                int32_t d = wc_abs32(y - by);
                if (d < 2) {
                    rim_boost = wc_max32(rim_boost, 200 - d * 90);
                }
            }

            int32_t af = wc_abs32(f);
            int32_t sand = 0;
            if (af < thresh) {
                sand = ((thresh - af) << 8) / thresh; /* 0..256 */
            }
            sand = sand + rim_boost - clear_boost;
            sand = wc_clamp32(sand + pupil, 0, 256);

            uint32_t h = wc_hash2((uint32_t)(x + 1) * 31u + grain_salt,
                                  (uint32_t)(y + 1) * 17u);
            uint32_t g8 = h & 255u;
            /* granularity: thin the sheet into grains */
            int32_t a = sand * (int32_t)(140u + (g8 >> 1)) >> 8;
            if (a < 40 && g8 > 250u && af < thresh * 3) {
                a = 120; /* stray grains scattered off the lines */
            }
            if (a > 256) {
                a = 256;
            }

            /* plate: dark steel with a faint drive glow under the center */
            int32_t r2c = (x - 80) * (x - 80) + (y - 96) * (y - 96);
            int32_t glow = r2c < 4096 ? ((4096 - r2c) * rig->energy) >> 15 : 0;
            uint16_t plate = wc_rgb565((uint32_t)(17 + glow),
                                       (uint32_t)(19 + (glow >> 1)),
                                       (uint32_t)(26 + (glow >> 2)));
            uint32_t sr = 205u + (g8 >> 3);
            uint32_t sg = 188u + (g8 >> 3);
            uint32_t sb = 148u + (g8 >> 4);
            row[x] = wc_mix565(plate, wc_rgb565(sr, sg, sb), (uint32_t)a);
        }
    }

    /* corner screws */
    static const int32_t sc[4][2] = { {7, 7}, {152, 7}, {7, 112}, {152, 112} };
    for (int i = 0; i < 4; ++i) {
        for (int32_t dy = -2; dy <= 2; ++dy) {
            for (int32_t dx = -2; dx <= 2; ++dx) {
                if (dx * dx + dy * dy > 5) {
                    continue;
                }
                uint16_t *p =
                    fb + (size_t)(sc[i][1] + dy) * WC_FACE_WIDTH + (size_t)(sc[i][0] + dx);
                *p = (dx == -1 && dy == -1) ? wc_rgb565(120, 124, 132)
                                            : wc_rgb565(38, 40, 46);
            }
        }
    }
}
