#include "wc_common.h"

/*
 * Teletext sextant: the face as a broadcast teletext page.
 *
 * The screen is a real 40-column teletext layout: a header row with page
 * number, service name and a live clock ticking off the sample counter, then
 * 19 rows of 2x3 block-mosaic cells. The face is first painted into an
 * 80x57 sixel image with the eight-color teletext palette, then each cell is
 * forced through the authentic G1 constraint — one foreground color per
 * cell, black background, six on/off blocks — so minority colors inside a
 * cell vanish exactly like real attribute clash. Head motion lands quantized
 * to whole sixels, which gives the page its chunky broadcast shuffle; a
 * flash-attribute beacon blinks at ~0.75 Hz while speaking, and the bottom
 * row carries test-card color bars.
 */

enum {
    WC_TT_COLS = 40,
    WC_TT_CELL_W = 4,
    WC_TT_CELL_H = 6,
    WC_TT_ROWS = 20,
};

static const uint16_t WC_TT_PAL[8] = {
    /* black, red, green, yellow, blue, magenta, cyan, white */
    0x0000u, 0xF800u, 0x07E0u, 0xFFE0u, 0x001Fu, 0xF81Fu, 0x07FFu, 0xFFFFu,
};

/* 3x5 glyphs, row-major, MSB = left pixel */
static uint16_t wc_tt_font(char c) {
    switch (c) {
        case '0': return (uint16_t)0x7B6Fu; /* 111 101 101 101 111 */
        case '1': return (uint16_t)0x2C97u; /* 010 110 010 010 111 */
        case '2': return (uint16_t)0x73E7u; /* 111 001 111 100 111 */
        case '3': return (uint16_t)0x72CFu; /* 111 001 011 001 111 */
        case '4': return (uint16_t)0x5BC9u; /* 101 101 111 001 001 */
        case '5': return (uint16_t)0x79CFu; /* 111 100 111 001 111 */
        case '6': return (uint16_t)0x79EFu; /* 111 100 111 101 111 */
        case '7': return (uint16_t)0x7292u; /* 111 001 010 010 010 */
        case '8': return (uint16_t)0x7BEFu; /* 111 101 111 101 111 */
        case '9': return (uint16_t)0x7BCFu; /* 111 101 111 001 111 */
        case 'P': return (uint16_t)0x7BE4u; /* 111 101 111 100 100 */
        case 'F': return (uint16_t)0x79A4u; /* 111 100 110 100 100 */
        case 'A': return (uint16_t)0x2BEDu; /* 010 101 111 101 101 */
        case 'B': return (uint16_t)0x6BAEu; /* 110 101 110 101 110 */
        case 'L': return (uint16_t)0x4927u; /* 100 100 100 100 111 */
        case 'E': return (uint16_t)0x79A7u; /* 111 100 110 100 111 */
        case 'T': return (uint16_t)0x7492u; /* 111 010 010 010 010 */
        case 'X': return (uint16_t)0x5AADu; /* 101 101 010 101 101 */
        case ':': return (uint16_t)0x0410u; /* 000 010 000 010 000 */
        case '#': return (uint16_t)0x7FFFu; /* full block */
        default: return 0;
    }
}

/* palette index of one sixel of the face image */
static uint32_t wc_tt_face_sixel(const wc_rig_t *rig, int32_t sx, int32_t sy) {
    /* face pixel coordinates, head motion quantized to sixels */
    int32_t px = sx * 2 + 1;
    int32_t py = 6 + sy * 2 + 1;
    int32_t hx = (rig->head_x_q8 >> 9) * 2;
    int32_t hy = (rig->head_y_q8 >> 9) * 2;
    int32_t dx = px - WC_CENTER_X - hx;
    int32_t dy = py - 64 - hy;

    /* bottom row: test-card color bars */
    if (sy >= 54) {
        return (uint32_t)(sx / 10);
    }

    /* face oval */
    static const int32_t inv_fa2 = (1 << 18) / (46 * 46);
    static const int32_t inv_fb2 = (1 << 18) / (44 * 44);
    int32_t fq = 4096;
    if (wc_abs32(dx) < 64 && wc_abs32(dy) < 62) {
        fq = wc_ellipse_q10(dx, dy, inv_fa2, inv_fb2);
    }
    if (fq >= 1024) {
        return 4; /* studio blue */
    }

    /* hair: red bob above the fringe zigzag */
    int32_t fringe = -28 + (wc_abs32(((px * 3) & 15) - 8) >> 2);
    if (dy < fringe) {
        return 1;
    }

    /* eyes */
    for (int e = 0; e < 2; ++e) {
        int32_t ex = e == 0 ? -24 : 24;
        int32_t lid = e == 0 ? rig->lid_l : rig->lid_r;
        int32_t edx = dx - ex;
        int32_t edy = dy + 14;
        int32_t b = wc_max32(1, 7 * lid >> 8);
        if (wc_abs32(edx) <= 10 && wc_abs32(edy) <= b) {
            int32_t q = wc_ellipse_q10(edx, edy, (1 << 18) / (10 * 10),
                                       (1 << 18) / (b * b));
            if (q < 1024) {
                int32_t pdx = edx - (rig->gaze_x * 4 >> 8);
                int32_t pdy = edy - (rig->gaze_y * 3 >> 8);
                if (b > 3 && pdx * pdx + pdy * pdy < 3 * 3) {
                    return 4; /* pupil */
                }
                return 7; /* sclera */
            }
        }
        /* brow bar */
        int32_t tilt = (e == 0 ? -rig->brow_q8 : rig->brow_q8);
        int32_t by = -23 - (rig->brow_q8 * 5 >> 8) + (edx * tilt >> 10);
        if (wc_abs32(edx) <= 8 && dy >= by - 1 && dy <= by + 1) {
            return 1;
        }
    }

    /* mouth */
    {
        int32_t mw = 12 + ((int32_t)rig->mouth_width * 12 >> 8);
        mw -= (int32_t)rig->mouth_round * (mw / 3) >> 8;
        int32_t mh = 2 + ((int32_t)rig->mouth_open * 14 >> 8);
        mh = mh * (256 - (int32_t)rig->mouth_press) >> 8;
        if (mh < 2) {
            mh = 2;
        }
        int32_t mdy = dy - 24;
        if (wc_abs32(dx) <= mw && wc_abs32(mdy) <= mh) {
            int32_t q = wc_ellipse_q10(dx, mdy, wc_inv_sq_q18(mw),
                                       wc_inv_sq_q18(mh));
            if (q < 1024) {
                if (rig->mouth_teeth > 100 && mh > 6 && mdy < -(mh >> 2)) {
                    return 7; /* teeth */
                }
                return 1; /* mouth red */
            }
        }
    }

    /* nose */
    if (wc_abs32(dx) <= 1 && dy >= 2 && dy <= 6) {
        return 6; /* cyan nose accent */
    }

    return 3; /* face yellow */
}

static void wc_tt_draw_cell(uint16_t *fb, int32_t col, int32_t row,
                            const uint32_t *six, uint32_t flash_on) {
    /* pick fg: the most frequent non-black color (ties -> lowest index) */
    uint32_t count[8] = {0};
    for (int i = 0; i < 6; ++i) {
        count[six[i] & 7u]++;
    }
    uint32_t fg = 7, best = 0;
    for (uint32_t c = 1; c < 8; ++c) {
        if (count[c] > best) {
            best = count[c];
            fg = c;
        }
    }
    uint16_t fgc = WC_TT_PAL[fg];
    uint16_t *cell = fb + (size_t)row * WC_TT_CELL_H * WC_FACE_WIDTH +
                     (size_t)col * WC_TT_CELL_W;
    (void)flash_on;
    for (int32_t yy = 0; yy < WC_TT_CELL_H; ++yy) {
        uint32_t sub_y = (uint32_t)yy >> 1; /* 0..2 */
        uint16_t *line = cell + (size_t)yy * WC_FACE_WIDTH;
        for (int32_t xx = 0; xx < WC_TT_CELL_W; ++xx) {
            uint32_t sub_x = (uint32_t)xx >> 1; /* 0..1 */
            uint32_t on = best > 0 && (six[sub_y * 2 + sub_x] & 7u) == fg;
            uint16_t c = on ? fgc : 0x0000u;
            /* CRT scanline: darken every other line */
            if ((yy & 1) && c) {
                c = wc_mix565(c, 0x0000u, 64);
            }
            line[xx] = c;
        }
    }
}

static void wc_tt_draw_char(uint16_t *fb, int32_t col, char ch, uint32_t pal_idx) {
    uint16_t bits = wc_tt_font(ch);
    uint16_t color = WC_TT_PAL[pal_idx & 7u];
    uint16_t *cell = fb + (size_t)col * WC_TT_CELL_W;
    for (int32_t yy = 0; yy < 5; ++yy) {
        uint16_t *line = cell + (size_t)yy * WC_FACE_WIDTH;
        for (int32_t xx = 0; xx < 3; ++xx) {
            uint32_t on = (bits >> (14 - (yy * 3 + xx))) & 1u;
            line[xx] = on ? color : 0x0000u;
        }
        line[3] = 0x0000u;
    }
    for (int32_t xx = 0; xx < 4; ++xx) {
        cell[(size_t)5 * WC_FACE_WIDTH + xx] = 0x0000u;
    }
}

void wc_render_teletext_sextant(
    const wc_keyframe_t *kf, const wc_rig_t *rig, uint32_t clock, uint16_t *fb) {
    (void)kf;
    /* header row */
    char hdr[WC_TT_COLS + 1];
    for (int i = 0; i < WC_TT_COLS; ++i) {
        hdr[i] = ' ';
    }
    static const char *page = "P142";
    static const char *name = "FABLETEXT";
    for (int i = 0; i < 4; ++i) {
        hdr[1 + i] = page[i];
    }
    for (int i = 0; name[i]; ++i) {
        hdr[7 + i] = name[i];
    }
    uint32_t secs = clock / 16000u;
    uint32_t mm = (secs / 60u) % 100u;
    uint32_t ss = secs % 60u;
    hdr[32] = (char)('0' + mm / 10u);
    hdr[33] = (char)('0' + mm % 10u);
    hdr[34] = ':';
    hdr[35] = (char)('0' + ss / 10u);
    hdr[36] = (char)('0' + ss % 10u);
    uint32_t flash_on = (clock / 21333u) & 1u;
    if (rig->speaking && flash_on) {
        hdr[38] = '#';
    }
    for (int32_t col = 0; col < WC_TT_COLS; ++col) {
        uint32_t pal = 7;
        if (col >= 1 && col <= 4) {
            pal = 3; /* page number yellow */
        } else if (col >= 7 && col < 16) {
            pal = 2; /* service name green */
        } else if (col == 38) {
            pal = 1; /* speaking beacon red */
        }
        wc_tt_draw_char(fb, col, hdr[col], pal);
    }

    /* mosaic body */
    for (int32_t row = 1; row < WC_TT_ROWS; ++row) {
        for (int32_t col = 0; col < WC_TT_COLS; ++col) {
            uint32_t six[6];
            for (int32_t sy = 0; sy < 3; ++sy) {
                for (int32_t sx = 0; sx < 2; ++sx) {
                    int32_t gx = col * 2 + sx;
                    int32_t gy = (row - 1) * 3 + sy;
                    six[sy * 2 + sx] = wc_tt_face_sixel(rig, gx, gy);
                }
            }
            wc_tt_draw_cell(fb, col, row, six, flash_on);
        }
    }
}
