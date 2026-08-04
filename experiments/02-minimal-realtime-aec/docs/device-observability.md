# Device observability

The native WebSocket firmware brings up Wi-Fi and the diagnostic HTTP server
before audio. A codec, AEC, or Realtime failure therefore remains remotely
inspectable instead of turning the screen into the only error channel.

Collect one timestamped evidence bundle from the Mac:

```sh
uv run tools/collect_device_evidence.py \
  --device http://stackchan.local \
  --label aec-baseline
```

Bundles are written beneath the gitignored `local/device-evidence/` directory.
Each bundle contains:

- `screen.bmp`: a real LVGL render of the active 320×240 screen;
- `avatar.json`: the current sprite avatar selection;
- `avatars/*.bmp`: one real LCD framebuffer for every device-valid avatar;
- `status.json`: application phase and audio counters;
- `debug-state.json`: firmware/build/reset identity and heap telemetry;
- `device.log`: the recent in-memory ESP log ring;
- `realtime-events.jsonl`: timestamped WebSocket/VAD/transcript/control events
  plus summarized audio-frame counts, retained in a bounded PSRAM journal;
- `recent-audio.wav`: the last five seconds of synchronized 16 kHz audio;
- `audio/mic-raw.s16le.pcm`: raw microphone samples;
- `audio/speaker-reference.s16le.pcm`: the exact samples sent to the codec;
- `audio/aec-clean.s16le.pcm`: the corresponding AEC output;
- `manifest.json`: endpoint outcomes, sizes, SHA-256 hashes, and audio format;
- a ZIP archive of the same evidence.

The three audio channels are sample-aligned and have a stable meaning:

1. raw microphone input;
2. exact speaker reference used by the echo canceller;
3. AEC-clean microphone output.

The firmware endpoints are:

| Endpoint | Purpose |
| --- | --- |
| `GET /api/status` | Application and audio counters |
| `GET /api/avatar` | Current avatar index, count, slug, and name |
| `POST /api/avatar` | Select the next device-valid avatar |
| `GET /api/debug/state` | Build, reset, uptime, and heap state |
| `GET /api/debug/logs.txt` | Recent captured device logs |
| `GET /api/debug/realtime-events.jsonl` | Structured recent Realtime event journal; accepts `?since=<sequence>` |
| `GET /api/debug/screen.bmp` | Actual rendered display |
| `GET /api/debug/recent.wav` | Rolling synchronized PCM window |
| `GET /api/diag/capture.wav` | Completed controlled AEC experiment |

The rolling audio recorder never blocks the audio task. While a host downloads
its stable view, new trace frames may be skipped, but capture cannot disturb
codec I/O or AEC processing.

By default the evidence collector cycles through the small device avatar
registry, captures the actual display after every selection, and restores the
original selection. Pass `--no-avatar-sweep` when a read-only capture is
required.
