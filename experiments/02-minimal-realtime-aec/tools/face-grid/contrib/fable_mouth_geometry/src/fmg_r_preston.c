#include "fmg_internal.h"

/*
 * Preston Blair sprite mouth. Eleven hand-authored 26x14 mouth cels follow
 * the classic ten-shape phoneme chart (Blair, "Advanced Animation", 1947)
 * with the same shape semantics Rhubarb Lip Sync documents for its A-F +
 * G/H/X set; all artwork here is original. Cels are picked by the shared
 * viseme classifier and blitted at 2x over a warm cartoon face.
 *
 * Sprite palette: o outline, l lip, i mouth interior, t teeth, g tongue.
 */

typedef const char *const fmg_rows_t[14];

static fmg_rows_t s_cel_rest = {
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    ".....oooooooooooooooo.....",
    "...oolllllllllllllllloo...",
    "..ollllllllllllllllllllo..",
    "...ollllllllllllllllllo...",
    "....oolllllllllllllloo....",
    "......oooooooooooooo......",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_mbp = {
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "....oooooooooooooooooo....",
    "..oolllllllllllllllllloo..",
    "..ollllloooooooooolllllo..",
    "..oolllllllllllllllllloo..",
    "....oooooooooooooooooo....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_ss = {
    "..........................",
    "..........................",
    "..........................",
    "....oooooooooooooooooo....",
    "..oolllllllllllllllllloo..",
    "..olttttottttottttottllo..",
    "..olttttottttottttottllo..",
    "..oolllllllllllllllllloo..",
    "....oooooooooooooooooo....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_fv = {
    "..........................",
    "..........................",
    "..........................",
    "....oooooooooooooooooo....",
    "..oolllllllllllllllllloo..",
    "..olttttttttttttttttttlo..",
    "..oltttttttttttttttttllo..",
    "...ollllllllllllllllllo...",
    "....oooooooooooooooooo....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_aa = {
    "......oooooooooooooo......",
    "....oolllllllllllllloo....",
    "...olttttttttttttttttlo...",
    "..olttttttttttttttttttlo..",
    "..oliiiiiiiiiiiiiiiiiilo..",
    "..oliiiiiiiiiiiiiiiiiilo..",
    "..oliiiiiiiiiiiiiiiiiilo..",
    "..oliiiiggggggggggiiiilo..",
    "...oliiiggggggggggiiilo...",
    "....ollgggggggggggglloo...",
    ".....oolllllllllllloo.....",
    ".......oooooooooooo.......",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_ah = {
    "..........................",
    "..........................",
    ".....oooooooooooooooo.....",
    "...oolllllllllllllllloo...",
    "..olltttttttttttttttllo...",
    "..oliiiiiiiiiiiiiiiiilo...",
    "..oliiiiiiiiiiiiiiiiilo...",
    "..olliiiiiiiiiiiiiiillo...",
    "...oolllllllllllllllloo...",
    ".....oooooooooooooooo.....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_oh = {
    "........oooooooooo........",
    "......oolllllllllloo......",
    ".....ollllllllllllllo.....",
    "....olliiiiiiiiiiiillo....",
    "....oliiiiiiiiiiiiiilo....",
    "....oliiiiiiiiiiiiiilo....",
    "....oliiiiiiiiiiiiiilo....",
    "....olliiiiiiiiiiiillo....",
    ".....ollllllllllllllo.....",
    "......oolllllllllloo......",
    "........oooooooooo........",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_uu = {
    "..........................",
    "..........................",
    "..........................",
    ".........oooooooo.........",
    "........ollllllllo........",
    ".......olliiiiiillo.......",
    ".......oliiiiiiiilo.......",
    ".......olliiiiiillo.......",
    "........ollllllllo........",
    ".........oooooooo.........",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_ee = {
    "..........................",
    "..........................",
    "..........................",
    "..oo..................oo..",
    "..olo................olo..",
    "..ooloo............ooloo..",
    "...oolloooooooooooolloo...",
    "....oolttttttttttttloo....",
    ".....olttttttttttttlo.....",
    ".....oolllllllllllloo.....",
    ".......oooooooooooo.......",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_ih = {
    "..........................",
    "..........................",
    "..........................",
    "....oooooooooooooooooo....",
    "..oolllllllllllllllllloo..",
    "..olttttttttttttttttttlo..",
    "..oliiiiiiiiiiiiiiiiiilo..",
    "..olttttttttttttttttttlo..",
    "..oolllllllllllllllllloo..",
    "....oooooooooooooooooo....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static fmg_rows_t s_cel_ln = {
    "..........................",
    "..........................",
    ".....oooooooooooooooo.....",
    "...oolllllllllllllllloo...",
    "..olltttttggggggtttttllo..",
    "..oliiiiggggggggggiiiilo..",
    "..oliiiiggggggggggiiiilo..",
    "..olliiiiiiiiiiiiiiiillo..",
    "...oollllllllllllllloo....",
    ".....oooooooooooooooo.....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
};

static const fmg_sprite_pal_t s_pal[] = {
    {'o', FMG_RGB565(74, 38, 32)},
    {'l', FMG_RGB565(214, 84, 78)},
    {'i', FMG_RGB565(88, 22, 26)},
    {'t', FMG_RGB565(250, 246, 234)},
    {'g', FMG_RGB565(238, 128, 128)},
};

static const fmg_sprite_t *fmg_preston_pick(const fmg_mouth_t *m)
{
    static const fmg_sprite_t rest = {s_cel_rest, 26, 14};
    static const fmg_sprite_t mbp = {s_cel_mbp, 26, 14};
    static const fmg_sprite_t ss = {s_cel_ss, 26, 14};
    static const fmg_sprite_t fv = {s_cel_fv, 26, 14};
    static const fmg_sprite_t aa = {s_cel_aa, 26, 14};
    static const fmg_sprite_t ah = {s_cel_ah, 26, 14};
    static const fmg_sprite_t oh = {s_cel_oh, 26, 14};
    static const fmg_sprite_t uu = {s_cel_uu, 26, 14};
    static const fmg_sprite_t ee = {s_cel_ee, 26, 14};
    static const fmg_sprite_t ih = {s_cel_ih, 26, 14};
    static const fmg_sprite_t ln = {s_cel_ln, 26, 14};
    switch (m->vis) {
    case FMG_VIS_MBP:
        return &mbp;
    case FMG_VIS_SS:
        return &ss;
    case FMG_VIS_FV:
        return &fv;
    case FMG_VIS_AA:
        /* Rhubarb's C shape doubles as the A->D inbetween */
        return m->open_q8 > 150 ? &aa : &ah;
    case FMG_VIS_EE:
        return &ee;
    case FMG_VIS_IH:
        return &ih;
    case FMG_VIS_OH:
        return m->open_q8 > 60 ? &oh : &uu;
    case FMG_VIS_UU:
        return &uu;
    case FMG_VIS_LN:
        return &ln;
    case FMG_VIS_REST:
    default:
        return m->open_q8 > 90 ? &ah : &rest;
    }
}

static void fmg_preston_eye(
    uint16_t *px, int32_t cx, int32_t cy, int32_t lid_q8, int32_t look_dx,
    int32_t look_dy)
{
    int32_t ry = (16 * fmg_clampi(lid_q8, 0, 280)) >> 8;
    if (ry <= 2) {
        /* closed: lash line */
        fmg_fill_rect(px, cx - 13, cy - 1, 27, 3, FMG_RGB565(74, 38, 32));
        return;
    }
    fmg_fill_ellipse(px, cx, cy, 15, ry + 1, FMG_RGB565(74, 38, 32));
    fmg_fill_ellipse(px, cx, cy, 14, ry, FMG_RGB565(252, 250, 244));
    int32_t pr = 5;
    int32_t pdx = fmg_clampi(look_dx, -(14 - pr - 1), 14 - pr - 1);
    int32_t pdy = fmg_clampi(look_dy, -(ry - 2), ry - 2);
    fmg_fill_ellipse(px, cx + pdx, cy + pdy, pr, pr < ry ? pr : ry,
                     FMG_RGB565(94, 58, 34));
    fmg_fill_ellipse(px, cx + pdx, cy + pdy, 2, 2 < ry ? 2 : ry,
                     FMG_RGB565(30, 20, 16));
    fmg_pixel(px, cx + pdx - 2, cy + pdy - 2, FMG_RGB565(255, 255, 255));
    if (lid_q8 < 200) {
        /* upper lid descends over the white */
        int32_t cover = ((200 - lid_q8) * ry) / 200;
        fmg_fill_rect(px, cx - 15, cy - ry - 1, 31, cover + 1,
                      FMG_RGB565(243, 214, 186));
        fmg_fill_rect(px, cx - 14, cy - ry - 1 + cover, 29, 2,
                      FMG_RGB565(74, 38, 32));
    }
}

void fmg_render_preston(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;

    fmg_fill(px, FMG_RGB565(247, 232, 208));

    /* blush */
    fmg_fill_ellipse_blend(px, 34 + sway, 72 + breath, 9, 5,
                           FMG_RGB565(246, 168, 152), 110);
    fmg_fill_ellipse_blend(px, 126 + sway, 72 + breath, 9, 5,
                           FMG_RGB565(246, 168, 152), 110);

    int32_t look_dx = (int32_t)kf->look_x * 9 / 128 + (idle.gaze_dx_q8 >> 8);
    int32_t look_dy = (int32_t)kf->look_y * 6 / 128 + (idle.gaze_dy_q8 >> 8);
    int32_t eye_y = 46 + breath;
    fmg_preston_eye(px, 52 + sway, eye_y, idle.lid_l_q8, look_dx, look_dy);
    fmg_preston_eye(px, 108 + sway, eye_y, idle.lid_r_q8, look_dx, look_dy);

    /* brows: thick strokes that ride the idle/keyframe brow offset */
    int32_t bl = eye_y - 24 + (idle.brow_l_q8 >> 8);
    int32_t br = eye_y - 24 + (idle.brow_r_q8 >> 8);
    fmg_fill_round_rect(px, 52 + sway - 12, bl, 25, 4, 2,
                        FMG_RGB565(96, 60, 40));
    fmg_fill_round_rect(px, 108 + sway - 12, br, 25, 4, 2,
                        FMG_RGB565(96, 60, 40));

    /* button nose */
    fmg_fill_ellipse(px, 80 + sway, 66 + breath, 5, 4,
                     FMG_RGB565(232, 186, 150));

    const fmg_sprite_t *cel = fmg_preston_pick(&mouth);
    int32_t jaw_drop = (mouth.jaw_q8 * 6) >> 8;
    fmg_sprite_blit(px, cel, 80 + sway, 92 + breath + jaw_drop, 2, s_pal,
                    (int)(sizeof(s_pal) / sizeof(s_pal[0])));
}
