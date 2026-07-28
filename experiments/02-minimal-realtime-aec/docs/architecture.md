# Architecture — continuous stream + AEC

## Target path

```text
┌─────────────────────────────────────────────────────────────┐
│ StackChan (CoreS3)                                          │
│                                                             │
│  ES7210 dual mic ──┐                                        │
│                    ├─► AEC (esp_capture audio_aec_src)      │
│  speaker ref ──────┘         │                              │
│                              ▼                              │
│                     cleaned mono frames                     │
│                              │                              │
│                              ▼                              │
│              OpenAI Realtime (WebRTC / Opus)                │
│                              │                              │
│                              ▼                              │
│                     AW88298 speaker                         │
└─────────────────────────────────────────────────────────────┘
```

## Barge-in

1. Mic path **never stops** for “agent talking.”  
2. AEC removes speaker energy so VAD doesn’t treat TTS as user speech.  
3. When user speech is detected upstream, stop local playout and let the session interrupt (WebRTC media path handles this more cleanly than DIY WS).

## Why WebRTC not raw WebSocket PCM

- OpenAI’s high-quality clients use WebRTC.  
- Espressif’s AEC path is already wired into their WebRTC media stack.  
- Base64 PCM over text WebSocket on ESP32 reintroduces the jitter/blocking bugs from experiment 01.

## Secrets

- `OPENAI_API_KEY` via build env / local only  
- Wi‑Fi in `settings.h` or NVS — never public git  

## Non-goals for v1

- MCP tools  
- Avatar polish  
- Servo choreography (add after voice is solid)
