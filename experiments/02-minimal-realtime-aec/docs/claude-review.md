# Review: Experiment 02 — Minimal OpenAI Realtime + AEC on CoreS3

Date: 2026-07-28

---

## 1. Architecture verdict: keep WebRTC demo

**Keep it.** The Espressif `openai_demo` is the right foundation. WebRTC + Opus + esp_capture AEC pipeline is architecturally correct. Every alternative (WS+base64 PCM, Arduino WS+Speex DIY, ElatoAI edge proxy) is strictly worse for full-duplex voice. The board.c / media_sys.c adaptation approach is sound.

The problem isn't the architecture choice — it's that a critical assumption was violated during the CoreS3 port. See below.

---

## 2. AEC on CoreS3 mono I2S: currently broken, fixable with software reference

### What the Korvo does (and why it works)

```
ES8311 DAC + ES7210 ADC share one I2S bus in TDM mode (4 slots)
┌──────────────────────────────────────────┐
│ TDM slot 0: mic ch0                       │
│ TDM slot 1: speaker loopback (reference)  │
│ TDM slot 2: mic ch1                       │
│ TDM slot 3: unused                        │
└──────────────────────────────────────────┘
Original code: channel=4, channel_mask=1|2 → reads [mic, ref]
AFE mic_layout defaults to "MR" → feed() gets interleaved mic+reference
AEC subtracts reference from mic → clean audio out
```

### What CoreS3 actually provides

```
ES7210 ADC on I2S DIN (GPIO 14)  — mic only, STD mono
AW88298 amp on I2S DOUT (GPIO 13) — speaker only, no loopback
BSP configures STD I2S, 1 channel.
```

AW88298 is a Class-D amplifier. Unlike ES8311 (a full codec with ADC+DAC), it has **no ADC path** and **cannot loop back** speaker audio onto the I2S bus. There is no reference channel on the wire.

### What the current code does (and why it's wrong)

`media_sys.c:54-58`:
```c
esp_capture_audio_aec_src_cfg_t aec_cfg = {
    .record_handle = board_record_handle(),
    .channel = 1,
    .channel_mask = 1,
    // mic_layout not set → defaults to "MR" (mic + reference)
};
```

`capture_audio_aec_src.c:679`: when `mic_layout` is NULL, defaults to `"MR"`.

`capture_audio_aec_src.c:99`: `afe_config_init("MR", ...)` configures the AFE for 2-channel interleaved input (1 mic + 1 reference).

`capture_audio_aec_src.c:527`: `cache_size = get_feed_chunksize() * 2` (bytes per channel).

`capture_audio_aec_src.c:386-402`: `read_size = cache_size * get_src_channel(src)` = `cache_size * 1`. Feeds a buffer of `chunksize` samples to an AFE expecting `chunksize * 2` samples.

**Result: buffer overread.** The AFE's `feed()` reads past the allocated buffer. If it doesn't crash, it reads garbage as "reference" — AEC output is undefined.

Even if the AEC source creation succeeds (doesn't return NULL), the echo cancellation is non-functional. The fallback to `esp_capture_new_audio_dev_src` (plain mic, no AEC) is the safe behavior but provides zero echo cancellation.

### How to actually feed the reference

**Option A — Software reference loopback (recommended):**

1. Tap the PCM being sent to AW88298 in the av_render output path
2. Write it to a shared ringbuffer accessible from the capture thread
3. In the capture feed loop, read mic from ES7210 AND reference from ringbuffer
4. Interleave into a 2-channel buffer: `[mic_sample, ref_sample, ...]`
5. Feed to AFE with `mic_layout = "MR"`, `channel = 2`

This is how xiaozhi-esp32 implements AEC on boards without hardware loopback. Requires modifying `capture_audio_aec_src.c` or writing a custom audio source wrapper that composes the two streams before feeding the AFE.

The `av_render` already decodes speaker audio — the data exists in `build_player_system()`. You need a hook point where PCM frames pass through before `esp_codec_dev_write`.

**Option B — Minimal: mic_layout = "M", no AEC:**

```c
.mic_layout = "M",
.channel = 1,
.channel_mask = 1,
```

AFE runs noise suppression + VAD on a single mic channel. No echo cancellation. Barge-in depends entirely on OpenAI's server-side VAD, which can't subtract the speaker signal it doesn't know about. Functional for conversation, poor for barge-in.

**Option C — Use `AFE_TYPE_VC` or `AFE_TYPE_FD` with Option A:**

The current code uses `AFE_TYPE_SR` (speech recognition). For full-duplex voice communication, `AFE_TYPE_VC` (voice communication, 16 kHz) or `AFE_TYPE_FD` (full duplex, includes nonlinear noise suppression) would be more appropriate. This is an additional improvement on top of fixing the reference feed.

---

## 3. Top 5 code/config fixes (ranked by impact)

### Fix 1: Feed speaker reference to AEC (CRITICAL)

Without this, AEC is non-functional. See Option A above. Minimum viable change:

1. Add a shared ringbuffer (`esp_gmf_data_queue` or simple `ringbuf_handle_t`)
2. In `build_player_system()`, hook the render output path to copy PCM to the ringbuffer
3. In `build_capture_system()`, create a custom source that reads mic from `board_record_handle()` and reference from the ringbuffer, interleaves them, and wraps the existing `esp_capture_new_audio_aec_src` with `channel = 2`, `mic_layout = "MR"`

Until this is done, set `mic_layout = "M"` to at least avoid the buffer overread.

### Fix 2: Remove hardcoded WiFi credentials from settings.h

`settings.h:14-15` commits WiFi SSID and password to git:
```c
#define WIFI_SSID "mispwoso2"
#define WIFI_PASSWORD "thanksforallthefish"
```

Move to build env vars (`-DWIFI_SSID="$ENV{WIFI_SSID}"` in CMakeLists.txt) or NVS provisioning. Same pattern as `OPENAI_API_KEY` already uses.

### Fix 3: Restore PSRAM cache configuration

The original Korvo sdkconfig had performance-critical cache settings:

```
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
```

These were removed in the CoreS3 port. CoreS3 has Quad PSRAM at 80 MHz (slower than Korvo's Octal at 120 MHz), making large caches even more important. Without them the build falls back to 16 KB icache / 32 KB dcache / 32B lines, hammering PSRAM bandwidth during AEC processing and Opus encode/decode.

Add to `sdkconfig.defaults.esp32s3`:
```
CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y
CONFIG_ESP32S3_DATA_CACHE_64KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
```

### Fix 4: Fix audio_threshold dead code

`media_sys.c:107-108`:
```c
uint32_t audio_threshold = 0;
audio_threshold *= aud_info.sample_rate * ...  // 0 * anything = 0
```

This was broken in upstream too, but the comment says "Buffer 100ms data." If you want 100ms buffering:

```c
uint32_t audio_threshold = 100;
audio_threshold = audio_threshold * aud_info.sample_rate * aud_info.channel * (aud_info.bits_per_sample >> 3) / 1000;
```

For 16kHz/mono/16-bit: 100ms = 3200 bytes. This smooths network jitter and prevents audio glitches.

### Fix 5: Delete dead sdkconfig.defaults.cores3

`sdkconfig.defaults.cores3` is never loaded by ESP-IDF (the target is `esp32s3`, not `cores3`). Its content is already in `sdkconfig.defaults.esp32s3`. Remove to avoid confusion.

---

## 4. Simplest path to excellent barge-in

**Phase 1 — Get AEC working (days, not weeks):**

1. Implement software reference loopback (Fix 1 above)
2. Set `AFE_TYPE_VC` with `aec_nlp_level = AEC_NLP_LEVEL_AGGR` for aggressive echo suppression
3. Test with `rec2play` CLI command — record while speaker is playing, verify echo is cancelled

**Phase 2 — Tune the pipeline:**

4. Enable `data_on_vad = true` in `aec_cfg` — sends audio only during detected speech, reducing bandwidth and giving OpenAI cleaner input for its own VAD
5. Increase `audio_render_fifo_size` to handle network jitter (current 100KB is fine)
6. Verify Opus sample rate matches AFE output (both should be 16 kHz mono, which they are)

**Phase 3 — Polish:**

7. On barge-in detection (user speaks while agent is talking), flush the render buffer: `av_render_reset(player_sys.player)` to stop playout immediately rather than waiting for the buffer to drain
8. Consider sending `response.cancel` via data channel when local VAD detects speech during agent output — this tells OpenAI to stop generating, reducing the echo window

**What NOT to do:**
- Don't mute the mic while speaking — that's the AI_StackChan_Ex half-duplex antipattern
- Don't try to build your own AEC with Speex — esp-sr's AFE is hardware-optimized for S3
- Don't switch to WebSocket+PCM — you'd lose the entire AEC pipeline integration

---

## 5. Better / smaller references

| Reference | Why it's useful |
|-----------|-----------------|
| [78/xiaozhi-esp32 `audio_processor.c`](https://github.com/78/xiaozhi-esp32) | Best open example of software AEC reference loopback on ESP32-S3 without hardware TDM. They copy playback PCM into a ringbuffer and feed it alongside mic data to the AFE. Study their `AudioProcessor` class. |
| [espressif/esp-sr AFE examples](https://github.com/espressif/esp-sr/tree/master/examples) | Official examples showing `afe_config_init()` with various `mic_layout` strings and channel configs. The `voice_interaction` example shows software reference feeding. |
| [espressif/esp-adf `pipeline_record_and_playback`](https://github.com/espressif/esp-adf/tree/master/examples/recorder) | Shows how to tap the audio pipeline for reference data using `audio_element` ringbuffers. Different framework (ADF vs GMF) but same pattern. |
| CoreS3 BSP source: `bsp_audio_codec_speaker_init()` | Check exactly how the BSP configures I2S — verify whether `esp_codec_dev_write()` can be intercepted or if you need to hook at the I2S driver level. |
| ESP-IDF I2S full-duplex example | If you end up needing to reconfigure I2S yourself (bypassing BSP), this shows proper full-duplex DMA setup on S3. |

---

## Summary

The architecture choice (adapt `openai_demo`) is correct. The CoreS3 port has one critical bug: **AEC receives no speaker reference signal**, making echo cancellation non-functional and causing a likely buffer overread. The fix is software reference loopback — copy playback PCM to a ringbuffer and interleave with mic data before feeding the AFE. This is a known pattern (xiaozhi-esp32 does it). Until that's implemented, at minimum set `mic_layout = "M"` to avoid the buffer overread, accepting no echo cancellation.

Secondary fixes: remove hardcoded WiFi creds, restore PSRAM cache config, fix the audio_threshold dead code, delete the unused sdkconfig file.
