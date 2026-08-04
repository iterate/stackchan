#include "pf_internal.h"
#include "pf_mouthbank.h"

/*
 * Two attribute-constrained portraits. Both draw the scene freely in a
 * machine palette, then run an authentic hardware-constraint resolve pass:
 *
 *  - ZX Attribute Bard: 8×8 cells may hold only ink+paper, and both must
 *    share one brightness group (black is neutral) — the resolve pass
 *    produces genuine attribute clash where features cross cell borders.
 *    The animated cyan/red border replays the tape-loading stripes.
 *
 *  - NES Tile Minstrel: every 16×16 screen block (8×8 art px) must pick
 *    one of four fixed 3-colour subpalettes over a shared backdrop
 *    colour; the remap shows period-correct block-edge colour errors.
 */

/* ---- ZX Spectrum ------------------------------------------------------- */

enum { ZX_W = 160, ZX_H = 120, ZX_COLOURS = 15 };

/* 0 black, 1-7 normal (D8), 8-14 bright (FF): blue, red, magenta, green,
 * cyan, yellow, white. */
static const uint8_t zx_rgb[ZX_COLOURS][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xD8 }, { 0xD8, 0x00, 0x00 },
    { 0xD8, 0x00, 0xD8 }, { 0x00, 0xD8, 0x00 }, { 0x00, 0xD8, 0xD8 },
    { 0xD8, 0xD8, 0x00 }, { 0xD8, 0xD8, 0xD8 }, { 0x00, 0x00, 0xFF },
    { 0xFF, 0x00, 0x00 }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0x00 },
    { 0x00, 0xFF, 0xFF }, { 0xFF, 0xFF, 0x00 }, { 0xFF, 0xFF, 0xFF },
};

enum {
    ZX_BLACK = 0,
    ZX_BLUE = 1,
    ZX_RED = 2,
    ZX_MAGENTA = 3,
    ZX_GREEN = 4,
    ZX_CYAN = 5,
    ZX_YELLOW = 6,
    ZX_WHITE = 7,
    ZX_BBLUE = 8,
    ZX_BRED = 9,
    ZX_BMAGENTA = 10,
    ZX_BGREEN = 11,
    ZX_BCYAN = 12,
    ZX_BYELLOW = 13,
    ZX_BWHITE = 14,
};

/* Small tables are rebuilt on the stack every frame so renderers stay
 * pure and stateless; the cost is a few hundred integer ops. */
typedef struct {
    uint16_t palette[ZX_COLOURS];
    int dist[ZX_COLOURS][ZX_COLOURS];
} zx_tables_t;

static void zx_tables_build(zx_tables_t *t) {
    for (int i = 0; i < ZX_COLOURS; ++i) {
        t->palette[i] =
            PF_RGB565(zx_rgb[i][0], zx_rgb[i][1], zx_rgb[i][2]);
        for (int j = 0; j < ZX_COLOURS; ++j) {
            int dr = (int)zx_rgb[i][0] - (int)zx_rgb[j][0];
            int dg = (int)zx_rgb[i][1] - (int)zx_rgb[j][1];
            int db = (int)zx_rgb[i][2] - (int)zx_rgb[j][2];
            t->dist[i][j] = dr * dr + dg * dg + db * db;
        }
    }
}

static int zx_bright_group(int c) {
    if (c == ZX_BLACK) {
        return -1; /* neutral: legal with either group */
    }
    return c >= ZX_BBLUE ? 1 : 0;
}

/* Force each 8×8 cell to two brightness-compatible colours. */
static void zx_resolve(pf_surface_t *s, const zx_tables_t *t, int x0,
                       int y0, int x1, int y1) {
    for (int cy = y0; cy < y1; cy += 8) {
        for (int cx = x0; cx < x1; cx += 8) {
            int count[ZX_COLOURS] = { 0 };
            for (int dy = 0; dy < 8; ++dy) {
                const uint8_t *row =
                    s->px + (size_t)(cy + dy) * (size_t)s->w + (size_t)cx;
                for (int dx = 0; dx < 8; ++dx) {
                    ++count[row[dx]];
                }
            }
            int c1 = 0, best = -1;
            for (int c = 0; c < ZX_COLOURS; ++c) {
                if (count[c] > best) {
                    best = count[c];
                    c1 = c;
                }
            }
            int g1 = zx_bright_group(c1);
            int c2 = c1, best2 = -1;
            for (int c = 0; c < ZX_COLOURS; ++c) {
                if (c == c1 || count[c] == 0) {
                    continue;
                }
                int g = zx_bright_group(c);
                if (g1 >= 0 && g >= 0 && g != g1) {
                    continue; /* mixed brightness is illegal in a cell */
                }
                if (count[c] > best2) {
                    best2 = count[c];
                    c2 = c;
                }
            }
            for (int dy = 0; dy < 8; ++dy) {
                uint8_t *row =
                    s->px + (size_t)(cy + dy) * (size_t)s->w + (size_t)cx;
                for (int dx = 0; dx < 8; ++dx) {
                    int c = row[dx];
                    if (c != c1 && c != c2) {
                        row[dx] = (uint8_t)(t->dist[c][c1] <=
                                                    t->dist[c][c2]
                                                ? c1
                                                : c2);
                    }
                }
            }
        }
    }
}

void pf_render_zx_attribute(uint16_t *fb, const face_keyframe_t *k,
                            const pf_rig_t *rig, uint32_t clock) {
    zx_tables_t tables;
    zx_tables_build(&tables);
    pf_surface_t art = pf_surface_attach(fb, ZX_W, ZX_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    /* Paper field. */
    pf_clear(s, ZX_BBLUE);
    /* Sunburst from the top-left corner. */
    for (int i = 0; i < 5; ++i) {
        pf_line(s, 8, 8, 40 + i * 26, 8 + i * 5, ZX_BYELLOW);
        pf_line(s, 8, 8, 20 + i * 12, 40 + i * 14, ZX_BYELLOW);
    }

    int hy = rig->breath / 100 + rig->bob / 90;
    int cx = 80;
    int fy = 56 + hy;

    /* Jacket: bright cyan with black lapels; white shirt triangle. */
    pf_fill_rect(s, 32, 92, 96, 20, ZX_BCYAN);
    pf_fill_ellipse(s, cx, 116, 52, 22, ZX_BCYAN);
    pf_line(s, cx - 26, 94, cx - 10, 110, ZX_BLACK);
    pf_line(s, cx + 26, 94, cx + 10, 110, ZX_BLACK);
    pf_fill_rect(s, cx - 8, 98, 16, 14, ZX_WHITE);
    pf_px(s, cx, 104, ZX_BLACK); /* button */
    pf_px(s, cx, 109, ZX_BLACK);
    /* Studs. */
    for (int i = 0; i < 4; ++i) {
        pf_px(s, cx - 34 + i * 3, 96 + i, ZX_BWHITE);
        pf_px(s, cx + 34 - i * 3, 96 + i, ZX_BWHITE);
    }

    /* Neck. */
    pf_fill_rect(s, cx - 7, fy + 28, 14, 12, ZX_BYELLOW);

    /* Wild bright-magenta hair: mass + spikes crossing cell borders. */
    pf_fill_ellipse(s, cx, fy - 16, 30, 20, ZX_BMAGENTA);
    for (int i = 0; i < 9; ++i) {
        uint32_t h = pf_hash32(0x2166U ^ (uint32_t)i);
        int bx = cx - 28 + i * 7;
        int tipx = bx + (int)(h % 11U) - 5;
        int tipy = fy - 38 - (int)((h >> 8) % 10U);
        pf_line(s, bx, fy - 22, tipx, tipy, ZX_BMAGENTA);
        pf_line(s, bx + 1, fy - 22, tipx + 1, tipy + 1, ZX_BMAGENTA);
        pf_line(s, bx + 2, fy - 20, tipx + 2, tipy + 2, ZX_MAGENTA);
    }
    pf_fill_ellipse(s, cx - 24, fy + 2, 6, 14, ZX_BMAGENTA); /* sideburn */
    pf_fill_ellipse(s, cx + 24, fy + 2, 6, 14, ZX_MAGENTA);

    /* Face. */
    pf_fill_ellipse(s, cx, fy, 21, 24, ZX_BYELLOW);
    pf_fill_ellipse(s, cx + 8, fy + 6, 10, 14, ZX_YELLOW); /* cheek shade */
    pf_fill_ellipse(s, cx - 4, fy - 2, 15, 18, ZX_BYELLOW);
    /* Fringe overlaps the forehead. */
    pf_fill_ellipse(s, cx - 6, fy - 16, 18, 8, ZX_BMAGENTA);
    pf_fill_ellipse(s, cx + 14, fy - 14, 8, 5, ZX_MAGENTA);

    /* Eyes. */
    int gx = rig->gaze_x / 48;
    int gy = rig->gaze_y / 74;
    int ey = fy - 2;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 9 : cx - 9;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        pf_fill_rect(s, ex - 4, ey - 3, 9, 7, ZX_BLACK);
        pf_fill_rect(s, ex - 3, ey - 2, 7, 5, ZX_BWHITE);
        pf_fill_rect(s, ex - 1 + gx, ey - 1 + gy, 2, 3, ZX_BLACK);
        int lid = ((255 - open) * 7) / 255;
        if (lid > 0) {
            pf_fill_rect(s, ex - 4, ey - 3, 9, lid, ZX_BYELLOW);
            pf_hline(s, ex - 4, ex + 4, ey - 4 + lid, ZX_BLACK);
        }
    }
    /* Brows: black slashes. */
    int bl = -rig->brow / 32;
    pf_line(s, cx - 13, ey - 6 + bl, cx - 5, ey - 8 + bl, ZX_BLACK);
    pf_line(s, cx + 5, ey - 8 + bl, cx + 13, ey - 6 + bl, ZX_BLACK);

    /* Nose. */
    pf_vline(s, cx, fy + 3, fy + 8, ZX_YELLOW);
    pf_px(s, cx + 1, fy + 8, ZX_BLACK);

    /* Mouth from the shared bank. */
    static const uint8_t mouth_cols[5] = {
        ZX_BLACK, ZX_BRED, ZX_BLACK, ZX_BWHITE, ZX_RED,
    };
    pf_draw_mouth_bank(s, cx, fy + 15, pf_mouth_classify(k), mouth_cols);

    /* Name tag. */
    pf_fill_rect(s, 116, 16, 34, 12, ZX_BYELLOW);
    pf_text3x5(s, 119, 19, "ZIGGY", ZX_BLACK);

    /* Attribute resolve over the paper area (the border is exempt, as on
     * real hardware). */
    zx_resolve(s, &tables, 8, 8, 152, 112);

    /* Tape-loading border stripes, animated. */
    int phase = (int)((ms / 24U) % 8U);
    for (int y = 0; y < ZX_H; ++y) {
        uint8_t c =
            (((y + phase) / 4) & 1) ? ZX_BCYAN : ZX_BRED;
        if (y < 8 || y >= 112) {
            pf_hline(s, 0, ZX_W - 1, y, c);
        } else {
            pf_hline(s, 0, 7, y, c);
            pf_hline(s, 152, ZX_W - 1, y, c);
        }
    }

    pf_present(fb, s, 1, 1, tables.palette, PF_FX_NONE);
}

/* ---- NES --------------------------------------------------------------- */

enum { NES_W = 80, NES_H = 60, NES_COLOURS = 13 };

enum {
    NES_BG = 0, /* shared backdrop */
    NES_BLACK,
    NES_WHITE,
    NES_SKIN,
    NES_SKIN_SH,
    NES_RED,
    NES_DKRED,
    NES_GREEN,
    NES_DKGREEN,
    NES_GOLD,
    NES_BROWN,
    NES_LTGRAY,
    NES_DKGRAY,
};

static const uint8_t nes_rgb[NES_COLOURS][3] = {
    { 0x0C, 0x0C, 0x2C }, { 0x00, 0x00, 0x00 }, { 0xFC, 0xFC, 0xFC },
    { 0xF8, 0xB8, 0x88 }, { 0xC8, 0x78, 0x48 }, { 0xC8, 0x28, 0x20 },
    { 0x70, 0x14, 0x10 }, { 0x38, 0xA0, 0x28 }, { 0x14, 0x58, 0x18 },
    { 0xF8, 0xC8, 0x38 }, { 0x88, 0x50, 0x20 }, { 0xBC, 0xBC, 0xBC },
    { 0x5C, 0x5C, 0x5C },
};

/* Four fixed 3-colour subpalettes over the shared backdrop. */
static const uint8_t nes_subpal[4][3] = {
    { NES_SKIN, NES_SKIN_SH, NES_BLACK }, /* face */
    { NES_RED, NES_DKRED, NES_GOLD },     /* cap */
    { NES_GREEN, NES_DKGREEN, NES_BROWN },/* tunic */
    { NES_WHITE, NES_LTGRAY, NES_DKGRAY },/* ui/eyes */
};

typedef struct {
    uint16_t palette[NES_COLOURS];
    uint8_t nearest[4][NES_COLOURS];
    int cost[4][NES_COLOURS];
} nes_tables_t;

static void nes_tables_build(nes_tables_t *t) {
    for (int i = 0; i < NES_COLOURS; ++i) {
        t->palette[i] =
            PF_RGB565(nes_rgb[i][0], nes_rgb[i][1], nes_rgb[i][2]);
    }
    for (int p = 0; p < 4; ++p) {
        for (int c = 0; c < NES_COLOURS; ++c) {
            int bestd = 0x7FFFFFFF;
            uint8_t bestc = nes_subpal[p][0];
            for (int i = 0; i < 3; ++i) {
                int cc = nes_subpal[p][i];
                int dr = (int)nes_rgb[c][0] - (int)nes_rgb[cc][0];
                int dg = (int)nes_rgb[c][1] - (int)nes_rgb[cc][1];
                int db = (int)nes_rgb[c][2] - (int)nes_rgb[cc][2];
                int d = dr * dr + dg * dg + db * db;
                if (d < bestd) {
                    bestd = d;
                    bestc = (uint8_t)cc;
                }
            }
            t->nearest[p][c] = bestc;
            t->cost[p][c] = bestd;
        }
    }
}

/* Attribute pass: each 8×8 art block picks its cheapest subpalette. */
static void nes_resolve(pf_surface_t *s, const nes_tables_t *t) {
    for (int by = 0; by < s->h; by += 8) {
        for (int bx = 0; bx < s->w; bx += 8) {
            int h = pf_mini(8, s->h - by);
            int w = pf_mini(8, s->w - bx);
            int best_p = 0;
            long best_cost = 0x7FFFFFFFL;
            for (int p = 0; p < 4; ++p) {
                long cost = 0;
                for (int dy = 0; dy < h; ++dy) {
                    const uint8_t *row = s->px +
                                         (size_t)(by + dy) * (size_t)s->w +
                                         (size_t)bx;
                    for (int dx = 0; dx < w; ++dx) {
                        if (row[dx] != NES_BG) {
                            cost += t->cost[p][row[dx]];
                        }
                    }
                }
                if (cost < best_cost) {
                    best_cost = cost;
                    best_p = p;
                }
            }
            for (int dy = 0; dy < h; ++dy) {
                uint8_t *row = s->px + (size_t)(by + dy) * (size_t)s->w +
                               (size_t)bx;
                for (int dx = 0; dx < w; ++dx) {
                    if (row[dx] != NES_BG) {
                        row[dx] = t->nearest[best_p][row[dx]];
                    }
                }
            }
        }
    }
}

void pf_render_nes_tile(uint16_t *fb, const face_keyframe_t *k,
                        const pf_rig_t *rig, uint32_t clock) {
    nes_tables_t tables;
    nes_tables_build(&tables);
    pf_surface_t art = pf_surface_attach(fb, NES_W, NES_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    pf_clear(s, NES_BG);
    /* Stage floor. */
    pf_fill_rect_dither(s, 0, 52, NES_W, 8, NES_DKGRAY, NES_BG,
                        PF_PAT_CHECKER, 32);
    /* Twinkling stage lights. */
    for (int i = 0; i < 8; ++i) {
        uint32_t h = pf_hash32(0x57ABU ^ (uint32_t)i);
        uint8_t c = (((ms / 400U) + i) & 3U) ? NES_DKGRAY : NES_WHITE;
        pf_px(s, 4 + (int)(h % 72U), 2 + (int)((h >> 8) % 6U), c);
    }

    int hy = rig->breath / 110 + rig->bob / 100;
    int cx = 40;
    int fy = 26 + hy;

    /* Tunic. */
    pf_fill_rect(s, 22, 46, 36, 8, NES_GREEN);
    pf_fill_rect(s, 22, 46, 36, 2, NES_DKGREEN);
    pf_line(s, 26, 53, 36, 46, NES_BROWN); /* lute strap */
    pf_line(s, 27, 53, 37, 46, NES_BROWN);
    /* Neck + collar. */
    pf_fill_rect(s, cx - 4, fy + 15, 8, 6, NES_SKIN);
    pf_hline(s, cx - 5, cx + 4, 46, NES_GOLD);

    /* Head with black outline. */
    pf_fill_ellipse(s, cx, fy, 12, 13, NES_BLACK);
    pf_fill_ellipse(s, cx, fy, 11, 12, NES_SKIN);
    pf_fill_ellipse(s, cx + 5, fy + 3, 5, 8, NES_SKIN_SH);
    pf_fill_ellipse(s, cx - 3, fy - 1, 7, 9, NES_SKIN);

    /* Ears. */
    pf_fill_rect(s, cx - 13, fy + 1, 2, 4, NES_SKIN);
    pf_fill_rect(s, cx + 11, fy + 1, 2, 4, NES_SKIN_SH);

    /* Feathered cap: red with gold trim and a bobbing feather. */
    int sway = rig->breath / 90;
    pf_fill_ellipse(s, cx - 1, fy - 10, 13, 5, NES_RED);
    pf_fill_ellipse(s, cx - 3, fy - 12, 10, 4, NES_RED);
    pf_hline(s, cx - 12, cx + 11, fy - 7, NES_DKRED);
    pf_hline(s, cx - 12, cx + 11, fy - 6, NES_GOLD);
    pf_line(s, cx + 8, fy - 12, cx + 13 + sway, fy - 18, NES_GOLD);
    pf_line(s, cx + 9, fy - 11, cx + 14 + sway, fy - 15, NES_GOLD);
    pf_px(s, cx + 13 + sway, fy - 19, NES_WHITE);

    /* Eyes. */
    int gx = rig->gaze_x / 52;
    int ey = fy - 1;
    for (int side = 0; side < 2; ++side) {
        int ex = side ? cx + 5 : cx - 5;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        if (open > 90) {
            pf_fill_rect(s, ex - 1 + gx, ey - 1, 2, 3, NES_BLACK);
            pf_px(s, ex - 1 + gx, ey - 1, NES_WHITE);
        } else {
            pf_hline(s, ex - 2, ex + 1, ey + 1, NES_BLACK);
        }
    }
    /* Brows. */
    int bl = -rig->brow / 40;
    pf_hline(s, cx - 7, cx - 3, ey - 3 + bl, NES_BLACK);
    pf_hline(s, cx + 3, cx + 7, ey - 3 + bl, NES_BLACK);
    /* Nose. */
    pf_px(s, cx, fy + 3, NES_SKIN_SH);
    pf_px(s, cx + 1, fy + 3, NES_BLACK);

    /* Mouth from the shared bank (drawn small: clip to jaw). */
    static const uint8_t mouth_cols[5] = {
        NES_BLACK, NES_RED, NES_BLACK, NES_WHITE, NES_DKRED,
    };
    pf_draw_mouth_bank(s, cx, fy + 8, pf_mouth_classify(k), mouth_cols);

    /* Name banner. */
    pf_fill_rect(s, 20, 55, 40, 5, NES_BG);
    pf_text3x5(s, 22, 55, "SIR LUTE 1UP", NES_WHITE);

    nes_resolve(s, &tables);
    pf_present(fb, s, 2, 2, tables.palette, PF_FX_NONE);
}
