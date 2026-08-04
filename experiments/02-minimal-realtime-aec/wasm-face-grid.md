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
       12-byte semantic keyframes
                    │
                    ▼
          Canvas parametric faces
```

Every grid cell receives the same PCM subarray before any cell is rendered.
This makes visual comparisons meaningful and prevents six independent browser
timers from drifting apart.

## Run it

```sh
cd tools/face-grid
npm run build
npm run serve
```

Open <http://127.0.0.1:4173>. The build output in `dist/` is an ordinary static
site. The current optimized WebAssembly module is about 17 KiB; the acoustic
viseme prototype model is a separate 14 KiB asset.

## Algorithms

| Driver | Features | Window | Persistent state | Model |
|---|---|---:|---:|---:|
| Envelope | mean absolute level + zero crossings | 10 ms | 60 B | none |
| Spectral | seven fixed-point Goertzel bands | 20 ms | 112 B | none |
| Acoustic viseme | MFCC/prototype classifier | 10 ms | 6,472 B | 14,352 B |

Each family has a responsive and a smoother configuration in the six-cell
grid. The middle spectral driver is intended as the embedded quality/cost sweet
spot: it adds coarse A/E/I/O/U and sibilant shape without an FFT, heap
allocation, PCM history buffer, or floating-point work.

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

The analyzer/renderer boundary is a packed 12-byte `face_keyframe_t`:

```text
mouth_open, mouth_width, mouth_round, mouth_press, mouth_teeth,
eye_open_left, eye_open_right, look_x, look_y, brow,
expression, flags
```

All values are semantic bytes, so another renderer, a network keyframe source,
or an on-device implementation can replace either side independently.

## Verification

The page publishes machine-readable hooks:

```js
window.__STACKCHAN_GRID_READY__
window.__STACKCHAN_GRID_DIAGNOSTICS__
```

Diagnostics expose the source, clock, sample cursor, per-driver state size,
pose, and error status. Browser checks cover all recorded/synthetic sources and
the real `AudioWorklet` microphone path.

The wider native regression remains:

```sh
uv run tools/test_face_rig.py
```

It tests packet-boundary invariance and all three drivers, then benchmarks the
same source. `idf.py build` separately proves that the shared implementation
still compiles in the ESP32-S3 firmware.

## Prior-art boundary

The renderer follows the established small-parameter procedural-face pattern.
`m5stack-avatar` is the closest permissively licensed source reference.
RoboEyes is useful conceptual prior art for cheap parametric eyes, but its GPL
implementation is not copied into this project.
