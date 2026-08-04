#include "pf_internal.h"

/*
 * Pixel Automaton — a riveted service robot: brushed-metal head, two 5×5
 * LED-matrix eyes whose lit block tracks gaze and collapses when blinking,
 * a seven-column VU-segment mouth driven by mouth_open with per-column
 * shimmer while speaking, and blinking chassis status lights.
 */

enum {
    AU_BG = 0,
    AU_GRID,
    AU_METAL_DK,
    AU_METAL,
    AU_METAL_LT,
    AU_METAL_HI,
    AU_OUTLINE,
    AU_LED_OFF,
    AU_LED_GRN,
    AU_LED_GRN_DIM,
    AU_LED_AMBER,
    AU_LED_RED,
    AU_GLASS,
    AU_RIVET,
    AU_ANTENNA,
    AU_PANEL,
    AU_LED_CYAN,
    AU_LED_WARM,
    AU_COLOUR_COUNT,
};

static const uint16_t au_palette[AU_COLOUR_COUNT] = {
    PF_RGB565(0x10, 0x14, 0x18), PF_RGB565(0x16, 0x1B, 0x22),
    PF_RGB565(0x48, 0x50, 0x58), PF_RGB565(0x68, 0x70, 0x78),
    PF_RGB565(0x8C, 0x94, 0xA0), PF_RGB565(0xB0, 0xB8, 0xC4),
    PF_RGB565(0x08, 0x0A, 0x0C), PF_RGB565(0x14, 0x24, 0x1C),
    PF_RGB565(0x30, 0xE0, 0x60), PF_RGB565(0x18, 0xA0, 0x40),
    PF_RGB565(0xF0, 0xB0, 0x20), PF_RGB565(0xE0, 0x40, 0x30),
    PF_RGB565(0x0C, 0x14, 0x10), PF_RGB565(0xC8, 0xD0, 0xDC),
    PF_RGB565(0x90, 0x98, 0xA4), PF_RGB565(0x14, 0x1A, 0x1E),
    PF_RGB565(0x40, 0xC8, 0xE0), PF_RGB565(0xF0, 0xF0, 0xD8),
};

enum { AU_W = 80, AU_H = 60 };

static void au_eye_matrix(pf_surface_t *s, int ox, int oy, int open,
                          int gaze_x, int gaze_y) {
    /* 5×5 LEDs, each 2×2 art px with a 1 px gap (14×14 total). */
    int px = pf_clampi(1 + gaze_x / 52, 0, 3);
    int py = pf_clampi(1 + gaze_y / 74, 0, 3);
    /* Blink collapses lit rows toward the centre row. */
    int half = (open * 5 + 127) / 255; /* 0..5 visible rows (about) */
    for (int r = 0; r < 5; ++r) {
        int dist = pf_absi(r * 2 - 4); /* 0 centre, 4 edge */
        int visible = dist / 2 < half;
        for (int c = 0; c < 5; ++c) {
            uint8_t colour = AU_LED_OFF;
            if (visible) {
                int in_pupil =
                    r >= py && r <= py + 1 && c >= px && c <= px + 1;
                int on_ring =
                    r >= py - 1 && r <= py + 2 && c >= px - 1 &&
                    c <= px + 2 && !in_pupil;
                /* Only the pupil block and its one-cell ring light up, so
                 * the gaze reads clearly against dark LEDs. */
                if (in_pupil) {
                    colour = AU_LED_GRN;
                } else if (on_ring && ((r + c) & 1)) {
                    colour = AU_LED_GRN_DIM;
                }
            }
            pf_fill_rect(s, ox + c * 3, oy + r * 3, 2, 2, colour);
        }
    }
}

void pf_render_pixel_automaton(uint16_t *fb, const face_keyframe_t *k,
                               const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, AU_W, AU_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    /* Workshop backdrop with a faint alignment grid. */
    pf_clear(s, AU_BG);
    for (int x = 0; x < AU_W; x += 8) {
        pf_vline(s, x, 0, AU_H - 1, AU_GRID);
    }
    for (int y = 0; y < AU_H; y += 8) {
        pf_hline(s, 0, AU_W - 1, y, AU_GRID);
    }

    int hy = rig->breath / 100; /* -1..1 chassis breathing */

    /* Antenna: wags with the head-bob, tip LED blinks slowly. */
    int wag = rig->bob / 64 + rig->sacc_x;
    pf_vline(s, 40, 2 + hy, 7 + hy, AU_ANTENNA);
    pf_line(s, 40, 4 + hy, 40 + wag / 2, 2 + hy, AU_ANTENNA);
    uint8_t tip = ((ms / 700U) & 1U) ? AU_LED_RED : AU_METAL_DK;
    pf_fill_rect(s, 39 + wag / 2, hy, 3, 3, tip);

    /* Head shell: brushed metal with rounded corners and rivets. */
    int top = 8 + hy;
    pf_fill_rect(s, 18, top, 44, 40, AU_METAL);
    for (int x = 18; x < 62; ++x) {
        if (((x - 18) / 3) & 1) {
            pf_vline(s, x, top, top + 39, AU_METAL_LT);
        }
    }
    pf_fill_rect(s, 18, top, 44, 2, AU_METAL_HI);
    pf_fill_rect(s, 18, top + 38, 44, 2, AU_METAL_DK);
    pf_rect(s, 18, top, 44, 40, AU_OUTLINE);
    /* Rounded corner trims. */
    pf_px(s, 18, top, AU_BG);
    pf_px(s, 61, top, AU_BG);
    pf_px(s, 18, top + 39, AU_BG);
    pf_px(s, 61, top + 39, AU_BG);
    /* Rivets. */
    pf_px(s, 20, top + 2, AU_RIVET);
    pf_px(s, 59, top + 2, AU_RIVET);
    pf_px(s, 20, top + 37, AU_RIVET);
    pf_px(s, 59, top + 37, AU_RIVET);
    /* Side bolts. */
    pf_fill_rect(s, 15, top + 16, 3, 6, AU_METAL_DK);
    pf_fill_rect(s, 62, top + 16, 3, 6, AU_METAL_DK);
    pf_px(s, 16, top + 18, AU_RIVET);
    pf_px(s, 63, top + 18, AU_RIVET);

    /* Brow plate tilts with the rig. */
    int bl = -rig->brow / 40;
    pf_fill_rect(s, 22, top + 5 + bl, 16, 2, AU_METAL_DK);
    pf_fill_rect(s, 42, top + 5 + bl, 16, 2, AU_METAL_DK);

    /* Eye bays. */
    pf_fill_rect(s, 22, top + 8, 16, 16, AU_GLASS);
    pf_fill_rect(s, 42, top + 8, 16, 16, AU_GLASS);
    pf_rect(s, 22, top + 8, 16, 16, AU_OUTLINE);
    pf_rect(s, 42, top + 8, 16, 16, AU_OUTLINE);
    au_eye_matrix(s, 23, top + 9, rig->eye_open_l, rig->gaze_x, rig->gaze_y);
    au_eye_matrix(s, 43, top + 9, rig->eye_open_r, rig->gaze_x, rig->gaze_y);

    /* Cheek vents. */
    for (int i = 0; i < 3; ++i) {
        pf_hline(s, 20, 24, top + 27 + i * 2, AU_METAL_DK);
        pf_hline(s, 56, 60, top + 27 + i * 2, AU_METAL_DK);
    }

    /* VU-segment mouth: seven columns, arch-weighted, shimmer when
     * speaking. Silent idle keeps a single dim centre row alive. */
    int mx = 26, my = top + 26, seg_w = 3, seg_h = 2;
    pf_fill_rect(s, mx - 2, my - 1, 7 * (seg_w + 1) + 3, 5 * (seg_h + 1) + 1,
                 AU_PANEL);
    pf_rect(s, mx - 2, my - 1, 7 * (seg_w + 1) + 3, 5 * (seg_h + 1) + 1,
            AU_OUTLINE);
    int speaking = (k->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0;
    for (int c = 0; c < 7; ++c) {
        int arch = 256 - (pf_absi(c - 3) * 60); /* centre columns taller */
        int level = ((int)k->mouth_open * arch) >> 8;
        if (speaking) {
            uint32_t jit =
                pf_hash32((uint32_t)c * 0x9E37U ^ (clock >> 9) ^ rig->seed);
            level += (int)(jit % 71U) - 35;
        }
        int lit = pf_clampi((level * 5) / 256, 0, 5);
        if (!speaking && lit == 0) {
            lit = 0;
        }
        for (int rrow = 0; rrow < 5; ++rrow) {
            /* Row 2 is the centre; fill outward symmetrically. */
            int dist = pf_absi(rrow - 2);
            uint8_t colour = AU_LED_OFF;
            if (dist * 2 < lit) {
                colour = (dist == 2) ? AU_LED_RED
                         : (dist == 1) ? AU_LED_AMBER
                                       : AU_LED_GRN;
            } else if (rrow == 2 && !speaking) {
                colour = AU_LED_GRN_DIM;
            }
            pf_fill_rect(s, mx + c * (seg_w + 1), my + rrow * (seg_h + 1),
                         seg_w, seg_h, colour);
        }
    }

    /* Chin plate + chest status LEDs. */
    pf_fill_rect(s, 30, 50 + hy, 20, 4, AU_METAL_DK);
    pf_fill_rect(s, 24, 54 + hy, 32, 6, AU_METAL);
    pf_rect(s, 24, 54 + hy, 32, 6, AU_OUTLINE);
    uint8_t l0 = ((ms / 430U) % 3U) == 0U ? AU_LED_CYAN : AU_LED_OFF;
    uint8_t l1 = ((ms / 430U) % 3U) == 1U ? AU_LED_AMBER : AU_LED_OFF;
    uint8_t l2 = ((ms / 430U) % 3U) == 2U ? AU_LED_WARM : AU_LED_OFF;
    pf_fill_rect(s, 28, 56 + hy, 2, 2, l0);
    pf_fill_rect(s, 34, 56 + hy, 2, 2, l1);
    pf_fill_rect(s, 40, 56 + hy, 2, 2, l2);
    pf_text3x5(s, 46, 55 + hy, "A7", AU_RIVET);

    pf_present(fb, s, 2, 2, au_palette, PF_FX_NONE);
}
