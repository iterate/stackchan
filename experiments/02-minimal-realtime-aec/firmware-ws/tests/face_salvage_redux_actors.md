# Salvage-redux actor pack

`face_salvage_redux_actors.c` is a standalone visual replacement pack for the
six weakest legacy atlas rows. It intentionally does not edit the dispatcher,
CMake, firmware integration, or WASM exports.

| Legacy ID | Redux actor | Visual grammar |
|---:|---|---|
| 4 | Story Scout | Clean cinematic large/small shape language, soft eyes, attached curved mouth |
| 6 | Pocket Courier | Two-pixel handheld-game silhouette with vector-quality face acting |
| 29 | Vela | Minimal mouthless luminous eye acting in the EVE/Anki lineage |
| 35 | Kite Oracle | Faceted folded-paper mask and attached ribbon-fold mouth |
| 36 | Orbit Gardener | Circular botanical automaton with petal irises and segmented voice arc |
| 52 | Felt Familiar | Bold theatre-puppet silhouette with stitched elastic features |

These are independent drawings, not one chassis with six palettes. Each actor
uses direct RGB565, caller-owned memory, strict integer arithmetic, and fixed
facial anchors. Head/body input affects gaze, brows, shading, or shoulders; it
never bobs the whole face. Mouth corners remain part of their respective
contours. Vela is deliberately mouthless and performs articulation through
eye aperture, width, roundness, lid pressure, and consonant-family notches.

## Acting contract

- All 11 `stage_expression` values change semantic face geometry.
- All 15 OVR visemes are visually distinct.
- The schema-v2 40-byte IR is consumed completely. Semantic fields drive their
  natural controls; remaining protocol variation appears as integrated scarf
  beads, belt studs, bezel vents, mask facets, orbit ticks, or collar stitches.
- Speech has two explicit anticipation frames, active articulation, two settle
  frames, then rest.
- Sample-clock motion uses a 9,600-sample triangle period. This avoids the old
  1,066-sample near-half-cycle strobe at a 533-sample/30 fps cadence.
- A four-pixel background border is preserved under adversarial input.

## Native review

Run:

```sh
firmware-ws/tests/run_face_salvage_redux_actors_native.sh \
  local/face-salvage-redux
```

The harness compiles strict C11 with `-Wall -Wextra -Wpedantic -Werror`, runs
ASan/UBSan and 1,536 adversarial renders, checks every IR byte, verifies fixed
anchors and timing, benchmarks all six actors, rejects heap references in the
optimized renderer object, and writes:

- `salvage-redux__6-actors__11-emotions__quiet.png`
- `salvage-redux__6-actors__11-emotions__quiet-contact-80x60.png`
- `salvage-redux__6-actors__11-emotions__quiet-device-40x30.png`
- `salvage-redux__6-actors__18f-speech__2-anticipation-2-settle.png`
- `salvage-redux__6-actors__18f-speech__2-anticipation-2-settle-contact-80x60.png`
- `salvage-redux__6-actors__18f-speech__2-anticipation-2-settle-device-40x30.png`
- `salvage-redux__6-actors__15-ovr-visemes__warm-active.png`
- `salvage-redux__6-actors__15-ovr-visemes__warm-active-contact-80x60.png`
- `salvage-redux__6-actors__15-ovr-visemes__warm-active-device-40x30.png`

Every native cell is 160×120. Review cells are unsmoothed center samples at
80×60 and the exact device-contact size of 40×30, deliberately exposing
one-pixel clipping and detached geometry rather than hiding it with
interpolation.
