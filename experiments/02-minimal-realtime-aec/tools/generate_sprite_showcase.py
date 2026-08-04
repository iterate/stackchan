#!/usr/bin/env python3
"""Generate two production FSPR V2 talking-portrait atlases.

The artwork is original, dependency-free procedural pixel art.  It is
dedicated to the public domain under CC0 1.0.  No sprites from commercial
games are copied or traced.  The navigator/automaton motifs refine the
earlier CC0 Fable pixel-character and sprite-sheet experiments in this repo;
every shipped pixel is still emitted here from geometric primitives.

The generated atlases deliberately exercise the complete face_sprite_sheet
engine:

* eleven facial-action expression banks matching face_stage.c;
* twenty-three mouth slots (Microsoft22 plus one custom asymmetrical shape);
* OVR15, VRM5, Preston9, Microsoft22, and custom viseme maps;
* four-level eyelids, moving pupils, three-level authored brows;
* deterministic idle acts, breathing, saccades, and automatic blinks;
* trimmed PackBits cells and conservative margins inside an 80x60 canvas.

Run this file whenever the art or the wire mappings change.  The generated
C file is flash-only data; playback still allocates no memory and performs no
floating-point work.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


HERE = Path(__file__).resolve().parent
MAIN_DIR = HERE.parent / "firmware-ws" / "main"
OUT_H = MAIN_DIR / "face_sprite_showcase.h"
OUT_C = MAIN_DIR / "face_sprite_showcase.c"

W = 80
H = 60
TRANSPARENT = 0


@dataclass(frozen=True)
class Expression:
    slug: str
    valence: int
    arousal: int
    corner: int
    brow_inner: int
    squint: int
    mouth_style: str


# These are the eleven resolved action targets from face_stage.c.  The sprite
# engine selects by proximity in action space, never by the stage enum.
EXPRESSIONS = (
    Expression("neutral", 0, 72, 0, 0, 0, "neutral"),
    Expression("warm", 52, 112, 36, 10, 20, "warm"),
    Expression("joy", 94, 184, 78, 22, 68, "joy"),
    Expression("concern", -52, 126, -24, 58, 14, "concern"),
    Expression("surprise", 18, 232, 8, 78, 0, "surprise"),
    Expression("thoughtful", 4, 92, -6, 18, 22, "thoughtful"),
    Expression("skeptical", -18, 104, -12, -14, 42, "skeptical"),
    Expression("determined", 12, 166, -8, -34, 54, "determined"),
    Expression("sleepy", 8, 34, 4, -26, 118, "sleepy"),
    Expression("excited", 86, 250, 62, 52, 14, "excited"),
    Expression("embarrassed", 28, 176, 24, 24, 116, "embarrassed"),
)


def rgb565(color: int) -> int:
    return (
        (((color >> 16) & 0xF8) << 8)
        | (((color >> 8) & 0xFC) << 3)
        | ((color & 0xFF) >> 3)
    )


class Art:
    """Small RGB canvas.  Pixels are 0xRRGGBB or None for transparency."""

    def __init__(
        self, width: int, height: int, fill: int | None = None
    ) -> None:
        self.width = width
        self.height = height
        self.px: list[int | None] = [fill] * (width * height)

    def put(self, x: int, y: int, color: int | None) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.px[y * self.width + x] = color

    def rect(
        self, x: int, y: int, width: int, height: int, color: int | None
    ) -> None:
        for yy in range(y, y + height):
            for xx in range(x, x + width):
                self.put(xx, yy, color)

    def frame(
        self, x: int, y: int, width: int, height: int, color: int
    ) -> None:
        self.rect(x, y, width, 1, color)
        self.rect(x, y + height - 1, width, 1, color)
        self.rect(x, y, 1, height, color)
        self.rect(x + width - 1, y, 1, height, color)

    def hline(self, x: int, y: int, width: int, color: int) -> None:
        self.rect(x, y, width, 1, color)

    def vline(self, x: int, y: int, height: int, color: int) -> None:
        self.rect(x, y, 1, height, color)

    def line(
        self, x0: int, y0: int, x1: int, y1: int, color: int
    ) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        error = dx + dy
        while True:
            self.put(x0, y0, color)
            if x0 == x1 and y0 == y1:
                return
            doubled = error * 2
            if doubled >= dy:
                error += dy
                x0 += sx
            if doubled <= dx:
                error += dx
                y0 += sy

    def disc(
        self,
        cx: int,
        cy: int,
        radius_x: int,
        radius_y: int,
        color: int | None,
    ) -> None:
        if radius_x < 0 or radius_y < 0:
            return
        if radius_x == 0 or radius_y == 0:
            self.put(cx, cy, color)
            return
        limit = radius_x * radius_x * radius_y * radius_y
        for dy in range(-radius_y, radius_y + 1):
            for dx in range(-radius_x, radius_x + 1):
                if (
                    dx * dx * radius_y * radius_y
                    + dy * dy * radius_x * radius_x
                    <= limit
                ):
                    self.put(cx + dx, cy + dy, color)

    def polygon(
        self, points: Iterable[tuple[int, int]], color: int
    ) -> None:
        vertices = tuple(points)
        if len(vertices) < 3:
            return
        min_y = min(point[1] for point in vertices)
        max_y = max(point[1] for point in vertices)
        for y in range(min_y, max_y + 1):
            intersections: list[int] = []
            for index, (x0, y0) in enumerate(vertices):
                x1, y1 = vertices[(index + 1) % len(vertices)]
                if y0 == y1:
                    continue
                if y < min(y0, y1) or y >= max(y0, y1):
                    continue
                x = x0 + ((y - y0) * (x1 - x0)) // (y1 - y0)
                intersections.append(x)
            intersections.sort()
            for index in range(0, len(intersections) - 1, 2):
                self.hline(
                    intersections[index],
                    y,
                    intersections[index + 1] - intersections[index] + 1,
                    color,
                )

    def checker(
        self,
        x: int,
        y: int,
        width: int,
        height: int,
        color: int,
        parity: int = 0,
    ) -> None:
        for yy in range(y, y + height):
            for xx in range(x, x + width):
                if ((xx + yy) & 1) == parity:
                    self.put(xx, yy, color)


def packbits(data: bytes) -> bytes:
    """Encode the exact PackBits dialect decoded by face_sprite_sheet.c."""
    result = bytearray()
    index = 0
    while index < len(data):
        run = 1
        while (
            index + run < len(data)
            and data[index + run] == data[index]
            and run < 128
        ):
            run += 1
        if run >= 3:
            result.append(257 - run)
            result.append(data[index])
            index += run
            continue
        start = index
        index += 1
        while index < len(data) and index - start < 128:
            if (
                index + 2 < len(data)
                and data[index] == data[index + 1] == data[index + 2]
            ):
                break
            index += 1
        literal = data[start:index]
        result.append(len(literal) - 1)
        result.extend(literal)
    return bytes(result)


@dataclass
class Cell:
    width: int
    height: int
    offset_x: int
    offset_y: int
    data: bytes
    encoding: int


class AtlasBuilder:
    def __init__(self, prefix: str, display_name: str, background: int):
        self.prefix = prefix
        self.display_name = display_name
        self.background = rgb565(background)
        self.palette: list[int] = [0]
        self.palette_lookup: dict[int, int] = {}
        self.cells: list[Cell] = []
        self.cell_lookup: dict[tuple[int, int, int, int, bytes], int] = {}
        self.names: dict[str, int] = {}
        self.raw_source_pixels = 0
        self.mouth_arrays: dict[str, list[int]] = {}
        self.banks: list[dict] = []
        self.viseme_map: list[tuple[int, int, int, int]] = []
        self.sequences: list[list[tuple[int, int, int, int]]] = []

    def palette_index(self, color: int) -> int:
        value = rgb565(color)
        existing = self.palette_lookup.get(value)
        if existing is not None:
            return existing
        if len(self.palette) >= 256:
            raise ValueError(f"{self.prefix}: palette overflow")
        index = len(self.palette)
        self.palette_lookup[value] = index
        self.palette.append(value)
        return index

    def add(self, name: str, art: Art) -> int:
        if name in self.names:
            raise ValueError(f"duplicate cell name {name}")
        indices = bytearray(art.width * art.height)
        min_x = art.width
        min_y = art.height
        max_x = -1
        max_y = -1
        for y in range(art.height):
            for x in range(art.width):
                color = art.px[y * art.width + x]
                if color is None:
                    continue
                indices[y * art.width + x] = self.palette_index(color)
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
        if max_x < 0:
            width = height = 1
            offset_x = offset_y = 0
            trimmed = bytes((TRANSPARENT,))
        else:
            width = max_x - min_x + 1
            height = max_y - min_y + 1
            offset_x = min_x
            offset_y = min_y
            trimmed = b"".join(
                bytes(
                    indices[
                        row * art.width + min_x :
                        row * art.width + min_x + width
                    ]
                )
                for row in range(min_y, max_y + 1)
            )
        key = (width, height, offset_x, offset_y, trimmed)
        existing = self.cell_lookup.get(key)
        if existing is not None:
            self.names[name] = existing
            return existing
        encoded = packbits(trimmed)
        encoding = 1
        if len(encoded) >= len(trimmed):
            encoded = trimmed
            encoding = 0
        index = len(self.cells)
        self.cells.append(
            Cell(width, height, offset_x, offset_y, encoded, encoding)
        )
        self.cell_lookup[key] = index
        self.names[name] = index
        self.raw_source_pixels += art.width * art.height
        return index

    def cell(self, name: str) -> int:
        return self.names[name]


def draw_stars(art: Art, color_a: int, color_b: int) -> None:
    """A fixed star/navigation field with no random-build dependency."""
    stars = (
        (6, 6),
        (13, 18),
        (19, 5),
        (66, 7),
        (72, 17),
        (60, 14),
        (7, 35),
        (72, 38),
        (16, 42),
        (64, 44),
    )
    for index, (x, y) in enumerate(stars):
        art.put(x, y, color_a if (index & 1) == 0 else color_b)
        if index in (2, 5):
            art.put(x + 1, y, color_b)
            art.put(x, y + 1, color_b)


# ---------------------------------------------------------------------------
# Shared viseme vocabulary


MOUTH_SLOT_NAMES = (
    "sil",
    "pp",
    "ff",
    "th",
    "dd",
    "kk",
    "ch",
    "ss",
    "nn",
    "rr",
    "aa",
    "e",
    "ih",
    "oh",
    "ou",
    "smile",
    "frown",
    "grin",
    "tongue",
    "pucker",
    "gasp",
    "lateral",
    "smirk",
)

MOUTH_ROLES = (
    0,  # sil -> rest
    1,  # pp -> press
    7,  # ff -> lip bite
    8,  # th -> tongue
    3,  # dd -> half
    3,  # kk -> half
    5,  # ch -> round
    2,  # ss -> teeth
    1,  # nn -> press
    5,  # rr -> round
    4,  # aa -> wide
    4,  # e -> wide
    3,  # ih -> half
    5,  # oh -> round
    6,  # ou -> pucker
    4,  # smile -> wide
    3,  # frown -> half
    2,  # grin -> teeth
    8,  # tongue
    6,  # pucker
    5,  # gasp
    3,  # lateral
    3,  # smirk
)


def add_viseme_maps(builder: AtlasBuilder) -> None:
    # OVR15 enumerator order from face_pose.h.
    ovr_slots = (10, 11, 12, 13, 14, 1, 7, 3, 4, 2, 5, 8, 9, 6, 0)
    for viseme, slot in enumerate(ovr_slots):
        builder.viseme_map.append((0, viseme, slot, MOUTH_ROLES[slot]))

    # VRM A/I/U/E/O.
    for viseme, slot in enumerate((10, 12, 14, 11, 13)):
        builder.viseme_map.append((1, viseme, slot, MOUTH_ROLES[slot]))

    # Preston/Rhubarb X A B C D E F G H.
    for viseme, slot in enumerate((0, 1, 7, 12, 10, 13, 14, 2, 18)):
        builder.viseme_map.append((2, viseme, slot, MOUTH_ROLES[slot]))

    # Microsoft 22 IDs have an intentionally lossless one-to-one path.
    for viseme in range(22):
        builder.viseme_map.append(
            (3, viseme, viseme, MOUTH_ROLES[viseme])
        )

    # One atlas-specific/custom mouth demonstrates the unbounded vocabulary.
    builder.viseme_map.append((255, 42, 22, MOUTH_ROLES[22]))


def mouth_curve(
    art: Art, cx: int, cy: int, half_width: int, curve: int, color: int
) -> None:
    """Three-segment pixel curve; positive curve smiles."""
    end_y = cy - curve
    art.line(cx - half_width, end_y, cx - half_width // 2, cy, color)
    art.line(cx - half_width // 2, cy, cx + half_width // 2, cy, color)
    art.line(cx + half_width // 2, cy, cx + half_width, end_y, color)


# ---------------------------------------------------------------------------
# Atlas one: VGA Star Navigator


ELDER = {
    "bg": 0x080B19,
    "sky0": 0x101A32,
    "sky1": 0x172849,
    "sky2": 0x24456A,
    "star": 0xC8E8FF,
    "star_dim": 0x5CA8C8,
    "grid": 0x2A6080,
    "skin0": 0x4C2C24,
    "skin1": 0x724536,
    "skin2": 0x9B684D,
    "skin3": 0xC3936B,
    "skin4": 0xE4BE91,
    "hair0": 0x4E5260,
    "hair1": 0x7D8490,
    "hair2": 0xB9C0C4,
    "hair3": 0xE7E1D4,
    "coat0": 0x182342,
    "coat1": 0x293A67,
    "coat2": 0x3F5D92,
    "gold0": 0x916C2E,
    "gold1": 0xD8B859,
    "white": 0xF0E8D4,
    "iris": 0x76B7B4,
    "pupil": 0x111018,
    "lip": 0x8D4B49,
    "lip_hi": 0xC46C61,
    "cavity": 0x230E18,
    "teeth": 0xE7DCC5,
    "tongue": 0xB95D61,
    "blush": 0xD77976,
    "cyan": 0x66E0DF,
}


def elder_base() -> Art:
    c = ELDER
    art = Art(W, H)

    # Circular astrogation window and parallax navigation grid.
    # Keep a full native-pixel safety border: these atlases are commonly
    # scaled 2x, so a single touched edge pixel reads as obvious clipping.
    art.disc(40, 25, 37, 24, c["sky0"])
    art.disc(40, 24, 34, 21, c["sky1"])
    art.disc(40, 23, 29, 17, c["sky0"])
    draw_stars(art, c["star"], c["star_dim"])
    art.line(5, 30, 20, 27, c["grid"])
    art.line(60, 27, 75, 30, c["grid"])
    art.line(4, 34, 18, 31, c["grid"])
    art.line(62, 31, 76, 34, c["grid"])
    art.disc(40, 23, 24, 13, c["sky1"])
    for x in (16, 64):
        art.vline(x, 12, 23, c["sky2"])
    art.hline(9, 43, 62, c["sky2"])

    # Coat and raised navigator collar stay three pixels inside the canvas.
    art.disc(40, 50, 31, 6, c["coat0"])
    art.disc(40, 51, 26, 5, c["coat1"])
    art.polygon(((15, 56), (29, 45), (37, 56)), c["coat2"])
    art.polygon(((65, 56), (51, 45), (43, 56)), c["coat0"])
    art.line(18, 55, 32, 47, c["gold0"])
    art.line(62, 55, 48, 47, c["gold0"])
    art.disc(40, 53, 3, 3, c["gold0"])
    art.disc(40, 52, 1, 1, c["gold1"])

    # Hair silhouette, ears, and banded VGA-era skin ramps.
    art.disc(40, 27, 23, 25, c["hair0"])
    art.disc(21, 29, 5, 10, c["hair1"])
    art.disc(59, 29, 5, 10, c["hair0"])
    art.disc(40, 26, 20, 23, c["skin0"])
    art.disc(38, 24, 18, 21, c["skin1"])
    art.disc(36, 22, 14, 17, c["skin2"])
    art.disc(34, 19, 9, 11, c["skin3"])
    art.disc(20, 29, 3, 6, c["skin2"])
    art.disc(60, 29, 3, 6, c["skin1"])
    art.put(20, 29, c["skin0"])
    art.put(60, 29, c["skin0"])

    # Swept silver hair and a tiny navigator earpiece.
    for index in range(6):
        art.line(
            24 + index,
            8 + (index & 1),
            18 + index // 2,
            25 + index,
            c["hair2"] if index < 3 else c["hair1"],
        )
    for index in range(4):
        art.line(
            55 - index,
            9,
            61 - index // 2,
            27 + index,
            c["hair1"],
        )
    art.rect(58, 24, 4, 10, c["coat0"])
    art.rect(59, 25, 2, 7, c["cyan"])
    art.put(62, 27, c["gold1"])

    # Deep eye sockets; replacement patches supply whites/lids/pupils.
    art.disc(31, 25, 8, 5, c["skin0"])
    art.disc(49, 25, 8, 5, c["skin0"])

    # Long asymmetric nose, wrinkles, and cheek planes.
    art.line(40, 25, 38, 34, c["skin0"])
    art.line(39, 27, 37, 34, c["skin3"])
    art.disc(39, 35, 4, 2, c["skin2"])
    art.put(36, 35, c["skin0"])
    art.put(41, 35, c["skin0"])
    art.hline(31, 14, 19, c["skin1"])
    art.hline(33, 17, 15, c["skin1"])
    art.put(52, 31, c["skin0"])
    art.put(54, 33, c["skin0"])

    # Beard mass.  The mouth is intentionally absent and overlays cleanly.
    art.disc(40, 42, 23, 14, c["hair0"])
    art.disc(39, 42, 20, 13, c["hair1"])
    art.disc(38, 45, 16, 11, c["hair2"])
    art.disc(40, 48, 11, 8, c["hair1"])
    for x in range(21, 60, 3):
        length = 3 + ((x * 7) % 6)
        art.vline(
            x,
            46 + ((x // 3) & 1),
            length,
            c["hair3"] if (x & 2) else c["hair0"],
        )

    # Keep a calm, uncluttered opening for the 23 mouth replacements.
    art.disc(40, 41, 15, 7, c["hair1"])
    return art


ELDER_EYES: dict[str, tuple[tuple[int, int], tuple[int, int]]] = {
    "neutral": ((6, 3), (6, 3)),
    "warm": ((6, 2), (6, 2)),
    "joy": ((6, 1), (6, 1)),
    "concern": ((6, 3), (6, 4)),
    "surprise": ((6, 4), (6, 4)),
    "thoughtful": ((6, 2), (6, 3)),
    "skeptical": ((6, 1), (6, 3)),
    "determined": ((6, 2), (6, 2)),
    "sleepy": ((6, 1), (6, 1)),
    "excited": ((6, 4), (6, 4)),
    "embarrassed": ((6, 1), (6, 1)),
}


def elder_eye(expression: str, side: int, lid: int) -> Art:
    c = ELDER
    art = Art(14, 10)
    radius_x, base_radius_y = ELDER_EYES[expression][side]
    if expression == "skeptical" and side == 0:
        base_radius_y = 1
    if expression == "thoughtful" and side == 0:
        base_radius_y = 2
    visible = max(0, 3 - lid)
    if visible == 0 or expression in ("joy", "embarrassed") and lid >= 1:
        y = 5
        curve = 1 if expression in ("joy", "embarrassed", "warm") else 0
        mouth_curve(art, 7, y, 5, curve, c["hair0"])
        if expression == "joy":
            art.put(2, y - 1, c["hair2"])
            art.put(12, y - 1, c["hair2"])
        return art

    radius_y = max(1, (base_radius_y * visible + 1) // 3)
    center_y = 5
    if expression == "concern" and side == 1:
        center_y += 1
    if expression == "determined":
        center_y += 1
    art.disc(7, center_y, radius_x + 1, radius_y + 1, c["skin0"])
    art.disc(7, center_y, radius_x, radius_y, c["white"])
    art.hline(2, center_y + radius_y, 11, c["skin0"])
    if expression == "surprise":
        art.put(1, center_y, c["skin4"])
        art.put(13, center_y, c["skin4"])
    if expression == "excited":
        art.put(3, center_y - radius_y, c["star"])
    return art


ELDER_BROWS: dict[str, tuple[tuple[int, int], tuple[int, int]]] = {
    "neutral": ((0, 0), (0, 0)),
    "warm": ((0, -1), (-1, 0)),
    "joy": ((1, -1), (-1, 1)),
    "concern": ((1, -2), (-2, 1)),
    "surprise": ((0, 0), (0, 0)),
    "thoughtful": ((-1, 1), (-1, 0)),
    "skeptical": ((1, -1), (0, 1)),
    "determined": ((-2, 1), (1, -2)),
    "sleepy": ((1, 0), (0, 1)),
    "excited": ((1, -1), (-1, 1)),
    "embarrassed": ((1, -1), (-1, 1)),
}


def elder_brow(expression: str, side: int, level: int) -> Art:
    c = ELDER
    art = Art(15, 7)
    y_left, y_right = ELDER_BROWS[expression][side]
    shift = 1 - level
    y0 = 3 + y_left + shift
    y1 = 3 + y_right + shift
    art.line(1, y0, 13, y1, c["hair0"])
    art.line(2, y0 - 1, 12, y1 - 1, c["hair2"])
    if expression in ("surprise", "excited"):
        art.put(7, min(y0, y1) - 1, c["hair3"])
    return art


def elder_mouth(slot: int, style: str) -> Art:
    c = ELDER
    art = Art(28, 15)
    cx = 14
    cy = 7
    smile = {
        "neutral": 0,
        "warm": 1,
        "joy": 2,
        "concern": -1,
        "surprise": 0,
        "thoughtful": 0,
        "skeptical": 0,
        "determined": -1,
        "sleepy": 0,
        "excited": 2,
        "embarrassed": 1,
    }[style]
    if style == "sleepy":
        cy += 1
    if style == "skeptical":
        cx += 1

    def open_mouth(
        rx: int,
        ry: int,
        *,
        teeth: bool = False,
        tongue: bool = False,
        rounder: bool = False,
    ) -> None:
        if rounder:
            rx = min(rx, ry + 2)
        art.disc(cx, cy, rx + 1, ry + 1, c["lip"])
        art.disc(cx, cy, rx, ry, c["cavity"])
        if teeth:
            art.rect(cx - max(1, rx - 2), cy - ry + 1, max(2, 2 * rx - 3), 2, c["teeth"])
            art.hline(cx - max(1, rx - 2), cy - ry + 3, max(2, 2 * rx - 3), c["skin0"])
        if tongue:
            art.disc(cx, cy + max(1, ry - 2), max(2, rx - 3), 2, c["tongue"])
            art.hline(cx - max(1, rx - 4), cy + max(0, ry - 2), max(2, 2 * rx - 7), c["lip_hi"])
        if smile > 0:
            art.put(cx - rx - 1, cy - 1, c["lip_hi"])
            art.put(cx + rx + 1, cy - 1, c["lip_hi"])
        elif smile < 0:
            art.put(cx - rx - 1, cy + 1, c["lip"])
            art.put(cx + rx + 1, cy + 1, c["lip"])

    if slot == 0:
        mouth_curve(art, cx, cy, 7, smile, c["cavity"])
        art.hline(cx - 4, cy + 1, 9, c["lip"])
    elif slot == 1:
        mouth_curve(art, cx, cy - 1, 6, smile, c["lip_hi"])
        mouth_curve(art, cx, cy + 1, 7, smile, c["lip"])
        art.hline(cx - 5, cy, 11, c["cavity"])
    elif slot == 2:
        art.rect(cx - 6, cy - 2, 12, 3, c["lip"])
        art.rect(cx - 5, cy - 1, 10, 2, c["teeth"])
        mouth_curve(art, cx, cy + 2, 7, smile, c["lip_hi"])
    elif slot == 3:
        open_mouth(6, 3, teeth=True, tongue=True)
        art.rect(cx - 1, cy - 1, 3, 4, c["tongue"])
    elif slot == 4:
        open_mouth(6, 3, teeth=True)
    elif slot == 5:
        open_mouth(7, 3)
        art.put(cx - 5, cy - 2, c["teeth"])
        art.put(cx + 4, cy - 2, c["teeth"])
    elif slot == 6:
        open_mouth(5, 4, rounder=True)
        art.hline(cx - 2, cy - 2, 5, c["teeth"])
    elif slot == 7:
        open_mouth(8, 2, teeth=True)
    elif slot == 8:
        mouth_curve(art, cx, cy, 6, smile, c["cavity"])
        art.put(cx, cy + 1, c["lip_hi"])
    elif slot == 9:
        open_mouth(5, 3, rounder=True)
        art.put(cx, cy - 2, c["teeth"])
    elif slot == 10:
        open_mouth(9 if style in ("joy", "excited") else 8, 5, tongue=True)
        art.hline(cx - 5, cy - 4, 11, c["teeth"])
    elif slot == 11:
        open_mouth(9, 3, teeth=True)
        art.hline(cx - 7, cy + 2, 15, c["tongue"])
    elif slot == 12:
        open_mouth(7, 2)
        art.hline(cx - 4, cy - 1, 9, c["teeth"])
    elif slot == 13:
        open_mouth(5, 5, rounder=True)
    elif slot == 14:
        open_mouth(3, 4, rounder=True)
        art.disc(cx, cy, 1, 2, c["cavity"])
    elif slot == 15:
        mouth_curve(art, cx, cy + 1, 8, 2, c["lip_hi"])
        mouth_curve(art, cx, cy + 2, 7, 2, c["cavity"])
    elif slot == 16:
        mouth_curve(art, cx, cy - 1, 8, -2, c["cavity"])
        art.put(cx - 8, cy + 2, c["lip"])
        art.put(cx + 8, cy + 2, c["lip"])
    elif slot == 17:
        open_mouth(9, 4, teeth=True)
        art.rect(cx - 6, cy, 13, 1, c["skin0"])
    elif slot == 18:
        open_mouth(7, 4, teeth=True, tongue=True)
        art.rect(cx - 2, cy - 1, 4, 5, c["tongue"])
    elif slot == 19:
        art.disc(cx, cy, 4, 3, c["lip_hi"])
        art.disc(cx, cy, 2, 1, c["cavity"])
        art.put(cx - 5, cy, c["lip"])
        art.put(cx + 5, cy, c["lip"])
    elif slot == 20:
        open_mouth(4, 6, rounder=True)
        art.put(cx - 1, cy - 4, c["teeth"])
    elif slot == 21:
        open_mouth(8, 3, tongue=True)
        art.line(cx - 7, cy - 2, cx + 5, cy + 2, c["lip_hi"])
    else:
        art.line(cx - 8, cy + 1, cx + 2, cy + 2, c["cavity"])
        art.line(cx + 2, cy + 2, cx + 8, cy - 1, c["lip_hi"])
        art.put(cx + 9, cy - 2, c["teeth"])
    return art


def elder_overlay(expression: str) -> Art:
    c = ELDER
    art = Art(W, H)

    # Moustache is a top layer so open mouths never paint over it.
    curve = 1 if expression in ("warm", "joy", "excited") else 0
    if expression in ("concern", "determined"):
        curve = -1
    art.line(40, 38, 33, 37 - curve, c["hair3"])
    art.line(33, 37 - curve, 26, 39 - curve, c["hair2"])
    art.line(40, 38, 47, 37 - curve, c["hair3"])
    art.line(47, 37 - curve, 54, 39 - curve, c["hair2"])
    art.put(39, 39, c["hair0"])
    art.put(40, 39, c["hair0"])

    if expression == "neutral":
        art.put(63, 27, c["cyan"])
    elif expression == "warm":
        art.hline(25, 33, 3, c["blush"])
        art.hline(52, 33, 3, c["blush"])
    elif expression == "joy":
        for x, y in ((17, 20), (61, 14), (64, 17)):
            art.put(x, y, c["gold1"])
            art.put(x - 1, y, c["gold0"])
            art.put(x + 1, y, c["gold0"])
            art.put(x, y - 1, c["gold0"])
            art.put(x, y + 1, c["gold0"])
        art.line(23, 26, 20, 24, c["hair2"])
        art.line(57, 26, 60, 24, c["hair2"])
    elif expression == "concern":
        art.put(54, 29, c["cyan"])
        art.put(54, 30, c["cyan"])
        art.put(55, 31, c["star"])
        art.line(32, 17, 37, 15, c["skin0"])
    elif expression == "surprise":
        art.line(33, 11, 39, 9, c["skin4"])
        art.line(41, 9, 47, 11, c["skin4"])
        art.put(17, 14, c["star"])
    elif expression == "thoughtful":
        art.disc(63, 15, 5, 5, c["sky2"])
        art.frame(60, 12, 6, 6, c["cyan"])
        art.put(63, 15, c["star"])
        art.line(52, 33, 56, 31, c["skin0"])
    elif expression == "skeptical":
        art.disc(49, 25, 9, 6, c["gold0"])
        art.disc(49, 25, 8, 5, None)
        art.line(56, 29, 62, 35, c["gold1"])
        art.put(25, 32, c["skin0"])
    elif expression == "determined":
        art.line(35, 12, 40, 15, c["skin0"])
        art.line(45, 12, 40, 15, c["skin0"])
        art.hline(34, 18, 13, c["skin0"])
    elif expression == "sleepy":
        for x, y, size in ((59, 11, 3), (65, 7, 4), (71, 3, 5)):
            art.hline(x, y, size, c["star_dim"])
            art.line(x + size - 1, y, x, y + 3, c["star_dim"])
            art.hline(x, y + 3, size, c["star_dim"])
    elif expression == "excited":
        for x, y in ((12, 13), (17, 9), (62, 10), (68, 15)):
            art.hline(x - 2, y, 5, c["cyan"])
            art.vline(x, y - 2, 5, c["star"])
        art.hline(25, 33, 4, c["blush"])
        art.hline(51, 33, 4, c["blush"])
    elif expression == "embarrassed":
        for x0 in (24, 50):
            for offset in range(3):
                art.line(
                    x0 + offset * 2,
                    31,
                    x0 - 2 + offset * 2,
                    35,
                    c["blush"],
                )
        art.put(58, 18, c["cyan"])
        art.put(58, 19, c["cyan"])
    return art


def build_elder() -> AtlasBuilder:
    builder = AtlasBuilder(
        "vga_star_navigator", "VGA Star Navigator", ELDER["bg"]
    )
    builder.add("base", elder_base())
    pupil = Art(3, 3)
    pupil.disc(1, 1, 1, 1, ELDER["iris"])
    pupil.put(1, 1, ELDER["pupil"])
    pupil.put(0, 0, ELDER["star"])
    builder.add("pupil", pupil)

    for expression in EXPRESSIONS:
        slug = expression.slug
        mouth_cells = []
        for slot, mouth_name in enumerate(MOUTH_SLOT_NAMES):
            name = f"mouth_{expression.mouth_style}_{mouth_name}"
            if name not in builder.names:
                builder.add(
                    name, elder_mouth(slot, expression.mouth_style)
                )
            mouth_cells.append(builder.cell(name))
        builder.mouth_arrays[slug] = mouth_cells

        eyes: list[list[int]] = [[], []]
        brows: list[list[int]] = [[], []]
        for side in range(2):
            for lid in range(4):
                name = f"eye_{slug}_{side}_{lid}"
                builder.add(name, elder_eye(slug, side, lid))
                eyes[side].append(builder.cell(name))
            for level in range(3):
                name = f"brow_{slug}_{side}_{level}"
                builder.add(name, elder_brow(slug, side, level))
                brows[side].append(builder.cell(name))
        overlay_name = f"overlay_{slug}"
        builder.add(overlay_name, elder_overlay(slug))
        no_pupil = slug in ("joy", "sleepy", "embarrassed")
        builder.banks.append(
            {
                "expression": expression,
                "base": builder.cell("base"),
                "overlay": builder.cell(overlay_name),
                "mouth_array": slug,
                "mouth_anchor": (26, 34),
                "eyes": eyes,
                "eye_anchors": ((24, 20), (42, 20)),
                "pupils": (
                    -1 if no_pupil else builder.cell("pupil"),
                    -1 if no_pupil else builder.cell("pupil"),
                ),
                "pupil_data": (
                    (30, 24, 28, 23, 32, 26, 2, 1),
                    (48, 24, 46, 23, 50, 26, 2, 1),
                ),
                "brows": brows,
                "brow_anchors": ((23, 13), (42, 13)),
                "brow_lift": 2,
            }
        )

    # A lens glint idle act.  All art remains inside the safe margin.
    for phase in range(3):
        glint = Art(7, 7)
        glint.put(3, 3, ELDER["star"])
        if phase >= 1:
            glint.hline(1, 3, 5, ELDER["cyan"])
            glint.vline(3, 1, 5, ELDER["star"])
        if phase == 2:
            glint.put(1, 1, ELDER["gold1"])
            glint.put(5, 5, ELDER["gold1"])
        builder.add(f"glint_{phase}", glint)
    builder.sequences.append(
        [
            (builder.cell("glint_0"), 1120, 53, 18),
            (builder.cell("glint_1"), 1440, 53, 18),
            (builder.cell("glint_2"), 1920, 53, 18),
            (builder.cell("glint_1"), 1120, 53, 18),
        ]
    )
    add_viseme_maps(builder)
    return builder


# ---------------------------------------------------------------------------
# Atlas two: Pocket Relay Creature


ROBOT = {
    "bg": 0x07131A,
    "grid": 0x0E2830,
    "shell0": 0x103C43,
    "shell1": 0x17616A,
    "shell2": 0x2A8790,
    "shell3": 0x73C8C1,
    "screen0": 0x071816,
    "screen1": 0x0D2924,
    "screen2": 0x123D34,
    "ink": 0x06100F,
    "eye": 0xB8FF9A,
    "eye_hi": 0xF0FFE0,
    "mouth": 0xFF7C58,
    "mouth_hi": 0xFFB065,
    "teeth": 0xFFF2C2,
    "tongue": 0xD94C64,
    "amber": 0xF4C84B,
    "red": 0xE84A45,
    "violet": 0x9A77D8,
    "cyan": 0x44E5DB,
    "dim": 0x416D68,
}


def robot_base() -> Art:
    c = ROBOT
    art = Art(W, H)

    # Sparse circuit-board backdrop.
    for y in (8, 16, 44, 52):
        art.hline(3, y, 74, c["grid"])
    for x in (7, 19, 61, 73):
        art.vline(x, 5, 50, c["grid"])
    for x, y in ((7, 8), (19, 16), (61, 8), (73, 44), (7, 52)):
        art.disc(x, y, 1, 1, c["dim"])

    # Antenna and ear relays give a compact creature silhouette.
    art.vline(40, 3, 7, c["shell3"])
    art.line(40, 3, 45, 2, c["shell2"])
    art.disc(46, 2, 2, 1, c["amber"])
    art.disc(12, 29, 8, 12, c["shell0"])
    art.disc(68, 29, 8, 12, c["shell0"])
    art.disc(12, 29, 5, 9, c["shell2"])
    art.disc(68, 29, 5, 9, c["shell2"])
    art.disc(11, 29, 2, 5, c["screen1"])
    art.disc(69, 29, 2, 5, c["screen1"])

    # Rounded portable chassis, face glass, and chunky specular bands.
    art.disc(40, 30, 29, 23, c["shell0"])
    art.disc(40, 28, 27, 21, c["shell1"])
    art.disc(38, 26, 23, 17, c["shell2"])
    art.polygon(((19, 17), (27, 10), (54, 10), (62, 18)), c["shell3"])
    art.disc(40, 31, 22, 17, c["screen0"])
    art.rect(20, 23, 40, 17, c["screen0"])
    art.disc(40, 30, 19, 14, c["screen1"])
    art.rect(22, 24, 36, 15, c["screen1"])
    art.frame(21, 21, 38, 23, c["ink"])
    art.hline(23, 22, 34, c["screen2"])
    for x in range(25, 57, 4):
        art.put(x, 42, c["screen2"])

    # Nose/sensor and cheek vents stay clear of replaceable features.
    art.disc(40, 32, 2, 2, c["amber"])
    art.put(39, 31, c["mouth_hi"])
    for y in (33, 36, 39):
        art.hline(22, y, 3, c["dim"])
        art.hline(55, y, 3, c["dim"])

    # Body yokes under the head, with ample lower clipping margin.
    art.rect(34, 49, 12, 4, c["shell0"])
    art.disc(40, 51, 23, 5, c["shell0"])
    art.disc(40, 52, 19, 4, c["shell1"])
    art.polygon(((25, 56), (33, 51), (39, 56)), c["shell2"])
    art.polygon(((55, 56), (47, 51), (41, 56)), c["shell0"])
    art.rect(36, 53, 8, 4, c["screen0"])
    art.put(38, 55, c["cyan"])
    art.put(41, 55, c["amber"])
    return art


ROBOT_EYES: dict[str, tuple[tuple[int, int], tuple[int, int]]] = {
    "neutral": ((5, 3), (5, 3)),
    "warm": ((5, 2), (5, 2)),
    "joy": ((5, 1), (5, 1)),
    "concern": ((5, 3), (5, 4)),
    "surprise": ((5, 4), (5, 4)),
    "thoughtful": ((5, 2), (5, 3)),
    "skeptical": ((5, 1), (5, 3)),
    "determined": ((5, 2), (5, 2)),
    "sleepy": ((5, 1), (5, 1)),
    "excited": ((5, 4), (5, 4)),
    "embarrassed": ((5, 1), (5, 1)),
}


def robot_eye(expression: str, side: int, lid: int) -> Art:
    c = ROBOT
    art = Art(12, 9)
    radius_x, base_radius_y = ROBOT_EYES[expression][side]
    visible = max(0, 3 - lid)
    if visible == 0 or expression in ("joy", "embarrassed") and lid >= 1:
        curve = 1 if expression in ("joy", "embarrassed", "warm") else 0
        mouth_curve(art, 6, 5, 4, curve, c["eye"])
        art.put(6, 6, c["screen2"])
        return art
    radius_y = max(1, (base_radius_y * visible + 1) // 3)
    cy = 4 + (1 if expression == "determined" else 0)
    # Cozmo-like cubic silhouette, but authored as chunky pixel polygons.
    if radius_y >= 3:
        points = (
            (1, cy - radius_y + 1),
            (3, cy - radius_y),
            (10, cy - radius_y + 1),
            (11, cy),
            (9, cy + radius_y),
            (2, cy + radius_y - 1),
            (1, cy + 1),
        )
        art.polygon(points, c["eye"])
        art.rect(3, cy - radius_y + 1, 6, max(1, 2 * radius_y - 1), c["eye_hi"])
    else:
        art.rect(1, cy - radius_y, 10, radius_y * 2 + 1, c["eye"])
        art.rect(3, cy, 6, 1, c["eye_hi"])
    if expression == "excited":
        art.put(2, cy - radius_y, c["cyan"])
        art.put(9, cy - radius_y + 1, c["cyan"])
    return art


ROBOT_BROWS: dict[str, tuple[tuple[int, int], tuple[int, int]]] = {
    "neutral": ((0, 0), (0, 0)),
    "warm": ((0, -1), (-1, 0)),
    "joy": ((1, -1), (-1, 1)),
    "concern": ((1, -2), (-2, 1)),
    "surprise": ((0, 0), (0, 0)),
    "thoughtful": ((-1, 1), (-1, 0)),
    "skeptical": ((1, -1), (0, 1)),
    "determined": ((-2, 1), (1, -2)),
    "sleepy": ((1, 0), (0, 1)),
    "excited": ((1, -1), (-1, 1)),
    "embarrassed": ((1, -1), (-1, 1)),
}


def robot_brow(expression: str, side: int, level: int) -> Art:
    c = ROBOT
    art = Art(12, 6)
    y0, y1 = ROBOT_BROWS[expression][side]
    shift = 1 - level
    y0 += 3 + shift
    y1 += 3 + shift
    art.line(1, y0, 10, y1, c["mouth"])
    art.line(2, y0 - 1, 9, y1 - 1, c["mouth_hi"])
    if expression in ("surprise", "excited"):
        art.put(5, min(y0, y1) - 1, c["amber"])
    return art


def robot_mouth(slot: int, style: str) -> Art:
    c = ROBOT
    art = Art(28, 13)
    cx = 14
    cy = 6
    smile = {
        "neutral": 0,
        "warm": 1,
        "joy": 2,
        "concern": -1,
        "surprise": 0,
        "thoughtful": 0,
        "skeptical": 0,
        "determined": -1,
        "sleepy": 0,
        "excited": 2,
        "embarrassed": 1,
    }[style]
    if style == "skeptical":
        cx += 1
    if style == "sleepy":
        cy += 1

    def cavity(
        width: int,
        height: int,
        *,
        teeth: bool = False,
        tongue: bool = False,
        rounder: bool = False,
    ) -> None:
        if rounder:
            width = min(width, height + 2)
        x = cx - width // 2
        y = cy - height // 2
        art.rect(x - 1, y, width + 2, height, c["mouth"])
        art.rect(x, y - 1, width, height + 2, c["mouth"])
        art.rect(x, y, width, height, c["ink"])
        if teeth:
            art.rect(x + 1, y, max(1, width - 2), 2, c["teeth"])
            for divider in range(x + 4, x + width - 1, 4):
                art.put(divider, y + 1, c["screen0"])
        if tongue:
            art.rect(x + 2, y + height - 2, max(2, width - 4), 2, c["tongue"])
        if smile:
            corner_y = y if smile > 0 else y + height - 1
            art.put(x - 2, corner_y, c["mouth_hi"])
            art.put(x + width + 1, corner_y, c["mouth_hi"])

    if slot == 0:
        mouth_curve(art, cx, cy, 6, smile, c["mouth"])
        art.put(cx, cy + 1, c["mouth_hi"])
    elif slot == 1:
        art.hline(cx - 6, cy - 1, 13, c["mouth_hi"])
        art.hline(cx - 7, cy + 1, 15, c["mouth"])
        art.hline(cx - 5, cy, 11, c["ink"])
    elif slot == 2:
        art.rect(cx - 6, cy - 2, 12, 3, c["teeth"])
        art.hline(cx - 7, cy - 2, 14, c["mouth"])
        art.hline(cx - 6, cy + 1, 12, c["mouth_hi"])
    elif slot == 3:
        cavity(11, 5, teeth=True, tongue=True)
        art.rect(cx - 1, cy, 3, 4, c["tongue"])
    elif slot == 4:
        cavity(12, 5, teeth=True)
    elif slot == 5:
        cavity(14, 5)
        art.put(cx - 5, cy - 1, c["teeth"])
        art.put(cx + 5, cy - 1, c["teeth"])
    elif slot == 6:
        cavity(9, 7, rounder=True)
    elif slot == 7:
        cavity(17, 4, teeth=True)
    elif slot == 8:
        art.hline(cx - 6, cy, 13, c["mouth"])
        art.put(cx - 3, cy + 1, c["mouth_hi"])
        art.put(cx + 3, cy + 1, c["mouth_hi"])
    elif slot == 9:
        cavity(9, 6, rounder=True)
        art.put(cx, cy, c["mouth"])
    elif slot == 10:
        cavity(19 if style in ("joy", "excited") else 17, 9, tongue=True)
        art.hline(cx - 6, cy - 4, 13, c["teeth"])
    elif slot == 11:
        cavity(19, 6, teeth=True)
    elif slot == 12:
        cavity(15, 4)
        art.hline(cx - 4, cy - 1, 9, c["teeth"])
    elif slot == 13:
        cavity(9, 9, rounder=True)
    elif slot == 14:
        cavity(6, 8, rounder=True)
    elif slot == 15:
        mouth_curve(art, cx, cy + 1, 9, 2, c["mouth_hi"])
        mouth_curve(art, cx, cy + 2, 7, 2, c["mouth"])
    elif slot == 16:
        mouth_curve(art, cx, cy - 1, 9, -2, c["mouth"])
        art.put(cx - 9, cy + 2, c["mouth_hi"])
        art.put(cx + 9, cy + 2, c["mouth_hi"])
    elif slot == 17:
        cavity(20, 7, teeth=True)
        art.hline(cx - 7, cy, 15, c["screen0"])
    elif slot == 18:
        cavity(14, 8, teeth=True, tongue=True)
        art.rect(cx - 2, cy - 1, 4, 5, c["tongue"])
    elif slot == 19:
        art.rect(cx - 4, cy - 2, 9, 5, c["mouth"])
        art.rect(cx - 2, cy - 1, 5, 3, c["ink"])
        art.put(cx - 5, cy, c["mouth_hi"])
        art.put(cx + 5, cy, c["mouth_hi"])
    elif slot == 20:
        cavity(8, 11, rounder=True)
        art.put(cx - 1, cy - 3, c["teeth"])
        art.put(cx + 1, cy - 3, c["teeth"])
    elif slot == 21:
        cavity(17, 6, tongue=True)
        art.line(cx - 8, cy - 2, cx + 6, cy + 2, c["mouth_hi"])
    else:
        art.line(cx - 9, cy + 1, cx + 2, cy + 2, c["mouth"])
        art.line(cx + 2, cy + 2, cx + 9, cy - 2, c["mouth_hi"])
        art.put(cx + 7, cy - 1, c["teeth"])
    return art


def robot_overlay(expression: str) -> Art:
    c = ROBOT
    art = Art(W, H)
    if expression == "neutral":
        art.put(39, 47, c["cyan"])
        art.put(42, 47, c["dim"])
    elif expression == "warm":
        art.rect(24, 37, 3, 2, c["mouth"])
        art.rect(53, 37, 3, 2, c["mouth"])
        art.put(46, 2, c["mouth_hi"])
    elif expression == "joy":
        for x, y in ((16, 15), (64, 15)):
            art.put(x, y, c["amber"])
            art.hline(x - 2, y, 5, c["mouth_hi"])
            art.vline(x, y - 2, 5, c["amber"])
        art.rect(24, 37, 4, 2, c["mouth"])
        art.rect(52, 37, 4, 2, c["mouth"])
    elif expression == "concern":
        art.put(56, 31, c["cyan"])
        art.put(56, 32, c["cyan"])
        art.put(57, 33, c["eye_hi"])
        art.put(46, 2, c["violet"])
    elif expression == "surprise":
        art.hline(34, 15, 12, c["amber"])
        art.put(32, 15, c["mouth_hi"])
        art.put(48, 15, c["mouth_hi"])
        art.disc(46, 2, 2, 1, c["red"])
    elif expression == "thoughtful":
        art.frame(61, 12, 7, 7, c["cyan"])
        art.put(64, 15, c["eye_hi"])
        art.line(57, 32, 61, 29, c["dim"])
    elif expression == "skeptical":
        art.frame(44, 23, 13, 9, c["violet"])
        art.line(57, 31, 63, 36, c["violet"])
        art.put(24, 37, c["mouth"])
    elif expression == "determined":
        art.hline(28, 17, 24, c["red"])
        art.put(46, 2, c["red"])
        art.rect(37, 46, 6, 2, c["amber"])
    elif expression == "sleepy":
        for x, y, size in ((58, 13, 3), (64, 9, 4), (70, 5, 5)):
            art.hline(x, y, size, c["dim"])
            art.line(x + size - 1, y, x, y + 3, c["dim"])
            art.hline(x, y + 3, size, c["dim"])
        art.put(46, 2, c["dim"])
    elif expression == "excited":
        for x, y in ((12, 13), (18, 8), (62, 8), (68, 14)):
            art.hline(x - 2, y, 5, c["cyan"])
            art.vline(x, y - 2, 5, c["eye_hi"])
        art.disc(46, 2, 2, 1, c["mouth_hi"])
        art.rect(24, 37, 4, 2, c["mouth"])
        art.rect(52, 37, 4, 2, c["mouth"])
    elif expression == "embarrassed":
        for x0 in (23, 52):
            art.line(x0, 36, x0 + 3, 39, c["mouth"])
            art.line(x0 + 3, 36, x0 + 6, 39, c["mouth"])
        art.put(58, 18, c["cyan"])
        art.put(58, 19, c["eye_hi"])
        art.put(46, 2, c["violet"])
    return art


def build_robot() -> AtlasBuilder:
    builder = AtlasBuilder(
        "pocket_relay_creature", "Pocket Relay Creature", ROBOT["bg"]
    )
    builder.add("base", robot_base())
    pupil = Art(3, 3)
    pupil.rect(0, 0, 3, 3, ROBOT["screen0"])
    pupil.put(1, 1, ROBOT["cyan"])
    pupil.put(0, 0, ROBOT["eye_hi"])
    builder.add("pupil", pupil)

    for expression in EXPRESSIONS:
        slug = expression.slug
        mouth_cells = []
        for slot, mouth_name in enumerate(MOUTH_SLOT_NAMES):
            name = f"mouth_{expression.mouth_style}_{mouth_name}"
            if name not in builder.names:
                builder.add(
                    name, robot_mouth(slot, expression.mouth_style)
                )
            mouth_cells.append(builder.cell(name))
        builder.mouth_arrays[slug] = mouth_cells

        eyes: list[list[int]] = [[], []]
        brows: list[list[int]] = [[], []]
        for side in range(2):
            for lid in range(4):
                name = f"eye_{slug}_{side}_{lid}"
                builder.add(name, robot_eye(slug, side, lid))
                eyes[side].append(builder.cell(name))
            for level in range(3):
                name = f"brow_{slug}_{side}_{level}"
                builder.add(name, robot_brow(slug, side, level))
                brows[side].append(builder.cell(name))
        overlay_name = f"overlay_{slug}"
        builder.add(overlay_name, robot_overlay(slug))
        no_pupil = slug in ("joy", "sleepy", "embarrassed")
        builder.banks.append(
            {
                "expression": expression,
                "base": builder.cell("base"),
                "overlay": builder.cell(overlay_name),
                "mouth_array": slug,
                "mouth_anchor": (26, 35),
                "eyes": eyes,
                "eye_anchors": ((25, 23), (43, 23)),
                "pupils": (
                    -1 if no_pupil else builder.cell("pupil"),
                    -1 if no_pupil else builder.cell("pupil"),
                ),
                "pupil_data": (
                    (30, 26, 28, 25, 32, 27, 2, 1),
                    (48, 26, 46, 25, 50, 27, 2, 1),
                ),
                "brows": brows,
                "brow_anchors": ((25, 17), (43, 17)),
                "brow_lift": 2,
            }
        )

    # Antenna heartbeat, permitted while speaking.
    for phase in range(3):
        pulse = Art(7, 5)
        pulse.disc(3, 2, phase + 1, max(1, phase), ROBOT["mouth"])
        pulse.put(3, 2, ROBOT["eye_hi"])
        builder.add(f"pulse_{phase}", pulse)
    builder.sequences.append(
        [
            (builder.cell("pulse_0"), 960, 43, 0),
            (builder.cell("pulse_1"), 1120, 43, 0),
            (builder.cell("pulse_2"), 1440, 43, 0),
            (builder.cell("pulse_1"), 960, 43, 0),
            (builder.cell("pulse_0"), 2400, 43, 0),
        ]
    )
    add_viseme_maps(builder)
    return builder


# ---------------------------------------------------------------------------
# C emission


def cell_token(cell: int) -> str:
    return "FACE_SPRITE_CELL_NONE" if cell < 0 else str(cell)


def bytes_rows(data: bytes, indent: str = "    ") -> str:
    rows = []
    for index in range(0, len(data), 16):
        rows.append(
            indent
            + ", ".join(str(value) for value in data[index : index + 16])
            + ","
        )
    return "\n".join(rows)


def words_rows(data: list[int], indent: str = "    ") -> str:
    rows = []
    for index in range(0, len(data), 8):
        rows.append(
            indent
            + ", ".join(f"0x{value:04x}" for value in data[index : index + 8])
            + ","
        )
    return "\n".join(rows)


def emit_eye(eye: list[int], anchor: tuple[int, int]) -> str:
    padded = eye + [-1] * (6 - len(eye))
    return (
        f"{{ .x = {anchor[0]}, .y = {anchor[1]}, .cell_count = {len(eye)}, "
        f".cells = {{ {', '.join(cell_token(value) for value in padded)} }}, "
        ".flags = 0, .reserved = 0 }"
    )


def emit_pupil(cell: int, data: tuple[int, ...]) -> str:
    x, y, min_x, min_y, max_x, max_y, range_x, range_y = data
    return (
        f"{{ .x = {x}, .y = {y}, .min_x = {min_x}, .min_y = {min_y}, "
        f".max_x = {max_x}, .max_y = {max_y}, .cell = {cell_token(cell)}, "
        f".range_x = {range_x}, .range_y = {range_y} }}"
    )


def emit_brow(
    brow: list[int], anchor: tuple[int, int], max_lift: int
) -> str:
    padded = brow + [-1] * (5 - len(brow))
    return (
        f"{{ .x = {anchor[0]}, .y = {anchor[1]}, "
        f".cell_count = {len(brow)}, "
        f".cells = {{ {', '.join(cell_token(value) for value in padded)} }}, "
        f".max_lift = {max_lift}, .flags = 0 }}"
    )


def atlas_blob(builder: AtlasBuilder) -> tuple[bytes, list[int]]:
    result = bytearray()
    offsets = []
    for cell in builder.cells:
        offsets.append(len(result))
        result.extend(cell.data)
    return bytes(result), offsets


def declared_asset_bytes(builder: AtlasBuilder, blob_bytes: int) -> int:
    """Portable payload bytes, excluding target-dependent pointer structs."""
    return (
        blob_bytes
        + len(builder.palette) * 2
        + len(builder.cells) * 20
        + len(builder.mouth_arrays) * len(MOUTH_SLOT_NAMES) * 2
        + len(builder.viseme_map) * 4
    )


def emit_atlas(builder: AtlasBuilder) -> tuple[str, dict[str, int]]:
    prefix = builder.prefix
    blob, offsets = atlas_blob(builder)
    parts: list[str] = []
    parts.append(
        f"static const uint16_t {prefix}_palette[{len(builder.palette)}] = {{\n"
        f"{words_rows(builder.palette)}\n"
        "};\n"
    )
    parts.append(
        f"static const uint8_t {prefix}_blob[{len(blob)}] = {{\n"
        f"{bytes_rows(blob)}\n"
        "};\n"
    )
    cell_lines = []
    for cell, offset in zip(builder.cells, offsets):
        cell_lines.append(
            "    { "
            f"{cell.width}, {cell.height}, {cell.offset_x}, {cell.offset_y}, "
            f"{offset}, {len(cell.data)}, {cell.encoding}, {{0, 0, 0}} "
            "},"
        )
    parts.append(
        f"static const face_sprite_cell_t {prefix}_cells"
        f"[{len(builder.cells)}] = {{\n"
        + "\n".join(cell_lines)
        + "\n};\n"
    )

    for name, mouths in builder.mouth_arrays.items():
        parts.append(
            f"static const uint16_t {prefix}_mouths_{name}"
            f"[{len(mouths)}] = {{\n"
            + "    "
            + ", ".join(str(value) for value in mouths)
            + ",\n};\n"
        )

    map_lines = [
        f"    {{ {viseme_set}, {viseme}, {slot}, {role} }},"
        for viseme_set, viseme, slot, role in builder.viseme_map
    ]
    parts.append(
        f"static const face_sprite_viseme_map_t {prefix}_visemes"
        f"[{len(map_lines)}] = {{\n"
        + "\n".join(map_lines)
        + "\n};\n"
    )

    sequence_names = []
    for sequence_index, sequence in enumerate(builder.sequences):
        frames_name = f"{prefix}_sequence_{sequence_index}_frames"
        sequence_names.append(frames_name)
        lines = [
            f"    {{ {cell}, {duration}, {x}, {y} }},"
            for cell, duration, x, y in sequence
        ]
        parts.append(
            f"static const face_sprite_sequence_frame_t {frames_name}"
            f"[{len(sequence)}] = {{\n"
            + "\n".join(lines)
            + "\n};\n"
        )
    sequence_lines = [
        f"    {{ {name}, "
        f"(uint16_t)(sizeof({name}) / sizeof({name}[0])), "
        "FACE_SPRITE_SEQUENCE_WHILE_SPEAKING, 0 },"
        for name in sequence_names
    ]
    parts.append(
        f"static const face_sprite_sequence_t {prefix}_sequences"
        f"[{len(sequence_lines)}] = {{\n"
        + "\n".join(sequence_lines)
        + "\n};\n"
    )

    bank_lines = []
    for bank in builder.banks:
        expression: Expression = bank["expression"]
        eye_left, eye_right = bank["eyes"]
        pupil_left, pupil_right = bank["pupils"]
        pupil_data_left, pupil_data_right = bank["pupil_data"]
        brow_left, brow_right = bank["brows"]
        bank_lines.append(
            "    {\n"
            "        .target = { "
            f"{expression.valence}, {expression.arousal}, "
            f"{expression.corner}, {expression.brow_inner}, "
            f"{expression.squint}, {{0, 0, 0}} "
            "},\n"
            f"        .base_cell = {bank['base']},\n"
            f"        .overlay_cell = {bank['overlay']},\n"
            "        .overlay_x = 0,\n"
            "        .overlay_y = 0,\n"
            f"        .mouth = {{ {bank['mouth_anchor'][0]}, "
            f"{bank['mouth_anchor'][1]}, "
            f"{prefix}_mouths_{bank['mouth_array']} }},\n"
            "        .eye_left = "
            + emit_eye(eye_left, bank["eye_anchors"][0])
            + ",\n"
            "        .eye_right = "
            + emit_eye(eye_right, bank["eye_anchors"][1])
            + ",\n"
            "        .pupil_left = "
            + emit_pupil(pupil_left, pupil_data_left)
            + ",\n"
            "        .pupil_right = "
            + emit_pupil(pupil_right, pupil_data_right)
            + ",\n"
            "        .brow_left = "
            + emit_brow(
                brow_left, bank["brow_anchors"][0], bank["brow_lift"]
            )
            + ",\n"
            "        .brow_right = "
            + emit_brow(
                brow_right, bank["brow_anchors"][1], bank["brow_lift"]
            )
            + ",\n"
            "    },"
        )
    parts.append(
        f"static const face_sprite_bank_t {prefix}_banks"
        f"[{len(bank_lines)}] = {{\n"
        + "\n".join(bank_lines)
        + "\n};\n"
    )

    public_name = f"face_sprite_{prefix}_atlas"
    parts.append(
        f"const face_sprite_atlas_t {public_name} = {{\n"
        "    .magic = FACE_SPRITE_MAGIC,\n"
        "    .version = FACE_SPRITE_VERSION,\n"
        f"    .native_width = {W},\n"
        f"    .native_height = {H},\n"
        "    .scale = 2,\n"
        f"    .transparent_index = {TRANSPARENT},\n"
        f"    .palette_count = {len(builder.palette)},\n"
        f"    .cell_count = {len(builder.cells)},\n"
        f"    .mouth_slot_count = {len(MOUTH_SLOT_NAMES)},\n"
        f"    .viseme_map_count = {len(builder.viseme_map)},\n"
        f"    .bank_count = {len(builder.banks)},\n"
        f"    .sequence_count = {len(builder.sequences)},\n"
        "    .cycle_count = 0,\n"
        "    .flags = FACE_SPRITE_ATLAS_IDLE_SACCADES |\n"
        "        FACE_SPRITE_ATLAS_AUTO_BLINK,\n"
        f"    .background = 0x{builder.background:04x},\n"
        "    .reserved = 0,\n"
        "    .selector = FACE_SPRITE_SELECTOR_DEFAULTS,\n"
        "    .timing = FACE_SPRITE_TIMING_DEFAULTS,\n"
        "    .fallback_slots = { 0, 1, 17, 4, 10, 13, 19, 2, 18 },\n"
        "    .reserved_slots = {0, 0, 0},\n"
        f"    .palette = {prefix}_palette,\n"
        f"    .cells = {prefix}_cells,\n"
        f"    .blob = {prefix}_blob,\n"
        f"    .blob_size = sizeof({prefix}_blob),\n"
        f"    .banks = {prefix}_banks,\n"
        f"    .viseme_map = {prefix}_visemes,\n"
        f"    .sequences = {prefix}_sequences,\n"
        "    .cycles = NULL,\n"
        f"    .name = \"{builder.display_name}\",\n"
        "};\n"
    )
    stats = {
        "blob_bytes": len(blob),
        "palette_bytes": len(builder.palette) * 2,
        "cell_count": len(builder.cells),
        "palette_count": len(builder.palette),
        "source_pixels": builder.raw_source_pixels,
        "declared_bytes": declared_asset_bytes(builder, len(blob)),
    }
    return "\n".join(parts), stats


HEADER = """\
/* Generated by tools/generate_sprite_showcase.py.  Do not hand-edit.
 * Original procedural artwork dedicated to the public domain under CC0 1.0.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "face_sprite_sheet.h"

typedef struct {
    const char *slug;
    const char *display_name;
    const face_sprite_atlas_t *atlas;
    /* PackBits/raw cell blob only. */
    uint32_t encoded_pixel_bytes;
    /* Blob + palette + cells + mouth maps + viseme map; no pointers/strings. */
    uint32_t portable_payload_bytes;
    uint32_t source_pixel_count;
    uint16_t cell_count;
    uint16_t palette_colors;
    uint8_t expression_banks;
    uint8_t mouth_slots;
} face_sprite_showcase_info_t;

extern const face_sprite_atlas_t face_sprite_vga_star_navigator_atlas;
extern const face_sprite_atlas_t face_sprite_pocket_relay_creature_atlas;

size_t face_sprite_showcase_count(void);
const face_sprite_showcase_info_t *face_sprite_showcase_info(size_t index);
"""


def main() -> None:
    builders = (build_elder(), build_robot())
    emitted = []
    stats = []
    for builder in builders:
        text, builder_stats = emit_atlas(builder)
        emitted.append(text)
        stats.append(builder_stats)

    info_rows = []
    for builder, item in zip(builders, stats):
        info_rows.append(
            "    {\n"
            f"        \"{builder.prefix}\",\n"
            f"        \"{builder.display_name}\",\n"
            f"        &face_sprite_{builder.prefix}_atlas,\n"
            f"        {item['blob_bytes']},\n"
            f"        {item['declared_bytes']},\n"
            f"        {item['source_pixels']},\n"
            f"        {item['cell_count']},\n"
            f"        {item['palette_count']},\n"
            f"        {len(EXPRESSIONS)},\n"
            f"        {len(MOUTH_SLOT_NAMES)},\n"
            "    },"
        )
    source = (
        "/* Generated by tools/generate_sprite_showcase.py.  Do not hand-edit.\n"
        " * Original procedural artwork dedicated to the public domain under CC0 1.0.\n"
        " */\n"
        '#include "face_sprite_showcase.h"\n\n'
        + "\n".join(emitted)
        + "\nstatic const face_sprite_showcase_info_t SHOWCASES[] = {\n"
        + "\n".join(info_rows)
        + "\n};\n\n"
        "size_t face_sprite_showcase_count(void)\n"
        "{\n"
        "    return sizeof(SHOWCASES) / sizeof(SHOWCASES[0]);\n"
        "}\n\n"
        "const face_sprite_showcase_info_t *face_sprite_showcase_info(\n"
        "    size_t index)\n"
        "{\n"
        "    return index < face_sprite_showcase_count()\n"
        "        ? &SHOWCASES[index]\n"
        "        : NULL;\n"
        "}\n"
    )
    OUT_H.write_text(HEADER, encoding="utf-8")
    OUT_C.write_text(source, encoding="utf-8")
    for builder, item in zip(builders, stats):
        ratio = (
            item["blob_bytes"] / item["source_pixels"]
            if item["source_pixels"]
            else 0.0
        )
        print(
            f"{builder.display_name}: {item['cell_count']} cells, "
            f"{item['palette_count']} colors, "
            f"{item['blob_bytes']} encoded pixel bytes, "
            f"{item['declared_bytes']} portable asset bytes, "
            f"{ratio:.3f} encoded bytes/source pixel"
        )


if __name__ == "__main__":
    main()
