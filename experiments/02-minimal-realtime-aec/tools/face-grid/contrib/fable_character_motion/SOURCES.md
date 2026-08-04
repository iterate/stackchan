# Source and licensing report

Research was performed live (web search + primary-source fetches) by
three parallel research passes: Disney/Pixar animation literature, game
and procedural animation, and social robotics / gaze science. This file
records what was consulted, its license status, and what — if anything —
was reused. **No source code, text, charts, or artwork was copied into
this contribution.** Only facts, numbers, and ideas were re-derived as
original C.

## Reuse summary (the short version)

| Reused thing | Source | License basis |
|---|---|---|
| Hash finalizer constants (0x7feb352d, 0x846ca68b) | Chris Wellons, hash-prospector ("lowbias32") | Public domain / CC0 (stated by the project) |
| Overshoot constant 1.70158 (≈10% overshoot) | Robert Penner's easing equations (2001) | BSD-3-Clause (verified via Qt's attribution page); a numeric constant plus independently-written polynomial code — noted here as attribution anyway |
| Smoothstep / smootherstep polynomials | Perlin, "Improving Noise" (SIGGRAPH 2002) | Published mathematics; re-implemented from the formula |
| Everything else | — | Original code informed by facts below |

CRC-32 (poly 0xEDB88320) and integer square root in the tests/toolkit
are textbook public-domain algorithms, written from the definitions.

## Animation literature (copyrighted books/papers — ideas only)

- Frank Thomas & Ollie Johnston, *Disney Animation: The Illusion of
  Life*, Abbeville, 1981. © Walt Disney Productions. Used: the twelve
  principles; the inbetween-count timing chart concept; blink staging
  rules ("blinks are good on any shift of eye direction"; asymmetric
  blink spacing; a hold dies after ~40 frames without a blink); eyes as
  the expression's core with a no-jitter warning; dialogue sync leads
  (eyes lead accents by 3+ frames).
- John Lasseter, "Principles of Traditional Animation Applied to 3D
  Computer Animation", SIGGRAPH 1987, ACM. Used: volume-conserving
  squash/stretch; anticipation-vs-speed tradeoff; lead/follow ordering
  ("the eyes will usually lead the head"); drag/settle proportional to
  weight; spline overshoot as accidental anticipation; separate path
  from timing so fast arcs don't flatten; avoid "twins".
- John Lasseter, "Tricks to Animating Characters with a Computer",
  SIGGRAPH 94 course / CG 35(2) 2001, ACM. Used: eyes→head→body order
  for thought-driven moves, reversed for force-driven; moving holds.
- Richard Williams, *The Animator's Survival Kit*, Faber & Faber, 2001.
  © R. Williams. Used: blink on head turns; accents lead sound by ~2
  frames; head accents ("mostly up; anticipate down, accent up"); walk
  on 12s / march time; invisible 1–3 frame anticipations.
- Preston Blair, *Cartoon Animation*, Walter Foster, 1994. Used:
  anticipation–action–settle as the universal three-phase envelope;
  accents occur at starts/stops/spacing/direction changes; phrase-level
  posing; head tilt as a first-class emotion channel.
- Harold Whitaker & John Halas, *Timing for Animation*, Focal Press,
  1981. Used: idle blink every 3–5 s; asymmetric blink inbetweens;
  ~1/5 s attention latency to new motion.
- Pixar in a Box (Khan Academy × Pixar), "Animation" unit. Partner
  content © Pixar/Disney. Used: spacing-vs-timing framing; ease curves
  as the emotional carrier of identical keys.

## Perception / physiology studies (facts cited)

- Bentivoglio et al., "Analysis of Blink Rate Patterns in Normal
  Subjects", *Movement Disorders* 12(6), 1997 — blink rates 17/26/4.5
  per minute (rest/conversation/reading), log-normal intervals.
- Trutoiu, Carter, Matthews, Hodgins (Disney Research/CMU), "Modeling
  and Animating Eye Blinks", *ACM TAP* 8(3), 2011 — asymmetric blink
  kinematics; ~233–300 ms preferred; asymmetric-eased beats symmetric.
- VanderWerf et al., *J. Neurophysiol.* 89, 2003 (via review
  literature) — close ~92 ms, open ~242 ms.
- Evinger et al., "Not looking while leaping", *Exp. Brain Res.* 100,
  1994 — blinks accompany ~97% of gaze shifts > 33°.
- Lee, Badler & Badler, "Eyes Alive", SIGGRAPH 2002, ACM — saccade
  magnitude/direction statistics, main-sequence duration, talking vs
  listening gaze-state durations; user study on random vs statistical
  gaze. (Author postprint read; not redistributed.)
- Argyle & Cook, *Gaze and Mutual Gaze*, CUP 1976 (via Eyes Alive) —
  ~75% listener vs ~41% speaker eye contact.
- Colburn, Cohen & Drucker, MSR-TR-2000-81 — gaze state-machine
  timings; nod-on-gaze-shift ~70%.
- Martinez-Conde et al. (review) — microsaccades 1–2/s, ≤ 1°.
- Stivers et al., PNAS 2009 — conversational response gap ~0–200 ms
  (reaction budget context).

## Robotics systems (papers/talks — ideas only)

- Pan, Choi, Kennedy, McIntosh, Campos Zamora, Niemeyer, Kim, Wieland,
  Christensen (Disney Research), "Realistic and Interactive Robot
  Gaze", 2020 — layered "Alive Show" (breath/blink/saccade as the
  animacy floor), eyes-lead-head, habituation, minimum engage dwell.
- Breazeal et al., "Active Vision for Sociable Robots", IEEE SMC-A 31,
  2001 (Kismet) — conjunctive eyes (independent-eye errors disturb),
  attention habituation, motion-quality change as the "I saw you" cue.
- Ribeiro & Paiva, "The Illusion of Robotic Life", HRI 2012 —
  principles-for-robots translation: blinks + sinusoidal breathing as
  secondary action; asymmetry as solid drawing; arcs for gaze sweeps.
- Anki Cozmo/Vector design history (trade press; Carlos Baena) — the
  simplify-the-eyes lesson. Parameterization studied via **pycozmo**
  (MIT) as the clean-room reference only.

## Game animation (talks/articles — ideas only)

- David Rosen, "An Indie Approach to Procedural Animation", GDC 2014 —
  few keyframes + procedural interpolation.
- Ryan Juckett, "Damped Springs" (2012) — damped spring closed forms
  (re-derived; this contribution uses stateless eased settles instead).
- t3ssel8r, "Giving Personality to Procedural Animations using Math"
  (2022) — second-order-system intuition: response < 0 anticipates,
  damping < 1 overshoots. Math re-derived conceptually; no code reused.
- Robert Penner, easing equations (2001), BSD-3-Clause — see reuse
  table.
- Ken Perlin, "Real Time Responsive Animation with Personality" (IEEE
  TVCG 1995) and Perlin & Goldberg, "Improv" (SIGGRAPH 1996) — noise
  octaves as motion texture; layered/composited actions; personality as
  parameter mixes. "Improving Noise" (SIGGRAPH 2002) — the quintic fade
  polynomial.
- AnimSchool "Breathing Life into Idle Animations" (2024); MoCap Online
  idle guide; Wikipedia/TV Tropes on idle-animation history (Sonic
  escalation ladder); pixel-art idle guides (2-frame breathing idles at
  400–500 ms/frame).

## GitHub repositories inspected (license verified per repo)

| Repo | License | Interaction |
|---|---|---|
| zayfod/pycozmo | MIT | Read as the clean Cozmo `ProceduralFace` parameter reference; no code copied |
| stack-chan/m5stack-avatar (formerly meganetaaan/) | MIT | Behavior analyzed (blink/saccade/breath timings); no code copied |
| FluxGarage/RoboEyes | **GPL-3.0** | Behavior analyzed only; **no code copied** (copyleft) |
| adafruit/Uncanny_Eyes | MIT per file header (no repo LICENSE) | Approach noted (bitmap LUT eyes); not used |
| stack-chan/stack-chan | Apache-2.0 | Context only |
| anki/cozmo-python-sdk, anki/vector-python-sdk | Apache-2.0 | Context only |
| digital-dream-labs/vector | **Proprietary** (DDL Software Asset License v1.0, Vector-robot-only) | **Not used**; identified as unusable for this project |
| skeeto/hash-prospector | Public domain / CC0 | lowbias32 constants reused (see summary) |

## Caveats recorded by the research passes

- "Double blink" frequency has no documented statistic; it is treated
  as an animator's device (implemented, flagged in RESEARCH.md).
- The Williams "X frames down, Y frames up" blink chart could not be
  verified in the book; the blink envelope here relies on Trutoiu et
  al. and Thomas & Johnston instead.
- Doug Dooley (ex-Pixar) animated for **Kuri**, not Jibo; no verifiable
  primary source on Jibo's animation internals was found.
- Bentivoglio's conversation blink-rate increase vs this engine's
  mid-utterance suppression is a deliberate divergence, documented in
  RESEARCH.md §2.
