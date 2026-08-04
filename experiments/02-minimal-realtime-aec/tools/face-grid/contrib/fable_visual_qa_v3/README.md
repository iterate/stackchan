# fable_visual_qa_v3 — visual acceptance tooling + renderer audit

Worker-isolated contribution. Everything in this directory (and generated
artifacts under `local/fable_visual_qa_v3/`) is scratch until the primary
agent reviews it. No shared/production file is modified.

## Contents

- `fvqa_metrics.c/.h` — native, integer-only frame-analysis core (no
  allocation in the per-frame paths, C11, host-compiled). Pure functions
  over RGB888 frames: border-contact / edge-clip measurement,
  foreground mask + connected components (topology pops), region-split
  emotion response (dead-eye / mouth-corner checks), palette-normalized
  cross-profile similarity (clone detection), and box-downscale for
  contact-scale readability.
- `fvqa_probe.c` — acceptance probe binary. Links the production renderer
  sources read-only (`firmware-ws/main/*.c`), renders every registered
  profile through `face_render_frame()` + `face_stage_cue_apply()`, runs
  the metric core, and emits:
  - enhanced per-profile contact sheets (11 emotions at native res +
    contact-scale row + temporal storyboard strip + border-contact strip),
  - `acceptance.json` with per-profile advisory metrics,
  - clone-cluster report across profiles.
- `fvqa_test.c` — unit tests for every metric on synthetic frames.
- `make_pngs.py` — PPM→PNG mirror + `index.html` gallery for the emitted
  sheets (self-contained; no external Python deps).
- `split_atlas.py` — cut a probe expression atlas into reviewable PNG bands.
- `Makefile` — `make test`, `make run` (probe + PNG gallery), `make clean`.
- `REPORT.md` — renderer-by-renderer ranked audit (legacy enum IDs, defects,
  concrete geometry/timing fixes).

## Ground rules honored

- Output only in this directory and `local/fable_visual_qa_v3/`.
- Numeric metrics are ADVISORY. They flag suspects and rank work; they can
  never promote a renderer. Promotion requires human review of the actual
  expression sheets and temporal storyboards.
- No copyleft source consulted or pasted; metric implementations are
  first-principles.
