# Motion manifest

Machine-readable copy: `manifest.json`. All functions are pure,
allocation-free, integer-only, and deterministic; Q10 means 1024 = 1.0.

## Easing / timing toolkit (`src/fable_ease.h`)

| Function | Domain → Range | Notes |
|---|---|---|
| `fable_ease_linear/in_quad/out_quad/in_cubic/out_cubic/smooth/smoother` | [0,1024] → [0,1024] | monotone, exact endpoints; `smooth` = smoothstep, `smoother` = Perlin quintic |
| `fable_ease_out_back` | [0,1024] → [-8, ~1124] | ≈10% overshoot (Penner constant 1.70158 as Q10 1742), lands exactly on 1024 |
| `fable_ease_in_back` | [0,1024] → [~-100, 1024] | anticipation dip |
| `fable_settle(t, cycles_q2)` | [0,1024] → [-256, 1280] | damped-cosine settle, quintic decay, ~13% first bounce |
| `fable_arc` | [0,1024] → [0,1024] | 4t(1-t) parabola, peak at midpoint |
| `fable_mix(a, b, m)` | Q10 blend | dials persona overshoot between smooth and back |
| `fable_cos_turn/sin_turn` | angle mod 4096 → [-1024,1024] | 65-entry quarter-wave LUT, interpolated |
| `fable_hash/hash2` | u32 → u32 | lowbias32 avalanche (public domain constants) |
| `fable_isqrt` | u32 → floor sqrt | exhaustively tested to 70k + spot 2^32 |
| `fable_vnoise(clock, period, seed)` | → [-1024,1024) | 1-D value noise, smoothstep lattice |
| `fable_schedule(clock, slot, dur, seed, out)` | O(1) event lookup | jittered one-event-per-slot scheduler; the statelessness backbone |

## Motion engine (`src/fable_motion.h`)

`fable_motion_eval(persona, keyframe, clock, out)` → `fable_motion_pose_t`
(30 bytes, padding-free):

| Pose field | Range | Behavior source |
|---|---|---|
| `eye_x/y_q2` | ±160/±120 quarter-px | fixation targets (triangular, activity-shaped) + main-sequence saccades + drift + ~1 px micro-flicks |
| `head_x/y_q2` | ±160/±120 | delayed re-timed chain (lag 70–150 ms, share 50–70%, persona overshoot, arc dip) + anticipation counter-move + nods/accents |
| `body_x/y_q2` | ±160/±120 | slow drag layer (lag 170–380 ms, share 22–40%) |
| `lid_left/right_q10` | 0..1024 | phrased blinks (fast close / slow open, optional second beat), saccade dip, vertical-gaze coupling, keyframe min-compose, per-eye lag |
| `lid_tilt` | ±32 | persona rest + tilt act |
| `brow_left/right` | ±64 | rest + activity poses + dialogue accents + acts + keyframe brow |
| `breath` | 0..255 | 3.3–5.3 s cycle, inhale 35/exhale 45/rest 20, ±25% wander, sub-pixel ripple, speech-halved, sigh boost |
| `stretch` | ±32 | volume-conserving squash/stretch riding breath + acts |
| `mouth_*` | 0..255 | keyframe pass-through; yawn override when idle |
| `act`, `act_phase` | enum, 0..255 | idle-only staged acts: glance, tilt, yawn, sigh, squint, wiggle |
| `energy` | 0..255 | persona energy shaded by activity |

## Personas (timing tables over one rig)

| Persona | Blink slot | Gaze slot | Head lag/share | Overshoot | Breath | Signature acts |
|---|---|---|---|---|---|---|
| `calm` (Calm Ember) | 4.0 s | 2.8 s | 100 ms / 60% | low | 4.3 s | glance, tilt, sigh |
| `perky` (Perky Pip) | 3.2 s | 1.6 s | 70 ms / 65% | high | 3.3 s | glance, wiggle, tilt, squint |
| `sleepy` (Sleepy Moss) | 5.2 s | 4.2 s | 150 ms / 55% | minimal | 5.3 s | yawn, sigh, tilt |
| `curious` (Curious Scout) | 3.8 s | 1.9 s | 90 ms / 70% | medium | 3.9 s | glance, tilt, squint, wiggle |
| `sage` (Attentive Sage) | 4.4 s | 3.4 s | 120 ms / 50% | low | 4.7 s | tilt, sigh, glance |

## Studies (`src/fable_studies.h`)

Contract mirrors `face_render_frame`: 160×120 RGB565, caller-owned
buffer, pure function of keyframe + clock. Golden CRCs pin all five.

| # | Slug | Persona | Principles staged | Host cost |
|---|---|---|---|---|
| 0 | `curious-scout` | curious | lead/follow/drag, anticipation, arcs, antenna follow-through | ~1.6 µs/frame |
| 1 | `ember-breath` | calm | squash & stretch breathing, blink phrasing, body-drag shadow | ~2.9 µs/frame |
| 2 | `pip-spark` | perky | overshoot/settle, exaggeration (150%), secondary cheeks | ~1.4 µs/frame |
| 3 | `moss-drowse` | sleepy | slow in/out, heavy tilted lids, yawns | ~2.1 µs/frame |
| 4 | `sage-stager` | sage | staging: activity lighting, mutual-gaze pupils, nods, accents | ~2.3 µs/frame |

## Test inventory

- `test_ease`: endpoint/monotonicity/bounds for every curve, trig
  identity + wrap, exhaustive isqrt, noise continuity/range/seeding,
  scheduler invariants. ~351k checks.
- `test_motion`: bitwise determinism (interleaved, out-of-order, clock
  wrap), pose bounds across personas × activities × hostile keyframes,
  blink rate windows per persona, blink fast-close/slow-open shape,
  keyframe lid override, eyes-lead-head, head settles on its follow
  share, anticipation counter-move detection, breath cycle counts, acts
  staged only while idle and within persona masks, speaking gaze busier
  than listening. ~978k checks.
- `test_studies`: contract errors, buffer-history independence, full
  pixel coverage, no-freeze bound (<1 s) and motion density, cross-study
  distinctness, golden CRC pinning.
- `bench_studies`: ns/eval and µs/frame.
- Float guard: `#pragma GCC poison` build + comment-stripped token
  audit; UBSan+ASan pass; arithmetic-shift assertion in every binary.
