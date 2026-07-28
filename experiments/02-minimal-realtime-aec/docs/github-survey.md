# GitHub survey — ESP32 / M5 OpenAI Realtime + AEC

Date: 2026-07-28

## Search axes

- `openai realtime esp32`, `v1/realtime`, `interrupt_response`
- `esp_capture_new_audio_aec_src`, ESP-SR AFE
- M5Stack / StackChan / CoreS3 voice firmwares
- ElatoAI, openai-realtime-embedded, xiaozhi-esp32

## Tier A — do this (correct architecture)

### espressif/esp-webrtc-solution `solutions/openai_demo`

- **URL:** https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/openai_demo  
- **Transport:** OpenAI Realtime **WebRTC** (GA: `client_secrets` + `realtime/calls`)  
- **AEC:** Yes — `esp_capture_new_audio_aec_src` in `media_sys.c`  
- **ES7210:** Explicitly documented for S3 TDM + reference channels  
- **Default board:** ESP32-S3-Korvo-2  
- **Board system:** `codec_board` + `board_cfg.txt` (many boards; **no CoreS3 yet**, but **ATOMS3_ECHO_BASE** exists)  
- **API key:** env `OPENAI_API_KEY`  
- **Why it wins:** Maintained by Espressif; AEC + continuous capture + Opus; same problem class as “ChatGPT-quality on a chip.”

### openai/openai-realtime-embedded

- Meta-repo that **points to** the Espressif WebRTC demo. Not a separate full client.

## Tier B — useful pieces, incomplete for our goal

### akdeb/ElatoAI

- ESP32-S3 Arduino + WebSocket **to Deno edge** + Opus  
- No on-device AEC; listen/speak state machine; edge proxy required  
- Good reference for Arduino audio tasks / Opus, **not** for pure OpenAI AEC client

### rjsachse/ESP32-SpeexDSP

- Speex AEC/NS library for Arduino  
- Usable if we insist on tiny Arduino WS client  
- We still must build: I2S duplex, OpenAI session, playout queue, barge-in

### 78/xiaozhi-esp32 (+ StackChan factory)

- Excellent **barge-in / AEC culture** for ESP32 voice products  
- **Not** OpenAI Realtime (XiaoZhi protocol / cloud)  
- Steal ideas, not the cloud backend, if we want OpenAI

### ronron-gh/AI_StackChan_Ex

- Only well-known StackChan OpenAI Realtime WebSocket port  
- Half-duplex mute, sample-rate bugs, blocking playout — experiment 01  
- **Do not base 02 on this**

## Tier C — different product

- warble / dotty-stackchan: local STT/LLM/TTS for stock StackChan protocol  
- Home Assistant / ESPHome voice assistants  

## Recommendation

**Do not invent another base64-PCM WebSocket stack.**  

**Do:** fork/adapt `openai_demo` + add `M5Stack_CoreS3` codec_board entry (ES7210 + AW88298 pins) + keep servos/face as optional later layers.
