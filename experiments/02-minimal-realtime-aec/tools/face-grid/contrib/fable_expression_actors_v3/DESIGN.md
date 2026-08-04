# fable_expression_actors_v3 — design notes

Goal: five renderers that are *characters*, not skins — every one
reads all 40 IR bytes, separates all eleven stage emotions at device
scale, parents its mouth corners, anticipates and settles, never
clips, and clears 30 fps on an ESP32-S3 with an order of magnitude to
spare.

## Split: one solver, five bodies

`fea_solve()` turns `face_render_key_t` + sample clock into a dense
`fea_pose_t` once per frame. It owns everything that should be uniform
across actors — schema gating, activity behavior, viseme tables and
vocabulary collapse, blink/saccade/breath scheduling, the acting
curve, emotion accent channels, and the whole-face pose (translation,
roll shear, area-conserving squash & stretch).

Each actor is a `layout()` that maps the pose onto its own anatomy
plus a rasterizer. `layout()` also feeds `fea_probe()`, so the test
suite asserts against the exact geometry that gets drawn — clamps and
parenting are proven, not hoped.

## Emotion identity = generic channels + accents + actor flourish

The stage layer (`face_stage_cue_apply`) already resolves emotion into
generic channels (corners, squints, brows, arousal, attention, gaze).
The solver honors those, then adds an 11-row accent table for what
generic channels cannot carry at 120 px (pupil dilation, sparkle,
gaze aversion patterns, posture, breath character, the surprise
O-mouth). Finally each actor keys its signature hardware off the
emotion index directly: ear poses, visor drops, emoji glyphs,
silhouette shapes, eye-light aspect. Three layers, so emotions stay
separable even on actors with very different feature sets.

`expression_weight` passes through a non-monotonic LUT — dip →
rise → overshoot → exact settle — which converts any cue attack ramp
into anticipation-active-settle with zero retained state.

## Fixed-point / determinism rules

Q4 screen geometry, Q8 envelopes, Q12 shear, Q14 trig (65-entry
quarter-wave LUT). All schedules hash the sample clock (splitmix32
finalizer) inside fixed activity epochs. No float, no heap, no
statics. Negative values are never left-shifted (UBSan-clean); right
shifts of negatives are avoided in favor of multiplications where the
sign can vary.

## Rasterizer

Painter's-order compositor with 0..32 alpha blending in RGB565:
AA h/v spans from Q4 fractional coverage, ellipses and per-corner
round-rects via integer isqrt per row, capsule strokes and triangles
by bbox, radial glow with hoisted reciprocals, and a two-bezier lip
mouth whose corner endpoints parent the interior, teeth band, tongue
hump, and lip strokes. Everything clamps to the frame; feature clamps
keep geometry inside the safe area even for adversarial keys.

## Actor-specific notes

- **mochi-cat** — ear triangles are the loudest channel (perk, droop,
  sideways flatten, asymmetric tilt) with an audio twitch while
  speaking; bead eyes close under fur-colored lids; lower-lid squeeze
  becomes a drawn happy-arc above 55 % so joy reads at 11 px.
- **karakuri-brass** — everything is a plate with seams and rivets;
  blinks are shutter snaps clipped to the lens circle (a slit seam
  appears when the leaves meet); DETERMINED lowers the visor,
  SURPRISE flings it up, EMBARRASSED lights cheek lamps.
- **emote-sticker** — highest-contrast rig plus emoji accessories
  gated per emotion and scaled by the acting curve; accessories are
  geometry (glyph strokes), not palette shifts.
- **will-o-wisp** — a 12-station teardrop profile is evaluated per
  row; emotion morphs width/crown/sag/wobble, two incommensurate
  sines flicker the crown, and the halo/body/hot-core triplet gives
  emissive depth for three hspans per row. Features are dark cuts;
  teeth glow through the grin.
- **mono-scope** — both per-eye IR channels drive one lens: min() is
  the aperture, the difference tilts the shutter line, so winks read
  as a quizzical tilt. The bright rounded-square eye-light (Vector
  idiom) reshapes per emotion; `attention` runs the halo ring.
