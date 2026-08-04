# fable_sprite_sheet — generic sprite-sheet face engine

A tiny, asset-agnostic sprite-sheet animation system for talking
faces: a compact indexed-colour atlas format (FSPR), a build-time
converter for PNG/Aseprite input, and an integer-only C playback
engine driven by the stable 12-byte `face_keyframe_t` and the 16 kHz
sample clock. Nothing here couples the engine to any particular art —
swapping in a legally licensed old-game-style sheet is a manifest plus
one converter run (see docs/LICENSING.md for what "legally" means).

## Quick start

```sh
./build.sh            # generate demo art, convert, compile, test
./wasm/build-wasm.sh  # WASM build + byte-identical browser check
```

`build.sh` locates the firmware headers at
`../../../../firmware-ws/main`; set `STACKCHAN_MAIN_DIR` if this
directory is somewhere else relative to the experiment tree.

Extra tools:

```sh
./build/test_sprite_face --update        # refresh golden CRCs
./build/test_sprite_face --dump DIR      # PPM frames for eyeballing
./build/test_sprite_face --raw DIR       # RGB565 dumps for the wasm check
python3 -m http.server -d . 8080         # then open /viewer/
```

## Layout

| Path | Contents |
|------|----------|
| `src/sprite_sheet.h` | FSPR atlas data format (pure const data) |
| `src/sprite_face.h/.c` | playback engine (integer-only, no alloc) |
| `tools/atlas_convert.py` | PNG+manifest / Aseprite JSON → C data |
| `tools/gen_demo_sheets.py` | generates the four original demo faces |
| `tools/png_io.py` | stdlib-only PNG codec |
| `tools/test_converter.py` | converter self-tests incl. Aseprite mode |
| `assets/<face>/` | generated sheet.png + manifest.json |
| `generated/` | converter output (C atlases + packing stats) |
| `tests/` | native harness, shared scenario, golden CRCs |
| `wasm/` | WASM bridge, build script, byte-identical check |
| `viewer/` | interactive browser viewer (sliders + talk test) |
| `docs/` | format spec, research notes, licensing, measurements |

## What the engine does

- Composites base → brows → eyes → pupils → mouth → idle overlays
  from flash-resident cells (the Sierra-portrait base-plus-patches
  model; every non-base layer is a small delta rectangle).
- Selects one of nine canonical mouth shapes (Rhubarb/Preston Blair
  X A B C D E F G H) from the continuous keyframe mouth parameters via
  data-driven thresholds; per-atlas subsets are resolved through
  build-time fallback chains, so a two-frame sheet still talks.
- Debounces shapes with Rhubarb's timing rules (70 ms minimum hold,
  120 ms before a pause may close the mouth) and inserts the classic
  in-betweens (A/B→D through C, C/D→F through E).
- Runs deterministic idle behaviour from hashed clock windows:
  autonomous blinks (fast close, slow open, occasional double blink,
  blink-on-gaze-change), pupil saccades, breathing bob, idle acts
  (sequences), and classic palette cycling — all pure functions of the
  sample clock, so native and WASM output is byte-identical.
- Uses caller-owned buffers only; `sprite_face_t` is 32 bytes of
  state, rendering allocates nothing, arithmetic is integer-only.

The demo art (all CC0, procedurally generated — see
docs/LICENSING.md) covers four styles and four feature subsets:
`ega_sorcerer` (16-colour EGA portrait, all nine shapes, palette-
cycled candle), `handheld_gobbo` (four-shade green, four shapes →
fallback exercise), `vga_navigator` (32-colour android, cycled status
strip), `terminal_operator` (amber phosphor, single wide eye slot).

## Swapping in real old-game-style sheets

1. Verify the licence first (docs/LICENSING.md): CC0 assets can be
   embedded as C arrays without obligations; CC-BY needs a credits
   file; CC-BY-SA/GPL and itch.io "royalty-free" packs are generally
   unsuitable for a public repo. Never use ripped sprites.
2. Slice the sheet: either write a grid manifest naming cell
   rectangles (docs/FORMAT.md, "Grid manifest") or, if the art ships
   as an Aseprite file, export with
   `aseprite -b face.ase --sheet sheet.png --data data.json
   --format json-array --list-tags --list-slices`
   and write a small role map (docs/FORMAT.md, "Aseprite mode").
3. Map whatever mouth frames exist onto the canonical shape letters;
   missing shapes fall back automatically. A bare
   open/half/closed mouth set maps as `D`, `C`, `X`.
4. Run `tools/atlas_convert.py`, add the generated `.c` to the build,
   and pass the atlas to `sprite_face_init`.

## Integration notes (for the primary agent)

- The engine is self-contained: include `src/` and the generated
  atlas, compile `sprite_face.c`, done. It only needs
  `face_keyframe.h`/`face_pose.h` from `firmware-ws/main`.
- `sprite_face_render` fills a full 160×120 RGB565 frame and matches
  the `face_render_frame` contract (keyframe + sample clock, caller
  buffer, no allocation); wiring it behind a
  `FACE_RENDER_FAMILY_PIXEL`-style profile is a thin adapter. The
  stateful context lives happily in a static.
- Since only mouth/eye rectangles change between most frames, a
  dirty-rect LCD flush (Abrash-style) is a natural firmware follow-up;
  the slot anchors already give the bounding boxes.
- Stack use is bounded by one 160-byte row buffer plus a 512-byte
  cycled-palette copy (only when an atlas uses cycles).
