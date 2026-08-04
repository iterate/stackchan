# PCM-driven face and device-free test rig

This design came from a source-first research pass using Claude CLI with the
Fable model at xhigh effort, followed by implementation and measured host
tests. The research covered the current firmware, official StackChan and
xiaozhi sources, xAI's protocol, LVGL, ESP-IDF host/QEMU support, Wokwi, Renode,
and established volume-driven lip-sync techniques.

## Design invariants

1. Animate from PCM actually handed to the codec, never network receipt.
2. Add no playback delay and no second audio buffer.
3. Never block or allocate on the audio task.
4. Produce the same pose for the same PCM regardless of WebSocket chunking.
5. Use the same C engine on the ESP and host.
6. Keep local audio muted unless a future tool explicitly opts in.

## Pluggable face engine

`face_driver` decouples streaming PCM analysis from rendering. Each driver
accepts arbitrary PCM16 chunks and emits the same packed 12-byte semantic
keyframe, so the analyzer can run on the device, in WebAssembly, or on a host
without changing the renderer.

The baseline `face_animator_push_pcm()` completes fixed 10 ms windows:

`face_animator_push_pcm()` accepts arbitrary signed PCM16 chunks and completes
fixed 10 ms windows:

- mean absolute sample magnitude drives mouth opening;
- a fixed noise floor suppresses silence, followed by a compressed range tuned
  against real Grok speech so ordinary vowels use most of the available pose;
- fast attack and slower release avoid lag and chatter;
- zero-crossing rate narrows high-frequency/fricative-like frames, providing
  visible variety without an FFT;
- the same smoothed mouth envelope subtly lifts and squints the eyes, coupling
  speech to the whole expression without another state variable;
- blink and gaze use a seeded xorshift generator advanced by playout samples.

The renderer-facing pose is 24 bytes; the stable analyzer-facing keyframe is
12 bytes. The baseline persistent engine is 60 bytes. It is integer-only and
allocation-free. An atomic seqlock gives LVGL a coherent snapshot without
making the audio writer wait.

The 112-byte spectral driver adds seven fixed-point Q14 Goertzel bands over a
20 ms triangular window. Band ratios produce coarse A/E/I/O/U and sibilant
shapes without an FFT, heap allocation, model, or retained PCM buffer.

The acoustic-viseme driver uses the existing MFCC/prototype classifier when a
larger memory budget is acceptable. It remains behind the same driver and
keyframe interfaces.

The device renderer is five LVGL objects: two eyes, two pupil children, and one
mouth. Pixel geometry is also shared with the host renderer.

## Why this clock

The network output stream and pipeline playback stream can put received audio
far ahead of acoustics. The codec-write point is bounded only by I2S/DMA and
display refresh. It is therefore the closest useful software point to audible
playout without adding a new delay.

The official StackChan firmware's `SpeakingModifier` changes mouth weight on a
default 180 ms timer, while its mouth geometry is a useful rounded-rectangle
interpolation. The implementation here keeps the simple geometry but replaces
timer flapping with measured PCM. See the first-party
[`speaking.h`](https://github.com/m5stack/StackChan/blob/b72b3ede38b32d54f0b6ba51c62cfcef2ec3ae1e/firmware/main/stackchan/modifiers/speaking.h)
and
[`mouth.cpp`](https://github.com/m5stack/StackChan/blob/b72b3ede38b32d54f0b6ba51c62cfcef2ec3ae1e/firmware/main/stackchan/avatar/skins/default/mouth.cpp).

More elaborate MFCC/phoneme approaches exist, such as
[`uLipSync`](https://github.com/hecomi/uLipSync). The drivers are deliberately
interchangeable so envelope, lightweight spectral classification, and
acoustic-viseme approaches can be compared against the same PCM and renderer
instead of choosing by intuition.

## Fake Grok Voice server

`tools/fake_grok_server.py` implements the xAI protocol subset consumed by the
firmware tooling:

- `session.created`, `conversation.created`, and `session.updated`;
- binary little-endian PCM at the configured supported rate;
- response/transcript lifecycle events;
- response cancellation;
- binary microphone input, deterministic server VAD events, and an automatic
  response;
- configurable packet sizes, time scale, and one injected stall.

The voice waveform is synthesized with integer arithmetic and no randomness.
Real-time pacing uses an absolute sample deadline, so per-packet scheduling
overhead cannot accumulate into false underruns. xAI documents binary PCM at
16 kHz and the JSON lifecycle/binary-audio split in its
[speech-to-speech API](https://docs.x.ai/developers/model-capabilities/audio/speech-to-speech).

## Simulator modes

`tools/face_simulator.py` compiles the exact production C into a temporary host
library and connects it to the fake server.

| Mode | Clock | Primary use |
|---|---|---|
| `realtime` | 10 ms wall-clock playout, 40 ms prebuffer | live face and jitter/stall behaviour |
| `virtual` | deterministic 10 ms virtual clock | fast regression and exact hashes |

The browser is a renderer, not the face algorithm. `--open` streams pose
snapshots to Canvas; virtual runs embed the complete trace in a standalone HTML
replay.

Every artifact run contains:

- `response.wav`;
- `face-trace.jsonl`, including queue depth and underrun flags;
- `summary.json`, including PCM/trace hashes and onset timing;
- `peak-face.svg`;
- `face-report.html`;
- the compiled host library.

## Real-provider video evidence

`tools/make_grok_face_videos.py` uses `realtime_probe.py` to capture genuine
xAI Realtime binary PCM and a sidecar manifest for every received WebSocket
audio frame. The exact captured PCM is then processed by the shared production
C engine and rendered at the device UI's 30 fps using the Remotion composition
under `tools/face-video/`.

Each MP4 embeds the real response audio and shows:

- the same 320×240 face geometry used by the firmware, enlarged 2×;
- the raw PCM waveform with the current 10 ms playout window highlighted;
- alternating spans for the original Grok binary WebSocket frames;
- playout sample indices, PCM level, zero crossings, mouth state, and packet
  payload size;
- the transcript returned by the same Realtime response.

Rendering does not play the audio through the Mac. Capture WAVs, exact PCM
hashes, render props, MP4 metadata, and final videos are kept together beneath
`local/grok-face-videos/`.

## One-command regression

```sh
uv run tools/test_face_rig.py
```

The command:

1. compiles/runs C tests for animation, driver dispatch, geometry, keyframe
   packing, spectral analysis, acoustic visemes, host bridge, and the existing
   speech leveler;
2. checks the per-driver RAM budgets;
3. benchmarks synthetic PCM through every driver;
4. compares 54-frame irregular packetisation with 13-frame coarse
   packetisation and requires identical PCM and face-trace hashes;
5. streams binary microphone PCM through the fake server's VAD and requires
   speech start/stop, transcription, and automatic response events;
6. requires mouth response within 20 ms;
7. runs an unstalled real-time stream with zero underruns;
8. injects a 250 ms stall and requires roughly 25 ten-millisecond underrun
   frames (one wall-clock tick of scheduler tolerance);
9. requires audio playback to remain disabled in every run.

The observed host numbers on an Apple Silicon Mac are 60 bytes for envelope,
112 bytes for spectral, and 6,472 bytes plus a 14,352-byte model for acoustic
visemes. PCM-to-mouth onset is 9.9375 ms, and every driver runs thousands of
times faster than real time on the host. Throughput measures the host, not the
ESP32-S3; the strict properties are bounded memory, absence of allocation,
fixed work per sample, and a successful firmware build.

## Why not an ESP emulator

None of the available options provides a meaningful end-to-end CoreS3 audio
simulation:

- ESP-IDF host applications are intended for portable logic and do not supply
  the CoreS3 codec/I2S/acoustic path
  ([official host-app docs](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-guides/host-apps.html));
- ESP-IDF QEMU can exercise an ESP32-S3 CPU/boot image, but its documented
  peripherals do not reproduce this I2S codec path
  ([official QEMU docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/qemu.html));
- Wokwi's ESP32 simulator does not provide the required CoreS3 I2S/display
  fidelity ([official ESP32 guide](https://docs.wokwi.com/guides/esp32));
- Renode's Xtensa work is not a CoreS3 board/audio model
  ([Xtensa support overview](https://antmicro.com/blog/2022/01/xtensa-isa-in-renode-for-sof-project)).

Sharing the pure production logic and replacing hardware at explicit PCM and
pose seams gives stronger, faster tests than a CPU emulator with fake
peripherals. Hardware-only behavior remains an explicit later test, not an
accidental blind spot.
