# AEC validation

“AEC initialized” does not prove that the acoustic path works. StackChan keeps
an objective diagnostic path in the firmware so echo cancellation can be tuned
and regression-tested independently of OpenAI Realtime.

## Capture contract

The firmware records one synchronized 16 kHz, signed 16-bit PCM WAV:

| Channel | Signal |
|---|---|
| 0 | Raw ES7210 microphone input |
| 1 | Exact PCM written to the AW88298 speaker path |
| 2 | ESP-SR full-duplex AEC output |

All three channels are written in the same audio-frame loop. This matters:
recording the speaker reference in a different task would hide timing and queue
errors—the most common reason an apparently configured AEC fails.

The diagnostic HTTP API is:

| Request | Purpose |
|---|---|
| `POST /api/diag/signal` | Upload mono 16 kHz, signed 16-bit little-endian PCM |
| `POST /api/diag/start` | Start synchronized playback and capture |
| `GET /api/diag/status` | Poll `idle`, `running`, `ready`, or `error` |
| `GET /api/diag/capture.wav` | Download the latest three-channel capture |

Diagnostic buffers live in PSRAM and are bounded. Credentials never appear in
the capture or its HTTP metadata.

## Host harness

The harness is an executable Python script with `uv`-managed dependencies:

```bash
cd experiments/02-minimal-realtime-aec
./tools/aec_lab.py self-test
```

The self-test synthesizes room echo and produces known-good and known-bad AEC
outputs. It must show the expected result for all far-end, near-end, and
double-talk fixtures before device measurements are trusted.

Once firmware is reachable:

```bash
./tools/aec_lab.py run \
  --device http://stackchan.local \
  --output local/aec-runs/$(date +%Y%m%d-%H%M%S)
```

The suite performs three runs:

1. **Far-end only:** StackChan plays a deterministic speech-like signal. This
   measures echo return loss enhancement and reference leakage.
2. **Near-end only:** the Mac plays a deterministic `say` phrase while
   StackChan’s reference is silent. This catches an over-aggressive canceller
   that removes the user along with the echo.
3. **Double-talk:** StackChan and the Mac speak together. This verifies the
   barge-in condition.

Each run produces an HTML report, JSON metrics, waveform and spectrogram plots,
the original three-channel capture, and separate WAV stems for listening.

## Initial acceptance thresholds

The defaults intentionally demand useful voice behavior rather than merely a
nonzero effect:

- Far-end ERLE: at least 12 dB after 750 ms adaptation.
- Speaker-reference correlation reduction: at least 6 dB.
- Band-limited speaker transfer-gain reduction: at least 6 dB.
- Near-end speech attenuation: no worse than 8 dB.
- Near-end raw/clean waveform similarity: at least 0.80.
- Clean-channel clipping: at most 0.1% of samples.

These are starting gates. Record the measured CoreS3 baseline before tightening
them; do not weaken them simply to turn a failing run green.
