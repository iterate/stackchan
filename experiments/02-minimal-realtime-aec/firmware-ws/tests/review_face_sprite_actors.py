#!/usr/bin/env python3
"""Label the native-resolution sprite-actor PPM contact sheets.

The C test emits exact renderer pixels. This script only adds gutters and
labels; it never resizes or filters a frame, so visual review sees the same
160x120 RGB565 quantisation the device and WASM paths receive.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROWS = (
    "EGA COURT MAGE",
    "VGA STAR CAPTAIN",
    "TALKIE MOON MECHANIC",
    "JRPG STORM FAMILIAR",
    "HANDHELD FOREST PET",
    "ARCADE CHROME PILOT",
)
EXPRESSIONS = (
    "NEUTRAL",
    "WARM",
    "JOY",
    "CONCERN",
    "SURPRISE",
    "THOUGHTFUL",
    "SKEPTICAL",
    "DETERMINED",
    "SLEEPY",
    "EXCITED",
    "EMBARRASSED",
)
VISEMES = (
    "AA",
    "E",
    "I",
    "O",
    "U",
    "PP",
    "SS",
    "TH",
    "DD",
    "FF",
    "KK",
    "NN",
    "RR",
    "CH",
    "SIL",
)
MOTION = (
    "REST",
    "REST",
    "ANTICIPATE",
    "ANTICIPATE",
    "AA ACTIVE",
    "E ACTIVE",
    "I ACTIVE",
    "O + BLINK",
    "U ACTIVE",
    "SETTLE",
    "SETTLE",
    "SETTLE",
)


def label_portrait_contact_sheet(
    path: Path, columns: tuple[str, ...], title: str
) -> Path:
    """Show the first two actors at exact 40x30, enlarged only for review."""
    source = Image.open(path).convert("RGB")
    native_width = 160
    native_height = 120
    device_width = 40
    device_height = 30
    display_scale = 4
    display_width = device_width * display_scale
    display_height = device_height * display_scale
    portrait_rows = ROWS[:2]
    assert source.width == native_width * len(columns)
    assert source.height == native_height * len(ROWS)

    left = 214
    top = 64
    result = Image.new(
        "RGB",
        (
            left + display_width * len(columns),
            top + display_height * len(portrait_rows),
        ),
        "#090b12",
    )
    nearest = Image.Resampling.NEAREST
    for row in range(len(portrait_rows)):
        for column in range(len(columns)):
            native = source.crop(
                (
                    column * native_width,
                    row * native_height,
                    (column + 1) * native_width,
                    (row + 1) * native_height,
                )
            )
            exact = native.resize(
                (device_width, device_height), resample=nearest
            )
            contact = exact.resize(
                (display_width, display_height), resample=nearest
            )
            result.paste(
                contact,
                (
                    left + column * display_width,
                    top + row * display_height,
                ),
            )

    draw = ImageDraw.Draw(result)
    font = ImageFont.load_default()
    draw.text((12, 10), title, fill="#ffffff", font=font)
    draw.text(
        (12, 30),
        "Exact 40x30 nearest-neighbour contact render · shown 4x",
        fill="#aab4c8",
        font=font,
    )
    for index, label in enumerate(columns):
        x = left + index * display_width + display_width // 2
        draw.text(
            (x, top - 15),
            label,
            fill="#dce9ff",
            anchor="ms",
            font=font,
        )
    for index, label in enumerate(portrait_rows):
        y = top + index * display_height + display_height // 2
        draw.text(
            (left - 10, y),
            label,
            fill="#dce9ff",
            anchor="rm",
            font=font,
        )
    for column in range(len(columns) + 1):
        x = left + column * display_width
        draw.line(
            (x, top, x, top + display_height * len(portrait_rows)),
            fill="#283041",
        )
    for row in range(len(portrait_rows) + 1):
        y = top + row * display_height
        draw.line(
            (left, y, left + display_width * len(columns), y),
            fill="#283041",
        )

    output = path.with_name(
        f"{path.stem}__portrait-exact40-contact4x.png"
    )
    result.save(output, optimize=True)
    return output


def label_sheet(path: Path, columns: tuple[str, ...], title: str) -> Path:
    source = Image.open(path).convert("RGB")
    frame_width = 160
    frame_height = 120
    assert source.width == frame_width * len(columns)
    assert source.height == frame_height * len(ROWS)

    left = 214
    top = 64
    result = Image.new("RGB", (left + source.width, top + source.height), "#090b12")
    result.paste(source, (left, top))
    draw = ImageDraw.Draw(result)
    font = ImageFont.load_default()
    draw.text((12, 10), title, fill="#ffffff", font=font)
    draw.text(
        (12, 30),
        "Exact 160x120 RGB565 frames · nearest-neighbour · no filtering",
        fill="#aab4c8",
        font=font,
    )
    for index, label in enumerate(columns):
        x = left + index * frame_width + frame_width // 2
        draw.text(
            (x, top - 15),
            label,
            fill="#dce9ff",
            anchor="ms",
            font=font,
        )
    for index, label in enumerate(ROWS):
        y = top + index * frame_height + frame_height // 2
        draw.text(
            (left - 10, y),
            label,
            fill="#dce9ff",
            anchor="rm",
            font=font,
        )
    for column in range(len(columns) + 1):
        x = left + column * frame_width
        draw.line((x, top, x, top + source.height), fill="#283041")
    for row in range(len(ROWS) + 1):
        y = top + row * frame_height
        draw.line((left, y, left + source.width, y), fill="#283041")

    output = path.with_suffix(".png")
    result.save(output, optimize=True)
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory
    outputs = (
        label_sheet(
            directory / "sprite-actors__6-languages__11-stage-emotions__mid-speech.ppm",
            EXPRESSIONS,
            "SPRITE ACTORS · EXPRESSION READABILITY",
        ),
        label_sheet(
            directory / "sprite-actors__6-languages__15-ovr-visemes__warm-active.ppm",
            VISEMES,
            "SPRITE ACTORS · OVR15 ARTICULATION",
        ),
        label_sheet(
            directory
            / "sprite-actors__6-languages__speech-start-blink-settle__motion.ppm",
            MOTION,
            "SPRITE ACTORS · CHRONOLOGICAL SPEECH ACTING",
        ),
        label_portrait_contact_sheet(
            directory / "sprite-actors__6-languages__11-stage-emotions__mid-speech.ppm",
            EXPRESSIONS,
            "PORTRAIT SPRITE ACTORS · EXPRESSION READABILITY",
        ),
        label_portrait_contact_sheet(
            directory / "sprite-actors__6-languages__15-ovr-visemes__warm-active.ppm",
            VISEMES,
            "PORTRAIT SPRITE ACTORS · OVR15 ARTICULATION",
        ),
        label_portrait_contact_sheet(
            directory
            / "sprite-actors__6-languages__speech-start-blink-settle__motion.ppm",
            MOTION,
            "PORTRAIT SPRITE ACTORS · CHRONOLOGICAL SPEECH / BLINK",
        ),
    )
    for output in outputs:
        print(output)


if __name__ == "__main__":
    main()
