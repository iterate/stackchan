# 02 — Minimal full-duplex OpenAI Realtime + AEC (StackChan)

**Goal:** Smallest *good* client: **mic stays open always**, **speaker reference → AEC**, stream cleaned audio to OpenAI Realtime, play response, **barge-in** works.

**Status:** Research complete; build target is adapting Espressif’s official demo + CoreS3 board config — not rewriting AI_StackChan_Ex.

---

## What you actually want (clarified)

```text
  mic ──► AEC ──► cleaned PCM ──► OpenAI Realtime ──► agent audio ──► speaker
           ▲                                              │
           └──────── speaker reference (echo cancel) ─────┘
```

- **No** permanent mic mute while speaking  
- **Yes** continuous uplink  
- **Yes** AEC so the model doesn’t hear itself (enables barge-in)

---

## GitHub trawl: what already exists

| Project | OpenAI Realtime? | Continuous mic? | AEC? | Notes |
|---------|------------------|-----------------|------|--------|
| **[espressif/esp-webrtc-solution → openai_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/openai_demo)** | **Yes (WebRTC GA)** | **Yes** | **Yes** (`esp_capture_new_audio_aec_src`) | **Best existing client.** Opus, AEC, Korvo-2 default; board configs via `codec_board`. |
| [openai/openai-realtime-embedded](https://github.com/openai/openai-realtime-embedded) | Links only | — | — | Points at Espressif WebRTC demo (not a full Arduino client). |
| [akdeb/ElatoAI](https://github.com/akdeb/ElatoAI) | Yes (via Deno edge, WS+Opus) | Partial (listen/speak modes) | **No** | Arduino-friendly; **no AEC**; not pure client-side to OpenAI. |
| [ronron-gh/AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) | Yes (WS+base64 PCM) | Half-duplex mute | **No** | Experiment 01 — poor audio path. |
| [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) | No (XiaoZhi protocol) | Yes + barge-in work | Software AEC (custom/ESP-SR) | Great voice UX, **not** OpenAI Realtime. |
| [rjsachse/ESP32-SpeexDSP](https://github.com/rjsachse/ESP32-SpeexDSP) | No | — | Speex AEC | Library only; glue still needed. |

**Conclusion:** Nobody has a polished “StackChan + OpenAI Realtime + AEC” product firmware.  
The **closest, already-correct architecture** is **Espressif `openai_demo`** (WebRTC + AEC). StackChan’s CoreS3 even uses the same **ES7210** dual-mic codec that demo is designed around (TDM + reference channel pattern).

WebSocket+base64-PCM DIY on Arduino will stay worse than this WebRTC path. ChatGPT.com also uses WebRTC.

---

## Hardware reality (StackChan / CoreS3)

| Piece | Role |
|--------|------|
| ES7210 | Dual mic ADC — **supports AEC reference pattern** (same family as Korvo) |
| AW88298 | Speaker amp (not ES8311 — need board profile mapping) |
| ESP32-S3 | Runs AEC in **software** (no separate AEC chip) |

There is **no** dedicated AEC silicon. AEC = ESP capture AEC / ESP-SR AFE / Speex.

---

## Implementation plan (this experiment)

1. Vendor / submodule [esp-webrtc-solution](https://github.com/espressif/esp-webrtc-solution) `openai_demo`.
2. Add **`M5Stack_CoreS3`** (and later StackChan servo hooks) to `codec_board/board_cfg.txt` with CoreS3 I2C/I2S pins.
3. Map **AW88298** as playback (or use existing codec path if BSP already covers CoreS3).
4. Secrets: Wi‑Fi + `OPENAI_API_KEY` from env / local (never commit).
5. Optional later: face avatar, servos — **not** in the minimal voice loop.

### Pins (CoreS3, from M5 docs)

| Function | GPIO |
|----------|------|
| I2C SDA / SCL | 12 / 11 |
| I2S BCK / WS / MCLK | 34 / 33 / 0 |
| ES7210 DIN | 14 |
| AW88298 DOUT | 13 |

---

## Why not “smallest WebSocket only”?

| | DIY WS+PCM (AI_StackChan_Ex style) | Espressif WebRTC openai_demo |
|--|-------------------------------------|------------------------------|
| Codec | PCM base64 in JSON | Opus over WebRTC |
| AEC | DIY Speex/ESP-SR glue | **Built-in** `audio_aec_src` |
| Barge-in | Easy to get wrong | Natural with continuous AEC’d capture |
| Match ChatGPT feel | Poor | Closest on ESP32 |

“Smallest good” ≠ fewest lines of bad PCM code.  
**Smallest good = lean on the maintained AEC+Realtime client and only add CoreS3/StackChan board support.**

---

## Docs

- [docs/github-survey.md](./docs/github-survey.md) — longer notes  
- [docs/architecture.md](./docs/architecture.md) — target data path  

## Next step

When ready to flash: check ESP-IDF install, add CoreS3 board section, build `openai_demo` for that board, smoke-test duplex barge-in on StackChan.
