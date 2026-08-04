#include "fmg_internal.h"

/*
 * Origami mask: a low-poly papercraft face in the Wintercroft-mask
 * tradition — flat-shaded triangle facets with visible fold creases.
 * Jaw facets hinge downward with JA and darken as they rotate away from
 * the light; the mouth is a faceted hexagonal opening with a paper-teeth
 * zigzag. Eyes are backlit angular slits that blink and drift.
 */

#define PAPER_BG FMG_RGB565(30, 28, 52)
#define CREASE FMG_RGB565(96, 88, 110)
#define SLIT_GLOW FMG_RGB565(255, 214, 130)
#define MOUTH_HOLE FMG_RGB565(20, 16, 30)

enum { OV_COUNT = 20 };

/* base vertex table, indexed by the triangle list below */
static const int16_t s_base_v[OV_COUNT][2] = {
    {80, 6},    /* 0 forehead peak */
    {30, 30},   /* 1 temple L */
    {130, 30},  /* 2 temple R */
    {80, 34},   /* 3 brow center */
    {42, 46},   /* 4 eye outer L */
    {118, 46},  /* 5 eye outer R */
    {66, 50},   /* 6 eye inner L */
    {94, 50},   /* 7 eye inner R */
    {80, 46},   /* 8 nose bridge */
    {80, 70},   /* 9 nose tip */
    {64, 74},   /* 10 nose wing L */
    {96, 74},   /* 11 nose wing R */
    {32, 70},   /* 12 cheek L */
    {128, 70},  /* 13 cheek R */
    {44, 96},   /* 14 jaw side L */
    {116, 96},  /* 15 jaw side R */
    {58, 88},   /* 16 mouth corner L */
    {102, 88},  /* 17 mouth corner R */
    {80, 104},  /* 18 chin top */
    {80, 118},  /* 19 chin point */
};

typedef struct {
    uint8_t a, b, c;
    uint8_t shade; /* base facet luminance 0..255 */
    uint8_t jaw_group; /* nonzero facets darken and drop with the jaw */
} fmg_ofacet_t;

static const fmg_ofacet_t s_facets[] = {
    {0, 1, 3, 208, 0},  {0, 3, 2, 178, 0},
    {1, 4, 3, 188, 0},  {3, 5, 2, 160, 0},
    {3, 4, 6, 214, 0},  {3, 7, 5, 186, 0},
    {3, 6, 8, 200, 0},  {3, 8, 7, 176, 0},
    {4, 12, 6, 172, 0}, {7, 13, 5, 150, 0},
    {6, 12, 10, 196, 0}, {7, 11, 13, 168, 0},
    {6, 10, 8, 216, 0}, {8, 11, 7, 188, 0},
    {8, 10, 9, 232, 0}, {8, 9, 11, 204, 0},
    {12, 16, 10, 182, 0}, {11, 17, 13, 158, 0},
    {10, 16, 9, 206, 0}, {9, 17, 11, 178, 0},
    {12, 14, 16, 166, 1}, {17, 15, 13, 142, 1},
    {14, 18, 16, 184, 1}, {17, 18, 15, 156, 1},
    {16, 18, 17, 196, 2}, {14, 19, 18, 150, 1},
    {18, 19, 15, 132, 1},
};

void fmg_render_origami(
    const fmg_keyframe_t *kf, uint32_t clock, uint16_t *px)
{
    fmg_idle_t idle;
    fmg_mouth_t mouth;
    fmg_idle_compute(kf, clock, &idle);
    fmg_mouth_compute(kf, &mouth);

    int32_t sway = idle.sway_q8 >> 8;
    int32_t breath = idle.breath_q8 >> 8;
    int32_t jaw = mouth.jaw_q8;
    int32_t jaw_drop = (jaw * 16) >> 8;
    int32_t corner_dx = ((mouth.width_q8 - 128) * 8 / 128) -
                        ((mouth.round_q8 * 8) >> 8);

    fmg_fill(px, PAPER_BG);
    /* wall shadow behind the mask */
    fmg_fill_ellipse_blend(px, 84, 66, 62, 58, FMG_RGB565(16, 14, 30), 130);

    /* posed vertices */
    int32_t vx[OV_COUNT], vy[OV_COUNT];
    for (int i = 0; i < OV_COUNT; i++) {
        vx[i] = s_base_v[i][0] + sway;
        vy[i] = s_base_v[i][1] + breath;
    }
    /* jaw hinge */
    vy[14] += jaw_drop / 2;
    vy[15] += jaw_drop / 2;
    vy[18] += jaw_drop;
    vy[19] += jaw_drop;
    /* mouth corners spread with width, purse with round */
    vx[16] = s_base_v[16][0] + sway - (corner_dx > 0 ? corner_dx : 0) +
             (corner_dx < 0 ? -corner_dx / 2 : 0);
    vx[17] = s_base_v[17][0] + sway + (corner_dx > 0 ? corner_dx : 0) -
             (corner_dx < 0 ? -corner_dx / 2 : 0);
    vy[16] += jaw_drop / 3;
    vy[17] += jaw_drop / 3;

    const int facet_count = (int)(sizeof(s_facets) / sizeof(s_facets[0]));
    for (int f = 0; f < facet_count; f++) {
        const fmg_ofacet_t *fc = &s_facets[f];
        int32_t lum = fc->shade;
        if (fc->jaw_group == 1) {
            lum -= (jaw * 44) >> 8; /* jaw facets rotate out of the light */
        } else if (fc->jaw_group == 2) {
            lum -= (jaw * 20) >> 8;
        }
        lum = fmg_clampi(lum, 40, 248);
        /* warm paper: R leads, B trails */
        uint16_t tone = FMG_RGB565(
            (uint32_t)fmg_clampi(lum + 14, 0, 255),
            (uint32_t)fmg_clampi(lum + 4, 0, 255),
            (uint32_t)fmg_clampi(lum - 10, 0, 255));
        int32_t tri[6] = {
            vx[fc->a] << 4, vy[fc->a] << 4,
            vx[fc->b] << 4, vy[fc->b] << 4,
            vx[fc->c] << 4, vy[fc->c] << 4,
        };
        fmg_poly_fill(px, tri, 3, tone);
    }
    /* fold creases */
    for (int f = 0; f < facet_count; f++) {
        const fmg_ofacet_t *fc = &s_facets[f];
        fmg_line(px, vx[fc->a], vy[fc->a], vx[fc->b], vy[fc->b], CREASE);
        fmg_line(px, vx[fc->b], vy[fc->b], vx[fc->c], vy[fc->c], CREASE);
        fmg_line(px, vx[fc->c], vy[fc->c], vx[fc->a], vy[fc->a], CREASE);
    }

    /* backlit eye slits: angular hexagons that blink */
    int32_t look_dx = (int32_t)kf->look_x * 4 / 128 + (idle.gaze_dx_q8 >> 9);
    int32_t look_dy = (int32_t)kf->look_y * 3 / 128 + (idle.gaze_dy_q8 >> 9);
    for (int side = 0; side < 2; side++) {
        int32_t lid = side == 0 ? idle.lid_l_q8 : idle.lid_r_q8;
        int32_t half_h = (5 * fmg_clampi(lid, 0, 256)) >> 8;
        int32_t ecx = (side == 0 ? 54 : 106) + sway + look_dx;
        int32_t ecy = 47 + breath + look_dy;
        if (half_h <= 0) {
            fmg_line(px, ecx - 10, ecy, ecx + 10, ecy, MOUTH_HOLE);
            continue;
        }
        int32_t slit[12] = {
            (ecx - 11) << 4, ecy << 4,
            (ecx - 4) << 4, (ecy - half_h) << 4,
            (ecx + 5) << 4, (ecy - half_h + 1) << 4,
            (ecx + 11) << 4, ecy << 4,
            (ecx + 4) << 4, (ecy + half_h) << 4,
            (ecx - 5) << 4, (ecy + half_h - 1) << 4,
        };
        fmg_poly_fill(px, slit, 6, SLIT_GLOW);
        fmg_pixel_blend(px, ecx - 6, ecy - half_h / 2, FMG_RGB565(255, 246, 210),
                        180);
        /* brow crease */
        int32_t brow = side == 0 ? idle.brow_l_q8 : idle.brow_r_q8;
        int32_t byy = ecy - 9 + (brow >> 8);
        fmg_line(px, ecx - 10, byy + 2, ecx, byy, CREASE);
        fmg_line(px, ecx, byy, ecx + 10, byy + 2, CREASE);
    }

    /* faceted mouth opening between vertices 16/17 */
    int32_t mcx = 80 + sway;
    int32_t up_y = vy[16] < vy[17] ? vy[16] : vy[17];
    int32_t open_dn = 2 + ((jaw * 20) >> 8);
    if (mouth.press_q8 > 150) {
        /* pressed: the hole folds shut into a crease */
        fmg_line(px, vx[16], vy[16], mcx, vy[16] + 1, MOUTH_HOLE);
        fmg_line(px, mcx, vy[16] + 1, vx[17], vy[17], MOUTH_HOLE);
        fmg_line(px, vx[16], vy[16] + 2, vx[17], vy[17] + 2, CREASE);
    } else {
        int32_t hole[12] = {
            vx[16] << 4, vy[16] << 4,
            (mcx - 10) << 4, (up_y - 3) << 4,
            (mcx + 10) << 4, (up_y - 3) << 4,
            vx[17] << 4, vy[17] << 4,
            (mcx + 9) << 4, (up_y + open_dn) << 4,
            (mcx - 9) << 4, (up_y + open_dn) << 4,
        };
        fmg_poly_fill(px, hole, 6, MOUTH_HOLE);
        /* paper teeth: zigzag strip hanging from the top edge */
        if (mouth.teeth_q8 > 60 && open_dn > 5) {
            for (int32_t k = -2; k < 2; k++) {
                int32_t tx = mcx + k * 9 + 4;
                int32_t tooth[6] = {
                    (tx - 4) << 4, (up_y - 2) << 4,
                    (tx + 4) << 4, (up_y - 2) << 4,
                    tx << 4, (up_y + 3 + ((mouth.teeth_q8 * 3) >> 8)) << 4,
                };
                fmg_poly_fill(px, tooth, 3, FMG_RGB565(238, 234, 222));
            }
        }
        /* pursed lips: a lighter fold diamond around the hole */
        if (mouth.round_q8 > 150) {
            fmg_line(px, vx[16] - 2, vy[16], mcx, up_y - 6, CREASE);
            fmg_line(px, mcx, up_y - 6, vx[17] + 2, vy[17], CREASE);
            fmg_line(px, vx[16] - 2, vy[16], mcx, up_y + open_dn + 3, CREASE);
            fmg_line(px, mcx, up_y + open_dn + 3, vx[17] + 2, vy[17], CREASE);
        }
    }
    (void)clock;
}
