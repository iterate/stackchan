# Task: consolidating multi-device abstraction

**Status:** open  
**Priority:** design before more board-specific firmware  
**Context:** we already have a solid portable face/sprite core (PCM → keyframe IR → RGB565, host + WASM + ESP). We do **not** yet have the same discipline for boards, I/O, and voice modality.

## Problem

We own (or will target) several different form factors:

- M5 StackChan (CoreS3 + body LEDs / servos)
- M5 Stick S3–class (hold-to-speak button, tight display/UI)
- Waveshare ESP32-S3 Touch AMOLED 1.8 (rich screen + touch)
- Home Assistant Voice Preview Edition (no face screen, strong mics + HW AEC, LED ring)
- Web (authoring, remote face, test once)

Writing per-product `if (DEVICE)` trees will not scale. We need one core and thin board packs.

## Goal

Find and document a consolidating abstraction so we can:

1. Write core logic once  
2. Test core logic once (host / WASM)  
3. Ship the same session + presence + face IR to every target  

Board-specific code should stay limited to pins, codecs, display flush, and capability flags.

## Axes to cover (capability matrix, not product names)

| Axis | Example values |
|------|----------------|
| Display | none · small · medium · hi-dpi |
| Pixel format | none · RGB565 · RGB888 |
| Audio duplex | half · continuous no-AEC · soft AEC · HW/offloaded AEC |
| Mic topology | none · mono · dual · array |
| Speaker reference | none · software tap · HW loopback |
| Talk modality | open-mic · PTT-hold · PTT-toggle · wake-word · server-VAD |
| Buttons / touch | none · N buttons · touch regions · free 2D · encoder |
| Indicators | none · single LED · strip · ring |
| Actuators | none · servos |
| CPU / memory class | S3-tight · S3-8MB · host/web · XMOS-offload |

## Semantic surfaces (sketch — not decided)

- **Intents in:** `talk.start/end`, mute, volume, interrupt — from button, touch, VAD, wake-word, web  
- **Presence out:** phase + level + affect → face renderer **and/or** LED ring/strip **and/or** web  
- **Audio backends:** capture class + AEC class; face always clocked from playout PCM when present  
- **Packages:** face atlas per resolution; presence patterns for screenless; board.yml for pins only  

PTT on Stick and open-mic on Voice PE should be the **same session** with a different talk-modality gate, not two apps.

## Likely shippable profiles (collapse permutations)

| Role | Example hardware | Avatar | Talk |
|------|------------------|--------|------|
| Room ear | HA Voice PE | LED ring | open-mic + HW AEC |
| Pocket mic | Stick S3 | LED / mini face | PTT-hold |
| Desk face | Waveshare AMOLED | rich face | touch-PTT or open |
| Robot | StackChan | face + body LEDs + servos | open-mic + soft AEC |
| Web twin | browser | same face package | open or PTT |

## Already done (do not reinvent)

- Portable face drivers and 40-byte IR  
- Sprite/avatar pipeline intent (`sprite-avatar-pipeline.md`)  
- Host + WASM parity tests for face C  

## Still to invent

- [ ] Capability / board profile schema (`board.yml` or equivalent)  
- [ ] Intent + presence APIs that core depends on (no GPIO in core)  
- [ ] Audio backend interface (none / soft AEC / offloaded clean PCM)  
- [ ] How screenless targets project stage IR onto LED rings  
- [ ] Repo layout: `core/` vs `ports/esp-idf/boards/…` vs face/presence packages  
- [ ] Explicit non-goals (e.g. ESP32 classic, full Zephyr port)  
- [ ] Fill real flash/RAM/mic/AEC facts per SKU once hardware is in hand  

## Related docs

- [architecture.md](./architecture.md)  
- [sprite-avatar-pipeline.md](./sprite-avatar-pipeline.md)  
- [pcm-face-rig.md](./pcm-face-rig.md)  
- [wasm-face-grid.md](./wasm-face-grid.md)  

## Note

This is a **task marker**, not the finished design. Next step when someone picks it up: propose the profile schema and one worked example for Voice PE (no screen) vs CoreS3 (face), without porting full firmware yet.
