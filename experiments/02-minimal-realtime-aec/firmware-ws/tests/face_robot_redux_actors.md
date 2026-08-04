# Robot Redux Actors

`face_robot_redux_actors.c` is a standalone, heap-free replacement set for
legacy renderer IDs 9 through 14. It renders the same 160×120 RGB565 surface
used by the firmware and WebAssembly builds, but is deliberately isolated from
the production dispatcher so the actors can be reviewed before integration.

## Replacement map

| Legacy ID | Style | Mouth model | Speech performance |
|---:|---|---|---|
| 9 | RoboEyes Alert Performer | none | angular sockets, planar lids, focus cores, spacing, and sharp anticipation |
| 10 | RoboEyes Soft Performer | none | elliptical recesses, curved lids, pupil gaze, warm glimmers, and gentle anticipation |
| 11 | M5 Avatar Classic Performer | cavity | continuous jaw/lip cavity, corners, teeth, and tongue |
| 12 | M5 Avatar Manga Performer | manga | continuous cavity plus manga eye, cheek, and brow acting |
| 13 | EVE Minimal Performer | none | binocular aperture, spacing, tilt, glow, and body acting |
| 14 | Jibo Orb Performer | none | monocular aperture, iris scale, lid, orbital accents, and body acting |

The mouthless actors are intentional. They do not grow a small decorative
mouth when speech starts. Their existing anatomy performs the speech instead,
which preserves each silhouette and avoids the usual audio-reactive
jaw-flapper look.

## Input and acting model

Every renderer consumes all 40 bytes of `face_render_key_t`, schema version 2.
The record separates four concerns:

1. continuous jaw, eye, gaze, and brow controls;
2. source speech analysis and audio energy;
3. viseme vocabulary, secondary viseme, blend, and speech phase;
4. authored emotion, affect, attention, head, and body direction.

OVR15, VRM5, Preston-Blair 9, Microsoft 22, and custom vocabularies can share
the same renderer boundary. The current actors resolve OVR15, VRM5, and
Preston-Blair inputs into continuous open/width/round/press controls and retain
the secondary shape for coarticulation.

`stage_expression` is independent of listening/thinking/speaking activity. All
six actors author distinct poses for neutral, warm, joy, concern, surprise,
thoughtful, skeptical, determined, sleepy, excited, and embarrassed. Emotion
changes lids, brows, corners, gaze, posture, or orbital accents; it is not a
palette-only change.

Speech timing has an explicit performance arc:

- frame 0: neutral baseline;
- frame 1: visible anticipation;
- active frames: viseme and PCM-derived continuous articulation;
- frames 13–14 in the native strip: two-frame settle;
- frame 15: rest.

Eye socket anchors remain fixed throughout speech. Motion happens inside and
around the sockets, which prevents the jittery "loose stickers" effect.

### Featured eye-only actors

Legacy IDs 9 and 10 intentionally use different topology rather than palette
variants:

- Alert has crisp octagonal housings, independently sloped upper and lower lid
  planes, compact dark focus cores, and orange socket-mounted activity rails.
  Its authored asymmetry makes concern, thought, skepticism, determination,
  sleep, and embarrassment readable without symbols.
- Soft has broad elliptical recesses, organic apertures, curved smiling lower
  lids, larger gaze pupils, and warm rim glimmers. Joy compresses upward from
  the lower lid while concern and embarrassment pull the upper planes inward.

Neither actor renders anything in the mouth zone. Speech uses a restrained
change in aperture and spacing, internal gaze travel, focus-core shape, a
one-frame lid anticipation, active micro-motion, and a two-frame settle. IR
details and phoneme identity are rendered as socket-mounted rail lengths, not
detached diagnostic specks.

## Source lineage

The implementations were written from scratch after studying local,
source-first checkouts:

- FluxGarage RoboEyes, commit `b42f8e596535`: rounded primitive eye anatomy,
  eyelid occlusion, mood geometry, and cheap embedded redraw;
- meganetaaan/m5stack-avatar, commit `7a90083edd9b`: separable eye, brow, mouth,
  and expression parts on a small M5 display;
- the local Vector/Cozmo engine checkout at commit `669ec7499a70` and pycozmo
  at `1b6dcd9b869a`: stable eye anchors and expression through continuous
  corner radii, upper/lower lid height, bend, angle, scale, and gaze
  parameters.

No bitmap, sprite, or source asset was copied. The result remains integer-only
and uses direct RGB565 primitives.

## Native verification

Run:

```sh
./firmware-ws/tests/run_face_robot_redux_actors_native.sh \
  local/face-robot-redux-actors
```

The runner performs:

- ASan and UBSan execution;
- deterministic-frame and API rejection tests;
- 4-pixel safe-border and guard-canary checks;
- 11-expression and 15-viseme visual-uniqueness checks for every actor;
- strict proof that each byte in the 40-byte IR can alter every actor;
- socket-anchor stability through a 16-frame speech sequence;
- substantial emotion deltas at both native and 40×30 contact scale;
- an empty mouth zone and bounded featured-eye topology changes;
- one-frame anticipation and two-frame settle checks;
- continuity bounds and 1,536 adversarial renders;
- direct legacy-ID equivalence;
- undefined-symbol rejection for heap allocation;
- optimized object-size reporting and a native render benchmark.

Review these generated sheets at original scale:

- `all-expressions.png`: six actor rows × eleven authored emotions;
- `all-expressions-contact.png`: the same matrix at 40×30 per cell;
- `all-visemes.png`: six actor rows × fifteen OVR visemes;
- `all-speech-16f.png`: chronological performance and settle sequence.
- `all-speech-16f-contact.png`: the temporal matrix at 40×30 per cell.

The artifact directory also contains one expression and speech strip per actor,
the native benchmark, object-size report, sanitizer log, and a text manifest.

## Integration boundary

The parent integration only needs to map legacy IDs 9–14 to
`face_robot_redux_render_legacy()`, or use the typed
`face_robot_redux_render()` API. The standalone module does not modify the
dispatcher, renderer enum, build graph, or shared animation state.
