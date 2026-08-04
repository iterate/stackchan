# Sprite actors: asset and review notes

## Artwork and licence

The six built-in actors in `face_sprite_actors.c` are original procedural
pixel artwork made for StackChan. They are dedicated to the public domain
under CC0-1.0. No pixels were copied, traced, or extracted from a commercial
game. Their names describe broad technical eras, not particular characters:

1. EGA Court Mage
2. VGA Star Captain
3. Talkie Moon Mechanic
4. JRPG Storm Familiar
5. Handheld Forest Pet
6. Arcade Chrome Pilot

Imported third-party sheets keep their own licence. The converter or caller
must record provenance and permission alongside the generated descriptor.

## External indexed-sheet format

`fsa_sheet_t` describes an allocation-free indexed atlas:

- one RGB565 palette and one byte-per-pixel index plane;
- trimmed `fsa_cell_t` rectangles with logical origin offsets;
- semantic pose records mapped from all 11 stage expressions;
- open-to-closed eye cells, fixed sockets, and separately clamped pupils;
- expression-specific mouth banks;
- arbitrary `(viseme_set, viseme) -> mouth frame` rows plus OVR15 fallback;
- 16 kHz-sample timing for pose/mouth holds, speech anticipation and settle,
  stepped blink cells, and deterministic auto-blink;
- explicit held-cut transition metadata.

The compositor validates every reference and palette index before rendering,
centres the native canvas, clips every destination write, and supports 1x–8x
integer nearest-neighbour scaling. The full 40-byte render key remains the
wire/interchange format; sprite assets do not invent a second animation IR.

## Repeatable native review

Compile and run from the repository root:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow -pedantic \
  -I experiments/02-minimal-realtime-aec/firmware-ws/main \
  experiments/02-minimal-realtime-aec/firmware-ws/main/face_sprite_actors.c \
  experiments/02-minimal-realtime-aec/firmware-ws/tests/face_sprite_actors_test.c \
  -o /tmp/face_sprite_actors_test

mkdir -p experiments/02-minimal-realtime-aec/local/face-sprite-actors
/tmp/face_sprite_actors_test \
  experiments/02-minimal-realtime-aec/local/face-sprite-actors

uv run --with pillow \
  experiments/02-minimal-realtime-aec/firmware-ws/tests/review_face_sprite_actors.py \
  experiments/02-minimal-realtime-aec/local/face-sprite-actors
```

The test includes descriptor validation, a real atlas-compositing canary,
guarded output buffers, 3,000 arbitrary 40-byte keys, deterministic purity,
all 66 style/expression combinations, temporal anticipation/blink/settle
assertions, and a host benchmark. The three labelled sheets retain exact
160x120 frames without filtering:

- 6 styles × 11 stage emotions at mid-speech;
- 6 styles × 15 OVR visemes;
- 6 chronological speech-start/active/blink/settle strips.
