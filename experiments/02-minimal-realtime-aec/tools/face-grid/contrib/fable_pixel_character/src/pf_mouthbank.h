#pragma once

#include "pf_internal.h"

/*
 * Shared ten-shape sprite mouth bank. Roles, not colours:
 *   o = dark lip/outline   l = lip body   c = cavity
 *   t = teeth              g = tongue
 * Renderers map roles to their own palette indices, so one set of original
 * drawings serves several art styles. Shape names follow the classic
 * six-basic/three-extended lip-sync convention; all pixels are original.
 */

typedef struct {
    const char *rows[8];
    uint8_t nrows;
    uint8_t width;
} pf_bank_shape_t;

static const pf_bank_shape_t pf_mouth_bank[PF_SHAPE_COUNT] = {
    [PF_SHAPE_REST] = {
        {
            "..o.........o..",
            ".ooooooooooooo.",
            "..lllllllllll..",
        },
        3, 15,
    },
    [PF_SHAPE_MBP] = {
        {
            "...lllllllll...",
            "..ooooooooooo..",
            "...lllllllll...",
        },
        3, 15,
    },
    [PF_SHAPE_FV] = {
        {
            "..ooooooooooo..",
            ".ottttttttttto.",
            "..lllllllllll..",
            "...ooooooooo...",
        },
        4, 15,
    },
    [PF_SHAPE_SS] = {
        {
            ".ooooooooooooo.",
            "ottttttttttttto",
            "o.ooooooooooo.o",
            ".ooooooooooooo.",
        },
        4, 15,
    },
    [PF_SHAPE_EE] = {
        {
            ".ooooooooooooo.",
            "ottttttttttttto",
            "occccccccccccco",
            "ottttttttttttto",
            ".ooooooooooooo.",
        },
        5, 15,
    },
    [PF_SHAPE_EH] = {
        {
            "...ooooooooo...",
            "..ottttttttto..",
            "..occccccccco..",
            "..ocgggggggco..",
            "...ooooooooo...",
        },
        5, 15,
    },
    [PF_SHAPE_AA] = {
        {
            "...ooooooooo...",
            "..ottttttttto..",
            ".occccccccccco.",
            ".occccccccccco.",
            ".ocgggggggggco.",
            "..occccccccco..",
            "...ooooooooo...",
        },
        7, 15,
    },
    [PF_SHAPE_OO] = {
        {
            ".....ooooo.....",
            "....olccclo....",
            "....olccclo....",
            ".....ooooo.....",
        },
        4, 15,
    },
    [PF_SHAPE_OH] = {
        {
            "....ooooooo....",
            "...ottttttto...",
            "...occccccco...",
            "...ocgggggco...",
            "...occccccco...",
            "....ooooooo....",
        },
        6, 15,
    },
    [PF_SHAPE_SMALL] = {
        {
            "...ooooooooo...",
            "..occccccccco..",
            "...ooooooooo...",
        },
        3, 15,
    },
};

/* colours[] order: o, l, c, t, g. */
static inline void pf_draw_mouth_bank(pf_surface_t *s, int cx, int cy,
                                      pf_mouth_shape_t shape,
                                      const uint8_t colours[5]) {
    if ((int)shape < 0 || shape >= PF_SHAPE_COUNT) {
        shape = PF_SHAPE_REST;
    }
    const pf_bank_shape_t *b = &pf_mouth_bank[shape];
    pf_blit(s, cx - b->width / 2, cy - b->nrows / 2, b->rows, b->nrows,
            "olctg", colours);
}
