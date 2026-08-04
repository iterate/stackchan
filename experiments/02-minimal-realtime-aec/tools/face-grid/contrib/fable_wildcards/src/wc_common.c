#include "wc_common.h"

/* Quarter-wave sine, Q14, sin(pi/2 * i / 256) for i in 0..256. */
static const int16_t WC_SIN_TAB[257] = {
    0, 101, 201, 302, 402, 503, 603, 704, 804, 904, 1005, 1105,
    1205, 1306, 1406, 1506, 1606, 1706, 1806, 1906, 2006, 2105, 2205, 2305,
    2404, 2503, 2603, 2702, 2801, 2900, 2999, 3098, 3196, 3295, 3393, 3492,
    3590, 3688, 3786, 3883, 3981, 4078, 4176, 4273, 4370, 4467, 4563, 4660,
    4756, 4852, 4948, 5044, 5139, 5235, 5330, 5425, 5520, 5614, 5708, 5803,
    5897, 5990, 6084, 6177, 6270, 6363, 6455, 6547, 6639, 6731, 6823, 6914,
    7005, 7096, 7186, 7276, 7366, 7456, 7545, 7635, 7723, 7812, 7900, 7988,
    8076, 8163, 8250, 8337, 8423, 8509, 8595, 8680, 8765, 8850, 8935, 9019,
    9102, 9186, 9269, 9352, 9434, 9516, 9598, 9679, 9760, 9841, 9921, 10001,
    10080, 10159, 10238, 10316, 10394, 10471, 10549, 10625, 10702, 10778,
    10853, 10928, 11003, 11077, 11151, 11224, 11297, 11370, 11442, 11514,
    11585, 11656, 11727, 11797, 11866, 11935, 12004, 12072, 12140, 12207,
    12274, 12340, 12406, 12472, 12537, 12601, 12665, 12729, 12792, 12854,
    12916, 12978, 13039, 13100, 13160, 13219, 13279, 13337, 13395, 13453,
    13510, 13567, 13623, 13678, 13733, 13788, 13842, 13896, 13949, 14001,
    14053, 14104, 14155, 14206, 14256, 14305, 14354, 14402, 14449, 14497,
    14543, 14589, 14635, 14680, 14724, 14768, 14811, 14854, 14896, 14937,
    14978, 15019, 15059, 15098, 15137, 15175, 15213, 15250, 15286, 15322,
    15357, 15392, 15426, 15460, 15493, 15525, 15557, 15588, 15619, 15649,
    15679, 15707, 15736, 15763, 15791, 15817, 15843, 15868, 15893, 15917,
    15941, 15964, 15986, 16008, 16029, 16049, 16069, 16088, 16107, 16125,
    16143, 16160, 16176, 16192, 16207, 16221, 16235, 16248, 16261, 16273,
    16284, 16295, 16305, 16315, 16324, 16332, 16340, 16347, 16353, 16359,
    16364, 16369, 16373, 16376, 16379, 16381, 16383, 16384, 16384,
};

int32_t wc_sin_q14(uint32_t phase) {
    uint32_t p = phase & 0xFFFFu;
    uint32_t quad = p >> 14;
    uint32_t rem = p & 0x3FFFu;
    if (quad & 1u) {
        rem = 0x4000u - rem;
    }
    uint32_t i = rem >> 6;
    uint32_t f = rem & 63u;
    int32_t v = WC_SIN_TAB[i];
    if (f) {
        v += ((WC_SIN_TAB[i + 1] - v) * (int32_t)f) >> 6;
    }
    return (quad >= 2u) ? -v : v;
}

int32_t wc_cos_q14(uint32_t phase) {
    return wc_sin_q14(phase + 0x4000u);
}

uint32_t wc_isqrt32(uint32_t v) {
    uint32_t r = 0;
    uint32_t bit = 1u << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

uint32_t wc_atan2_u16(int32_t y, int32_t x) {
    if (x == 0 && y == 0) {
        return 0;
    }
    int32_t ax = wc_abs32(x);
    int32_t ay = wc_abs32(y);
    int32_t swap = ay > ax;
    int32_t num = swap ? ax : ay;
    int32_t den = swap ? ay : ax;
    /* t in [0,1] as Q10; atan(t) ~ (pi/4)t + 0.273 t (1-t), in turn units */
    int32_t t = (num << 10) / den;
    int32_t a = (8192 * t >> 10) + (int32_t)(((int64_t)2847 * t * (1024 - t)) >> 20);
    if (swap) {
        a = 16384 - a;
    }
    if (x < 0) {
        a = 32768 - a;
    }
    if (y < 0) {
        a = 65536 - a;
    }
    return (uint32_t)a & 0xFFFFu;
}

int32_t wc_noise_q14(uint32_t t, uint32_t rate, uint32_t salt) {
    uint32_t k = t / rate;
    uint32_t f = t % rate;
    int32_t h0 = (int32_t)(wc_hash2(k, salt) & 0x3FFFu);
    int32_t h1 = (int32_t)(wc_hash2(k + 1u, salt) & 0x3FFFu);
    return h0 + (int32_t)(((int64_t)(h1 - h0) * (int32_t)f) / (int32_t)rate);
}

uint16_t wc_mix565(uint16_t b, uint16_t a, uint32_t alpha_q8) {
    if (alpha_q8 >= 256u) {
        return a;
    }
    if (alpha_q8 == 0u) {
        return b;
    }
    uint32_t br = (b >> 11) & 31u, bg = (b >> 5) & 63u, bb = b & 31u;
    uint32_t ar = (a >> 11) & 31u, ag = (a >> 5) & 63u, ab = a & 31u;
    uint32_t r = br + (((ar - br) * alpha_q8) >> 8);
    uint32_t g = bg + (((ag - bg) * alpha_q8) >> 8);
    uint32_t bl = bb + (((ab - bb) * alpha_q8) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* ---------------------------------------------------------------- idle rig */

enum {
    WC_SALT_BLINK = 0xB11Fu,
    WC_SALT_DBL = 0xD0B1u,
    WC_SALT_SAC = 0x5ACCu,
    WC_SALT_TX = 0x71A2u,
    WC_SALT_TY = 0x71B3u,
    WC_SALT_FLICK = 0xF11Cu,

    WC_BLINK_PERIOD = 54000,   /* 3.375 s grid, jitter +-0.875 s */
    WC_BLINK_CLOSE = 560,      /* 35 ms snap shut */
    WC_BLINK_HOLD_END = 1200,
    WC_BLINK_OPEN_END = 2800,  /* 100 ms relaxed open */
    WC_DBL_OFFSET = 3600,

    WC_SAC_PERIOD = 20000,     /* 1.25 s hold grid */
    WC_SAC_TRANS = 900,        /* 56 ms flick */
    WC_HEAD_LAG = 1400,        /* 87 ms follow-through delay */
};

/* Closure profile of one blink: 0..255 for offsets inside the episode. */
static int32_t wc_blink_profile(int32_t o) {
    if (o < 0 || o >= WC_BLINK_OPEN_END) {
        return 0;
    }
    if (o < WC_BLINK_CLOSE) {
        return (o * 255) / WC_BLINK_CLOSE;
    }
    if (o < WC_BLINK_HOLD_END) {
        return 255;
    }
    return ((WC_BLINK_OPEN_END - o) * 255) / (WC_BLINK_OPEN_END - WC_BLINK_HOLD_END);
}

/* Total idle closure at time t: consider this episode and the previous. */
static int32_t wc_blink_closure_at(uint32_t t) {
    uint32_t i = t / WC_BLINK_PERIOD;
    int32_t best = 0;
    for (uint32_t k = 0; k < 2u; ++k) {
        uint32_t idx = i - k;
        if (idx > i) {
            break; /* underflow before the first episode */
        }
        uint32_t jitter = wc_hash2(idx, WC_SALT_BLINK) % 28000u;
        uint32_t start = idx * WC_BLINK_PERIOD + 6000u + jitter;
        int32_t o = (int32_t)(t - start);
        best = wc_max32(best, wc_blink_profile(o));
        if (wc_hash2(idx, WC_SALT_DBL) % 8u == 0u) {
            best = wc_max32(best, wc_blink_profile(o - WC_DBL_OFFSET));
        }
    }
    return best;
}

/* Idle saccade target for hold index j, scaled down while speaking. */
static void wc_sac_target(uint32_t j, int32_t speaking, int32_t *tx, int32_t *ty) {
    if ((int32_t)j < 0) {
        *tx = 0;
        *ty = 0;
        return;
    }
    uint32_t rx = wc_hash2(j, WC_SALT_TX);
    uint32_t ry = wc_hash2(j, WC_SALT_TY);
    int32_t big = (rx % 11u) == 0u;
    int32_t mag = big ? 150 : 56;
    int32_t x = (((int32_t)((rx >> 8) & 0x1FFu) - 256) * mag) >> 8;
    int32_t y = (((int32_t)((ry >> 8) & 0x1FFu) - 256) * (mag / 2 + 10)) >> 8;
    if (speaking) {
        x >>= 1;
        y >>= 1;
    }
    *tx = x;
    *ty = y;
}

typedef struct {
    int32_t gx, gy;      /* idle gaze */
    int32_t jump;        /* size of the current/most recent flick */
    int32_t since_start; /* samples since the flick began (large if settled) */
    int32_t upcoming;    /* size of an imminent flick, 0 otherwise */
    int32_t pre;         /* samples until that flick */
} wc_sac_state_t;

static void wc_sac_eval(uint32_t t, int32_t speaking, wc_sac_state_t *s) {
    uint32_t j = t / WC_SAC_PERIOD;
    uint32_t start = j * WC_SAC_PERIOD + wc_hash2(j, WC_SALT_SAC) % 8000u;
    int32_t px, py, tx, ty;
    if (t < start) {
        /* still holding the previous target; the flick is imminent */
        wc_sac_target(j - 1u, speaking, &tx, &ty);
        wc_sac_target(j >= 2u ? j - 2u : 0u, speaking, &px, &py);
        s->gx = tx;
        s->gy = ty;
        s->since_start = WC_SAC_PERIOD;
        s->jump = 0;
        int32_t nx, ny;
        wc_sac_target(j, speaking, &nx, &ny);
        s->upcoming = wc_abs32(nx - tx) + wc_abs32(ny - ty);
        s->pre = (int32_t)(start - t);
        return;
    }
    wc_sac_target(j, speaking, &tx, &ty);
    wc_sac_target(j >= 1u ? j - 1u : 0u, speaking, &px, &py);
    int32_t o = (int32_t)(t - start);
    s->since_start = o;
    s->jump = wc_abs32(tx - px) + wc_abs32(ty - py);
    s->upcoming = 0;
    s->pre = 0;
    if (o >= WC_SAC_TRANS) {
        s->gx = tx;
        s->gy = ty;
        return;
    }
    int32_t u = (o << 10) / WC_SAC_TRANS;                /* Q10 */
    int32_t e = (u * u * (3072 - 2 * u)) >> 20;          /* smoothstep Q10 */
    int32_t os = ((u * u >> 10) * (1024 - u)) >> 10;     /* overshoot bell */
    e += (os * 96) >> 10;
    s->gx = px + ((tx - px) * e >> 10);
    s->gy = py + ((ty - py) * e >> 10);
}

void wc_rig_derive(const wc_keyframe_t *kf, uint32_t clock, wc_rig_t *rig) {
    rig->mouth_open = kf->mouth_open;
    rig->mouth_width = kf->mouth_width;
    rig->mouth_round = kf->mouth_round;
    rig->mouth_press = kf->mouth_press;
    rig->mouth_teeth = kf->mouth_teeth;
    rig->speaking = (kf->flags & WC_KEYFRAME_FLAG_SPEAKING) ? 1u : 0u;

    int32_t energy = kf->mouth_open;
    if (rig->speaking) {
        energy = wc_max32(energy, 48);
    }
    energy = wc_min32(255, energy + kf->mouth_press / 4);
    rig->energy = (uint8_t)energy;

    /* breathing: 4 s period, halved while speaking */
    uint32_t bc = clock % 64000u;
    int32_t breath = wc_sin_q14(bc * 1049u >> 10);
    if (rig->speaking) {
        breath >>= 1;
    }
    rig->breath_q14 = (int16_t)breath;

    /* blinks; right eye trails the left by ~19 ms */
    int32_t closure_l = wc_blink_closure_at(clock);
    int32_t closure_r = wc_blink_closure_at(clock > 300u ? clock - 300u : 0u);
    if (kf->flags & WC_KEYFRAME_FLAG_BLINKING) {
        closure_l = 255;
        closure_r = 255;
    }
    rig->blink_closure = (uint8_t)wc_max32(closure_l, closure_r);

    /* saccades + anticipation + head follow-through */
    wc_sac_state_t now, lag;
    wc_sac_eval(clock, (int32_t)rig->speaking, &now);
    wc_sac_eval(clock > WC_HEAD_LAG ? clock - WC_HEAD_LAG : 0u,
                (int32_t)rig->speaking, &lag);

    int32_t widen = 0;
    if (now.upcoming > 90 && now.pre < 1200) {
        widen = 40; /* eyes open a touch before a large flick */
    }

    int32_t lid_l = wc_min32(kf->eye_left_open, 255 - closure_l);
    int32_t lid_r = wc_min32(kf->eye_right_open, 255 - closure_r);
    rig->lid_l = (uint8_t)wc_clamp32(lid_l + widen, 0, 255);
    rig->lid_r = (uint8_t)wc_clamp32(lid_r + widen, 0, 255);

    int32_t gx = kf->look_x * 2 + now.gx;
    int32_t gy = kf->look_y * 2 + now.gy;
    rig->gaze_x = (int16_t)wc_clamp32(gx, -256, 256);
    rig->gaze_y = (int16_t)wc_clamp32(gy, -256, 256);

    /* head trails gaze at 1/3 amplitude, with a damped wobble after flicks */
    int32_t hx = (lag.gx << 8) / 3 / 32;   /* gaze ~[-256,256] -> px Q8 */
    int32_t hy = (lag.gy << 8) / 3 / 40;
    if (now.jump > 60 && now.since_start < 6000) {
        int32_t decay = ((6000 - now.since_start) << 10) / 6000;
        int32_t w = wc_sin_q14((uint32_t)(now.since_start * 20u));
        hx += ((now.jump * w >> 14) * decay >> 10) << 2;
    }
    hy += (breath * 3) >> 7;
    rig->head_x_q8 = (int16_t)wc_clamp32(hx, -1536, 1536);
    rig->head_y_q8 = (int16_t)wc_clamp32(hy, -1536, 1536);

    int32_t brow = kf->brow * 3;
    brow += breath >> 9;
    brow += widen * 2;
    brow -= wc_max32(closure_l, closure_r) >> 2;
    rig->brow_q8 = (int16_t)wc_clamp32(brow, -512, 512);

    int32_t flick = (wc_noise_q14(clock, 1024u, WC_SALT_FLICK) * 3 +
                     wc_noise_q14(clock, 256u, WC_SALT_FLICK + 1u)) >> 2;
    rig->flick_q14 = (uint16_t)wc_clamp32(flick, 0, 16384);
    rig->reserved = 0;
}
