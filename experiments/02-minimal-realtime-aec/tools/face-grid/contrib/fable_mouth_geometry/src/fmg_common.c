#include "fmg_internal.h"

/* ---- fixed-point math ---- */

/*
 * Bhaskara I sine approximation on integers (max error ~0.16%), chosen over
 * a table so no generated data needs auditing. Full turn = 65536.
 */
int32_t fmg_sin_q14(uint16_t angle)
{
    int32_t half = angle & 0x7FFF;
    int64_t n = (int64_t)half * (32768 - half);
    int64_t denom = (5LL << 30) - 4 * n;
    int32_t s = (int32_t)(((16 * n) << 14) / denom);
    return (angle & 0x8000U) ? -s : s;
}

int32_t fmg_cos_q14(uint16_t angle)
{
    return fmg_sin_q14((uint16_t)(angle + 16384U));
}

uint32_t fmg_isqrt(uint32_t v)
{
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* lowbias32 integer scrambler (Chris Wellons, public domain constants). */
uint32_t fmg_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

int32_t fmg_smooth_q8(int32_t t_q8)
{
    t_q8 = fmg_clampi(t_q8, 0, 256);
    return (3 * t_q8 * t_q8 * 256 - 2 * t_q8 * t_q8 * t_q8) >> 16;
}

uint16_t fmg_blend565(uint16_t dst, uint16_t src, int32_t alpha_q8)
{
    alpha_q8 = fmg_clampi(alpha_q8, 0, 256);
    int32_t dr = (dst >> 11) & 31;
    int32_t dg = (dst >> 5) & 63;
    int32_t db = dst & 31;
    int32_t sr = (src >> 11) & 31;
    int32_t sg = (src >> 5) & 63;
    int32_t sb = src & 31;
    dr += ((sr - dr) * alpha_q8) >> 8;
    dg += ((sg - dg) * alpha_q8) >> 8;
    db += ((sb - db) * alpha_q8) >> 8;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static uint16_t fmg_angle_of(uint32_t t, uint32_t period)
{
    return (uint16_t)(((uint64_t)(t % period) << 16) / period);
}

/* ---- deterministic idle motion ---- */

enum {
    FMG_BLINK_PERIOD = 56000, /* 3.5 s between blink epochs */
    FMG_BLINK_CLOSE = 800,    /* fast lid drop (~50 ms) */
    FMG_BLINK_HOLD = 400,
    FMG_BLINK_OPEN = 1600,    /* slower reopen (~100 ms) */
    FMG_BLINK_TOTAL = FMG_BLINK_CLOSE + FMG_BLINK_HOLD + FMG_BLINK_OPEN,
    FMG_SACCADE_PERIOD = 20000, /* 1.25 s fixations */
    FMG_SACCADE_MOVE = 640,     /* 40 ms ballistic move */
    FMG_BREATH_PERIOD = 72000,  /* 4.5 s breathing cycle */
    FMG_SWAY_PERIOD = 208000,   /* 13 s lateral drift */
    FMG_BROW_PERIOD = 96000,    /* brow gesture epochs (6 s) */
};

/*
 * Lid closure envelope for one blink: 0 open .. 256 closed, with a small
 * negative tail after reopening (lids overshoot slightly wider — classic
 * follow-through) and a subtle pre-blink widen (anticipation).
 */
static int32_t fmg_blink_env(int32_t u)
{
    if (u < -600) {
        return 0;
    }
    if (u < 0) {
        return -(8 * (600 + u)) / 600; /* anticipation widen */
    }
    if (u < FMG_BLINK_CLOSE) {
        return fmg_smooth_q8(u * 256 / FMG_BLINK_CLOSE);
    }
    u -= FMG_BLINK_CLOSE;
    if (u < FMG_BLINK_HOLD) {
        return 256;
    }
    u -= FMG_BLINK_HOLD;
    if (u < FMG_BLINK_OPEN) {
        return 256 - fmg_smooth_q8(u * 256 / FMG_BLINK_OPEN);
    }
    u -= FMG_BLINK_OPEN;
    if (u < 900) {
        return -(12 * (900 - u)) / 900; /* follow-through overshoot */
    }
    return 0;
}

static int32_t fmg_blink_closure(uint32_t t)
{
    uint32_t epoch = t / FMG_BLINK_PERIOD;
    int32_t phase = (int32_t)(t % FMG_BLINK_PERIOD);
    uint32_t h = fmg_hash(epoch ^ 0xB11CB11CU);
    int32_t start = 6000 + (int32_t)(h % 40000U);
    int32_t closure = fmg_blink_env(phase - start);
    if ((h >> 8) % 5U == 0U) {
        /* occasional quick double blink */
        int32_t second = fmg_blink_env(phase - start - FMG_BLINK_TOTAL - 900);
        if (second > closure) {
            closure = second;
        }
    }
    return closure;
}

static void fmg_saccade_target(uint32_t epoch, int32_t *dx, int32_t *dy)
{
    uint32_t h = fmg_hash(epoch ^ 0x5ACCADE5U);
    *dx = (int32_t)((h & 0xFFU) * 11U >> 8) - 5;
    *dy = (int32_t)(((h >> 8) & 0xFFU) * 7U >> 8) - 3;
}

void fmg_idle_compute(
    const fmg_keyframe_t *kf, uint32_t clock, fmg_idle_t *out)
{
    bool speaking = (kf->flags & FMG_FLAG_SPEAKING) != 0;

    /* blink gates the keyframe's commanded eye openness */
    int32_t closure = fmg_blink_closure(clock);
    int32_t mul = 256 - closure; /* may exceed 256 during overshoot */
    out->lid_l_q8 = (fmg_u8_q8(kf->eye_left_open) * mul) >> 8;
    out->lid_r_q8 = (fmg_u8_q8(kf->eye_right_open) * mul) >> 8;

    /* saccades: ballistic hop at each fixation boundary + micro drift */
    uint32_t se = clock / FMG_SACCADE_PERIOD;
    int32_t phase = (int32_t)(clock % FMG_SACCADE_PERIOD);
    int32_t cx, cy, px, py;
    fmg_saccade_target(se, &cx, &cy);
    fmg_saccade_target(se == 0 ? 0 : se - 1, &px, &py);
    int32_t s = fmg_smooth_q8(
        phase >= FMG_SACCADE_MOVE ? 256 : phase * 256 / FMG_SACCADE_MOVE);
    int32_t gx = (px << 8) + (((cx - px) << 8) * s >> 8);
    int32_t gy = (py << 8) + (((cy - py) << 8) * s >> 8);
    gx += fmg_sin_q14(fmg_angle_of(clock, 9000)) * 44 >> 14;
    gy += fmg_sin_q14(fmg_angle_of(clock, 12700)) * 32 >> 14;
    /* defer to strong commanded gaze and steady speaking eye contact */
    int32_t look_mag = (kf->look_x < 0 ? -kf->look_x : kf->look_x) +
                       (kf->look_y < 0 ? -kf->look_y : kf->look_y);
    if (look_mag > 24 || speaking) {
        gx /= 2;
        gy /= 2;
    }
    out->gaze_dx_q8 = gx;
    out->gaze_dy_q8 = gy;

    /* brows: keyframe offset + slow wave + occasional raises + activity */
    int32_t brow = -(int32_t)kf->brow * 256 / 16; /* raised = negative px */
    brow = fmg_clampi(brow, -6 * 256, 6 * 256);
    brow += fmg_sin_q14(fmg_angle_of(clock, 131072)) * 300 >> 14;
    uint32_t be = clock / FMG_BROW_PERIOD;
    uint32_t bh = fmg_hash(be ^ 0xB20BB20BU);
    int32_t bl = brow;
    int32_t br = brow;
    if (bh % 4U == 0U && !speaking) {
        int32_t bp = (int32_t)(clock % FMG_BROW_PERIOD);
        int32_t lift = 0;
        if (bp > 20000 && bp < 52000) {
            int32_t in = bp - 20000;
            int32_t outp = 52000 - bp;
            int32_t edge = in < outp ? in : outp;
            lift = fmg_smooth_q8(edge > 6000 ? 256 : edge * 256 / 6000);
        }
        bl -= lift * 2;
        br -= (bh & 0x10000U) ? lift * 2 : lift; /* sometimes asymmetric */
    }
    if (kf->expression == FMG_ACTIVITY_LISTENING) {
        bl -= 420;
        br -= 420;
    } else if (kf->expression == FMG_ACTIVITY_THINKING) {
        bl -= 640;
        br += 220;
    }
    out->brow_l_q8 = bl;
    out->brow_r_q8 = br;

    out->breath_q8 = fmg_sin_q14(fmg_angle_of(clock, FMG_BREATH_PERIOD)) *
                     330 >> 14;
    out->sway_q8 = fmg_sin_q14(fmg_angle_of(clock, FMG_SWAY_PERIOD)) *
                   380 >> 14;
}

/* ---- mouth model + viseme classifier ---- */

typedef struct {
    uint8_t open, width, round, press, teeth;
} fmg_vis_anchor_t;

/*
 * Anchor poses per discrete viseme, matching the parameter conventions the
 * host viseme tracker uses when it flattens its 15-class output into the
 * 12-byte keyframe (see firmware face_viseme.c s_shapes).
 */
static const fmg_vis_anchor_t s_vis_anchors[FMG_VIS_COUNT] = {
    [FMG_VIS_REST] = {6, 110, 30, 0, 0},
    [FMG_VIS_AA] = {236, 205, 24, 0, 18},
    [FMG_VIS_EE] = {155, 246, 0, 0, 128},
    [FMG_VIS_IH] = {102, 255, 0, 0, 155},
    [FMG_VIS_OH] = {214, 112, 255, 0, 16},
    [FMG_VIS_UU] = {112, 82, 244, 0, 10},
    [FMG_VIS_MBP] = {12, 164, 18, 255, 0},
    [FMG_VIS_SS] = {70, 210, 0, 0, 220},
    [FMG_VIS_FV] = {38, 198, 0, 176, 235},
    [FMG_VIS_LN] = {80, 198, 6, 0, 145},
};

void fmg_mouth_compute(const fmg_keyframe_t *kf, fmg_mouth_t *out)
{
    out->open_q8 = fmg_u8_q8(kf->mouth_open);
    out->width_q8 = fmg_u8_q8(kf->mouth_width);
    out->round_q8 = fmg_u8_q8(kf->mouth_round);
    out->press_q8 = fmg_u8_q8(kf->mouth_press);
    out->teeth_q8 = fmg_u8_q8(kf->mouth_teeth);
    out->speaking = (kf->flags & FMG_FLAG_SPEAKING) != 0;

    /* JALI axes: bilabial pressure overrides jaw drop; lip axis is how
     * strongly the lips articulate versus hang slack. */
    out->jaw_q8 = (out->open_q8 * (256 - (out->press_q8 * 7 >> 3))) >> 8;
    int32_t lip = out->round_q8;
    if (out->press_q8 > lip) {
        lip = out->press_q8;
    }
    if (out->teeth_q8 / 2 > lip) {
        lip = out->teeth_q8 / 2;
    }
    out->lip_q8 = lip;

    if (!out->speaking && kf->mouth_open < 10 && kf->mouth_press < 20) {
        out->vis = FMG_VIS_REST;
        return;
    }
    int32_t best = INT32_MAX;
    fmg_vis_t best_vis = FMG_VIS_REST;
    for (int i = 0; i < FMG_VIS_COUNT; i++) {
        const fmg_vis_anchor_t *a = &s_vis_anchors[i];
        int32_t d = 0;
        int32_t t;
        t = (int32_t)kf->mouth_open - a->open;
        d += 5 * (t < 0 ? -t : t);
        t = (int32_t)kf->mouth_width - a->width;
        d += 3 * (t < 0 ? -t : t);
        t = (int32_t)kf->mouth_round - a->round;
        d += 5 * (t < 0 ? -t : t);
        t = (int32_t)kf->mouth_press - a->press;
        d += 7 * (t < 0 ? -t : t);
        t = (int32_t)kf->mouth_teeth - a->teeth;
        d += 4 * (t < 0 ? -t : t);
        if (d < best) {
            best = d;
            best_vis = (fmg_vis_t)i;
        }
    }
    out->vis = best_vis;
}

/* ---- primitives ---- */

void fmg_fill(uint16_t *px, uint16_t color)
{
    for (int i = 0; i < FMG_PIXEL_COUNT; i++) {
        px[i] = color;
    }
}

void fmg_pixel(uint16_t *px, int32_t x, int32_t y, uint16_t color)
{
    if (x >= 0 && x < FMG_WIDTH && y >= 0 && y < FMG_HEIGHT) {
        px[y * FMG_WIDTH + x] = color;
    }
}

void fmg_pixel_blend(
    uint16_t *px, int32_t x, int32_t y, uint16_t color, int32_t alpha_q8)
{
    if (x >= 0 && x < FMG_WIDTH && y >= 0 && y < FMG_HEIGHT) {
        uint16_t *p = &px[y * FMG_WIDTH + x];
        *p = fmg_blend565(*p, color, alpha_q8);
    }
}

void fmg_hline(
    uint16_t *px, int32_t x0, int32_t x1, int32_t y, uint16_t color)
{
    if (y < 0 || y >= FMG_HEIGHT) {
        return;
    }
    x0 = fmg_clampi(x0, 0, FMG_WIDTH - 1);
    x1 = fmg_clampi(x1, 0, FMG_WIDTH - 1);
    uint16_t *row = &px[y * FMG_WIDTH];
    for (int32_t x = x0; x <= x1; x++) {
        row[x] = color;
    }
}

void fmg_hline_blend(
    uint16_t *px, int32_t x0, int32_t x1, int32_t y, uint16_t color,
    int32_t alpha_q8)
{
    if (y < 0 || y >= FMG_HEIGHT) {
        return;
    }
    x0 = fmg_clampi(x0, 0, FMG_WIDTH - 1);
    x1 = fmg_clampi(x1, 0, FMG_WIDTH - 1);
    uint16_t *row = &px[y * FMG_WIDTH];
    for (int32_t x = x0; x <= x1; x++) {
        row[x] = fmg_blend565(row[x], color, alpha_q8);
    }
}

void fmg_vspan(
    uint16_t *px, int32_t x, int32_t y0, int32_t y1, uint16_t color)
{
    if (x < 0 || x >= FMG_WIDTH) {
        return;
    }
    y0 = fmg_clampi(y0, 0, FMG_HEIGHT - 1);
    y1 = fmg_clampi(y1, 0, FMG_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; y++) {
        px[y * FMG_WIDTH + x] = color;
    }
}

void fmg_vspan_blend(
    uint16_t *px, int32_t x, int32_t y0, int32_t y1, uint16_t color,
    int32_t alpha_q8)
{
    if (x < 0 || x >= FMG_WIDTH) {
        return;
    }
    y0 = fmg_clampi(y0, 0, FMG_HEIGHT - 1);
    y1 = fmg_clampi(y1, 0, FMG_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; y++) {
        uint16_t *p = &px[y * FMG_WIDTH + x];
        *p = fmg_blend565(*p, color, alpha_q8);
    }
}

void fmg_fill_rect(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h,
    uint16_t color)
{
    for (int32_t r = y; r < y + h; r++) {
        fmg_hline(px, x, x + w - 1, r, color);
    }
}

void fmg_fill_rect_blend(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h,
    uint16_t color, int32_t alpha_q8)
{
    for (int32_t r = y; r < y + h; r++) {
        fmg_hline_blend(px, x, x + w - 1, r, color, alpha_q8);
    }
}

void fmg_fill_ellipse(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (int32_t dy = -ry; dy <= ry; dy++) {
        int32_t half =
            (int32_t)(rx * fmg_isqrt((uint32_t)(ry * ry - dy * dy))) / ry;
        fmg_hline(px, cx - half, cx + half, cy + dy, color);
    }
}

void fmg_fill_ellipse_blend(
    uint16_t *px, int32_t cx, int32_t cy, int32_t rx, int32_t ry,
    uint16_t color, int32_t alpha_q8)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    for (int32_t dy = -ry; dy <= ry; dy++) {
        int32_t half =
            (int32_t)(rx * fmg_isqrt((uint32_t)(ry * ry - dy * dy))) / ry;
        fmg_hline_blend(px, cx - half, cx + half, cy + dy, color, alpha_q8);
    }
}

void fmg_fill_round_rect(
    uint16_t *px, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
    uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int32_t rmax = (w < h ? w : h) / 2;
    if (r > rmax) {
        r = rmax;
    }
    for (int32_t i = 0; i < h; i++) {
        int32_t dy = -1;
        if (i < r) {
            dy = r - i;
        } else if (i >= h - r) {
            dy = i - (h - r) + 1;
        }
        int32_t inset = 0;
        if (dy > 0) {
            inset = r - (int32_t)fmg_isqrt((uint32_t)(r * r - dy * dy));
        }
        fmg_hline(px, x + inset, x + w - 1 - inset, y + i, color);
    }
}

void fmg_line(
    uint16_t *px, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint16_t color)
{
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t sx = dx < 0 ? -1 : 1;
    int32_t sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int32_t err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        fmg_pixel(px, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int32_t e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

void fmg_poly_fill(
    uint16_t *px, const int32_t *xy_q4, int count, uint16_t color)
{
    if (count < 3 || count > FMG_POLY_MAX) {
        return;
    }
    int32_t miny = xy_q4[1];
    int32_t maxy = xy_q4[1];
    for (int i = 1; i < count; i++) {
        int32_t y = xy_q4[i * 2 + 1];
        if (y < miny) {
            miny = y;
        }
        if (y > maxy) {
            maxy = y;
        }
    }
    int32_t y0 = fmg_clampi(miny >> 4, 0, FMG_HEIGHT - 1);
    int32_t y1 = fmg_clampi((maxy >> 4) + 1, 0, FMG_HEIGHT - 1);
    for (int32_t y = y0; y <= y1; y++) {
        int32_t yc = (y << 4) + 8;
        int32_t xs[FMG_POLY_MAX];
        int n = 0;
        for (int i = 0; i < count; i++) {
            int j = (i + 1) % count;
            int32_t ay = xy_q4[i * 2 + 1];
            int32_t by = xy_q4[j * 2 + 1];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) {
                int32_t ax = xy_q4[i * 2];
                int32_t bx = xy_q4[j * 2];
                xs[n++] = ax + (int32_t)((int64_t)(bx - ax) * (yc - ay) /
                                         (by - ay));
            }
        }
        for (int i = 1; i < n; i++) {
            int32_t v = xs[i];
            int k = i - 1;
            while (k >= 0 && xs[k] > v) {
                xs[k + 1] = xs[k];
                k--;
            }
            xs[k + 1] = v;
        }
        for (int i = 0; i + 1 < n; i += 2) {
            int32_t xa = (xs[i] + 7) >> 4;
            int32_t xb = ((xs[i + 1] + 7) >> 4) - 1;
            if (xb >= xa) {
                fmg_hline(px, xa, xb, y, color);
            }
        }
    }
}

int32_t fmg_qbez_q4(int32_t p0, int32_t p1, int32_t p2, int32_t t_q8)
{
    int32_t u = 256 - t_q8;
    return (int32_t)(((int64_t)u * u * p0 + 2LL * u * t_q8 * p1 +
                      (int64_t)t_q8 * t_q8 * p2) >>
                     16);
}

/* ---- ASCII sprite blitting ---- */

void fmg_sprite_blit(
    uint16_t *px, const fmg_sprite_t *sprite, int32_t cx, int32_t cy,
    int32_t scale, const fmg_sprite_pal_t *pal, int pal_count)
{
    if (scale < 1) {
        scale = 1;
    }
    int32_t ox = cx - sprite->w * scale / 2;
    int32_t oy = cy - sprite->h * scale / 2;
    for (int32_t r = 0; r < sprite->h; r++) {
        const char *row = sprite->rows[r];
        for (int32_t c = 0; c < sprite->w && row[c] != '\0'; c++) {
            char ch = row[c];
            if (ch == '.' || ch == ' ') {
                continue;
            }
            for (int p = 0; p < pal_count; p++) {
                if (pal[p].ch == ch) {
                    fmg_fill_rect(
                        px, ox + c * scale, oy + r * scale, scale, scale,
                        pal[p].color);
                    break;
                }
            }
        }
    }
}
