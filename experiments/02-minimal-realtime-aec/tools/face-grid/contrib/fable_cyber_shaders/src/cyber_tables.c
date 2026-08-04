#include "cyber_internal.h"

/*
 * Init-time table construction. Everything is integer; 64-bit division
 * is allowed here (never in the per-pixel paths) because init runs once.
 */

/*
 * Canonical 8x8 Bayer ordered-dither threshold matrix (values 0..63),
 * from the recursive construction in B. E. Bayer, "An optimum method for
 * two-level rendition of continuous-tone pictures", ICC 1973. The matrix
 * itself is public-domain mathematics.
 */
const uint8_t cyber_bayer8[64] = {
    0, 32, 8, 40, 2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4, 36, 14, 46, 6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
    3, 35, 11, 43, 1, 33, 9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7, 39, 13, 45, 5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

static uint16_t pack565(uint32_t r, uint32_t g, uint32_t b)
{
    if (r > 255u) {
        r = 255u;
    }
    if (g > 255u) {
        g = 255u;
    }
    if (b > 255u) {
        b = 255u;
    }
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint32_t lerp8(uint32_t a, uint32_t b, uint32_t t_q8)
{
    return a + (((b - a) * t_q8) >> 8) + ((b < a) ? 0u : 0u);
}

/* Signed-safe channel interpolation (channels are 0..255). */
static uint32_t mix_channel(uint32_t a, uint32_t b, uint32_t t_q8)
{
    int32_t delta = (int32_t)b - (int32_t)a;
    int32_t v = (int32_t)a + ((delta * (int32_t)t_q8) >> 8);
    return (uint32_t)cyber_clamp32(v, 0, 255);
}

typedef struct {
    uint8_t index;
    uint8_t r, g, b;
} cyber_stop_t;

/* Piecewise-linear multi-stop gradient into a 256-entry RGB565 ramp. */
static void build_ramp(uint16_t *pal, const cyber_stop_t *stops,
                       int stop_count)
{
    int seg = 0;
    for (int i = 0; i < CYBER_PALETTE_SIZE; ++i) {
        while (seg + 2 < stop_count && i > stops[seg + 1].index) {
            ++seg;
        }
        const cyber_stop_t *s0 = &stops[seg];
        const cyber_stop_t *s1 = &stops[seg + 1];
        uint32_t span = (uint32_t)(s1->index - s0->index);
        uint32_t t_q8 = 0;
        if (span > 0 && i >= s0->index) {
            uint32_t pos = (uint32_t)(i - s0->index);
            t_q8 = (pos * 256u) / span;
            if (t_q8 > 256u) {
                t_q8 = 256u;
            }
        }
        pal[i] = pack565(mix_channel(s0->r, s1->r, t_q8),
                         mix_channel(s0->g, s1->g, t_q8),
                         mix_channel(s0->b, s1->b, t_q8));
    }
}

/*
 * Sine table via the Bhaskara I approximation,
 *   sin(pi*u) ~= 16*u*(1-u) / (5 - 4*u*(1-u)),  u in [0, 1],
 * evaluated in integers (worst-case error ~0.16%). 64-bit math is fine
 * at init time.
 */
static void build_sin_table(int16_t *table)
{
    for (int i = 0; i < CYBER_SIN_TABLE_SIZE; ++i) {
        int half = i & (CYBER_SIN_TABLE_SIZE / 2 - 1); /* 0..511 */
        /* S = a*(512-a), scale 2^18 of u*(1-u). */
        int64_t s = (int64_t)half * (512 - half);
        int64_t numerator = (16 * s) << 14;
        int64_t denominator = (5 << 18) - 4 * s;
        int32_t value = (int32_t)(numerator / denominator);
        if (i >= CYBER_SIN_TABLE_SIZE / 2) {
            value = -value;
        }
        table[i] = (int16_t)value;
    }
}

/*
 * Exponential glow falloff g[d] = 255 * ratio^d built by iterated Q16
 * multiplication; index is |distance| in Q4 pixels. ratio_q16 selects the
 * half-life: 0.5^(1/(halflife_px*16)) in Q16.
 */
static void build_glow(uint8_t *table, uint32_t ratio_q16)
{
    uint32_t raw[CYBER_GLOW_TABLE_SIZE];
    uint32_t acc_q16 = 255u << 16;
    for (int i = 0; i < CYBER_GLOW_TABLE_SIZE; ++i) {
        raw[i] = acc_q16 >> 16;
        acc_q16 = (uint32_t)(((uint64_t)acc_q16 * ratio_q16) >> 16);
    }
    /*
     * Renormalize so the table reaches exactly zero at its end. This
     * removes the far-distance plateau a clamped lookup would produce
     * and lets renderers skip geometry past the table range without
     * changing any pixel.
     */
    uint32_t tail = raw[CYBER_GLOW_TABLE_SIZE - 1];
    for (int i = 0; i < CYBER_GLOW_TABLE_SIZE; ++i) {
        uint32_t v = raw[i] > tail ? raw[i] - tail : 0u;
        table[i] = (uint8_t)((v * 255u) / (255u - tail));
    }
}

/* Separable vignette factor tables, Q8, minimum ~132 in the corners. */
static void build_vignette(uint8_t *vx, int nx, uint8_t *vy, int ny)
{
    for (int i = 0; i < nx; ++i) {
        int32_t centred = i * 2 - (nx - 1);
        int32_t falloff = (centred * centred * 64) / ((nx - 1) * (nx - 1));
        vx[i] = (uint8_t)(255 - falloff);
    }
    for (int i = 0; i < ny; ++i) {
        int32_t centred = i * 2 - (ny - 1);
        int32_t falloff = (centred * centred * 56) / ((ny - 1) * (ny - 1));
        vy[i] = (uint8_t)(255 - falloff);
    }
}

/*
 * Plasma palette: entries 0..127 are a cyclic indigo/magenta/cyan hue
 * wheel via the cosine-palette construction col(t) = a + b*cos(2pi*t + d)
 * (Quilez form with c = 1 so the ramp tiles seamlessly); entries 128..255
 * are a dark-to-white ramp used inside the face silhouette.
 */
static void build_plasma_palette(uint16_t *pal, const int16_t *sin_q14)
{
    for (int i = 0; i < 128; ++i) {
        uint32_t turns = ((uint32_t)i << 16) / 128u;
        uint32_t quarter = CYBER_TURN / 4u;
        int32_t cr = sin_q14[(((turns + quarter) >> 6) +
                              (uint32_t)(0.00 * 0)) &
                             (CYBER_SIN_TABLE_SIZE - 1)];
        int32_t cg = sin_q14[(((turns + quarter + 21845u) >> 6)) &
                             (CYBER_SIN_TABLE_SIZE - 1)];
        int32_t cb = sin_q14[(((turns + quarter + 43690u) >> 6)) &
                             (CYBER_SIN_TABLE_SIZE - 1)];
        /* a + b*cos: tuned duotone — R mid, G low, B high. */
        int32_t r = 118 + ((cr * 104) >> 14);
        int32_t g = 34 + ((cg * 30) >> 14);
        int32_t b = 150 + ((cb * 100) >> 14);
        pal[i] = pack565((uint32_t)cyber_clamp32(r, 0, 255),
                         (uint32_t)cyber_clamp32(g, 0, 255),
                         (uint32_t)cyber_clamp32(b, 0, 255));
    }
    static const cyber_stop_t face_ramp[] = {
        { 128, 8, 12, 24 },
        { 176, 70, 160, 190 },
        { 224, 170, 240, 250 },
        { 255, 244, 255, 255 },
    };
    /* Reuse the generic builder on the upper half by offsetting stops. */
    for (int i = 128; i < CYBER_PALETTE_SIZE; ++i) {
        int seg = 0;
        while (seg + 2 < (int)(sizeof(face_ramp) / sizeof(face_ramp[0])) &&
               i > face_ramp[seg + 1].index) {
            ++seg;
        }
        const cyber_stop_t *s0 = &face_ramp[seg];
        const cyber_stop_t *s1 = &face_ramp[seg + 1];
        uint32_t span = (uint32_t)(s1->index - s0->index);
        uint32_t t_q8 =
            span ? (((uint32_t)(i - s0->index) * 256u) / span) : 0u;
        pal[i] = pack565(mix_channel(s0->r, s1->r, t_q8),
                         mix_channel(s0->g, s1->g, t_q8),
                         mix_channel(s0->b, s1->b, t_q8));
    }
}

/*
 * Edge-glow palette: eight 32-entry hue bands (Siri-ish rainbow), each a
 * black -> hue -> near-white ramp so the border can sweep hues while the
 * face reads as tinted white at full brightness.
 */
static void build_edge_palette(uint16_t *pal)
{
    static const uint8_t hues[8][3] = {
        { 0, 200, 255 },   /* cyan      */
        { 40, 120, 255 },  /* azure     */
        { 120, 60, 255 },  /* violet    */
        { 220, 40, 255 },  /* magenta   */
        { 255, 60, 180 },  /* pink      */
        { 255, 120, 60 },  /* ember     */
        { 60, 255, 160 },  /* spring    */
        { 0, 255, 230 },   /* aqua      */
    };
    for (int band = 0; band < 8; ++band) {
        for (int level = 0; level < 32; ++level) {
            uint32_t t_q8 = (uint32_t)level * 256u / 31u;
            uint32_t r, g, b;
            if (t_q8 < 176u) {
                uint32_t u = t_q8 * 256u / 176u;
                r = (hues[band][0] * u) >> 8;
                g = (hues[band][1] * u) >> 8;
                b = (hues[band][2] * u) >> 8;
            } else {
                uint32_t u = (t_q8 - 176u) * 256u / 80u;
                r = mix_channel(hues[band][0], 250, u);
                g = mix_channel(hues[band][1], 252, u);
                b = mix_channel(hues[band][2], 255, u);
            }
            pal[band * 32 + level] = pack565(r, g, b);
        }
    }
}

static void scale_palette(uint16_t *dst, const uint16_t *src,
                          uint32_t gain_q8)
{
    for (int i = 0; i < CYBER_PALETTE_SIZE; ++i) {
        uint32_t c = src[i];
        uint32_t r = (c >> 11) & 31u;
        uint32_t g = (c >> 5) & 63u;
        uint32_t b = c & 31u;
        r = (r * gain_q8) >> 8;
        g = (g * gain_q8) >> 8;
        b = (b * gain_q8) >> 8;
        dst[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

int32_t cyber_sd_segment(int32_t x, int32_t y, int32_t ax, int32_t ay,
                         int32_t bx, int32_t by, int32_t r)
{
    int32_t pax = x - ax;
    int32_t pay = y - ay;
    int32_t bax = bx - ax;
    int32_t bay = by - ay;
    /* Q4 dot products; screen-space magnitudes keep these in range. */
    int32_t dot_pb = pax * bax + pay * bay;
    int32_t dot_bb = bax * bax + bay * bay;
    if (dot_bb <= 0) {
        return cyber_length_q4(pax, pay) - r;
    }
    /* h in Q7; >>4 pre-scale keeps the multiply inside int32. */
    int32_t h_q7 = ((dot_pb >> 4) * 128) / (dot_bb >> 4);
    h_q7 = cyber_clamp32(h_q7, 0, 128);
    int32_t cx = ax + ((bax * h_q7) >> 7);
    int32_t cy = ay + ((bay * h_q7) >> 7);
    return cyber_length_q4(x - cx, y - cy) - r;
}

void cyber_face_init(cyber_face_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    build_sin_table(ctx->sin_q14);

    /* Half-lives of roughly 0.8 px, 3 px, and 10 px (Q4 steps). */
    build_glow(ctx->glow_core, 62000u);
    build_glow(ctx->glow_neon, 64580u);
    build_glow(ctx->glow_soft, 65262u);

    build_vignette(ctx->vignette_x, CYBER_FIELD_WIDTH, ctx->vignette_y,
                   CYBER_FIELD_HEIGHT);

    {
        static const cyber_stop_t neon_cyan[] = {
            { 0, 0, 0, 0 },
            { 88, 0, 38, 78 },
            { 200, 0, 205, 255 },
            { 255, 205, 255, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_NEON_SDF_CYAN], neon_cyan,
                   4);
    }
    {
        static const cyber_stop_t neon_magenta[] = {
            { 0, 0, 0, 0 },
            { 88, 52, 0, 66 },
            { 200, 255, 0, 205 },
            { 255, 255, 215, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_NEON_SDF_MAGENTA],
                   neon_magenta, 4);
    }
    {
        static const cyber_stop_t liquid[] = {
            { 0, 0, 0, 0 },
            { 80, 0, 44, 58 },
            { 168, 0, 210, 175 },
            { 224, 130, 255, 235 },
            { 255, 235, 255, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_LIQUID_SMIN], liquid, 5);
    }
    {
        static const cyber_stop_t crt[] = {
            { 0, 0, 0, 0 },
            { 92, 0, 52, 34 },
            { 204, 44, 255, 170 },
            { 255, 214, 255, 230 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_CRT_CHROMATIC], crt, 4);
    }
    {
        static const cyber_stop_t holo[] = {
            { 0, 0, 0, 0 },
            { 76, 0, 30, 92 },
            { 190, 70, 220, 255 },
            { 255, 225, 250, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_HOLO_WIREFRAME], holo, 4);
    }
    {
        static const cyber_stop_t orb[] = {
            { 0, 0, 0, 0 },
            { 84, 28, 0, 92 },
            { 186, 120, 82, 255 },
            { 255, 225, 232, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_VOICE_ORB], orb, 4);
    }
    {
        static const cyber_stop_t optic[] = {
            { 0, 0, 0, 0 },
            { 84, 58, 0, 0 },
            { 190, 255, 34, 8 },
            { 255, 255, 224, 168 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_RED_OPTIC], optic, 4);
    }
    {
        static const cyber_stop_t hub75[] = {
            { 0, 0, 0, 0 },
            { 72, 34, 0, 52 },
            { 172, 255, 0, 178 },
            { 224, 255, 120, 230 },
            { 255, 255, 244, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_HUB75_NEON], hub75, 5);
    }
    build_edge_palette(ctx->palette[CYBER_PROFILE_EDGE_GLOW]);
    {
        static const cyber_stop_t glitch[] = {
            { 0, 0, 0, 0 },
            { 76, 26, 42, 66 },
            { 196, 168, 224, 255 },
            { 255, 255, 255, 255 },
        };
        build_ramp(ctx->palette[CYBER_PROFILE_GLITCH_MASK], glitch, 4);
    }
    build_plasma_palette(ctx->palette[CYBER_PROFILE_PALETTE_PLASMA],
                         ctx->sin_q14);

    /* HUB75 LED cell shading: centre, inner, edge, grid-gap. */
    scale_palette(ctx->led_palette[0],
                  ctx->palette[CYBER_PROFILE_HUB75_NEON], 256u);
    scale_palette(ctx->led_palette[1],
                  ctx->palette[CYBER_PROFILE_HUB75_NEON], 196u);
    scale_palette(ctx->led_palette[2],
                  ctx->palette[CYBER_PROFILE_HUB75_NEON], 96u);
    scale_palette(ctx->led_palette[3],
                  ctx->palette[CYBER_PROFILE_HUB75_NEON], 22u);

    for (int i = 0; i < CYBER_FACE_PIXEL_COUNT; ++i) {
        ctx->scratch[i] = 0;
    }
    ctx->magic = CYBER_CTX_MAGIC;
    (void)lerp8;
}
