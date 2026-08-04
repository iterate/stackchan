# Research digest: principles → integer algorithms

How authoritative animation material became the deterministic integer
functions in `src/`. Citations are abbreviated here; full titles, URLs,
and license status are in `SOURCES.md`. Everything was re-derived as
ideas — no source text, charts, artwork, or code was copied.

## 1. The principle map

The twelve principles (Thomas & Johnston 1981; Lasseter's SIGGRAPH 1987
adaptation) and where each lives in this contribution:

| Principle | Where it became code |
|---|---|
| Squash and stretch | `breath_level_q10` → `pose.stretch`; studies trade width for height (volume conservation per Lasseter: scale one axis up, the others down) |
| Anticipation | `anticipation_offset`: head counter-moves opposite an upcoming saccade, smaller and slower than the action it precedes |
| Staging | acts run only while idle (`plan_act` gates on activity); `sage-stager` shifts lighting per activity so one idea reads at a time |
| Straight ahead vs pose to pose | the scheduler is pose-to-pose (targets + eased transitions); noise layers are the straight-ahead texture on top — the blend Ribeiro & Paiva recommend for robots |
| Follow-through / overlapping action | `chain_position`: one saccade chain evaluated three times with per-layer lag, duration, and share (eyes 0 ms/100%, head ~70–150 ms/50–70%, body ~170–380 ms/22–40%) |
| Slow in / slow out | every transition runs through the Q10 ease family; nothing lerps linearly |
| Arcs | horizontal head travel adds a parabolic vertical dip (`fable_arc`), per Ribeiro & Paiva's arc rule for gaze sweeps |
| Secondary action | breathing, micro-gaze noise, brow accents — all subordinate amplitudes that never override the primary channel |
| Timing | persona tables re-time one rig into five characters; same targets, different durations (Thomas & Johnston's inbetween chart: duration alone maps "impact" → "thoughtful") |
| Exaggeration | `pip-spark` renders offsets at 150%; overshoot mix is a per-persona dial |
| Solid drawing | asymmetry: per-eye blink lag (8–20 ms), asymmetric brows while thinking, mirrored lid tilts — "one side of a face should never mirror the other" (Lasseter 1987) |
| Appeal | study geometry: big simple eye masses, minimal other features (the Cozmo lesson: fewer, larger features read better at small sizes) |

## 2. Blink phrasing

Facts used (see SOURCES for the studies):

- Spontaneous blink rate: ~17/min at rest, ~26/min conversing, ~4.5/min
  reading (Bentivoglio et al. 1997). Idle-figure guidance in animation:
  a blink every 3–5 s (Whitaker & Halas).
- Blinks are strongly asymmetric: down phase ~90 ms, up phase ~240 ms
  (VanderWerf et al. 2003 via review literature); perceptual studies
  prefer ~233–300 ms total, asymmetric-eased over symmetric (Trutoiu et
  al. 2011). Animation sources prescribe the same shape ("slow out of an
  extreme", Thomas & Johnston; different spacing up vs down, Whitaker &
  Halas).
- Blinks accompany large gaze shifts (~97% of shifts > 33°, Evinger et
  al. 1994); animators use a blink to stage any change of eye direction
  (Thomas & Johnston) and to survive holds longer than ~1.7 s.
- Full closure dwell is short (10–50 ms); hobby robot eyes that hold
  shut for 300+ ms read wrong (see the m5stack-avatar analysis in
  SOURCES).

Implementation (`blink_times`, `blink_beat`, `blink_openness`):
slot-scheduled blinks (persona slots 3.2–5.2 s) with close/hold/open of
70–160 / 30–90 / 130–340 ms per persona, ease-in-quad close and
ease-out-cubic reopen, an occasional shallower second beat (the
"double blink" is an animator's device, not a documented statistic —
flagged as such in the research), a saccade-coupled lid dip on large
gaze shifts, and a per-eye lag for asymmetry. Host lids compose by
minimum so the keyframe always wins.

Known divergence, on purpose: Bentivoglio measures *more* blinking in
conversation; this engine instead skips ~22% of blink slots while the
speaking flag is set. The keyframe's speaking flag marks the robot's own
speech-output windows, where the animation literature (Kismet
observations) has blinks clustering at utterance *ends* rather than
mid-phrase; suppressing mid-utterance blinks approximates that with a
stateless schedule. Documented so a reviewer can flip it.

## 3. Gaze: saccades, fixations, follow-through

Facts used:

- Main sequence: saccade duration ≈ 20–30 ms + 2.2–2.7 ms/deg (Becker
  via Lee/Badler/Badler 2002); 90% of natural saccades < 15°; magnitude
  frequency falls off exponentially; cardinal directions are twice as
  common as diagonals.
- Fixation/gaze-state statistics (Eyes Alive, 2002): listening = long
  mutual gaze (~7.9 s) with brief aversions (~0.43 s); talking = shorter
  holds (~3.1 s) and longer aversions (~0.93 s). Mutual gaze ~75%
  listening vs ~41% speaking (Argyle & Cook 1976 via Eyes Alive).
- Uniform-random gaze is the failure mode: Eyes Alive's user study rated
  it "jittery … distracted"; statistically-shaped gaze reads as
  purposeful. This motivated the triangular (center-biased) target
  distribution instead of uniform.
- Eyes lead, head follows, body drags — Lasseter 1994 ("the eyes should
  move first, locking focus a few frames before the head"), Disney
  Research's animatronic gaze (eyes arrive first at high speed, head at
  lower speed), and game look-at layering (clamp + lag + deadband).
- Drag and settle time are proportional to weight (Lasseter 1987);
  overshoot ~10% is the canonical back-ease constant (Penner's 1.70158).
- Microsaccades: 1–2 per second, ≤ 1°, tiny corrective flicks
  (Martinez-Conde). A face that pauses them reads as paused.

Implementation (`plan_gaze`, `chain_position`, `anticipation_offset`,
micro-flick block in `fable_motion_eval`): fixation slots of 1.6–4.2 s
jittered by hash; targets drawn triangularly around the host's look
point with per-activity gain (listening 45%, thinking 90% + up/side
bias, speaking 75% + occasional doubled aversion); saccade duration =
base + per-px slope (the main sequence mapped px≈deg at this scale);
three re-timed copies of the transition chain give eye/head/body layers;
head transitions blend smoothstep→back-ease by the persona's overshoot
dial and dip through an arc; the anticipation term ramps up over
90–140 ms before large saccades and releases over the head lag. The
superposition form (sum of eased deltas) keeps the position continuous
even when a slow body layer is still settling as the next saccade fires.
Micro-motion is drift noise plus a scheduled ~1 px alternating flick per
~0.75 s.

Tension worth recording: Thomas & Johnston warn that "any jitter or
false move on an inbetween destroys believability" of eyes, while the
robotics literature demands micro-motion (frozen reads dead within
~1.7 s even to the animators). The resolution here: micro-flicks are
discrete eased steps of about a pixel — closer to "pupil moves are
teleports of ≥ 1 px" than to wobble — and drift amplitude stays at ~1 px.

## 4. Breathing

Rest respiration is ~12–18 cycles/min with exhale longer than inhale
and a pause at the bottom; robots that stop breathing read as dead
(Disney Research's "Alive Show" layer lists breathing as the minimum
for animacy; Ribeiro & Paiva: "a soft, slow sinusoidal motion … to
simulate breathing"). Implementation: 3.3–5.3 s persona periods split
inhale 35% / exhale 45% / rest 20%, smoothstep-eased, amplitude
wandering ±25% on an 11 s noise lattice, a small always-on ripple so
the chest never goes perfectly still, halved amplitude while speaking,
and a sigh act that temporarily boosts it. Squash/stretch rides the
breath level and studies compensate width against height.

## 5. Idle acts and personality

Game idle practice: a base breathing loop plus timer-scheduled discrete
"fidgets", varied so no two loops repeat, never fully still (AnimSchool;
MoCap Online notes a single loop reads canned within ~90 s; the Sonic
escalation ladder is the canonical example). Perlin's Improv layers
noise octaves under scripted actions; personality is a different mix of
the same layers (Perlin 1995).

Implementation: act slots of 8–16 s; a hash picks from the persona's
act mask (glance-around, tilt, yawn, sigh, squint, wiggle) or nothing;
trapezoid envelopes (ease in 20% / hold / ease out 30%); acts stage only
while idle. Personas are pure timing/amplitude tables over one rig —
five characters (calm, perky, sleepy, curious, sage) from the same
functions, which is the Timing principle doing the character work.

## 6. Dialogue behavior

- Speaking: head accents on hash-scheduled beats (~1 s cadence, 65%
  occupancy) with amplitude scaled by the current `mouth_open`, so loud
  moments nod and quiet ones pass; brows lift with each accent (head
  accents on dialogue are "always" present per Williams; accents land on
  starts/stops/changes per Blair). Gaze averts more (Argyle & Cook).
- Listening: soft nods every ~5.2 s (a nod on gaze-shift/backchannel is
  a documented listener behavior, Colburn et al. 2000), raised brows,
  center-biased gaze.
- Thinking: gaze up and to a hash-chosen side, asymmetric brows, no
  acts. ("Look up to think" is folk staging rather than measured fact —
  chosen deliberately for legibility.)

## 7. What was deliberately left out

- Vergence and per-eye gaze: Kismet found independent-eye errors look
  disturbing; a flat display can't vergence anyway. Both eyes share one
  gaze vector.
- Neural/statistical audio-to-face models: out of scope per the brief;
  the keyframe already carries the mouth.
- The Eyes Alive 6th-order velocity polynomial: at ≤ 20 px of travel a
  Q10 eased ramp is visually identical and far cheaper.
- Lower-lid animation: Trutoiu et al. measured no perceptual effect at
  small scale; lids here are upper-lid covers plus tilt.
