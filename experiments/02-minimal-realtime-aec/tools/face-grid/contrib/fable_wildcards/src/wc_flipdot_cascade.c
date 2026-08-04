#include "wc_common.h"

/*
 * Flip-dot cascade: a 40x30 electromechanical disc panel.
 *
 * The panel refreshes its target image every 100 ms like a real row-driven
 * sign: each row is strobed 2.5 ms after the previous one, plus a per-dot
 * mechanical scatter. A disc caught mid-flip renders as a foreshortened
 * ellipse (its visible width follows |cos| of the flip angle), showing the
 * old face color until halfway and the new one after, with a dark edge-on
 * slit and a glint in between. Because the renderer compares the target
 * image at the current and previous refresh ticks, every blink and saccade
 * arrives as a mechanical wave sweeping down the panel — the signature
 * flip-dot cascade — with zero retained state.
 */

enum {
    WC_FD_COLS = 40,
    WC_FD_ROWS = 30,
    WC_FD_CELL = 4,
    WC_FD_REFRESH = 1600,   /* 100 ms between target updates */
    WC_FD_ROW_LAT = 40,     /* 2.5 ms per row strobe */
    WC_FD_FLIP = 480,       /* 30 ms disc travel */
};

/* Target state of one dot for the pose at quantized time tq. */
static int32_t wc_fd_state(const wc_keyframe_t *kf, uint32_t tq,
                           const wc_rig_t *rig, int32_t c, int32_t r) {
    (void)kf;
    int32_t px = c * WC_FD_CELL + WC_FD_CELL / 2 - (rig->head_x_q8 >> 8);
    int32_t py = r * WC_FD_CELL + WC_FD_CELL / 2 - (rig->head_y_q8 >> 8);

    /* eyes: yellow ring with a punched-out pupil */
    for (int e = 0; e < 2; ++e) {
        int32_t cx = WC_CENTER_X + (e == 0 ? -WC_EYE_DX : WC_EYE_DX);
        int32_t lid = e == 0 ? rig->lid_l : rig->lid_r;
        int32_t dx = px - cx;
        int32_t dy = py - WC_EYE_Y;
        int32_t a = 13;
        int32_t b = 2 + (11 * lid >> 8);
        if (dx * dx * b * b + dy * dy * a * a <= a * a * b * b) {
            int32_t pdx = dx - (rig->gaze_x * 5 >> 8);
            int32_t pdy = dy - (rig->gaze_y * 4 >> 8);
            if (b > 6 && pdx * pdx + pdy * pdy < 4 * 4) {
                return 0; /* pupil */
            }
            return 1;
        }
        /* brow bar */
        int32_t tilt = (e == 0 ? -rig->brow_q8 : rig->brow_q8);
        int32_t by = WC_EYE_Y - 15 - (rig->brow_q8 * 5 >> 8) +
                     (dx * tilt >> 10);
        if (dx >= -11 && dx <= 11 && py >= by - 2 && py <= by + 1) {
            return 1;
        }
    }

    /* mouth: widening bar / opening superellipse on dot row 90 */
    {
        int32_t w = 13 + ((int32_t)rig->mouth_width * 11 >> 8);
        int32_t h = 2 + ((int32_t)rig->mouth_open * 15 >> 8);
        h = h * (256 - (int32_t)rig->mouth_press) >> 8;
        int32_t rq = (int32_t)rig->mouth_round;
        w = w - (rq * (w / 3) >> 8);
        w += rig->breath_q14 * 3 >> 13; /* idle breathing sways the width */
        int32_t dx = px - WC_CENTER_X;
        int32_t dy = py - 90;
        if (h < 4) {
            if (dy >= -1 && dy <= 1 && wc_abs32(dx) <= w) {
                return 1;
            }
        } else if (dx * dx * h * h + dy * dy * w * w <= w * w * h * h) {
            /* teeth: comb gaps in the upper half of a wide open mouth */
            if (rig->mouth_teeth > 110 && h > 8 && dy < -1 &&
                ((px >> 1) & 3) == 1) {
                return 0;
            }
            return 1;
        }
    }

    /* nose dot */
    if (px >= 79 && px <= 81 && py >= 67 && py <= 69) {
        return 1;
    }

    /* idle marquee: a slow chase around the border while fully idle */
    if (!rig->speaking && rig->energy < 12) {
        if (c == 0 || c == WC_FD_COLS - 1 || r == 0 || r == WC_FD_ROWS - 1) {
            int32_t ring = (c == 0)   ? r
                           : (r == WC_FD_ROWS - 1) ? WC_FD_ROWS - 1 + c
                           : (c == WC_FD_COLS - 1)
                               ? WC_FD_ROWS - 1 + WC_FD_COLS - 1 + (WC_FD_ROWS - 1 - r)
                               : 2 * (WC_FD_ROWS - 1) + WC_FD_COLS - 1 +
                                     (WC_FD_COLS - 1 - c);
            uint32_t ph = (uint32_t)ring * 472u + tq / 3u;
            if (wc_sin_q14(ph) > 15500) {
                return 1;
            }
        }
    }
    return 0;
}

void wc_render_flipdot_cascade(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)rig;
    uint32_t t0 = clock - clock % WC_FD_REFRESH;
    uint32_t tprev = t0 >= WC_FD_REFRESH ? t0 - WC_FD_REFRESH : 0u;

    wc_rig_t rig_now, rig_prev;
    wc_rig_derive(kf, t0, &rig_now);
    wc_rig_derive(kf, tprev, &rig_prev);

    for (int32_t r = 0; r < WC_FD_ROWS; ++r) {
        for (int32_t c = 0; c < WC_FD_COLS; ++c) {
            int32_t s_now = wc_fd_state(kf, t0, &rig_now, c, r);
            int32_t s_prev = wc_fd_state(kf, tprev, &rig_prev, c, r);

            uint32_t lat = (uint32_t)r * WC_FD_ROW_LAT +
                           wc_hash2((uint32_t)c, (uint32_t)r * 7919u) % 96u;
            int32_t el = (int32_t)(clock - t0) - (int32_t)lat;
            int32_t phase_q8 = 256; /* settled on s_now */
            if (s_now != s_prev) {
                phase_q8 = wc_clamp32(el * 256 / WC_FD_FLIP, 0, 256);
            }

            /* visible width of the disc: |cos| of flip angle */
            int32_t wf_q14 = wc_abs32(wc_cos_q14((uint32_t)(phase_q8 << 7)));
            int32_t show = phase_q8 < 128 ? s_prev : s_now;
            int32_t settled = (s_now == s_prev) || phase_q8 >= 256;

            /* recent clack flash */
            int32_t flash = 0;
            if (s_now != s_prev && el >= WC_FD_FLIP && el < WC_FD_FLIP + 200) {
                flash = 40 - el * 40 / (WC_FD_FLIP + 200);
            }

            uint32_t wear = wc_hash2((uint32_t)c * 131u, (uint32_t)r) & 31u;
            int32_t inv_wf = wf_q14 > 256 ? (int32_t)((1 << 22) / wf_q14) : 0;

            uint16_t *cell = fb + (size_t)r * WC_FD_CELL * WC_FACE_WIDTH +
                             (size_t)c * WC_FD_CELL;
            for (int32_t yy = 0; yy < WC_FD_CELL; ++yy) {
                for (int32_t xx = 0; xx < WC_FD_CELL; ++xx) {
                    /* disc center at (1.5, 1.5) in the 4x4 cell, Q4 coords */
                    int32_t dx = (xx << 4) + 8 - 24;
                    int32_t dy = (yy << 4) + 8 - 24;
                    uint32_t rr = (uint32_t)(dx * dx + dy * dy);
                    uint32_t rgb;
                    int32_t inside;
                    if (settled || inv_wf == 0) {
                        inside = rr <= 30u * 30u && !(!settled && inv_wf == 0);
                    } else {
                        int32_t sx = (dx * inv_wf) >> 8; /* stretch by 1/wf */
                        inside = (uint32_t)(sx * sx + dy * dy) <= 30u * 30u;
                    }
                    if (!settled && inv_wf == 0) {
                        /* edge-on: dark slit + glint */
                        inside = dx >= -6 && dx <= 6;
                        if (inside) {
                            rgb = (yy == 1) ? 0x9CD3u /* glint */
                                            : wc_rgb565(50, 48, 46);
                            cell[(size_t)yy * WC_FACE_WIDTH + xx] = (uint16_t)rgb;
                            continue;
                        }
                    }
                    if (!inside) {
                        cell[(size_t)yy * WC_FACE_WIDTH + xx] =
                            wc_rgb565(10, 10, 13);
                        continue;
                    }
                    int32_t shade = ((1 - yy) * 9) + (int32_t)flash;
                    if (show) {
                        int32_t v = 214 - (int32_t)wear + shade;
                        int32_t g = 186 - (int32_t)wear + shade;
                        rgb = wc_rgb565((uint32_t)wc_clamp32(v + 24, 0, 255),
                                        (uint32_t)wc_clamp32(g, 0, 255),
                                        (uint32_t)wc_clamp32(34 + shade, 0, 255));
                    } else {
                        int32_t v = 27 - (int32_t)(wear >> 1) + shade;
                        /* rim highlight so off discs still read as discs */
                        if (yy == 0 && xx == 1) {
                            v += 26;
                        }
                        rgb = wc_rgb565((uint32_t)wc_clamp32(v + 3, 0, 255),
                                        (uint32_t)wc_clamp32(v + 2, 0, 255),
                                        (uint32_t)wc_clamp32(v, 0, 255));
                    }
                    cell[(size_t)yy * WC_FACE_WIDTH + xx] = (uint16_t)rgb;
                }
            }
        }
    }
}
