#include "sb_internal.h"

/*
 * Mouth channel: maps OVR15 visemes (or continuous keyframe controls)
 * onto the compact sprite mouth bank, then rate-limits shape switching
 * with a debounce + minimum-hold machine.
 *
 * The frequency/amplitude split is deliberate: the *cell* changes
 * slowly (no flutter on noisy analysers), while openness amplitude
 * rides on the layer weight and jaw dy every tick, so loud speech
 * still reads loud between switches. Closing shapes are allowed to
 * land faster than opening shapes because visible plosives and
 * interruptions must not lag.
 */

static const uint8_t k_ovr15_to_cell[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = SB_MOUTH_AI,
    [FACE_VISEME_E] = SB_MOUTH_E,
    [FACE_VISEME_I] = SB_MOUTH_E,
    [FACE_VISEME_O] = SB_MOUTH_O,
    [FACE_VISEME_U] = SB_MOUTH_U,
    [FACE_VISEME_PP] = SB_MOUTH_MBP,
    [FACE_VISEME_SS] = SB_MOUTH_S,
    [FACE_VISEME_TH] = SB_MOUTH_LTH,
    [FACE_VISEME_DD] = SB_MOUTH_LTH,
    [FACE_VISEME_FF] = SB_MOUTH_FV,
    [FACE_VISEME_KK] = SB_MOUTH_E,
    [FACE_VISEME_NN] = SB_MOUTH_LTH,
    [FACE_VISEME_RR] = SB_MOUTH_O,
    [FACE_VISEME_CH] = SB_MOUTH_S,
    [FACE_VISEME_SIL] = SB_MOUTH_REST,
};

static bool sb_mouth_closed_family(uint8_t cell)
{
    return cell == SB_MOUTH_REST || cell == SB_MOUTH_SMILE ||
           cell == SB_MOUTH_FROWN || cell == SB_MOUTH_PRESS ||
           cell == SB_MOUTH_MBP;
}

static uint8_t sb_mouth_target(const sb_t *sb, uint32_t clock,
                               uint8_t rest_cell, uint8_t talk)
{
    bool art_fresh = clock >= sb->art_at &&
                     sb_ms_between(sb->art_at, clock) <= SB_ART_FRESH_MS;
    bool art_recent =
        clock >= sb->art_at &&
        sb_ms_between(sb->art_at, clock) <= sb->config.mouth_close_ms;
    bool speech_expected = clock < sb->speech_expect_until &&
                           sb->state == SB_STATE_ASSISTANT_SPEAKING;
    if (talk <= 18 && !(art_recent && sb->art.mouth_open > 18) &&
        !speech_expected) {
        return rest_cell;
    }

    if (art_fresh && sb->art_viseme < FACE_VISEME_COUNT) {
        uint8_t cell = k_ovr15_to_cell[sb->art_viseme];
        if (cell == SB_MOUTH_REST) {
            return rest_cell;
        }
        if (cell == SB_MOUTH_AI && talk > 225) {
            return SB_MOUTH_WIDE_OPEN;
        }
        return cell;
    }

    if (art_fresh) {
        const face_keyframe_t *kf = &sb->art;
        if (kf->mouth_press > 140) {
            return SB_MOUTH_MBP;
        }
        if (kf->mouth_teeth > 150) {
            return SB_MOUTH_S;
        }
        if (kf->mouth_round > 140) {
            return kf->mouth_open > 140 ? SB_MOUTH_O : SB_MOUTH_U;
        }
        if (kf->mouth_width > 180 && kf->mouth_open < 100) {
            return SB_MOUTH_E;
        }
        if (kf->mouth_open < 24) {
            return rest_cell;
        }
        if (kf->mouth_open > 225) {
            return SB_MOUTH_WIDE_OPEN;
        }
        return SB_MOUTH_AI;
    }

    /* Transcript-paced fallback speech: alternate simple shapes from
     * the deterministic talk envelope alone. */
    if (talk > 150) {
        return SB_MOUTH_AI;
    }
    if (talk > 60) {
        return SB_MOUTH_E;
    }
    return SB_MOUTH_MBP;
}

void sb_mouth_update(sb_t *sb, uint32_t clock, uint8_t rest_cell,
                     uint8_t talk, sb_layer_t *mouth_layer)
{
    uint8_t target = sb_mouth_target(sb, clock, rest_cell, talk);

    if (target != sb->mouth_pending) {
        sb->mouth_pending = target;
        sb->mouth_pending_since = clock;
    }
    if (target != sb->mouth_cell) {
        uint32_t pending_ms = sb_ms_between(sb->mouth_pending_since, clock);
        uint32_t held_ms = sb_ms_between(sb->mouth_since, clock);
        uint32_t debounce = sb->config.mouth_debounce_ms;
        uint32_t need_hold = sb->config.mouth_min_hold_ms;
        if (sb_mouth_closed_family(target)) {
            /* Plosives and stops may land sooner. */
            need_hold /= 2;
        } else if (sb_mouth_closed_family(sb->mouth_cell)) {
            /* Speech onset from rest: react fast. */
            need_hold /= 2;
            debounce /= 2;
        }
        if (pending_ms >= debounce && held_ms >= need_hold) {
            sb->mouth_cell = target;
            sb->mouth_since = clock;
        }
    }

    uint8_t cell = sb->mouth_cell;
    mouth_layer->cell = cell;
    mouth_layer->rot = 0;
    mouth_layer->stretch = 0;
    mouth_layer->dx_q2 = 0;

    if (sb_mouth_closed_family(cell) && cell != SB_MOUTH_MBP) {
        /* Emotion rest band. */
        mouth_layer->weight = 230;
        mouth_layer->dy_q2 = 0;
        return;
    }
    /* Articulating: amplitude rides on weight + jaw drop. */
    int32_t w = 90 + (int32_t)talk * 165 / 255;
    mouth_layer->weight = sb_clamp_u8(w);
    int32_t jaw = (int32_t)talk * (cell == SB_MOUTH_WIDE_OPEN ? 16 : 12) /
                  255;
    if (cell == SB_MOUTH_MBP) {
        jaw = 0;
    }
    mouth_layer->dy_q2 = sb_clamp_i8(jaw);
}

void sb_mouth_shut(sb_t *sb, uint32_t clock, uint8_t rest_cell)
{
    sb->mouth_cell =
        sb_mouth_closed_family(rest_cell) ? rest_cell : SB_MOUTH_PRESS;
    sb->mouth_pending = sb->mouth_cell;
    sb->mouth_since = clock;
    sb->mouth_pending_since = clock;
    sb->mouth_level_q8 /= 4;
}
