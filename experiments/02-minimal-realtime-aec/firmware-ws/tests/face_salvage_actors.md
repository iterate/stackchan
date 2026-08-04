# Salvage actor pack

`face_salvage_actors.c` is a standalone, allocation-free replacement pack for
six legacy renderer rows. It does not alter the production dispatcher.

| Legacy ID | Replacement |
|---:|---|
| 4 | Amber Terminal Operator |
| 6 | Dithered Film-Noir Rogue |
| 29 | Neon Cyan Faceplate |
| 35 | Red Optic Performer |
| 36 | HUB75 Block Mascot |
| 52 | Two-Tone Zine Rogue |

The styles intentionally use different visual grammars rather than shared
geometry with palette swaps. All use the schema-v2 40-byte render IR, fixed
safe-area anchors, integer arithmetic, caller-owned RGB565 memory, and smooth
sample-clock motion. The optic is deliberately mouthless; it maps articulation
to iris deformation, shutters, and side meters.

Run the complete standalone rig:

```sh
firmware-ws/tests/run_face_salvage_actors_native.sh \
  local/face-salvage-actors
```

It produces:

- ASan/UBSan and adversarial results;
- a 6×11 native expression atlas and one 11-frame sheet per style;
- chronological 16-frame speech strips at 30 fps;
- a 6×15 OVR-viseme atlas;
- speed and optimized-object size reports.

Promotion is a visual decision. The hash checks only catch accidental duplicate
states; they cannot establish acting quality. Inspect every 160×120 expression,
the left-to-right speech strips, gaze containment, mouth/jaw mass, and temporal
continuity before wiring a style into `face_render.c`.
