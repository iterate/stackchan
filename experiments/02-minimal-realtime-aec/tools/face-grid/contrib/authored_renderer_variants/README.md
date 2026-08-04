# Authored renderer variants

An isolated candidate pack containing nine deterministic renderers derived
from three deliberately different visual lineages:

| Rows | Profiles |
|---|---|
| 1–3 | Appeal Scout, Clockwork Puppet, Manga Spark |
| 4–6 | Newsroom Editor, Cabaret Mime, Blueprint Companion |
| 7–9 | Command Deck, Nebula Dome, Solar Rogue |

Each profile consumes the firmware's 40-byte `face_render_key_t` and writes a
complete 160×120 RGB565 frame. The implementation is integer-only,
allocation-free, deterministic across native and WebAssembly builds, and
intentionally not registered in the product renderer table yet.

## Review

```sh
make test
make sheets
make asan ubsan strict wasm-verify
```

The generated sheets use profiles as rows. The expression sheet has the
eleven stage-expression fixtures as columns, the viseme sheet has the OVR15
mouth bank as columns, and the temporal sheet has 24 consecutive
speech/blink frames as columns. Files containing `exact40` show the actual
40×30 contact resolution enlarged with nearest-neighbour scaling for review.

The `preview/` PNGs are the current visual-review artifacts. The native and
exact-contact sheets are both retained so a profile cannot hide weak
silhouettes behind high-resolution detail.
