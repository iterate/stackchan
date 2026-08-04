#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fable_keyframe.h"
#include "fable_motion.h"

/*
 * Five original face studies, one per persona, each staging a different
 * subset of the animation principles. The signature mirrors
 * face_render_frame() from firmware-ws/main/face_render.h: a pure
 * function of keyframe + 16 kHz sample clock producing one 160x120
 * RGB565 frame with no allocation and integer arithmetic only.
 */
enum {
    FABLE_STUDY_WIDTH = 160,
    FABLE_STUDY_HEIGHT = 120,
    FABLE_STUDY_PIXELS = FABLE_STUDY_WIDTH * FABLE_STUDY_HEIGHT,
};

typedef enum {
    /* Eye lead / head follow / body drag, anticipation, arcs. */
    FABLE_STUDY_CURIOUS_SCOUT = 0,
    /* Squash and stretch on a breathing body, blink phrasing. */
    FABLE_STUDY_EMBER_BREATH,
    /* Overshoot and settle, exaggerated timing, secondary cheeks. */
    FABLE_STUDY_PIP_SPARK,
    /* Slow in / slow out, heavy lids, long blink phrasing, yawns. */
    FABLE_STUDY_MOSS_DROWSE,
    /* Staging: listening / thinking / speaking poses and lighting. */
    FABLE_STUDY_SAGE_STAGER,
    FABLE_STUDY_COUNT,
} fable_study_t;

const char *fable_study_slug(fable_study_t study);
const char *fable_study_name(fable_study_t study);
const fable_persona_t *fable_study_persona(fable_study_t study);

bool fable_study_render(fable_study_t study,
                        const fable_keyframe_t *keyframe,
                        uint32_t sample_clock,
                        uint16_t *rgb565,
                        size_t pixel_capacity);
