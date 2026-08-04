# Source and licensing research report — fable_cyber_shaders

Web research performed 2026-07-28 against primary sources (article HTML,
GitHub LICENSE files via the GitHub API and raw.githubusercontent.com,
Espressif documentation PDFs, Wayback Machine for one Cloudflare-gated
page). Everything below informed a **clean-room integer-C
implementation**: the renderer reuses published *mathematics* and
*ideas*; no code was copied from any external project, including
permissively licensed ones.

## 1. SDF, smooth minimum, cosine palettes (Inigo Quilez)

Primary sources fetched:

- <https://iquilezles.org/articles/distfunctions2d/>
- <https://iquilezles.org/articles/smin/>
- <https://iquilezles.org/articles/palettes/>
- <https://iquilezles.org/articles/> (index carrying the license grant)

**License.** The only explicit grant on the site, verbatim from the
articles index: *"Lastly, all technical code snippets you'll find are
under the MIT license so you can easily reuse them, but the
mathematical/shader art is protected and requires a license for use."*
There is no per-article license header. Article prose and images remain
all-rights-reserved; his Shadertoy *art* explicitly requires a license.

**What we used (as mathematics, re-derived in Q4 fixed point):**

- `sdCircle(p; r) = |p| − r`; `sdBox`/`sdRoundedBox` via
  `q = |p| − b`, `d = |max(q,0)| + min(max(qx,qy),0) (− r)`;
  `sdSegment` via the clamped projection `t = clamp(((p−a)·(b−a)) /
  |b−a|², 0, 1)`. Implemented in `cyber_internal.h` /
  `cyber_tables.c` with integer square roots.
- Quadratic polynomial smooth minimum
  `h = max(k − |a−b|, 0); smin = min(a,b) − h²/(4k)` — the variant the
  smin article recommends as "fast, close enough to circular, never
  overestimates". Implemented as `cyber_smin_q4`.
- Cosine palette `color(t) = a + b·cos(2π(c·t + d))` with the article's
  constraint that c must be an integer for a seamless cyclic ramp —
  used (c = 1) for the plasma hue wheel in `build_plasma_palette`; the
  other ramps are simpler multi-stop linear gradients.

**What we did not use:** any Shadertoy shader source. Shadertoy's terms
(<https://www.shadertoy.com/terms>, via Wayback snapshot 2025-10-01)
state shaders default to **CC BY-NC-SA 3.0 Unported** unless the author
declares otherwise — unsuitable for this project, so Shadertoy pages
were avoided as an implementation source entirely.

## 2. Ordered dithering

Primary source: <https://en.wikipedia.org/wiki/Ordered_dithering>,
citing B. E. Bayer, "An optimum method for two-level rendition of
continuous-tone pictures", IEEE ICC, 1973. The canonical 8×8 threshold
matrix (values 0..63, recursive construction with quadrant offsets
{0,2,3,1}) is public-domain mathematics; `cyber_bayer8` is that
canonical matrix. We apply it as a ±4 palette-index dither before ramp
lookup (only on smooth-ramp palettes; band-packed and cyclic palettes
opt out). The HUB75 profile additionally uses the 2×2 matrix with a
frame-parity offset — the folklore "temporal dithering" trick noted in
the Wikipedia Dither article as LCD frame-rate control.

## 3. ESP32-S3 platform facts

From the ESP32-S3 Series Datasheet v2.2
(<https://documentation.espressif.com/esp32-s3_datasheet_en.pdf>):
dual-core Xtensa LX7 at up to 240 MHz, 512 KB SRAM, **hardware 32-bit
multiplier and 32-bit divider**, single-precision FPU, 128-bit PIE SIMD
extension, 1329.92 CoreMark for two cores at 240 MHz (5.54
CoreMark/MHz). The hardware divider justifies the few per-frame and
per-pixel divisions we do keep; everything else is shifts, adds, and
32-bit multiplies. esp-dsp (<https://github.com/espressif/esp-dsp>,
Apache-2.0) publishes a 14.5× ESP32→S3 speedup for int16 FFT via PIE —
headroom this renderer does not even need, but a future SIMD path for
the field pass could exploit it.

SPI throughput (ESP-IDF SPI master docs): 80 MHz max master clock; a
160×120 RGB565 frame is 38,400 bytes ≈ 3.8 ms at 80 MHz, 7.7 ms at
40 MHz — comfortably inside a 33 ms frame with DMA.

**Fast integer magnitude.** Alpha-max-plus-beta-min
(<https://en.wikipedia.org/wiki/Alpha_max_plus_beta_min_algorithm>):
provided as `cyber_length_fast_q4` with α=15/16, β=15/32 (max error
≈1.6% before the max-correction). The shipped profiles ended up fast
enough to afford the exact 16-iteration shift-and-subtract integer
square root (<https://en.wikipedia.org/wiki/Integer_square_root>)
everywhere it matters, so the approximation is currently unused by the
hot paths but kept for future budget squeezes.

## 4. MCU software-rendering prior art (existence proofs, ideas only)

| Project | License | Relevance |
|---|---|---|
| kilograham/rp2040-doom | GPLv2 (chocolate-doom lineage) + BSD-3 (new code) | 320×200 dual-core software rendering on a far weaker MCU |
| Wren6991/PicoDVI | BSD-3-Clause | canonical render-small-then-upscale (QVGA framebuffer pixel-doubled to 640×480 at scanout) |
| tuupola/esp_effects | MIT-0 | plasma/metaballs/rotozoom demo effects running on ESP32-class hardware |
| Waveshare ESP32-S3 3D-Box demo | MIT | shaded 3D cube at a claimed ~80 fps on an S3 + SPI LCD |

No existing "Shadertoy on ESP32" port was found; the SDF-face niche
this contribution fills appears genuinely open. The plasma effect is
early-1990s demoscene folklore with no attributable owner; Lode
Vandevenne's tutorial (<https://lodev.org/cgtutor/plasma.html>, ©
all-rights-reserved) was consulted for the standard *math* only (sum of
sines of x, y, t, radial term; cycle a seamless palette).

## 5. Robot-face and LED-ecosystem licensing audit

| Project | SPDX | Notes for this contribution |
|---|---|---|
| FluxGarage/RoboEyes | GPL-3.0-or-later | **ideas only** (parametric eyes as rounded rects, mood lids); no code read or translated; our eye rig derives from the SDF math above |
| meganetaaan/m5stack-avatar | MIT | closest permissive reference face in the parent project; not copied here |
| adafruit/Uncanny_Eyes | MIT (sketch-header only; no LICENSE file) | prior art for lid/eye motion; weak provenance noted |
| WLED (wled/WLED) | **EUPL-1.2 since 2024-10-15** (PR #4194; ≤0.15.0-b6 was MIT) | copyleft — treated like GPL: concepts only, no source consulted |
| FastLED/FastLED | MIT | palette-cycling tradition; not used as source |
| mrcodetastic/ESP32-HUB75-MatrixPanel-DMA (renamed from …-I2S-DMA) | MIT | the *panel aesthetic* our hub75-neon profile imitates; simulation written from scratch |
| lovyan03/LovyanGFX | core FreeBSD/BSD-2 (license.txt bundles third-party notices, hence GitHub "NOASSERTION") | the device push layer the firmware would use; irrelevant to this standalone code |
| lvgl/lvgl, thorvg/thorvg | MIT | not used |
| espressif/esp-dsp | Apache-2.0 (patent grant) | benchmark data only |
| Pixelblaze (simap/pixelblaze) | firmware proprietary; pattern library largely unlicensed | existence proof for per-pixel engines on ESP32; nothing reused |
| anki/cozmo-python-sdk, anki/vector-python-sdk | Apache-2.0 | contain **no** ProceduralFace parameter code |
| digital-dream-labs/vector (`cannedAnimLib/proceduralFace/`) | **Digital Dream Labs Software Asset License 1.0 — non-OSI, restricted** | the actual ProceduralFace parameter model lives here; **not consulted**. Our layout (eye centres/half-extents, lid closure, brow segments, gaze offsets) was designed from the generic keyframe fields already frozen in `face_keyframe.h` |
| DanielSWolf/rhubarb-lip-sync | MIT | upstream viseme context only |

**Reviewer-flag summary.** Copyright protects expression, not ideas, so
parametric eyes, glow falloff, ordered dither, and plasma are freely
reimplementable even where the best-known reference is GPL (RoboEyes)
or EUPL (WLED) — this file is the provenance note showing the
implementation was specified from published math and observation, not
translated from copyleft source. The sharpest residual risk is
patents around procedural robot emotional expression (Anki's
portfolio); mitigation here is that the parameter model is the
project's own pre-existing 12-byte keyframe, and eyes-as-rounded-rects
with lids is broad prior art (Uncanny_Eyes, RoboEyes, m5stack-avatar).
Nothing in this directory derives from the DDL-licensed Vector source
or from any Shadertoy shader.

## 6. Effect-by-effect provenance

| Effect in this contribution | Idea source | Status |
|---|---|---|
| 2D SDFs (circle, rounded box, capsule, segment) | IQ distfunctions2d (math) | math; MIT-granted reference snippets not copied |
| Quadratic smooth minimum | IQ smin article (math) | math |
| Distance-baked glow `exp(−k·d)` via iterated-ratio LUT | folklore + user-research briefs | original integer construction |
| Cosine palette (cyclic, c=1) | IQ palettes article (math) | math |
| 8×8/2×2 Bayer ordered dither | Bayer 1973 via Wikipedia | public-domain math |
| Scanlines, rolling bar, chromatic channel offset | CRT folklore (research brief §2) | original RGB565 mask implementation |
| Render-small-upscale pipeline | PicoDVI pattern (BSD) + brief §3 | pattern, reimplemented |
| Plasma (sum-of-sines + palette cycling) | demoscene folklore | original integer implementation |
| LED-matrix cell simulation | HUB75 panel appearance | original |
| Blink/saccade/breathing vocabulary | Cozmo/Vector behaviour *descriptions* in the user research; parameter model is this repo's own keyframe | original implementation, no DDL source consulted |
| Bhaskara I sine approximation | classical (7th century) | public domain |
| Alpha-max-beta-min, integer sqrt | Wikipedia references above | public-domain math |
| FNV-1a hash (tests only) | public-domain reference constants | public domain |

## 7. Open questions for the primary agent

- If any profile graduates into the firmware build, keep this file next
  to it; it is the derivation-hygiene record a license reviewer will
  ask for.
- The estimated S3 numbers use a deliberately pessimistic 100× host
  factor; an on-device `esp_timer` run of `bench/bench_cyber_face.c`
  (it is plain C11) should replace them before any fps claims ship.
