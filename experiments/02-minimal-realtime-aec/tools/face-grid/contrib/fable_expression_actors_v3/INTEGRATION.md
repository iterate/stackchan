# Integration mapping (suggestions only — no shared files touched)

The pack is standalone. To expose it through the production
`face_render.h` registry, the primary agent would make these edits
(mirroring how earlier packs were integrated):

## 1. `firmware-ws/main/face_render.h`

Append to `face_render_profile_t` (before `FACE_RENDER_PROFILE_COUNT`):

```c
    FACE_RENDER_ACTOR_MOCHI_CAT,
    FACE_RENDER_ACTOR_KARAKURI_BRASS,
    FACE_RENDER_ACTOR_EMOTE_STICKER,
    FACE_RENDER_ACTOR_WILL_O_WISP,
    FACE_RENDER_ACTOR_MONO_SCOPE,
```

Family: either reuse `FACE_RENDER_FAMILY_TOON` (5) or add a new
`FACE_RENDER_FAMILY_ACTOR = 6`; `fea_profile_info()` currently reports
family 5 and is a one-line change.

## 2. `firmware-ws/main/face_render.c`

Add the registry rows (slug/name/family from `fea_registry.c`) and the
dispatch:

```c
    case FACE_RENDER_ACTOR_MOCHI_CAT:
    case FACE_RENDER_ACTOR_KARAKURI_BRASS:
    case FACE_RENDER_ACTOR_EMOTE_STICKER:
    case FACE_RENDER_ACTOR_WILL_O_WISP:
    case FACE_RENDER_ACTOR_MONO_SCOPE:
        return fea_render_frame(
            (fea_profile_t)(profile - FACE_RENDER_ACTOR_MOCHI_CAT),
            render_key, sample_clock, rgb565, pixel_capacity);
```

Metadata per profile: `mouth_kind = FACE_RENDER_MOUTH_POLYGON` and
`flags = EYE_FOCUS | POLYGON_MOUTH | IDLE_MOTION`, except
`fea-mono-scope` which is eye-only as of v4:
`mouth_kind = FACE_RENDER_MOUTH_NONE`,
`flags = EYE_FOCUS | IDLE_MOTION | NO_MOUTH`.
`estimated_ops_per_pixel` 11–13 (see `fea_registry.c`).

## 3. Build wiring

- `firmware-ws/main/CMakeLists.txt`: add the nine `src/fea_*.c` files
  (or a single amalgamated unit if preferred).
- `tools/face-grid/build-wasm.sh`: same file list; the pack is already
  proven byte-identical under emscripten.
- Drop `compat/` at integration time; the production headers win
  (re-diff first — the firmware tree moves).

## 4. Acceptance

- `tools/run_face_render_quality.py`: expected results per profile —
  distinct 11/11, clear 10/10, weak 0/55, mean pair ROI Δ 0.069–0.088,
  zero jumps, max Δ ≤ 0.053, frozen 0.
- `tools/test_face_rig.py`: no analyzer changes; renderers only.
- The internal solver consumes `stage_expression`,
  `expression_weight`, `speech_phase`, both viseme slots + blend, all
  action bytes, and `schema_version` gating — the axes the
  integration audit flagged as ignored by every earlier pack.

## 5. Semantics guarantees relevant to review

- `controls.expression` is treated strictly as activity
  (idle/listening/thinking/speaking), never as emotion or a sprite
  bank; unknown values degrade to idle.
- `viseme_set` values other than OVR15/VRM5/PRESTON9/MICROSOFT22
  disable viseme shaping instead of misindexing (controls-only mouth).
- Keys with `schema_version < 2` are rendered from the 12-byte prefix
  with neutral extended defaults.
- `fea-mono-scope` consumes every mouth byte through its eye-light
  (jaw -> height, width -> width, round -> radius/narrowing, press ->
  flattening, teeth -> bright scanline, tongue -> warm under-core,
  corners/curve -> smile-cut), so byte-liveness holds despite the
  missing mouth.
- `FACE_EXPRESSION_CUSTOM` (255) and out-of-range stage expressions
  render as neutral identity while generic action channels still act.
