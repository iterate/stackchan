#include "pf_internal.h"

/*
 * Deterministic idle acting. All timing derives from the 16 kHz sample
 * clock; every random-looking decision is a hash of (salt, slot index), so
 * a frame can be reproduced from (profile, keyframe, clock) alone and the
 * same uptime always replays the same performance.
 *
 * Slot scheme: time is divided into fixed slots per behaviour; each slot
 * hashes to an event offset/target inside the slot. This keeps evaluation
 * O(1) regardless of uptime.
 */

enum {
    PF_BLINK_SLOT_MS = 3200,
    PF_BLINK_CLOSE_MS = 90,
    PF_BLINK_HOLD_MS = 40,
    PF_BLINK_OPEN_MS = 130,
    PF_BLINK_TOTAL_MS =
        PF_BLINK_CLOSE_MS + PF_BLINK_HOLD_MS + PF_BLINK_OPEN_MS,
    PF_SACCADE_SLOT_MS = 900,
    PF_SACCADE_MOVE_MS = 80,
    PF_BROW_SLOT_MS = 2600,
    PF_BREATH_PERIOD_MS = 3800,
};

/* Blink envelope inside a slot: 255 open → 0 closed → 255 open. */
static int pf_blink_envelope(uint32_t pos_ms, uint32_t start_ms) {
    if (pos_ms < start_ms) {
        return 255;
    }
    uint32_t t = pos_ms - start_ms;
    if (t < PF_BLINK_CLOSE_MS) {
        return 255 - (int)(t * 255U / PF_BLINK_CLOSE_MS);
    }
    t -= PF_BLINK_CLOSE_MS;
    if (t < PF_BLINK_HOLD_MS) {
        return 0;
    }
    t -= PF_BLINK_HOLD_MS;
    if (t < PF_BLINK_OPEN_MS) {
        return (int)(t * 255U / PF_BLINK_OPEN_MS);
    }
    return 255;
}

static void pf_saccade_target(uint32_t salt, uint32_t slot, int *dx,
                              int *dy) {
    uint32_t r = pf_hash32(salt ^ (slot * 2654435761U) ^ 0x5ACCADEU);
    if ((r % 10U) < 6U) {
        /* Centre bias: most of the time the idle gaze rests forward. */
        *dx = 0;
        *dy = 0;
        return;
    }
    *dx = (int)((r >> 4) % 5U) - 2;
    *dy = (int)((r >> 8) % 3U) - 1;
}

void pf_rig_compute(pf_rig_t *rig, const face_keyframe_t *k, uint32_t clock,
                    uint32_t salt) {
    uint32_t ms = clock / (PIXEL_FACE_SAMPLE_RATE / 1000);

    /* ---- blink scheduler ---- */
    uint32_t slot = ms / PF_BLINK_SLOT_MS;
    uint32_t pos = ms % PF_BLINK_SLOT_MS;
    uint32_t r = pf_hash32(salt ^ (slot * 0x9E3779B9U) ^ 0xB111CU);
    uint32_t start = r % (PF_BLINK_SLOT_MS - 2U * PF_BLINK_TOTAL_MS - 60U);
    int env = pf_blink_envelope(pos, start);
    if (((r >> 12) % 5U) == 0U) {
        /* Occasional double blink immediately after the first. */
        int env2 = pf_blink_envelope(pos, start + PF_BLINK_TOTAL_MS + 50U);
        env = pf_mini(env, env2);
    }
    if (k->flags & FACE_KEYFRAME_FLAG_BLINKING) {
        env = 0;
    }
    rig->blink = env;
    rig->eye_open_l = ((int)k->eye_left_open * env) / 255;
    rig->eye_open_r = ((int)k->eye_right_open * env) / 255;

    /* ---- saccades ---- */
    uint32_t sslot = ms / PF_SACCADE_SLOT_MS;
    uint32_t spos = ms % PF_SACCADE_SLOT_MS;
    int tx, ty, px, py;
    pf_saccade_target(salt, sslot, &tx, &ty);
    pf_saccade_target(salt, sslot - 1U, &px, &py);
    if (spos < PF_SACCADE_MOVE_MS) {
        int t = (int)(spos * 256U / PF_SACCADE_MOVE_MS);
        rig->sacc_x = pf_lerp(px, tx, t);
        rig->sacc_y = pf_lerp(py, ty, t);
    } else {
        rig->sacc_x = tx;
        rig->sacc_y = ty;
    }
    rig->gaze_x = pf_clampi((int)k->look_x + rig->sacc_x * 14, -127, 127);
    rig->gaze_y = pf_clampi((int)k->look_y + rig->sacc_y * 14, -127, 127);

    /* ---- brow acting ---- */
    uint32_t bslot = ms / PF_BROW_SLOT_MS;
    uint32_t bpos = ms % PF_BROW_SLOT_MS;
    uint32_t br = pf_hash32(salt ^ (bslot * 0x85EBCA6BU) ^ 0xB40BU);
    int act = 0;
    if ((br % 10U) < 3U) {
        uint32_t bstart = (br >> 8) % (PF_BROW_SLOT_MS - 1000U);
        if (bpos >= bstart && bpos < bstart + 700U) {
            uint32_t bt = bpos - bstart;
            int amp = ((br >> 16) & 1U) ? 34 : -22;
            if (bt < 150U) {
                act = (int)(bt * (uint32_t)pf_absi(amp) / 150U);
            } else if (bt < 550U) {
                act = pf_absi(amp);
            } else {
                act = (int)((700U - bt) * (uint32_t)pf_absi(amp) / 150U);
            }
            if (amp < 0) {
                act = -act;
            }
        }
    }
    rig->brow = pf_clampi((int)k->brow + act, -127, 127);

    /* ---- breathing ---- */
    uint32_t bph = (ms % PF_BREATH_PERIOD_MS) * 256U / PF_BREATH_PERIOD_MS;
    rig->breath = pf_sin8((uint8_t)bph);

    /* ---- speech head-bob (anticipation + follow-through) ---- */
    if (k->flags & FACE_KEYFRAME_FLAG_SPEAKING) {
        int wave = pf_sin8((uint8_t)((ms * 256U / 280U) & 0xFFU));
        rig->bob = (wave * (int)k->mouth_open) / 255;
    } else {
        rig->bob = 0;
    }

    /* ---- smooth flicker noise (candle, phosphor, LED shimmer) ---- */
    uint32_t f0 = pf_hash32(salt ^ ((ms >> 6) * 0xC2B2AE35U)) & 0xFFU;
    uint32_t f1 = pf_hash32(salt ^ (((ms >> 6) + 1U) * 0xC2B2AE35U)) & 0xFFU;
    int frac = (int)((ms & 63U) << 2);
    rig->flicker = pf_lerp((int)f0, (int)f1, frac);

    rig->seed = pf_hash32(clock ^ salt);
}
