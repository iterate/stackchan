# Integrating fable_toon_acting into the production tree

Exact steps for the primary agent. The pack is standalone; nothing here
was applied to production files by the worker. `face_render.c` was being
edited concurrently while this was written, so **anchor on symbols, not
line numbers**.

The pack renders the full frame itself (background included) and does its
own expression/motion evaluation from the raw 40-byte key, so it must be
dispatched as an **external family hook** (like `face_robot_eyes_render`
and `face_pixel_pack_render`) *before* `evaluate_expression_actions()` /
`motion_for()` run.

## 0. What to copy

Copy these five files into `firmware-ws/main/` (keeping names):

```
src/fta.h            -> firmware-ws/main/fta.h
src/fta_internal.h   -> firmware-ws/main/fta_internal.h
src/fta_math.c       -> firmware-ws/main/fta_math.c
src/fta_act.c        -> firmware-ws/main/fta_act.c
src/fta_draw.c       -> firmware-ws/main/fta_draw.c
src/fta_styles.c     -> firmware-ws/main/fta_styles.c
```

(Or rename to `face_toon_*` — every include is a quoted local include,
so a mechanical rename of files + `#include` lines is safe. `fta.h`
includes `"face_keyframe.h"`, which resolves to the live firmware header
once the files sit in `main/`; the pack's `compat/` copies are only for
standalone builds and are **not** copied.)

`fta_act.c` also includes `"face_stage.h"` for the expression/gesture
enums — already present in `main/`.

## 1. `firmware-ws/main/face_render.h`

Add a family id to `face_render_family_t`:

```c
    FACE_RENDER_FAMILY_CYBER = 4,
    FACE_RENDER_FAMILY_TOON = 5,
```

Add three profiles to `face_render_profile_t` immediately before
`FACE_RENDER_PROFILE_COUNT` (contiguous, order must match
`fta_profile_t`):

```c
    FACE_RENDER_TOON_BEAN,
    FACE_RENDER_TOON_INK,
    FACE_RENDER_TOON_EMBER,
    FACE_RENDER_PROFILE_COUNT,
```

## 2. `firmware-ws/main/face_render.c`

a. Near the other family includes at the top:

```c
#include "fta.h"
```

b. Three rows in `static const profile_description_t PROFILES[...]`
(designated initializers, so position in the array is by enum):

```c
    [FACE_RENDER_TOON_BEAN] = {
        "toon-bean", "Toon Bean acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 11,
    },
    [FACE_RENDER_TOON_INK] = {
        "toon-ink", "Toon Ink acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 9,
    },
    [FACE_RENDER_TOON_EMBER] = {
        "toon-ember", "Toon Ember acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 10,
    },
```

Note `work_width`/`work_height` in `profile_description_t` are `uint8_t`
(existing rows use 80/60 for half-res); 160/120 fits `uint8_t` for width
only up to 255 — 160 and 120 both fit. If the struct field widths change,
mirror whatever the neighbouring full-res rows do.

c. `FAMILY_NAMES[]` gains an entry at index `FACE_RENDER_FAMILY_TOON`:

```c
    [FACE_RENDER_FAMILY_TOON] = "toon",
```

d. In `face_render_frame()`, next to the other external family hooks
(`face_robot_eyes_render` / `face_pixel_pack_render` range checks),
**before** `evaluate_expression_actions()`:

```c
    if (profile >= FACE_RENDER_TOON_BEAN &&
        profile <= FACE_RENDER_TOON_EMBER) {
        return fta_render_frame(
            (fta_profile_t)(profile - FACE_RENDER_TOON_BEAN),
            render_key, sample_clock, rgb565, pixel_capacity);
    }
```

`fta_info_t` is layout-identical to `face_render_info_t` (both
static-asserted at 16 bytes), so `face_render_profile_info()` needs no
change — the PROFILES row covers it. `fta_profile_info()` exists if a
struct-copy path is ever preferred.

## 3. `firmware-ws/main/CMakeLists.txt`

Append to the source list:

```
    "fta_math.c"
    "fta_act.c"
    "fta_draw.c"
    "fta_styles.c"
```

## 4. `tools/face-grid/build-wasm.sh`

Append the same four files to the explicit source list (the one already
naming `face_render.c`, `face_robot_eyes.c`, …):

```
  "$MAIN_DIR/fta_math.c"
  "$MAIN_DIR/fta_act.c"
  "$MAIN_DIR/fta_draw.c"
  "$MAIN_DIR/fta_styles.c"
```

The browser lab needs no JS change: tiles and the stage-cue path key off
`face_render_profile_count()`.

## 5. `tools/run_face_render_quality.py`

Add the four `fta_*.c` files to the `compile_probe` source list (kept in
sync by hand with build-wasm.sh, per existing convention).

## 6. `tools/test_face_rig.py`

Add the four files to both hand-maintained lists: the `face_render`
native test case and `face_render_benchmark`.

## 7. Verify

```sh
python3 tools/run_face_render_quality.py --strict
```

Expected for the three new profiles, from this pack's mirror of the same
metrics (see README results): expression distinct 11/11, clear 10/10,
weak 0/55; motion zero abrupt jumps, max ROI delta ≤ 0.051, zero frozen
pairs — i.e. `pass`/`pass`, no warns.

## Semantics the integrator should know

- `controls.expression` is read as conversational **activity**
  (idle/listening/thinking/speaking); authored emotion comes from
  `stage_expression` + `expression_weight`. The two axes compose; a
  strong stage emotion fades the activity poses.
- `FACE_KEYFRAME_FLAG_BLINKING` clamps the aperture to ~15%;
  autonomous blinks are scheduled from the clock either way (constant
  cadence per style, so a frozen-clock atlas never catches one column
  mid-blink).
- `speech_phase` STARTING/ENDING pose anticipation/settle; the runtime
  currently only emits IDLE/ACTIVE, and the pack behaves gracefully
  without the transient phases.
- Unknown `viseme_set` values disable viseme accents only; the
  `controls` articulation prefix still drives the mouth fully.
- The renderer is a pure function of (profile, key, clock): no context
  struct, no heap, `FTA_CONTEXT_BYTES == 0`.
