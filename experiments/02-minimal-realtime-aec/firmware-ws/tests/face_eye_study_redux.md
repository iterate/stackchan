# Face eye-study redux

`face_eye_study_redux.{h,c}` is a standalone, integration-ready replacement
for legacy render profiles 15 through 22. It deliberately has no dependency on
legacy renderer dispatch and this study does not modify dispatch, CMake, shared
test scripts, enums, or another face module.

## Exact legacy contract

| Legacy ID | Redux profile |
|---:|---|
| 15 | Saccade laboratory |
| 16 | Brow dialogue |
| 17 | Lid anticipation |
| 18 | Iris parallax |
| 19 | Sleep and wake |
| 20 | Curious asymmetric gaze |
| 21 | Dot-matrix eyes |
| 22 | Cat optics |

The public enum preserves those numeric IDs exactly. The resolver copies and
signatures all 40 bytes of `face_render_key_t`. It is deterministic,
integer-only, stateless, heap-free, and clips every write to the 160×120 RGB565
frame. Eye centers are fixed per profile; head/body/look cues affect only
bounded gaze, aperture, brows, and authored corner acting.

## Performance model

Each style supplies a stable socket silhouette and palette. Shared procedural
acting resolves:

- all 11 stage emotions into distinct aperture, proportion, lid, brow, gaze,
  and pupil geometry;
- smoothstep autonomous blinks with a readable full-width closed-lid curve;
- hashed but deterministic saccade targets with smooth anticipation/travel/
  settle interpolation;
- bounded gaze/parallax and aperture-clipped iris, pupil, ray, and highlight
  layers;
- speech STARTING, ACTIVE energy, and ENDING channels across lid, aperture,
  width, brow, pupil, and micro-gaze;
- deliberately asymmetric concern, thought, skepticism, determination, and
  embarrassment without translating the face or sockets.

The renderer is eye-only: it requires no mouth result and emits no mouth
geometry.

## Native verification

Run:

```sh
firmware-ws/tests/run_face_eye_study_redux_native.sh
```

The isolated runner builds and executes:

- strict C11 native tests with warnings as errors;
- separate AddressSanitizer and UndefinedBehaviorSanitizer runs;
- 256 adversarial full-IR cases with frame canaries and landmark bounds;
- exact legacy mapping, deterministic output, and complete 40-byte source-copy
  and signature coverage;
- fixed-socket/extreme-gaze checks proving gaze changes cannot translate
  pixels outside eye landmarks;
- pairwise-distinct renders and geometry-channel changes for all 11 emotions
  in all eight profiles;
- authored closed-lid readability and speech anticipation/active/settle
  assertions;
- a 210-frame-per-profile integrated blink/saccade continuity probe;
- a per-profile throughput benchmark whose hard gate is greater than 30 fps.

The runner also regenerates four PPM/PNG contact sheets in
`tests/artifacts/face_eye_study_redux/`. Review those sheets at original size;
the dense chronology sheets are the primary motion-quality evidence.

## Integration boundary

The root integrator can include `face_eye_study_redux.h`, map an existing
legacy profile ID through
`face_eye_study_redux_profile_from_legacy_id()`, and call
`face_eye_study_redux_render_checked()`. No renderer context allocation is
required (`FACE_EYE_STUDY_REDUX_CONTEXT_BYTES == 0`).

