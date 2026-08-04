# fable_toon_acting — design notes

Goal: standalone production C renderer styles that make a 160×120 RGB565 face
*act* — all eleven stage expressions clearly separable, viseme speech with
coarticulation, speech start/end anticipation and settle, gaze, head/body
gesture, squash & stretch — using only cheap integer procedural primitives.

## Input contract

Pure function of the 40-byte `face_render_key_t` (schema v2) plus the 16 kHz
sample clock. No retained state, no heap, no floats. The stage pipeline
(`face_stage_cue_apply`) already resolves cues into action channels:

- `controls.*` — jaw/eye/gaze prefix (12-byte keyframe): `mouth_open`,
  `mouth_width`, `mouth_round`, `mouth_press`, `mouth_teeth`,
  `eye_left_open`, `eye_right_open`, `look_x/y`, `brow`, activity, flags.
- articulation — `viseme`, `viseme_secondary`, `viseme_blend`,
  `viseme_weight`, `audio_level`, `speech_phase`.
- acting — `mouth_corner_left/right`, `cheek`, `eye_*_squint`,
  `brow_inner`, `brow_outer_left/right`, `head_roll/yaw/pitch`,
  `body_lean_x/y`, `affect_valence/arousal`, `attention`,
  `stage_expression` + `expression_weight`.

## Acting model (the layered solver)

A behavior solver (`fta_act_solve`) turns the key + clock into a dense
integer "performance frame" consumed by the rasterizer. Layers, applied in
order:

1. **Base rig pose** from the generic action channels (works even if
   `stage_expression` is absent).
2. **Expression accents**: per-expression table adds what generic channels
   cannot separate at 120 px — e.g. blush patches (embarrassed), lid droop +
   slow phase (sleepy), sparkle pupils (excited), asymmetric knit
   (skeptical vs thoughtful), pressed mouth line (determined). Gated by
   `stage_expression`, scaled by `expression_weight`.
3. **Acting curve**: `expression_weight` (0..255, ramped in time by the cue
   envelope) is mapped through a non-monotonic response LUT —
   dip (anticipation) → rise → ~108 % overshoot → settle at 100 %. Because
   the cue attack ramps the weight through the LUT in time, the face
   anticipates and settles without renderer state.
4. **Speech phases**: `FACE_SPEECH_STARTING` poses an inhale (brows up, lids
   wide, slight scale-up, mouth pre-open); `ENDING` poses the exhale/settle
   (everything eases down, corners relax). `ACTIVE` adds an audio-level head
   bob; `IDLE` breathes.
5. **Viseme articulation**: primary/secondary viseme blended by
   `viseme_blend` (coarticulation), weighted by `viseme_weight`, mixed over
   the keyframe mouth channels. Jaw (open) and lips (width/round/press)
   treated independently (JALI-style separation).
6. **Deterministic life**: clock-scheduled blinks (with pre-blink lid dip
   and slower reopen), saccades + fixation drift, breathing sway, occasional
   idle acts. All schedules are integer hashes of the block index so frames
   are reproducible at any clock.
7. **Whole-face transform**: head yaw/pitch/roll and body lean move/shear
   the rig; squash & stretch conserves area (stretch_y ⇒ inverse squash_x)
   driven by jaw, bounce gesture, and speech energy.

## Rendering (integer scanline compositor)

One pass over 120 rows; per row the solver-space geometry is reduced to a
small set of spans/edges evaluated incrementally:

- rounded-rect/superellipse eye whites, iris + pupil + highlight discs,
- lids as coverage cuts (upper/lower, angled), brows as thick angled strokes,
- mouth as upper/lower lip quadratic edges with independent corner offsets,
  interior with teeth band + tongue,
- cheek blush ellipses, background vignette.

Fixed-point Q8 coordinates; RGB565 alpha blends with 0..32 alpha; 2-3 px
soft edges via coverage ramps; no per-pixel divides or sqrt in hot paths.
Hard requirement: every primitive clamps to the frame — nothing clips.

## Styles

All share the solver; each style is a parameter set (proportions, palette,
stroke weights, accent gains) so acting quality is uniform:

- `toon-bean` — big soft-rectangle eyes + rubber-hose mouth on warm cream;
  the flagship acting face.
- `toon-ink` — the same rig as line art on paper (outline rendering path).
- `toon-ember` — emissive amber irises on dark glass with enforced
  bilateral eye visibility; proves the solver generalizes across looks.

## QA

- unit tests: determinism (-O0 vs -O2 hash equality), no-clip proof
  (border scan), expression separation (pairwise frame distance between all
  11 expressions above threshold), motion checks (blink/saccade/gesture
  produce bounded, nonzero temporal energy), viseme distinctness,
  performance budget.
- ASan/UBSan + strict warnings clean; xtensa cross-compile float-free scan.
- labeled contact sheets (PPM → PNG) of 11 expressions × styles, viseme
  charts, and motion strips; reviewed by eye before sign-off.
