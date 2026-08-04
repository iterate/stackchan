# fable_wildcards: seven wildcard visual languages

Standalone prototypes of seven face renderers whose point is algorithmic
variety, not palette variety. Each one obeys the shared contract from
`../README.md`: a 160×120 RGB565 frame that is a pure function of the
12-byte `face_keyframe_t` (mirrored here as `wc_keyframe_t`, identical
layout) plus the 16 kHz sample clock — integer-only, allocation-free,
caller-owned buffers, zero retained state, byte-identical under WebAssembly.

| Profile | One-line pitch |
|---|---|
| `wc-scope-beam` | XY vector-CRT face; the mouth **is** a synthesized voice waveform; stateless P7 phosphor trails |
| `wc-flipdot-cascade` | Electromechanical flip-disc sign; every expression change arrives as a row-scan mechanical wave |
| `wc-chladni-sand` | Cymatics: visemes select plate resonances, sand settles on nodal lines that outline the face |
| `wc-halftone-press` | Two-plate comic letterpress: rotated AM dot screens, drifting misregistration, manga emphasis lines |
| `wc-wayang-lamp` | Shadow-puppet theatre: hinged jaw, voice pours out of the mouth as lamp light |
| `wc-ferro-pool` | Ferrofluid: voice raises a Rosensweig spike crown and a hexagonal three-wave lattice |
| `wc-teletext-sextant` | Authentic teletext page: 2×3 G1 mosaics, one color per cell, real attribute clash, live header clock |

Animated previews for every profile (idle and scripted speech) live in
`preview/`. `manifest.json` carries the machine-readable renderer manifest;
`RESEARCH.md` is the prior-art and licensing audit.

## Build and test

```sh
make test        # contract, purity, canaries, fuzz, frozen golden CRCs
make bench       # per-profile host timings
make wasm-check  # emscripten build must reproduce the native CRCs exactly
make frames      # build the PPM frame dumper
./tools/make_previews.sh   # regenerate preview/*.gif (needs ffmpeg)
```

All hot paths are int32 adds/multiplies/shifts; divisions appear only in
per-frame setup, small LUT builds, or narrow rim/edge bands. Worst-case
profile is 0.166 ms/frame on an M4 Max host (see `manifest.json` for the
honest ESP32-S3 extrapolation).

## Layout

```
src/wildcard_face.h        public API (mirrors face_render.h conventions)
src/wildcard_face.c        dispatch, slugs, metadata
src/wc_common.[ch]         Q14 trig, hashes, isqrt/atan2, RGB565, idle rig
src/wc_<profile>.c         one file per visual language
test/test_wildcards.c      native + wasm test suite with golden CRCs
tools/render_frames.c      deterministic keyframe scripts -> PPM frames
tools/make_previews.sh     preview GIF generator
```

## The shared deterministic idle rig

`wc_rig_derive()` in `wc_common.c` converts (keyframe, clock) into the pose
every renderer consumes, so all seven languages share one believable
nervous system: hashed blink episodes (asymmetric close/open, double
blinks, the right eye trailing 19 ms), saccade holds with anticipation
widening before large flicks and ~6 % overshoot during them, head
follow-through that lags gaze by 87 ms and wobbles after big jumps,
breathing, and a band-limited flicker channel. Analyzer-provided keyframe
values always bound the idle motion, never the other way around.

Because the rig is a pure function of the clock, renderers may evaluate it
at *other* times to fake memory: the scope re-derives poses along its 120 ms
phosphor history, and the flip-dot panel compares the poses at the current
and previous 100 ms refresh ticks to decide which discs are mid-flip.

## Integration notes for the primary agent

- `wc_keyframe_t` is layout-identical to `face_keyframe_t`; integration is
  a typedef swap plus enum surgery. The manifest proposes
  `FACE_RENDER_FAMILY_WILDCARD = 5`.
- `estimated_ops_per_pixel` values in `wildcard_face.c` follow the existing
  metadata convention.
- Mouth-kind labels do not all fit the current `face_render_mouth_kind_t`
  (a waveform beam and a hinged light gap are new categories); the manifest
  documents each.
- The `expression` byte is currently unused (reserved semantics unclear at
  this ABI revision); every other keyframe byte is consumed.
- Stack scratch stays under 3 KiB (largest: the Chladni per-axis cosine
  tables); nothing touches the heap.
