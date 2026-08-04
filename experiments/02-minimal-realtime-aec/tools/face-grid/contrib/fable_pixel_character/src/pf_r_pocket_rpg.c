#include "pf_internal.h"

/*
 * Pocket RPG Dialogue — a four-shade handheld JRPG scene: bordered
 * portrait window on the left, dialogue window on the right whose mock
 * text types on while the character speaks, hearts and a name in the
 * status strip. The portrait mouth is a deliberate three-frame flap, the
 * classic handheld dialogue idiom.
 */

enum {
    GB_DARKEST = 0, /* 0F380F */
    GB_DARK,        /* 306230 */
    GB_LIGHT,       /* 8BAC0F */
    GB_LIGHTEST,    /* 9BBC0F */
};

static const uint16_t gb_palette[4] = {
    PF_RGB565(0x0F, 0x38, 0x0F), PF_RGB565(0x30, 0x62, 0x30),
    PF_RGB565(0x8B, 0xAC, 0x0F), PF_RGB565(0x9B, 0xBC, 0x0F),
};

enum { GB_W = 80, GB_H = 60 };

static void gb_window(pf_surface_t *s, int x, int y, int w, int h) {
    pf_fill_rect(s, x, y, w, h, GB_LIGHTEST);
    pf_rect(s, x, y, w, h, GB_DARKEST);
    pf_rect(s, x + 1, y + 1, w - 2, h - 2, GB_LIGHT);
    pf_rect(s, x + 2, y + 2, w - 4, h - 4, GB_DARKEST);
}

static void gb_heart(pf_surface_t *s, int x, int y, uint8_t c) {
    static const char *const rows[4] = { "##.##", "#####", ".###.",
                                         "..#.." };
    uint8_t colours[1];
    colours[0] = c;
    pf_blit(s, x, y, rows, 4, "#", colours);
}

void pf_render_pocket_rpg(uint16_t *fb, const face_keyframe_t *k,
                          const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, GB_W, GB_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;
    int speaking = (k->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0;

    /* Field backdrop: light sky, checker meadow strip. */
    pf_clear(s, GB_LIGHTEST);
    pf_fill_rect_dither(s, 0, 44, GB_W, 16, GB_LIGHT, GB_LIGHTEST,
                        PF_PAT_CHECKER, 32);

    /* Portrait window. */
    gb_window(s, 3, 5, 32, 39);
    pf_fill_rect(s, 6, 8, 26, 33, GB_LIGHT);

    int hy = rig->breath / 128; /* -1..0..1 gentle bob */
    int cx = 19, cy = 24 + hy;

    /* Round-faced hero: spiky hair, big eyes, three-frame flap mouth. */
    pf_fill_ellipse(s, cx, cy, 10, 11, GB_DARKEST); /* outline */
    pf_fill_ellipse(s, cx, cy, 9, 10, GB_LIGHTEST); /* face */
    /* Spiky fringe. */
    pf_fill_ellipse(s, cx, cy - 6, 10, 5, GB_DARKEST);
    for (int i = 0; i < 5; ++i) {
        int sx = cx - 8 + i * 4;
        pf_line(s, sx, cy - 9, sx + 2, cy - 13, GB_DARKEST);
        pf_line(s, sx + 2, cy - 13, sx + 3, cy - 9, GB_DARKEST);
    }
    pf_fill_rect(s, cx - 10, cy - 13, 1, 3, GB_LIGHT); /* stray lock */

    /* Eyes: 2×3 blocks that squash on blink; brows ride the rig. */
    int gx = rig->gaze_x / 64;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 4 : cx - 5;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        int h = 1 + (open * 3) / 255; /* 1..4 rows */
        pf_fill_rect(s, ex + gx, cy - 1 + (4 - h) / 2, 2, h, GB_DARKEST);
        pf_px(s, ex + gx, cy - 1 + (4 - h) / 2, GB_LIGHT); /* catchlight */
        pf_hline(s, ex - 1, ex + 2, cy - 4 - rig->brow / 64, GB_DARK);
    }
    /* Blush marks. */
    pf_px(s, cx - 7, cy + 3, GB_LIGHT);
    pf_px(s, cx + 6, cy + 3, GB_LIGHT);
    /* Tiny nose. */
    pf_px(s, cx, cy + 3, GB_DARK);

    /* Three-frame flap mouth. */
    int open = k->mouth_open;
    if (k->mouth_press > 150) {
        open = 0;
    }
    if (open < 60) {
        pf_hline(s, cx - 2, cx + 2, cy + 6, GB_DARKEST);
    } else if (open < 160) {
        pf_fill_rect(s, cx - 2, cy + 5, 5, 3, GB_DARKEST);
        pf_hline(s, cx - 1, cx + 1, cy + 6, GB_DARK);
    } else {
        pf_fill_rect(s, cx - 3, cy + 4, 7, 5, GB_DARKEST);
        pf_fill_rect(s, cx - 2, cy + 7, 5, 2, GB_DARK); /* tongue */
    }

    /* Dialogue window with typing mock text. */
    gb_window(s, 37, 5, 40, 39);
    /* Typing budget in "letters"; loops every few seconds while speaking,
     * settles complete when idle. */
    int budget = speaking ? (int)((ms / 55U) % 90U) : 90;
    int line_caps[4] = { 15, 14, 15, 9 };
    for (int line = 0; line < 4; ++line) {
        int y = 9 + line * 8;
        int x = 40;
        int used = 0;
        for (int word = 0; word < 5 && used < line_caps[line]; ++word) {
            uint32_t h =
                pf_hash32(0x9B0CU ^ (uint32_t)(line * 7 + word));
            int len = 2 + (int)(h % 4U); /* word length in letters */
            for (int ch = 0; ch < len && used < line_caps[line]; ++ch) {
                if (budget > 0) {
                    uint8_t c = ((h >> (ch * 3)) & 7U) == 0 ? GB_DARK
                                                            : GB_DARKEST;
                    pf_fill_rect(s, x, y, 2, 4, c);
                }
                x += 2;
                ++used;
                --budget;
            }
            x += 2; /* word gap */
        }
    }
    /* Continue arrow blinks when the text is done. */
    if (!speaking && ((ms / 480U) & 1U)) {
        pf_px(s, 72, 39, GB_DARKEST);
        pf_hline(s, 71, 73, 38, GB_DARKEST);
        pf_hline(s, 70, 74, 37, GB_DARKEST);
    }

    /* Status strip: name + hearts. */
    pf_text3x5(s, 5, 48, "PIP LV3", GB_DARKEST);
    for (int i = 0; i < 3; ++i) {
        gb_heart(s, 36 + i * 7, 47, i < 2 ? GB_DARKEST : GB_DARK);
    }
    /* Coin counter mock. */
    pf_fill_circle(s, 62, 50, 2, GB_DARKEST);
    pf_px(s, 62, 50, GB_LIGHT);
    pf_text3x5(s, 66, 48, "42", GB_DARKEST);

    pf_present(fb, s, 2, 2, gb_palette, PF_FX_NONE);
}
