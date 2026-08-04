#include "pf_internal.h"

/*
 * VGA Elder Closeup — a 256-colour-era portrait: banded ramp shading from
 * nested offset ellipses (no dither, the VGA signature), a candle-lit rim
 * that flickers deterministically, and the continuous polygon mouth inside
 * a full beard. Original character: Eldrin, a hillside hermit sage.
 */

enum {
    VE_BG0 = 0,
    VE_BG1,
    VE_BG2,
    VE_BLACK,
    VE_SKIN0, /* darkest */
    VE_SKIN1,
    VE_SKIN2,
    VE_SKIN3,
    VE_SKIN4, /* lightest */
    VE_BEARD0,
    VE_BEARD1,
    VE_BEARD2,
    VE_BEARD3,
    VE_ROBE0,
    VE_ROBE1,
    VE_ROBE2,
    VE_ROBE3,
    VE_GOLD,
    VE_GOLD_HI,
    VE_EYE_WHITE,
    VE_IRIS,
    VE_LIP_DARK,
    VE_LIP_MID,
    VE_CAVITY,
    VE_TEETH,
    VE_TONGUE,
    VE_COLOUR_COUNT,
};

static const uint16_t ve_palette[VE_COLOUR_COUNT] = {
    PF_RGB565(0x14, 0x10, 0x0C), PF_RGB565(0x24, 0x1A, 0x12),
    PF_RGB565(0x38, 0x28, 0x1C), PF_RGB565(0x00, 0x00, 0x00),
    PF_RGB565(0x4A, 0x30, 0x20), PF_RGB565(0x6E, 0x4A, 0x32),
    PF_RGB565(0x96, 0x68, 0x48), PF_RGB565(0xC0, 0x8E, 0x62),
    PF_RGB565(0xE0, 0xB4, 0x88), PF_RGB565(0x58, 0x58, 0x50),
    PF_RGB565(0x8C, 0x8C, 0x84), PF_RGB565(0xC0, 0xC0, 0xB8),
    PF_RGB565(0xE8, 0xE8, 0xE0), PF_RGB565(0x28, 0x10, 0x38),
    PF_RGB565(0x40, 0x20, 0x50), PF_RGB565(0x58, 0x30, 0x70),
    PF_RGB565(0x70, 0x48, 0xA0), PF_RGB565(0xC0, 0x90, 0x30),
    PF_RGB565(0xE8, 0xC0, 0x60), PF_RGB565(0xE8, 0xE0, 0xD0),
    PF_RGB565(0x78, 0x50, 0x28), PF_RGB565(0x6E, 0x3A, 0x2E),
    PF_RGB565(0x9A, 0x5A, 0x46), PF_RGB565(0x20, 0x08, 0x08),
    PF_RGB565(0xD8, 0xD0, 0xB8), PF_RGB565(0xA0, 0x48, 0x38),
};

enum { VE_W = 160, VE_H = 120 };

void pf_render_vga_elder(uint16_t *fb, const face_keyframe_t *k,
                         const pf_rig_t *rig, uint32_t clock) {
    (void)clock;
    pf_surface_t art = pf_surface_attach(fb, VE_W, VE_H);
    pf_surface_t *s = &art;

    /* Study wall: banded candle glow centred behind the head. */
    pf_clear(s, VE_BG0);
    pf_fill_ellipse(s, 84, 56, 74, 60, VE_BG1);
    pf_fill_ellipse(s, 86, 54, 52, 44, VE_BG2);
    /* Corner vignette. */
    pf_fill_rect_dither(s, 0, 0, 26, 10, VE_BG0, VE_BLACK, PF_PAT_BAYER8, 22);
    pf_fill_rect_dither(s, 134, 0, 26, 10, VE_BG0, VE_BLACK, PF_PAT_BAYER8,
                        22);

    int hy = rig->bob / 100 + rig->breath / 128; /* -1..1 head nod */
    int cx = 80;
    int fy = 50 + hy;

    /* Robe and shoulders (breath raises them slightly). */
    int shoulder = 98 + (rig->breath > 64 ? -1 : 0);
    pf_fill_rect(s, 8, shoulder, 144, VE_H - shoulder, VE_ROBE1);
    pf_fill_ellipse(s, cx, shoulder + 30, 62, 30, VE_ROBE1);
    pf_fill_ellipse(s, cx - 40, shoulder + 26, 20, 18, VE_ROBE2);
    pf_fill_ellipse(s, cx + 44, shoulder + 30, 20, 18, VE_ROBE0);
    pf_hline(s, 20, 140, shoulder + 6, VE_GOLD);
    pf_fill_circle(s, cx, shoulder + 12, 3, VE_GOLD);
    pf_px(s, cx - 1, shoulder + 11, VE_GOLD_HI);

    /* Side hair falls over the robe. */
    pf_fill_ellipse(s, cx - 27, 62 + hy, 8, 22, VE_BEARD1);
    pf_fill_ellipse(s, cx + 27, 62 + hy, 8, 22, VE_BEARD0);
    pf_fill_ellipse(s, cx - 28, 58 + hy, 4, 16, VE_BEARD2);

    /* Face: nested offset ellipses build the banded ramp. */
    pf_fill_ellipse(s, cx, fy, 25, 30, VE_SKIN1);
    pf_fill_ellipse(s, cx - 3, fy - 2, 22, 27, VE_SKIN2);
    pf_fill_ellipse(s, cx - 6, fy - 4, 17, 22, VE_SKIN3);
    pf_fill_ellipse(s, cx - 8, fy - 7, 10, 13, VE_SKIN4);

    /* Bald crown with a few liver spots; thin combed strands. */
    pf_px(s, cx + 6, fy - 24, VE_SKIN1);
    pf_px(s, cx - 2, fy - 26, VE_SKIN1);
    pf_px(s, cx + 12, fy - 21, VE_SKIN1);
    for (int i = 0; i < 4; ++i) {
        pf_line(s, cx - 20 + i * 3, fy - 27 - (i & 1), cx - 26, fy - 14 + i,
                VE_BEARD2);
    }

    /* Ears. */
    pf_fill_ellipse(s, cx - 26, fy + 4, 4, 7, VE_SKIN2);
    pf_fill_ellipse(s, cx + 26, fy + 4, 4, 7, VE_SKIN1);
    pf_px(s, cx - 26, fy + 4, VE_SKIN1);
    pf_px(s, cx + 26, fy + 4, VE_SKIN0);

    /* Candle rim light on the left cheek and temple, gated by flicker. */
    if (rig->flicker > 70) {
        int strength = rig->flicker > 170 ? 2 : 1;
        for (int dy = -22; dy <= 18; dy += 1) {
            uint32_t t = (uint32_t)(30 * 30 - dy * dy);
            int dx = (int)pf_isqrt(t * (25U * 25U) / (30U * 30U));
            pf_px(s, cx - dx + 1, fy + dy, VE_SKIN4);
            if (strength == 2 && (dy & 1)) {
                pf_px(s, cx - dx + 2, fy + dy, VE_SKIN3);
            }
        }
    }

    /* Forehead wrinkles: a third appears when the brows lift. */
    int wr = -rig->brow / 34; /* raise = negative shift up */
    pf_hline(s, cx - 12, cx + 10, fy - 16 + wr, VE_SKIN1);
    pf_hline(s, cx - 14, cx + 12, fy - 12 + wr / 2, VE_SKIN1);
    if (rig->brow > 30) {
        pf_hline(s, cx - 10, cx + 8, fy - 20 + wr, VE_SKIN1);
    }

    /* Deep-set eyes. */
    int gx = rig->gaze_x / 42; /* -3..3 */
    int gy = rig->gaze_y / 74; /* -1..1 */
    int ey = fy - 2;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 11 : cx - 11;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_ellipse(s, ex, ey, 8, 5, VE_SKIN1); /* socket shadow */
        pf_fill_ellipse(s, ex, ey, 6, 3, VE_EYE_WHITE);
        pf_fill_circle(s, ex + gx, ey + gy, 2, VE_IRIS);
        pf_px(s, ex + gx, ey + gy, VE_BLACK);
        pf_px(s, ex + gx - 1, ey + gy - 1, VE_BEARD3); /* catchlight */
        int lid = ((255 - open) * 7) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 6, ey - 4, 13, lid, VE_SKIN2);
            pf_hline(s, ex - 6, ex + 6, ey - 5 + lid, VE_SKIN1);
        }
        pf_hline(s, ex - 5, ex + 5, ey + 4, VE_SKIN1); /* lower bag */
        int cf = side ? ex + 7 : ex - 7;
        pf_px(s, cf, ey, VE_SKIN1); /* crow's feet */
        pf_px(s, cf + (side ? 1 : -1), ey - 1, VE_SKIN1);
        pf_px(s, cf + (side ? 1 : -1), ey + 1, VE_SKIN1);
    }

    /* Bushy grey brows ride the rig. */
    int bl = -rig->brow / 26;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 11 : cx - 11;
        int slant = side ? -rig->brow / 64 : rig->brow / 64;
        pf_fill_ellipse(s, ex, ey - 7 + bl, 7, 2, VE_BEARD2);
        pf_fill_ellipse(s, ex + (side ? 3 : -3), ey - 8 + bl + slant, 4, 1,
                        VE_BEARD3);
    }

    /* Long nose. */
    pf_vline(s, cx - 1, fy + 2, fy + 12, VE_SKIN1);
    pf_vline(s, cx - 2, fy + 6, fy + 12, VE_SKIN2);
    pf_fill_ellipse(s, cx - 1, fy + 13, 4, 3, VE_SKIN2);
    pf_px(s, cx - 3, fy + 12, VE_SKIN4); /* tip light */
    pf_px(s, cx - 4, fy + 14, VE_SKIN0); /* nostril */
    pf_px(s, cx + 2, fy + 14, VE_SKIN0);

    /* Beard: layered mass with strand texture, then the mouth cave. */
    int by = fy + 40;
    pf_fill_ellipse(s, cx, by, 29, 27, VE_BEARD1);
    pf_fill_ellipse(s, cx - 2, by - 3, 25, 23, VE_BEARD2);
    pf_fill_ellipse(s, cx, by + 8, 18, 16, VE_BEARD1);
    for (int i = -24; i <= 24; i += 2) {
        uint32_t h = pf_hash32(0xBEA4D0U ^ (uint32_t)(i + 40));
        int x = cx + i;
        /* Keep strands inside the beard mass: clip to the ellipse edge. */
        uint32_t t = (uint32_t)(29 * 29 - i * i);
        int edge = (int)pf_isqrt(t * (27U * 27U) / (29U * 29U));
        int y0 = by + 2 + (int)((h >> 8) % 5U);
        int y1 = pf_mini(y0 + 6 + (int)(h % 9U), by + edge - 2);
        if (y1 > y0) {
            pf_vline(s, x, y0, y1, (h & 2U) ? VE_BEARD0 : VE_BEARD3);
        }
    }

    /* Mouth cave and continuous lips. */
    int my = fy + 22;
    pf_fill_ellipse(s, cx, my + 1, 15, 7, VE_BEARD0);
    pf_lips_t lips = {
        cx, my, 12, 16, VE_LIP_DARK, VE_LIP_MID, VE_CAVITY, VE_TEETH,
        VE_TONGUE,
    };
    pf_draw_lips(s, &lips, k);

    /* Moustache swoops drawn over the top lip. */
    pf_fill_ellipse(s, cx - 9, my - 3, 8, 2, VE_BEARD2);
    pf_fill_ellipse(s, cx + 9, my - 3, 8, 2, VE_BEARD2);
    pf_fill_ellipse(s, cx - 14, my - 1, 4, 2, VE_BEARD3);
    pf_fill_ellipse(s, cx + 14, my - 1, 4, 2, VE_BEARD3);

    pf_present(fb, s, 1, 1, ve_palette, PF_FX_NONE);
}
