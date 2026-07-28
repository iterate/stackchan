# Why AI_StackChan_Ex Realtime feels bad

Honest assessment from the first bring-up (2026-07-28).

## Compared to ChatGPT website voice

ChatGPT.com / the mobile app use **WebRTC** to the Realtime API: proper media pipeline, jitter buffering, AEC, codec negotiation. That is the “good” path OpenAI optimised for.

AI_StackChan_Ex uses **raw WebSocket + base64 PCM16** on an **ESP32-S3**:

| Factor | ChatGPT website | AI_StackChan_Ex on StackChan |
|--------|-----------------|------------------------------|
| Transport | WebRTC | WebSocket + TLS |
| Audio encoding | Opus (media) | PCM16 base64 in JSON text frames |
| Device | Phone/PC | ESP32-S3 ~240 MHz + Wi‑Fi |
| Mic/spk | Full duplex + AEC | Half-duplex: mic off while speaker plays |
| Playback | OS/browser jitter buffer | Manual double buffer + `while (Speaker.isPlaying())` **inside the WebSocket callback** |

So yes: website voice will feel far better. That is not your imagination.

## Concrete bugs / design smells in this firmware

1. **Sample rate mismatch (upstream)**  
   Mic recorded at **16 kHz** (`RT_REC_SAMPLE_RATE`) while the Realtime session advertises **24 kHz** PCM. Model hears wrong-speed audio → bad ASR and weird turns.

2. **Playback blocks the WebSocket task**  
   `streamAudioDelta()` waits for the speaker to finish **before** the event loop can process the next frames. That causes jitter, stalls, and “thinking / interrupting itself” vibes.

3. **Huge Serial logging on every audio delta**  
   Logging base64 sizes / payload sizes on large frames stalls the same loop that must keep audio moving.

4. **Tool dump on every session**  
   Dozens of function-calling tools (alarms, volume, Japanese descriptions…) inflate every session and invite the model to tool-call instead of just talking.

5. **Half-duplex audio**  
   Mic is ended while speaking, then restarted after `response.done`. Not continuous duplex like WebRTC.

6. **Japanese default role**  
   Unrelated to quality, but confusing on first boot.

## Is there a better StackChan + OpenAI Realtime firmware?

**Short answer: not really — AI_StackChan_Ex is basically the only open StackChan project that talks OpenAI Realtime over WebSocket.** There is no polished “StackChan ↔ gpt-realtime WebRTC” community firmware as of this write-up.

### What exists instead

| Project | OpenAI Realtime? | What it actually is | Voice quality expectation |
|---------|------------------|---------------------|---------------------------|
| **[AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex)** (this experiment) | Yes (WebSocket PCM) | Hobby Arduino/PlatformIO stack | Mediocre on device (see above) |
| **[m5stack/StackChan](https://github.com/m5stack/StackChan) factory / AI Agent** | No (XiaoZhi protocol) | Official product firmware + app | Often *smoother* on-device; backend is XiaoZhi cloud (not OpenAI RT) |
| **[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)** family | No | ESP32 voice-assistant protocol used by stock StackChan | Purpose-built for MCU voice; can self-host |
| **[rebelthor/warble](https://github.com/rebelthor/warble)** | No | Local STT+LLM+TTS backend **compatible with stock StackChan** | Can be good if Mac is powerful; not OpenAI Realtime |
| **[BrettKinny/dotty-stackchan](https://github.com/BrettKinny/dotty-stackchan)** | No | Self-hosted xiaozhi-esp32-server + local brain | Same family as warble |
| **M5 ESPHome / “voice assistant” burns** | Usually Home Assistant pipeline | Different stack entirely | HA assist quality, not ChatGPT Realtime |
| **Proxy architecture (recommended if you want OpenAI quality)** | Yes (WebRTC on Mac) | Mac/browser holds Realtime; robot is thin audio/servo client | Can approach website quality |

## Practical recommendation

If the goal is **“sounds like ChatGPT Realtime”**:

1. **Do not expect AI_StackChan_Ex to match the website** on ESP32 WebSocket PCM.
2. Prefer either:
   - **Factory StackChan AI Agent** (XiaoZhi) for a usable desk robot, or  
   - **Mac-side OpenAI Realtime (WebRTC)** + robot as a dumb speaker/mic/servo shell (new experiment).
3. If staying on AI_StackChan_Ex, the only hope is deep surgery: 24 kHz I/O, zero logging in the audio path, non-blocking playout, no tools, server VAD — still won’t match WebRTC.

## Related patches in this experiment

- `0001` — SPIFFS config without microSD  
- `0002` — English default role  
- Further audio fixes (sample rate / logging) may land as `0003` if we keep iterating this stack
