# Sprite avatar production pipeline

## End goal

Create an `agentskills.io`-style skill plus deterministic tooling that can take
an arbitrary character prompt and optional reference image, use image
generation APIs to author a moving avatar, and produce validated packages for
the browser and several classes of embedded display.

The finished avatar must respond to:

- streaming PCM;
- transcript deltas and speech-started/speech-ended events;
- visemes or phonemes when the provider exposes them;
- AI-authored emotion and stage directions;
- procedural blink, gaze, head turn, head bob, and idle timing.

It must include font rendering and timed transcript presentation, not only the
face.

## Portable boundary

WASM is the portable animation/state runtime, not the browser's display API:

```text
PCM + realtime events + stage directions
                  |
                  v
       retained avatar state machine
                  |
                  v
 resolved sprite layers + draw commands + text layout
             /                         \
            v                           v
  ESP32 display backend          browser Canvas backend
  M5GFX/LovyanGFX/LVGL          Canvas2D/WebGL/ImageData
```

The behavior core, atlas selection, coarticulation, blink/gaze/idle clocks, and
fallback rules are portable C. The browser calls that C through WASM. ESP-IDF
compiles the same files
directly. Backends only handle target framebuffer details such as RGB565 vs
RGBA, scaling, rotation, clipping, and display flush.

For tiny avatars, flash-resident indexed bitmaps are preferred over runtime
image decoding. A compiled atlas contains:

- a palette;
- PackBits or raw indexed pixel cells;
- expression banks;
- named eye, lid, pupil, brow, mouth, body, and accessory layers;
- anchors, pivots, safe bounds, and per-layer z order;
- viseme-vocabulary maps and fallback roles;
- retained transition, hold, blink, gaze, and idle timing.

The source of truth is a target-neutral manifest. The compiler emits both C
flash tables and a web inspection bundle, while native and WASM tests prove
that both resolve the same layer and draw-command sequence.

The current one-command publication seam is:

```sh
cd tools/sprite-pipeline
python3 -B avatar_pipeline.py publish
```

It discovers `characters/*/avatar.json`, assembles and validates every
character, installs the CoreS3 C atlases, and regenerates the catalogue shared
by the firmware and browser/WASM build. Adding a character does not require a
hand edit to either renderer registry.

## Authoring assembly line

1. Turn the prompt/reference into a constrained character design sheet.
2. Generate expression, eye, mouth/viseme, turn, and accessory sheets on a
   flat background with stable framing.
3. Segment and normalize cells.
4. Pixel-snap at the native authoring resolution.
5. Quantize to the target palette and remove isolated/noisy pixels.
6. Catalogue named parts and infer candidate anchors.
7. Present an interactive review step for correcting crops, anchors, pivots,
   z order, and fallback mappings.
8. Compile a canonical atlas.
9. Derive target packages from it, including alternate resolutions/palettes
   where simple scaling is insufficient.
10. Validate all emotions, visemes, blinks, turns, speech cadence, clipping,
    memory, frame time, and native/WASM parity.

Image-model output is source material, never trusted runtime data. The compiler
owns the actual resolution, palette, transparency, landmarks, fallbacks, and
validation.

## Target profiles

Each device profile declares:

- physical and logical resolution;
- pixel format and palette limits;
- nearest-neighbour scale or authored-resolution variant;
- maximum flash, RAM, scratch bytes, and frame time;
- display backend and orientation;
- permitted layer count and transition effects.

An avatar package may share semantic banks while carrying different raster
cells for 40x30, 80x60, 160x120, or other targets. Validation is per target;
passing at one resolution does not imply another target is acceptable.

## Portable-boundary proof

The first hardware proof is complete. The browser/WASM review room and the
CoreS3 firmware compile the same FSPR atlas registry and C sprite renderer.
CoreS3 renders a 160×120 logical sprite framebuffer at 2× nearest-neighbour
scale on its 320×240 LCD. A screen tap or `POST /api/avatar` cycles the
device-valid subset, while the evidence harness captures every real LCD
framebuffer and restores the prior selection.

The browser catalogue may include zoomed-out characters and experimental
atlases. The CoreS3 registry deliberately exposes only full-screen,
black-background feature faces that treat StackChan itself as the character.
