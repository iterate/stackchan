# Integrating an FSPP atlas

Nothing in this pack modifies shared files; this documents the exact
changes the primary agent would make to expose a generated atlas as a
production render profile, following the shipped mossling pattern.

## 1. Vendor the generated pair

Copy `samples/<id>/cores3-face/fspp_<id>_cores3_face_atlas.{c,h}` next to
the other generated atlases (e.g. `firmware-ws/main/`). The `.c` includes
its own header, which includes `face_sprite_sheet.h`; no other
dependencies.

## 2. Wrapper (mirrors face_sprite_mossling.c)

```c
/* face_sprite_<id>.c */
#include "face_sprite_sheet.h"
#include "fspp_<id>_cores3_face_atlas.h"

bool face_sprite_<id>_render(const face_render_key_t *render_key,
                             uint32_t sample_clock,
                             uint16_t *rgb565, size_t pixel_capacity)
{
    static face_sprite_player_t player;
    static bool ready;
    if (!ready) {
        if (!face_sprite_player_init(
                &player, &face_sprite_fspp_<id>_cores3_face_atlas)) {
            return false;
        }
        ready = true;
    }
    /* Optional: zero-pose fix-up and deterministic idle motion, as in
     * face_sprite_mossling.c. */
    return face_sprite_render_snapshot(
        &player, render_key, sample_clock, rgb565, pixel_capacity);
}
```

## 3. Registry row + dispatch (face_render.c / face_render.h)

- Append `FACE_RENDER_SPRITE_SHEET_<ID>` to `face_render_profile_t`
  before `FACE_RENDER_PROFILE_COUNT`.
- Add a `profile_description_t` row: family `PIXEL`, mouth kind `SPRITE`,
  flags `PIXELATED | SPRITE_MOUTH | HALF_RES` (plus `IDLE_MOTION` if the
  wrapper adds it), work size = the atlas's native canvas.
- Add the dispatch `case` in `face_render_frame` calling the wrapper.

## 4. Builds

- `firmware-ws/main/CMakeLists.txt`: add the two new `.c` files.
- `tools/face-grid/build-wasm.sh`: they are picked up with the other
  `face_*.c` sources; the new profile then appears in the review room
  automatically (slug from the registry row).

## 5. Browser-side manifest (optional, no C required)

`samples/<id>/<target>/manifest.json` contains the palette (RGB565 and
#rrggbb), per-cell geometry, and the shared blob as base64 in the exact
PackBits dialect documented in `schema/atlas_manifest.schema.json`. A
small JS decoder can render the identical asset for inspection tooling
without loading WASM; `blob_sha256`/`raw_sha256` fields let it prove
parity with the C tables.

## Multi-target note

Each target emits an independent symbol
(`face_sprite_fspp_<id>_<target>_atlas`), so a Stick-class device links
only the 40×30 stage atlas while the CoreS3 links the 80×60 portrait.
Per-target `max_flash_bytes` budgets are enforced at build time and
recorded in each manifest.
