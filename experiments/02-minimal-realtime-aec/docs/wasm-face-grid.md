# Real-time WebAssembly face grid

The browser laboratory runs the production face-analysis C as WebAssembly. It
does not contain JavaScript reimplementations of the algorithms:

```text
WAV / real Grok capture / microphone
                    │
                    ▼
           mono PCM16 at 16 kHz
                    │
          one authoritative sample clock
                    │
       ┌────────────┼─────────────┐
       ▼            ▼             ▼
    envelope     Goertzel     MFCC viseme
       └────────────┼─────────────┘
                    ▼
       40-byte facial-animation IR
                    │
       32-byte sparse stage cues
                    │
                    ▼
       portable C sprite renderer
              ┌─────┴─────┐
              ▼           ▼
          device RGB565   browser Canvas
```

Every matrix row receives the same PCM subarray before any cell is rendered,
and every direction is evaluated at the same authoritative sample clock. This
makes comparisons meaningful and prevents independent browser timers from
drifting apart.

## Run it

```sh
cd tools/face-grid
npm run build
npm run serve
```

Open <http://127.0.0.1:4173>. The build output in `dist/` is an ordinary static
site. Measure the current optimized WebAssembly output rather than relying on
a checked-in size claim; the optional acoustic-viseme prototype model remains
a separate roughly 14 KiB asset.

## Native-scale review room

The top of the page is the judgement surface. It renders the active FSPR
sprite catalogue as independent 160×120 backing canvases, large enough to inspect without the
downsampling used by the coverage matrix. Previous/Next walks the complete
sprite catalogue; names live below the bitmap so labels never obscure mouth
or edge pixels. Clicking a card opens the still-larger live inspector.

The same toolbar selects the PCM capture, analyser family, and AI stage
direction. Recorded Grok and synthetic clips loop by default, with an explicit
one-shot toggle, restart, local mono PCM16 16 kHz WAV loading, and live
microphone capture. Switching source or microphone invalidates any pending
audio operation so an old clip cannot resume after a rapid mode change.

The giant Cartesian matrix remains below this room. Use the review room for
visual decisions and the matrix for broad combination coverage.

## Algorithms

| Driver | Features | Window | Persistent state | Model |
|---|---|---:|---:|---:|
| Envelope | mean absolute level + zero crossings | 10 ms | 60 B | none |
| Spectral | seven fixed-point Goertzel bands | 20 ms | 112 B | none |
| Acoustic viseme | MFCC/prototype classifier | 10 ms | 6,472 B | 14,352 B |

Each family has responsive and smoother configurations. The live page renders
their complete Cartesian product:

```text
6 PCM analysers × 11 stage directions × every active sprite atlas
```

The `matrix`, `overview`, and `atlas` densities render each face at 40×30,
40×30, and 20×15 CSS pixels respectively. They change presentation density,
not the production RGB565 algorithm. The scheduler runs at 30 fps; the
diagnostics separately report the time required to refresh the entire
thousands-of-faces atlas. Do not describe that full-atlas sweep rate as 30 fps.

The spectral family is the embedded quality/cost sweet spot: it adds coarse
A/E/I/O/U and sibilant shape without an FFT, heap allocation, PCM history
buffer, or floating-point work.

## Browser clocks and input

Recorded playback is keyed to `AudioContext.currentTime`, with one shared
`AudioBuffer` and one sample cursor. Live microphone capture uses an
`AudioWorklet`; echo cancellation, noise suppression, and automatic gain
control are requested off so the browser does not silently change the signal.
A streaming resampler converts the browser device rate, commonly 48 kHz, to
16 kHz before calling the same C ABI.

The page includes deterministic synthetic speech, captured Grok Leo/Rex/Eve
PCM, local mono PCM16 16 kHz WAV loading, and live microphone input. A direct
Grok connection should be added through a tiny server-side relay rather than
putting an API key in browser code. That relay can feed the existing PCM seam
without changing analysis or rendering.

## Stable interchange

The first 12 bytes of the packed 40-byte `face_keyframe_t` remain a stable
control prefix:

```text
mouth_open, mouth_width, mouth_round, mouth_press, mouth_teeth,
eye_open_left, eye_open_right, look_x, look_y, brow,
expression, flags
```

Bytes 12–39 add speech-result confidence and energy, viseme set/current/next
and coarticulation, speech phase, per-eye squint, inner/outer brows, independent
mouth corners, jaw, tongue, cheeks, head/body pose, valence, arousal,
attention, stage-expression ID and weight, and an IR version. The supported
viseme vocabularies include OVR15, VRM5, Preston9, Microsoft22, and custom
sources. At 30 fps the full IR is 1,200 bytes/s.

AI-authored performance direction uses a separate packed 32-byte stage cue:
expression, gaze, nod/shake/tilt/lean/bounce gesture, blend, easing,
interruptibility, and attack/hold/release timing. The cue is sparse; it is
resolved into the same 40-byte per-frame IR before rendering. This lets an
agent say “warm, glance left, then a small nod” without coupling tool semantics
to any particular face implementation.

## Verification

The page publishes machine-readable hooks:

```js
window.__STACKCHAN_GRID_READY__
window.__STACKCHAN_GRID_DIAGNOSTICS__
```

Diagnostics expose the source, clock, sample cursor, per-driver state size,
pose, and error status. They also expose the current review page/profile IDs,
native page size, selected analyser/direction, loop state, transport cursor,
and microphone state. Browser checks cover all recorded/synthetic sources,
real loop wraparound, pagination, native canvas dimensions, rapid input-mode
changes, and the real `AudioWorklet` microphone path.

The wider native regression remains:

```sh
uv run tools/test_face_rig.py
```

It tests packet-boundary invariance, all drivers, the active sprite renderer,
packed IR/stage cues, native/WASM byte identity, memory safety, and benchmark
budgets. `idf.py build` separately proves that the shared implementation still
compiles in the ESP32-S3 firmware.

Sprite-atlas promotion retains a deliberately human visual gate. The legacy
procedural-renderer quality scripts below remain useful for temporal checks
but are no longer the browser catalogue or production rendering architecture:

```sh
python3 tools/run_face_render_quality.py --strict \
  --output local/face-render-quality
python3 tools/review_face_motion.py local/face-render-quality
```

Inspect all 11 expressions, viseme sheets, and temporal storyboards both at
native 160×120 and at the exact unsmoothed 40×30 contact scale. The repeatable
packet contains labelled expression/contact atlases, a uniform-cycle temporal
strip, frame-delta evidence, metrics, and a manifest. Geometry, duplicate, and
motion metrics may reject a renderer, but they cannot promote one by
themselves: clipped anatomy, detached mouths, weak acting, and decoration that
out-acts the face require visual review.

The optional Grok reviewer consumes the same ordered PNG storyboards. Its
advice is secondary and must be checked against the actual pixels. The current
official xAI API accepts base64 PNG/JPEG inputs for
[image understanding](https://docs.x.ai/developers/model-capabilities/images/understanding).
Its documented video-understanding switch is limited to videos encountered by
[X Search](https://docs.x.ai/developers/tools/x-search), while the separate
[Imagine video API](https://docs.x.ai/developers/rest-api-reference/inference/videos)
generates, edits, or extends video rather than critiquing an arbitrary local
MP4. The deterministic ordered storyboard is therefore both supported and
more reproducible for automated animation review.

For contact sheets whose row numbers are not printed into the bitmap, pass an
authoritative mapping instead of asking the model to infer identities:

```sh
uv run tools/review_face_visuals.py \
  local/face-render-quality/expression-atlas.png \
  --context "Rows 00..66 are legacy renderer IDs 00..66 in order." \
  --output local/face-render-quality/grok-expression-review.json
```

`--context-file` is available for longer multi-attachment manifests. The
review result is evidence for a human decision, never an automatic promotion
or source-edit instruction.

## Prior-art boundary

The renderer follows the established small-parameter procedural-face pattern.
`m5stack-avatar` is the closest permissively licensed source reference.
RoboEyes is useful conceptual prior art for cheap parametric eyes, but its GPL
implementation is not copied into this project.
