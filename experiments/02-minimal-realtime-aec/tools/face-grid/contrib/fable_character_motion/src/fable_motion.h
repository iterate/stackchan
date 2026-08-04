#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "fable_ease.h"
#include "fable_keyframe.h"

/*
 * Character-motion engine: a pure function of (persona, keyframe, sample
 * clock) that stages the classic animation principles as integer arithmetic.
 *
 *   anticipation      - the head counter-moves before large gaze shifts
 *   slow in/slow out  - every transition runs through the Q10 ease family
 *   follow-through    - eyes lead, head follows late, a drag layer trails
 *   overshoot/settle  - head transitions overshoot and settle per persona
 *   arcs              - horizontal head travel dips through a parabolic arc
 *   secondary action  - breathing, micro-gaze noise, brow accents
 *   staging           - idle acts run only when the face is otherwise idle
 *   timing/appeal     - persona tables re-time the same rig into characters
 *
 * The engine retains no state and allocates nothing: every episodic
 * behavior (blinks, fixations, idle acts, nods, speech accents) is derived
 * from the clock through fable_schedule(), so any frame can be evaluated
 * out of order and two calls with equal inputs are byte-identical.
 */

/* Idle acts (secondary-action set pieces, staged only while idle). */
enum {
    FABLE_ACT_NONE = 0,
    FABLE_ACT_GLANCE = 1,  /* slow look around, eyes leading the head */
    FABLE_ACT_TILT = 2,    /* curious head tilt via lids and brows */
    FABLE_ACT_YAWN = 3,    /* long lid droop, wide mouth, big stretch */
    FABLE_ACT_SIGH = 4,    /* one deep breath cycle with heavy lids */
    FABLE_ACT_SQUINT = 5,  /* brief scrutinizing squint, brows down */
    FABLE_ACT_WIGGLE = 6,  /* perky squash-and-stretch shimmy */
    FABLE_ACT_COUNT = 7,
};

#define FABLE_ACT_BIT(act) (1U << (act))

/*
 * Persona: an integer timing table that re-times one rig into one
 * character. All durations are milliseconds unless suffixed otherwise;
 * amplitudes in pixels (px), quarter-pixels (q2), Q10 units, or percent.
 */
typedef struct {
    const char *slug;
    const char *name;
    uint32_t seed;             /* decorrelates schedules between personas */

    /* Blink phrasing. */
    uint16_t blink_slot_ms;    /* one blink per slot, jittered inside it */
    uint16_t blink_close_ms;   /* lid descent (fast) */
    uint16_t blink_hold_ms;    /* fully closed dwell */
    uint16_t blink_open_ms;    /* lid ascent (slow) */
    uint8_t double_blink_pct;  /* chance a slot phrases a second beat */
    uint8_t blink_asym_ms;     /* right lid trails left by this much */

    /* Gaze: fixations, saccades, and the follow-through chain. */
    uint16_t gaze_slot_ms;     /* fixation cadence */
    uint8_t gaze_range_x_px;   /* exploration half-range around the host
                                  look target */
    uint8_t gaze_range_y_px;
    uint8_t saccade_base_ms;   /* saccade duration = base + per_px * amp */
    uint8_t saccade_per_px_ms;
    uint8_t micro_amp_q2;      /* micro-saccade/drift amplitude */
    uint16_t micro_period_ms;  /* drift lattice period */
    uint8_t head_follow_pct;   /* head carries this share of gaze offset */
    uint8_t head_lag_ms;       /* eyes lead; head starts this much later */
    uint16_t head_dur_pct;     /* head transition length vs saccade (>=100) */
    uint8_t body_follow_pct;   /* drag layer share (antennae, plates) */
    uint16_t body_lag_ms;
    uint16_t body_dur_pct;
    uint8_t overshoot_q10;     /* 0 = smooth stop, 1024 = full back-ease */
    uint8_t arc_dip_q2;        /* vertical arc depth while traveling */

    /* Anticipation before large gaze shifts. */
    uint8_t anticipation_ms;
    uint8_t anticipation_pct;  /* counter-move share of upcoming travel */
    uint8_t anticipation_min_px;

    /* Breathing (secondary action, squash and stretch). */
    uint16_t breath_period_ms;
    uint8_t breath_amp;        /* 0..255 */
    uint8_t stretch_max;       /* peak |stretch| in Q5 (32 = 1 px scale) */

    /* Idle acts. */
    uint16_t act_slot_ms;
    uint8_t act_none_pct;      /* chance a slot stages nothing */
    uint8_t act_mask;          /* FABLE_ACT_BIT() set of allowed acts */

    /* Resting face. */
    uint16_t lid_rest_q10;     /* resting upper-lid openness, <= 1024 */
    int8_t brow_rest;
    int8_t lid_tilt_rest;      /* -32..32, positive = inner corners up */
    uint8_t energy;            /* renderer accent level */
} fable_persona_t;

/* Acted pose: what a renderer draws. Offsets are quarter-pixel (q2). */
typedef struct {
    int16_t eye_x_q2;   /* gaze offset for pupils/eye placement (leads) */
    int16_t eye_y_q2;
    int16_t head_x_q2;  /* face-plate offset (follows) */
    int16_t head_y_q2;
    int16_t body_x_q2;  /* slow drag layer (trails the head) */
    int16_t body_y_q2;
    uint16_t lid_left_q10;  /* 0 closed .. 1024 fully open */
    uint16_t lid_right_q10;
    int8_t lid_tilt;    /* -32..32 lid slant for mood/tilt acts */
    int8_t brow_left;   /* -64 furrow .. 64 raise */
    int8_t brow_right;
    uint8_t breath;     /* 0 exhaled .. 255 peak inhale */
    int8_t stretch;     /* -32 squash .. 32 stretch (volume-conserving) */
    uint8_t mouth_open; /* keyframe mouth after act overrides */
    uint8_t mouth_width;
    uint8_t mouth_round;
    uint8_t mouth_press;
    uint8_t mouth_teeth;
    uint8_t act;        /* FABLE_ACT_* currently staged (0 = none) */
    uint8_t act_phase;  /* 0..255 progress through the act */
    uint8_t energy;     /* persona energy shaded by activity */
    uint8_t reserved;   /* zeroed: keeps the struct padding-free so
                           poses can be hashed/compared bytewise */
} fable_motion_pose_t;

_Static_assert(sizeof(fable_motion_pose_t) == 30,
               "acted pose must stay padding-free");

/* Built-in personas. */
extern const fable_persona_t FABLE_PERSONA_CALM;    /* even-keeled default */
extern const fable_persona_t FABLE_PERSONA_PERKY;   /* quick, springy */
extern const fable_persona_t FABLE_PERSONA_SLEEPY;  /* slow, heavy-lidded */
extern const fable_persona_t FABLE_PERSONA_CURIOUS; /* wide-ranging gaze */
extern const fable_persona_t FABLE_PERSONA_SAGE;    /* attentive, steady */

enum { FABLE_PERSONA_COUNT = 5 };

/* Indexable persona table, order matching the studies manifest. */
const fable_persona_t *fable_persona_at(uint32_t index);

/*
 * Evaluate the acted pose for one instant. `clock` is the 16 kHz sample
 * clock; `keyframe` may be NULL for a silent idle preview.
 */
void fable_motion_eval(const fable_persona_t *persona,
                       const fable_keyframe_t *keyframe,
                       uint32_t clock,
                       fable_motion_pose_t *out);
