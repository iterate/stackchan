#!/usr/bin/env python3
"""Build dependency-free labelled boards from exact 40x30 PPM dumps."""

from __future__ import annotations

import argparse
from pathlib import Path


VARIANTS = [
    "SUNBLADE RANGER",
    "TAVERN BARD",
    "MOONKEEP ROGUE",
    "ASTRAL ARCHIVIST",
    "STORM SEER",
    "HEARTH SAGE",
    "DOCKYARD PILOT",
    "NEON ENGINEER",
    "INTERCOM CAPTAIN",
    "CRT CONCIERGE",
    "ARCADE SENTINEL",
    "PINBALL BELLHOP",
    "ACORN SCOUT",
    "BOG SPRITE",
    "MOONCAP FAMILIAR",
    "TIN WARDEN",
    "LANTERN MOTH",
    "SLIME COURIER",
]

HERO_MOSSLING = [
    "POCKET MOSSLING BASE",
    "ACORN SCOUT",
    "BOG SPRITE",
    "MOONCAP FAMILIAR",
]

MATRICES = {
    "expressions-11": [
        "NEUTRAL",
        "WARM",
        "JOY",
        "CONCERN",
        "SURPRISE",
        "THOUGHT",
        "SKEPTIC",
        "DETERMINED",
        "SLEEPY",
        "EXCITED",
        "EMBARRASSED",
    ],
    "visemes-15": [
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
    ],
    "speech-blink-24f": [f"F{index:02d}" for index in range(24)],
    "idle-turn-blink-32f": [f"I{index:02d}" for index in range(32)],
}

FAMILIES = [
    ("ega-wayfarer", 0),
    ("vga-oracle", 3),
    ("talkie-mechanic", 6),
    ("arcade-automaton", 9),
    ("pocket-mossling", 12),
    ("dmg-handheld", 15),
]

# Compact 5x7 uppercase font. Rows are five-bit masks, left to right.
FONT = {
    " ": [0, 0, 0, 0, 0, 0, 0],
    "-": [0, 0, 0, 31, 0, 0, 0],
    ".": [0, 0, 0, 0, 0, 6, 6],
    "0": [14, 17, 19, 21, 25, 17, 14],
    "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 2, 4, 8, 31],
    "3": [30, 1, 1, 14, 1, 1, 30],
    "4": [2, 6, 10, 18, 31, 2, 2],
    "5": [31, 16, 16, 30, 1, 1, 30],
    "6": [6, 8, 16, 30, 17, 17, 14],
    "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14],
    "9": [14, 17, 17, 15, 1, 2, 12],
    "A": [14, 17, 17, 31, 17, 17, 17],
    "B": [30, 17, 17, 30, 17, 17, 30],
    "C": [14, 17, 16, 16, 16, 17, 14],
    "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31],
    "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [14, 17, 16, 23, 17, 17, 15],
    "H": [17, 17, 17, 31, 17, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14],
    "J": [7, 2, 2, 2, 18, 18, 12],
    "K": [17, 18, 20, 24, 20, 18, 17],
    "L": [16, 16, 16, 16, 16, 16, 31],
    "M": [17, 27, 21, 21, 17, 17, 17],
    "N": [17, 25, 21, 19, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14],
    "P": [30, 17, 17, 30, 16, 16, 16],
    "Q": [14, 17, 17, 17, 21, 18, 13],
    "R": [30, 17, 17, 30, 20, 18, 17],
    "S": [15, 16, 16, 14, 1, 1, 30],
    "T": [31, 4, 4, 4, 4, 4, 4],
    "U": [17, 17, 17, 17, 17, 17, 14],
    "V": [17, 17, 17, 17, 17, 10, 4],
    "W": [17, 17, 17, 21, 21, 21, 10],
    "X": [17, 17, 10, 4, 10, 17, 17],
    "Y": [17, 17, 10, 4, 4, 4, 4],
    "Z": [31, 1, 2, 4, 8, 16, 31],
}


def _token(stream) -> bytes:
    while True:
        byte = stream.read(1)
        if not byte:
            raise ValueError("unexpected EOF in PPM header")
        if byte == b"#":
            stream.readline()
            continue
        if not byte.isspace():
            break
    value = bytearray(byte)
    while True:
        byte = stream.read(1)
        if not byte or byte.isspace():
            return bytes(value)
        value.extend(byte)


def read_ppm(path: Path) -> tuple[int, int, bytearray]:
    with path.open("rb") as stream:
        if _token(stream) != b"P6":
            raise ValueError(f"{path}: only P6 PPM is supported")
        width = int(_token(stream))
        height = int(_token(stream))
        if int(_token(stream)) != 255:
            raise ValueError(f"{path}: max value must be 255")
        pixels = bytearray(stream.read(width * height * 3))
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: truncated pixel payload")
    return width, height, pixels


def write_ppm(
    path: Path, width: int, height: int, pixels: bytearray
) -> None:
    with path.open("wb") as stream:
        stream.write(f"P6\n{width} {height}\n255\n".encode())
        stream.write(pixels)


def crop(
    pixels: bytearray,
    width: int,
    left: int,
    top: int,
    crop_width: int,
    crop_height: int,
) -> bytearray:
    result = bytearray(crop_width * crop_height * 3)
    for row in range(crop_height):
        source = ((top + row) * width + left) * 3
        target = row * crop_width * 3
        result[target : target + crop_width * 3] = pixels[
            source : source + crop_width * 3
        ]
    return result


def scale_nearest(
    pixels: bytearray, width: int, height: int, factor: int
) -> tuple[int, int, bytearray]:
    scaled_width = width * factor
    scaled_height = height * factor
    result = bytearray(scaled_width * scaled_height * 3)
    for y in range(height):
        expanded = bytearray()
        start = y * width * 3
        for x in range(width):
            color = pixels[start + x * 3 : start + x * 3 + 3]
            expanded.extend(color * factor)
        for repeat in range(factor):
            target = (y * factor + repeat) * scaled_width * 3
            result[target : target + scaled_width * 3] = expanded
    return scaled_width, scaled_height, result


def put(
    pixels: bytearray,
    width: int,
    height: int,
    x: int,
    y: int,
    color: tuple[int, int, int],
) -> None:
    if x < 0 or y < 0 or x >= width or y >= height:
        return
    offset = (y * width + x) * 3
    pixels[offset : offset + 3] = bytes(color)


def rect(
    pixels: bytearray,
    width: int,
    height: int,
    x: int,
    y: int,
    rect_width: int,
    rect_height: int,
    color: tuple[int, int, int],
) -> None:
    for yy in range(y, y + rect_height):
        for xx in range(x, x + rect_width):
            put(pixels, width, height, xx, yy, color)


def paste(
    destination: bytearray,
    destination_width: int,
    source: bytearray,
    source_width: int,
    source_height: int,
    left: int,
    top: int,
) -> None:
    for row in range(source_height):
        source_start = row * source_width * 3
        target_start = (
            (top + row) * destination_width + left
        ) * 3
        destination[
            target_start : target_start + source_width * 3
        ] = source[
            source_start : source_start + source_width * 3
        ]


def text_width(value: str, scale: int) -> int:
    return max(0, len(value) * 6 * scale - scale)


def draw_text(
    pixels: bytearray,
    width: int,
    height: int,
    x: int,
    y: int,
    value: str,
    scale: int,
    color: tuple[int, int, int],
) -> None:
    cursor = x
    for character in value.upper():
        rows = FONT.get(character, FONT[" "])
        for row, mask in enumerate(rows):
            for column in range(5):
                if mask & (1 << (4 - column)):
                    rect(
                        pixels,
                        width,
                        height,
                        cursor + column * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color,
                    )
        cursor += 6 * scale


def labelled_board(
    source_width: int,
    source_height: int,
    source: bytearray,
    row_labels: list[str],
    column_labels: list[str],
    scale: int = 4,
) -> tuple[int, int, bytearray]:
    cell_w = 40 * scale
    cell_h = 30 * scale
    label_w = 224
    header_h = 48
    scaled_w, scaled_h, scaled = scale_nearest(
        source, source_width, source_height, scale
    )
    board_w = label_w + scaled_w
    board_h = header_h + scaled_h
    background = bytes((19, 22, 29))
    board = bytearray(background * (board_w * board_h))
    paste(board, board_w, scaled, scaled_w, scaled_h, label_w, header_h)
    line = (76, 84, 99)
    ink = (238, 241, 247)
    for row, label in enumerate(row_labels):
        y = header_h + row * cell_h
        rect(board, board_w, board_h, 0, y, label_w, 1, line)
        rect(
            board,
            board_w,
            board_h,
            0,
            y + cell_h - 1,
            label_w,
            1,
            line,
        )
        draw_text(
            board,
            board_w,
            board_h,
            12,
            y + (cell_h - 14) // 2,
            label,
            2,
            ink,
        )
    for column, label in enumerate(column_labels):
        x = label_w + column * cell_w
        rect(board, board_w, board_h, x, header_h, 1, scaled_h, line)
        label_scale = 2 if len(column_labels) <= 16 else 1
        draw_text(
            board,
            board_w,
            board_h,
            x + (cell_w - text_width(label, label_scale)) // 2,
            18,
            label,
            label_scale,
            ink,
        )
    rect(
        board, board_w, board_h, label_w, header_h, scaled_w, 1, line
    )
    return board_w, board_h, board


def save_board(
    path: Path,
    width: int,
    height: int,
    pixels: bytearray,
    rows: list[str],
    columns: list[str],
) -> None:
    board_w, board_h, board = labelled_board(
        width, height, pixels, rows, columns
    )
    write_ppm(path, board_w, board_h, board)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory: Path = args.directory

    for stem, columns in MATRICES.items():
        width, height, source = read_ppm(
            directory / f"all-{stem}-exact40x30.ppm"
        )
        save_board(
            directory / f"labelled-all-{stem}-exact40x30-4x.ppm",
            width,
            height,
            source,
            VARIANTS,
            columns,
        )
        for family, first_row in FAMILIES:
            family_pixels = crop(
                source, width, 0, first_row * 30, width, 3 * 30
            )
            save_board(
                directory
                / f"labelled-{family}-{stem}-exact40x30-4x.ppm",
                width,
                3 * 30,
                family_pixels,
                VARIANTS[first_row : first_row + 3],
                columns,
            )

        hero_width, hero_height, hero = read_ppm(
            directory / f"hero-mossling-{stem}-exact40x30.ppm"
        )
        save_board(
            directory
            / f"labelled-hero-mossling-{stem}-exact40x30-4x.ppm",
            hero_width,
            hero_height,
            hero,
            HERO_MOSSLING,
            columns,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
