# Architecture — continuous audio, AEC, and playout-synchronous face

## Device path

```text
                                      ┌─→ 10 ms face DSP ─→ LVGL face
                                      │
provider WS → output buffers → levelled PCM → codec/I2S → speaker
                                      │
                                      └───────────────┐
                                                      ▼
microphone codec → raw PCM ─────────────────────────→ AEC → clean uplink
                                                speaker reference
```

The microphone task does not stop while the agent speaks. Barge-in flushes
queued output, while synchronized speaker-reference PCM lets AEC distinguish
the agent from nearby user speech.

The face tap is inside the audio task immediately after the blocking codec
write. A failed write is represented as silence. This keeps network buffering
out of the animation clock and adds no face-specific audio buffer.

## Provider tracks

The firmware shares one audio/AEC pipeline and has build-time protocol
profiles:

| Track | Provider rate | Transport | Device conversion |
|---|---:|---|---|
| xAI Grok Voice | 16 kHz | binary PCM16 LE | none |
| OpenAI Realtime | 24 kHz | JSON/base64 PCM16 LE | ASRC to/from 16 kHz |

Both ultimately produce 16 kHz PCM for the CoreS3 speaker and face.

## Host equivalent

```text
deterministic fake Grok WS → speaker-like 10 ms queue
                                      │
                                      ▼
                production face_animator.c + face_geometry.c
                                      │
                         browser Canvas / trace artifacts
```

Real-time mode uses an absolute sample deadline, a 40 ms prebuffer, and
zero-filled underruns. Virtual mode removes wall-clock sleeps but advances the
same 10 ms playout clock. Neither mode opens an audio output device.

## Concurrency and allocation

- Audio is the sole face-state writer.
- LVGL is a non-blocking snapshot reader through an atomic seqlock.
- Face analysis performs no heap allocation, floating point, FFT, queue send,
  lock acquisition, or logging.
- LVGL updates only when the visible pose changes.
- Host allocation exists only in the `face_host_bridge` test adapter.

## Test boundary

Desktop simulation intentionally does not pretend to emulate the ESP32-S3.
It shares the deterministic production logic and replaces hardware at public
PCM/pose seams. Codec, DMA, LCD, and acoustic AEC are tested on the real device
through the diagnostic HTTP capture endpoints.
