# Face mouth actors visual review

Each `actor-*-expressions.png` is an unscaled, labelled strip of the eleven
authored stage expressions at the renderer's native 160x120 resolution.

`actor-speech-temporal.png` is a six-row temporal review. Rows are Preston,
JALI, ribbon, teeth/tongue, LED VU, and origami. Columns are:

`REST -> ANT -> AA -> AA/E -> EE -> E/O -> OH -> O/MBP -> MBP -> FV -> TH -> SET`

`ANT` uses `FACE_SPEECH_STARTING`; `SET` uses
`FACE_SPEECH_ENDING`. Transition columns use `viseme_secondary` and
`viseme_blend`, so the sheet exercises anticipation, coarticulation, bilabial
pressure, teeth, tongue, and settle instead of showing unrelated static poses.

Each `actor-*-smooth-motion.png` is the 12-frame window around that actor's
largest transition in the production 180-frame smooth-input probe. These
strips come from the integrated `face_render_frame()` path, including the
five-second joy/nod stage cue.

The strict probe captured with these artifacts passed all 62 production
profiles with zero motion failures and zero abrupt jumps. Actor-specific
motion results were:

| Actor | Abrupt | Median ROI Δ | P95 ROI Δ | Maximum ROI Δ | Max changed | Peak frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Preston | 0 | 0.002772 | 0.013917 | 0.019734 | 0.034766 | 43 |
| JALI | 0 | 0.002143 | 0.009266 | 0.012000 | 0.028906 | 71 |
| Ribbon | 0 | 0.002750 | 0.014164 | 0.018691 | 0.040234 | 47 |
| Teeth/tongue | 0 | 0.003192 | 0.013062 | 0.016159 | 0.032656 | 90 |
| LED VU | 0 | 0.000000 | 0.014098 | 0.024773 | 0.054688 | 41 |
| Origami | 0 | 0.001546 | 0.007201 | 0.010725 | 0.029531 | 9 |

The `.ppm` files are the dump program's deterministic native output. The
matching `.png` files are lossless conversions for convenient review.

Generate the PPM artifacts from the firmware workspace with:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
  -I main main/face_mouth_actors.c tests/face_mouth_actors_dump.c \
  -o /tmp/face_mouth_actors_dump
/tmp/face_mouth_actors_dump tests/artifacts/face_mouth_actors
```

Generate the integrated expression and smooth-motion report from the
experiment directory with:

```sh
python3 tools/run_face_render_quality.py --strict
```
