#include <string.h>

#include "sb_internal.h"

/*
 * Core: session state machine, blink/breath/gaze channels, emotion
 * envelope, transcript pacing, and frame composition. Integer-only;
 * randomness is drawn exclusively at scheduled decision points.
 */

/* ------------------------------------------------------------------ */
/* Small utilities                                                     */
/* ------------------------------------------------------------------ */

uint32_t sb_rand(sb_t *sb)
{
    uint32_t x = sb->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sb->rng = x;
    return x;
}

uint32_t sb_rand_range(sb_t *sb, uint32_t lo, uint32_t hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (sb_rand(sb) % (hi - lo + 1U));
}

uint8_t sb_rand_pct(sb_t *sb)
{
    return (uint8_t)(sb_rand(sb) % 100U);
}

int32_t sb_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

uint8_t sb_clamp_u8(int32_t v)
{
    return (uint8_t)sb_clamp_i32(v, 0, 255);
}

int8_t sb_clamp_i8(int32_t v)
{
    return (int8_t)sb_clamp_i32(v, -127, 127);
}

int32_t sb_ease_smooth_q8(int32_t t_q8)
{
    t_q8 = sb_clamp_i32(t_q8, 0, 256);
    /* 3t^2 - 2t^3 in Q8. */
    int32_t t2 = (t_q8 * t_q8) >> 8;
    int32_t t3 = (t2 * t_q8) >> 8;
    return sb_clamp_i32(3 * t2 - 2 * t3, 0, 256);
}

int32_t sb_ease_out_q8(int32_t t_q8)
{
    t_q8 = sb_clamp_i32(t_q8, 0, 256);
    int32_t inv = 256 - t_q8;
    return 256 - ((inv * inv) >> 8);
}

int32_t sb_ease_pulse_q8(int32_t t_q8)
{
    t_q8 = sb_clamp_i32(t_q8, 0, 256);
    if (t_q8 <= 128) {
        return sb_ease_smooth_q8(t_q8 * 2);
    }
    return sb_ease_smooth_q8((256 - t_q8) * 2);
}

/* ------------------------------------------------------------------ */
/* Behavioral tables                                                   */
/* ------------------------------------------------------------------ */

/* clip_weights order matches sb_clip_t:
 *  NONE SACC GLAN ACK TILT TURN SIGH SETT LEAN STAR DROP SRCH gN gS gB */
const sb_state_profile_t sb_state_profiles[SB_STATE_COUNT] = {
    [SB_STATE_DISCONNECTED] = {
        .attention = 30, .gaze_bias_x = 0, .gaze_bias_y = 16,
        .gaze_jitter = 5, .fix_min_ms = 2600, .fix_max_ms = 5200,
        .blink_interval_pct = 170, .blink_speed_pct = 210,
        .breath_period_ms = 5200, .breath_amp = 190,
        .fx_cell = SB_FX_ZZZ, .eye_base = SB_EYE_SOFT,
        .clip_none_pct = 55,
        .clip_weights = {0, 6, 5, 0, 4, 0, 18, 8, 0, 0, 30, 0, 0, 0, 0},
    },
    [SB_STATE_CONNECTING] = {
        .attention = 150, .gaze_bias_x = 0, .gaze_bias_y = -6,
        .gaze_jitter = 10, .fix_min_ms = 700, .fix_max_ms = 1500,
        .blink_interval_pct = 90, .blink_speed_pct = 100,
        .breath_period_ms = 3100, .breath_amp = 95,
        .fx_cell = SB_FX_ALERT, .eye_base = SB_EYE_OPEN,
        .clip_none_pct = 25,
        .clip_weights = {0, 18, 10, 0, 8, 12, 4, 4, 4, 0, 0, 36, 0, 0, 0},
    },
    [SB_STATE_LISTENING] = {
        .attention = 220, .gaze_bias_x = 0, .gaze_bias_y = -1,
        .gaze_jitter = 7, .fix_min_ms = 900, .fix_max_ms = 2200,
        .blink_interval_pct = 100, .blink_speed_pct = 100,
        .breath_period_ms = 3600, .breath_amp = 70,
        .fx_cell = SB_FX_NONE, .eye_base = SB_EYE_OPEN,
        .clip_none_pct = 30,
        .clip_weights = {0, 26, 8, 6, 12, 5, 6, 8, 9, 0, 0, 0, 0, 0, 0},
    },
    [SB_STATE_USER_SPEAKING] = {
        .attention = 255, .gaze_bias_x = 0, .gaze_bias_y = -3,
        .gaze_jitter = 4, .fix_min_ms = 1200, .fix_max_ms = 2600,
        .blink_interval_pct = 135, .blink_speed_pct = 90,
        .breath_period_ms = 3400, .breath_amp = 60,
        .fx_cell = SB_FX_NONE, .eye_base = SB_EYE_OPEN,
        .clip_none_pct = 42,
        .clip_weights = {0, 14, 2, 24, 10, 0, 2, 4, 12, 0, 0, 0, 0, 0, 0},
    },
    [SB_STATE_THINKING] = {
        .attention = 160, .gaze_bias_x = 12, .gaze_bias_y = -16,
        .gaze_jitter = 9, .fix_min_ms = 700, .fix_max_ms = 1600,
        .blink_interval_pct = 110, .blink_speed_pct = 100,
        .breath_period_ms = 3200, .breath_amp = 85,
        .fx_cell = SB_FX_DOTS, .eye_base = SB_EYE_OPEN,
        .clip_none_pct = 24,
        .clip_weights = {0, 18, 12, 0, 10, 4, 4, 6, 0, 0, 0, 26, 0, 0, 0},
    },
    [SB_STATE_ASSISTANT_SPEAKING] = {
        .attention = 235, .gaze_bias_x = 0, .gaze_bias_y = 0,
        .gaze_jitter = 5, .fix_min_ms = 1000, .fix_max_ms = 2200,
        .blink_interval_pct = 115, .blink_speed_pct = 100,
        .breath_period_ms = 3200, .breath_amp = 55,
        .fx_cell = SB_FX_NONE, .eye_base = SB_EYE_OPEN,
        .clip_none_pct = 44,
        .clip_weights = {0, 16, 4, 0, 8, 3, 0, 5, 6, 0, 0, 0, 0, 0, 0},
    },
    [SB_STATE_INTERRUPTED] = {
        .attention = 255, .gaze_bias_x = 0, .gaze_bias_y = -4,
        .gaze_jitter = 2, .fix_min_ms = 400, .fix_max_ms = 800,
        .blink_interval_pct = 200, .blink_speed_pct = 80,
        .breath_period_ms = 2600, .breath_amp = 60,
        .fx_cell = SB_FX_NONE, .eye_base = SB_EYE_WIDE,
        .clip_none_pct = 100,
        .clip_weights = {0},
    },
    [SB_STATE_ERROR] = {
        .attention = 120, .gaze_bias_x = 0, .gaze_bias_y = 12,
        .gaze_jitter = 4, .fix_min_ms = 1500, .fix_max_ms = 3000,
        .blink_interval_pct = 130, .blink_speed_pct = 135,
        .breath_period_ms = 4300, .breath_amp = 75,
        .fx_cell = SB_FX_ERROR, .eye_base = SB_EYE_SOFT,
        .clip_none_pct = 48,
        .clip_weights = {0, 8, 4, 0, 6, 0, 14, 14, 0, 0, 10, 0, 0, 0, 0},
    },
};

/* Per-state resting brows (emotion accents override these). */
static const uint8_t k_state_brow[SB_STATE_COUNT][2] = {
    [SB_STATE_DISCONNECTED] = {SB_BROW_NEUTRAL, SB_BROW_NEUTRAL},
    [SB_STATE_CONNECTING] = {SB_BROW_RAISED, SB_BROW_RAISED},
    [SB_STATE_LISTENING] = {SB_BROW_NEUTRAL, SB_BROW_NEUTRAL},
    [SB_STATE_USER_SPEAKING] = {SB_BROW_RAISED, SB_BROW_RAISED},
    [SB_STATE_THINKING] = {SB_BROW_KNIT, SB_BROW_NEUTRAL},
    [SB_STATE_ASSISTANT_SPEAKING] = {SB_BROW_NEUTRAL, SB_BROW_NEUTRAL},
    [SB_STATE_INTERRUPTED] = {SB_BROW_RAISED, SB_BROW_RAISED},
    [SB_STATE_ERROR] = {SB_BROW_SAD, SB_BROW_SAD},
};

const sb_emotion_accent_t sb_emotion_accents[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] = {
        SB_BROW_NEUTRAL, SB_BROW_NEUTRAL, SB_NO_OVERRIDE, 255,
        SB_MOUTH_REST, 0, 0, 0, SB_NO_OVERRIDE, 255, 0, 100},
    [FACE_EXPRESSION_WARM] = {
        SB_BROW_NEUTRAL, SB_BROW_NEUTRAL, SB_EYE_HAPPY, 180,
        SB_MOUTH_SMILE, 0, 0, 2, SB_NO_OVERRIDE, 255, 8, 100},
    [FACE_EXPRESSION_JOY] = {
        SB_BROW_RAISED, SB_BROW_RAISED, SB_EYE_HAPPY, 140,
        SB_MOUTH_SMILE, 0, -2, 4, SB_FX_SPARK, 200, 0, 120},
    [FACE_EXPRESSION_CONCERN] = {
        SB_BROW_SAD, SB_BROW_SAD, SB_NO_OVERRIDE, 255,
        SB_MOUTH_FROWN, 0, 4, -2, SB_NO_OVERRIDE, 255, 10, 90},
    [FACE_EXPRESSION_SURPRISE] = {
        SB_BROW_RAISED, SB_BROW_RAISED, SB_EYE_WIDE, 120,
        SB_MOUTH_O, 0, -6, 0, SB_NO_OVERRIDE, 255, -24, 110},
    [FACE_EXPRESSION_THOUGHTFUL] = {
        SB_BROW_KNIT, SB_BROW_NEUTRAL, SB_NO_OVERRIDE, 255,
        SB_MOUTH_PRESS, 10, -10, 3, SB_NO_OVERRIDE, 255, 6, 95},
    [FACE_EXPRESSION_SKEPTICAL] = {
        SB_BROW_KNIT, SB_BROW_RAISED, SB_EYE_SQUINT, 170,
        SB_MOUTH_PRESS, 6, 0, -3, SB_NO_OVERRIDE, 255, 6, 95},
    [FACE_EXPRESSION_DETERMINED] = {
        SB_BROW_KNIT, SB_BROW_KNIT, SB_EYE_SQUINT, 190,
        SB_MOUTH_PRESS, 0, 0, 0, SB_NO_OVERRIDE, 255, 0, 110},
    [FACE_EXPRESSION_SLEEPY] = {
        SB_BROW_SAD, SB_BROW_SAD, SB_EYE_SOFT, 120,
        SB_MOUTH_REST, 0, 6, 2, SB_FX_ZZZ, 220, 40, 70},
    [FACE_EXPRESSION_EXCITED] = {
        SB_BROW_RAISED, SB_BROW_RAISED, SB_EYE_WIDE, 150,
        SB_MOUTH_SMILE, 0, -3, 0, SB_FX_SPARK, 180, -12, 140},
    [FACE_EXPRESSION_EMBARRASSED] = {
        SB_BROW_SAD, SB_BROW_SAD, SB_EYE_SOFT, 160,
        SB_MOUTH_SMILE, 8, 8, 5, SB_NO_OVERRIDE, 255, 14, 90},
};

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void sb_config_default(sb_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->settle_ms = 180;
    config->interrupted_ms = 700;
    config->clip_slot_ms = 1100;
    config->blink_min_ms = 2400;
    config->blink_max_ms = 6200;
    config->blink_close_ms = 70;
    config->blink_open_ms = 150;
    config->breath_period_ms = 3600;
    config->mouth_min_hold_ms = 90;
    config->mouth_debounce_ms = 50;
    config->mouth_close_ms = 220;
    config->transcript_char_ms = 55;
    config->double_blink_pct = 14;
    config->ack_nod_pct = 55;
    config->idle_none_pct = 30;
}

static void sb_reset_schedules(sb_t *sb, uint32_t clock)
{
    const sb_state_profile_t *p = &sb_state_profiles[sb->state];
    sb->blink_next = clock +
        sb_rand_range(sb, sb->config.blink_min_ms, sb->config.blink_max_ms) *
            (uint32_t)p->blink_interval_pct / 100U * SB_SAMPLES_PER_MS;
    sb->blink_start = 0;
    sb->blink_double = 0;
    sb->blink_inhibit_until = clock;
    sb->clip = SB_CLIP_NONE;
    sb->clip_next = clock +
        sb_rand_range(sb, 400, sb->config.clip_slot_ms) * SB_SAMPLES_PER_MS;
    sb->ack_next = clock + sb_rand_range(sb, 2500, 4500) * SB_SAMPLES_PER_MS;
    sb->fix_next = clock + sb_rand_range(sb, 150, 600) * SB_SAMPLES_PER_MS;
    sb->sacc_start = clock;
    sb->sacc_dur_ms = 1;
    sb->breath_last_clock = clock;
    sb->res_head_x_q4 = 0;
    sb->res_head_y_q4 = 0;
    sb->res_tilt_q4 = 0;
    sb->mouth_since = clock;
    sb->mouth_pending_since = clock;
    sb->last_clock = clock;
}

static bool sb_config_valid(const sb_config_t *c)
{
    return c != NULL && c->blink_max_ms >= c->blink_min_ms &&
           c->blink_min_ms > 0 && c->breath_period_ms > 0 &&
           c->clip_slot_ms > 0 && c->settle_ms > 0 &&
           c->mouth_min_hold_ms > 0 && c->interrupted_ms > 0;
}

bool sb_init_with_config(sb_t *sb, uint32_t seed, const sb_config_t *config)
{
    bool ok = sb_config_valid(config);
    memset(sb, 0, sizeof(*sb));
    if (ok) {
        sb->config = *config;
    } else {
        sb_config_default(&sb->config);
    }
    sb->rng = seed != 0U ? seed : 0x5EEDCAFEU;
    sb->state = SB_STATE_DISCONNECTED;
    sb->prev_state = SB_STATE_DISCONNECTED;
    sb->art_viseme = FACE_VISEME_NONE;
    sb->mouth_cell = SB_MOUTH_REST;
    sb->mouth_pending = SB_MOUTH_REST;
    sb->cue_active = 0;
    sb->cue_pending = 0;
    sb_reset_schedules(sb, 0);
    return ok;
}

void sb_init(sb_t *sb, uint32_t seed)
{
    sb_config_t config;
    sb_config_default(&config);
    (void)sb_init_with_config(sb, seed, &config);
}

sb_state_t sb_state_get(const sb_t *sb)
{
    return (sb_state_t)sb->state;
}

/* ------------------------------------------------------------------ */
/* Session state machine                                               */
/* ------------------------------------------------------------------ */

static void sb_transition(sb_t *sb, sb_state_t next, uint32_t clock)
{
    if (next == (sb_state_t)sb->state) {
        return;
    }
    sb_clip_preempt(sb, clock);
    sb->prev_state = sb->state;
    sb->state = (uint8_t)next;
    sb->state_since = clock;

    const sb_state_profile_t *p = &sb_state_profiles[next];
    sb->clip_next = clock +
        sb_rand_range(sb, 350, sb->config.clip_slot_ms) * SB_SAMPLES_PER_MS;
    sb->fix_next = clock + sb_rand_range(sb, 80, 240) * SB_SAMPLES_PER_MS;
    (void)p;

    switch (next) {
    case SB_STATE_USER_SPEAKING:
        sb->blink_inhibit_until = clock + 700U * SB_SAMPLES_PER_MS;
        sb->ack_next =
            clock + sb_rand_range(sb, 2500, 4500) * SB_SAMPLES_PER_MS;
        break;
    case SB_STATE_INTERRUPTED:
        sb_clip_start(sb, SB_CLIP_STARTLE, clock,
                      (uint16_t)sb_rand_range(sb, 450, 600), 1, 200);
        sb_mouth_shut(sb, clock, SB_MOUTH_REST);
        sb->blink_inhibit_until = clock + 400U * SB_SAMPLES_PER_MS;
        break;
    case SB_STATE_CONNECTING:
        if (sb->prev_state == SB_STATE_DISCONNECTED) {
            /* wake-up perk */
            sb_clip_start(sb, SB_CLIP_STARTLE, clock, 380, 1, 90);
        }
        break;
    case SB_STATE_LISTENING:
        if ((sb->prev_state == SB_STATE_ASSISTANT_SPEAKING ||
             sb->prev_state == SB_STATE_INTERRUPTED) &&
            sb_rand_pct(sb) < 60) {
            sb_clip_start(sb, SB_CLIP_SETTLE, clock,
                          (uint16_t)sb_rand_range(sb, 500, 700), 1, 120);
        }
        break;
    case SB_STATE_THINKING:
        sb->fix_next = clock + 40U * SB_SAMPLES_PER_MS;
        break;
    case SB_STATE_ASSISTANT_SPEAKING:
        sb->speech_expect_until = clock;
        break;
    case SB_STATE_ERROR:
        sb_clip_start(sb, SB_CLIP_SETTLE, clock, 550, 1, 140);
        sb_mouth_shut(sb, clock, SB_MOUTH_PRESS);
        break;
    case SB_STATE_DISCONNECTED:
        sb->clip_next =
            clock + sb_rand_range(sb, 1200, 2400) * SB_SAMPLES_PER_MS;
        break;
    default:
        break;
    }
}

void sb_handle_event(sb_t *sb, sb_event_t event, uint32_t sample_clock)
{
    sb_state_t s = (sb_state_t)sb->state;
    bool connected = s == SB_STATE_LISTENING || s == SB_STATE_USER_SPEAKING ||
                     s == SB_STATE_THINKING ||
                     s == SB_STATE_ASSISTANT_SPEAKING ||
                     s == SB_STATE_INTERRUPTED;

    switch (event) {
    case SB_EV_DISCONNECTED:
        sb_transition(sb, SB_STATE_DISCONNECTED, sample_clock);
        break;
    case SB_EV_CONNECTING:
        if (s == SB_STATE_DISCONNECTED || s == SB_STATE_ERROR) {
            sb_transition(sb, SB_STATE_CONNECTING, sample_clock);
        }
        break;
    case SB_EV_CONNECTED:
        if (s == SB_STATE_DISCONNECTED || s == SB_STATE_CONNECTING ||
            s == SB_STATE_ERROR) {
            sb_transition(sb, SB_STATE_LISTENING, sample_clock);
        }
        break;
    case SB_EV_USER_SPEECH_STARTED:
        if (connected) {
            sb_transition(sb, SB_STATE_USER_SPEAKING, sample_clock);
        }
        break;
    case SB_EV_USER_SPEECH_STOPPED:
        if (s == SB_STATE_USER_SPEAKING) {
            /* A pause is the classic acknowledgement moment. */
            if (sb_rand_pct(sb) < sb->config.ack_nod_pct) {
                sb_clip_start(sb, SB_CLIP_ACK_NOD, sample_clock,
                              (uint16_t)sb_rand_range(sb, 550, 800), 1,
                              (uint8_t)sb_rand_range(sb, 120, 200));
            }
            sb_transition(sb, SB_STATE_LISTENING, sample_clock);
        }
        break;
    case SB_EV_THINKING:
        if (s == SB_STATE_LISTENING || s == SB_STATE_USER_SPEAKING) {
            sb_transition(sb, SB_STATE_THINKING, sample_clock);
        }
        break;
    case SB_EV_ASSISTANT_SPEECH_STARTED:
        if (connected) {
            sb_transition(sb, SB_STATE_ASSISTANT_SPEAKING, sample_clock);
        }
        break;
    case SB_EV_ASSISTANT_SPEECH_STOPPED:
        if (s == SB_STATE_ASSISTANT_SPEAKING) {
            sb_transition(sb, SB_STATE_LISTENING, sample_clock);
        }
        break;
    case SB_EV_INTERRUPTED:
        if (s == SB_STATE_ASSISTANT_SPEAKING || s == SB_STATE_THINKING) {
            sb_transition(sb, SB_STATE_INTERRUPTED, sample_clock);
        }
        break;
    case SB_EV_ERROR:
        sb_transition(sb, SB_STATE_ERROR, sample_clock);
        break;
    case SB_EV_ERROR_CLEARED:
        if (s == SB_STATE_ERROR) {
            sb_transition(sb, SB_STATE_LISTENING, sample_clock);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Stage direction / articulation / transcript inputs                  */
/* ------------------------------------------------------------------ */

static uint32_t sb_cue_weight(const face_stage_cue_t *cue, uint32_t clock,
                              bool *finished)
{
    *finished = false;
    if (clock < cue->start_sample) {
        return 0;
    }
    uint32_t e = clock - cue->start_sample;
    uint32_t intensity = cue->intensity;
    if (cue->attack_samples > 0) {
        if (e < cue->attack_samples) {
            return intensity * e / cue->attack_samples;
        }
        e -= cue->attack_samples;
    }
    if ((cue->flags & FACE_STAGE_FLAG_HOLD_FINAL) != 0U) {
        return intensity;
    }
    if (e < cue->hold_samples) {
        return intensity;
    }
    e -= cue->hold_samples;
    if (cue->release_samples > 0 && e < cue->release_samples) {
        return intensity * (cue->release_samples - e) / cue->release_samples;
    }
    *finished = true;
    return 0;
}

void sb_direct(sb_t *sb, const face_stage_cue_t *cue)
{
    if (cue == NULL) {
        return;
    }
    uint32_t now = sb->last_clock;

    if (cue->gesture != FACE_GESTURE_NONE) {
        uint32_t total = (cue->attack_samples + cue->hold_samples +
                          cue->release_samples) /
                         SB_SAMPLES_PER_MS;
        if (total < 300U || total > 4000U) {
            total = 800U;
        }
        uint8_t clip = SB_CLIP_NONE;
        int8_t dir = 1;
        switch (cue->gesture) {
        case FACE_GESTURE_NOD:
            clip = SB_CLIP_GESTURE_NOD;
            break;
        case FACE_GESTURE_SHAKE:
            clip = SB_CLIP_GESTURE_SHAKE;
            break;
        case FACE_GESTURE_TILT:
            clip = SB_CLIP_HEAD_TILT;
            dir = (cue->valence < 0) ? -1 : 1;
            break;
        case FACE_GESTURE_LEAN_IN:
            clip = SB_CLIP_LEAN_IN;
            break;
        case FACE_GESTURE_BOUNCE:
            clip = SB_CLIP_GESTURE_BOUNCE;
            break;
        default:
            break;
        }
        if (clip != SB_CLIP_NONE) {
            uint8_t amp = cue->intensity != 0 ? cue->intensity : 160;
            sb_clip_preempt(sb, now);
            sb_clip_start(sb, clip, now, (uint16_t)total, dir, amp);
        }
    }

    if (cue->expression == FACE_EXPRESSION_NEUTRAL && cue->intensity == 0 &&
        cue->gesture != FACE_GESTURE_NONE) {
        return; /* pure gesture cue; keep the current emotion */
    }

    if (!sb->cue_active) {
        sb->cue = *cue;
        sb->cue_active = 1;
        return;
    }
    switch (cue->interrupt_mode) {
    case FACE_STAGE_INTERRUPT_QUEUE:
        sb->cue_queued = *cue;
        sb->cue_pending = 1;
        break;
    case FACE_STAGE_INTERRUPT_CUT:
        sb->cue = *cue;
        sb->cue_pending = 0;
        break;
    case FACE_STAGE_INTERRUPT_BLEND:
    default: {
        /* Continue the ramp from the current resolved weight. */
        bool fin = false;
        uint32_t w = sb_cue_weight(&sb->cue, now, &fin);
        sb->cue = *cue;
        if (cue->intensity > 0 && w > 0 && cue->attack_samples > 0 &&
            cue->start_sample <= now) {
            uint32_t skip = cue->attack_samples * w / cue->intensity;
            uint32_t elapsed = now - cue->start_sample;
            if (skip > elapsed) {
                sb->cue.start_sample = now - skip;
            }
        }
        sb->cue_pending = 0;
        break;
    }
    }
}

void sb_set_articulation(sb_t *sb, const face_keyframe_t *keyframe,
                         uint8_t viseme, uint32_t sample_clock)
{
    if (keyframe == NULL) {
        return;
    }
    sb->art = *keyframe;
    sb->art_viseme = viseme;
    sb->art_at = sample_clock;
}

void sb_transcript_delta(sb_t *sb, uint32_t sample_clock, uint16_t chars)
{
    uint32_t base = sb->speech_expect_until;
    if (base < sample_clock) {
        base = sample_clock;
    }
    uint32_t add =
        (uint32_t)chars * sb->config.transcript_char_ms * SB_SAMPLES_PER_MS;
    uint32_t cap = sample_clock + 4000U * SB_SAMPLES_PER_MS;
    base += add;
    if (base > cap) {
        base = cap;
    }
    sb->speech_expect_until = base;
    sb->transcript_last = sample_clock;
}

/* ------------------------------------------------------------------ */
/* Channels                                                            */
/* ------------------------------------------------------------------ */

enum {
    SB_BLINK_DOUBLE_PENDING = 1U << 0,
    SB_BLINK_ACTIVE = 1U << 1,
};

static void sb_blink_schedule_next(sb_t *sb, uint32_t clock,
                                   const sb_state_profile_t *p)
{
    uint32_t interval_ms =
        sb_rand_range(sb, sb->config.blink_min_ms, sb->config.blink_max_ms) *
        (uint32_t)p->blink_interval_pct / 100U;
    sb->blink_next = clock + interval_ms * SB_SAMPLES_PER_MS;
}

static void sb_blink_update(sb_t *sb, uint32_t clock,
                            const sb_state_profile_t *p)
{
    if ((sb->blink_double & SB_BLINK_ACTIVE) == 0U) {
        if (clock < sb->blink_next) {
            return;
        }
        if (clock < sb->blink_inhibit_until) {
            sb->blink_next =
                sb->blink_inhibit_until +
                sb_rand_range(sb, 120, 420) * SB_SAMPLES_PER_MS;
            return;
        }
        if (sb_ms_between(sb->blink_next, clock) > 600U) {
            /* Missed by a large clock jump; reschedule instead of a
             * late surprise blink. */
            sb_blink_schedule_next(sb, clock, p);
            return;
        }
        sb->blink_start = clock;
        sb->blink_hold_ms = (uint16_t)sb_rand_range(sb, 25, 65);
        sb->blink_double = SB_BLINK_ACTIVE;
        if (sb_rand_pct(sb) < sb->config.double_blink_pct) {
            sb->blink_double |= SB_BLINK_DOUBLE_PENDING;
        }
        return;
    }

    uint32_t close_ms = (uint32_t)sb->config.blink_close_ms *
                        p->blink_speed_pct / 100U;
    uint32_t open_ms = (uint32_t)sb->config.blink_open_ms *
                       p->blink_speed_pct / 100U;
    uint32_t total = close_ms + sb->blink_hold_ms + open_ms;
    if (sb_ms_between(sb->blink_start, clock) >= total) {
        if ((sb->blink_double & SB_BLINK_DOUBLE_PENDING) != 0U) {
            sb->blink_double = 0;
            sb->blink_next =
                clock + sb_rand_range(sb, 140, 260) * SB_SAMPLES_PER_MS;
        } else {
            sb->blink_double = 0;
            sb_blink_schedule_next(sb, clock, p);
        }
    }
}

/* Lid closure 0..255 at `lag_ms` behind the leading lid. */
static uint8_t sb_blink_value(const sb_t *sb, uint32_t clock,
                              const sb_state_profile_t *p, uint32_t lag_ms)
{
    if ((sb->blink_double & SB_BLINK_ACTIVE) == 0U) {
        return 0;
    }
    uint32_t e = sb_ms_between(sb->blink_start, clock);
    if (e < lag_ms) {
        return 0;
    }
    e -= lag_ms;
    uint32_t close_ms = (uint32_t)sb->config.blink_close_ms *
                        p->blink_speed_pct / 100U;
    uint32_t open_ms = (uint32_t)sb->config.blink_open_ms *
                       p->blink_speed_pct / 100U;
    if (close_ms == 0) {
        close_ms = 1;
    }
    if (open_ms == 0) {
        open_ms = 1;
    }
    if (e < close_ms) {
        return (uint8_t)(sb_ease_smooth_q8((int32_t)(e * 256U / close_ms)) *
                         255 / 256);
    }
    e -= close_ms;
    if (e < sb->blink_hold_ms) {
        return 255;
    }
    e -= sb->blink_hold_ms;
    if (e < open_ms) {
        return (uint8_t)(255 -
                         sb_ease_smooth_q8((int32_t)(e * 256U / open_ms)) *
                             255 / 256);
    }
    return 0;
}

static void sb_gaze_update(sb_t *sb, uint32_t clock,
                           const sb_state_profile_t *p,
                           const sb_emotion_accent_t *accent,
                           uint32_t emotion_w, bool cue_gaze,
                           uint8_t cue_target)
{
    if (clock >= sb->fix_next) {
        int32_t bias_x = p->gaze_bias_x;
        int32_t bias_y = p->gaze_bias_y;
        int32_t jitter = p->gaze_jitter;
        bias_x += (int32_t)accent->gaze_dx * (int32_t)emotion_w / 255;
        bias_y += (int32_t)accent->gaze_dy * (int32_t)emotion_w / 255;
        if (sb->state == SB_STATE_THINKING) {
            /* Alternate the pondering side. */
            if ((sb_rand(sb) & 1U) != 0U) {
                bias_x = -bias_x;
            }
        }
        if (cue_gaze) {
            switch (cue_target) {
            case FACE_GAZE_USER:
                bias_x = 0;
                bias_y = 0;
                break;
            case FACE_GAZE_LEFT:
                bias_x = -40;
                bias_y = 0;
                break;
            case FACE_GAZE_RIGHT:
                bias_x = 40;
                bias_y = 0;
                break;
            case FACE_GAZE_UP:
                bias_x = 0;
                bias_y = -30;
                break;
            case FACE_GAZE_DOWN:
                bias_x = 0;
                bias_y = 26;
                break;
            case FACE_GAZE_AWAY:
                bias_x = -46;
                bias_y = 10;
                break;
            default:
                break;
            }
            jitter = jitter / 3;
        }
        int32_t jx = jitter > 0
                         ? (int32_t)sb_rand_range(sb, 0, (uint32_t)jitter * 2) -
                               jitter
                         : 0;
        int32_t jy = jitter > 0
                         ? ((int32_t)sb_rand_range(sb, 0,
                                                   (uint32_t)jitter * 2) -
                            jitter) *
                               3 / 5
                         : 0;
        sb->gaze_tgt_x = sb_clamp_i8(bias_x + jx);
        sb->gaze_tgt_y = sb_clamp_i8(bias_y + jy);
        sb->sacc_from_x_q4 = sb->gaze_x_q4;
        sb->sacc_from_y_q4 = sb->gaze_y_q4;
        sb->sacc_start = clock;
        int32_t dist_q4 = sb->gaze_tgt_x * 16 - sb->gaze_x_q4;
        if (dist_q4 < 0) {
            dist_q4 = -dist_q4;
        }
        sb->sacc_dur_ms = (uint16_t)sb_clamp_i32(60 + dist_q4 / 8, 60, 170);
        sb->fix_next =
            clock + sb_rand_range(sb, p->fix_min_ms, p->fix_max_ms) *
                        SB_SAMPLES_PER_MS;
    }

    uint32_t e_ms = sb_ms_between(sb->sacc_start, clock);
    int32_t t_q8 = sb->sacc_dur_ms > 0
                       ? (int32_t)(e_ms * 256U / sb->sacc_dur_ms)
                       : 256;
    int32_t k = sb_ease_smooth_q8(t_q8);
    sb->gaze_x_q4 = (int16_t)(sb->sacc_from_x_q4 +
                              ((sb->gaze_tgt_x * 16 - sb->sacc_from_x_q4) *
                               k) / 256);
    sb->gaze_y_q4 = (int16_t)(sb->sacc_from_y_q4 +
                              ((sb->gaze_tgt_y * 16 - sb->sacc_from_y_q4) *
                               k) / 256);
}

static uint8_t sb_breath_update(sb_t *sb, uint32_t clock,
                               const sb_state_profile_t *p,
                               uint8_t breath_boost)
{
    uint32_t period_ms = p->breath_period_ms;
    if (period_ms == 0) {
        period_ms = sb->config.breath_period_ms;
    }
    uint32_t dt = clock - sb->breath_last_clock;
    sb->breath_last_clock = clock;
    /* dt in samples; one turn = period_ms * 16 samples. */
    uint32_t denom = period_ms * SB_SAMPLES_PER_MS;
    sb->breath_phase_q16 += (uint32_t)((uint64_t)dt * 65536U / denom);
    uint32_t ph = (sb->breath_phase_q16 >> 8) & 0xFFU; /* 0..255 turn */
    /* Inhale 42% rising, exhale 58% falling, smoothed. */
    int32_t v;
    if (ph < 107U) {
        v = sb_ease_smooth_q8((int32_t)(ph * 256U / 107U));
    } else {
        v = 256 - sb_ease_smooth_q8((int32_t)((ph - 107U) * 256U / 149U));
    }
    int32_t amp = p->breath_amp + breath_boost;
    return sb_clamp_u8(v * amp / 256);
}

static void sb_residual_decay(sb_t *sb, uint32_t dt_ms)
{
    int16_t *fields[3] = {&sb->res_head_x_q4, &sb->res_head_y_q4,
                          &sb->res_tilt_q4};
    for (int i = 0; i < 3; ++i) {
        int32_t v = *fields[i];
        if (v == 0) {
            continue;
        }
        int32_t step = v * (int32_t)dt_ms / (int32_t)sb->config.settle_ms;
        if (step == 0) {
            step = v > 0 ? 1 : -1;
        }
        v -= step;
        *fields[i] = (int16_t)v;
    }
}

/* ------------------------------------------------------------------ */
/* Tick                                                                */
/* ------------------------------------------------------------------ */

static uint8_t sb_fx_weight(const sb_t *sb, uint32_t clock, uint8_t fx)
{
    uint32_t in_state_ms = sb_ms_between(sb->state_since, clock);
    switch (fx) {
    case SB_FX_DOTS: {
        uint32_t step = (in_state_ms / 400U) % 4U;
        return (uint8_t)(64U + step * 64U > 255U ? 255U : 64U + step * 64U);
    }
    case SB_FX_ZZZ: {
        uint32_t ph = (in_state_ms % 2400U) * 256U / 2400U;
        return (uint8_t)(128 + sb_ease_pulse_q8((int32_t)ph) * 127 / 256);
    }
    case SB_FX_ALERT:
        return (in_state_ms / 320U) % 2U == 0U ? 255 : 96;
    case SB_FX_SPARK: {
        uint32_t ph = (in_state_ms % 600U) * 256U / 600U;
        return (uint8_t)(160 + sb_ease_pulse_q8((int32_t)ph) * 95 / 256);
    }
    case SB_FX_ERROR:
        return 230;
    default:
        return 255;
    }
}

void sb_tick(sb_t *sb, uint32_t sample_clock, sb_frame_t *out)
{
    if (out == NULL) {
        return;
    }
    if (sample_clock < sb->last_clock) {
        /* Backward jump: deterministic re-arm at the new clock. */
        sb_reset_schedules(sb, sample_clock);
        sb->state_since = sample_clock;
        sb->art_at = sample_clock;
        sb->speech_expect_until = sample_clock;
        sb->sacc_from_x_q4 = sb->gaze_x_q4;
        sb->sacc_from_y_q4 = sb->gaze_y_q4;
        sb->sacc_start = sample_clock;
    }
    uint32_t dt_ms = sb_ms_between(sb->last_clock, sample_clock);

    /* Transient state auto-decay. */
    if (sb->state == SB_STATE_INTERRUPTED &&
        sb_ms_between(sb->state_since, sample_clock) >=
            sb->config.interrupted_ms) {
        sb_transition(sb, SB_STATE_LISTENING, sample_clock);
    }

    const sb_state_profile_t *p = &sb_state_profiles[sb->state];

    /* Emotion envelope (and queued-cue promotion). */
    uint32_t emotion_w = 0;
    uint8_t emotion = FACE_EXPRESSION_NEUTRAL;
    bool cue_gaze = false;
    uint8_t cue_target = FACE_GAZE_AUTO;
    if (sb->cue_active) {
        bool finished = false;
        emotion_w = sb_cue_weight(&sb->cue, sample_clock, &finished);
        if (finished) {
            if (sb->cue_pending) {
                sb->cue = sb->cue_queued;
                sb->cue_pending = 0;
                if (sb->cue.start_sample < sample_clock) {
                    sb->cue.start_sample = sample_clock;
                }
                emotion_w = 0;
            } else {
                sb->cue_active = 0;
            }
        }
        if (sb->cue_active) {
            emotion = sb->cue.expression < FACE_EXPRESSION_COUNT
                          ? sb->cue.expression
                          : (uint8_t)FACE_EXPRESSION_NEUTRAL;
            if (emotion_w > 0 && sb->cue.gaze_target != FACE_GAZE_AUTO) {
                cue_gaze = true;
                cue_target = sb->cue.gaze_target;
            }
        }
    }
    const sb_emotion_accent_t *accent = &sb_emotion_accents[emotion];

    /* Channels (order fixed for determinism). */
    sb_blink_update(sb, sample_clock, p);
    sb_clips_schedule(sb, sample_clock, p, accent);
    sb_gaze_update(sb, sample_clock, p, accent, emotion_w, cue_gaze,
                   cue_target);
    sb_residual_decay(sb, dt_ms);

    sb_clip_out_t clip_out;
    sb_clip_eval(sb, sample_clock, &clip_out);
    if (clip_out.blink_now != 0U &&
        (sb->blink_double & SB_BLINK_ACTIVE) == 0U) {
        sb->blink_start = sample_clock;
        sb->blink_hold_ms = 30;
        sb->blink_double = SB_BLINK_ACTIVE;
    }

    uint8_t breath =
        sb_breath_update(sb, sample_clock, p, clip_out.breath_boost);

    /* Talk energy: live articulation, else transcript-paced synth. */
    bool art_fresh =
        sample_clock >= sb->art_at &&
        sb_ms_between(sb->art_at, sample_clock) <= SB_ART_FRESH_MS;
    int32_t talk_target = 0;
    if (art_fresh) {
        talk_target = sb->art.mouth_open;
    } else if (sb->state == SB_STATE_ASSISTANT_SPEAKING &&
               sample_clock < sb->speech_expect_until) {
        uint32_t ms = sb_ms_between(sb->state_since, sample_clock);
        uint32_t idx = ms / 280U;
        uint32_t ph = (ms % 280U) * 256U / 280U;
        uint32_t h = idx * 2654435761U + 0x9E37U;
        h ^= h >> 13;
        uint32_t amp = 96U + (h & 63U);
        talk_target = sb_ease_pulse_q8((int32_t)ph) * (int32_t)amp / 256;
    }
    int32_t level_q8 = sb->mouth_level_q8;
    int32_t target_q8 = talk_target * 256;
    int32_t alpha;
    if (target_q8 > level_q8) {
        alpha = sb_clamp_i32((int32_t)dt_ms * 10, 0, 256);
    } else {
        alpha = sb_clamp_i32((int32_t)dt_ms * 3, 0, 256);
    }
    level_q8 += (target_q8 - level_q8) * alpha / 256;
    sb->mouth_level_q8 = (uint16_t)sb_clamp_i32(level_q8, 0, 65535);
    uint8_t talk = (uint8_t)(sb->mouth_level_q8 >> 8);

    /* ---------------- compose the frame ---------------- */
    memset(out, 0, sizeof(*out));
    out->sample_clock = sample_clock;
    out->frame_index = sb->frame_index++;
    out->schema_version = SB_SCHEMA_VERSION;
    out->state = sb->state;
    out->clip = sb->clip;
    out->emotion = emotion;
    out->emotion_weight = (uint8_t)sb_clamp_i32((int32_t)emotion_w, 0, 255);
    out->breath = breath;
    out->talk = talk;

    /* Gaze (channel + clip contribution). */
    int32_t gaze_x =
        (sb->gaze_x_q4 + clip_out.gaze_dx_q4) / 16;
    int32_t gaze_y =
        (sb->gaze_y_q4 + clip_out.gaze_dy_q4) / 16;
    out->gaze_x = sb_clamp_i8(sb_clamp_i32(gaze_x, -64, 64));
    out->gaze_y = sb_clamp_i8(sb_clamp_i32(gaze_y, -64, 64));

    /* Attention: profile baseline, onset boost while user speaks. */
    int32_t attention = p->attention;
    if (sb->state == SB_STATE_USER_SPEAKING &&
        sb_ms_between(sb->state_since, sample_clock) < 600U) {
        attention += 30;
    }
    out->attention = sb_clamp_u8(attention);

    /* Blink with slight right-lid lag and emotion lid droop. */
    uint8_t blink_l = sb_blink_value(sb, sample_clock, p, 0);
    uint8_t blink_r = sb_blink_value(sb, sample_clock, p, 12);
    out->blink = blink_l > blink_r ? blink_l : blink_r;
    int32_t droop =
        (int32_t)accent->lid_soften * (int32_t)emotion_w / 255;

    /* Head pose = clip + residual + breath bob + talk bob. */
    int32_t head_dx_q4 = clip_out.head_dx_q4 + sb->res_head_x_q4;
    int32_t head_dy_q4 = clip_out.head_dy_q4 + sb->res_head_y_q4;
    int32_t tilt_q4 = clip_out.tilt_q4 + sb->res_tilt_q4 +
                      ((int32_t)accent->head_tilt * (int32_t)emotion_w /
                       255) *
                          16;
    head_dy_q4 += -((int32_t)breath * 10) / 255; /* rise on inhale */
    head_dy_q4 += (int32_t)talk / 40;            /* subtle speech bob */
    head_dx_q4 += (sb->gaze_x_q4 + clip_out.gaze_dx_q4) / 20; /* follow */

    int32_t body_dy_q4 =
        clip_out.body_dy_q4 - ((int32_t)breath * 16) / 255;
    int32_t stretch = clip_out.stretch + (int32_t)breath / 40;

    /* Eye cells: blink dominates, then clip/emotion overrides. */
    uint8_t eye_base = p->eye_base;
    if (clip_out.eye_override != SB_NO_OVERRIDE) {
        eye_base = clip_out.eye_override;
    } else if (accent->eye_override != SB_NO_OVERRIDE &&
               emotion_w >= accent->eye_min_weight) {
        eye_base = accent->eye_override;
    }
    int32_t lid_l = blink_l + droop;
    int32_t lid_r = blink_r + droop;
    uint8_t eye_l = eye_base;
    uint8_t eye_r = eye_base;
    if (lid_l > 200) {
        eye_l = SB_EYE_CLOSED;
    } else if (lid_l > 90 && eye_base != SB_EYE_CLOSED) {
        eye_l = SB_EYE_SOFT;
    }
    if (lid_r > 200) {
        eye_r = SB_EYE_CLOSED;
    } else if (lid_r > 90 && eye_base != SB_EYE_CLOSED) {
        eye_r = SB_EYE_SOFT;
    }

    /* Brows: emotion accents override state defaults; clips bias. */
    uint8_t brow_l = k_state_brow[sb->state][0];
    uint8_t brow_r = k_state_brow[sb->state][1];
    if (emotion_w >= 96) {
        brow_l = accent->brow_l;
        brow_r = accent->brow_r;
    }
    if (clip_out.brow_bias >= 16) {
        brow_l = SB_BROW_RAISED;
        brow_r = SB_BROW_RAISED;
    } else if (clip_out.brow_bias <= -16) {
        brow_l = SB_BROW_KNIT;
        brow_r = SB_BROW_KNIT;
    }

    /* Head cell: clip override, then tilt, then gaze direction. */
    uint8_t head_cell = SB_HEAD_FRONT;
    int32_t tilt_units = tilt_q4 / 16;
    if (clip_out.head_cell != SB_NO_OVERRIDE) {
        head_cell = clip_out.head_cell;
    } else if (tilt_units <= -6) {
        head_cell = SB_HEAD_TILT_L;
    } else if (tilt_units >= 6) {
        head_cell = SB_HEAD_TILT_R;
    } else if (out->gaze_x <= -26) {
        head_cell = SB_HEAD_QUARTER_L;
    } else if (out->gaze_x >= 26) {
        head_cell = SB_HEAD_QUARTER_R;
    } else if (out->gaze_y <= -30) {
        head_cell = SB_HEAD_UP;
    } else if (out->gaze_y >= 30) {
        head_cell = SB_HEAD_DOWN;
    }

    /* FX: emotion accent may override the state badge. */
    uint8_t fx = p->fx_cell;
    if (accent->fx != SB_NO_OVERRIDE && emotion_w >= accent->fx_min_weight) {
        fx = accent->fx;
    }
    if (fx == SB_FX_ZZZ && sb->state == SB_STATE_DISCONNECTED &&
        sb_ms_between(sb->state_since, sample_clock) < 4000U) {
        fx = SB_FX_NONE; /* only snore once clearly asleep */
    }

    sb_layer_t *L = out->layers;
    L[SB_LAYER_BODY].cell = 0;
    L[SB_LAYER_BODY].weight = 255;
    L[SB_LAYER_BODY].dy_q2 = sb_clamp_i8(body_dy_q4 / 4);
    L[SB_LAYER_BODY].stretch = sb_clamp_i8(stretch);

    L[SB_LAYER_HEAD].cell = head_cell;
    L[SB_LAYER_HEAD].weight = 255;
    L[SB_LAYER_HEAD].dx_q2 = sb_clamp_i8(head_dx_q4 / 4);
    L[SB_LAYER_HEAD].dy_q2 = sb_clamp_i8(head_dy_q4 / 4);
    L[SB_LAYER_HEAD].rot = sb_clamp_i8(tilt_units);
    L[SB_LAYER_HEAD].stretch = sb_clamp_i8(stretch);

    int8_t eye_dx = sb_clamp_i8((int32_t)out->gaze_x / 16);
    int8_t eye_dy = sb_clamp_i8((int32_t)out->gaze_y / 16);
    L[SB_LAYER_EYE_L].cell = eye_l;
    L[SB_LAYER_EYE_L].weight = sb_clamp_u8(255 - lid_l / 4);
    L[SB_LAYER_EYE_L].dx_q2 = eye_dx;
    L[SB_LAYER_EYE_L].dy_q2 = eye_dy;
    L[SB_LAYER_EYE_R].cell = eye_r;
    L[SB_LAYER_EYE_R].weight = sb_clamp_u8(255 - lid_r / 4);
    L[SB_LAYER_EYE_R].dx_q2 = eye_dx;
    L[SB_LAYER_EYE_R].dy_q2 = eye_dy;

    int32_t brow_lift = 0;
    if (brow_l == SB_BROW_RAISED || brow_r == SB_BROW_RAISED) {
        brow_lift = 2 + (int32_t)emotion_w / 64;
    }
    brow_lift += clip_out.brow_bias / 8;
    L[SB_LAYER_BROW_L].cell = brow_l;
    L[SB_LAYER_BROW_L].weight = sb_clamp_u8(96 + (int32_t)emotion_w);
    L[SB_LAYER_BROW_L].dy_q2 = sb_clamp_i8(-brow_lift);
    L[SB_LAYER_BROW_R].cell = brow_r;
    L[SB_LAYER_BROW_R].weight = sb_clamp_u8(96 + (int32_t)emotion_w);
    L[SB_LAYER_BROW_R].dy_q2 = sb_clamp_i8(-brow_lift);

    /* Mouth: emotion rest band + debounced articulation. */
    uint8_t rest_cell = SB_MOUTH_REST;
    if (emotion_w >= 110) {
        rest_cell = accent->mouth_rest;
    }
    sb_mouth_update(sb, sample_clock, rest_cell, talk,
                    &L[SB_LAYER_MOUTH]);
    /* Mouth rides with the head. */
    L[SB_LAYER_MOUTH].dx_q2 =
        sb_clamp_i8(L[SB_LAYER_MOUTH].dx_q2 + head_dx_q4 / 4);
    L[SB_LAYER_MOUTH].dy_q2 =
        sb_clamp_i8(L[SB_LAYER_MOUTH].dy_q2 + head_dy_q4 / 4);

    L[SB_LAYER_FX].cell = fx;
    L[SB_LAYER_FX].weight =
        fx == SB_FX_NONE ? 0 : sb_fx_weight(sb, sample_clock, fx);

    sb->last_clock = sample_clock;
}
