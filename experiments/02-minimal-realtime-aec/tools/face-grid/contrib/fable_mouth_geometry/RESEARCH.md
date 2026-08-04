# Source & licensing research — fable_mouth_geometry

All renderers in this directory are **original, independently implemented
C**. No source code was copied from any external project. The systems below
were studied as *behavioral and historical references* via their first-party
docs, papers, and repositories (research pass 2026-07-28); licenses were
verified so nothing copyleft or proprietary leaks into this contribution.

## Mouth-shape and viseme systems

| Source | License | What we used |
|---|---|---|
| Preston Blair, *Advanced Animation* (Walter T. Foster, 1947, p. 35; reprinted in *Cartoon Animation*, 1994) | Copyrighted book art; the phoneme *groupings* are uncopyrightable facts/method | The classic ten-shape chart (A I / E / O / U / consonant group / F V / L / M B P / W Q / rest) shapes the cel roster in `fmg_r_preston.c`. All cel artwork is drawn from scratch as ASCII art. |
| Rhubarb Lip Sync — github.com/DanielSWolf/rhubarb-lip-sync | MIT | Shape *semantics* only: A closed-pressure (PBM), B clenched teeth (K/S/T/EE), C mid-open transition, D wide open, E rounded, F pucker, G teeth-on-lip (F/V), H tongue-up L, X rest. Mirrored in the `FMG_VIS_*` classes and the C-as-inbetween rule. |
| JALI (Edwards, Landreth, Fiume, Singh, SIGGRAPH 2016, DOI 10.1145/2897824.2925984) | ACM paper, no code released; commercialized separately | The two-axis insight — JA (jaw+tongue) vs LI (lip muscle) with the "ventriloquist singularity" at (0,0), and that bilabials must still close at zero jaw. Implemented as `jaw_q8` (open gated by press) and `lip_q8` in `fmg_mouth_compute`, and the per-column envelope mouth in `fmg_r_jali.c`. |
| Cohen & Massaro 1993, "Modeling Coarticulation in Synthetic Visual Speech" (Springer, DOI 10.1007/978-4-431-66911-1_13) | Copyrighted chapter; equations freely implementable | The dominance idea — overlapping per-segment exponential dominance so targets blend and are rarely fully reached — reduced to integer one-pole filters with per-articulator attack/release and explicit press-over-jaw dominance in `fmg_coart.c`. |
| Meta Oculus Lipsync viseme reference | Proprietary SDK; the 15-viseme *set* (MPEG-4 derived) is factual | Cross-checked that our 10 discrete classes cover the same articulatory space the 15-viseme sets span. |
| Amazon Polly viseme speech marks (AWS docs) | AWS docs; symbol set is API surface | Confirms the host-side keyframe stream design; not used directly. |
| SCUMM/Sierra talkie formats (ScummVM docs/wiki; ScummC wiki) | ScummVM is GPL-2.0-or-later — **format facts only, no code** | Historical grounding for `fmg_r_talkie.c`: v6+ SCUMM used authored VCTL timestamp tracks to advance small mouth-cel sets; v5-era talkies simply cycled talk frames — our amplitude ladder is that era's look driven by the keyframe instead. |
| Thomas & Johnston, *The Illusion of Life* (1981) | Copyrighted book; the 12 principles are ideas | Anticipation (pre-blink widen), follow-through (lid overshoot on reopen), slow-in/slow-out (smoothstep envelopes) in `fmg_idle_compute`. |

## Robot-face and display prior art

| Source | License | What we used |
|---|---|---|
| meganetaaan/m5stack-avatar | MIT | Reference for the parts/expression architecture and open-ratio mouth convention; no code taken. |
| FluxGarage/RoboEyes | **GPL-3.0** | Behavior description only (rounded-rect eyes, autoblinker, idle reposition). Nothing was ported — this pack's idle engine is an independent design; keeping GPL code out was a hard constraint. |
| Anki Cozmo/Vector procedural face — official SDKs Apache-2.0; parameter model documented by zayfod/pycozmo (MIT) | Apache-2.0 / MIT | Validated the "small parameter set + procedural saccades/blinks" approach (pycozmo documents 43 parameters on a 128x64 canvas; Vector screen 184x96 RGB565). Conceptual template for the idle chassis. |
| Adafruit Uncanny Eyes / M4_Eyes | MIT | Considered (polar-LUT textured eyes) and rejected as heavier than parametric primitives; noted for future eye-focused packs. |
| Knight Rider KITT voice modulator (fan wiki + replica part docs) | TV prop; replicate the *idea*, not trade dress | Three-column mirrored VU bars, center taller, red segments — `fmg_r_ledvu.c`. Our bezel/eye design is original; the idle scanner sweep is a nod, not a copy. |
| Big Mouth Billy Bass (Gemmy, 1999; Hackaday coverage) | n/a | The canonical amplitude-flap mouth: justification for the talkie/VU amplitude ladder. |
| Flip-disc displays (Wikipedia; AlfaZeta XY5) | n/a (technology) | Two-state discs, coarse pitch, dark unlit side, mechanical board updates — `fmg_r_dotmatrix.c` (unlit discs rendered dark, one-dot board shifts). |
| Oscilloscope art & music: Ben Laposky "Oscillons" (1952-), Jerobeam Fenderson oscilloscopemusic.com, Vectrex vector CRT | n/a | The XY/Lissajous vocabulary of `fmg_r_scope.c`: ring eyes, waveform mouth, phosphor persistence faked with phase-offset passes. |
| Wintercroft low-poly masks; Star Fox-era flat shading | Wintercroft templates are commercial/proprietary | Aesthetic direction only for `fmg_r_origami.c`; the 20-vertex/27-facet mesh is authored from scratch. |

## In-repo sources

- `firmware-ws/main/face_keyframe.h` / `face_render.h` — the 12-byte keyframe
  ABI and render contract mirrored (not included) by `src/fmg.h`.
- `firmware-ws/main/face_viseme.c` `s_shapes` — the host's 15-viseme →
  keyframe parameter table. The `FMG_VIS_*` anchor poses reuse those values
  (same repo, first-party) so classification inverts the host's own mapping.

## Algorithms & constants

- Bhaskara I sine approximation (7th-century identity, public domain) —
  `fmg_sin_q14`, chosen over a LUT so no generated tables need auditing.
- `lowbias32` integer hash by Chris Wellons (public domain, from his
  "prospecting for hash functions" work) — deterministic idle scheduling.
- CRC-32 (ISO 3309 polynomial 0xEDB88320, public domain) — golden tests.
- Standard bit-by-bit integer square root (public domain folklore) —
  ellipse scanlines.

## Platform facts (first-party)

- M5Stack CoreS3: 320x240 ILI9342C IPS, ESP32-S3, 16 MB flash + 8 MB PSRAM
  (docs.m5stack.com/en/core/CoreS3) — the 160x120 target is exactly half
  resolution for 2x integer upscale.
- ESP32-S3: dual Xtensa LX7 @ 240 MHz with vector instructions
  (espressif.com/en/products/socs/esp32-s3).

## License posture of this contribution

Everything here is original work: safe under the repository's license.
Attribution notes above are informational; no external license obligations
attach because no external code or artwork was copied.
