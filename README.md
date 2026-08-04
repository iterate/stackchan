# StackChan experiments

Public notes and tooling for experiments with the [M5Stack StackChan](https://docs.m5stack.com/en/StackChan) desktop robot.

Each experiment lives in its own folder under [`experiments/`](./experiments/). Nothing here should contain Wi‑Fi passwords, API keys, or other secrets. Secrets stay in gitignored `local/` dirs on your machine (or Doppler).

## Experiments

| # | Folder | Status | Summary |
|---|--------|--------|---------|
| 01 | [`experiments/01-ai-stackchan-ex-realtime`](./experiments/01-ai-stackchan-ex-realtime) | Done (poor UX) | [AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) WS+PCM — half-duplex, no AEC; lessons only |
| 02 | [`experiments/02-minimal-realtime-aec`](./experiments/02-minimal-realtime-aec) | Firmware + host rig | **Continuous mic + AEC + configurable Grok/OpenAI Realtime**, device observability, and a playout-synchronous PCM face |

## Hardware baseline

- **Device:** Official M5Stack StackChan (CoreS3 + serial servos)
- **Cable:** USB‑C data cable, Mac ↔ CoreS3 (for flash + serial only)
- **Not ESPHome:** these firmwares are mostly PlatformIO / Arduino (or other stacks per experiment)

## Repo conventions

```text
experiments/
  NN-short-name/
    README.md          # human walkthrough for this experiment
    docs/              # deeper notes, decisions, gotchas
    config/            # example YAML / env templates (no secrets)
    patches/           # diffs against upstream, if any
    scripts/           # clone / apply / flash helpers
    local/             # gitignored: real secrets, private notes
    upstream/          # gitignored: cloned upstream sources + build tree
```

## Safety

- Do **not** commit files under `local/` or real `SC_SecConfig.yaml` values.
- Prefer Doppler (or similar) for API keys; inject at flash time via scripts.
- Public repo: assume every committed byte is world-readable.
