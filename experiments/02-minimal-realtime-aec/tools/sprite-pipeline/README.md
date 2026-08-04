# StackChan sprite-only avatar pipeline

This is the production-facing front door for authored avatars. It accepts
separate regular expression and mouth grids, keys every cell independently,
and compiles the result through the allocation-free FSPR C/WASM runtime.

To build, validate, and install every authored character into both the
CoreS3 firmware catalogue and the browser/WASM catalogue:

```sh
python3 -B avatar_pipeline.py publish
```

Adding a character is deliberately folder-shaped: create
`characters/<character-id>/avatar.json`, place the immutable expression and
mouth grids it names under that folder's `source/`, then run `publish`. The
command assembles and validates every target, emits its indexed C atlas,
installs the CoreS3 products, and regenerates the catalogue consumed by the
shared native/WASM renderer. No JavaScript or firmware registry edit is
required when another character is added.

To iterate on only one character without installing it:

```sh
python3 -B avatar_pipeline.py build \
  --character characters/gameboy-bot/avatar.json
```

Each character keeps immutable image-model outputs under `source/`.
Reproducible compiler products go under `generated/`:

- `source-sheet.png` — cleaned multi-source assembly;
- `fspp-spec.json` and `assembly-report.json` — full geometry/provenance;
- `build/*/manifest.json` — browser-readable FSPR data;
- `build/*/*_atlas.c/.h` — the same indexed cells for ESP32;
- `build/*/previews/*.png` — visual acceptance sheets;
- `debug/` — segmentation, registration, and snapped-cell evidence.

`target.frame` is the physical render surface; `target.canvas × target.scale`
is the pixel-art image centred inside it. The current profiles prove that the
same authored character and C renderer can target:

- CoreS3: 80×60×2 or 40×30×4 inside a 160×120 logical surface;
- M5StickS3: 40×30×4 inside its 240×135 landscape surface.

Board code supplies the surface and flushes its RGB565 pixels. It does not
know how cells, expressions, visemes, or animation frames are stored.
