#include "pf_internal.h"
#include "pf_mouthbank.h"

/*
 * EGA Quest Portrait — a Sierra-quest-style hero box: 16 fixed colours,
 * chunky 2× pixels, checkerboard dither standing in for the midtones EGA
 * never had, and a name plate under the portrait. Original character:
 * Sir Rowan, a young ranger with a feathered cap.
 */

enum {
    EGA_BLACK = 0,
    EGA_BLUE = 1,
    EGA_GREEN = 2,
    EGA_CYAN = 3,
    EGA_RED = 4,
    EGA_MAGENTA = 5,
    EGA_BROWN = 6,
    EGA_LGRAY = 7,
    EGA_DGRAY = 8,
    EGA_LBLUE = 9,
    EGA_LGREEN = 10,
    EGA_LCYAN = 11,
    EGA_LRED = 12,
    EGA_LMAGENTA = 13,
    EGA_YELLOW = 14,
    EGA_WHITE = 15,
};

static const uint16_t ega_palette[16] = {
    PF_RGB565(0x00, 0x00, 0x00), PF_RGB565(0x00, 0x00, 0xAA),
    PF_RGB565(0x00, 0xAA, 0x00), PF_RGB565(0x00, 0xAA, 0xAA),
    PF_RGB565(0xAA, 0x00, 0x00), PF_RGB565(0xAA, 0x00, 0xAA),
    PF_RGB565(0xAA, 0x55, 0x00), PF_RGB565(0xAA, 0xAA, 0xAA),
    PF_RGB565(0x55, 0x55, 0x55), PF_RGB565(0x55, 0x55, 0xFF),
    PF_RGB565(0x55, 0xFF, 0x55), PF_RGB565(0x55, 0xFF, 0xFF),
    PF_RGB565(0xFF, 0x55, 0x55), PF_RGB565(0xFF, 0x55, 0xFF),
    PF_RGB565(0xFF, 0xFF, 0x55), PF_RGB565(0xFF, 0xFF, 0xFF),
};

enum { EGA_W = 80, EGA_H = 60 };

void pf_render_ega_quest(uint16_t *fb, const face_keyframe_t *k,
                         const pf_rig_t *rig, uint32_t clock) {
    (void)clock;
    pf_surface_t art = pf_surface_attach(fb, EGA_W, EGA_H);
    pf_surface_t *s = &art;

    /* Dialogue box: lit bevel over a plain blue field. */
    pf_clear(s, EGA_BLUE);
    pf_rect(s, 0, 0, EGA_W, EGA_H, EGA_WHITE);
    pf_rect(s, 1, 1, EGA_W - 2, EGA_H - 2, EGA_DGRAY);

    /* Breathing shifts the torso less than the head; bob adds speech energy. */
    int breathe = rig->breath / 100;               /* -1..1 */
    int hy = breathe + rig->bob / 90;              /* head dy, -2..2 */
    int hx = 40;

    /* Tunic + shoulders. */
    pf_fill_rect(s, 18, 50, 44, 8, EGA_GREEN);
    pf_fill_rect_dither(s, 18, 50, 44, 2, EGA_GREEN, EGA_LGREEN,
                        PF_PAT_CHECKER, 24);

    /* Neck. */
    pf_fill_rect(s, 36, 44 + breathe, 8, 7, EGA_LRED);
    pf_fill_rect_dither(s, 36, 44 + breathe, 8, 2, EGA_LRED, EGA_RED,
                        PF_PAT_CHECKER, 32);

    /* Face: LRED base, RED checker shadow on the right, YELLOW highlight. */
    int fy = 27 + hy;
    pf_fill_ellipse(s, hx, fy, 13, 15, EGA_LRED);
    pf_fill_ellipse_dither(s, hx + 6, fy + 2, 7, 12, EGA_LRED, EGA_RED,
                           PF_PAT_CHECKER, 32);
    pf_fill_ellipse(s, hx - 3, fy - 1, 8, 11, EGA_LRED);

    /* Ears. */
    pf_fill_rect(s, hx - 15, fy + 1, 3, 5, EGA_LRED);
    pf_fill_rect(s, hx + 12, fy + 1, 3, 5, EGA_LRED);
    pf_px(s, hx - 14, fy + 3, EGA_RED);
    pf_px(s, hx + 13, fy + 3, EGA_RED);

    /* Hair fringe under the cap. */
    pf_fill_rect_dither(s, hx - 12, fy - 12, 24, 4, EGA_BROWN, EGA_RED,
                        PF_PAT_CHECKER, 24);

    /* Feathered cap, swaying feather. */
    int cap_y = fy - 14;
    pf_fill_ellipse(s, hx, cap_y, 14, 5, EGA_GREEN);
    pf_fill_rect(s, hx - 14, cap_y + 1, 28, 3, EGA_GREEN);
    pf_fill_rect_dither(s, hx - 14, cap_y - 2, 28, 3, EGA_GREEN, EGA_LGREEN,
                        PF_PAT_CHECKER, 24);
    pf_hline(s, hx - 14, hx + 14, cap_y + 4, EGA_BLACK);
    int sway = pf_sin8((uint8_t)((rig->breath + 127) / 2)) / 48; /* -2..2 */
    pf_line(s, hx + 12, cap_y - 1, hx + 17 + sway, cap_y - 9, EGA_YELLOW);
    pf_line(s, hx + 13, cap_y, hx + 19 + sway, cap_y - 7, EGA_YELLOW);
    pf_px(s, hx + 17 + sway, cap_y - 10, EGA_WHITE);

    /* Eyes: whites, blue iris tracking gaze, skin lids per blink. */
    int gx = rig->gaze_x / 48; /* -2..2 */
    int gy = rig->gaze_y / 74; /* -1..1 */
    int ey = fy - 2;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? hx + 6 : hx - 6;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_rect(s, ex - 3, ey - 2, 7, 5, EGA_WHITE);
        pf_rect(s, ex - 3, ey - 2, 7, 5, EGA_DGRAY);
        pf_fill_rect(s, ex - 1 + gx, ey - 1 + gy, 2, 3, EGA_BLUE);
        pf_px(s, ex - 1 + gx, ey + gy, EGA_BLACK);
        int lid = ((255 - open) * 6) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 3, ey - 2, 7, lid, EGA_LRED);
            pf_hline(s, ex - 3, ex + 3, ey - 3 + lid, EGA_RED);
        }
    }

    /* Brows: brown bars that tilt with the rig. */
    int browlift = -rig->brow / 36; /* +raise = up */
    int tilt = rig->brow < -40 ? 1 : 0;
    pf_fill_rect(s, hx - 9, ey - 5 + browlift + tilt, 7, 2, EGA_BROWN);
    pf_fill_rect(s, hx + 3, ey - 5 + browlift, 7, 2, EGA_BROWN);

    /* Nose: small shadow L. */
    pf_vline(s, hx, fy + 2, fy + 5, EGA_RED);
    pf_hline(s, hx, hx + 1, fy + 5, EGA_RED);

    /* Mouth from the shared sprite bank. */
    static const uint8_t mouth_cols[5] = {
        EGA_RED, EGA_LRED, EGA_BLACK, EGA_WHITE, EGA_LRED,
    };
    pf_draw_mouth_bank(s, hx, fy + 10, pf_mouth_classify(k), mouth_cols);

    /* Chin shade. */
    pf_fill_rect_dither(s, hx - 4, fy + 13, 9, 2, EGA_LRED, EGA_RED,
                        PF_PAT_CHECKER, 16);

    /* Name plate. */
    pf_fill_rect(s, 16, 51, 48, 8, EGA_BLACK);
    pf_rect(s, 16, 51, 48, 8, EGA_LGRAY);
    pf_hline(s, 17, 62, 58, EGA_DGRAY);
    pf_text3x5(s, 23, 53, "SIR ROWAN", EGA_WHITE);

    pf_present(fb, s, 2, 2, ega_palette, PF_FX_NONE);
}
