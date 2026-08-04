#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "face_keyframe.h"
#include "face_stage.h"

/*
 * fable_sprite_behavior_v1 — deterministic behavior/state layer for
 * sprite avatars.
 *
 * This module is the "brain" between a realtime voice session and a
 * sprite renderer. It consumes:
 *
 *   - session lifecycle events (disconnected .. assistant speaking,
 *     interruption, error);
 *   - explicit AI emotion/stage direction as the production 32-byte
 *     face_stage_cue_t;
 *   - transcript delta timing;
 *   - PCM-derived articulation as the stable 12-byte face_keyframe_t
 *     plus an optional OVR15 viseme.
 *
 * and emits one renderer-neutral sprite frame per tick: which semantic
 * cell each layer shows plus small integer layer transforms. It never
 * touches pixels; a sprite renderer maps (role, cell) onto its atlas
 * and applies the offsets however its compositor prefers.
 *
 * Contract highlights:
 *
 *   - integer arithmetic only, no heap, no I/O;
 *   - all state lives in one caller-owned sb_t (fixed size, asserted);
 *   - identical (seed, event/tick sequence) input produces
 *     byte-identical sb_frame_t streams on ESP32, host, and WASM;
 *   - every episodic behavior (blinks, saccades, micro-clips) draws
 *     randomness only at scheduled decision points from one seeded
 *     xorshift generator, so replays are exact;
 *   - micro-clips are interruptible: state changes preempt them and
 *     residual offsets decay smoothly (no pose pops);
 *   - mouth shape switching is rate-limited (debounce + minimum hold)
 *     while openness amplitude is preserved, so fast noisy visemes do
 *     not become mouth flutter but loud vowels still open wide.
 */

/* ------------------------------------------------------------------ */
/* Renderer-neutral IR                                                 */
/* ------------------------------------------------------------------ */

enum { SB_SCHEMA_VERSION = 1 };

/* Layer roles, back-to-front. A renderer may merge or ignore roles
 * (e.g. bake brows into eye cells) but must not reinterpret them. */
typedef enum {
    SB_LAYER_BODY = 0,
    SB_LAYER_HEAD,
    SB_LAYER_EYE_L,
    SB_LAYER_EYE_R,
    SB_LAYER_BROW_L,
    SB_LAYER_BROW_R,
    SB_LAYER_MOUTH,
    SB_LAYER_FX,
    SB_LAYER_COUNT,
} sb_layer_role_t;

/* Head bank: turned/tilted variants are cells because tiny sprite
 * avatars redraw a turned head instead of rotating pixels. */
enum {
    SB_HEAD_FRONT = 0,
    SB_HEAD_QUARTER_L,
    SB_HEAD_QUARTER_R,
    SB_HEAD_UP,
    SB_HEAD_DOWN,
    SB_HEAD_TILT_L,
    SB_HEAD_TILT_R,
    SB_HEAD_CELL_COUNT,
};

/* Eye bank cells describe lid aperture/quality. Gaze direction is the
 * frame's gaze_x/gaze_y; a renderer either offsets a pupil layer or
 * picks a baked look-direction variant. */
enum {
    SB_EYE_OPEN = 0,
    SB_EYE_WIDE,
    SB_EYE_SOFT,   /* half lidded */
    SB_EYE_CLOSED,
    SB_EYE_HAPPY,  /* upward arc */
    SB_EYE_SQUINT,
    SB_EYE_CELL_COUNT,
};

enum {
    SB_BROW_NEUTRAL = 0,
    SB_BROW_RAISED,
    SB_BROW_KNIT,
    SB_BROW_SAD,
    SB_BROW_ANGRY,
    SB_BROW_CELL_COUNT,
};

/* Mouth bank: an emotion band used at rest plus a compact
 * sprite-friendly viseme vocabulary (Preston-Blair-like grouping of
 * the OVR15 set). Openness amplitude rides on the layer weight and
 * jaw dy so a small bank still reads loud vs quiet. */
enum {
    SB_MOUTH_REST = 0,
    SB_MOUTH_SMILE,
    SB_MOUTH_FROWN,
    SB_MOUTH_PRESS,
    SB_MOUTH_AI,        /* open vowel */
    SB_MOUTH_E,         /* spread */
    SB_MOUTH_O,
    SB_MOUTH_U,
    SB_MOUTH_MBP,       /* closed plosive */
    SB_MOUTH_FV,
    SB_MOUTH_LTH,       /* tongue visible */
    SB_MOUTH_S,         /* teeth sibilant */
    SB_MOUTH_WIDE_OPEN, /* emphatic AA */
    SB_MOUTH_CELL_COUNT,
};

/* Status badge layer (optional for a renderer). */
enum {
    SB_FX_NONE = 0,
    SB_FX_ZZZ,    /* asleep / disconnected */
    SB_FX_DOTS,   /* thinking ellipsis */
    SB_FX_ALERT,  /* connecting / attention ping */
    SB_FX_ERROR,
    SB_FX_SPARK,  /* delight accent */
    SB_FX_CELL_COUNT,
};

/* One layer selection + transform. Offsets are quarter pixels in the
 * 160x120 reference space; renderers at other resolutions scale them.
 * rot is a tilt hint (-64..64 ~ -90..90 degrees) for renderers that
 * can shear/rotate; cell-based renderers may ignore it because tilted
 * head cells are also selected. stretch is squash(-)/stretch(+) in
 * 1/32 units for renderers that can scale. */
typedef struct {
    uint8_t cell;
    uint8_t weight;  /* 0..255 emphasis/openness/opacity hint */
    int8_t dx_q2;
    int8_t dy_q2;
    int8_t rot;
    int8_t stretch;
} sb_layer_t;

enum { SB_LAYER_BYTES = 6 };

_Static_assert(sizeof(sb_layer_t) == SB_LAYER_BYTES,
               "sprite layer record must stay 6 bytes");

/* Session behavior states (also reported per frame). */
typedef enum {
    SB_STATE_DISCONNECTED = 0,
    SB_STATE_CONNECTING,
    SB_STATE_LISTENING,          /* connected, attentive default */
    SB_STATE_USER_SPEAKING,
    SB_STATE_THINKING,           /* response pending */
    SB_STATE_ASSISTANT_SPEAKING,
    SB_STATE_INTERRUPTED,        /* transient; decays to LISTENING */
    SB_STATE_ERROR,
    SB_STATE_COUNT,
} sb_state_t;

/* Micro-clips (reported per frame for tests/telemetry). */
typedef enum {
    SB_CLIP_NONE = 0,
    SB_CLIP_SACCADE,    /* small fixation jump, eyes lead */
    SB_CLIP_GLANCE,     /* larger look-away and return */
    SB_CLIP_ACK_NOD,    /* listening acknowledgement */
    SB_CLIP_HEAD_TILT,
    SB_CLIP_HEAD_TURN,
    SB_CLIP_BREATH_SIGH,
    SB_CLIP_SETTLE,     /* posture reset toward neutral */
    SB_CLIP_LEAN_IN,    /* attentive approach */
    SB_CLIP_STARTLE,    /* interruption recoil */
    SB_CLIP_DROOP,      /* drowsy sink (disconnected) */
    SB_CLIP_SEARCH,     /* scanning sweep (connecting/thinking) */
    SB_CLIP_GESTURE_NOD,    /* staged via face_stage_cue_t */
    SB_CLIP_GESTURE_SHAKE,
    SB_CLIP_GESTURE_BOUNCE,
    SB_CLIP_COUNT,
} sb_clip_t;

/* One output frame. Everything a sprite compositor needs, nothing it
 * must obey: unknown cells fall back to cell 0 of the bank. */
typedef struct {
    uint32_t sample_clock;   /* 16 kHz clock this frame was evaluated at */
    uint32_t frame_index;
    uint8_t schema_version;  /* SB_SCHEMA_VERSION */
    uint8_t state;           /* sb_state_t */
    uint8_t clip;            /* sb_clip_t currently staged */
    uint8_t emotion;         /* face_expression_t */
    uint8_t emotion_weight;  /* 0..255 resolved cue envelope */
    int8_t gaze_x;           /* -64..64, + is avatar's right */
    int8_t gaze_y;           /* -64..64, + is down */
    uint8_t blink;           /* 0 open .. 255 fully closed */
    uint8_t breath;          /* 0 exhaled .. 255 peak inhale */
    uint8_t talk;            /* smoothed speech energy 0..255 */
    uint8_t attention;       /* 0 inward/asleep .. 255 locked on user */
    uint8_t reserved;
    sb_layer_t layers[SB_LAYER_COUNT];
} sb_frame_t;

enum { SB_FRAME_BYTES = 20 + SB_LAYER_COUNT * SB_LAYER_BYTES };

_Static_assert(sizeof(sb_frame_t) == SB_FRAME_BYTES,
               "sprite behavior frame must stay packed");

/* ------------------------------------------------------------------ */
/* Inputs                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    SB_EV_DISCONNECTED = 0,
    SB_EV_CONNECTING,
    SB_EV_CONNECTED,                /* session ready -> LISTENING */
    SB_EV_USER_SPEECH_STARTED,
    SB_EV_USER_SPEECH_STOPPED,
    SB_EV_THINKING,                 /* response requested/pending */
    SB_EV_ASSISTANT_SPEECH_STARTED,
    SB_EV_ASSISTANT_SPEECH_STOPPED,
    SB_EV_INTERRUPTED,              /* barge-in cancelled playback */
    SB_EV_ERROR,
    SB_EV_ERROR_CLEARED,
    SB_EV_COUNT,
} sb_event_t;

/* Tunables. sb_config_default() fills the reviewed defaults; all
 * times are milliseconds on the 16 kHz sample clock (16 samples/ms). */
typedef struct {
    uint16_t settle_ms;          /* residual pose decay after preemption */
    uint16_t interrupted_ms;     /* INTERRUPTED dwell before LISTENING */
    uint16_t clip_slot_ms;       /* micro-clip decision cadence */
    uint16_t blink_min_ms;       /* uniform blink interval draw */
    uint16_t blink_max_ms;
    uint16_t blink_close_ms;     /* lid descent */
    uint16_t blink_open_ms;      /* lid ascent */
    uint16_t breath_period_ms;
    uint16_t mouth_min_hold_ms;  /* dwell before another mouth switch */
    uint16_t mouth_debounce_ms;  /* target persistence before switch */
    uint16_t mouth_close_ms;     /* silence -> closed latency */
    uint16_t transcript_char_ms; /* expected speech per transcript char */
    uint8_t double_blink_pct;
    uint8_t ack_nod_pct;         /* nod chance at a user pause */
    uint8_t idle_none_pct;       /* clip slot stages nothing */
} sb_config_t;

/* Internal channel state. Treat as opaque; layout is only public so
 * callers can place sb_t in static storage. */
typedef struct {
    sb_config_t config;
    uint32_t rng;

    /* Session machine. */
    uint8_t state;
    uint8_t prev_state;
    uint32_t state_since;

    /* Blink channel. */
    uint32_t blink_next;
    uint32_t blink_start;
    uint16_t blink_hold_ms;
    uint8_t blink_double;
    uint32_t blink_inhibit_until;

    /* Micro-clip channel. */
    uint8_t clip;
    uint32_t clip_start;
    uint16_t clip_dur_ms;
    int8_t clip_dir;      /* -1/+1 direction parameter */
    uint8_t clip_amp;     /* 0..255 amplitude parameter */
    uint32_t clip_next;
    uint32_t ack_next;    /* next acknowledgement opportunity */

    /* Breath channel (phase accumulator, Q16 turns). */
    uint32_t breath_phase_q16;
    uint32_t breath_last_clock;

    /* Gaze channel. */
    int16_t gaze_x_q4;    /* current, Q4 of -64..64 range */
    int16_t gaze_y_q4;
    int16_t sacc_from_x_q4;
    int16_t sacc_from_y_q4;
    int8_t gaze_tgt_x;
    int8_t gaze_tgt_y;
    uint32_t sacc_start;
    uint16_t sacc_dur_ms;
    uint32_t fix_next;

    /* Residual pose decay (preempted clip smoothing), Q4. */
    int16_t res_head_x_q4;
    int16_t res_head_y_q4;
    int16_t res_tilt_q4;

    /* Mouth channel. */
    uint8_t mouth_cell;
    uint8_t mouth_pending;
    uint32_t mouth_since;
    uint32_t mouth_pending_since;
    uint16_t mouth_level_q8;  /* smoothed openness */

    /* Articulation cache. */
    face_keyframe_t art;
    uint8_t art_viseme;
    uint32_t art_at;

    /* Stage direction. */
    face_stage_cue_t cue;
    face_stage_cue_t cue_queued;
    uint8_t cue_active;
    uint8_t cue_pending;

    /* Transcript pacing. */
    uint32_t speech_expect_until;
    uint32_t transcript_last;

    uint32_t frame_index;
    uint32_t last_clock;
} sb_t;

enum { SB_STATE_BYTES_MAX = 256 };

_Static_assert(sizeof(sb_t) <= SB_STATE_BYTES_MAX,
               "behavior state must stay small and fixed");

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Reviewed defaults (attentive listening, calm idle). */
void sb_config_default(sb_config_t *config);

/* Initialise with defaults; `seed` decorrelates instances. Equal
 * seeds and inputs replay byte-identically. */
void sb_init(sb_t *sb, uint32_t seed);

/* Initialise with explicit tunables. Returns false (and initialises
 * with defaults) if config is NULL or contains a zero period. */
bool sb_init_with_config(sb_t *sb, uint32_t seed, const sb_config_t *config);

/* Feed one session lifecycle event at a sample time. Events that make
 * no sense in the current state (e.g. assistant speech while
 * disconnected) are ignored rather than trusted. */
void sb_handle_event(sb_t *sb, sb_event_t event, uint32_t sample_clock);

/* Explicit AI performance direction. The cue's expression becomes the
 * emotion envelope (attack/hold/release honoured); NOD/SHAKE/TILT/
 * LEAN_IN/BOUNCE gestures stage the matching micro-clip; gaze_target
 * overrides the behavioral gaze until the cue releases. interrupt_mode
 * selects blend/cut/queue against a currently active cue. */
void sb_direct(sb_t *sb, const face_stage_cue_t *cue);

/* Latest PCM-derived articulation. `viseme` is FACE_VISEME_* (OVR15)
 * or FACE_VISEME_NONE when the analyser has no classification; the
 * continuous keyframe controls are used as fallback shape evidence. */
void sb_set_articulation(sb_t *sb, const face_keyframe_t *keyframe,
                         uint8_t viseme, uint32_t sample_clock);

/* Transcript delta timing: `chars` new characters arrived. Used to
 * pace expected speech and to keep the mouth alive if articulation
 * stalls while the assistant is still speaking. */
void sb_transcript_delta(sb_t *sb, uint32_t sample_clock, uint16_t chars);

/* Evaluate one frame at `sample_clock` (16 kHz, monotonic; a backward
 * jump resets schedules deterministically). Any tick cadence works;
 * tests use fixed cadences for exact replays. */
void sb_tick(sb_t *sb, uint32_t sample_clock, sb_frame_t *out);

/* Current behavior state. */
sb_state_t sb_state_get(const sb_t *sb);
