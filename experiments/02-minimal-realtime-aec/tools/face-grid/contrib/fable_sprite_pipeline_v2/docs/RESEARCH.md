# Prior art consulted

All source-first, in-repo unless noted.

## The runtime this pack targets

- `firmware-ws/main/face_sprite_sheet.h/.c` — FSPR v2
  `face_sprite_atlas_t` container and allocation-free player: palette ≤256
  with reserved transparent index, trimmed cells addressing a shared
  raw/PackBits blob, expression banks selected by weighted-L1 proximity in
  (valence, arousal, mouth corner, inner brow, squint) action space, 23-slot
  mouth arrays, viseme maps keyed by (set, id), role-based fallback slots,
  data-driven selector/timing tables in 16 kHz samples.
  `face_sprite_player_init` validates everything up front (magic, ranges,
  exact PackBits stream termination, safe-canvas placements, duplicate
  viseme pairs); the emitter here was written against those rules and the
  C smoke test proves acceptance.
- `firmware-ws/main/face_keyframe.h` — the stable 12-byte
  `face_keyframe_t` control prefix and 40-byte `face_render_key_t`
  (viseme set/id/blend, speech phase, affect, per-side face actions,
  head/body pose, schema version 2).
- `firmware-ws/main/face_sprite_mossling.c` + `_generated.c` — the shipped
  proof that a generated atlas renders through
  `FACE_RENDER_SPRITE_SHEET_POCKET_MOSSLING` on device and in WASM; also
  the pattern for a per-profile wrapper (static player, lazy init,
  zero-pose fix-up, deterministic idle head turn).

## Generators studied

- `tools/generate_sprite_showcase.py` — the shared `AtlasBuilder`/
  `emit_atlas` machinery: cell trim/dedupe, PackBits dialect, C table
  naming, the 23-slot vocabulary (`MOUTH_SLOT_NAMES`), slot→role table
  (`MOUTH_ROLES`), OVR15/VRM5/Preston9/Microsoft22 map layouts, the 11
  canonical expression targets from `face_stage.c`, and
  `fallback_slots = {0, 1, 17, 4, 10, 13, 19, 2, 18}`. FSPP reimplements
  these conventions (same wire values) without importing repo-tool code,
  keeping the pack self-contained per contrib rules.
- `tools/generate_mossling_sprite_atlas.py` — the hand-audited pipeline
  FSPP automates: eleven hand-measured crop boxes and per-frame eye/mouth
  landmarks, fixed DMG palette nearest-color quantization, hand-tuned
  background delta, procedural replacement mouths. Its outputs bound what
  "good" looks like for the same source sheet (15.6 KB portable payload vs
  FSPP's fully automated 18.4 KB).
- `contrib/fable_sprite_sheet` (v1, in its worktree) — a parallel FSPR
  engine + converter for *authored* art: hand-written grid manifests or
  Aseprite JSON, no keying/snap/segmentation, Rhubarb 9-shape vocabulary,
  golden CRCs, standalone WASM viewer. v2 deliberately targets the
  firmware's FSPR v2 instead of v1's private dialect so output runs in the
  production player, and adds the image-model front half v1 explicitly
  lacked.

## Method notes

- Sheet-layout conventions (rows of expressions, mouth-study heads, flat
  background, detached decoration marks) taken from the real mossling
  concept sheet; the synthetic fixture reproduces the failure modes
  (pitch wobble, edge anti-aliasing, channel noise, background gradient,
  baked mouths, detached sparkles) so tests exercise them without a
  network or licensing risk.
- PackBits is the classic Apple dialect as implemented by the firmware's
  `stream_next()`; the reference decoder in `fspp/util.py` mirrors it
  including the exact-termination rule.
- Preston-Blair/Rhubarb-style role reduction (9 canonical mouth roles)
  underlies the fallback chain, matching `face_sprite_mouth_role_t`.
- No external code was copied; everything in `fspp/` is original,
  written against in-repo formats and public-domain-dedicated.
