#include "fmg_internal.h"

/*
 * EGA talkie flap: the classic CD-talkie era approach — a tiny ladder of
 * mouth cels swapped by amplitude, the way v5-era adventure games cycled
 * talk frames (per-line sync tracks arrived later; see RESEARCH.md). Big
 * chunky 4px blocks, a strict 16-color EGA palette, and pupils that move
 * in whole-block steps keep it period-correct.
 */

#define EGA_BLACK FMG_RGB565(0, 0, 0)
#define EGA_BLUE FMG_RGB565(0, 0, 170)
#define EGA_GREEN FMG_RGB565(0, 170, 0)
#define EGA_RED FMG_RGB565(170, 0, 0)
#define EGA_BROWN FMG_RGB565(170, 85, 0)
#define EGA_LGRAY FMG_RGB565(170, 170, 170)
#define EGA_DGRAY FMG_RGB565(85, 85, 85)
#define EGA_LRED FMG_RGB565(255, 85, 85)
#define EGA_YELLOW FMG_RGB565(255, 255, 85)
#define EGA_WHITE FMG_RGB565(255, 255, 255)

typedef const char *const fmg_rows8_t[8];

static fmg_rows8_t s_flap0 = {
    "............",
    "............",
    "............",
    ".oooooooooo.",
    ".orrrrrrrro.",
    ".oooooooooo.",
    "............",
    "............",
};

static fmg_rows8_t s_flap1 = {
    "............",
    "............",
    "..oooooooo..",
    ".oorrrrrroo.",
    ".oriiiiiiro.",
    ".oorrrrrroo.",
    "..oooooooo..",
    "............",
};

static fmg_rows8_t s_flap2 = {
    "............",
    "..oooooooo..",
    ".oorrrrrroo.",
    ".orttttttro.",
    ".oriiiiiiro.",
    ".oriiiiiiro.",
    ".oorrrrrroo.",
    "..oooooooo..",
};

static fmg_rows8_t s_flap3 = {
    ".oooooooooo.",
    ".orrrrrrrro.",
    ".orttttttro.",
    ".oriiiiiiro.",
    ".oriiiiiiro.",
    ".origgggiro.",
    ".orrrrrrrro.",
    ".oooooooooo.",
};

static fmg_rows8_t s_flap_round = {
    "............",
    "...oooooo...",
    "..oorrrroo..",
    ".oorriirroo.",
    ".oorriirroo.",
    "..oorrrroo..",
    "...oooooo...",
    "............",
};

static fmg_rows8_t s_flap_press = {
    "............",
    "............",
    "............",
    "oooooooooooo",
    "orrrrrrrrrro",
    "oooooooooooo",
    "............",
    "............",
};

static const fmg_sprite_pal_t s_pal[] = {
    {'o', EGA_BLACK},
    {'r', EGA_RED},
    {'i', EGA_DGRAY},
    {'t', EGA_WHITE},
    {'g', EGA_LRED},
};

static void fmg_blk(uint16_t *px, int32_t bx, int32_t by, uint16_t color)
{
    fmg_fill_rect(px, bx * 4, by * 4, 4, 4, color);
}

static void fmg_blk_rect(
    uint16_t *px, int32_t bx, int32_t by, int32_t bw, int32_t bh,
    uint16_t color)
{
    fmg_fill_rect(px, bx * 4, by * 4, bw * 4, bh * 4, color);
}

void fmg_render_talkie(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    fmg_fill(px, EGA_BLUE);

    /* head: yellow block face with brown hair helmet and green shirt */
    fmg_blk_rect(px, 10, 4, 20, 22, EGA_YELLOW);
    fmg_blk_rect(px, 11, 3, 18, 1, EGA_BROWN);
    fmg_blk_rect(px, 10, 4, 20, 2, EGA_BROWN);
    fmg_blk_rect(px, 10, 6, 2, 2, EGA_BROWN);
    fmg_blk_rect(px, 28, 6, 2, 2, EGA_BROWN);
    /* ears */
    fmg_blk_rect(px, 9, 12, 1, 3, EGA_YELLOW);
    fmg_blk_rect(px, 30, 12, 1, 3, EGA_YELLOW);
    /* neck + shirt */
    fmg_blk_rect(px, 17, 26, 6, 1, EGA_YELLOW);
    fmg_blk_rect(px, 12, 27, 16, 3, EGA_GREEN);

    /* pupils step in whole blocks like period sprites did */
    int32_t pdx = fmg_clampi(
        ((int32_t)kf->look_x * 3 / 128) + (idle.gaze_dx_q8 >> 10), -1, 1);
    int32_t pdy = fmg_clampi(
        ((int32_t)kf->look_y * 2 / 128) + (idle.gaze_dy_q8 >> 10), -1, 1);
    for (int side = 0; side < 2; side++) {
        int32_t ex = side == 0 ? 14 : 23;
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        int32_t brow_by = 8 + fmg_clampi(brow >> 10, -1, 1);
        fmg_blk_rect(px, ex, brow_by, 3, 1, EGA_BLACK);
        if (lid <= 64) {
            fmg_blk_rect(px, ex, 12, 3, 1, EGA_BLACK);
        } else {
            int32_t h = lid > 192 ? 3 : 2;
            fmg_blk_rect(px, ex, 11, 3, h, EGA_WHITE);
            fmg_blk(px, ex + 1 + pdx, 11 + (h > 2 ? 1 : 0) + (pdy > 0 ? 1 : 0),
                    EGA_BLACK);
        }
    }

    /* nose */
    fmg_blk(px, 19, 15, EGA_BROWN);
    fmg_blk(px, 20, 15, EGA_BROWN);

    /* amplitude-ladder mouth cel */
    static const fmg_sprite_t flaps[4] = {
        {s_flap0, 12, 8}, {s_flap1, 12, 8}, {s_flap2, 12, 8},
        {s_flap3, 12, 8},
    };
    static const fmg_sprite_t round_cel = {s_flap_round, 12, 8};
    static const fmg_sprite_t press_cel = {s_flap_press, 12, 8};
    const fmg_sprite_t *cel;
    if (mouth.press_q8 > 150) {
        cel = &press_cel;
    } else if (mouth.round_q8 > 150) {
        cel = &round_cel;
    } else {
        int32_t idx = fmg_clampi((mouth.open_q8 * 4) >> 8, 0, 3);
        cel = &flaps[idx];
    }
    fmg_sprite_blit(px, cel, 80, 86, 3, s_pal,
                    (int)(sizeof(s_pal) / sizeof(s_pal[0])));
    (void)clock;
}
