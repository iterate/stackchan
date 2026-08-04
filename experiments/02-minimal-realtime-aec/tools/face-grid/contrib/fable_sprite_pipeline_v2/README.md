# FSPP — Fable sprite pipeline v2

A deterministic, stdlib-only assembly line from **raw image-model sprite
sheets** (or authoring briefs for prompt/reference sources) to **validated
FSPR v2 `face_sprite_atlas_t` flash tables** plus a browser/WASM-friendly
JSON manifest, at **multiple device resolutions**.

This is the vertical slice of `docs/sprite-avatar-pipeline.md`'s authoring
assembly line (steps 2–10): the compiler owns resolution, palette,
transparency, landmarks, fallbacks, and validation; image-model output is
source material, never trusted runtime data.

```
prompt/reference ──▶ authoring brief + layout contract  (spritepipe.py brief)
                            │  (image model, human in the loop)
                            ▼
raw sheet PNG ──▶ key background ──▶ segment (component clustering)
      ──▶ pixel-snap (pitch detect + median resample)
      ──▶ register on rig canvas (shared scale, baseline, erase baked mouths,
          synthesize blink lids, crop mouth-study faces)
      ──▶ quantize (deterministic median cut, despeckle)
      ──▶ per-target derive (face_fill / stage framing, NN + exact anchors)
      ──▶ emit  atlas.c/.h  +  manifest.json  +  preview sheets
      ──▶ validate  registration · clipping · palette · coverage ·
                    budgets · legibility · PackBits round-trip ·
                    player contract · determinism (full double build)
```

## Quick start

```sh
# everything: fixture sheet, tests, both sample builds, C smoke tests
make all

# individual pieces
make fixture              # deterministic synthetic "image-model" sheet
make test                 # 25 unit + end-to-end tests (python3, stdlib only)
make samples              # build samples/bloomling and samples/mossling
make c-smoke              # compile emitted atlas + PRODUCTION player, render
python3 -B spritepipe.py build --spec fixtures/bloomling_spec.json --out out/
python3 -B spritepipe.py brief --spec my_prompt_spec.json --out brief/
```

`STACKCHAN_MAIN_DIR` (default `../../../../firmware-ws/main`) locates the
firmware headers/player for the C smoke test. No Python dependencies, no
network; identical inputs always produce identical bytes (the build runs
its whole pipeline twice and diffs every artifact hash before writing).

## What is demonstrated, with evidence

| Claim | Evidence |
|---|---|
| Real image-model sheet compiles unmodified | `samples/mossling/` from `tools/face-grid/assets/mossling/pocket-mossling-expression-viseme-concepts-v1.png` |
| Synthetic worst case (grid wobble, AA, noise, gradient bg, baked mouths, detached decorations) | `samples/bloomling/` from `fixtures/make_fixture_sheet.py` |
| Production player accepts the output | `make c-smoke` / `c-smoke-mossling`: `face_sprite_player_init` + 88-frame sweep through `firmware-ws/main/face_sprite_sheet.c`, replayed bit-identically |
| Face-filling portrait and zoomed-out character from one catalog | each sample's `cores3-face/` (80×60 ×2) vs `pocket-stage/` (40×30 stage framing) |
| All four viseme vocabularies resolve to pixels | `*/manifest.json` `viseme_map` (OVR15/VRM5/Preston9/Microsoft22 + custom) with build-time role fallbacks |
| Blink without authored lids | `*/previews/blink_x*.png` — lids synthesized from the portrait's own colors |
| Erased baked mouths + replaceable 23-slot mouths | `*/previews/mouths_x*.png` |

Numeric gates reject; they never promote. Visual acceptance stays a human
decision over the preview sheets, per the repo's review discipline.

## Layout

```
spritepipe.py            CLI: brief · build · validate
fspp/                    pipeline modules (stdlib only)
  png_io.py                deterministic PNG codec
  raster.py                background detect + flood-fill keying
  segment.py               band + connected-component segmentation
  snap.py                  fake-pixel pitch detection and snapping
  rig.py                   shared scale, registration, erase, lid synthesis
  quantize.py              median-cut palette, despeckle
  catalog.py               target derivation and atlas assembly
  emit_c.py                FSPR v2 C tables
  manifest.py              JSON manifest
  previews.py              reference compositor + contact sheets
  validate.py              gates
  pipeline.py              orchestration, determinism proof, briefs
  vocab.py                 23-slot mouth vocabulary, viseme maps, 11 targets
  spec.py                  character spec validation
schema/                  JSON Schemas for the spec and the manifest
fixtures/                bloomling generator + specs (bloomling, mossling)
tests/                   run_tests.py + c_smoke/harness.c
samples/                 generated sample outputs (committed evidence)
```

## Character spec

One JSON file per avatar (see `schema/character_spec.schema.json` and the
two fixtures). It declares the source (sheet path or prompt), the sheet
layout as named rows, the rig (canvas, baseline, mouth box, eye boxes), the
palette budget, and the device targets. Mouth rows may be isolated cels
**or** whole mouth-study faces — `rig.mouth.source: auto` detects which and
crops study faces at the mouth box after registration.

## Fallbacks

- **Mouth slots**: authored slots cover a subset of the 23; every missing
  slot borrows the cel of its canonical role's preferred slot at build time
  (recorded as `borrowed_from` in the manifest), so the player needs no
  fallback logic and every viseme of every vocabulary lands on pixels.
- **Expressions**: banks are selected by action-space proximity in the
  player; the coverage gate reports the nearest-bank distance for all 11
  canonical targets when a sheet authors only a subset.
- **Prompt sources**: `spritepipe.py brief` emits the constrained sheet
  contract (rows, background, palette, framing rules) for the image model,
  and the returned sheet enters the same pipeline via `--sheet`.

## Integration

See `INTEGRATION.md`. Summary: the emitted `*_atlas.c` compiles against
`firmware-ws/main/face_sprite_sheet.h` unchanged; a thin
`face_sprite_mossling.c`-style wrapper (static player + lazy init) plus a
`face_render.c` profile row and dispatch case exposes it on device and in
the face-grid WASM room. Nothing in this pack touches shared files.

## Licensing

All code and the synthetic Bloomling art are original and CC0-1.0. The
Mossling sample derives from the project-local generated concept sheet
already in `tools/face-grid/assets/mossling/`. Never feed the compiler
ripped sprites or copyleft sheets; the spec carries a `license` field and
the manifest records source hashes for audit.
