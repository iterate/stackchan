# Session log — 01 AI_StackChan_Ex Realtime

Chronological notes from the first bring-up (public; no secrets).

## 2026-07-28

### Context

- Hardware: official M5 StackChan (CoreS3).
- Wanted: [AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex), OpenAI Realtime API, later MCP.
- Constraint: **no microSD card**.
- Operator is new to embedded; had assumed ESPHome (incorrect for this project).

### What we found

- Upstream Realtime setup: PlatformIO env `m5stack-cores3-realtime`, YAML for Wi‑Fi/key/LLM.
- Upstream CoreS3 path: YAML only from SD; missing SD → reboot loop.
- AtomS3R already loads YAML from SPIFFS; we reused that pattern for CoreS3 fallback.
- MCP client uses **legacy SSE** (`GET /sse`), not streamable-HTTP only servers.
- OpenAI key available via Doppler (`OPENAI_API_KEY` in project `os`); never written into git.

### What we changed

- Patch `main.cpp` for SD → SPIFFS fallback on Core2/CoreS3.
- Public monorepo `iterate/stackchan` with per-experiment folders.
- Experiment `01-ai-stackchan-ex-realtime`: docs, example config, scripts, patch.
- First boot plan: **voice only** (empty `mcpServers`); MCP in a later pass.
- Wi‑Fi SSID chosen for device: `mispwoso2` (password only in local gitignored files).

### Device bring-up

- [x] Flash `m5stack-cores3-realtime` + SPIFFS over `/dev/cu.usbmodem1101`
- [x] Serial: `SD not found; config loaded from SPIFFS.`
- [x] Wi‑Fi connected; IP assigned on LAN
- [x] **Realtime WebSocket confirmed:** `[WSc] Connected to url: /v1/realtime?model=gpt-realtime`, `session.updated`, streaming `response.output_audio.delta`
- [x] First utterance was Japanese **こんにちは** — upstream default role is Japanese, not a failed Realtime path
- [x] Reflashed with English default role (`patches/0002-english-default-role.patch`) + SPIFFS wipe so stored system prompt re-inits

### Latency note

On-device Realtime still has noticeable lag vs the ChatGPT phone app: ESP32 Wi‑Fi, PCM base64 over WebSocket, and `semantic_vad` waiting for end-of-speech. That is expected for this stack; it is still the Realtime audio API (not STT→LLM→TTS).

### Still to do

- [ ] Confirm English conversation after role patch
- [ ] Wire MCP (later)

