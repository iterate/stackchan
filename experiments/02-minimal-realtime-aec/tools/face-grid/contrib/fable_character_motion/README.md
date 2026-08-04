# fable_character_motion — animation-principles motion engine

Contribution from the character-animation principles worker (see
`BRIEF.md`). It turns Disney/Pixar animation principles, game procedural
animation, and social-robotics gaze science into a **stateless,
integer-only motion engine** plus **five original face studies** for the
160×120 RGB565 renderer lab.

Everything is a pure function of `(persona, 12-byte keyframe, 16 kHz
sample clock)`: no retained state, no allocation, no floating point, no
`Date.now()`-style hidden inputs. Two calls with equal inputs are
byte-identical on any little-endian, arithmetic-right-shift platform
(host, ESP32-S3, wasm32). Golden CRCs pin 25 frames; `run_tests.sh`
rebuilds the suite with emcc and re-checks those natively-generated CRCs
under wasm32/node, so browser byte-identity is verified, not assumed.

## Layout

| Path | What it is |
|---|---|
| `src/fable_ease.h/.c` | Q10 easing/oscillator/hash/value-noise toolkit + the O(1) deterministic event scheduler |
| `src/fable_motion.h/.c` | persona tables + the motion engine (`fable_motion_eval`) |
| `src/fable_studies.h/.c` | five face studies mirroring the `face_render_frame` contract |
| `src/fable_keyframe.h` | standalone ABI mirror of the stable 12-byte keyframe |
| `tests/` | unit tests, golden frames, benchmark, float-poison header |
| `tools/render_ppm.c` | contact-sheet renderer for visual review |
| `MANIFEST.md`, `manifest.json` | motion manifest (functions, personas, studies) |
| `RESEARCH.md` | research digest: principle → algorithm, with citations |
| `SOURCES.md` | source and licensing report |

## Build and verify

```sh
./run_tests.sh   # optimized tests, UBSan+ASan tests, float poisoning, audit
make bench       # ns/eval and us/frame on the host
make ppm         # contact sheets into out/ (PPM; `sips -s format png` to view)
make golden      # print the golden CRC table after an intentional change
```

No dependencies beyond a C11 compiler and make.

## Contract

`fable_study_render(study, keyframe, sample_clock, rgb565, capacity)`
mirrors `face_render_frame()` in `firmware-ws/main/face_render.h`. The
keyframe may be `NULL` for silent idle previews. `keyframe->expression`
is read as the activity enum that `face_keyframe_from_pose()` stores
(idle / listening / thinking / speaking), which stages listening vs
speaking poses; unknown values degrade to idle/speaking safely.

Keyframe authority is preserved: host-commanded lids compose with
procedural blinks by minimum (`eye_left_open = 0` forces the eye shut at
any clock), host `look_x/look_y` becomes the attention point that
procedural gaze explores around, and host mouth bytes pass through
untouched except while a yawn act is staged and the speaking flag is
clear.

## The five studies

| Slug | Persona | Principles staged |
|---|---|---|
| `curious-scout` | Curious Scout | eye lead / head follow / body drag, anticipation, arcs (antenna follow-through) |
| `ember-breath` | Calm Ember | squash and stretch on a breathing body, blink phrasing |
| `pip-spark` | Perky Pip | overshoot and settle, exaggeration (150% offsets), secondary cheeks |
| `moss-drowse` | Sleepy Moss | slow in/slow out, heavy tilted lids, yawn and sigh acts |
| `sage-stager` | Attentive Sage | staging: activity-tinted lighting, mutual-gaze pupils, nods and speech accents |

## Performance

Host (Apple silicon, `-O2`): motion eval ≈ 0.11 µs; full frames 1.4–2.9
µs. The renderers are span-fill only (no per-pixel shading), so even at
a pessimistic 100× host-to-ESP32-S3 factor a frame stays under 0.3 ms —
two orders of magnitude inside the 33 ms/30 fps budget. The engine
itself is cheap enough to run per scanline if anyone ever wants that.

## Determinism notes

- All arithmetic is on explicit-width integers; 64-bit intermediates
  guard every Q10 cubic/quintic against overflow. UBSan runs clean.
- Arithmetic right shift on negatives is implementation-defined in ISO C
  but arithmetic on all supported toolchains; every test binary asserts
  it at startup instead of assuming silently.
- Episodic behavior (blinks, fixations, acts, nods, accents) derives
  from the clock through a slot scheduler (`fable_schedule`), so any
  frame is O(1) and frames may be evaluated in any order.
- The clock wraps after ~74.5 hours at 16 kHz; schedules simply reseed
  at the wrap (a one-frame retiming, no unsafe behavior).
- Activity changes (idle → speaking etc.) switch gaze gains
  discontinuously by design; the host should not flip activity more
  than a few times a second or small (<= 3 px) gaze pops can show.
