# compat/ — verbatim firmware ABI copies

Unmodified copies of first-party project files, taken from
`firmware-ws/main/` on 2026-07-29 so the pack builds and self-tests
standalone:

- `face_pose.h`, `face_keyframe.h` — the 40-byte `face_render_key_t` IR
- `face_stage.h`, `face_stage.c` — stage cues; the test suite applies
  cues through the *real* implementation, so the quality mirror cannot
  drift from production semantics

Point `ABI_INC=../../../../firmware-ws/main` at the live tree to build
against current headers instead (`STAGE_SRC` overrides the stage
implementation the tests compile). At integration time these copies
are discarded — production files win. Re-diff before collecting: the
firmware tree was being edited while this pack was written.
