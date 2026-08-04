# Memory and performance measurements

Measured 2026-07-28 on the development machine (Apple Silicon,
clang -O2) via `build/test_sprite_face`; regenerate any time with
`./build.sh`. Numbers for the ESP32-S3 are extrapolations with the
reasoning shown — the module has not been flashed to hardware yet.

## Flash footprint

Per-atlas const data (blob + cell table + palette + banks +
sequences), from `generated/*.stats.json` and the harness:

| Atlas | Cells | Colours | Blob | vs raw indices | Total flash |
|-------|------:|--------:|-----:|---------------:|------------:|
| ega_sorcerer | 22 | 20 | 4139 B | 53% | ~4.8 KB |
| handheld_gobbo | 11 | 5 | 1667 B | 26% | ~2.0 KB |
| vga_navigator | 18 | 25 | 1213 B | 18% | ~1.8 KB |
| terminal_operator | 10 | 9 | 1041 B | 18% | ~1.5 KB |

PackBits on flat-shaded indexed pixel art lands at 18–53% of raw —
far better than the ~88% PackBits manages on planar NES tile data
(nesdev.org's benchmark), confirming the cell-patch + RLE choice.
The sorcerer compresses worst because its checkered background
defeats run-length encoding by design.

Engine code: `sprite_face.c` compiles to **5.3 KB** of text at
`-Os` on arm64 (no data, no BSS beyond the CRC-less engine itself);
expect the same order on Xtensa. The four demo atlases plus engine
total ~15 KB flash.

## RAM

- Framebuffer: 38,400 B (160×120 RGB565), **caller-owned**.
- Engine state: `sprite_face_t` = **32 B**.
- Stack during render: one 160 B row buffer, plus a 512 B effective
  palette only when the atlas uses colour cycling; everything else is
  scalars. No heap use ever — enforced in CI by `nm -u` on the
  engine object (no `malloc/calloc/realloc/free/printf/fopen`).

## Speed

Native (Apple Silicon, -O2), full 160×120 frame incl. background
fill, base blit at 2× scale, all layers, over the 300-frame scenario:

| Atlas | µs/frame (native) |
|-------|------------------:|
| ega_sorcerer | 18–28 |
| handheld_gobbo | 19–26 |
| vga_navigator | 18–25 |
| terminal_operator | 18–25 |

(Range across repeated runs; load-dependent.)

**ESP32-S3 estimate.** Per frame the engine performs ~19,200
background writes + ~19,200 base pixel writes + a few thousand layer
writes (~45 K 16-bit stores) plus ~5–7 K bytes of RLE decode with
simple branches — order 300–500 K cycles with load/store overheads.
At 240 MHz that is **~1.5–2.5 ms/frame**, i.e. **5–8% of one core at
30 fps**, leaving the 33 ms budget essentially untouched. Even a
pessimistic 10× miss on this estimate still holds 30 fps. (For
comparison the procedural firmware profiles budget 5–9 estimated ops
per pixel; this engine's per-pixel work is a table lookup and a
store.)

## Determinism / browser parity

- Golden CRCs over the 300-frame scenario (`tests/golden_crcs.txt`):
  `ega_sorcerer d3fbc2a1`, `handheld_gobbo 7685ebab`,
  `vga_navigator d2e92cc9`, `terminal_operator 4ae117ba` — stable
  across fresh engine instances, replays after clock regression, and
  full scratch rebuilds (assets regenerated from scripts).
- WASM parity: `wasm/check.mjs` verifies all four aggregate CRCs and
  byte-compares 12 raw RGB565 frames (frames 0/100/250 per atlas)
  against native dumps — all byte-identical.
- WASM artifact: 18.6 KB `.wasm` + 9.3 KB JS glue, *including* the
  engine, all four atlases, the scenario and CRC code.

## Test inventory

`build.sh` runs: atlas validation accept/reject (magic, version,
scale, dimensions, truncated blob, palette bounds), render guards
(capacity, NULL), cross-instance determinism, clock-regression replay,
selector coverage of all nine shapes, debounce/min-hold/close-delay
property checks, golden CRCs, converter self-tests (PackBits
round-trip, manifest errors, synthetic Aseprite export end-to-end),
Aseprite-path engine validation, and the no-allocation symbol check.
`wasm/build-wasm.sh` adds the byte-identical browser check.
