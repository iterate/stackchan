#include "pf_internal.h"

/*
 * Two 1-bit portraits sharing one pipeline: the scene is composed in
 * 8-bit grayscale (the art surface holds 0..255), then quantised to two
 * inks by different dither strategies:
 *
 *  - Dithered Rogue: ordered Bayer 8×8, white ink on near-black, a hooded
 *    figure lit from below with drifting torchlight.
 *  - Atkinson Portrait: Atkinson error diffusion (6/8 of the error), dark
 *    ink on paper white, a round-glasses librarian in a knit cardigan lit
 *    by a slowly moving window light so the diffusion pattern shimmers.
 */

static const uint16_t rogue_palette[2] = {
    PF_RGB565(0x0E, 0x0E, 0x12), PF_RGB565(0xE8, 0xE8, 0xE0),
};

static const uint16_t atkinson_palette[2] = {
    PF_RGB565(0x20, 0x1C, 0x18), PF_RGB565(0xF2, 0xEE, 0xE4),
};

enum { OB_W = 160, OB_H = 120 };

/* Additive light: raise gray inside an ellipse, strongest at the centre. */
static void ob_light(pf_surface_t *s, int cx, int cy, int rx, int ry,
                     int amount) {
    for (int dy = -ry; dy <= ry; ++dy) {
        int y = cy + dy;
        if (y < 0 || y >= s->h) {
            continue;
        }
        uint32_t t = (uint32_t)(ry * ry - dy * dy);
        int dx = (int)pf_isqrt(t * (uint32_t)(rx * rx) / (uint32_t)(ry * ry));
        for (int x = pf_maxi(cx - dx, 0); x <= pf_mini(cx + dx, s->w - 1);
             ++x) {
            int fall = 256 - (pf_absi(x - cx) * 256) / (rx + 1);
            uint8_t *p = &s->px[(size_t)y * (size_t)s->w + (size_t)x];
            *p = (uint8_t)pf_clampi(*p + ((amount * fall) >> 8), 0, 255);
        }
    }
}

void pf_render_dithered_rogue(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, OB_W, OB_H);
    pf_surface_t *s = &art;
    (void)clock;

    /* True black sky (gray 2 dithers to nothing) with explicit stars. */
    pf_clear(s, 2);
    for (int i = 0; i < 22; ++i) {
        uint32_t h = pf_hash32(0x57A45U ^ (uint32_t)i);
        int sx = (int)(h % OB_W);
        int sy = (int)((h >> 8) % 44U);
        pf_px(s, sx, sy, 210);
        if ((h & 3U) == 0U) {
            pf_px(s, sx - 1, sy, 110);
            pf_px(s, sx + 1, sy, 110);
            pf_px(s, sx, sy - 1, 110);
            pf_px(s, sx, sy + 1, 110);
        }
    }

    int hy = rig->breath / 100 + rig->bob / 100;
    int cx = 80;

    /* Cloaked shoulders. */
    pf_fill_ellipse(s, cx, 128, 66, 40, 52);
    pf_fill_ellipse(s, cx - 30, 126, 24, 30, 64);
    for (int i = 0; i < 5; ++i) {
        pf_line(s, cx - 40 + i * 18, 102, cx - 46 + i * 20, 119, 30);
    }

    /* Hood: outer mass, rim highlight, inner shadow. */
    int top = 12 + hy;
    pf_fill_ellipse(s, cx, top + 52, 52, 56, 74);
    pf_fill_ellipse(s, cx, top + 50, 48, 52, 96);
    pf_fill_ellipse(s, cx, top + 54, 40, 46, 58);
    pf_fill_ellipse(s, cx, top + 52, 33, 40, 22); /* hood cavern */
    /* Fold creases. */
    pf_line(s, cx - 44, top + 30, cx - 34, top + 78, 48);
    pf_line(s, cx + 44, top + 32, cx + 36, top + 80, 48);
    pf_line(s, cx - 20, top + 2, cx - 30, top + 20, 112);
    pf_line(s, cx + 20, top + 2, cx + 30, top + 20, 112);

    /* Face inside the hood, lit from below (torchlight). */
    int fy = top + 52;
    pf_fill_ellipse(s, cx, fy + 4, 20, 24, 70);
    pf_fill_ellipse(s, cx - 2, fy + 10, 17, 17, 118);
    pf_fill_ellipse(s, cx - 3, fy + 16, 13, 10, 150);
    /* Torch flicker breathes over the jaw. */
    ob_light(s, cx - 4, fy + 18, 18, 12, 30 + rig->flicker / 6);

    /* Eyes: narrow glowing slits under the hood shadow. */
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 9 : cx - 9;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        int h = 1 + (open * 2) / 255; /* 1..3 rows */
        pf_fill_rect(s, ex - 4, fy - 2, 8, 5, 12); /* socket */
        int gx = rig->gaze_x / 52;
        pf_fill_rect(s, ex - 3 + gx, fy - 1 + (3 - h) / 2, 6, h, 232);
        if (h > 1) {
            pf_px(s, ex - 3 + gx, fy - 1 + (3 - h) / 2, 160);
        }
    }
    /* Brow shadow bar deepens with a frown. */
    if (rig->brow < -20) {
        pf_fill_rect(s, cx - 14, fy - 5, 28, 3, 10);
    }

    /* Nose shadow + mouth in grayscale. */
    pf_vline(s, cx, fy + 5, fy + 10, 60);
    pf_px(s, cx - 1, fy + 10, 40);
    pf_lips_t lips = { cx, fy + 15, 9, 12, 40, 110, 12, 210, 80 };
    pf_draw_lips(s, &lips, k);

    /* Film grain over the lit region keeps the dither alive. */
    for (int y = fy - 10; y < fy + 30; ++y) {
        for (int x = cx - 22; x <= cx + 22; x += 3) {
            uint32_t h = pf_hash32((uint32_t)(y * 331 + x) ^ 0x64A1U);
            int xx = x + (int)(h % 3U);
            uint8_t *p = &s->px[(size_t)y * OB_W + (size_t)xx];
            int v = *p + (int)(h % 17U) - 8;
            *p = (uint8_t)pf_clampi(v, 0, 255);
        }
    }

    pf_dither_bayer_1bit(s);
    pf_present(fb, s, 1, 1, rogue_palette, PF_FX_NONE);
}

void pf_render_atkinson_portrait(uint16_t *fb, const face_keyframe_t *k,
                                 const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, OB_W, OB_H);
    pf_surface_t *s = &art;
    (void)clock;

    /* Paper white with a soft vertical wash. */
    for (int y = 0; y < OB_H; ++y) {
        uint8_t v = (uint8_t)(232 - y / 4);
        pf_hline(s, 0, OB_W - 1, y, v);
    }
    /* Window light drifts slowly with the flicker channel. */
    int lx = 34 + rig->flicker / 8;
    ob_light(s, lx, 30, 60, 46, 26);

    int hy = rig->breath / 110;
    int cx = 80;
    int fy = 52 + hy;

    /* Cardigan with knit texture. */
    pf_fill_ellipse(s, cx, 130, 56, 42, 120);
    for (int y = 96; y < OB_H; y += 3) {
        for (int x = cx - 52; x <= cx + 52; x += 4) {
            uint8_t g = pf_get(s, x, y);
            if (g > 60 && g < 200) {
                pf_px(s, x + ((y / 3) & 1) * 2, y, (uint8_t)(g - 34));
            }
        }
    }
    /* Collar. */
    pf_fill_ellipse(s, cx, 104, 16, 8, 236);
    pf_fill_rect(s, cx - 2, 104, 4, 8, 90); /* brooch ribbon */

    /* Neck. */
    pf_fill_rect(s, cx - 7, fy + 34, 14, 14, 168);

    /* Head: soft grayscale ramp, hair bun. */
    pf_fill_ellipse(s, cx, fy, 24, 28, 150);
    pf_fill_ellipse(s, cx - 4, fy - 2, 20, 24, 190);
    pf_fill_ellipse(s, cx - 7, fy - 4, 14, 17, 215);
    /* Hair: swept back into a bun. */
    pf_fill_ellipse(s, cx, fy - 22, 24, 12, 70);
    pf_fill_ellipse(s, cx - 26, fy - 6, 6, 16, 74);
    pf_fill_ellipse(s, cx + 26, fy - 6, 6, 16, 60);
    pf_fill_circle(s, cx + 20, fy - 30, 9, 58);
    pf_fill_circle(s, cx + 22, fy - 32, 4, 96); /* bun highlight */
    for (int i = 0; i < 5; ++i) {
        pf_line(s, cx - 20 + i * 8, fy - 26, cx - 14 + i * 8, fy - 18, 96);
    }

    /* Ears + pearl earrings. */
    pf_fill_ellipse(s, cx - 24, fy + 6, 3, 5, 176);
    pf_fill_ellipse(s, cx + 24, fy + 6, 3, 5, 150);
    pf_px(s, cx - 24, fy + 11, 240);
    pf_px(s, cx + 24, fy + 11, 226);

    /* Round glasses: dark rims, pale lenses, moving catchlight. */
    int gx = rig->gaze_x / 48;
    int gy = rig->gaze_y / 74;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 10 : cx - 10;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_circle(s, ex, fy + 1, 8, 40);   /* rim */
        pf_fill_circle(s, ex, fy + 1, 7, 205);  /* lens */
        pf_px(s, ex - 3, fy - 2, 250);          /* lens glint */
        if (open > 90) {
            pf_fill_circle(s, ex + gx, fy + 1 + gy, 2, 30);
            pf_px(s, ex + gx, fy + gy, 120);
        } else {
            pf_hline(s, ex - 4, ex + 4, fy + 2, 60); /* closed lash */
        }
    }
    pf_hline(s, cx - 2, cx + 2, fy + 1, 40); /* bridge */
    pf_line(s, cx - 18, fy, cx - 24, fy - 2, 40); /* temples */
    pf_line(s, cx + 18, fy, cx + 24, fy - 2, 40);

    /* Brows above the rims. */
    int bl = -rig->brow / 30;
    pf_fill_ellipse(s, cx - 10, fy - 9 + bl, 6, 1, 80);
    pf_fill_ellipse(s, cx + 10, fy - 9 + bl, 6, 1, 80);

    /* Nose. */
    pf_vline(s, cx, fy + 6, fy + 14, 140);
    pf_fill_ellipse(s, cx, fy + 15, 3, 2, 170);
    pf_px(s, cx - 2, fy + 16, 110);
    pf_px(s, cx + 2, fy + 16, 110);

    /* Gentle smile lines + polygon mouth. */
    pf_lips_t lips = { cx, fy + 23, 10, 12, 90, 140, 30, 235, 120 };
    pf_draw_lips(s, &lips, k);
    pf_px(s, cx - 12, fy + 21, 130);
    pf_px(s, cx + 12, fy + 21, 130);

    pf_dither_atkinson_1bit(s);
    /* Atkinson output: 0 = ink? No: bit 1 means bright. Palette maps
     * index 1 to paper, index 0 to ink. */
    pf_present(fb, s, 1, 1, atkinson_palette, PF_FX_NONE);
}
