# FSPR atlas format

FSPR ("Fable SPRite") is a compact, flash-resident sprite-sheet
format for talking faces. It is emitted as plain C const data by
`tools/atlas_convert.py`; the authoritative struct definitions live in
`src/sprite_sheet.h` (`SPRITE_SHEET_VERSION` guards evolution).

## Model

A face is one or more **banks** (expression variants) composited from
**cells** — rectangles of palette indices — in painter's order:

```
base -> brow L/R -> eye L/R (lid state) -> pupil L/R -> mouth -> overlay
```

Everything except the base is a small delta patch pasted at an anchor,
the technique Sierra's SCI portrait player used (base bitmap + timed
mouth overlays) and the reason a whole talking face fits in a few KB.
Cells are trimmed to their opaque bounding box (stored `offset_x/y`
restore the logical box) and deduplicated by content at build time.

## Pixels

- Palette: RGB565 (`uint16_t`), at most 256 entries, index 0 reserved
  as the transparent index by the converter.
- Cell data: 8-bit palette indices, row-major, either raw
  (`SPRITE_CELL_ENCODING_RAW`) or PackBits run-length encoded
  (`SPRITE_CELL_ENCODING_PACKBITS`); the converter picks whichever is
  smaller per cell. PackBits control byte `n`: `0..127` = copy `n+1`
  literals, `128` = no-op, `129..255` = repeat next byte `257-n`
  times. Runs may span row boundaries.
- An atlas declares `native_width/height` and `scale` (1 or 2). The
  engine centres the face on the 160×120 output, filling the border
  with `background`, and nearest-neighbour upscales while blitting.

## Mouth shapes

The canonical shape set is Rhubarb Lip Sync's nine (the Hanna-Barbera
/ Preston Blair tradition): `X` rest, `A` closed-pressed (M/B/P), `B`
clenched teeth, `C` half open, `D` wide open, `E` rounded, `F`
puckered, `G` teeth-on-lip (F/V), `H` tongue raised (L). The keyframe
never carries a shape directly; the engine classifies the continuous
`mouth_open/width/round/press/teeth` fields through the per-atlas
`sprite_selector_t` thresholds.

Sheets may provide any non-empty subset. The **converter** resolves
the full nine-entry table through fallback chains
(e.g. `E → F → C → D → X`), so the engine never needs fallback logic.

## Deterministic timing

All times are 16 kHz samples (16 per millisecond), configured in
`sprite_timing_t`:

| Field | Default | Source |
|-------|---------|--------|
| `mouth_min_hold` | 1120 (70 ms) | Rhubarb `minShapeDuration` |
| `mouth_close_delay` | 1920 (120 ms) | Rhubarb pause rule |
| `blink_close/hold/open` | 80/40/120 ms | animator convention (fast close, slow open) |
| `blink_window` | 4 s | conversational blink rate |
| `gaze_window` | 1.3 s | idle saccade cadence |
| `idle_window` | 6 s | idle-act roll |
| `breathe_period` | 4.2 s | bob cycle |

Blinks, saccades, idle acts and palette cycling are pure functions of
the clock (hashed epoch windows), so identical call sequences render
byte-identical frames on any platform. Cross-frame state is only the
mouth debounce machine and forced-blink edge tracking (32 bytes).

## Grid manifest (converter input 1)

```jsonc
{
  "name": "ega_sorcerer",
  "image": "sheet.png",
  "scale": 2,
  "native_size": [80, 60],
  "background": "#0000AA",
  "features": ["breathe", "saccades", "auto_blink"],
  "cells": { "base": [x, y, w, h], "mouth_X": [x, y, w, h], ... },
  "banks": [{
    "base": "base",
    "mouth": { "anchor": [30, 40],
               "shapes": { "X": "mouth_X", "D": "mouth_D", ... } },
    "eye_left":  { "anchor": [27, 22], "lids": ["open", "half", "closed"] },
    "eye_right": { "anchor": [41, 22], "lids": [...], "flip_x": true },
    "pupil_left": { "anchor": [31, 25], "cell": "pupil",
                    "range": [2, 1], "clamp": [29, 23, 34, 27] },
    "brow_left": { "anchor": [26, 16],
                   "levels": ["low", "mid", "high"], "max_lift": 2 }
  }],
  "sequences": [{
    "while_speaking": false,
    "frames": [{ "cell": "sparkle_0", "ms": 130, "at": [12, 44] }, ...]
  }],
  "cycles": [{ "colors": ["#fff854", "#ff8040", ...], "period_ms": 130 }],
  "selector": { "open_min": 20, ... },      // optional overrides
  "timing": { "mouth_min_hold": 70, ... }   // optional, in ms
}
```

Notes:

- eye lids are ordered fully open → fully closed; brow levels lowest →
  highest; `flip_x` reuses cells mirrored (such cells are exempted
  from trimming so the mirror stays anchored).
- `cycles` lists ring colours in rotation order; every pixel whose
  RGB565 value equals a cycle colour joins that ring (classic
  palette-cycling semantics), so cycle colours must be distinct from
  static colours *after* 565 quantisation — the converter errors on
  rings that collide with each other.
- pupil `clamp` bounds the pupil cell's top-left corner in native
  coordinates; `range` scales `look_x/y` (±127 → ±range px).

## Aseprite mode (converter input 2)

Author frames on the full canvas, tag them, and add slices where the
patches live. Export:

```sh
aseprite -b face.ase --sheet sheet.png --data data.json \
  --format json-array --list-tags --list-slices
```

(json-hash also accepted; rotation must be off — Aseprite never
rotates. Trimmed exports are handled through `spriteSourceSize`.)

Then convert with a role map:

```jsonc
{
  "name": "hero", "scale": 2, "background": "#101020",
  "base_tag": "base",                    // 1-frame tag
  "mouth": { "tag_prefix": "mouth-", "slice": "mouth" },
  "eyes":  { "tag": "eyes", "left_slice": "eye_left",
             "right_slice": "eye_right" },   // frames = lid states
  "pupil": { "tag": "pupil", "range": [2, 1] },
  "brows": { "tag": "brows", "max_lift": 2 },
  "idle_prefix": "idle-"                 // tags become sequences,
}                                        // durations from the export
```

Single-frame tags `mouth-X` … `mouth-H` name the shapes; the slice
rectangle is both crop and anchor. Multi-bank (expression) authoring
is grid-manifest-only for now.

The `.aseprite` binary format itself is fully documented
(`docs/ase-file-specs.md` in the Aseprite repo: header, zlib cel
chunks, tags, palette, slices) and parseable with `struct` + `zlib`
alone, so a direct `.ase` reader is a reasonable later addition; the
JSON path was chosen because it also covers TexturePacker-style
exports from other tools.

## Engine API

```c
sprite_face_t face;
sprite_face_init(&face, &ega_sorcerer_atlas);   /* validates fully */
sprite_face_render(&face, &keyframe, sample_clock,
                   framebuffer, SPRITE_FACE_PIXEL_COUNT);
```

`sprite_face_init` walks every cell stream, bank reference and cycle
range; after it returns true the render path performs no out-of-bounds
reads regardless of atlas content. A backwards clock jump resets state
(and the whole engine replays deterministically).
