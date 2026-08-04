#include "fmg_internal.h"

/*
 * Oscilloscope trace mouth: a phosphor-green XY-scope face in the spirit
 * of Lissajous scope art and oscilloscope music. Eyes are Lissajous
 * rings that flatten when blinking; the mouth is a synthesized waveform
 * whose amplitude follows openness, whose spatial frequency rises with
 * frication (teeth), and which becomes a rotating ellipse when rounded.
 * Three phase-offset passes fake phosphor persistence; silence shows a
 * flat line with a sweeping heartbeat blip.
 */

#define CRT_BG FMG_RGB565(3, 10, 6)
#define GRID FMG_RGB565(14, 40, 24)
#define GRID_AXIS FMG_RGB565(20, 58, 34)
#define BEAM FMG_RGB565(96, 255, 128)
#define BEAM_MID FMG_RGB565(52, 160, 80)
#define BEAM_LOW FMG_RGB565(26, 92, 48)

static void fmg_scope_dot(
    uint16_t *px, int32_t x, int32_t y, uint16_t color, int32_t alpha)
{
    fmg_pixel_blend(px, x, y, color, alpha);
    fmg_pixel_blend(px, x - 1, y, color, alpha / 3);
    fmg_pixel_blend(px, x + 1, y, color, alpha / 3);
    fmg_pixel_blend(px, x, y - 1, color, alpha / 3);
    fmg_pixel_blend(px, x, y + 1, color, alpha / 3);
}

static void fmg_scope_ring(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint32_t phase, uint16_t color, int32_t alpha)
{
    for (int i = 0; i < 48; i++) {
        uint16_t a = (uint16_t)((i << 16) / 48 + phase);
        int32_t x = cx + ((fmg_cos_q14(a) * rx) >> 14);
        int32_t y = cy + ((fmg_sin_q14(a) * ry) >> 14);
        fmg_scope_dot(px, x, y, color, alpha);
    }
}

void fmg_render_scope(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;

    fmg_fill(px, CRT_BG);
    /* graticule */
    for (int32_t x = 0; x < FMG_WIDTH; x += 16) {
        fmg_vspan(px, x, 0, FMG_HEIGHT - 1, GRID);
    }
    for (int32_t y = 8; y < FMG_HEIGHT; y += 16) {
        fmg_hline(px, 0, FMG_WIDTH - 1, y, GRID);
    }
    fmg_vspan(px, 80, 0, FMG_HEIGHT - 1, GRID_AXIS);
    fmg_hline(px, 0, FMG_WIDTH - 1, 88, GRID_AXIS);

    /* Lissajous ring eyes */
    int32_t look_dx = (int32_t)kf->look_x * 6 / 128 + (idle.gaze_dx_q8 >> 8);
    int32_t look_dy = (int32_t)kf->look_y * 4 / 128 + (idle.gaze_dy_q8 >> 8);
    for (int side = 0; side < 2; side++) {
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t ecx = (side == 0 ? 52 : 108) + sway;
        int32_t ecy = 44 + breath;
        int32_t ry = (12 * fmg_clampi(lid, 0, 260)) >> 8;
        if (ry < 1) {
            ry = 1; /* blink: ring collapses to a line */
        }
        uint32_t ph = (clock * 3) & 0xFFFFU;
        fmg_scope_ring(px, ecx, ecy, 12, ry, ph, BEAM_LOW, 120);
        fmg_scope_ring(px, ecx, ecy, 12, ry, ph + 40000, BEAM, 190);
        if (ry > 3) {
            fmg_scope_dot(px, ecx + fmg_clampi(look_dx, -7, 7),
                          ecy + fmg_clampi(look_dy, -(ry - 2), ry - 2), BEAM,
                          255);
        }
        /* brow trace */
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        int32_t byy = ecy - 18 + (brow >> 8);
        for (int32_t d = -8; d <= 8; d++) {
            int32_t yy = byy - ((8 - (d < 0 ? -d : d)) / 3);
            fmg_pixel_blend(px, ecx + d, yy, BEAM_MID, 170);
        }
    }

    /* mouth trace */
    int32_t my = 88 + breath;
    int32_t mx = 80 + sway;
    if (mouth.round_q8 > 140) {
        /* rounded vowel: the trace closes into a rotating ellipse */
        int32_t rx = 12 + ((mouth.width_q8 * 10) >> 8);
        int32_t ry = 4 + ((mouth.open_q8 * 14) >> 8);
        uint32_t ph = clock * 5;
        fmg_scope_ring(px, mx, my, rx, ry, ph & 0xFFFFU, BEAM_LOW, 110);
        fmg_scope_ring(px, mx, my, rx, ry, (ph + 30000) & 0xFFFFU, BEAM_MID,
                       160);
        fmg_scope_ring(px, mx, my, rx, ry, (ph + 52000) & 0xFFFFU, BEAM, 220);
    } else {
        int32_t amp = (mouth.open_q8 * 11) >> 8;
        int32_t frica = (mouth.teeth_q8 * 5) >> 8;
        int32_t press = mouth.press_q8;
        for (int pass = 0; pass < 3; pass++) {
            uint32_t pclock = clock - (uint32_t)pass * 900U;
            int32_t alpha = pass == 2 ? 235 : (pass == 1 ? 120 : 60);
            uint16_t color = pass == 2 ? BEAM : (pass == 1 ? BEAM_MID
                                                           : BEAM_LOW);
            int32_t prev_y = my;
            for (int32_t x = 26; x <= 134; x++) {
                int32_t rel = x - mx;
                int32_t y = my;
                if (amp > 0) {
                    uint16_t a1 = (uint16_t)(rel * 1400 + pclock * 6);
                    uint16_t a2 = (uint16_t)(rel * 3300 + pclock * 11);
                    y += (fmg_sin_q14(a1) * amp) >> 14;
                    y += (fmg_sin_q14(a2) * amp) >> 15;
                }
                if (frica > 0) {
                    uint32_t h = fmg_hash((uint32_t)x ^ (pclock >> 5));
                    y += (int32_t)(h % 7U) * frica / 8 - frica / 3;
                }
                if (press > 150) {
                    y = my; /* bilabial closure: dead-flat line */
                }
                /* window the wave toward the corners */
                int32_t edge = rel < 0 ? -rel : rel;
                if (edge > 40) {
                    y = my + ((y - my) * (54 - edge)) / 14;
                }
                fmg_scope_dot(px, x, y, color, alpha);
                if (pass == 2 && (y - prev_y > 2 || prev_y - y > 2)) {
                    int32_t step = y > prev_y ? 1 : -1;
                    for (int32_t yy = prev_y; yy != y; yy += step) {
                        fmg_pixel_blend(px, x - 1, yy, color, alpha / 2);
                    }
                }
                prev_y = y;
            }
        }
        /* silence: sweeping heartbeat blip */
        if (!mouth.speaking && amp < 3) {
            int32_t bx = (int32_t)((clock / 260U) % 200U);
            if (bx < 160 && bx >= 26 && bx <= 134) {
                fmg_scope_dot(px, bx - 2, my - 3, BEAM, 220);
                fmg_scope_dot(px, bx - 1, my - 8, BEAM, 255);
                fmg_scope_dot(px, bx, my - 12, BEAM, 255);
                fmg_scope_dot(px, bx + 1, my - 6, BEAM, 255);
                fmg_scope_dot(px, bx + 2, my - 2, BEAM, 220);
            }
        }
    }

    /* beam-intensity dot breathing in the corner */
    fmg_scope_dot(px, 148, 110, BEAM_MID, 120 + (idle.breath_q8 / 4));
}
