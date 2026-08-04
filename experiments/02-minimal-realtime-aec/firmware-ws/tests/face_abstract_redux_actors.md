# Abstract redux actors 30, 31, 32, 34, and 37

`face_abstract_redux.{h,c}` provides five integrated abstract actors while
retaining their legacy profile IDs:

| ID | Actor | Acting identity |
|---:|---|---|
| 30 | Neon Ribbon Performer | Paired neon eyes, ribbon mouth, connected voice meters |
| 31 | Liquid Droplet Familiar | Stable droplet silhouette, elastic eyes and soft mouth |
| 32 | CRT Phosphor Puppet | Fixed console, phosphor lids, brows, and matrix mouth |
| 34 | Voice Orbit Familiar | Single eye, independent brow signals, expanding voice rings |
| 37 | Edge-Light Sentinel | Fixed trapezoid mask, dual optics, faceted mouth |

## Second-pass contract

- The renderer remains integer-only, deterministic, heap-free, and writes an
  exact 160×120 RGB565 frame.
- The complete 40-byte `face_render_key_t` is copied into the resolved pose
  and included in its FNV-1a input signature.
- Face, eye, and mouth centers are fixed pose anchors. Head and body controls
  affect local gaze, brows, and authored deformation, not whole-face
  translation.
- Every non-neutral expression changes at least two eye, brow, or gaze
  channels and remains distinct from the other ten at 40×30.
- Speech geometry is driven by an integer smoothstep envelope derived from
  viseme and mouth controls. Raw `audio_level` remains available in the source
  record but does not jitter geometry or status lights.
- Starting speech raises the eyes and brows before the mouth reaches its large
  beat. Ending speech reduces aperture over an eased, asymmetric settle.
- Liquid and Sentinel rescale the shared eye aperture into their smaller
  sockets instead of clamping most open-eye emotions to one shape.
- Voice Orbit keeps its mouthless identity: phoneme energy expands the ring
  and ticks while two separate brow signals preserve asymmetric emotions.
- Gradient-backed actors seed the complete frame before drawing the protected
  border, preventing stale caller pixels.

## Verification

Run:

```sh
firmware-ws/tests/run_face_abstract_redux_actors_native.sh
```

The isolated runner builds `face_abstract_redux.c`, the production stage-cue
resolver used by its motion regression, and the focused test/dump/benchmark
programs. It performs strict warnings-as-errors builds, 256 full-record
adversarial cases with frame canaries, all-emotion contact-scale distinctness
checks, fixed-anchor and temporal acting assertions, the integrated 180-frame
motion-discontinuity gate, raw-audio invariance checks, separate ASan and UBSan
runs, an allocator-symbol check, artifact generation, and a host benchmark.

## Review artifacts

- `abstract-redux-expressions-native-5x11.png`: five actors by eleven
  emotions, with exact 160×120 tiles.
- `abstract-redux-expressions-40x30-5x11.png`: exact nearest-neighbour 40×30
  versions of those tiles.
- `abstract-redux-temporal-native-5x16.png`: rest, two anticipation frames,
  three held three-frame phoneme sections, three settle frames, and release.
- `abstract-redux-temporal-40x30-5x16.png`: exact 40×30 versions of the same
  timeline.
- `before-second-pass-expressions-native-5x11.png` and
  `before-second-pass-expressions-40x30-5x11.png`: exact crops from the
  pre-change integrated expression sheets.

The artifact directory retains PPM sources for regenerated sheets and PNG
inspection copies. No labels or separators are present inside the native or
40×30 tiles.
