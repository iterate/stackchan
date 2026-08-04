# fable_robot_eyes — source, licensing, and behavior research

Research report backing the renderer pack in this directory. Everything in
the implementation was written from scratch against the numbers and design
descriptions below; no code was copied from any of the referenced projects.

## 1. Licensing audit of the referenced projects

| Source | License (verified) | Use here |
|---|---|---|
| Anki `digital-dream-labs/vector` (open-sourced Vector engine) | **Digital Dream Labs Software Asset License v1.0 (2021-05-25)** — custom, not OSI; grants use only to Vector owners "with the Covered Products and with no other products or other purpose" | **Read-only inspiration.** Parameter names, keep-alive constants, and design ideas were studied and independently re-implemented; no code copied |
| `anki/cozmo-python-sdk`, `anki/vector-python-sdk`, `cozmoclad` wheel | **Apache-2.0** (LICENSE.txt, © Anki 2016–2018) | Reference for the ProceduralEye parameter list and `set_idle_animation` semantics |
| `kercre123/victor` ("the leak", full Anki source mirror) | **No license — leaked proprietary source** | Used only to verify facts already visible in the official DDL repo (clad enum names). Nothing taken from it |
| FluxGarage/RoboEyes | **GPL-3.0** (LICENSE.txt; Dennis Hölscher) | **Conceptual prior art only** — behavior list and default constants read from the repo; the implementation here shares no code, and the contrib rules forbid copying copyleft source |
| meganetaaan/m5stack-avatar | **MIT** (LICENSE + per-file headers, © Shinya Ishikawa) | Behavior constants (blink/saccade/breath timing) used as a tuning reference; drawing model independently re-created |
| stack-chan/stack-chan | **Apache-2.0** (root LICENSE) | Blink/breath/saccade filter constants used as a tuning reference |
| adafruit/Uncanny_Eyes | **MIT per source header** (Phil Burgess); *no repo-level LICENSE file* | Saccade/blink scheduling constants as tuning reference. Flag for reviewers: the license lives in file headers only |
| M4_Eyes (`adafruit/Adafruit_Learning_System_Guides`) | **MIT** (SPDX headers) | Macro/micro-saccade split and easing reference |
| adafruit/Pi_Eyes | **MIT** (GitHub-detected; `eyes.py` has no header) | Movement/hold timing reference |
| playfultechnology/esp32-eyes | **GPL-3.0** | Prior art citation only (Cozmo-style emotion presets on ESP32) |
| Lee, Badler & Badler, "Eyes Alive" (SIGGRAPH 2002) | Academic paper | Statistical model re-implemented from published tables (facts/statistics, not code) |
| Andrist et al., "Conversational Gaze Aversion for Humanlike Robots" (HRI 2014) | Academic paper | Aversion timing/direction statistics |
| EVE (WALL·E) | Pixar character; no code involved | Aesthetic reference only (two glowing shape-change eyes on black), per Fortune/CNNMoney reporting on the Ive/Stanton design consult |
| Jibo, Vector, Cozmo, Furby, Tamagotchi idle behaviors | Product observations from reviews/teardowns | Behavioral inspiration for the idle-act repertoire |

**Verdict:** the pack is clean-room with respect to GPL (RoboEyes,
esp32-eyes) and the restrictive DDL license. The permissively licensed
projects (MIT/Apache) contributed only constants and design conventions,
which are not copyrightable expression; even so, every envelope and
scheduler here is structured differently (stateless epoch hashing vs.
their stateful timers).

## 2. Anki procedural-face system (primary template)

Verified from the official DDL repo and SDKs:

- `ProceduralEyeParameter` per eye (Cozmo 19, Vector 25 params): center
  x/y, scale x/y, angle, **eight corner radii** (4 corners × x/y as
  fractions of eye size), upper/lower lid Y, lid angle, lid bend; Vector
  adds saturation/lightness/glow/hotspot. Face level: angle, center x/y,
  scale x/y, scanline opacity. Nominal Vector eye: 43×57 px on a 184×96
  RGB565 LCD, spacing 92 px.
- **KeepFaceAlive** (`faceLayerManager.cpp`): eye darts every
  **1000–2250 ms**, max **15 px** from center (1 px when "focused");
  `LookAt` scales eyes **up to 1.05–1.1 looking up, down to 0.85–0.9
  looking down**, outer eye +0.03–0.1 when looking sideways; hotspot
  (pupil) leads the dart by **1.5×**; darts >5 px insert an interpolation
  frame with **vertical squash 0.85** ("adds a sort of mini-blink to the
  dart (per animators)"); the **lagging axis moves at 0.4×** so darts
  curve. Blinks every **3000–10000 ms**; the blink keyframe table
  squash-stretches scaleY/scaleX through `.85/1.05 → .6/1.2 → .1/2.5 →
  .05/5.0 (closed) → .15/2.0 → .7/1.2 → .9/1.0` at 33 ms steps — the eye
  *widens as it collapses*. Pupil saccades: interpolation *jumps* at the
  half-way point instead of lerping.
- Animator lineage: Carlos Baena (Pixar) led; Mooly Segal/Dei
  Gaztelumendi describe explicit Disney-12-principles practice; eyes were
  deliberately simplified (no brows, no pupils) to carry all emotion.

Mapped into this pack: rounded-rect eyes with per-corner radii and lid
cut lines (`fre_eye_draw_t`), squash-stretch blinks that widen while
collapsing, vertical-gaze eye scaling, gaze-side eye growth, curved
darts via a quarter-duration lag on the minor axis, saccade-windowed
stretch, and a keep-alive act scheduler.

## 3. Oculomotor and social-gaze literature → engine constants

Blink:

- Rates (Bentivoglio et al. 1997, *Mov Disord* 12:1028): rest **17/min**,
  conversation **26/min**, reading 4.5/min. Speakers blink far more than
  listeners (Bailly et al.: ~0.6/s speaking vs ~0.1/s listening).
  → activity blink cycles: idle 3.5 s, speaking 2.1 s, listening 4.7 s,
  thinking 3.3 s (`FRE_ACTIVITY` table).
- Kinematics (VanderWerf et al. 2003, *J Neurophysiol* 89:2784): total
  100–400 ms; **down-phase ~75 ms and nearly amplitude-invariant;
  reopening 100–200 ms**, asymptotic. Trutoiu et al. 2011 (Disney/CMU):
  asymmetric profiles with **full closure** are rated most natural.
  → `fre_blink_wave`: 75 ms ease-in close, 20 ms hold, 150 ms cubic
  ease-out reopen with a ~5 % settle overshoot; tests assert
  reopen > close per event.
- Gaze-evoked blinks (Evinger et al. 1994): orbicularis activity in
  **97 % of gaze shifts >33°**, negligible below ~10°.
  → blink probability ramps linearly above a 10° equivalent amplitude.
- Blinks stage gaze changes (*The Illusion of Life*; Williams/Harris:
  "the person will blink en route" on a head turn) and belong *between*
  gaze shifts, not during dramatic expression (Ribeiro & Paiva,
  arXiv:1904.02898).
  → evoked blink starts ~30 ms before the macro saccade movement.
- Doublets: peer-reviewed incidence unavailable (UNVERIFIED); a 12 %
  doublet at +320 ms is used as a plausible heuristic.

Saccades:

- Main sequence (Bahill et al. 1975; Eyes Alive restatement):
  duration ≈ **25 ms + 2.4 ms/deg**; 90 % of natural saccades <15°.
  → `fre_saccade_duration_ms` with 256 gaze units ≡ 25°.
- Eyes Alive (Lee, Badler & Badler 2002) statistical tables:
  magnitude `P = 15.7·e^(−A/6.9)` → 16-point inverse-CDF LUT
  (`FRE_SACCADE_MAG_Q8`); direction bins R 15.5 / UR 6.5 / U 17.7 /
  UL 7.4 / L 16.8 / DL 7.9 / D 20.4 / DR 7.8 % → cumulative table 0;
  dwell: talking mutual ≈ 3.1 s vs away ≈ 0.9 s, listening mutual
  ≈ 7.9 s vs away ≈ 0.4 s → gaze macro-cycle lengths and aversion
  bout ranges per activity.
- Undershoot + corrective saccade ~150 ms later is the physiological
  pattern; cartoon overshoot-and-settle is the animation pattern.
  → signed `overshoot_pct` per profile: realism profiles (saccade_lab,
  cat_optics) undershoot and correct; robot profiles overshoot.
- Microsaccades (Martinez-Conde et al.): **1–2 per second**, amplitudes
  mostly ≤0.4°, clamp 1°; drift <0.5°/s; tremor sub-pixel (ignored).
  → 640 ms micro epochs (70 % contain one 20 ms dart) + two-sine drift.

Lids and brows:

- Becker & Fuchs 1988: the upper lid is **yoked to vertical eye
  position**; lid saccades share eye-saccade duration; downward lid
  motion is a passive fall. → `follow` term narrows the aperture on
  down-gaze (~35 % at full deflection) and widens on up-gaze (~15 %);
  the lid channel (`lid_gaze_y_q8`) reads the gaze solver a profile
  lookahead (40–110 ms) into the future so lids lead the eyes —
  statelessness makes the lookahead free.
- Eyebrow flash as social signal, brow beats on speech emphasis, and
  thinking-knit asymmetry are animation conventions (Illusion of Life;
  Kalegina et al. HRI'18 found lowered brows read "intelligent").

Conversation:

- Kendon 1967 / Argyle & Cook 1976: speaker looks at listener ~41 % of
  the time, listener ~75 %; speakers avert at utterance starts and
  return at ends. Andrist et al. HRI 2014: cognitive aversions
  **3.54 ± 1.26 s** and mostly **upward**; intimacy aversions ~1–2 s and
  mostly sideways, rarer while listening (interval 7.2 s) than speaking
  (4.75 s). Glenberg et al. 1998: looking away while thinking is real
  and helps. → the three direction-CDF variants and the thinking
  activity's long up-biased aversions.

Idle acts and drowsiness:

- *The Illusion of Life*: a held pose goes dead after **<2 s** without
  motion; a blink recaptures life. Sleepy characters keep **one eye
  more open than the other**. → act scheduler cadence, per-eye blink
  asymmetry (`asym_pct`), drowsy profile asymmetry 8 %.
- Product keep-alives: Cozmo/Vector snore/doze on the charger, wake
  with a stretch, explore, narrow eyes when thinking, slit eyes when
  asleep; RoboEyes ships autoblinker + idle reposition + confused/laugh
  flickers; Furby/Tamagotchi sleep when neglected. → the act
  repertoire (glance-aside, look-up-think, squint, brow flash, wink,
  drift-and-refocus, shiver, slow blink, tilt) and the 45 s doze cycle
  with startle recoveries, sleep twitches, and flutter wake.
- Cat slow blink (affiliative signal, Humphrey et al. 2020 *Sci Rep*):
  cat_optics replaces ~30 % of blinks with 1.3 s partial slow blinks.
- Breathing: at-rest respiratory rates 12–20/min; m5stack-avatar (3.3 s)
  and Stack-chan (6 s) bracket the 3.4–4.6 s periods used here.
- Pupil: task-evoked dilation is small and slow; hippus wobbles at
  ~0.2 Hz. → arousal-coupled dilation plus two slow sine wobbles;
  the cat slit opens with arousal.

Robot-face design space (Kalegina et al. HRI'18, 157 faces): eyes are
the invariant feature (all faces have them; 34 % have no mouth — hence
most profiles here are mouthless and advertise `FRE_FLAG_NO_MOUTH`);
no-pupil faces read "soulless" *unless they blink* — exactly the bet
this pack makes; baby-schema research (Lorenz; Glocker et al. 2009)
backs the large round eye geometry.

## 4. Design decisions specific to this contribution

- **Statelessness as the animation superpower.** The firmware contract
  (pure function of keyframe + clock) forbids retained state, so every
  event stream uses fixed epochs whose parameters are hashed from the
  epoch index (splitmix32 finalizer). Consequences: byte-identical
  replay everywhere, and *lookahead is free* — lids and brows read the
  future to produce anticipation, which stateful engines need queued
  plans for.
- **Two overshoot idioms, signed.** One tuning knob covers cartoon
  follow-through (positive) and physiological undershoot-plus-correction
  (negative), so ROBOT profiles feel animated while EYES profiles feel
  observed.
- **Blink styles.** Squash-stretch (Anki-style, eye widens while
  collapsing) versus rigid lid-cut with a closed-lid capsule — chosen
  per profile via `closed_line`.
- **Integer determinism.** Q4 screen geometry with analytic-coverage AA,
  Q8 envelopes, Q14 trig via a 65-entry quarter-wave LUT; portable
  arithmetic-shift helpers (`fre_sar32/64`) avoid implementation-defined
  behavior on negative shifts; periodic phases reduce modulo the period
  before scaling (`fre_turn16`) so u32 products cannot wrap.
- **Limitations.** (a) Statelessness cannot detect activity *transitions*
  (no wake-flash on listen-start; the firmware's animator layer owns
  that). (b) Blink-at-speech-pause entrainment (Nakano & Kitazawa 2010)
  needs mouth history, so it is only approximated by the speaking blink
  rate. (c) The doze cycle is clock-periodic rather than
  inactivity-driven, again by statelessness.

## 5. Primary links

- https://github.com/digital-dream-labs/vector (LICENSE, cannedAnimLib/proceduralFace, faceLayerManager.cpp, docs/architecture/proceduralFace.md)
- https://github.com/anki/cozmo-python-sdk · https://github.com/anki/vector-python-sdk
- https://github.com/FluxGarage/RoboEyes · https://github.com/meganetaaan/m5stack-avatar · https://github.com/stack-chan/stack-chan
- https://github.com/adafruit/Uncanny_Eyes · M4_Eyes in https://github.com/adafruit/Adafruit_Learning_System_Guides · https://github.com/adafruit/Pi_Eyes
- Eyes Alive: https://doi.org/10.1145/566570.566629
- Andrist et al. 2014: https://pages.cs.wisc.edu/~bilge/pubs/2014/HRI14-Andrist.pdf
- Bentivoglio et al. 1997: https://pubmed.ncbi.nlm.nih.gov/9399231/
- VanderWerf et al. 2003: https://journals.physiology.org/doi/full/10.1152/jn.00557.2002
- Trutoiu et al. 2011: https://la.disneyresearch.com/wp-content/uploads/Modeling-and-Animating-Eye-Blinks-Paper.pdf
- Evinger et al. 1994 (via PMC3262917): https://pmc.ncbi.nlm.nih.gov/articles/PMC3262917/
- Ruhland et al. 2015 STAR: https://graphics.cs.wisc.edu/Papers/2014/RABPBGMM14/STAR_Eyes_Final.pdf
- Kalegina et al. HRI'18: https://hcrlab.cs.washington.edu/assets/pdfs/2018/kalegina2018hri.pdf
- Ribeiro & Paiva: https://arxiv.org/abs/1904.02898
- GDC 2017 Cozmo animation pipeline: https://gdcvault.com/play/1024221/
- Anki animator interviews: https://medium.com/kickstarter/animating-the-future-meet-the-cartoonists-giving-life-to-ankis-adorable-robot-vector-1def073de502
