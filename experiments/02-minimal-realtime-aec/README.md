# 02 — Continuous Realtime voice, AEC, and a PCM-driven face

**Current status:** the native ESP-IDF firmware builds, and the exact face DSP
now runs device-free against a deterministic Grok-compatible voice server.
Physical speaker/AEC validation still requires the CoreS3.

This is not a continuation of the old half-duplex port. The microphone remains
open, AEC receives the actual speaker reference, and the selected Realtime
provider is a build-time track:

- xAI Grok Voice: native 16 kHz binary PCM (default);
- OpenAI Realtime: 24 kHz compatibility path with ASRC.

## PCM-driven face

The face follows PCM at the last useful point before sound leaves the device:

```text
Realtime packets → jitter buffers → leveler → codec write → speaker
                                             │
                                             └→ 10 ms face analysis → LVGL
```

It deliberately does not follow WebSocket arrival time. Buffered network audio
can be hundreds of milliseconds ahead of the speaker and would make lip sync
wrong.

The shared C driver interface now has three allocation-free implementations:

- a 60-byte mean-absolute-envelope and zero-crossing baseline;
- a 112-byte fixed-point Goertzel driver for coarse A/E/I/O/U and sibilant
  shapes without an FFT or PCM history buffer;
- a 6.3 KiB MFCC/prototype acoustic-viseme driver for the higher-quality host
  or larger-device track.

All three emit the same packed 12-byte semantic keyframe and have responsive
and smoother configurations. Strong syllables can affect the eyes and brow as
well as the mouth. Arbitrary packet boundaries cannot affect output.

## Run everything locally

The complete regression suite needs no device, API key, or audible output:

```sh
uv run tools/test_face_rig.py
```

It compiles the production C on the Mac, runs native unit tests and synthetic
CPU benchmarks for all three face drivers, exercises two different Grok
packetisations, and injects a timed network stall. Audio is always muted.

To watch the live, speaker-clocked face:

```sh
uv run tools/face_simulator.py --mode realtime --open
```

To compare all six algorithm/configuration combinations in WebAssembly:

```sh
cd tools/face-grid
npm run build
npm run serve
```

Then open <http://127.0.0.1:4173>. It accepts deterministic or captured Grok
PCM, a local 16 kHz PCM16 WAV, or a live microphone stream.

To create a deterministic replay report as fast as the host can run:

```sh
uv run tools/face_simulator.py \
  --mode virtual \
  --artifacts local/face-rig/manual
```

The artifact directory contains the received WAV, JSONL face/queue/underrun
trace, summary metrics, peak SVG, and a self-contained animated HTML report.

The fake Grok endpoint is independently runnable:

```sh
uv run tools/fake_grok_server.py --port 8765
```

`tools/realtime_probe.py --provider xai --url
'ws://127.0.0.1:8765/v1/realtime?model={model}' --no-auth` can then test the
same protocol seam used for real-provider probing.

## Render real Grok PCM videos

The real-provider video rig captures raw 16 kHz binary PCM and its WebSocket
frame boundaries, runs the samples through the production face C, and renders
MP4s with synchronized audio and a waveform/packet overlay:

```sh
uv run tools/make_grok_face_videos.py
```

The default run captures Leo, Rex, and Eve from
`wss://api.x.ai/v1/realtime`, loading the API key through the same
environment/Doppler path as `realtime_probe.py`. Use `--voices leo` for one
clip or `--reuse-captures --output-dir <existing-run>` to rerender without
another API call. Generated WAVs, packet manifests, face traces, render props,
MP4s, and a validation manifest remain under the gitignored
`local/grok-face-videos/` directory.

## Firmware

```sh
source ~/esp/esp-idf/export.sh
cd firmware-ws
idf.py build
```

Choose the Grok or OpenAI track with `idf.py menuconfig` under
**StackChan Realtime**. Local credentials stay in the gitignored `local/`
directory.

## What local testing proves

The host and WebAssembly rigs run the byte-identical face-analysis C used by
firmware. They prove protocol handling, playout-clock behaviour,
packet-boundary invariance, mouth timing, release, idle motion, rendering
geometry, deterministic artifacts, and underrun detection.

It cannot prove the CoreS3 codec/DMA timing, real LCD pixels, acoustic echo
path, or AEC quality. Those are covered by the device evidence and synchronized
three-channel AEC harness when hardware is present.

## Documentation

- [PCM face design and research](docs/pcm-face-rig.md)
- [Real-time WebAssembly face grid](docs/wasm-face-grid.md)
- [Sprite avatar production pipeline](docs/sprite-avatar-pipeline.md)
- [Current audio architecture](docs/architecture.md)
- [AEC validation](docs/aec-validation.md)
- [Device observability](docs/device-observability.md)
- [Earlier GitHub survey](docs/github-survey.md)
