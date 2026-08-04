# Legacy mouth studies 23–28: standalone redux

`face_mouth_study_redux.{h,c}` is an isolated replacement study for the six
legacy mouth renderer IDs. It is deliberately not connected to
`face_render.c` or the firmware build:

| Legacy ID | Standalone profile | Character/rig identity |
|---:|---|---|
| 23 | `FACE_MOUTH_STUDY_REDUX_PRESTON` | Pip, a pixel/cel Preston-style rig |
| 24 | `FACE_MOUTH_STUDY_REDUX_JALI` | Jali, a faceted polygon performance mask |
| 25 | `FACE_MOUTH_STUDY_REDUX_RIBBON` | Ruby, a curved cabaret/ribbon rig |
| 26 | `FACE_MOUTH_STUDY_REDUX_TEETH_TONGUE` | Munch, a teeth-and-tongue creature |
| 27 | `FACE_MOUTH_STUDY_REDUX_LED_VU` | VU-5, a face-integrated LED display |
| 28 | `FACE_MOUTH_STUDY_REDUX_ORIGAMI` | Ori, a folded-paper fox mask |

## Contract

- Exact 160×120 RGB565 output.
- Integer-only, deterministic, stateless, and heap-free renderer.
- Full 40-byte `face_render_key_t` retained in the resolved pose and included
  in an input signature.
- Stable face-space eye and mouth anchors; mouth corners, cheeks, and jaw are
  derived from one resolved skeleton.
- Eleven authored emotions drive eyes, brows, gaze, corners, aperture, width,
  cheeks, and jaw.
- Native OVR15, VRM5, and Preston9 decoding with continuous primary/secondary
  viseme blending.
- Speech phase acting: at least two rendered frames of eye/lid/cheek
  anticipation before a large mouth beat, held active articulation, and an
  eased asymmetric settle.
- Aperture is constrained before rendering so adversarial downturned/open
  combinations preserve room for the complete lip stroke instead of clipping.

## Second-pass acting constraints

- ID 23 keeps the pixel/cel rig but adds speaking eye and brow response so its
  gaze is not frozen around large mouth beats.
- ID 24 keeps fixed diamond eye sockets. Gaze is integer-eased and clamped
  inside them, the mouth corners are the cheek vertices, and the jaw facet no
  longer chatters or translates with head inputs.
- ID 25 uses a smooth integer aperture curve capped at 23 pixels. Three-frame
  anticipation moves the lids, brows, and cheeks before the cabaret mouth
  reaches its peak.
- ID 26 moves the eyes and brows before the large jaw beat, then resolves with
  a small jaw skew and unequal mouth-corner settle.
- ID 27 uses eye-led 5×5 bitmap acting and a held, smooth LED aperture
  envelope. Four-frame active segments keep one vowel/coarticulation
  silhouette instead of converting raw audio changes into mouth chatter.
- ID 28 keeps the face, eye sockets, mouth anchors, and fold/jaw geometry
  fixed in face space. Expression changes move the features within the mask,
  not the whole head.

## Isolated verification

Run:

```sh
firmware-ws/tests/run_face_mouth_study_redux_native.sh
```

The runner builds only the standalone module and its dedicated tools. It runs:

- strict C11 native assertions;
- a 256-case full-record adversarial test with frame canaries;
- ASan and UBSan versions of the same suite;
- full 11-emotion distinctness and geometric response checks;
- explicit aperture-cap, fixed-socket, pinned-anchor, anticipation, settle,
  held-envelope, and fixed-fold assertions;
- all 15 OVR, 5 VRM, and 9 Preston visemes for every rig;
- 256-step AA→U coarticulation continuity checks;
- a six-profile 30 fps budget benchmark;
- contact-sheet generation in
  `tests/artifacts/face_mouth_study_redux/`.

The benchmark is a host-side regression guard, not an ESP32 performance
measurement.

## Review artifacts

- `before-second-pass-expressions-native.png` and
  `before-second-pass-temporal-native.png`: preserved first-pass inspection
  sheets.
- `mouth-study-expressions-native-6x11.png`: all eleven emotions, one rig per
  row, using exact 160×120 rendered tiles with no label pixels.
- `mouth-study-expressions-40x30-6x11.png`: nearest-neighbour 40×30 inspection
  tiles for the same expressions.
- `mouth-study-temporal-speech-native-6x24.png`: rest, three anticipation
  frames, four held active phoneme sections, three settle frames, and release.
- `mouth-study-temporal-speech-40x30-6x24.png`: exact 40×30 inspection tiles
  for the same timeline.
- `mouth-study-vocabularies-6x29.png`: OVR15 + VRM5 + Preston9.

The `native` and `40x30` sheets contain only frame pixels. The older labelled
sheets keep each character tile at 160×120 and add a 12-pixel external label
strip which is not part of the rendered frame.
