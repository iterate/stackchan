# Close-up / Toon Actors

`face_closeup_toon_actors.c` is a standalone, heap-free replacement set for
legacy renderer IDs 42 through 51. It renders directly into the firmware and
WebAssembly 160×120 RGB565 surface. The module remains isolated from the
production dispatcher so the actors can be reviewed before integration.

## Replacement map

| Legacy ID | Actor | Distinctive rig | Mouth grammar |
|---:|---|---|---|
| 42 | Brow Dialogue Director | angular binocular dialogue mask | segmented signal ribbon and diamond aperture |
| 43 | Sleep/Wake Dreamer | soft moon face, night cap, symbolic accents | soft cupid bow and lower-lip crescent |
| 44 | Iris Parallax Scout | warm explorer helmet and oversized goggles | broad flat-topped adventure grin |
| 45 | Cat Optics Familiar | ears, almond eyes, slit pupils, muzzle, whiskers | split W muzzle and lower-heavy cavity |
| 46 | M5 Manga Lead | human hair silhouette, large eyes, lashes, manga accents | small petal/cupid anime mouth |
| 47 | VGA Star Navigator | purple helmet, dark visor, instrument-panel mouth | segmented console waveform and grille |
| 48 | Pocket Relay Creature | green creature silhouette and coral side gills | elastic lip with anchored corner dimples |
| 49 | EGA Quest Squire | angular steel helmet, plume, diamond eye apertures | hard faceted press and polygonal cavity |
| 50 | VGA Elder Storyteller | white hair, moustache, beard, small close-set eyes | narrow beard-framed lip and smile creases |
| 51 | Talkie Moon Mechanic | orange cap and deliberately asymmetric goggle rig | offset crooked lip and asymmetric tooth gap |

These are ten separately drawn character rigs, not palette variants. The
socket, brow, lid, gaze, cheek, mouth, head, and body systems share a compact
pose resolver, while each actor owns its silhouette, render anatomy, and mouth
topology. `mouth_kind` describes anatomy; the finer `mouth_grammar` metadata
identifies the ten distinct drawing/acting systems.

## Input, emotion, and speech

Every actor consumes all 40 bytes of `face_render_key_t`, schema version 2.
That record keeps PCM-derived continuous controls, source visemes, secondary
viseme/coarticulation, authored expression, affect, attention, gaze, and stage
direction independent.

OVR15, VRM5, and Preston-Blair 9 inputs resolve into continuous jaw open,
lip width, roundness, pressure, consonant, teeth, and tongue controls. Mouth
corners are parented to the mouth center, and the center follows the skull;
head movement therefore cannot leave detached lip corners behind.

All actors author neutral, warm, joy, concern, surprise, thoughtful,
skeptical, determined, sleepy, excited, and embarrassed poses. Emotion changes
geometry: brows, lids, gaze, pupils, cheeks, mouth corners, posture, and
actor-specific appendages. Mouth acting is explicit: warm/joy use rising
smile arcs, concern folds into a frown, thoughtful/skeptical become
asymmetric, surprise contracts into a small round or faceted aperture,
determined compresses into a firm press, sleepy relaxes, excited widens, and
embarrassed twists awkwardly. It is not implemented as a color swap.

The speech state is explicitly temporal:

- frame 0: neutral baseline;
- frame 1: anticipation with reduced jaw opening and alert eyes/posture;
- active frames: continuous PCM and viseme articulation;
- frames 13–14 in the native strip: two-frame settle;
- frame 15: rest.

Socket anchors remain fixed during speech. Gaze moves the pupils inside them,
and lids/brows deform around them, avoiding jittery floating-eye animation.
The subtle whole-face speech bob uses a 9,600-sample period at 16 kHz. The
period is intentionally much slower than the 30 fps render interval; the old
1,066-sample period landed near alternating half-cycles and visibly strobed.

## Embedded constraints

The production renderer:

- uses integer arithmetic only;
- performs no allocation;
- draws directly into RGB565;
- keeps an untouched four-pixel safety border;
- has deterministic output for a render key and sample clock;
- renders symbolic actor details only when appropriate to that actor.

The dump utility uses host-side allocation solely to assemble review sheets;
that code is not part of the embedded renderer.

## Native verification

Run:

```sh
./firmware-ws/tests/run_face_closeup_toon_actors_native.sh \
  local/face-closeup-toon-actors
```

The runner performs:

- ASan and UBSan execution;
- deterministic-frame and API rejection checks;
- guard-canary and four-pixel safe-border checks;
- topology-mask separation for every actor pair;
- eleven-expression and fifteen-viseme uniqueness for every actor;
- mouth-region uniqueness for all eleven expressions and ten unique mouth
  grammar identifiers;
- proof that each byte in the 40-byte IR can change every actor;
- fixed socket anchors through a 16-frame speech performance;
- explicit regression coverage for the 9,600-sample bob period;
- one anticipation and two settle frames;
- bounded frame-to-frame continuity;
- 2,560 adversarial renders;
- direct legacy-ID equivalence;
- rejection of heap allocator references in the optimized object;
- native render benchmark and code-size reporting.

Review these generated sheets at original scale:

- `all-expressions.png`: ten actor rows × eleven emotions;
- `all-visemes.png`: ten actor rows × fifteen OVR visemes;
- `all-speech-16f.png`: chronological anticipation, speech, and settle.
- `all-expressions-contact-80x60.png`: the emotion sheet with each native cell
  reduced to 80×60 using unsmoothed center sampling;
- `all-speech-16f-contact-80x60.png`: the same harsh contact-scale check for
  chronological speech.

The artifact directory also contains one expression and one speech strip per
actor, plus the benchmark, code-size report, sanitizer log, and manifest.

## Integration boundary

The parent integration maps legacy IDs 42–51 to
`face_closeup_toon_render_legacy()`, or calls the typed
`face_closeup_toon_render()` API. This standalone work intentionally does not
modify the dispatcher, renderer enum, build graph, or shared animation state.
