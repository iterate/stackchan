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

### Still to do on device

- [ ] `apply-local-config.sh` with real local secrets
- [ ] `flash.sh` when CoreS3 is plugged in
- [ ] Confirm serial line `SD not found; config loaded from SPIFFS.`
- [ ] Confirm Realtime conversation
- [ ] Add MCP experiment step / second folder or extend this one
