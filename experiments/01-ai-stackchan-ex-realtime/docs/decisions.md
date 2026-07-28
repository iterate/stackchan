# Decisions — 01 AI_StackChan_Ex Realtime

## Why not ESPHome?

AI_StackChan_Ex is an **Arduino / PlatformIO** project with a large custom audio + WebSocket + avatar stack. Porting it to ESPHome would be a rewrite, not a config tweak. This experiment tracks the **supported** upstream path.

## Why SPIFFS instead of buying a microSD?

Upstream docs put YAML on SD (`/yaml/…`, `/app/AiStackChanEx/…`). Official StackChan *has* an SD slot on CoreS3, but we did not have a card available.

AtomS3R already supports SPIFFS-only config. We extended CoreS3/Core2 to **fall back to SPIFFS** when `SD.begin` fails, using the same flat filenames:

- `/SC_ExConfig.yaml`
- `/SC_SecConfig.yaml`
- `/SC_BasicConfig.yaml`

Tradeoffs:

- SPIFFS is smaller than SD; keep YAML tiny.
- API keys live on device flash (same class of risk as SD).
- After first Wi‑Fi connect, FTP (`stackchan` / `stackchan`) can edit SPIFFS without reflashing.

## Board / PlatformIO env

| Hardware | PlatformIO env |
|----------|----------------|
| Official M5 StackChan (CoreS3) | `m5stack-cores3-realtime` |
| DIY Core2 Stack-chan | `m5stack-core2-realtime` |

Realtime is a **build flag** (`-DREALTIME_API`), not a YAML toggle.

## Servo YAML for official StackChan

From upstream basic-usage docs for **M5_SCS** (Feetech SCS0009 on StackChan product):

- pins `x: 7`, `y: 6`
- center `x: 150`, `y: 90`
- `servo_type: "M5_SCS"`

Copy-to-SD samples in upstream still show Core2 PWM defaults; do not use those blindly on the product unit.

## OpenAI Realtime

- Model endpoint used by upstream: `gpt-realtime` over WSS to `api.openai.com`
- Key field: `apikey.aiservice` in `SC_SecConfig.yaml`
- STT/TTS YAML entries are largely unused on the pure Realtime audio path
- Touch forehead to start/stop listening

## MCP (deferred for first boot)

Upstream `MCPClient`:

- Connects to `url:port`
- `GET /sse` for the event stream (legacy MCP SSE)
- Discovers tools and registers them as OpenAI function tools on the Realtime session

Requirements for a custom MCP server:

1. Reachable from the robot’s LAN IP (not localhost-on-Mac from the robot’s POV)
2. SSE transport **or** Supergateway in front of stdio MCP
3. Listed under `llm.mcpServers` with `disabled: false`

First bring-up leaves `mcpServers: []` so voice works even if no tool server is running.

## Secrets handling

| Where | Committed? |
|-------|------------|
| `config/*.example` | Yes — placeholders only |
| `local/*` | No — gitignored |
| Doppler `OPENAI_API_KEY` | External |
| Wi‑Fi password | `local/` only |

## Upstream worktree policy

`upstream/` is gitignored so this public repo stays small and we never accidentally push a forked full firmware history with keys in `data/`. Scripts re-clone and re-apply the patch.
