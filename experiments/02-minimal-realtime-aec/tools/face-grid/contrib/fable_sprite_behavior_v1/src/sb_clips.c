#include <string.h>

#include "sb_internal.h"

/*
 * Micro-clip channel: weighted, interruptible set pieces layered over
 * the continuous blink/breath/gaze channels. Every clip is authored to
 * start and end at zero contribution, so a clip that runs to completion
 * needs no cleanup; preemption folds the instantaneous contribution
 * into the residual decay so nothing pops.
 */

/* Duration draw ranges per clip (ms). */
typedef struct {
    uint16_t min_ms;
    uint16_t max_ms;
} sb_clip_span_t;

static const sb_clip_span_t k_clip_span[SB_CLIP_COUNT] = {
    [SB_CLIP_SACCADE] = {160, 220},
    [SB_CLIP_GLANCE] = {900, 1600},
    [SB_CLIP_ACK_NOD] = {550, 800},
    [SB_CLIP_HEAD_TILT] = {1400, 2600},
    [SB_CLIP_HEAD_TURN] = {1000, 1800},
    [SB_CLIP_BREATH_SIGH] = {1800, 2600},
    [SB_CLIP_SETTLE] = {550, 700},
    [SB_CLIP_LEAN_IN] = {1200, 1800},
    [SB_CLIP_STARTLE] = {380, 600},
    [SB_CLIP_DROOP] = {2200, 3800},
    [SB_CLIP_SEARCH] = {1200, 2000},
    [SB_CLIP_GESTURE_NOD] = {600, 1200},
    [SB_CLIP_GESTURE_SHAKE] = {600, 1200},
    [SB_CLIP_GESTURE_BOUNCE] = {600, 1200},
};

void sb_clip_start(sb_t *sb, uint8_t clip, uint32_t clock, uint16_t dur_ms,
                   int8_t dir, uint8_t amp)
{
    if (clip >= SB_CLIP_COUNT) {
        return;
    }
    if (dur_ms == 0) {
        dur_ms = (uint16_t)sb_rand_range(sb, k_clip_span[clip].min_ms,
                                         k_clip_span[clip].max_ms);
    }
    sb->clip = clip;
    sb->clip_start = clock;
    sb->clip_dur_ms = dur_ms;
    sb->clip_dir = dir >= 0 ? 1 : -1;
    sb->clip_amp = amp;
}

/* Attack/sustain/release window in Q8 (in_q8 rise, out_q8 fall). */
static int32_t sb_asr_q8(int32_t t_q8, int32_t in_q8, int32_t out_q8)
{
    if (t_q8 <= 0 || t_q8 >= 256) {
        return 0;
    }
    if (t_q8 < in_q8) {
        return sb_ease_smooth_q8(t_q8 * 256 / in_q8);
    }
    if (t_q8 > 256 - out_q8) {
        return sb_ease_smooth_q8((256 - t_q8) * 256 / out_q8);
    }
    return 256;
}

/* n raised-cosine bobs over the clip: 0..256 each. */
static int32_t sb_bobs_q8(int32_t t_q8, int32_t n)
{
    int32_t ph = (t_q8 * n) % 256;
    return sb_ease_pulse_q8(ph);
}

void sb_clip_eval(const sb_t *sb, uint32_t clock, sb_clip_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->eye_override = SB_NO_OVERRIDE;
    out->head_cell = SB_NO_OVERRIDE;
    if (sb->clip == SB_CLIP_NONE || sb->clip_dur_ms == 0) {
        return;
    }
    uint32_t e_ms = sb_ms_between(sb->clip_start, clock);
    if (e_ms >= sb->clip_dur_ms) {
        return;
    }
    int32_t t = (int32_t)(e_ms * 256U / sb->clip_dur_ms); /* 0..255 */
    int32_t dir = sb->clip_dir;
    int32_t amp = sb->clip_amp;

    switch (sb->clip) {
    case SB_CLIP_SACCADE:
        /* Marker clip: the gaze channel does the moving. */
        break;
    case SB_CLIP_GLANCE: {
        /* Look away, dwell, return; eyes lead, head follows ~30%. */
        int32_t k = sb_asr_q8(t, 72, 88);
        int32_t gx = dir * (amp * 44 / 255) * k / 256; /* gaze units */
        out->gaze_dx_q4 = (int16_t)(gx * 16);
        out->gaze_dy_q4 = (int16_t)(-(amp * 6 / 255) * k * 16 / 256);
        out->head_dx_q4 = (int16_t)(gx * 3 / 2);
        break;
    }
    case SB_CLIP_ACK_NOD: {
        /* Two small agreeing dips with a brow lift on the first. */
        int32_t env = sb_asr_q8(t, 40, 64);
        int32_t bob = sb_bobs_q8(t, 2);
        out->head_dy_q4 =
            (int16_t)((amp * 40 / 255) * bob / 256 * env / 256);
        out->brow_bias = (int8_t)(t < 128 ? 20 : 8);
        break;
    }
    case SB_CLIP_HEAD_TILT: {
        int32_t k = sb_asr_q8(t, 60, 72);
        int32_t units = dir * (8 + amp * 6 / 255);
        out->tilt_q4 = (int16_t)(units * k * 16 / 256);
        out->head_dy_q4 = (int16_t)(k * 8 / 256);
        break;
    }
    case SB_CLIP_HEAD_TURN: {
        int32_t k = sb_asr_q8(t, 64, 80);
        out->head_dx_q4 = (int16_t)(dir * (amp * 40 / 255) * k / 256);
        out->gaze_dx_q4 = (int16_t)(dir * (amp * 30 / 255) * k * 16 / 256);
        if (k > 176) {
            out->head_cell = dir < 0 ? SB_HEAD_QUARTER_L
                                     : SB_HEAD_QUARTER_R;
        }
        break;
    }
    case SB_CLIP_BREATH_SIGH: {
        /* One deep breath; shoulders drop and lids soften on exhale. */
        int32_t k = sb_ease_pulse_q8(t < 118 ? t * 256 / 236
                                             : 128 + (t - 118) * 128 / 138);
        out->breath_boost = (uint8_t)(k * 70 / 256);
        if (t >= 118) {
            int32_t drop = sb_ease_smooth_q8((t - 118) * 256 / 138);
            out->body_dy_q4 = (int16_t)(drop * 20 / 256);
            out->head_dy_q4 = (int16_t)(drop * 12 / 256);
            out->eye_override = SB_EYE_SOFT;
        } else {
            out->stretch = (int8_t)(k * 6 / 256);
        }
        break;
    }
    case SB_CLIP_SETTLE: {
        /* Posture relax: one soft downward exhale-like beat. */
        int32_t k = sb_ease_pulse_q8(t);
        out->head_dy_q4 = (int16_t)(k * 10 / 256);
        out->body_dy_q4 = (int16_t)(k * 8 / 256);
        break;
    }
    case SB_CLIP_LEAN_IN: {
        int32_t k = sb_asr_q8(t, 72, 84);
        out->head_dy_q4 = (int16_t)(k * (amp * 24 / 255) / 256);
        out->stretch = (int8_t)(k * 8 / 256);
        out->gaze_dy_q4 = (int16_t)(-(k * 4 / 256) * 16);
        break;
    }
    case SB_CLIP_STARTLE: {
        /* Recoil up-back with squash, wide eyes, one quick blink. */
        int32_t k = sb_asr_q8(t, 44, 128);
        out->head_dy_q4 = (int16_t)(-(amp * 48 / 255) * k / 256);
        out->stretch = (int8_t)(-(k * 10 * amp / 255) / 256);
        out->eye_override = SB_EYE_WIDE;
        out->brow_bias = 24;
        if (e_ms < 40U) {
            out->blink_now = 1;
        }
        break;
    }
    case SB_CLIP_DROOP: {
        /* Nod off gradually, then catch yourself at the end. */
        if (t < 218) {
            int32_t k = sb_ease_smooth_q8(t * 256 / 218);
            out->head_dy_q4 = (int16_t)(k * 40 / 256);
            out->tilt_q4 = (int16_t)(dir * k * 3 * 16 / 256);
            if (k > 90) {
                out->eye_override = SB_EYE_SOFT;
            }
        } else {
            /* the catch: quick recovery, alert eyes, tiny blink */
            int32_t k = 256 - sb_ease_out_q8((t - 218) * 256 / 38);
            out->head_dy_q4 = (int16_t)(k * 40 / 256);
            out->eye_override = SB_EYE_OPEN;
            if (t - 218 < 8) {
                out->blink_now = 1;
            }
        }
        break;
    }
    case SB_CLIP_SEARCH: {
        /* Sweep one side, dwell, sweep to the other; head follows. */
        int32_t pos; /* -256..256 */
        if (t < 96) {
            pos = -sb_ease_smooth_q8(t * 256 / 96);
        } else if (t < 144) {
            pos = -256;
        } else if (t < 240) {
            pos = -256 + 2 * sb_ease_smooth_q8((t - 144) * 256 / 96);
        } else {
            pos = 256 - 2 * sb_ease_smooth_q8((t - 240) * 256 / 16);
        }
        int32_t gx = dir * (amp * 34 / 255) * pos / 256;
        out->gaze_dx_q4 = (int16_t)(gx * 16);
        out->head_dx_q4 = (int16_t)(gx * 8 / 5);
        break;
    }
    case SB_CLIP_GESTURE_NOD: {
        int32_t env = sb_asr_q8(t, 32, 48);
        int32_t bob = sb_bobs_q8(t, 3);
        out->head_dy_q4 =
            (int16_t)((amp * 56 / 255) * bob / 256 * env / 256);
        out->brow_bias = 16;
        break;
    }
    case SB_CLIP_GESTURE_SHAKE: {
        /* Three half-cycles left/right. */
        int32_t env = sb_asr_q8(t, 32, 48);
        int32_t ph = (t * 3) % 512; /* triangle -256..256 */
        int32_t tri = ph < 256 ? ph * 2 - 256 : 768 - ph * 2;
        int32_t dx = dir * (amp * 36 / 255) * tri / 256 * env / 256;
        out->head_dx_q4 = (int16_t)dx;
        out->gaze_dx_q4 = (int16_t)(dx * 8);
        if (dx > 20) {
            out->head_cell = SB_HEAD_QUARTER_R;
        } else if (dx < -20) {
            out->head_cell = SB_HEAD_QUARTER_L;
        }
        break;
    }
    case SB_CLIP_GESTURE_BOUNCE: {
        int32_t env = sb_asr_q8(t, 32, 40);
        int32_t bob = sb_bobs_q8(t, 2);
        int32_t lift = (amp * 48 / 255) * bob / 256 * env / 256;
        out->head_dy_q4 = (int16_t)(-lift);
        out->body_dy_q4 = (int16_t)(-lift * 5 / 8);
        out->stretch = (int8_t)(bob > 128 ? 8 : -6);
        break;
    }
    default:
        break;
    }
}

void sb_clip_preempt(sb_t *sb, uint32_t clock)
{
    if (sb->clip == SB_CLIP_NONE) {
        return;
    }
    sb_clip_out_t now;
    sb_clip_eval(sb, clock, &now);
    sb->res_head_x_q4 = (int16_t)(sb->res_head_x_q4 + now.head_dx_q4);
    sb->res_head_y_q4 = (int16_t)(sb->res_head_y_q4 + now.head_dy_q4);
    sb->res_tilt_q4 = (int16_t)(sb->res_tilt_q4 + now.tilt_q4);
    /* Fold the gaze contribution into the gaze channel so it eases
     * back to the fixation target instead of popping. */
    if (now.gaze_dx_q4 != 0 || now.gaze_dy_q4 != 0) {
        sb->gaze_x_q4 = (int16_t)(sb->gaze_x_q4 + now.gaze_dx_q4);
        sb->gaze_y_q4 = (int16_t)(sb->gaze_y_q4 + now.gaze_dy_q4);
        sb->sacc_from_x_q4 = sb->gaze_x_q4;
        sb->sacc_from_y_q4 = sb->gaze_y_q4;
        sb->sacc_start = clock;
        sb->sacc_dur_ms = 140;
    }
    sb->clip = SB_CLIP_NONE;
}

void sb_clips_schedule(sb_t *sb, uint32_t clock,
                       const sb_state_profile_t *profile,
                       const sb_emotion_accent_t *accent)
{
    /* Natural clip completion. */
    if (sb->clip != SB_CLIP_NONE &&
        sb_ms_between(sb->clip_start, clock) >= sb->clip_dur_ms) {
        sb->clip = SB_CLIP_NONE;
        uint32_t slot = (uint32_t)sb->config.clip_slot_ms * 100U /
                        (accent->energy_pct != 0 ? accent->energy_pct : 100U);
        sb->clip_next =
            clock + sb_rand_range(sb, slot / 2, slot + slot / 2) *
                        SB_SAMPLES_PER_MS;
    }

    /* Periodic acknowledgement while the user keeps talking. */
    if (sb->state == SB_STATE_USER_SPEAKING && clock >= sb->ack_next) {
        if (sb->clip == SB_CLIP_NONE &&
            sb_rand_pct(sb) < sb->config.ack_nod_pct) {
            sb_clip_start(sb, SB_CLIP_ACK_NOD, clock, 0, 1,
                          (uint8_t)sb_rand_range(sb, 100, 170));
        }
        sb->ack_next =
            clock + sb_rand_range(sb, 3200, 6400) * SB_SAMPLES_PER_MS;
    }

    if (sb->clip != SB_CLIP_NONE || clock < sb->clip_next) {
        return;
    }

    /* Weighted decision slot. */
    uint32_t slot = (uint32_t)sb->config.clip_slot_ms * 100U /
                    (accent->energy_pct != 0 ? accent->energy_pct : 100U);
    sb->clip_next = clock + sb_rand_range(sb, slot / 2, slot + slot / 2) *
                                SB_SAMPLES_PER_MS;

    uint8_t none_pct = profile->clip_none_pct > sb->config.idle_none_pct
                           ? profile->clip_none_pct
                           : sb->config.idle_none_pct;
    if (sb_rand_pct(sb) < none_pct) {
        return;
    }
    uint32_t total = 0;
    for (int i = 0; i < SB_CLIP_COUNT; ++i) {
        total += profile->clip_weights[i];
    }
    if (total == 0) {
        return;
    }
    uint32_t r = sb_rand(sb) % total;
    uint8_t pick = SB_CLIP_NONE;
    for (int i = 0; i < SB_CLIP_COUNT; ++i) {
        uint32_t w = profile->clip_weights[i];
        if (r < w) {
            pick = (uint8_t)i;
            break;
        }
        r -= w;
    }
    if (pick == SB_CLIP_NONE) {
        return;
    }
    int8_t dir = (sb_rand(sb) & 1U) != 0U ? 1 : -1;
    uint8_t amp = (uint8_t)sb_rand_range(sb, 110, 220);
    if (pick == SB_CLIP_SACCADE) {
        /* Retarget the gaze channel now; the clip is only a marker. */
        sb->fix_next = clock;
    }
    sb_clip_start(sb, pick, clock, 0, dir, amp);
}
