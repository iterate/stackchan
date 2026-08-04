#include "pf_internal.h"
#include "pf_mouthbank.h"

/*
 * CGA Arcade Cadet — the four unforgiving colours of CGA palette 1 (high
 * intensity): black, cyan, magenta, white. Shading comes entirely from
 * diagonal cross-hatch fills at 25/50/75 percent, the way early PC ports
 * faked midtones. A twinkling starfield and a blinking INSERT COIN line
 * give it the attract-mode feel.
 */

enum {
    CGA_BLACK = 0,
    CGA_CYAN,
    CGA_MAGENTA,
    CGA_WHITE,
};

static const uint16_t cga_palette[4] = {
    PF_RGB565(0x00, 0x00, 0x00), PF_RGB565(0x55, 0xFF, 0xFF),
    PF_RGB565(0xFF, 0x55, 0xFF), PF_RGB565(0xFF, 0xFF, 0xFF),
};

enum { CGA_W = 80, CGA_H = 60 };

void pf_render_cga_arcade(uint16_t *fb, const face_keyframe_t *k,
                          const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, CGA_W, CGA_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    /* Starfield: hash positions, twinkle colour by clock. */
    pf_clear(s, CGA_BLACK);
    for (int i = 0; i < 22; ++i) {
        uint32_t h = pf_hash32(0x57A2U ^ (uint32_t)i);
        uint8_t c = (((ms / 340U) + (h >> 16)) & 3U) == 0U ? CGA_WHITE
                                                           : CGA_CYAN;
        pf_px(s, (int)(h % CGA_W), (int)((h >> 8) % 50U), c);
    }

    int hy = rig->breath / 110 + rig->bob / 100;
    int cx = 40;
    int fy = 27 + hy;

    /* Suit chest with hatched shading and a chest lamp. */
    pf_fill_rect_dither(s, 14, 48, 52, 12, CGA_BLACK, CGA_CYAN, PF_PAT_DIAG,
                        48);
    pf_hline(s, 14, 65, 48, CGA_WHITE);
    pf_fill_rect(s, 36, 50, 8, 5, CGA_MAGENTA);
    pf_px(s, 39, 52, CGA_WHITE);

    /* Helmet dome: white ring, hatched glass, antenna nub. */
    pf_fill_circle(s, cx, fy, 24, CGA_WHITE);
    pf_fill_circle(s, cx, fy, 22, CGA_BLACK);
    pf_fill_rect_dither(s, cx - 22, fy - 22, 44, 12, CGA_BLACK, CGA_CYAN,
                        PF_PAT_DIAG, 16);
    pf_px(s, cx - 10, fy - 18, CGA_WHITE); /* glass glint */
    pf_px(s, cx - 12, fy - 16, CGA_WHITE);
    pf_vline(s, cx, fy - 27, fy - 24, CGA_WHITE);
    uint8_t beacon = ((ms / 260U) & 1U) ? CGA_MAGENTA : CGA_BLACK;
    pf_fill_rect(s, cx - 1, fy - 30, 3, 3, beacon);

    /* Face inside the glass: white skin, magenta hatch shading. */
    pf_fill_ellipse(s, cx, fy + 3, 15, 16, CGA_WHITE);
    pf_fill_ellipse_dither(s, cx + 6, fy + 6, 9, 12, CGA_WHITE, CGA_MAGENTA,
                           PF_PAT_DIAG, 16);
    pf_fill_ellipse(s, cx - 4, fy + 1, 10, 11, CGA_WHITE);
    /* Cyan hair fringe. */
    pf_fill_ellipse(s, cx - 2, fy - 8, 13, 5, CGA_CYAN);
    pf_fill_rect_dither(s, cx - 13, fy - 6, 26, 3, CGA_CYAN, CGA_BLACK,
                        PF_PAT_DIAG, 24);

    /* Eyes. */
    int gx = rig->gaze_x / 48;
    int gy = rig->gaze_y / 74;
    int ey = fy + 1;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 6 : cx - 6;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_rect(s, ex - 3, ey - 2, 7, 5, CGA_BLACK);
        pf_fill_rect(s, ex - 2, ey - 1, 5, 3, CGA_WHITE);
        pf_fill_rect(s, ex - 1 + gx, ey - 1 + gy, 2, 3, CGA_BLACK);
        pf_px(s, ex + gx, ey - 1 + gy, CGA_CYAN);
        int lid = ((255 - open) * 5) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 3, ey - 2, 7, lid, CGA_WHITE);
            pf_hline(s, ex - 3, ex + 3, ey - 3 + lid, CGA_BLACK);
        }
    }
    /* Brows: magenta dashes. */
    int bl = -rig->brow / 36;
    pf_hline(s, cx - 9, cx - 3, ey - 4 + bl, CGA_MAGENTA);
    pf_hline(s, cx + 3, cx + 9, ey - 4 + bl, CGA_MAGENTA);

    /* Nose. */
    pf_vline(s, cx, fy + 5, fy + 8, CGA_MAGENTA);
    pf_px(s, cx + 1, fy + 8, CGA_MAGENTA);

    /* Mouth from the shared bank. */
    static const uint8_t mouth_cols[5] = {
        CGA_MAGENTA, CGA_WHITE, CGA_BLACK, CGA_WHITE, CGA_MAGENTA,
    };
    pf_draw_mouth_bank(s, cx, fy + 13, pf_mouth_classify(k), mouth_cols);

    /* Helmet collar seals the suit. */
    pf_fill_rect(s, cx - 14, fy + 19, 28, 3, CGA_WHITE);
    pf_fill_rect_dither(s, cx - 14, fy + 20, 28, 2, CGA_WHITE, CGA_CYAN,
                        PF_PAT_DIAG, 32);

    /* Attract-mode line on its own black strip. */
    pf_fill_rect(s, 0, 52, CGA_W, 8, CGA_BLACK);
    if ((ms / 470U) & 1U) {
        pf_text3x5(s, 17, 54, "INSERT COIN", CGA_WHITE);
    } else {
        pf_text3x5(s, 29, 54, "READY", CGA_CYAN);
    }
    /* Hi-score corner. */
    pf_text3x5(s, 2, 2, "1UP", CGA_MAGENTA);

    pf_present(fb, s, 2, 2, cga_palette, PF_FX_NONE);
}
