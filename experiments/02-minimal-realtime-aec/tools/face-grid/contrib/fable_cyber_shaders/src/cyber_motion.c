#include "cyber_internal.h"

/*
 * Deterministic idle motion. Every quantity is a pure function of the
 * 16 kHz sample clock and the keyframe, so replaying a clock sequence
 * reproduces the exact frame sequence on any platform.
 *
 * The animation vocabulary follows the classic procedural-face playbook
 * (Cozmo/Vector-style saccades and blinks, m5stack-avatar breathing):
 *  - blinks on a jittered ~3.4 s schedule with a fast close, a short
 *    hold, a slower open, occasional double blinks, and a subtle
 *    anticipation widen just before the lids drop;
 *  - saccades: gaze retargets on a jittered ~1.4 s schedule with a fast
 *    smoothstep transition and a slow micro-drift between jumps;
 *  - breathing: a ~4.2 s sinusoidal bob that also modulates glow;
 *  - speech: keyframe mouth values dominate, the gaze centres and
 *    quickens, the lids squint slightly on strong syllables, and glow
 *    pulses with mouth_open.
 */

enum {
    CYBER_BLINK_PERIOD_MS = 3400,
    CYBER_BLINK_CLOSE_MS = 70,
    CYBER_BLINK_HOLD_MS = 40,
    CYBER_BLINK_OPEN_MS = 110,
    CYBER_BLINK_TOTAL_MS =
        CYBER_BLINK_CLOSE_MS + CYBER_BLINK_HOLD_MS + CYBER_BLINK_OPEN_MS,
    CYBER_SACCADE_IDLE_MS = 1400,
    CYBER_SACCADE_SPEAK_MS = 900,
    CYBER_SACCADE_MOVE_MS = 90,
    CYBER_BREATH_PERIOD_MS = 4200,
};

/* Integer smoothstep: u in Q8 -> eased Q8 (3u^2 - 2u^3). */
static uint32_t smoothstep_q8(uint32_t u_q8)
{
    if (u_q8 >= 256u) {
        return 256u;
    }
    uint32_t u2 = u_q8 * u_q8;              /* Q16 */
    uint32_t u3 = (u2 * u_q8) >> 8;         /* Q16 */
    return (3u * u2 - 2u * u3) >> 8;        /* Q8  */
}

/*
 * Lid closure contributed by the autonomous blink engine at time t_ms,
 * 0 (open) .. 255 (closed). Checks the current and previous schedule
 * cycles so a blink straddling a cycle boundary is not clipped.
 */
static uint32_t blink_closure(uint32_t t_ms)
{
    uint32_t closure = 0;
    uint32_t cycle = t_ms / CYBER_BLINK_PERIOD_MS;
    for (uint32_t back = 0; back < 2u; ++back) {
        uint32_t c = cycle - back;
        if (back > cycle) {
            break;
        }
        uint32_t h = cyber_hash32(c * 2654435761u + 0x1234u);
        uint32_t start =
            c * CYBER_BLINK_PERIOD_MS + 350u + (h & 0x7FFu);
        uint32_t repeats = ((h >> 12) & 7u) == 0u ? 2u : 1u;
        for (uint32_t r = 0; r < repeats; ++r) {
            uint32_t s = start + r * (CYBER_BLINK_TOTAL_MS + 90u);
            if (t_ms < s) {
                continue;
            }
            uint32_t dt = t_ms - s;
            if (dt >= CYBER_BLINK_TOTAL_MS) {
                continue;
            }
            uint32_t phase_closure;
            if (dt < CYBER_BLINK_CLOSE_MS) {
                phase_closure = (dt * 255u) / CYBER_BLINK_CLOSE_MS;
            } else if (dt < CYBER_BLINK_CLOSE_MS + CYBER_BLINK_HOLD_MS) {
                phase_closure = 255u;
            } else {
                uint32_t od =
                    dt - CYBER_BLINK_CLOSE_MS - CYBER_BLINK_HOLD_MS;
                phase_closure = 255u - (od * 255u) / CYBER_BLINK_OPEN_MS;
            }
            if (phase_closure > closure) {
                closure = phase_closure;
            }
        }
    }
    return closure;
}

/* Anticipation widen: a small negative closure just before a blink. */
static int32_t blink_anticipation(uint32_t t_ms)
{
    uint32_t cycle = t_ms / CYBER_BLINK_PERIOD_MS;
    uint32_t h = cyber_hash32(cycle * 2654435761u + 0x1234u);
    uint32_t start = cycle * CYBER_BLINK_PERIOD_MS + 350u + (h & 0x7FFu);
    if (t_ms >= start || start - t_ms > 200u) {
        return 0;
    }
    uint32_t lead = start - t_ms; /* 1..200 */
    return -(int32_t)(((200u - lead) * 26u) / 200u);
}

/* Saccade target for schedule segment `seg`, in Q4 pixels. */
static void saccade_target(uint32_t seg, uint32_t speaking, int32_t *tx,
                           int32_t *ty)
{
    uint32_t h = cyber_hash32(seg * 0x9E3779B9u + 0xBEEFu);
    int32_t x = (int32_t)(h % 13u) - 6;        /* -6..6 px  */
    int32_t y = (int32_t)((h >> 8) % 7u) - 3;  /* -3..3 px  */
    if (speaking) {
        x /= 2;
        y /= 2;
    }
    *tx = x * 16;
    *ty = y * 16;
}

void cyber_motion_compute(const cyber_face_ctx_t *ctx,
                          const cyber_keyframe_t *kf,
                          uint32_t sample_clock, cyber_motion_t *m)
{
    uint32_t t_ms = sample_clock >> 4; /* 16 samples per millisecond */
    m->t_ms = t_ms;
    m->speaking = (kf->flags & CYBER_KEYFRAME_FLAG_SPEAKING) ? 1u : 0u;
    m->expression = kf->expression;

    /* ---- lids: keyframe openness x autonomous blink ---- */
    uint32_t closure;
    if (kf->flags & CYBER_KEYFRAME_FLAG_BLINKING) {
        closure = 255u;
    } else {
        closure = blink_closure(t_ms);
        int32_t anticipation = blink_anticipation(t_ms);
        int32_t squint = 0;
        if (m->speaking) {
            squint = (int32_t)((kf->mouth_open * 40u) >> 8);
        }
        if (kf->expression == CYBER_EXPRESSION_HAPPY) {
            squint += 30;
        } else if (kf->expression == CYBER_EXPRESSION_SURPRISED) {
            squint -= 44;
        }
        int32_t c = (int32_t)closure + squint + anticipation;
        closure = (uint32_t)cyber_clamp32(c, 0, 255);
    }
    /*
     * Blend blink closure with per-eye keyframe openness: the effective
     * openness is keyframe openness scaled by (255 - closure).
     */
    uint32_t open_left =
        ((uint32_t)kf->eye_left_open * (255u - closure)) >> 8;
    uint32_t open_right =
        ((uint32_t)kf->eye_right_open * (255u - closure)) >> 8;
    m->lid_close_left = 255u - open_left;
    m->lid_close_right = 255u - open_right;

    /* ---- gaze: keyframe look + saccades + micro-drift ---- */
    uint32_t seg_ms =
        m->speaking ? CYBER_SACCADE_SPEAK_MS : CYBER_SACCADE_IDLE_MS;
    uint32_t seg = t_ms / seg_ms;
    uint32_t seg_dt = t_ms - seg * seg_ms;
    int32_t prev_x, prev_y, cur_x, cur_y;
    saccade_target(seg == 0 ? 0 : seg - 1, m->speaking, &prev_x, &prev_y);
    saccade_target(seg, m->speaking, &cur_x, &cur_y);
    uint32_t ease = smoothstep_q8(seg_dt >= CYBER_SACCADE_MOVE_MS
                                      ? 256u
                                      : (seg_dt * 256u) /
                                            CYBER_SACCADE_MOVE_MS);
    int32_t sac_x = prev_x + (((cur_x - prev_x) * (int32_t)ease) >> 8);
    int32_t sac_y = prev_y + (((cur_y - prev_y) * (int32_t)ease) >> 8);
    /* Micro-drift, about 0.5 px at ~0.23 Hz, different x/y phases. */
    int32_t drift_x =
        (cyber_sin_q14(ctx, t_ms * 15u) * 8) >> 14;
    int32_t drift_y =
        (cyber_sin_q14(ctx, t_ms * 11u + 20000u) * 6) >> 14;
    /* Keyframe look: int8 full range maps to about +/-10 px. */
    int32_t look_x = ((int32_t)kf->look_x * 160) / 127;
    int32_t look_y = ((int32_t)kf->look_y * 112) / 127;
    m->gaze_x_q4 =
        cyber_clamp32(look_x + sac_x + drift_x, -14 * 16, 14 * 16);
    m->gaze_y_q4 =
        cyber_clamp32(look_y + sac_y + drift_y, -9 * 16, 9 * 16);

    /* ---- breathing bob and glow gain ---- */
    uint32_t breath_turns =
        ((t_ms % CYBER_BREATH_PERIOD_MS) * CYBER_TURN) /
        CYBER_BREATH_PERIOD_MS;
    int32_t breath = cyber_sin_q14(ctx, breath_turns); /* +/- 16384 */
    m->breath_y_q4 = (breath * 20) >> 14;              /* +/- 1.25px */
    int32_t gain = 256 + ((breath * 20) >> 14);
    if (m->speaking) {
        gain += (int32_t)((uint32_t)kf->mouth_open >> 2);
    }
    if (kf->expression == CYBER_EXPRESSION_ANGRY) {
        /* Agitated shimmer: fast small pulse. */
        gain += (cyber_sin_q14(ctx, t_ms * 320u) * 18) >> 14;
    }
    m->glow_gain_q8 = (uint32_t)cyber_clamp32(gain, 176, 352);

    /* ---- mouth geometry ---- */
    uint32_t round_q8 = kf->mouth_round;
    /* Wide half-width 10..26 px shrinking toward 7 px when rounded. */
    int32_t wide_hw = 160 + (int32_t)(((uint32_t)kf->mouth_width * 256u) >> 8);
    int32_t hw =
        wide_hw - (((wide_hw - 112) * (int32_t)round_q8) >> 8);
    /* Half-height 0.75..13 px, grown up to +40% when rounded. */
    int32_t hh = 12 + (int32_t)(((uint32_t)kf->mouth_open * 196u) >> 8);
    hh += (hh * (int32_t)round_q8 * 2) / (5 * 256);
    /* Press flattens toward a line. */
    hh = (hh * (int32_t)(256u - ((uint32_t)kf->mouth_press * 3u / 4u))) >>
         8;
    if (hh < 8) {
        hh = 8;
    }
    m->mouth_half_w_q4 = hw;
    m->mouth_half_h_q4 = hh;
    m->mouth_round_q8 = (int32_t)round_q8;

    int32_t curve = 0;
    if (kf->expression == CYBER_EXPRESSION_HAPPY) {
        curve = 5 * 16;
    } else if (kf->expression == CYBER_EXPRESSION_SAD) {
        curve = -5 * 16;
    } else if (kf->expression == CYBER_EXPRESSION_ANGRY) {
        curve = -2 * 16;
    }
    /* Idle micro-smile so a silent face is not a flat line. */
    if (!m->speaking && curve == 0) {
        curve = 24;
    }
    m->mouth_curve_q4 = curve;

    /* ---- brows ---- */
    int32_t brow = kf->brow; /* positive = raised */
    int32_t drop = -(brow * 10 * 16) / 127; /* raise lifts, furrow drops */
    int32_t tilt = 0;
    if (kf->expression == CYBER_EXPRESSION_ANGRY) {
        drop += 3 * 16;
        tilt = 200;
    } else if (kf->expression == CYBER_EXPRESSION_SAD) {
        tilt = -150;
    } else if (kf->expression == CYBER_EXPRESSION_SURPRISED) {
        drop -= 4 * 16;
    }
    if (brow < 0) {
        tilt += (-brow * 220) / 127;
    }
    m->brow_drop_q4 = cyber_clamp32(drop, -8 * 16, 8 * 16);
    m->brow_tilt_q8 = cyber_clamp32(tilt, -255, 255);
}
