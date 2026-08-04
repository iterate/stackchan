#include "pf_internal.h"

/*
 * Amber Terminal — a face composed entirely of character cells on a warm
 * phosphor CRT: 20×12 cells of 8×10 px, a 5×7 glyph font, ink-only
 * "ASCII art" facial features, a status line, an audio meter, and scanline
 * dimming. The mouth is a glyph-string system: each mouth shape swaps a
 * different row of characters into the face.
 */

enum {
    TM_BG = 0,
    TM_GLOW,
    TM_FAINT,
    TM_DIM,
    TM_MID,
    TM_BRIGHT,
    TM_HOT,
    TM_COLOUR_COUNT,
};

static const uint16_t tm_palette[TM_COLOUR_COUNT] = {
    PF_RGB565(0x0A, 0x05, 0x00), PF_RGB565(0x18, 0x0C, 0x00),
    PF_RGB565(0x2A, 0x15, 0x00), PF_RGB565(0x55, 0x2B, 0x00),
    PF_RGB565(0xAA, 0x55, 0x00), PF_RGB565(0xFF, 0xB0, 0x00),
    PF_RGB565(0xFF, 0xD8, 0x80),
};

enum { TM_W = 160, TM_H = 120, TM_CELL_W = 8, TM_CELL_H = 10 };

typedef struct {
    char ch;
    const char *rows[7];
} tm_glyph_t;

static const tm_glyph_t tm_font[] = {
    { '(', { "...#.", "..#..", ".#...", ".#...", ".#...", "..#..",
             "...#." } },
    { ')', { ".#...", "..#..", "...#.", "...#.", "...#.", "..#..",
             ".#..." } },
    { 'o', { ".....", ".....", ".###.", "#...#", "#...#", "#...#",
             ".###." } },
    { 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#",
             ".###." } },
    { '-', { ".....", ".....", ".....", "#####", ".....", ".....",
             "....." } },
    { '=', { ".....", ".....", "#####", ".....", "#####", ".....",
             "....." } },
    { '_', { ".....", ".....", ".....", ".....", ".....", ".....",
             "#####" } },
    { '/', { "....#", "....#", "...#.", "..#..", ".#...", "#....",
             "#...." } },
    { '\\', { "#....", "#....", ".#...", "..#..", "...#.", "....#",
              "....#" } },
    { '^', { "..#..", ".#.#.", "#...#", ".....", ".....", ".....",
             "....." } },
    { '|', { "..#..", "..#..", "..#..", "..#..", "..#..", "..#..",
             "..#.." } },
    { '#', { ".#.#.", "#####", ".#.#.", ".#.#.", ".#.#.", "#####",
             ".#.#." } },
    { '~', { ".....", ".....", ".#...", "#.#.#", "...#.", ".....",
             "....." } },
    { '.', { ".....", ".....", ".....", ".....", ".....", "..##.",
             "..##." } },
    { ':', { ".....", "..#..", ".....", ".....", "..#..", ".....",
             "....." } },
    { '+', { ".....", "..#..", "..#..", "#####", "..#..", "..#..",
             "....." } },
    { '\'', { "..#..", "..#..", ".....", ".....", ".....", ".....",
              "....." } },
    { 'v', { ".....", ".....", "#...#", "#...#", ".#.#.", "..#..",
             "....." } },
    { 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#",
             ".###." } },
};

enum { TM_FONT_COUNT = (int)(sizeof(tm_font) / sizeof(tm_font[0])) };

/* Draw one glyph in cell (cx, cy) of the 20×12 grid. */
static void tm_cell(pf_surface_t *s, int cx, int cy, char ch,
                    uint8_t colour) {
    if (ch == ' ' || ch == '.') {
        return;
    }
    for (int i = 0; i < TM_FONT_COUNT; ++i) {
        if (tm_font[i].ch != ch) {
            continue;
        }
        int ox = cx * TM_CELL_W + 1;
        int oy = cy * TM_CELL_H + 1;
        for (int gy = 0; gy < 7; ++gy) {
            const char *row = tm_font[i].rows[gy];
            for (int gx = 0; gx < 5; ++gx) {
                if (row[gx] == '#') {
                    pf_px(s, ox + gx, oy + gy, colour);
                }
            }
        }
        return;
    }
}

static void tm_cells(pf_surface_t *s, int cx, int cy, const char *str,
                     uint8_t colour) {
    for (int i = 0; str[i] != '\0'; ++i) {
        tm_cell(s, cx + i, cy, str[i], colour);
    }
}

/* Mouth shapes as glyph strings; '.' cells stay empty. */
typedef struct {
    const char *top;
    const char *mid;
    const char *bot;
} tm_mouth_t;

static const tm_mouth_t tm_mouths[PF_SHAPE_COUNT] = {
    [PF_SHAPE_REST] = { "......", ".----.", "......" },
    [PF_SHAPE_MBP] = { "......", ".====.", "......" },
    [PF_SHAPE_FV] = { "......", ".-==-.", "......" },
    [PF_SHAPE_SS] = { "......", ".####.", "......" },
    [PF_SHAPE_EE] = { ".====.", ".====.", "......" },
    [PF_SHAPE_EH] = { "./--\\.", ".\\__/.", "......" },
    [PF_SHAPE_AA] = { "./--\\.", ".|..|.", ".\\__/." },
    [PF_SHAPE_OO] = { "......", "..().." , "......" },
    [PF_SHAPE_OH] = { "..()..", "..().." , "......" },
    [PF_SHAPE_SMALL] = { "......", ".-..-.", "......" },
};

void pf_render_amber_terminal(uint16_t *fb, const face_keyframe_t *k,
                              const pf_rig_t *rig, uint32_t clock) {
    pf_surface_t art = pf_surface_attach(fb, TM_W, TM_H);
    pf_surface_t *s = &art;
    uint32_t ms = clock / 16U;

    pf_clear(s, TM_BG);
    /* Soft phosphor bloom behind the face block. */
    pf_fill_ellipse(s, 80, 62, 66, 46, TM_GLOW);

    /* Rare brightness dip makes the tube feel analogue. */
    uint8_t hot = rig->flicker < 26 ? TM_MID : TM_BRIGHT;
    uint8_t face = rig->flicker < 26 ? TM_DIM : TM_MID;

    /* Status line + blinking cursor. */
    pf_text3x5(s, 4, 3, "STACKCHAN TTY0 9600", TM_DIM);
    if ((ms / 530U) & 1U) {
        pf_fill_rect(s, 80, 2, 4, 6, TM_MID);
    }

    /* Frame box around the face field. */
    for (int cx = 2; cx <= 17; ++cx) {
        tm_cell(s, cx, 1, '-', TM_FAINT);
        tm_cell(s, cx, 10, '-', TM_FAINT);
    }
    for (int cy = 2; cy <= 9; ++cy) {
        tm_cell(s, 2, cy, '|', TM_FAINT);
        tm_cell(s, 17, cy, '|', TM_FAINT);
    }
    tm_cell(s, 2, 1, '+', TM_DIM);
    tm_cell(s, 17, 1, '+', TM_DIM);
    tm_cell(s, 2, 10, '+', TM_DIM);
    tm_cell(s, 17, 10, '+', TM_DIM);

    /* Brows: '~' cells that ride up a row when the brows lift. */
    int brow_row = rig->brow > 40 ? 2 : 3;
    tm_cells(s, 5, brow_row, "~~~", face);
    tm_cells(s, 12, brow_row, "~~~", face);

    /* Eyes: "( o )" groups; the pupil cell follows gaze, lids close to
     * '-' and '_'. Wide-open brows switch the pupil to 'O'. */
    int pup = pf_clampi(1 + rig->gaze_x / 64, 0, 2);
    char eye_open_ch = rig->brow > 60 ? 'O' : 'o';
    for (int side = 0; side < 2; ++side) {
        int base = side ? 11 : 4;
        int open = side ? rig->eye_open_r : rig->eye_open_l;
        tm_cell(s, base, 4, '(', face);
        tm_cell(s, base + 4, 4, ')', face);
        if (open > 170) {
            tm_cell(s, base + 1 + pup, 4, eye_open_ch, hot);
        } else if (open > 70) {
            tm_cell(s, base + 1 + pup, 4, '-', hot);
        } else {
            tm_cells(s, base + 1, 4, "___", face);
        }
    }

    /* Nose. */
    tm_cell(s, 9, 5, '/', TM_DIM);
    tm_cell(s, 10, 5, '\\', TM_DIM);

    /* Glyph mouth. */
    pf_mouth_shape_t shape = pf_mouth_classify(k);
    const tm_mouth_t *m = &tm_mouths[shape];
    uint8_t mc = (k->flags & FACE_KEYFRAME_FLAG_SPEAKING) ? hot : face;
    tm_cells(s, 7, 7, m->top, mc);
    tm_cells(s, 7, 8, m->mid, mc);
    tm_cells(s, 7, 9, m->bot, mc);

    /* Audio meter along the bottom row. */
    pf_text3x5(s, 4, 112, "SND", TM_DIM);
    int bars = ((int)k->mouth_open * 14) / 255;
    for (int i = 0; i < 14; ++i) {
        uint8_t c = i < bars ? (i > 10 ? TM_HOT : TM_MID) : TM_FAINT;
        pf_fill_rect(s, 20 + i * 6, 111, 4, 6, c);
    }
    /* Breathing indicator: slow spinner in the corner. */
    static const char spin[4] = { '|', '/', '-', '\\' };
    tm_cell(s, 18, 0, spin[(ms / 800U) & 3U], TM_DIM);

    pf_present(fb, s, 1, 1, tm_palette, PF_FX_SCANLINE);
}
