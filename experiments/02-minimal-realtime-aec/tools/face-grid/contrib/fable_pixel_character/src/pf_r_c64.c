#include "pf_internal.h"
#include "pf_mouthbank.h"

/*
 * C64 Multicolor Punk — the breadbin look: 80×120 art presented with
 * double-wide (2×1) pixels like multicolor bitmap mode, the community-
 * measured 16-colour palette, animated raster bars behind the character,
 * and the READY. prompt with a blinking cursor. Original character: Razz,
 * a mohawked demoscene punk.
 */

enum {
    C64_BLACK = 0,
    C64_WHITE,
    C64_RED,
    C64_CYAN,
    C64_PURPLE,
    C64_GREEN,
    C64_BLUE,
    C64_YELLOW,
    C64_ORANGE,
    C64_BROWN,
    C64_LRED,
    C64_DGRAY,
    C64_MGRAY,
    C64_LGREEN,
    C64_LBLUE,
    C64_LGRAY,
};

static const uint16_t c64_palette[16] = {
    PF_RGB565(0x00, 0x00, 0x00), PF_RGB565(0xFF, 0xFF, 0xFF),
    PF_RGB565(0x88, 0x39, 0x32), PF_RGB565(0x67, 0xB6, 0xBD),
    PF_RGB565(0x8B, 0x3F, 0x96), PF_RGB565(0x55, 0xA0, 0x49),
    PF_RGB565(0x40, 0x31, 0x8D), PF_RGB565(0xBF, 0xCE, 0x72),
    PF_RGB565(0x8B, 0x54, 0x29), PF_RGB565(0x57, 0x42, 0x00),
    PF_RGB565(0xB8, 0x69, 0x62), PF_RGB565(0x50, 0x50, 0x50),
    PF_RGB565(0x78, 0x78, 0x78), PF_RGB565(0x94, 0xE0, 0x89),
    PF_RGB565(0x78, 0x69, 0xC4), PF_RGB565(0x9F, 0x9F, 0x9F),
};

enum { C64_W = 80, C64_H = 120 };

void pf_render_c64_multicolor(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, C64_W, C64_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    /* Blue field with drifting raster bars. */
    pf_clear(s, C64_BLUE);
    int drift = (int)((ms / 60U) % 48U);
    static const uint8_t bar_cols[6] = {
        C64_PURPLE, C64_LBLUE, C64_CYAN, C64_LBLUE, C64_PURPLE, C64_BLUE,
    };
    for (int b = 0; b < 4; ++b) {
        int base = ((b * 34 + drift) % 136) - 8;
        for (int i = 0; i < 6; ++i) {
            pf_hline(s, 2, C64_W - 3, base + i, bar_cols[i]);
        }
    }
    /* Screen border (light blue, like the boot screen). */
    pf_rect(s, 0, 0, C64_W, C64_H, C64_LBLUE);
    pf_rect(s, 1, 1, C64_W - 2, C64_H - 2, C64_LBLUE);

    /* READY. prompt with blinking cursor. */
    pf_text3x5(s, 5, 6, "READY.", C64_LBLUE);
    if ((ms / 530U) & 1U) {
        pf_fill_rect(s, 5, 13, 4, 5, C64_LBLUE);
    }

    int hy = rig->breath / 100 + rig->bob / 90;
    int cx = 40;
    int fy = 62 + hy;

    /* Leather jacket with studs. */
    pf_fill_rect(s, 12, 100, 56, 18, C64_DGRAY);
    pf_fill_ellipse(s, cx, 118, 32, 20, C64_DGRAY);
    pf_line(s, cx - 16, 102, cx - 6, 116, C64_BLACK);
    pf_line(s, cx + 16, 102, cx + 6, 116, C64_BLACK);
    pf_fill_rect(s, cx - 4, 106, 8, 12, C64_MGRAY); /* shirt */
    for (int i = 0; i < 5; ++i) {
        pf_px(s, 16 + i * 4, 103 + (i & 1), C64_LGRAY);
        pf_px(s, 64 - i * 4, 103 + (i & 1), C64_LGRAY);
    }

    /* Neck. */
    pf_fill_rect(s, cx - 5, fy + 24, 10, 12, C64_LRED);
    pf_fill_rect(s, cx - 5, fy + 24, 10, 3, C64_RED);

    /* Shaved sides + skull. */
    pf_fill_ellipse(s, cx, fy, 15, 19, C64_LRED);
    pf_fill_ellipse(s, cx + 10, fy + 5, 5, 11, C64_RED);
    pf_fill_ellipse(s, cx - 3, fy - 2, 11, 14, C64_LRED);

    /* Mohawk: green spikes marching over the crown. */
    for (int i = 0; i < 6; ++i) {
        int bx = cx - 10 + i * 4;
        int lean = rig->breath / 80 + (i - 3) / 2;
        pf_line(s, bx, fy - 16, bx + lean, fy - 30 - (i % 2) * 3,
                C64_GREEN);
        pf_line(s, bx + 1, fy - 16, bx + lean + 1, fy - 29 - (i % 2) * 3,
                C64_LGREEN);
        pf_line(s, bx + 2, fy - 15, bx + lean + 2, fy - 26, C64_GREEN);
    }
    pf_fill_ellipse(s, cx, fy - 14, 12, 5, C64_GREEN);

    /* Ears + rings. */
    pf_fill_ellipse(s, cx - 15, fy + 4, 2, 4, C64_LRED);
    pf_fill_ellipse(s, cx + 15, fy + 4, 2, 4, C64_RED);
    pf_px(s, cx - 15, fy + 9, C64_YELLOW);
    pf_px(s, cx - 15, fy + 10, C64_YELLOW);
    pf_px(s, cx + 15, fy + 9, C64_YELLOW);

    /* Eyes with heavy liner. */
    int gx = rig->gaze_x / 48;
    int gy = rig->gaze_y / 74;
    int ey = fy - 1;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 6 : cx - 6;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_rect(s, ex - 3, ey - 2, 7, 6, C64_BLACK);
        pf_fill_rect(s, ex - 2, ey - 1, 5, 4, C64_WHITE);
        pf_fill_rect(s, ex - 1 + gx, ey + gy, 2, 3, C64_BROWN);
        pf_px(s, ex - 1 + gx, ey + gy, C64_BLACK);
        int lid = ((255 - open) * 6) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 3, ey - 2, 7, lid, C64_LRED);
            pf_hline(s, ex - 3, ex + 3, ey - 3 + lid, C64_BLACK);
        }
    }
    /* Pierced brow: studs above the right brow. */
    int bl = -rig->brow / 34;
    pf_hline(s, cx - 9, cx - 3, ey - 5 + bl, C64_BLACK);
    pf_hline(s, cx + 3, cx + 9, ey - 5 + bl, C64_BLACK);
    pf_px(s, cx + 7, ey - 7 + bl, C64_LGRAY);
    pf_px(s, cx + 9, ey - 6 + bl, C64_LGRAY);

    /* Nose with ring. */
    pf_vline(s, cx, fy + 3, fy + 8, C64_RED);
    pf_px(s, cx + 2, fy + 9, C64_YELLOW);

    /* Mouth from the shared bank. */
    static const uint8_t mouth_cols[5] = {
        C64_BLACK, C64_RED, C64_BLACK, C64_WHITE, C64_LRED,
    };
    pf_draw_mouth_bank(s, cx, fy + 15, pf_mouth_classify(k), mouth_cols);

    /* Jaw shade. */
    pf_fill_ellipse_dither(s, cx, fy + 20, 8, 3, C64_LRED, C64_RED,
                           PF_PAT_CHECKER, 24);

    pf_present(fb, s, 2, 1, c64_palette, PF_FX_NONE);
}
