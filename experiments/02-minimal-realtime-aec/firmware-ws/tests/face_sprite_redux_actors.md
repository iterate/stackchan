# Sprite Redux production actors

`face_sprite_redux_actors.c` is the allocation-free production replacement
pack for three legacy sprite profiles. It keeps the shared dispatcher and
build wiring unchanged.

| Legacy ID | Actor | Readable game grammar |
|---:|---|---|
| 58 | Talkie Moon Mechanic | warm 16-bit repair-bay close-up, orange cap, offset goggle and headset |
| 59 | JRPG Storm Familiar | high-key violet cat familiar, expressive ears and anime eye cels |
| 60 | Handheld Forest Pet | four-tone handheld LCD pet with leaf ears and chunky square eyes |

The actors are independently composed, not palette swaps. Artwork is authored
on an 80×60 logical grid and enlarged to 160×120 with exact 2× pixels. Every
primitive clips to a four-logical-pixel safe area. That leaves an untouched
eight-pixel native border and a two-pixel border in the exact, unsmoothed
40×30 audit tiles.

## Sprite performance contract

The renderer consumes all 40 bytes of the schema-v2 render key. Eye and mouth
sockets remain fixed while gaze, lids, brows, mouth corners, head and body
performance, affect, attention, activity and speech phase animate around those
anchors.

All eleven stage emotions alter face geometry and/or the attached cap, ears or
leaf silhouette. The small corner symbols are garnish; expression identity
still comes through in the eyes, brows, mouth corners and silhouette when the
symbol is ignored.

Speech uses one static 15×9 palette-indexed source cel for every OVR15 shape:

`AA, E, I, O, U, PP, SS, TH, DD, FF, KK, NN, RR, CH, SIL`.

The blitter crops and integer-scales those authored cels into each actor's
palette. Continuous width, height, roundness, press, teeth, tongue, corner and
coarticulation inputs still shape the result. Strong viseme weights select the
semantic source cel directly, so speech cannot collapse back into a generic
line or amplitude rectangle.

The 16-frame temporal sheet is chronological:

| Frames | State |
|---:|---|
| 0–1 | two-step `STARTING` anticipation |
| 2–13 | active `AA E I O U PP FF TH SS RR CH AA` articulation |
| 14–15 | two-step `ENDING` settle |

Anticipation progressively opens the lids, compresses the mouth and lifts the
attached silhouette. Settle reverses those cues over two frames. No temporal
state or heap allocation is retained; identical actor/key/clock inputs produce
identical pixels, including in the WASM-compatible C path.

## Reproduce and inspect

From `experiments/02-minimal-realtime-aec`, run:

```sh
firmware-ws/tests/run_face_sprite_redux_actors_native.sh \
  firmware-ws/tests/artifacts/face_sprite_redux_actors
```

The focused runner performs:

- warnings-as-errors C11 builds;
- ASan and UBSan coverage with frame canaries;
- the same focused contract suite under Emscripten/Node when available;
- 512 adversarial 40-byte keys per actor (1,536 frames total);
- byte-by-byte proof that every render-key byte can change output;
- all-expression native and exact-40×30 distinctness;
- all-viseme cel selection and at least ten exact-40×30 visual results;
- fixed eye/mouth anchor and two-step anticipation/settle assertions;
- repeat-render determinism and direct-versus-legacy dispatch equivalence;
- a standalone-object allocator-symbol audit and CPU benchmark.

Review these six unlabeled sheets at original size:

- `sprite-redux-expressions-native-3x11.png`
- `sprite-redux-expressions-exact-40x30-3x11.png`
- `sprite-redux-visemes-native-3x15.png`
- `sprite-redux-visemes-exact-40x30-3x15.png`
- `sprite-redux-temporal-native-3x16.png`
- `sprite-redux-temporal-exact-40x30-3x16.png`

Rows are legacy IDs 58, 59 and 60. Expression columns are `neutral, warm, joy,
concern, surprise, thoughtful, skeptical, determined, sleepy, excited,
embarrassed`. Viseme columns follow the OVR15 order above.

The artifact directory retains PPM regeneration sources, PNG inspection
copies, native sanitizer and WASM test output, object-size details and
benchmark output.
