# 01 — AI_StackChan_Ex + OpenAI Realtime (no microSD)

**Goal:** Run [ronron-gh/AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) on the official **M5 StackChan (CoreS3)** with the **OpenAI Realtime API**, without a microSD card, and document every step for someone new to embedded tooling.

**Status:** repo scaffolding + SPIFFS fallback patch ready; flash once Wi‑Fi secrets are in `local/` and PlatformIO build succeeds.

**Not ESPHome.** This stack is **VS Code / CLI PlatformIO + Arduino**. ESPHome is a different ecosystem; ignore it for this experiment.

---

## Architecture

```text
 Mac (you)                         StackChan CoreS3
 ─────────                         ────────────────
 PlatformIO ──USB-C──► flash app + SPIFFS YAML
                         │
 OpenAI Realtime API ◄──Wi‑Fi── WebSocket audio (gpt-realtime)
                         │
 (later) MCP SSE :port ◄──Wi‑Fi── function tools on your LAN
```

Upstream normally expects YAML on a **microSD**. We do not have one, so we:

1. Patch firmware so CoreS3 **falls back to SPIFFS** when SD mount fails (same idea as AtomS3R).
2. Put YAML in `firmware/data/` and flash with PlatformIO **`uploadfs`**.

---

## What you need

| Item | Required? | Notes |
|------|-----------|--------|
| Official M5 StackChan (CoreS3) | Yes | Cube with face + pan/tilt |
| USB‑C **data** cable | Yes (for flash) | Power-only cables fail upload |
| microSD | **No** | SPIFFS instead |
| Wi‑Fi credentials | Yes | Same LAN as any future MCP host |
| OpenAI API key | Yes | We pull from Doppler locally; never commit |
| PlatformIO Core (`pio`) | Yes | `brew install platformio` or pip |
| MCP server | Optional | Skipped for first voice-only bring-up |

### Cables / ports

1. Plug USB‑C into the **CoreS3** on StackChan (the module with the screen — side/bottom USB‑C).
2. Other end into your Mac.
3. After flashing you can unplug; the robot only needs power + Wi‑Fi for Realtime. MCP later needs your Mac (or server) reachable on the LAN.

No breadboard wires or ESPHome USB adapters beyond that.

---

## Repo layout for this experiment

```text
01-ai-stackchan-ex-realtime/
  README.md                 ← you are here
  docs/
    decisions.md            ← why SPIFFS, why not ESPHome, MCP notes
    flash-checklist.md      ← short operational checklist
  config/                   ← committed examples only
    SC_SecConfig.yaml.example
    SC_ExConfig.yaml.example
    SC_BasicConfig.yaml.example
  patches/
    0001-cores3-spiffs-config-fallback.patch
  scripts/
    bootstrap.sh            ← clone upstream, apply patch
    apply-local-config.sh   ← copy local secrets → firmware/data
    flash.sh                ← build + upload firmware + SPIFFS
  local/                    ← gitignored (create yourself)
    SC_SecConfig.yaml       ← real Wi‑Fi + API key
  upstream/                 ← gitignored clone of AI_StackChan_Ex
```

---

## One-time setup

### 1. Bootstrap upstream + patch

```bash
cd experiments/01-ai-stackchan-ex-realtime
./scripts/bootstrap.sh
```

This clones AI_StackChan_Ex into `upstream/` (if missing) and applies `patches/0001-cores3-spiffs-config-fallback.patch`.

### 2. Create local secrets (never commit)

```bash
mkdir -p local
cp config/SC_SecConfig.yaml.example local/SC_SecConfig.yaml
# edit local/SC_SecConfig.yaml — Wi‑Fi SSID/password + OpenAI key
```

Or inject OpenAI from Doppler (example):

```bash
OPENAI_KEY="$(doppler secrets get OPENAI_API_KEY --plain --project os --config dev_jonas)"
# paste into local/SC_SecConfig.yaml apikey.aiservice
```

Also copy non-secret app/servo examples if you want to customize:

```bash
cp config/SC_ExConfig.yaml.example local/SC_ExConfig.yaml
cp config/SC_BasicConfig.yaml.example local/SC_BasicConfig.yaml
```

### 3. Apply config into the build tree + flash

```bash
./scripts/apply-local-config.sh
./scripts/flash.sh
```

`flash.sh` runs:

- `pio run -e m5stack-cores3-realtime`
- `pio run -e m5stack-cores3-realtime -t upload`
- `pio run -e m5stack-cores3-realtime -t uploadfs`

### 4. Serial monitor (first boot)

```bash
cd upstream/firmware
pio device monitor -e m5stack-cores3-realtime
```

Look for:

- `SD not found; config loaded from SPIFFS.`
- Wi‑Fi IP
- Avatar bubble → **Please touch**

### 5. Talk

1. Wait until bubble says **Please touch** (not stuck on Connecting…).
2. Touch near the **forehead** (top of the LCD) → **Listening…**
3. Speak; Realtime answers with audio.
4. Touch forehead again to stop; ~30s idle also ends the session.
5. Touch **center** of screen to pause/resume servos.

---

## MCP (later)

Upstream MCP client expects **legacy SSE** transport:

- `GET http://<host>:<port>/sse`
- JSON-RPC POSTs to the message URL from the stream

Use your Mac’s **LAN IP** in YAML (`url`), never `127.0.0.1` (that is the robot). Stdio-only MCP servers can be bridged with [Supergateway](https://github.com/supercorp-ai/supergateway):

```bash
npx -y supergateway --stdio "your-mcp-command" --port 8000
```

Enable servers under `llm.mcpServers` in `SC_ExConfig` (see example). First bring-up left MCP **empty** on purpose.

Details: [docs/decisions.md](./docs/decisions.md).

---

## Patch summary

`patches/0001-cores3-spiffs-config-fallback.patch` changes `firmware/src/main.cpp`:

- **Before:** CoreS3 only loads YAML from SD; failure → reboot loop.
- **After:** try SD; if missing, load flat SPIFFS paths (`/SC_*.yaml`) like AtomS3R.

SPIFFS file layout (via PlatformIO `firmware/data/`):

| SPIFFS path | Role |
|-------------|------|
| `/SC_SecConfig.yaml` | Wi‑Fi + API keys |
| `/SC_ExConfig.yaml` | LLM / Realtime / MCP |
| `/SC_BasicConfig.yaml` | Servos (M5_SCS for official StackChan) |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Reboot every ~5s / failed settings | `uploadfs` not run, or empty `firmware/data` |
| No `/dev/cu.usb*` | Wrong/cheap cable; try another USB‑C data cable |
| Stuck on Connecting… | Bad OpenAI key, no internet, or Wi‑Fi wrong |
| Upload fails | Hold reset on CoreS3 as upload starts; close other serial monitors |
| MCP fails (later) | Firewall, wrong LAN IP, non-SSE transport |

---

## Upstream credits

- [AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) by ronron-gh (Realtime API, MCP, stackchan-arduino)
- [stack-chan](https://github.com/stack-chan/stack-chan) original project
- [M5Stack StackChan](https://docs.m5stack.com/en/StackChan) hardware
