# Cyber wildcard actors (legacy IDs 33, 38, 39)

This standalone module contains three original, heap-free RGB565 actors:

| Legacy ID | Slug | Visual contract |
| --- | --- | --- |
| 33 | `cyber-chladni-voiceplate` | Dark resonance instrument with fixed luminous sockets and a lower voice transducer |
| 38 | `cyber-teletext-performer` | Sparse retro CRT/game portrait built from stable pixel panels |
| 39 | `cyber-ferrofluid-familiar` | Magnetic tank with fixed pole pieces, liquid eye wells, and an attached lower mouth well |

The old effects-first versions failed at contact size: Chladni sand read as
starfield noise, the teletext mosaic hid the face, and ferrofluid spikes looked
detached. The current renderers keep the chassis and eye sockets fixed. Only
bounded eye apertures, pupils, brows, cheeks, and the attached mouth articulate.
There is no whole-face bob, random grain, raw VU-meter mouth, moving cursor, or
sample-clock shimmer.

## Acting and IR contract

- All 11 stage expressions alter facial geometry, not only palette.
- Speech phase drives anticipation, active eye/brow focus, and a controlled
  ending settle.
- OVR15 primary/secondary visemes, blend, raw mouth controls, audio level,
  teeth, and tongue remain parallel articulation tracks.
- `face_cyber_wildcard_pose_t` retains the complete 40-byte render key.
- A small, fixed two-row telemetry rail encodes its 32-bit FNV signature. This
  makes every IR byte observable without spraying checksum noise through the
  face.
- The same IR produces identical pixels at every `sample_clock`. The clock is
  still accepted for the common interface and event alignment, but it cannot
  create unauthored visual chatter.
- The render path is integer-only, stateless, and allocation-free.

## Native verification

From `firmware-ws`:

```sh
mkdir -p /tmp/face-cyber-wildcards

clang -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main main/face_cyber_wildcards.c \
  tests/face_cyber_wildcards_test.c \
  -o /tmp/face-cyber-wildcards/test

/tmp/face-cyber-wildcards/test

clang -std=c11 -O2 -Wall -Wextra -Werror \
  -I main main/face_cyber_wildcards.c \
  tests/face_cyber_wildcards_dump.c \
  -o /tmp/face-cyber-wildcards/dump

/tmp/face-cyber-wildcards/dump \
  tests/artifacts/face_cyber_wildcards
```

The sanitizer suite covers:

- 1,536 adversarial full-IR frames with canaries and border checks;
- deterministic rendering and bounded poses;
- an individual mutation of every one of the 40 IR bytes for every actor;
- geometry-led separability for all 11 emotions;
- native, 80×60, and exact 40×30 expression/viseme separability;
- fixed sockets, fixed exterior, no clipping, and bounded deltas over a
  16-frame idle → anticipation → active speech → settle sequence;
- zero pixel changes when only `sample_clock` changes;
- strong pairwise separation between the three visual languages.

## Review sheets

The dump writes native RGB565-derived PPM sheets and exact box-filtered contact
sheets:

- `all-expressions-{160x120,80x60,40x30}.ppm`
- `all-visemes-{160x120,80x60,40x30}.ppm`
- `speech-16f-{160x120,80x60,40x30}.ppm`

Rows are Chladni, teletext, then ferrofluid. Expression columns are neutral,
warm, joy, concern, surprise, thoughtful, skeptical, determined, sleepy,
excited, and embarrassed. Speech columns are chronological at 533 samples per
frame.

## Remaining weaknesses

- The Chladni actor now favors readable instrument-face acting over a literal
  particle simulation; its resonance identity is deliberately abstract.
- Several consonant visemes converge at 40×30, although every non-silent
  viseme still differs from silence and all 15 remain distinct natively.
- Teletext brow diagonals necessarily quantize at 40×30.
- Ferrofluid is a procedural magnetic-well design, not a fluid simulation.
- The telemetry rail is intentionally visible and changes when the IR changes;
  it is confined below the performance area so it cannot destabilize sockets
  or mouth geometry.

This contribution deliberately does not change the dispatcher, firmware build,
WASM wrapper, or website integration.
