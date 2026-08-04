#pragma once

#include "sprite_behavior.h"

/*
 * Internal contracts shared by sb_core.c / sb_clips.c / sb_mouth.c.
 * Nothing here is part of the public ABI.
 */

enum {
    SB_SAMPLES_PER_MS = 16, /* 16 kHz clock */
    SB_ART_FRESH_MS = 250,  /* articulation considered live */
    SB_NO_OVERRIDE = 255,
};

/* Per-state behavioral profile (const flash tables in sb_core.c). */
typedef struct {
    uint8_t attention;
    int8_t gaze_bias_x;
    int8_t gaze_bias_y;
    uint8_t gaze_jitter;      /* fixation half-range around the bias */
    uint16_t fix_min_ms;
    uint16_t fix_max_ms;
    uint8_t blink_interval_pct; /* scales configured blink interval */
    uint8_t blink_speed_pct;    /* scales blink close/open duration */
    uint16_t breath_period_ms;
    uint8_t breath_amp;
    uint8_t fx_cell;
    uint8_t eye_base;
    uint8_t clip_none_pct;
    uint8_t clip_weights[SB_CLIP_COUNT];
} sb_state_profile_t;

/* Emotion accents applied on top of the state profile. */
typedef struct {
    uint8_t brow_l;
    uint8_t brow_r;
    uint8_t eye_override;   /* SB_NO_OVERRIDE = keep */
    uint8_t eye_min_weight; /* emotion weight needed for the override */
    uint8_t mouth_rest;     /* rest-band mouth cell */
    int8_t gaze_dx;
    int8_t gaze_dy;
    int8_t head_tilt;       /* rot bias */
    uint8_t fx;             /* SB_NO_OVERRIDE = keep state fx */
    uint8_t fx_min_weight;
    int8_t lid_soften;      /* +droop / -raise, blink offset */
    uint8_t energy_pct;     /* micro-clip cadence scale */
} sb_emotion_accent_t;

extern const sb_state_profile_t sb_state_profiles[SB_STATE_COUNT];
extern const sb_emotion_accent_t sb_emotion_accents[FACE_EXPRESSION_COUNT];

/* What a staged micro-clip contributes to the composed pose. */
typedef struct {
    int16_t head_dx_q4;
    int16_t head_dy_q4;
    int16_t tilt_q4;        /* rot units, Q4 */
    int16_t gaze_dx_q4;
    int16_t gaze_dy_q4;
    int16_t body_dy_q4;
    int8_t stretch;
    int8_t brow_bias;       /* -32 knit .. 32 raise */
    uint8_t eye_override;   /* SB_NO_OVERRIDE = none */
    uint8_t breath_boost;   /* extra breath amplitude */
    uint8_t head_cell;      /* SB_NO_OVERRIDE = none */
    uint8_t blink_now;      /* clip requests an immediate blink */
} sb_clip_out_t;

/* RNG: xorshift32, drawn only at decision points. */
uint32_t sb_rand(sb_t *sb);
uint32_t sb_rand_range(sb_t *sb, uint32_t lo, uint32_t hi); /* inclusive */
uint8_t sb_rand_pct(sb_t *sb);                              /* 0..99 */

/* Integer easing, t in Q8 (0..256). */
int32_t sb_ease_smooth_q8(int32_t t_q8);      /* smoothstep */
int32_t sb_ease_out_q8(int32_t t_q8);         /* decelerating */
int32_t sb_ease_pulse_q8(int32_t t_q8);       /* 0->1->0 raised cosine-ish */

int32_t sb_clamp_i32(int32_t v, int32_t lo, int32_t hi);
uint8_t sb_clamp_u8(int32_t v);
int8_t sb_clamp_i8(int32_t v);

static inline uint32_t sb_ms_between(uint32_t from, uint32_t to)
{
    return (to - from) / SB_SAMPLES_PER_MS;
}

/* Clip channel (sb_clips.c). */
void sb_clips_schedule(sb_t *sb, uint32_t clock,
                       const sb_state_profile_t *profile,
                       const sb_emotion_accent_t *accent);
void sb_clip_start(sb_t *sb, uint8_t clip, uint32_t clock,
                   uint16_t dur_ms, int8_t dir, uint8_t amp);
/* Fold the active clip's current contribution into residuals and
 * clear it (preemption path). */
void sb_clip_preempt(sb_t *sb, uint32_t clock);
void sb_clip_eval(const sb_t *sb, uint32_t clock, sb_clip_out_t *out);

/* Mouth channel (sb_mouth.c). Returns the mouth layer selection. */
void sb_mouth_update(sb_t *sb, uint32_t clock, uint8_t rest_cell,
                     uint8_t talk, sb_layer_t *mouth_layer);
/* Immediate closed shape (interruption / state exits). */
void sb_mouth_shut(sb_t *sb, uint32_t clock, uint8_t rest_cell);
