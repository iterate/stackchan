# Brief: fable_sprite_pipeline_v2

Independently build a concrete contribution under
`tools/face-grid/contrib/fable_sprite_pipeline_v2` only (plus
`local/fable_sprite_pipeline_v2` for disposable artifacts). Do not edit
production files.

We need an assembly line that accepts a prompt/reference image or raw
image-model sprite sheet, pixel-snaps and palette-quantizes it,
segments/catalogues reusable layers/frames, supports fallbacks, emits
compact C/flash bitmap data plus a browser/WASM-friendly manifest,
produces multiple target resolutions, and validates registration,
clipping, palettes, coverage, and determinism.

First thoroughly inspect `docs/sprite-avatar-pipeline.md`,
`docs/task-multi-device-abstraction.md`, existing `face_sprite_sheet.*`,
`face_sprite_mossling*`, `tools/face-grid`, and existing
`fable_sprite_sheet` prior art. Implement a usable vertical slice with
README, schema, CLI/tooling, fixtures/tests, and generated sample
outputs. Design for face-filling StackChan portraits as well as
zoomed-out characters. Use the existing 12-byte keyframe / richer
renderer IR where appropriate. Do not commit.
