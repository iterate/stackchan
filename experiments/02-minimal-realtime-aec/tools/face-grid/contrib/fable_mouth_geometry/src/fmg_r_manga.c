#include "fmg_internal.h"

/*
 * Manga snap: clean white line-art face with anime-style mouth cels that
 * snap between poses (selection quantized so it steps like limited
 * animation shot on threes), big shine-dotted irises, ∩-shaped happy
 * blinks, blush strokes, a sweat drop while thinking, and emphasis lines
 * when shouting.
 */

#define MANGA_BG FMG_RGB565(250, 250, 252)
#define MANGA_INK FMG_RGB565(24, 20, 28)
#define MANGA_IRIS FMG_RGB565(64, 72, 112)
#define MANGA_RED FMG_RGB565(158, 44, 66)
#define MANGA_PINK FMG_RGB565(240, 130, 148)
#define MANGA_BLUSH FMG_RGB565(244, 156, 164)
#define MANGA_SWEAT FMG_RGB565(120, 170, 235)

typedef const char *const fmg_rows10_t[10];

static fmg_rows10_t s_m_rest = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    "....oo........oo....",
    ".....oo......oo.....",
    "......oooooooo......",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_press = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    "....oooooooooooo....",
    "....................",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_small_o = {
    "....................",
    "....................",
    "....................",
    ".......oooooo.......",
    "......oo....oo......",
    "......o......o......",
    "......oo....oo......",
    ".......oooooo.......",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_shout = {
    "....................",
    "...oooooooooooooo...",
    "...oiiiiiiiiiiiio...",
    "....oiiiiiiiiiio....",
    "....oiiiiiiiiiio....",
    ".....oiiiiiiiio.....",
    ".....oiggggggio.....",
    "......oggggggo......",
    ".......oooooo.......",
    "....................",
};

static fmg_rows10_t s_m_smile = {
    "....................",
    "....................",
    "..oo............oo..",
    "...oo..........oo...",
    "....oooooooooooo....",
    "....otttttttttto....",
    "....oooooooooooo....",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_teeth = {
    "....................",
    "....................",
    "....................",
    "....oooooooooooo....",
    "....otttttttttto....",
    "....oiiiiiiiiiio....",
    "....oooooooooooo....",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_grin = {
    "....................",
    "....................",
    "....................",
    "....oooooooooooo....",
    "....ottottottotto...",
    "....oooooooooooo....",
    "....................",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_fv = {
    "....................",
    "....................",
    "....................",
    "....................",
    ".....oooooooooo.....",
    ".....otttttttto.....",
    "......oooooooo......",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_ln = {
    "....................",
    "....................",
    "....................",
    "......oooooooo......",
    ".....oggggggggo.....",
    ".....oiiiiiiiio.....",
    "......oooooooo......",
    "....................",
    "....................",
    "....................",
};

static fmg_rows10_t s_m_wavy = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    ".....oo..oo..oo.....",
    "....o..oo..oo..o....",
    "....................",
    "....................",
    "....................",
};

static const fmg_sprite_pal_t s_pal[] = {
    {'o', MANGA_INK},
    {'i', MANGA_RED},
    {'t', FMG_RGB565(255, 255, 255)},
    {'g', MANGA_PINK},
};

static const fmg_sprite_t *fmg_manga_pick(
    const fmg_mouth_t *m, const fmg_keyframe_t *kf)
{
    static const fmg_sprite_t rest = {s_m_rest, 20, 10};
    static const fmg_sprite_t press = {s_m_press, 20, 10};
    static const fmg_sprite_t small_o = {s_m_small_o, 20, 10};
    static const fmg_sprite_t shout = {s_m_shout, 20, 10};
    static const fmg_sprite_t smile = {s_m_smile, 20, 10};
    static const fmg_sprite_t teeth = {s_m_teeth, 20, 10};
    static const fmg_sprite_t grin = {s_m_grin, 20, 10};
    static const fmg_sprite_t fv = {s_m_fv, 20, 10};
    static const fmg_sprite_t ln = {s_m_ln, 20, 10};
    static const fmg_sprite_t wavy = {s_m_wavy, 20, 10};
    if (!m->speaking && kf->expression == FMG_ACTIVITY_THINKING) {
        return &wavy;
    }
    /* quantize openness so cel changes snap like limited animation */
    int32_t open_snap = (m->open_q8 >> 6) << 6;
    switch (m->vis) {
    case FMG_VIS_MBP:
        return &press;
    case FMG_VIS_SS:
        return &grin;
    case FMG_VIS_FV:
        return &fv;
    case FMG_VIS_EE:
        return &smile;
    case FMG_VIS_IH:
        return &teeth;
    case FMG_VIS_OH:
    case FMG_VIS_UU:
        return open_snap >= 224 ? &shout : &small_o;
    case FMG_VIS_LN:
        return &ln;
    case FMG_VIS_AA:
        return open_snap >= 128 ? &shout : &teeth;
    case FMG_VIS_REST:
    default:
        return open_snap >= 128 ? &teeth : &rest;
    }
}

/* ring ellipse: filled dark then punched with the background color */
static void fmg_ring(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    int32_t thick, uint16_t ink, uint16_t bg)
{
    fmg_fill_ellipse(px, cx, cy, rx, ry, ink);
    if (rx > thick && ry > thick) {
        fmg_fill_ellipse(px, cx, cy, rx - thick, ry - thick, bg);
    }
}

static void fmg_manga_eye(
    uint16_t *px, int32_t cx, int32_t cy, int32_t lid_q8, int32_t look_dx,
    int32_t look_dy, bool happy)
{
    if (lid_q8 <= 40) {
        /* closed: happy ∩ arch or sleepy line */
        if (happy) {
            for (int32_t dy = -6; dy <= 0; dy++) {
                int32_t half = (int32_t)(11 * fmg_isqrt((uint32_t)(36 - dy * dy))) / 6;
                fmg_hline(px, cx - half, cx - half + 2, cy + dy, MANGA_INK);
                fmg_hline(px, cx + half - 2, cx + half, cy + dy, MANGA_INK);
            }
            fmg_fill_rect(px, cx - 3, cy - 7, 7, 2, MANGA_INK);
        } else {
            fmg_fill_rect(px, cx - 10, cy - 1, 21, 2, MANGA_INK);
        }
        return;
    }
    int32_t ry = (15 * fmg_clampi(lid_q8, 0, 280)) >> 8;
    if (ry < 3) {
        ry = 3;
    }
    fmg_ring(px, cx, cy, 12, ry, 2, MANGA_INK, MANGA_BG);
    int32_t pdx = fmg_clampi(look_dx, -4, 4);
    int32_t pdy = fmg_clampi(look_dy, -(ry - 3), ry - 3);
    int32_t iry = ry - 2 < 9 ? ry - 2 : 9;
    fmg_fill_ellipse(px, cx + pdx, cy + pdy, 6, iry, MANGA_IRIS);
    fmg_fill_ellipse(px, cx + pdx, cy + pdy + 1, 3, iry > 4 ? 4 : iry - 1,
                     MANGA_INK);
    fmg_fill_ellipse(px, cx + pdx - 2, cy + pdy - iry / 2, 2, 2,
                     FMG_RGB565(255, 255, 255));
    fmg_pixel(px, cx + pdx + 3, cy + pdy + 2, FMG_RGB565(255, 255, 255));
    /* heavy upper lash */
    fmg_fill_rect(px, cx - 12, cy - ry - 1, 25, 2, MANGA_INK);
}

void fmg_render_manga(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;

    fmg_fill(px, MANGA_BG);

    bool happy = kf->expression == FMG_ACTIVITY_LISTENING && !mouth.speaking;

    /* blush strokes */
    for (int i = 0; i < 3; i++) {
        fmg_line(px, 30 + sway + i * 4, 74 + breath, 34 + sway + i * 4,
                 68 + breath, MANGA_BLUSH);
        fmg_line(px, 122 + sway + i * 4, 74 + breath, 126 + sway + i * 4,
                 68 + breath, MANGA_BLUSH);
    }

    int32_t look_dx = (int32_t)kf->look_x * 8 / 128 + (idle.gaze_dx_q8 >> 8);
    int32_t look_dy = (int32_t)kf->look_y * 5 / 128 + (idle.gaze_dy_q8 >> 8);
    int32_t eye_y = 50 + breath;
    fmg_manga_eye(px, 50 + sway, eye_y, idle.lid_l_q8, look_dx, look_dy,
                  happy);
    fmg_manga_eye(px, 110 + sway, eye_y, idle.lid_r_q8, look_dx, look_dy,
                  happy);

    /* thin arched brows */
    int32_t bl = eye_y - 26 + (idle.brow_l_q8 >> 8);
    int32_t br = eye_y - 26 + (idle.brow_r_q8 >> 8);
    fmg_line(px, 38 + sway, bl + 3, 50 + sway, bl, MANGA_INK);
    fmg_line(px, 50 + sway, bl, 62 + sway, bl + 2, MANGA_INK);
    fmg_line(px, 98 + sway, br + 2, 110 + sway, br, MANGA_INK);
    fmg_line(px, 110 + sway, br, 122 + sway, br + 3, MANGA_INK);

    /* nose tick */
    fmg_line(px, 80 + sway, 66 + breath, 78 + sway, 70 + breath, MANGA_INK);

    /* sweat drop while thinking */
    if (kf->expression == FMG_ACTIVITY_THINKING) {
        int32_t drop = (fmg_sin_q14(
                            (uint16_t)(((uint64_t)(clock % 48000) << 16) /
                                       48000)) *
                        3) >> 14;
        fmg_fill_ellipse(px, 134 + sway, 34 + drop, 4, 6, MANGA_SWEAT);
        fmg_fill_ellipse(px, 133 + sway, 32 + drop, 1, 2,
                         FMG_RGB565(230, 242, 255));
    }

    const fmg_sprite_t *cel = fmg_manga_pick(&mouth, kf);
    fmg_sprite_blit(px, cel, 80 + sway, 90 + breath, 2, s_pal,
                    (int)(sizeof(s_pal) / sizeof(s_pal[0])));

    /* emphasis lines when shouting */
    if (mouth.open_q8 > 200 && mouth.speaking) {
        fmg_line(px, 52 + sway, 84 + breath, 44 + sway, 80 + breath,
                 MANGA_INK);
        fmg_line(px, 52 + sway, 96 + breath, 44 + sway, 100 + breath,
                 MANGA_INK);
        fmg_line(px, 108 + sway, 84 + breath, 116 + sway, 80 + breath,
                 MANGA_INK);
        fmg_line(px, 108 + sway, 96 + breath, 116 + sway, 100 + breath,
                 MANGA_INK);
    }
}
