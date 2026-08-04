# fable_toon_acting — research notes and licensing audit

Every number in the solver traces to one of the sources below, or is a
hand-tuned value validated against the pack's own quality metrics. No
code, structure, or artwork was taken from any copyleft or proprietary
source; GPL projects are cited as prior-art existence proofs only.

## 1. Classical animation principles applied

- **Squash & stretch with exact 2D area conservation** —
  `scale_x = 65536 / scale_y` (integer reciprocal). The 3D volume rule
  is 1/√n per remaining axis; in 2D the exponent is 1. Restrained to
  ±12% total, per the review guidance that Cozmo-grade motion uses
  10–15% travel, and pivoted at the plate bottom so the character stays
  planted. (Volume-preservation derivation: DapurGrafik 2014; principle:
  Thomas & Johnston's 12 principles — the principles are uncopyrightable
  ideas; no book text or art reproduced.)
- **Anticipation / overshoot / settle without state** — the acting
  response curve (17-entry Q8 LUT: dip ~−5% until w≈0.19, ~+8%
  overshoot at w≈0.75, settle 1.0) is applied to the accent layer, so a
  stage cue's attack envelope traverses dip→overshoot→settle in time.
  Overshoot ladder reference: the classic 120/90/105/97.5/101.25/100
  decaying-oscillation used by motion designers (Apple Motion docs,
  Mt. Mograph); our peak is gentler for a 120 px face.
- **Follow-through / overlapping action** — features lag the plate on
  the breathing wave (1/12-turn phase lag), the plate trails the
  features on yaw (2.5D parallax), and blush responds through the
  slower magnitude curve.
- **Arcs and secondary action** — speech adds an audio-scaled head bob
  (~3.2 Hz) and brow emphasis above audio level 150; gaze drift bows
  fixation paths.

## 2. Oculomotor and facial-behavior literature → engine constants

| Constant | Value in solver | Source |
|---|---|---|
| Blink down phase | 1,500 samples = 94 ms | 92 ± 17 ms, eyelid kinematics (J. Neurophysiol. 2003; VanderWerf) |
| Blink closed hold | 800 samples = 50 ms | ~50 ms fully closed (same) |
| Blink reopen | 3,900 samples = 244 ms, fast-out | 242 ± 55 ms, ~2.6× down phase, velocity peaks early |
| Blink cadence | 52,000 samples = 3.25 s (~18/min) | Bentivoglio 1997: rest ≈ 17/min (conversation 26/min noted; cadence kept constant per style so a frozen-clock atlas never catches one expression mid-blink) |
| Double blinks | 15% of slots | human double-blink habit (behavior literature) |
| Pre-blink dip | 800 samples, ~9% lid | anticipation staging (animation practice) |
| Saccade flight | 640 samples = 40 ms, no overshoot | main-sequence 30–100 ms; saccades are ballistic; overshoot reads as a tic |
| Fixation length | 1.75 s slots + drift | M5Stack Avatar's 0.5–2.5 s (MIT) and fixation literature |
| Micro-drift | ±0.4 px two incommensurate waves | fixational drift; two frequencies prevent loop detection |
| Breathing | 9–23 bpm from arousal, inhale 40%/exhale 60%, ±1.2 px + ±1.5% scale | resting adults 12–20/min; asymmetric respiration; surprise holds breath |
| Gaze aversion (thinking) | up-and-away bias + sparser blinks | Doherty-Sneddon 2005: aversion peaks while thinking |
| Embarrassment display | 5 s cycle: gaze drop → two smile-control beats | Keltner 1995 / Keltner & Buswell 1996 timed sequence |
| Pupil affect | dilation with arousal; surprise constriction ~−45%, excited +19% | arousal–pupil coupling; cartoon staging of FACS AU5+26 vs AU12 |

## 3. Emotion authoring (FACS-grounded)

The stage layer provides corners/cheek/squint/brow/roll/valence/arousal
(`face_stage.c` targets). The accent table adds the discriminating
features from the FACS prototypes: AU1+4 knit for concern (inner-up,
outer-down lid tilt), AU1+2+5+26 for surprise (widened eye box, pupil
constriction, jaw O-override), unilateral asymmetry for skeptical (the
asymmetry *is* the signal), AU4+7+23 firmness for determined (V-brow,
narrowed lids, pressed wide mouth, forward posture), lid droop 41/43
for sleepy, AU6+12 with sparkle and bounce for joy/excited, and the
Keltner embarrassment sequence with blush. FACS AU numbering and
emotion→AU prototypes are published scientific facts (Ekman & Friesen
EMFACS; the FACS manual itself was not used or reproduced).

## 4. Speech articulation

- **JALI split** (Edwards et al., SIGGRAPH 2016): jaw and lip are
  independent axes. The mouth drops 60% of `open` into the jaw (lower
  curve) and 40% into the upper lip; lip identity (round/press/teeth/
  tongue) comes from channels + viseme accents. Idea only; no rig or
  code from the JALI project.
- **Coarticulation** (Cohen & Massaro 1993 dominance blending):
  primary/secondary viseme accents mix by `viseme_blend`, weighted by
  `viseme_weight`; the press channel dominates (bilabials must seal —
  `PP` accent doubles press). The blend-sweep continuity test bounds
  the per-step frame delta.
- **Viseme accents** for the OVR15 vocabulary (funnel for O/U, spread +
  teeth for E/I/SS/TH, tongue for TH/DD/NN/RR, press for PP/FF): the 15
  viseme names and phoneme groupings are a de-facto interface standard,
  safe to target; no Meta SDK code or reference imagery used. Shape
  values transcribed from this project's own `face_viseme.c` table.
- **Speech start/end**: STARTING poses lips-press-then-open (the
  documented "close tighter before speech" anticipation) with raised
  brows and widened lids; ENDING settles corners/jaw. Craft references:
  Escape Studios (2-frame audio lead), Animator Island (sentence-end
  holds) — technique facts.

## 5. Appeal and proportions

Proportions follow the **baby schema** (Lorenz; Glocker 2009; PNAS
2009): large eyes set *below* the vertical face centre (eye centers at
y≈64 of 120), wide inter-eye gap (~16 px), small low mouth, tall
forehead, rounded silhouette. This is the IP-safe substitute for
studying protected characters. Iris ≈ 0.55–0.57 of the eye width per
the project's own visual-review praise of that ratio. Cozmo/Vector
material (Fast Company/designboom interviews, GDC 2017 talk listing)
informed the *discipline* — lid/pupil hierarchy, restrained travel —
not any silhouette, asset, or parameter dump. pycozmo's reverse-
engineered parameter list was read as documentation of technique; no
geometry or animation data was reproduced.

## 6. Embedded technique

- RGB565 blend via the split-mask trick (`0x07e0f81f`), one multiply
  for all three channels — standard public-domain idiom.
- Q4 span endpoints give 1 px AA coverage ramps; per-row rounded-rect
  half-widths from a bitwise integer sqrt; per-column quadratic lip
  curves with integer weights.
- Per-style salts desync blinks between tiles (project convention from
  the pixel-pack family).
- M5Stack Avatar (MIT, Shinya Ishikawa) informed idle cadence ranges;
  its 300–500 ms closed blink was deliberately *not* copied (5× slower
  than measured human lids — reads sleepy).

## 7. Licensing audit

| Source | License | Used as |
|---|---|---|
| FluxGarage RoboEyes, micropython-roboeyes, ggldnl PEL | GPL-3.0 | **ideas only** (mood/idle-macro taxonomy); zero code/structure/art |
| M5Stack Avatar | MIT | cadence numbers; no code copied (values re-derived and re-tuned) |
| pycozmo procedural_face.py | MIT repo; reverse-engineered content | technique documentation only; no Cozmo silhouette/assets reproduced |
| Anki Cozmo GDC 2017 talk, Fast Company, designboom | © respective owners | cited facts about design decisions |
| JALI (SIGGRAPH 2016) | © ACM; commercial product | the two-axis idea, independently implemented |
| Cohen & Massaro 1993 | academic | published model, reimplemented from the math |
| FACS / EMFACS prototypes | scientific facts; manual © Ekman | AU combinations as facts; manual not used |
| Keltner 1995/1996 | academic | display timing facts |
| Bentivoglio 1997; VanderWerf 2003; saccade main-sequence papers | academic | numeric facts |
| Baby-schema literature (Lorenz, Glocker, PNAS, PMC reviews) | academic, several open-access | proportion guidance |
| OVR15 viseme names / phoneme groups | de-facto standard; SDK © Meta | vocabulary targeted; SDK untouched; no reference images |
| Live2D parameter conventions | docs © Live2D | naming conventions as design pattern; SDK untouched |
| Preston Blair phoneme taxonomy | drawings © estate | taxonomy only; all shapes original |
| Escape Studios / Animator Island craft posts | editorial | technique facts |
| This repository's `face_stage.c`, `face_viseme.c`, quality probe | project first-party | vendored (compat/) and mirrored respectively |

**Conclusion**: the pack is original work; everything imported is either
an uncopyrightable idea, a published scientific fact, first-party
project code (vendored verbatim with provenance noted), or MIT-licensed
inspiration re-derived rather than copied. Nothing in this directory is
GPL-encumbered, and no protected character design is imitated.
