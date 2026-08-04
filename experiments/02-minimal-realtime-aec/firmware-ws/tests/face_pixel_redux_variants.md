# Pixel Redux authored variant pack

This is an isolated, allocation-free renderer pack for review before profile
IDs are assigned in the shared face registry. It contains eighteen procedural
characters: three interpretations of each of the five Pixel Redux bases, plus
a second strict four-shade handheld family.

The pack does not contain a sprite sheet or copied bitmap art. The Pocket
Mossling and DMG families are drawn from deterministic rectangles, ellipses,
lines, triangles and quads. Concept art informed silhouette and acting choices
only.

## Integration contract

- Include `face_pixel_redux_variants.h`.
- Call `face_pixel_redux_variant_render(variant, key, sample_clock, pixels,
  FACE_PIXEL_REDUX_PIXEL_COUNT)`.
- The output is RGB565 at 160x120. Artwork is authored on an 80x60 logical
  grid and retains a two-logical-pixel safe border.
- `sample_clock` is the PCM-domain clock. It drives deterministic speech
  secondary action and slow idle motion; no hidden wall clock or renderer
  state is required.
- The only undefined production symbol in the optimized object is
  `face_pixel_redux_actor_resolve`; there are no heap calls.

The enum order, slugs, names and visual theses are frozen in
`face_pixel_redux_variants_manifest.json`.

## Pocket acting

The six Pocket/DMG actors use exactly these four RGB colours before RGB565
quantisation:

```text
#0f380f  #306230  #8bac0f  #9bbc0f
```

The procedural Mossling trio adds:

- coherent whole-head bob with a slower occasional two-pixel turn;
- a staggered left/hold/right blink rather than simultaneous lid snapping;
- expression-led leaf-ear rotation and sprout/reed/cap secondary action;
- excited-eye sparkle clusters distinct from ordinary surprise;
- continuous viseme-driven open, wide, round, pressed, teeth and tongue axes;
- speech-start anticipation and speech-end settle inherited from the shared
  Pixel Redux pose resolver.

The Tin Warden, Lantern Moth and Slime Courier deliberately use different
material grammars: rigid hinged plates, light wing/antenna follow-through and
elastic gel opposed by a rigid bag.

## Repeatable review

Run:

```sh
firmware-ws/tests/run_face_pixel_redux_variants_native.sh \
  local/pixel-redux-variants-final
```

The command performs:

- ASan and UBSan tests over all expressions and visemes;
- 256 adversarial keyframes per variant, guarded on both sides;
- strict safe-border and four-colour assertions;
- deterministic idle/bob/turn/blink checks;
- no-heap object inspection and a native CPU benchmark;
- native 160x120 and exact 40x30 labelled contact sheets;
- nearest-neighbour MP4 previews for the six Pocket/DMG actors.

Important review artifacts are:

- `labelled-pocket-mossling-expressions-11-exact40x30-4x.png`
- `labelled-pocket-mossling-visemes-15-exact40x30-4x.png`
- `labelled-pocket-mossling-speech-blink-24f-exact40x30-4x.png`
- `labelled-pocket-mossling-idle-turn-blink-32f-exact40x30-4x.png`
- the equivalent `labelled-dmg-handheld-*` matrices
- per-character `*-native160-nearest4x-10fps.mp4` previews

The exact-40 sheets use nearest-neighbour centre sampling, so they catch
details that disappear at the smallest intended grid rather than merely
showing a scaled-down native image.
