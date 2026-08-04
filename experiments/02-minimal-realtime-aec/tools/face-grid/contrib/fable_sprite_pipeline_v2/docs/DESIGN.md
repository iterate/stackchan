# FSPP design notes

## Goals and non-goals

FSPP compiles untrusted image-model sheet art into the existing, proven
runtime: FSPR v2 (`face_sprite_atlas_t` + `face_sprite_sheet.c`), driven by
the 12-byte `face_keyframe_t` prefix and the 40-byte `face_render_key_t`
at the 16 kHz sample clock. It deliberately does **not** invent a new
player, IR, or viseme vocabulary — v1's FSPR engine and the firmware's
mossling path proved those; the missing piece was the front half of the
assembly line (image-model input, snapping, segmentation, cataloguing,
multi-target validation), which is what v2 builds.

Non-goals for this slice: pupils/brow layers (banks disable them exactly
like the shipped mossling atlas), idle sequences and palette cycles
(emitted empty), calling image APIs (the brief flow keeps a human in the
loop), and the interactive review UI (stage debug artifacts and preview
sheets are its input when it comes).

## Stage decisions

**Keying.** Modal border color (8-level buckets, integer mean), then BFS
flood fill within a per-channel tolerance from the border only. Interior
pixels that merely resemble the background (highlights) are unreachable by
the flood and stay foreground. A global threshold — what the original
mossling generator used — cannot make that distinction.

**Segmentation.** Rows from the row projection (image models separate rows
well). Cells within a band from 4-connected components, not column
projection: on the real mossling sheet, neighbouring characters' leaf-ears
overlap in x-projection, so a projection gap never appears. The N largest
components seed the N named cells; components overlapping ≥60% of a core's
x-range merge into it (split outlines, detached hats); the rest are
satellites attached to the nearest core. A cell-sized component that only
attaches by distance aborts the build — the layout names too few cells,
and silently blending two characters would be worse than failing.

**Pixel snap.** Per-cell edge-energy peaks (adjacent smeared columns
collapsed), pitch = modal peak spacing with ±1 pooling, reconciled to a
sheet-wide median; phase re-fit per cell because wobble drifts across a
large sheet. Snapping resamples one color per logical cell via per-channel
medians. Exact modular-alignment scoring was tried first and is degenerate
for pitch ≤ 3 with ±1 wobble (every position is within 1 of a grid line);
spacing histograms are stable there.

**Registration (the load-bearing decision).** One rational scale for the
whole sheet: median dominant-component (body) size across portraits,
scaled to fill `rig.max_body`. Every cell — portraits and mouth cels —
uses that same factor, then portraits register by centering the dominant
component and sitting it on the baseline. Consequences:

- spec anchor boxes (mouth, eyes) are meaningful in canvas coordinates
  regardless of what pitch the snap stage detected — a wrong pitch costs
  fidelity, never registration;
- relative sizes between cels are preserved (a tiny "ou" mouth stays
  tiny);
- detached decorations ride along at their relative offsets but cannot
  shift the body anchor (they are excluded from the dominant component).

**Baked-mouth erase.** Fill color from the ring around the mouth box —
choosing the *brightest bucket holding ≥15% of the ring*, because the ring
mixes face color with outline strokes and a plain mode happily floods the
box with outline-dark. Only pixels differing from the fill by more than a
tolerance are replaced, preserving shading.

**Mouth-study faces.** Image models frequently draw mouth variants on
complete heads. `rig.mouth.source: auto` compares median mouth-cell art
area against the median portrait body; face-sized cels are registered
exactly like portraits and the rig's mouth box is cropped out of them.
Because the crop is face content, it composites seamlessly over any bank
and fully covers the erase region.

**Lid synthesis.** Three-stage blink (transparent / half / closed) from
the neutral portrait's own surrounding skin color with a darker lash edge,
integer ellipse masks. An atlas can blink without any authored lid art;
authored lids can replace this later without changing the data model.

**Quantize.** Median cut with fully specified tie-breaking (largest
channel range, then population; representatives are weighted integer
means; final order sorted by luminance) or a locked palette. Index 0 is
reserved transparent. Colors colliding after RGB565 truncation merge
deterministically and are recorded in the manifest. Despeckle is a single
majority-filter pass over a copy.

**Targets.** Each target derives from the canvas-space indexed catalog by
one rational transform (`face_fill` keeps the rig canvas; `stage` fits it
inside canvas-minus-margin, centered). Anchors transform by the same
integer mapping as pixels, so cross-target registration is exact by
construction; palettes never change. Validation is per target — passing at
80×60 does not imply 40×30 passes (legibility gate).

**Emission.** C tables mirror `tools/generate_sprite_showcase.py`'s
emitter (verified against `firmware-ws/main/face_sprite_sheet.h` structs
and the player's `face_sprite_player_init` validation rules), with
`FACE_SPRITE_SELECTOR_DEFAULTS`/`FACE_SPRITE_TIMING_DEFAULTS` and computed
`fallback_slots`. The manifest carries the same data plus provenance
hashes and per-slot borrow records; `blob_base64` + the documented
PackBits dialect make a JS renderer straightforward.

**Determinism.** `compute()` is a pure function of (spec bytes, sheet
bytes). `build()` runs it twice and compares every artifact hash before
touching disk; the report embeds spec/sheet SHA-256 and the decoded-pixel
hash of the source. No floats outside the pitch-confidence percentages
(integer-derived), no wall clock, no RNG (the fixture generator's seeded
xorshift is fixture-only).

## Validation gates

`background_key`, `segmentation`, `grid_snap`, `shared_scale`,
`mouth_source`, then per target: `packbits_roundtrip` (reference decoder
must reproduce every cell exactly — the same dialect `stream_next()`
decodes), `player_contract` (the init-time rules: canvas×scale ≤ 160×120,
palette/cell index ranges, 23-slot arrays, unique viseme pairs, bank 0
neutral, ≤6 lids), `registration` (baseline spread, anchor containment in
every bank's base cell; width deviation is informational), `clipping`
(every blit box inside the canvas; stage margin violations warn),
`palette`, `coverage` (all four vocabularies fully mapped, no slot without
pixels, canonical-target distances, borrow list), `budget` (declared
portable payload vs target flash budget), `legibility` (minimum feature
sizes after scaling), and `determinism`.

## Known limitations / next steps

- Eye boxes come from the spec, tuned against the first preview run
  (auto-detection of eye landmarks is the obvious next stage; the review
  step in the pipeline doc anticipates human correction regardless).
- Non-integer NN scaling produces slightly uneven fat pixels; an
  integer-pitch-aware resampler would keep them square.
- The erase fill is flat per box; a per-column smear or gradient fill
  would blend better on strongly shaded muzzles (mostly hidden today by
  face-crop mouth cels).
- Pupils, brows, idle sequences, and palette cycles are structurally
  supported by the emitter's data model but not yet authored from sheets.
- The stage-framing target validates but the legibility gate is coarse
  (pixel heights, not contrast).
- WASM parity is inherited from compiling the same C; wiring a
  `face_render.c` profile (see INTEGRATION.md) plus the grid's golden
  hashes would make it a measured property.
