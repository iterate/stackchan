#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow>=11,<13"]
# ///
"""Build the authored Pocket Mossling FSPR atlas.

The source sheet is project-local generated concept art.  This script turns
that reference into a deliberately tiny runtime asset:

* eleven complete expression portraits;
* an exact four-colour DMG palette plus transparency;
* twenty-three mapped mouth cels covering OVR15, VRM5, Preston9, and
  Microsoft22;
* three-stage eyelids for deterministic natural blinks;
* PackBits-compressed flash data for the existing allocation-free C player.

The generated C is shared by the ESP32 firmware and the browser's WASM build.
No source bitmap decoding happens at runtime.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

from generate_sprite_showcase import (
    Art,
    AtlasBuilder,
    EXPRESSIONS,
    MOUTH_ROLES,
    MOUTH_SLOT_NAMES,
    add_viseme_maps,
    emit_atlas,
)


HERE = Path(__file__).resolve().parent
EXPERIMENT_DIR = HERE.parent
MAIN_DIR = EXPERIMENT_DIR / "firmware-ws" / "main"
SOURCE = (
    HERE
    / "face-grid"
    / "assets"
    / "mossling"
    / "pocket-mossling-expression-viseme-concepts-v1.png"
)
OUT_H = MAIN_DIR / "face_sprite_mossling_generated.h"
OUT_C = MAIN_DIR / "face_sprite_mossling_generated.c"
PREVIEW_DIR = EXPERIMENT_DIR / "local" / "mossling-sprite-atlas"

W = 80
H = 60
BACKGROUND = (155, 188, 15)
SOURCE_BACKGROUND = (247, 251, 230)

# Original DMG-inspired four-shade ramp, darkest to lightest.
DMG_RGB = (
    0x0F380F,
    0x306230,
    0x8BAC0F,
    0x9BBC0F,
)


@dataclass(frozen=True)
class SourceFrame:
    slug: str
    box: tuple[int, int, int, int]
    eye_left: tuple[int, int]
    eye_right: tuple[int, int]
    mouth: tuple[int, int]


@dataclass(frozen=True)
class NormalizedFrame:
    art: Art
    scale: float
    x: int
    y: int
    source_box: tuple[int, int, int, int]

    def point(self, source: tuple[int, int]) -> tuple[int, int]:
        left, top, _, _ = self.source_box
        return (
            self.x + round((source[0] - left) * self.scale),
            self.y + round((source[1] - top) * self.scale),
        )


# The generated reference intentionally has no labels.  These manually audited
# boxes isolate the eleven authored portraits without pulling in neighbours.
SOURCE_FRAMES = (
    SourceFrame(
        "neutral", (18, 68, 268, 311), (108, 190), (177, 190), (143, 232)
    ),
    SourceFrame(
        "warm", (274, 69, 507, 311), (349, 205), (422, 205), (386, 232)
    ),
    SourceFrame(
        "joy", (516, 64, 746, 311), (589, 204), (660, 204), (624, 233)
    ),
    SourceFrame(
        "concern",
        (760, 77, 983, 311),
        (834, 205),
        (906, 205),
        (870, 239),
    ),
    SourceFrame(
        "surprise",
        (988, 80, 1205, 311),
        (1059, 206),
        (1132, 206),
        (1096, 240),
    ),
    SourceFrame(
        "thoughtful",
        (1214, 80, 1440, 311),
        (1288, 204),
        (1361, 204),
        (1325, 239),
    ),
    SourceFrame(
        "skeptical",
        (46, 380, 279, 615),
        (112, 510),
        (183, 510),
        (149, 548),
    ),
    SourceFrame(
        "determined",
        (304, 381, 555, 620),
        (384, 508),
        (457, 508),
        (422, 548),
    ),
    SourceFrame(
        "sleepy",
        (594, 379, 808, 621),
        (654, 512),
        (717, 512),
        (687, 548),
    ),
    SourceFrame(
        "excited",
        (866, 354, 1112, 621),
        (945, 493),
        (1027, 493),
        (986, 542),
    ),
    SourceFrame(
        "embarrassed",
        (1140, 388, 1362, 626),
        (1216, 520),
        (1288, 520),
        (1253, 556),
    ),
)


def rgb_tuple(value: int) -> tuple[int, int, int]:
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def nearest_dmg(red: int, green: int, blue: int) -> int:
    best = 0
    best_distance = 1 << 30
    for index, value in enumerate(DMG_RGB):
        pr, pg, pb = rgb_tuple(value)
        distance = (
            (red - pr) * (red - pr)
            + (green - pg) * (green - pg)
            + (blue - pb) * (blue - pb)
        )
        if distance < best_distance:
            best = index
            best_distance = distance
    return DMG_RGB[best]


def is_foreground(pixel: tuple[int, int, int]) -> bool:
    red, green, blue = pixel
    delta = max(
        abs(red - SOURCE_BACKGROUND[0]),
        abs(green - SOURCE_BACKGROUND[1]),
        abs(blue - SOURCE_BACKGROUND[2]),
    )
    return delta >= 24 and (red < 235 or green < 243 or blue < 218)


def normalize_frame(image: Image.Image, source: SourceFrame) -> NormalizedFrame:
    left, top, right, bottom = source.box
    crop = image.crop(source.box).convert("RGB")
    crop_width, crop_height = crop.size
    scale = min(74.0 / crop_width, 53.0 / crop_height)
    out_width = max(1, round(crop_width * scale))
    out_height = max(1, round(crop_height * scale))
    resized = crop.resize(
        (out_width, out_height),
        resample=Image.Resampling.NEAREST,
    )
    source_mask = Image.new("L", crop.size, 0)
    source_mask.putdata(
        [255 if is_foreground(pixel) else 0 for pixel in crop.getdata()]
    )
    mask = source_mask.resize(
        (out_width, out_height),
        resample=Image.Resampling.NEAREST,
    )

    offset_x = (W - out_width) // 2
    offset_y = 56 - out_height
    art = Art(W, H)
    pixels = resized.load()
    mask_pixels = mask.load()
    for y in range(out_height):
        for x in range(out_width):
            if mask_pixels[x, y] == 0:
                continue
            red, green, blue = pixels[x, y]
            art.put(
                offset_x + x,
                offset_y + y,
                nearest_dmg(red, green, blue),
            )
    return NormalizedFrame(
        art=art,
        scale=scale,
        x=offset_x,
        y=offset_y,
        source_box=(left, top, right, bottom),
    )


def mouth_cell(slot: int) -> Art:
    """Draw one compact, replaceable authored-style lower-face cel."""
    dark, mid, moss, light = DMG_RGB
    art = Art(30, 18)
    cx = 15
    cy = 9

    def cavity(
        width: int,
        height: int,
        *,
        teeth: bool = False,
        tongue: bool = False,
        rounder: bool = False,
    ) -> None:
        if rounder:
            art.disc(cx, cy, max(2, width // 2), max(2, height // 2), dark)
        else:
            art.disc(cx, cy, max(2, width // 2), max(2, height // 2), dark)
            art.hline(cx - width // 2 + 1, cy - height // 2, width - 2, dark)
        if teeth:
            art.hline(cx - max(2, width // 2 - 2), cy - 2, max(4, width - 4), light)
        if tongue:
            art.hline(cx - max(2, width // 3), cy + max(1, height // 3), max(4, width * 2 // 3), mid)

    if slot == 0:  # sil
        art.hline(cx - 5, cy, 11, dark)
        art.put(cx - 6, cy - 1, mid)
        art.put(cx + 6, cy - 1, mid)
    elif slot == 1:  # PP
        art.hline(cx - 6, cy - 1, 13, dark)
        art.hline(cx - 4, cy + 1, 9, mid)
    elif slot == 2:  # FF
        art.hline(cx - 7, cy - 2, 15, dark)
        art.hline(cx - 5, cy - 1, 11, light)
        art.hline(cx - 4, cy + 1, 9, mid)
    elif slot == 3:  # TH
        cavity(13, 7, teeth=True, tongue=True)
        art.rect(cx - 2, cy + 1, 5, 4, mid)
    elif slot == 4:  # DD
        cavity(13, 6, teeth=True)
    elif slot == 5:  # kk
        cavity(15, 6)
        art.put(cx - 5, cy - 1, light)
        art.put(cx + 5, cy - 1, light)
    elif slot == 6:  # CH
        cavity(10, 9, rounder=True)
    elif slot == 7:  # SS
        cavity(18, 5, teeth=True)
    elif slot == 8:  # nn
        art.hline(cx - 6, cy, 13, dark)
        art.put(cx - 3, cy + 1, mid)
        art.put(cx + 3, cy + 1, mid)
    elif slot == 9:  # RR
        cavity(11, 8, rounder=True)
        art.put(cx, cy + 2, mid)
    elif slot == 10:  # aa
        cavity(20, 11, teeth=True, tongue=True)
    elif slot == 11:  # E
        cavity(21, 7, teeth=True)
    elif slot == 12:  # ih
        cavity(16, 5, teeth=True)
    elif slot == 13:  # oh
        cavity(12, 12, rounder=True)
    elif slot == 14:  # ou
        cavity(7, 9, rounder=True)
        art.put(cx - 5, cy, mid)
        art.put(cx + 5, cy, mid)
    elif slot == 15:  # smile
        art.line(cx - 8, cy - 2, cx - 4, cy + 1, dark)
        art.hline(cx - 4, cy + 1, 9, dark)
        art.line(cx + 4, cy + 1, cx + 8, cy - 2, dark)
    elif slot == 16:  # frown
        art.line(cx - 8, cy + 2, cx - 4, cy - 1, dark)
        art.hline(cx - 4, cy - 1, 9, dark)
        art.line(cx + 4, cy - 1, cx + 8, cy + 2, dark)
    elif slot == 17:  # grin
        cavity(22, 8, teeth=True)
        art.hline(cx - 7, cy + 1, 15, moss)
    elif slot == 18:  # tongue
        cavity(15, 9, teeth=True, tongue=True)
        art.rect(cx - 2, cy + 1, 5, 5, mid)
    elif slot == 19:  # pucker
        art.disc(cx, cy, 4, 3, dark)
        art.disc(cx, cy, 2, 1, light)
        art.put(cx - 6, cy, mid)
        art.put(cx + 6, cy, mid)
    elif slot == 20:  # gasp
        cavity(9, 14, rounder=True)
        art.hline(cx - 2, cy - 4, 5, light)
    elif slot == 21:  # lateral
        cavity(18, 7, tongue=True)
        art.line(cx - 8, cy - 2, cx + 7, cy + 2, mid)
    else:  # smirk
        art.line(cx - 9, cy + 2, cx + 2, cy + 1, dark)
        art.line(cx + 2, cy + 1, cx + 9, cy - 3, mid)
        art.put(cx + 7, cy - 2, light)
    return art


def erase_baked_mouth(art: Art, center: tuple[int, int]) -> None:
    """Remove only the source mouth, retaining cheeks and lower eye shapes."""
    _, _, moss, light = DMG_RGB
    cx, cy = center
    art.disc(cx, cy + 1, 11, 4, light)
    art.put(cx - 9, cy + 4, moss)
    art.put(cx + 9, cy + 4, moss)
    art.hline(cx - 4, cy + 5, 9, moss)


def eye_cell(closed: int) -> Art:
    dark, _, moss, light = DMG_RGB
    art = Art(18, 12)
    if closed == 0:
        return art
    art.disc(9, 6, 7, 4, light)
    art.put(3, 8, moss)
    art.put(15, 8, moss)
    if closed == 1:
        art.hline(5, 7, 9, dark)
        art.hline(7, 8, 5, dark)
    else:
        art.line(3, 7, 7, 5, dark)
        art.hline(7, 5, 5, dark)
        art.line(11, 5, 15, 7, dark)
    return art


def art_to_image(art: Art, background: int = DMG_RGB[-1]) -> Image.Image:
    image = Image.new("RGB", (art.width, art.height), rgb_tuple(background))
    pixels = image.load()
    for y in range(art.height):
        for x in range(art.width):
            color = art.px[y * art.width + x]
            if color is not None:
                pixels[x, y] = rgb_tuple(color)
    return image


def write_previews(
    frames: list[tuple[SourceFrame, NormalizedFrame]],
    mouths: list[Art],
) -> None:
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    scale = 4
    label_height = 18
    expression_sheet = Image.new(
        "RGB",
        (6 * W * scale, 2 * (H * scale + label_height)),
        (15, 56, 15),
    )
    draw = ImageDraw.Draw(expression_sheet)
    for index, (source, frame) in enumerate(frames):
        x = (index % 6) * W * scale
        y = (index // 6) * (H * scale + label_height)
        sprite = art_to_image(frame.art).resize(
            (W * scale, H * scale),
            Image.Resampling.NEAREST,
        )
        expression_sheet.paste(sprite, (x, y))
        draw.text((x + 4, y + H * scale + 2), source.slug, fill=(155, 188, 15))
    expression_sheet.save(PREVIEW_DIR / "authored-expressions-native4x.png")

    mouth_sheet = Image.new(
        "RGB",
        (6 * 30 * scale, 4 * (18 * scale + label_height)),
        rgb_tuple(DMG_RGB[-1]),
    )
    draw = ImageDraw.Draw(mouth_sheet)
    for index, (name, art) in enumerate(zip(MOUTH_SLOT_NAMES, mouths)):
        x = (index % 6) * 30 * scale
        y = (index // 6) * (18 * scale + label_height)
        sprite = art_to_image(art).resize(
            (30 * scale, 18 * scale),
            Image.Resampling.NEAREST,
        )
        mouth_sheet.paste(sprite, (x, y))
        draw.text((x + 3, y + 18 * scale + 2), name, fill=(15, 56, 15))
    mouth_sheet.save(PREVIEW_DIR / "authored-mouths-native4x.png")


def build_atlas(image: Image.Image) -> tuple[AtlasBuilder, list[NormalizedFrame], list[Art]]:
    builder = AtlasBuilder(
        "pocket_mossling_authored",
        "Pocket Mossling · authored DMG sprite",
        DMG_RGB[-1],
    )
    normalized = [
        normalize_frame(image, source) for source in SOURCE_FRAMES
    ]
    for source, frame in zip(SOURCE_FRAMES, normalized):
        erase_baked_mouth(frame.art, frame.point(source.mouth))
    mouth_art = [mouth_cell(index) for index in range(len(MOUTH_SLOT_NAMES))]
    mouth_cells: list[int] = []
    for name, art in zip(MOUTH_SLOT_NAMES, mouth_art):
        builder.add(f"mouth_{name}", art)
        mouth_cells.append(builder.cell(f"mouth_{name}"))

    builder.add("eye_open", eye_cell(0))
    builder.add("eye_half", eye_cell(1))
    builder.add("eye_closed", eye_cell(2))
    eye_cells = [
        builder.cell("eye_open"),
        builder.cell("eye_half"),
        builder.cell("eye_closed"),
    ]
    transparent = Art(1, 1)
    builder.add("transparent", transparent)

    for expression, source, frame in zip(
        EXPRESSIONS, SOURCE_FRAMES, normalized
    ):
        builder.add(f"base_{source.slug}", frame.art)
        builder.mouth_arrays[source.slug] = mouth_cells
        eye_left = frame.point(source.eye_left)
        eye_right = frame.point(source.eye_right)
        mouth = frame.point(source.mouth)
        # The concept sheet's mouth landmarks are very close to the oversized
        # eyes after reduction to 80x60.  Give the replaceable speech cel its
        # own lower-face lane so wide/open visemes never chew into the lids.
        mouth = (mouth[0], mouth[1] + 4)
        builder.banks.append(
            {
                "expression": expression,
                "base": builder.cell(f"base_{source.slug}"),
                "overlay": builder.cell("transparent"),
                "mouth_array": source.slug,
                "mouth_anchor": (mouth[0] - 15, mouth[1] - 9),
                "eyes": (eye_cells, eye_cells),
                "eye_anchors": (
                    (eye_left[0] - 9, eye_left[1] - 6),
                    (eye_right[0] - 9, eye_right[1] - 6),
                ),
                "pupils": (-1, -1),
                "pupil_data": (
                    (0, 0, 0, 0, 0, 0, 0, 0),
                    (0, 0, 0, 0, 0, 0, 0, 0),
                ),
                "brows": ([], []),
                "brow_anchors": ((0, 0), (0, 0)),
                "brow_lift": 0,
            }
        )

    # Keep the generic sequence descriptor path valid without visible art.
    builder.sequences.append(
        [(builder.cell("transparent"), 1600, 0, 0)]
    )
    add_viseme_maps(builder)
    return builder, normalized, mouth_art


def write_generated(builder: AtlasBuilder) -> None:
    body, stats = emit_atlas(builder)
    body = body.replace(
        "FACE_SPRITE_ATLAS_IDLE_SACCADES |\n"
        "        FACE_SPRITE_ATLAS_AUTO_BLINK",
        "FACE_SPRITE_ATLAS_BREATHE |\n"
        "        FACE_SPRITE_ATLAS_AUTO_BLINK",
    )
    header = """\
/* Generated by tools/generate_mossling_sprite_atlas.py. Do not hand-edit. */
#pragma once

#include "face_sprite_sheet.h"

extern const face_sprite_atlas_t face_sprite_pocket_mossling_authored_atlas;
"""
    source = f"""\
/* Generated by tools/generate_mossling_sprite_atlas.py. Do not hand-edit.
 *
 * Source: tools/face-grid/assets/mossling/
 *   pocket-mossling-expression-viseme-concepts-v1.png
 * Runtime art is reduced to four DMG shades and flash-resident PackBits cells.
 * Encoded pixels: {stats["blob_bytes"]} bytes; cells: {stats["cell_count"]}.
 */
#include "face_sprite_mossling_generated.h"

#include <stddef.h>

{body}
"""
    OUT_H.write_text(header)
    OUT_C.write_text(source)


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"missing source sheet: {SOURCE}")
    image = Image.open(SOURCE).convert("RGB")
    builder, normalized, mouths = build_atlas(image)
    write_generated(builder)
    write_previews(list(zip(SOURCE_FRAMES, normalized)), mouths)
    print(f"generated {OUT_C.relative_to(EXPERIMENT_DIR)}")
    print(f"preview {PREVIEW_DIR.relative_to(EXPERIMENT_DIR)}")


if __name__ == "__main__":
    main()
