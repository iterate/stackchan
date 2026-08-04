#include "pf_internal.h"
#include "pf_mouthbank.h"

/*
 * Talkie Closeup — early-90s "talkie" dialogue portrait: flat fills with
 * bold black outlines, a huge jaw that physically drops with mouth_open,
 * a nine-shape sprite mouth, asymmetric brow acting, and a mock verb bar.
 * Original character: Captain Marlow, a weather-beaten sea dog.
 */

enum {
    TK_BG = 0,
    TK_SPOT,
    TK_OUTLINE,
    TK_SKIN,
    TK_SKIN_SH,
    TK_SKIN_HI,
    TK_BAND,
    TK_BAND_DK,
    TK_BAND_DOT,
    TK_SHIRT,
    TK_SHIRT_SH,
    TK_GOLD,
    TK_EYE_WHITE,
    TK_PUPIL,
    TK_LIP_DARK,
    TK_LIP_MID,
    TK_CAVITY,
    TK_TEETH,
    TK_TONGUE,
    TK_HAIR,
    TK_BAR_BG,
    TK_BAR_TXT,
    TK_BAR_TXT_HI,
    TK_COLOUR_COUNT,
};

static const uint16_t tk_palette[TK_COLOUR_COUNT] = {
    PF_RGB565(0x18, 0x28, 0x30), PF_RGB565(0x2A, 0x40, 0x40),
    PF_RGB565(0x00, 0x00, 0x00), PF_RGB565(0xE0, 0xA0, 0x70),
    PF_RGB565(0xB8, 0x78, 0x50), PF_RGB565(0xF8, 0xC8, 0x90),
    PF_RGB565(0xC0, 0x30, 0x28), PF_RGB565(0x88, 0x20, 0x18),
    PF_RGB565(0xF0, 0xE8, 0xD0), PF_RGB565(0xE8, 0xE0, 0xD0),
    PF_RGB565(0xB0, 0xA8, 0x98), PF_RGB565(0xE8, 0xB8, 0x30),
    PF_RGB565(0xFF, 0xFF, 0xFF), PF_RGB565(0x20, 0x18, 0x10),
    PF_RGB565(0x70, 0x28, 0x20), PF_RGB565(0xA0, 0x40, 0x30),
    PF_RGB565(0x30, 0x0C, 0x08), PF_RGB565(0xF8, 0xF0, 0xE0),
    PF_RGB565(0xC0, 0x50, 0x40), PF_RGB565(0x40, 0x28, 0x18),
    PF_RGB565(0x28, 0x24, 0x30), PF_RGB565(0x98, 0x90, 0xB8),
    PF_RGB565(0xE0, 0xD8, 0xF8),
};

enum { TK_W = 80, TK_H = 60 };

void pf_render_talkie_closeup(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock) {
    (void)clock;
    pf_surface_t art = pf_surface_attach(fb, TK_W, TK_H);
    pf_surface_t *s = &art;

    pf_clear(s, TK_BG);
    pf_fill_ellipse(s, 40, 26, 30, 24, TK_SPOT);

    int hy = rig->bob / 80 + rig->breath / 128; /* -1..1 */
    int hx = 40 + rig->bob / 110;
    int fy = 26 + hy;
    int jaw = ((int)k->mouth_open * 3) / 255; /* jaw drop 0..3 px */

    /* Shirt + shoulders. */
    pf_fill_rect(s, 12, 47, 56, 6, TK_SHIRT);
    pf_fill_rect(s, 12, 47, 12, 6, TK_SHIRT_SH);
    pf_hline(s, 12, 67, 47, TK_OUTLINE);
    pf_line(s, 36, 53, 40, 48, TK_SHIRT_SH); /* collar V */
    pf_line(s, 44, 53, 40, 48, TK_SHIRT_SH);

    /* Neck. */
    pf_fill_rect(s, hx - 5, fy + 16, 10, 7, TK_SKIN);
    pf_vline(s, hx - 5, fy + 16, fy + 22, TK_OUTLINE);
    pf_vline(s, hx + 4, fy + 16, fy + 22, TK_OUTLINE);

    /* Ears sit behind the head silhouette; the earring hangs free. */
    pf_fill_ellipse(s, hx - 17, fy + 3, 3, 5, TK_OUTLINE);
    pf_fill_ellipse(s, hx + 17, fy + 3, 3, 5, TK_OUTLINE);
    pf_fill_ellipse(s, hx - 17, fy + 3, 2, 4, TK_SKIN);
    pf_fill_ellipse(s, hx + 17, fy + 3, 2, 4, TK_SKIN_SH);

    /* Jaw block: outlined, drops with mouth_open. */
    pf_fill_ellipse(s, hx, fy + 12 + jaw, 12, 9, TK_OUTLINE);
    pf_fill_ellipse(s, hx, fy + 12 + jaw, 11, 8, TK_SKIN);
    /* Head: outlined flat fill with one shadow band. */
    pf_fill_ellipse(s, hx, fy, 17, 15, TK_OUTLINE);
    pf_fill_ellipse(s, hx, fy, 16, 14, TK_SKIN);
    pf_fill_ellipse(s, hx + 9, fy + 4, 6, 9, TK_SKIN_SH);
    pf_fill_ellipse(s, hx - 4, fy - 2, 10, 10, TK_SKIN);
    pf_fill_ellipse(s, hx - 7, fy - 4, 6, 6, TK_SKIN_HI);

    /* Stubble on the jaw. */
    pf_fill_ellipse_dither(s, hx, fy + 14 + jaw, 8, 4, TK_SKIN, TK_SKIN_SH,
                           PF_PAT_CHECKER, 14);

    int swing = rig->bob / 70 + rig->sacc_x / 2;
    pf_px(s, hx - 18 + swing / 2, fy + 8, TK_GOLD);
    pf_fill_circle(s, hx - 18 + swing, fy + 10, 1, TK_GOLD);

    /* Bandana with knot and polka dots. */
    pf_fill_ellipse(s, hx, fy - 11, 17, 8, TK_OUTLINE);
    pf_fill_ellipse(s, hx, fy - 11, 16, 7, TK_BAND);
    pf_fill_rect(s, hx - 16, fy - 10, 33, 4, TK_BAND);
    pf_hline(s, hx - 16, hx + 16, fy - 6, TK_OUTLINE);
    pf_fill_ellipse(s, hx - 12, fy - 14, 6, 3, TK_BAND_DK);
    for (int i = 0; i < 7; ++i) {
        uint32_t h = pf_hash32(0xD07U ^ (uint32_t)i);
        int dx = (int)(h % 27U) - 13;
        int dy = -8 - (int)((h >> 8) % 6U);
        pf_px(s, hx + dx, fy + dy, TK_BAND_DOT);
    }
    /* Knot tails on the right. */
    pf_line(s, hx + 15, fy - 10, hx + 20, fy - 5, TK_BAND);
    pf_line(s, hx + 16, fy - 11, hx + 21, fy - 7, TK_BAND_DK);
    pf_px(s, hx + 20, fy - 4, TK_BAND);

    /* Sideburn wisps. */
    pf_vline(s, hx - 15, fy + 2, fy + 6, TK_HAIR);
    pf_vline(s, hx + 15, fy + 2, fy + 5, TK_HAIR);

    /* Big talkie eyes. */
    int gx = rig->gaze_x / 42;
    int gy = rig->gaze_y / 74;
    int ey = fy - 1;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? hx + 7 : hx - 7;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_ellipse(s, ex, ey, 5, 4, TK_OUTLINE);
        pf_fill_ellipse(s, ex, ey, 4, 3, TK_EYE_WHITE);
        pf_fill_rect(s, ex - 1 + gx, ey - 1 + gy, 2, 2, TK_PUPIL);
        pf_px(s, ex + gx, ey - 1 + gy, TK_EYE_WHITE);
        int lid = ((255 - open) * 7) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 5, ey - 4, 11, lid, TK_SKIN);
            pf_hline(s, ex - 4, ex + 4, ey - 5 + lid, TK_OUTLINE);
        }
    }

    /* Asymmetric acting brows: the left one does the work. */
    int bl = -rig->brow / 30;
    int br = -rig->brow / 80;
    pf_fill_rect(s, hx - 11, ey - 7 + bl, 8, 2, TK_OUTLINE);
    pf_fill_rect(s, hx + 4, ey - 7 + br, 8, 2, TK_OUTLINE);

    /* Scar over the right cheek. */
    pf_line(s, hx + 10, fy + 4, hx + 13, fy + 9, TK_SKIN_SH);
    pf_px(s, hx + 11, fy + 6, TK_LIP_DARK);

    /* Broad nose. */
    pf_line(s, hx - 1, fy + 2, hx - 2, fy + 7, TK_SKIN_SH);
    pf_fill_ellipse(s, hx - 1, fy + 8, 3, 2, TK_SKIN_SH);
    pf_px(s, hx - 2, fy + 7, TK_SKIN_HI);

    /* Sprite mouth rides the dropped jaw. */
    static const uint8_t mouth_cols[5] = {
        TK_LIP_DARK, TK_LIP_MID, TK_CAVITY, TK_TEETH, TK_TONGUE,
    };
    pf_draw_mouth_bank(s, hx, fy + 12 + jaw, pf_mouth_classify(k),
                       mouth_cols);

    /* Verb bar. */
    pf_fill_rect(s, 0, 53, TK_W, 7, TK_BAR_BG);
    pf_hline(s, 0, TK_W - 1, 53, TK_OUTLINE);
    pf_text3x5(s, 3, 54, "LOOK", TK_BAR_TXT);
    pf_text3x5(s, 23, 54, "TALK", TK_BAR_TXT_HI);
    pf_text3x5(s, 43, 54, "USE", TK_BAR_TXT);
    pf_text3x5(s, 59, 54, "WALK", TK_BAR_TXT);

    pf_present(fb, s, 2, 2, tk_palette, PF_FX_NONE);
}
