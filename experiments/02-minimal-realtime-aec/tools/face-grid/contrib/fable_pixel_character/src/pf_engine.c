#include "pf_internal.h"

/*
 * The art surface aliases the caller's RGB565 frame. An art buffer is at
 * most 160×120 bytes while the frame is 160×120×2 bytes, and pf_present()
 * walks the frame back-to-front: for screen pixel (x, y) the art byte read
 * offset (y/sy)*aw + x/sx never exceeds the destination byte offset
 * 2*(y*W + x), so every read happens before its memory is overwritten.
 */

pf_surface_t pf_surface_attach(uint16_t *fb, int art_w, int art_h) {
    pf_surface_t s;
    s.px = (uint8_t *)fb;
    s.w = art_w;
    s.h = art_h;
    return s;
}

void pf_present(uint16_t *fb, const pf_surface_t *art, int scale_x,
                int scale_y, const uint16_t *palette, unsigned fx) {
    for (int y = PIXEL_FACE_HEIGHT - 1; y >= 0; --y) {
        const uint8_t *row = art->px + (size_t)(y / scale_y) * (size_t)art->w;
        uint16_t *dst = fb + (size_t)y * PIXEL_FACE_WIDTH;
        int dim = (fx & PF_FX_SCANLINE) && (y & 1);
        for (int x = PIXEL_FACE_WIDTH - 1; x >= 0; --x) {
            uint16_t c = palette[row[x / scale_x]];
            if (dim) {
                c = (uint16_t)(c - ((c >> 2) & 0x39E7u));
            }
            dst[x] = c;
        }
    }
}

/* ---- primitives -------------------------------------------------------- */

void pf_clear(pf_surface_t *s, uint8_t c) {
    int n = s->w * s->h;
    for (int i = 0; i < n; ++i) {
        s->px[i] = c;
    }
}

void pf_px(pf_surface_t *s, int x, int y, uint8_t c) {
    if (x >= 0 && x < s->w && y >= 0 && y < s->h) {
        s->px[(size_t)y * (size_t)s->w + (size_t)x] = c;
    }
}

uint8_t pf_get(const pf_surface_t *s, int x, int y) {
    if (x >= 0 && x < s->w && y >= 0 && y < s->h) {
        return s->px[(size_t)y * (size_t)s->w + (size_t)x];
    }
    return 0;
}

void pf_hline(pf_surface_t *s, int x0, int x1, int y, uint8_t c) {
    if (y < 0 || y >= s->h) {
        return;
    }
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    x0 = pf_maxi(x0, 0);
    x1 = pf_mini(x1, s->w - 1);
    uint8_t *row = s->px + (size_t)y * (size_t)s->w;
    for (int x = x0; x <= x1; ++x) {
        row[x] = c;
    }
}

void pf_vline(pf_surface_t *s, int x, int y0, int y1, uint8_t c) {
    if (x < 0 || x >= s->w) {
        return;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    y0 = pf_maxi(y0, 0);
    y1 = pf_mini(y1, s->h - 1);
    for (int y = y0; y <= y1; ++y) {
        s->px[(size_t)y * (size_t)s->w + (size_t)x] = c;
    }
}

void pf_fill_rect(pf_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    for (int i = 0; i < h; ++i) {
        pf_hline(s, x, x + w - 1, y + i, c);
    }
}

void pf_rect(pf_surface_t *s, int x, int y, int w, int h, uint8_t c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    pf_hline(s, x, x + w - 1, y, c);
    pf_hline(s, x, x + w - 1, y + h - 1, c);
    pf_vline(s, x, y, y + h - 1, c);
    pf_vline(s, x + w - 1, y, y + h - 1, c);
}

void pf_line(pf_surface_t *s, int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = pf_absi(x1 - x0);
    int dy = -pf_absi(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pf_px(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void pf_fill_ellipse(pf_surface_t *s, int cx, int cy, int rx, int ry,
                     uint8_t c) {
    if (rx <= 0 || ry <= 0) {
        pf_px(s, cx, cy, c);
        return;
    }
    /* Radii stay small (< 180), so ry²·rx² fits comfortably in uint32. */
    uint32_t ry2 = (uint32_t)(ry * ry);
    uint32_t rx2 = (uint32_t)(rx * rx);
    for (int dy = -ry; dy <= ry; ++dy) {
        uint32_t t = ry2 - (uint32_t)(dy * dy);
        int dx = (int)pf_isqrt(t * rx2 / ry2);
        pf_hline(s, cx - dx, cx + dx, cy + dy, c);
    }
}

void pf_fill_circle(pf_surface_t *s, int cx, int cy, int r, uint8_t c) {
    pf_fill_ellipse(s, cx, cy, r, r, c);
}

/* ---- ordered dithering ------------------------------------------------- */

static const uint8_t pf_bayer8[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44, 4, 36, 14, 46, 6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    { 3, 35, 11, 43, 1, 33, 9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47, 7, 39, 13, 45, 5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};

int pf_dither_pick(pf_pattern_t pat, int level, int x, int y) {
    level = pf_clampi(level, 0, 64);
    switch (pat) {
    case PF_PAT_CHECKER: {
        /* 2×2 checkerboard quantised to five levels. */
        static const uint8_t checker[2][2] = { { 0, 32 }, { 48, 16 } };
        return level > (int)checker[y & 1][x & 1];
    }
    case PF_PAT_DIAG: {
        /* Diagonal hatch: level picks how many of 4 diagonals are lit. */
        int phase = (x + y) & 3;
        static const uint8_t diag[4] = { 0, 32, 16, 48 };
        return level > (int)diag[phase];
    }
    case PF_PAT_BAYER8:
    default:
        return level > (int)pf_bayer8[y & 7][x & 7];
    }
}

void pf_fill_rect_dither(pf_surface_t *s, int x, int y, int w, int h,
                         uint8_t c0, uint8_t c1, pf_pattern_t pat,
                         int level) {
    int x1 = pf_mini(x + w - 1, s->w - 1);
    int y1 = pf_mini(y + h - 1, s->h - 1);
    for (int yy = pf_maxi(y, 0); yy <= y1; ++yy) {
        uint8_t *row = s->px + (size_t)yy * (size_t)s->w;
        for (int xx = pf_maxi(x, 0); xx <= x1; ++xx) {
            row[xx] = pf_dither_pick(pat, level, xx, yy) ? c1 : c0;
        }
    }
}

void pf_fill_ellipse_dither(pf_surface_t *s, int cx, int cy, int rx, int ry,
                            uint8_t c0, uint8_t c1, pf_pattern_t pat,
                            int level) {
    if (rx <= 0 || ry <= 0) {
        return;
    }
    uint32_t ry2 = (uint32_t)(ry * ry);
    uint32_t rx2 = (uint32_t)(rx * rx);
    for (int dy = -ry; dy <= ry; ++dy) {
        uint32_t t = ry2 - (uint32_t)(dy * dy);
        int dx = (int)pf_isqrt(t * rx2 / ry2);
        int y = cy + dy;
        if (y < 0 || y >= s->h) {
            continue;
        }
        int x0 = pf_maxi(cx - dx, 0);
        int x1 = pf_mini(cx + dx, s->w - 1);
        uint8_t *row = s->px + (size_t)y * (size_t)s->w;
        for (int x = x0; x <= x1; ++x) {
            row[x] = pf_dither_pick(pat, level, x, y) ? c1 : c0;
        }
    }
}

/* ---- 1-bit conversions ------------------------------------------------- */

void pf_dither_bayer_1bit(pf_surface_t *gray) {
    for (int y = 0; y < gray->h; ++y) {
        uint8_t *row = gray->px + (size_t)y * (size_t)gray->w;
        for (int x = 0; x < gray->w; ++x) {
            int threshold = ((int)pf_bayer8[y & 7][x & 7] << 2) + 2;
            row[x] = row[x] > threshold ? 1 : 0;
        }
    }
}

void pf_dither_atkinson_1bit(pf_surface_t *gray) {
    /*
     * Bill Atkinson's variant: diffuse 6/8 of the quantisation error to six
     * neighbours, discard the rest. Serial but deterministic; errors are
     * accumulated in place with saturation.
     */
    int w = gray->w;
    int h = gray->h;
    for (int y = 0; y < h; ++y) {
        uint8_t *row = gray->px + (size_t)y * (size_t)w;
        uint8_t *row1 = (y + 1 < h) ? row + w : 0;
        uint8_t *row2 = (y + 2 < h) ? row + 2 * w : 0;
        for (int x = 0; x < w; ++x) {
            int old = row[x];
            int bit = old > 127 ? 1 : 0;
            int err = old - (bit ? 255 : 0);
            int e8 = err / 8;
            row[x] = (uint8_t)bit;
            if (x + 1 < w) {
                row[x + 1] = (uint8_t)pf_clampi(row[x + 1] + e8, 0, 255);
            }
            if (x + 2 < w) {
                row[x + 2] = (uint8_t)pf_clampi(row[x + 2] + e8, 0, 255);
            }
            if (row1) {
                if (x > 0) {
                    row1[x - 1] =
                        (uint8_t)pf_clampi(row1[x - 1] + e8, 0, 255);
                }
                row1[x] = (uint8_t)pf_clampi(row1[x] + e8, 0, 255);
                if (x + 1 < w) {
                    row1[x + 1] =
                        (uint8_t)pf_clampi(row1[x + 1] + e8, 0, 255);
                }
            }
            if (row2) {
                row2[x] = (uint8_t)pf_clampi(row2[x] + e8, 0, 255);
            }
        }
    }
}

/* ---- string-art sprites ------------------------------------------------ */

void pf_blit(pf_surface_t *s, int ox, int oy, const char *const *rows,
             int nrows, const char *chars, const uint8_t *colours) {
    for (int ry = 0; ry < nrows; ++ry) {
        const char *row = rows[ry];
        for (int rx = 0; row[rx] != '\0'; ++rx) {
            char ch = row[rx];
            if (ch == '.' || ch == ' ') {
                continue;
            }
            for (int i = 0; chars[i] != '\0'; ++i) {
                if (chars[i] == ch) {
                    pf_px(s, ox + rx, oy + ry, colours[i]);
                    break;
                }
            }
        }
    }
}

/* ---- 3×5 micro font ---------------------------------------------------- */

typedef struct {
    char ch;
    uint8_t rows[5]; /* 3 low bits per row, MSB-left */
} pf_glyph3x5_t;

static const pf_glyph3x5_t pf_font3x5[] = {
    { 'A', { 002, 005, 007, 005, 005 } },
    { 'B', { 006, 005, 006, 005, 006 } },
    { 'C', { 003, 004, 004, 004, 003 } },
    { 'D', { 006, 005, 005, 005, 006 } },
    { 'E', { 007, 004, 006, 004, 007 } },
    { 'F', { 007, 004, 006, 004, 004 } },
    { 'G', { 003, 004, 005, 005, 003 } },
    { 'H', { 005, 005, 007, 005, 005 } },
    { 'I', { 007, 002, 002, 002, 007 } },
    { 'J', { 001, 001, 001, 005, 002 } },
    { 'K', { 005, 005, 006, 005, 005 } },
    { 'L', { 004, 004, 004, 004, 007 } },
    { 'M', { 007, 007, 005, 005, 005 } },
    { 'N', { 005, 007, 007, 005, 005 } },
    { 'O', { 002, 005, 005, 005, 002 } },
    { 'P', { 006, 005, 006, 004, 004 } },
    { 'Q', { 002, 005, 005, 002, 001 } },
    { 'R', { 006, 005, 006, 005, 005 } },
    { 'S', { 003, 004, 002, 001, 006 } },
    { 'T', { 007, 002, 002, 002, 002 } },
    { 'U', { 005, 005, 005, 005, 007 } },
    { 'V', { 005, 005, 005, 002, 002 } },
    { 'W', { 005, 005, 005, 007, 007 } },
    { 'X', { 005, 005, 002, 005, 005 } },
    { 'Y', { 005, 005, 002, 002, 002 } },
    { 'Z', { 007, 001, 002, 004, 007 } },
    { '0', { 002, 005, 005, 005, 002 } },
    { '1', { 002, 006, 002, 002, 007 } },
    { '2', { 006, 001, 002, 004, 007 } },
    { '3', { 007, 001, 002, 001, 006 } },
    { '4', { 005, 005, 007, 001, 001 } },
    { '5', { 007, 004, 006, 001, 006 } },
    { '6', { 003, 004, 006, 005, 002 } },
    { '7', { 007, 001, 002, 002, 002 } },
    { '8', { 002, 005, 002, 005, 002 } },
    { '9', { 002, 005, 003, 001, 006 } },
    { '.', { 000, 000, 000, 000, 002 } },
    { ',', { 000, 000, 000, 002, 004 } },
    { ':', { 000, 002, 000, 002, 000 } },
    { '-', { 000, 000, 007, 000, 000 } },
    { '!', { 002, 002, 002, 000, 002 } },
    { '?', { 006, 001, 002, 000, 002 } },
    { '\'', { 002, 002, 000, 000, 000 } },
    { '>', { 004, 002, 001, 002, 004 } },
    { '<', { 001, 002, 004, 002, 001 } },
    { '/', { 001, 001, 002, 004, 004 } },
    { '+', { 000, 002, 007, 002, 000 } },
    { '=', { 000, 007, 000, 007, 000 } },
    { '*', { 005, 002, 007, 002, 005 } },
};

enum { PF_FONT3X5_COUNT = (int)(sizeof(pf_font3x5) / sizeof(pf_font3x5[0])) };

static const pf_glyph3x5_t *pf_font3x5_find(char ch) {
    for (int i = 0; i < PF_FONT3X5_COUNT; ++i) {
        if (pf_font3x5[i].ch == ch) {
            return &pf_font3x5[i];
        }
    }
    return 0;
}

void pf_text3x5(pf_surface_t *s, int x, int y, const char *str, uint8_t c) {
    for (int i = 0; str[i] != '\0'; ++i) {
        const pf_glyph3x5_t *g = pf_font3x5_find(str[i]);
        if (g) {
            for (int gy = 0; gy < 5; ++gy) {
                uint8_t bits = g->rows[gy];
                for (int gx = 0; gx < 3; ++gx) {
                    if (bits & (uint8_t)(004 >> gx)) {
                        pf_px(s, x + gx, y + gy, c);
                    }
                }
            }
        }
        x += 4;
    }
}

int pf_text3x5_width(const char *str) {
    int n = 0;
    while (str[n] != '\0') {
        ++n;
    }
    return n > 0 ? n * 4 - 1 : 0;
}

/* ---- deterministic integer math ---------------------------------------- */

uint32_t pf_hash32(uint32_t x) {
    /* lowbias32 by Chris Wellons (public domain). */
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static const int8_t pf_sin_table[256] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
    90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127,
    127, 127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121,
    120, 118, 117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98,
    96, 94, 92, 90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54,
    51, 49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85,
    -88, -90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111,
    -112, -113, -115, -116, -117, -118, -120, -121, -122, -122, -123, -124,
    -125, -125, -126, -126, -126, -127, -127, -127, -127, -127, -127, -127,
    -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,
    -98, -96, -94, -92, -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65,
    -63, -60, -57, -54, -51, -49, -46, -43, -40, -37, -34, -31, -28, -25, -22,
    -19, -16, -12, -9, -6, -3,
};

int pf_sin8(uint8_t phase) { return pf_sin_table[phase]; }

int32_t pf_isqrt(uint32_t v) {
    uint32_t res = 0;
    uint32_t bit = 1U << 30;
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
    return (int32_t)res;
}
