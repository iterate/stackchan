# fable_cyber_shaders — fixed-point cyberpunk software-shader faces

Eleven original MCU software-shader face renderers covering the
firmware's `FACE_RENDER_FAMILY_CYBER` block: SDF neon faces (cyan +
magenta), a smooth-min liquid face, a CRT with chromatic aberration, a
holographic wireframe, a voice orb, a HAL-style red optic, a HUB75 LED
matrix simulation, a Siri-style edge glow, a glitch mask, and a
palette-cycled plasma silhouette.

Everything renders 160×120 RGB565 into a caller-owned buffer from the
stable 12-byte keyframe plus the 16 kHz sample clock. There is **no
floating point anywhere** (verified by symbol scan on the Xtensa
build), no per-frame allocation, and no hidden state: identical inputs
produce byte-identical frames on host, WebAssembly, and ESP32-S3.

```
keyframe (12 B) + sample_clock ──► cyber_motion_compute   (blinks,
                                        │                  saccades,
                                        ▼                  breathing…)
                        stage A: SDF/glow field at 80×60
                        (40×30 hub75, 160×120 stamped wireframe)
                                        │  one byte per cell
                                        ▼
                        stage B: 2× upscale + palette LUT +
                        Bayer dither + scanlines + chromatic
                        offset + bars/jitter ──► 160×120 RGB565
```

## Quick start

```sh
make test          # full suite incl. 55 golden frame hashes
make bench         # per-profile host timings
make preview       # BMP contact sheet -> preview/index.html
make wasm-verify   # emcc+node build must hash-match the host build
make xtensa-check  # cross-compile, prove no float, print sizes
```

Requires only a C compiler for test/bench/preview; `emcc`+`node` for
wasm-verify; the ESP-IDF `xtensa-esp32s3-elf-gcc` on PATH for
xtensa-check.

## API

```c
static cyber_face_ctx_t ctx;          /* 30,608 bytes, caller-owned */
cyber_face_init(&ctx);                /* builds all LUTs, once      */

cyber_keyframe_t kf = ...;            /* byte-mirror of face_keyframe_t */
cyber_face_render(&ctx, CYBER_PROFILE_VOICE_ORB, &kf,
                  sample_clock, framebuffer, 160 * 120);
```

`cyber_face_ctx_t` holds init-time lookup tables (sine Q14, three glow
falloffs, 11 palettes + 4 LED-shading palettes, vignette, ≈11 KB) plus
a 19,200-byte scratch plane reused by every profile. After init the
only mutated memory is that scratch and the output frame.

### Integrating with the firmware

`cyber_keyframe_t` and `cyber_face_info_t` are laid out byte-for-byte
like `face_keyframe_t` and `face_render_info_t` (both static-asserted).
A shim is one switch arm per profile:

```c
case FACE_RENDER_VOICE_ORB: ... {
    return cyber_face_render(&cyber_ctx,
        (cyber_profile_t)(profile - FACE_RENDER_NEON_SDF_CYAN),
        (const cyber_keyframe_t *)keyframe,
        sample_clock, rgb565, pixel_capacity);
}
```

## The profiles

| # | slug | one-liner |
|---|------|-----------|
| 0 | `neon-sdf-cyan` | filled rounded-box eyes, bent-capsule mouth, capsule brows, additive cyan glow |
| 1 | `neon-sdf-magenta` | lidded-disc eyes, magenta ramp, 1-cell chromatic fringe |
| 2 | `liquid-smin` | blobs fused by quadratic smooth-min; a drifting droplet merges with each feature in turn |
| 3 | `crt-chromatic` | phosphor face behind 75% scanlines, rolling bar, hum jitter, vignette |
| 4 | `holo-wireframe` | stamped-glow octagon head, diamond eyes, node dots, interlace + scan band + flicker |
| 5 | `voice-orb` | breathing ring with inner eye dots and three orbiting satellites |
| 6 | `red-optic` | single red lens; lids are an aperture shutter; specular dot tracks gaze |
| 7 | `hub75-neon` | 40×30 LED panel simulation with grid gaps, 8 brightness levels, temporal dither |
| 8 | `edge-glow` | screen-border rainbow sweep (8 hue bands × 32 levels) + minimal centre face |
| 9 | `glitch-mask` | ice-white face with scheduled row tearing, RGB splits, block dropouts |
| 10 | `palette-plasma` | four-sine plasma on a seamless cosine-palette wheel; face is a bright silhouette mask |

All profiles honour every keyframe field they sensibly can: mouth
open/width/round/press/teeth reshape the mouth (or orb/optic pulse),
per-eye openness and the blink flag drive lids/shutters, look_x/y sets
gaze, brow tilts or lifts the brow capsules, and expressions 1–4
(happy/sad/angry/surprised) adjust curve, lids, brows, and glow.

### Deterministic idle motion

Derived purely from the sample clock (wraps safely with uint32
arithmetic): blinks on a jittered ~3.4 s schedule (70 ms close, 40 ms
hold, 110 ms open, 1-in-8 double blinks, a subtle anticipation widen
200 ms before the lids drop); saccades every ~1.4 s (~0.9 s and
half-amplitude while speaking) with a 90 ms smoothstep jump and sinusoid
micro-drift; a 4.2 s breathing bob that also modulates glow; speech
squint proportional to mouth_open. Replaying a clock sequence replays
the exact frames — that is what the golden-hash tests pin down.

## Performance

Host benchmark (`make bench`, Apple M-series, clang -O2, 600 animated
frames per profile):

| profile | µs/frame (host) | est. S3 cycles/out-px¹ |
|---|---:|---:|
| neon-sdf-cyan | 114.3 | 143 |
| neon-sdf-magenta | 134.4 | 168 |
| liquid-smin | 146.9 | 184 |
| crt-chromatic | 143.6 | 180 |
| holo-wireframe | 20.9 | 26 |
| voice-orb | 96.9 | 121 |
| red-optic | 66.0 | 83 |
| hub75-neon | 22.9 | 29 |
| edge-glow | 79.5 | 99 |
| glitch-mask | 135.1 | 169 |
| palette-plasma | 123.8 | 155 |

¹ Using a deliberately pessimistic **100×** host→ESP32-S3 factor
(single LX7 core at 240 MHz, hardware 32-bit mul/div, 5.54
CoreMark/MHz). The 30 fps budget is 33.3 ms/frame ≈ 416 cycles per
output pixel; the worst profile lands at ~14.7 ms (≈2.3× headroom) and
the median around 6 ms. Structural reasons this holds up on target:
the hot loops are 32-bit adds/shifts/multiplies plus byte-table
lookups (the S3 has a hardware divider for the few divisions), the
field pass touches only 4,800 cells (1,200 for hub75), the whole
working set (LUTs + scratch ≈ 30 KB) fits in SRAM cache-friendly
strides, and a 38,400-byte frame pushes over SPI in 3.8–7.7 ms with
DMA. `bench/bench_cyber_face.c` is plain C11 — run it under `esp_timer`
on the device for authoritative numbers before shipping fps claims.

Cost anatomy per field cell (worst profiles): 3–6 exact integer square
roots (16-iteration shift-subtract), a handful of clamps and table
lookups; per output pixel: one or three palette reads, a Bayer add, and
RGB565 mask arithmetic.

## Correctness and determinism

- `make test`: metadata sanity, argument rejection, cross-context
  determinism, guard-band overrun checks around the framebuffer,
  motion/blink/speech visibility per profile, a 400-case random
  keyframe fuzz, and 55 golden FNV-1a64 frame hashes
  (`tests/golden_hashes.inc`, regenerate with `make regen-golden`).
- `make wasm-verify`: builds the same suite with emscripten, runs it
  under node, and diffs the dumped hash table against the host build —
  currently **byte-identical**, as required for the browser laboratory.
- `make xtensa-check`: `xtensa-esp32s3-elf-gcc -O2 -Werror` build plus
  an `nm` scan proving no `__…sf/df` soft-float helpers are referenced;
  total `.text` ≈ 13.9 KiB across the four objects.
- Byte-identity rests on: integer-only arithmetic, fixed-width types,
  no signed overflow, and table construction that itself is integer
  (Bhaskara I sine, iterated-ratio exponentials) — nothing depends on
  libm, FPU rounding, or platform `int` width.

## Provenance

All code here is original. The mathematics (2D SDFs, quadratic smooth
minimum, cosine palettes, Bayer matrices, plasma, Bhaskara sine,
integer sqrt) comes from primary published sources; copyleft or
restricted projects (RoboEyes GPL-3.0, WLED EUPL-1.2, Anki/DDL
ProceduralFace, Shadertoy CC BY-NC-SA defaults, Pixelblaze) were used
as *prior-art existence proofs only*. Full audit with links, license
quotes, and an effect-by-effect provenance table: **RESEARCH.md**.

## Known limits / future work

- Palettes are art-directed multi-stop ramps; swapping moods at runtime
  (e.g. listening vs thinking hues) would only need extra palettes in
  the ctx and a profile parameter.
- The field pass is scalar; the S3's PIE 128-bit SIMD could vectorize
  the glow lookups if a future profile needs the budget.
- `estimated_ops_per_pixel` in the metadata is the conservative S3
  cycle estimate above, not a device measurement.
- Expressions beyond 0–4 render as neutral by design.
