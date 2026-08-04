# Pixel Redux actor pack

`face_pixel_redux_actors.c` is a standalone, allocation-free replacement pack
for the five weakest legacy pixel rows. It does not alter the production
dispatcher.

| Legacy ID | Replacement | Visual grammar |
|---:|---|---|
| 0 | EGA Wayfarer | strict 16-colour quest portrait and authored mouth cels |
| 1 | VGA Oracle | dithered elder portrait with shaded mouth and beard |
| 2 | CD-Talkie Mechanic | asymmetric cinematic close-up with richer lip mass |
| 3 | Arcade Automaton | cyan eye modules and an articulated LED speech rail |
| 5 | Pocket Mossling | four-tone handheld-RPG familiar |

All five render original procedural artwork on an 80×60 logical grid, enlarged
to 160×120 with exact 2× pixels. Their facial anchors stay fixed, every draw
primitive clips to a four-physical-pixel safe border, and the renderer uses
integer arithmetic and caller-owned RGB565 memory only.

## Render IR and acting

The renderer consumes every byte of the schema-v2 40-byte render key. It keeps
stage direction, activity, affect, attention, gaze, head/body pose, speech
phase, direct controls, and dense articulation independently visible rather
than letting one source erase another. OVR15, VRM5, Preston9, and Microsoft22
visemes map into continuous open/width/round/press/teeth/tongue controls, with
primary-to-secondary interpolation.

The expression sheet columns are:

`neutral, warm, joy, concern, surprise, thoughtful, skeptical, determined,
sleepy, excited, embarrassed`.

Each expression changes geometry—eyes, lids, brows, mouth corners, pose, cheek
mass, or an intentionally small stage icon—not merely colour.

The 16-frame speech sheets are chronological:

| Frame | State |
|---:|---|
| 0 | listening rest |
| 1 | one-frame `STARTING` anticipation, including lid compression |
| 2–13 | active `AA E I O U PP FF TH SS RR CH AA` articulation |
| 14 | two-part `ENDING` settle |
| 15 | listening rest |

During active speech the eyes, asymmetric lids, gaze, cheeks, pose, and mouth
all participate. Human and familiar mouth corners are attached to the lip
geometry; the robot uses a fixed mechanical rail with animated LED segments
and cheek actuators. The sequence intentionally includes more than three
recognisable talk poses and is not an amplitude-only jaw flap.

## Source-first provenance

No third-party artwork, character, sprite sheet, or traced silhouette is
included. The implementation uses only broad, old techniques:

- the local MIT-licensed `m5stack-avatar` checkout informed the separation of
  face parts and expression state; no source or assets were copied;
- the local `pycozmo/procedural_face.py` checkout informed the fixed-topology,
  parameter-stream approach; no source or assets were copied;
- the repository's `fable_mouth_geometry` research notes informed the generic
  idea of authored adventure-game mouth cels;
- the standard IBM EGA colour values are a historical hardware palette, not
  character artwork.

Every character and pixel composition in this pack is original.

## Reproduce and inspect

Run the complete native rig:

```sh
firmware-ws/tests/run_face_pixel_redux_actors_native.sh \
  local/face-pixel-redux-actors
```

It produces:

- ASan/UBSan results over 2,560 adversarial renders plus contract tests;
- a 5×11 native expression atlas and five native expression rows;
- a 5×16 chronological speech atlas and five native speech strips;
- 40×30-per-tile contact sheets for the zoomed-out matrix;
- isolated CPU-time throughput and optimized-object size reports.

Promotion remains a visual decision. Inspect the native and contact-scale
atlases for anatomy, eye life, attached mouth corners, real expression
differences, speech anticipation/settle, accidental crop writes, and temporal
continuity before wiring any row into the shared dispatcher.
