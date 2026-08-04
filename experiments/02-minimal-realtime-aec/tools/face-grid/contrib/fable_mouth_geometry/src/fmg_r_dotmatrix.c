#include "fmg_internal.h"

/*
 * Flip-dot matrix: the whole face lives on a 32x24 two-state dot board
 * (5 px pitch) like transit flip-disc signs — unlit dots still show as
 * dark discs, the mouth is a discrete viseme glyph font, and the pupil is
 * a hole punched in a lit eye disc. Amber-on-black HUB75 palette.
 */

enum {
    DOT_COLS = 32,
    DOT_ROWS = 24,
    DOT_PITCH = 5,
};

#define DOT_BG FMG_RGB565(6, 6, 8)
#define DOT_OFF FMG_RGB565(30, 32, 40)
#define DOT_ON FMG_RGB565(255, 176, 32)
#define DOT_DIM FMG_RGB565(122, 86, 26)

/* glyph font: '#' lit, '+' dim, '.' off; 12x7 dots each */
typedef const char *const fmg_glyph_t[7];

static fmg_glyph_t s_g_rest = {
    "............",
    "............",
    "............",
    ".##########.",
    "............",
    "............",
    "............",
};

static fmg_glyph_t s_g_mbp = {
    "............",
    "............",
    ".##########.",
    ".##########.",
    "............",
    "............",
    "............",
};

static fmg_glyph_t s_g_aa = {
    ".##########.",
    ".##......##.",
    ".#........#.",
    ".#........#.",
    ".#........#.",
    ".##......##.",
    ".##########.",
};

static fmg_glyph_t s_g_oh = {
    "...######...",
    "..##....##..",
    ".##......##.",
    ".##......##.",
    ".##......##.",
    "..##....##..",
    "...######...",
};

static fmg_glyph_t s_g_uu = {
    "............",
    "....####....",
    "...#....#...",
    "...#....#...",
    "....####....",
    "............",
    "............",
};

static fmg_glyph_t s_g_ee = {
    "............",
    ".#........#.",
    ".##......##.",
    "..##....##..",
    "...######...",
    "............",
    "............",
};

static fmg_glyph_t s_g_ih = {
    "............",
    ".##########.",
    ".#+#+#+#+#+.",
    ".##########.",
    "............",
    "............",
    "............",
};

static fmg_glyph_t s_g_ss = {
    "............",
    ".##########.",
    ".++++++++++.",
    ".##########.",
    "............",
    "............",
    "............",
};

static fmg_glyph_t s_g_fv = {
    "............",
    "............",
    ".##########.",
    "..########..",
    "............",
    "............",
    "............",
};

static fmg_glyph_t s_g_ln = {
    "...######...",
    "..#......#..",
    "..#..##..#..",
    "..#..##..#..",
    "..#......#..",
    "...######...",
    "............",
};

static const char *const *fmg_dot_glyph(const fmg_mouth_t *m)
{
    switch (m->vis) {
    case FMG_VIS_MBP:
        return s_g_mbp;
    case FMG_VIS_AA:
        return m->open_q8 > 140 ? s_g_aa : s_g_ih;
    case FMG_VIS_EE:
        return s_g_ee;
    case FMG_VIS_IH:
        return s_g_ih;
    case FMG_VIS_OH:
        return s_g_oh;
    case FMG_VIS_UU:
        return s_g_uu;
    case FMG_VIS_SS:
        return s_g_ss;
    case FMG_VIS_FV:
        return s_g_fv;
    case FMG_VIS_LN:
        return s_g_ln;
    case FMG_VIS_REST:
    default:
        return m->open_q8 > 100 ? s_g_ih : s_g_rest;
    }
}

static void fmg_dot_disc(
    uint16_t *px, int32_t col, int32_t row, uint8_t level)
{
    int32_t cx = col * DOT_PITCH + 2;
    int32_t cy = row * DOT_PITCH + 2;
    uint16_t color = level == 0 ? DOT_OFF : (level == 1 ? DOT_DIM : DOT_ON);
    fmg_fill_ellipse(px, cx, cy, 2, 2, color);
    if (level >= 2) {
        fmg_pixel(px, cx - 1, cy - 1, FMG_RGB565(255, 226, 140));
    }
}

static void fmg_dot_set(uint8_t *dots, int32_t col, int32_t row, uint8_t v)
{
    if (col >= 0 && col < DOT_COLS && row >= 0 && row < DOT_ROWS) {
        dots[row * DOT_COLS + col] = v;
    }
}

void fmg_render_dotmatrix(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    uint8_t dots[DOT_COLS * DOT_ROWS];
    for (int i = 0; i < DOT_COLS * DOT_ROWS; i++) {
        dots[i] = 0;
    }

    /* whole board shifts by one dot with the slow sway — mechanical char */
    int32_t shift = idle.sway_q8 > 128 ? 1 : (idle.sway_q8 < -128 ? -1 : 0);

    /* eyes: lit discs with a punched-out pupil */
    int32_t pdx = fmg_clampi(
        (int32_t)kf->look_x * 2 / 96 + (idle.gaze_dx_q8 >> 9), -2, 2);
    int32_t pdy = fmg_clampi(
        (int32_t)kf->look_y * 2 / 96 + (idle.gaze_dy_q8 >> 9), -1, 1);
    for (int side = 0; side < 2; side++) {
        int32_t ex = (side == 0 ? 9 : 22) + shift;
        int32_t ey = 8;
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t rows_open = (lid * 7 + 128) >> 8; /* 0..7ish */
        if (rows_open <= 0) {
            for (int32_t c = -3; c <= 3; c++) {
                fmg_dot_set(dots, ex + c, ey, 1);
            }
        } else {
            for (int32_t r = -3; r <= 3; r++) {
                if (r < -(rows_open / 2) - 1 || r > rows_open / 2 + 1) {
                    continue;
                }
                for (int32_t c = -3; c <= 3; c++) {
                    if (c * c + r * r <= 11) {
                        fmg_dot_set(dots, ex + c, ey + r, 2);
                    }
                }
            }
            fmg_dot_set(dots, ex + pdx, ey + pdy, 0);
            fmg_dot_set(dots, ex + pdx + 1, ey + pdy, 0);
            fmg_dot_set(dots, ex + pdx, ey + pdy + 1, 0);
            fmg_dot_set(dots, ex + pdx + 1, ey + pdy + 1, 0);
        }
        /* brow: dim bar riding the brow offset */
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        int32_t brow_row = ey - 5 + fmg_clampi(brow >> 9, -1, 1);
        for (int32_t c = -2; c <= 2; c++) {
            fmg_dot_set(dots, ex + c, brow_row, 1);
        }
    }

    /* mouth glyph */
    const char *const *glyph = fmg_dot_glyph(&mouth);
    for (int32_t r = 0; r < 7; r++) {
        for (int32_t c = 0; c < 12 && glyph[r][c] != '\0'; c++) {
            uint8_t v = glyph[r][c] == '#' ? 2
                        : (glyph[r][c] == '+' ? 1 : 0);
            if (v != 0) {
                fmg_dot_set(dots, 10 + c + shift, 14 + r, v);
            }
        }
    }

    /* breathing status dot in the corner */
    fmg_dot_set(dots, 1, DOT_ROWS - 2, idle.breath_q8 > 0 ? 2 : 1);

    fmg_fill(px, DOT_BG);
    for (int32_t r = 0; r < DOT_ROWS; r++) {
        for (int32_t c = 0; c < DOT_COLS; c++) {
            fmg_dot_disc(px, c, r, dots[r * DOT_COLS + c]);
        }
    }
}
