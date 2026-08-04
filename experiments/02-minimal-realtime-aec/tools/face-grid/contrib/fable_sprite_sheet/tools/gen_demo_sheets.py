#!/usr/bin/env python3
"""Generate the original demonstration sprite sheets.

Every sheet here is drawn by this script from geometric primitives, so
the art is unambiguously original (CC0, see docs/LICENSING.md). The
four faces deliberately cover different pixel styles and different
feature subsets of the FSPR format:

- ega_sorcerer    16-colour EGA adventure-game portrait; all nine
                  mouth shapes, asymmetric eyes, pupils, brows,
                  candle-flame palette cycle, sparkle idle act.
- handheld_gobbo  four-shade green handheld look; only four mouth
                  shapes (exercises build-time fallback chains),
                  flipped right eye, ear-wiggle idle act.
- vga_navigator   32-colour sci-fi android; seven mouth shapes,
                  status-strip palette cycle, antenna idle act.
- terminal_operator  amber phosphor terminal; three mouth shapes,
                  one wide eye slot, scanlined base, cursor idle act.

Output: assets/<name>/sheet.png + manifest.json, ready for
atlas_convert.py --manifest.
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png_io  # noqa: E402

ASSETS_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "assets")


class Art:
    """A small RGBA canvas holding 0xRRGGBB ints or None (clear)."""

    def __init__(self, width: int, height: int,
                 fill: int | None = None):
        self.width = width
        self.height = height
        self.px: list[int | None] = [fill] * (width * height)

    def put(self, x: int, y: int, color: int | None) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.px[y * self.width + x] = color

    def rect(self, x: int, y: int, w: int, h: int,
             color: int | None) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, color)

    def frame(self, x: int, y: int, w: int, h: int, color: int) -> None:
        self.rect(x, y, w, 1, color)
        self.rect(x, y + h - 1, w, 1, color)
        self.rect(x, y, 1, h, color)
        self.rect(x + w - 1, y, 1, h, color)

    def disc(self, cx: int, cy: int, rx: int, ry: int,
             color: int | None) -> None:
        if rx <= 0 or ry <= 0:
            return
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if dx * dx * ry * ry + dy * dy * rx * rx \
                        <= rx * rx * ry * ry:
                    self.put(cx + dx, cy + dy, color)

    def hline(self, x: int, y: int, w: int, color: int) -> None:
        self.rect(x, y, w, 1, color)

    def vline(self, x: int, y: int, h: int, color: int) -> None:
        self.rect(x, y, 1, h, color)

    def line(self, x0: int, y0: int, x1: int, y1: int,
             color: int) -> None:
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

    def checker(self, x: int, y: int, w: int, h: int, color: int,
                parity: int = 0) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if (xx + yy) % 2 == parity:
                    self.put(xx, yy, color)

    def blit(self, other: "Art", x: int, y: int) -> None:
        for yy in range(other.height):
            for xx in range(other.width):
                color = other.px[yy * other.width + xx]
                if color is not None:
                    self.put(x + xx, y + yy, color)


def pack_sheet(cells: dict[str, Art]) -> tuple[Art, dict[str, list]]:
    """Left-to-right shelf packing with 1 px gaps."""
    max_width = 256
    sheet_w = 0
    sheet_h = 0
    x = y = shelf = 0
    placements: dict[str, list] = {}
    for name, art in cells.items():
        if x + art.width + 1 > max_width:
            x = 0
            y += shelf + 1
            shelf = 0
        placements[name] = [x, y, art.width, art.height]
        shelf = max(shelf, art.height)
        x += art.width + 1
        sheet_w = max(sheet_w, x)
        sheet_h = max(sheet_h, y + art.height)
    sheet = Art(sheet_w, sheet_h)
    for name, art in cells.items():
        px, py, _, _ = placements[name]
        sheet.blit(art, px, py)
    return sheet, placements


def write_face(name: str, cells: dict[str, Art],
               manifest: dict) -> None:
    sheet, placements = pack_sheet(cells)
    directory = os.path.join(ASSETS_DIR, name)
    os.makedirs(directory, exist_ok=True)
    rgba = bytearray(sheet.width * sheet.height * 4)
    for index, color in enumerate(sheet.px):
        if color is None:
            continue
        rgba[index * 4] = (color >> 16) & 0xFF
        rgba[index * 4 + 1] = (color >> 8) & 0xFF
        rgba[index * 4 + 2] = color & 0xFF
        rgba[index * 4 + 3] = 255
    png_io.write_png_rgba(
        os.path.join(directory, "sheet.png"),
        sheet.width, sheet.height, rgba)
    manifest = dict(manifest)
    manifest["name"] = name
    manifest["image"] = "sheet.png"
    manifest["cells"] = placements
    with open(os.path.join(directory, "manifest.json"), "w",
              encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
    print(f"{name}: sheet {sheet.width}x{sheet.height}, "
          f"{len(cells)} cells")


def draw_mouth_set(width: int, height: int, outline: int, lip: int,
                   interior: int, teeth: int, tongue: int,
                   skin: int) -> dict[str, Art]:
    """Parametric nine-shape Preston Blair / Rhubarb mouth set.

    Cells are fully opaque (skin background) so any shape cleanly
    replaces any other on the base face.
    """
    cx = width // 2
    cy = height // 2
    shapes: dict[str, Art] = {}

    def blank() -> Art:
        return Art(width, height, fill=skin)

    # X: relaxed closed line.
    art = blank()
    art.hline(cx - 6, cy, 12, outline)
    art.hline(cx - 5, cy + 1, 10, lip)
    shapes["X"] = art

    # A: pressed lips (M/B/P), slightly compressed and thicker.
    art = blank()
    art.hline(cx - 5, cy - 1, 10, lip)
    art.hline(cx - 6, cy, 12, outline)
    art.hline(cx - 5, cy + 1, 10, lip)
    shapes["A"] = art

    # B: slightly open, clenched teeth.
    art = blank()
    art.rect(cx - 6, cy - 2, 12, 5, outline)
    art.rect(cx - 5, cy - 1, 10, 3, teeth)
    art.hline(cx - 5, cy, 10, interior)
    shapes["B"] = art

    # C: half-open oval.
    art = blank()
    art.disc(cx, cy, 6, 3, outline)
    art.disc(cx, cy, 5, 2, interior)
    shapes["C"] = art

    # D: wide open with tongue.
    art = blank()
    art.disc(cx, cy, 7, 5, outline)
    art.disc(cx, cy, 6, 4, interior)
    art.disc(cx, cy + 3, 4, 2, tongue)
    art.hline(cx - 4, cy - 3, 8, teeth)
    shapes["D"] = art

    # E: rounded medium open (AO/ER).
    art = blank()
    art.disc(cx, cy, 4, 4, outline)
    art.disc(cx, cy, 3, 3, interior)
    shapes["E"] = art

    # F: small pucker (UW/OO/W).
    art = blank()
    art.disc(cx, cy, 3, 3, outline)
    art.disc(cx, cy, 2, 2, lip)
    art.disc(cx, cy, 1, 1, interior)
    shapes["F"] = art

    # G: upper teeth on lower lip (F/V).
    art = blank()
    art.rect(cx - 5, cy - 2, 10, 3, outline)
    art.rect(cx - 4, cy - 1, 8, 2, teeth)
    art.hline(cx - 5, cy + 1, 10, lip)
    shapes["G"] = art

    # H: open with tongue up behind teeth (long L).
    art = blank()
    art.disc(cx, cy, 5, 4, outline)
    art.disc(cx, cy, 4, 3, interior)
    art.rect(cx - 2, cy - 2, 4, 3, tongue)
    shapes["H"] = art
    return shapes


# ------------------------------------------------------------------ #
# Face 1: EGA sorcerer (16-colour adventure portrait)                #
# ------------------------------------------------------------------ #

def build_ega_sorcerer() -> None:
    # Classic EGA hardware palette entries.
    BLACK, BLUE, GREEN, CYAN = 0x000000, 0x0000AA, 0x00AA00, 0x00AAAA
    RED, MAGENTA, BROWN, LGRAY = 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA
    DGRAY, LBLUE, LGREEN = 0x555555, 0x5555FF, 0x55FF55
    LRED, LMAGENTA, YELLOW, WHITE = 0xFF5555, 0xFF55FF, 0xFFFF55, \
        0xFFFFFF
    SKIN, SKIN_HI = BROWN, 0xFFAA55  # EGA light-brown pair
    # Hues reserved for the cycled candle flame; all four stay unique
    # after RGB565 quantisation and match no static palette colour.
    FLAME = [0xFFF854, 0xFF8040, 0xF85030, 0xFFD080]

    W, H = 80, 60
    base = Art(W, H, fill=BLUE)
    base.checker(0, 0, W, H, BLACK, 1)          # dark parlour wall
    base.rect(0, 52, W, 8, DGRAY)               # desk
    base.hline(0, 52, W, LGRAY)
    # Robe and shoulders.
    base.disc(40, 68, 26, 22, MAGENTA)
    base.disc(40, 70, 20, 18, LMAGENTA)
    # Head.
    base.disc(40, 28, 15, 17, SKIN)
    base.disc(40, 26, 14, 14, SKIN_HI)
    # Beard.
    base.disc(40, 44, 11, 10, LGRAY)
    base.disc(40, 46, 9, 8, WHITE)
    base.rect(29, 38, 22, 6, SKIN_HI)           # clear cheeks band
    # Hat.
    base.disc(40, 14, 16, 6, MAGENTA)
    base.rect(24, 12, 32, 4, MAGENTA)
    base.line(40, 0, 30, 13, MAGENTA)
    base.line(40, 0, 50, 13, MAGENTA)
    base.line(40, 1, 31, 13, LMAGENTA)
    base.rect(31, 12, 19, 2, LMAGENTA)
    base.put(40, 2, YELLOW)
    # Nose.
    base.vline(40, 28, 6, SKIN)
    base.disc(40, 34, 2, 1, SKIN)
    base.put(39, 34, BLACK)
    base.put(41, 34, BLACK)
    # Candle in the corner; flame pixels use the cycled colours.
    base.rect(6, 40, 4, 12, WHITE)
    base.rect(6, 40, 4, 1, LGRAY)
    base.put(7, 36, FLAME[0])
    base.put(8, 36, FLAME[1])
    base.put(7, 37, FLAME[2])
    base.put(8, 37, FLAME[3])
    base.rect(4, 52, 8, 2, LGRAY)

    cells: dict[str, Art] = {"base": base}

    # Eyes: asymmetric pair (distinct left/right cells, no flipping).
    for side, sx in (("l", 0), ("r", 1)):
        for state, lid in (("open", 0), ("half", 3), ("closed", 6)):
            art = Art(12, 9, fill=SKIN_HI)
            art.disc(5 + sx, 4, 5, 4, BLACK)
            art.disc(5 + sx, 4, 4, 3, WHITE)
            if lid:
                art.rect(0, 0, 12, lid, SKIN_HI)
                art.hline(0, lid, 12, BLACK)
            if state == "closed":
                art.rect(0, 0, 12, 9, SKIN_HI)
                art.hline(1, 5, 10, BLACK)
                art.hline(2, 6, 8, DGRAY)
            cells[f"eye_{side}_{state}"] = art

    pupil = Art(3, 3, fill=None)
    pupil.disc(1, 1, 1, 1, BLACK)
    pupil.put(0, 0, LBLUE)
    cells["pupil"] = pupil

    for level, tilt in (("low", 2), ("mid", 0), ("high", -1)):
        art = Art(13, 5, fill=None)
        art.line(0, 2 + tilt, 12, 2 - tilt, WHITE)
        art.line(0, 3 + tilt, 12, 3 - tilt, LGRAY)
        cells[f"brow_{level}"] = art

    mouths = draw_mouth_set(
        20, 12, outline=BLACK, lip=LRED, interior=RED,
        teeth=WHITE, tongue=LRED, skin=WHITE)
    for shape, art in mouths.items():
        cells[f"mouth_{shape}"] = art

    # Idle act: a wand sparkle drifting over the desk.
    for phase in range(3):
        art = Art(7, 7, fill=None)
        art.put(3, 3, WHITE)
        if phase >= 1:
            art.put(3, 1, YELLOW)
            art.put(3, 5, YELLOW)
            art.put(1, 3, YELLOW)
            art.put(5, 3, YELLOW)
        if phase == 2:
            art.put(1, 1, CYAN)
            art.put(5, 5, CYAN)
            art.put(5, 1, LGREEN)
            art.put(1, 5, LGREEN)
        cells[f"sparkle_{phase}"] = art

    manifest = {
        "scale": 2,
        "native_size": [W, H],
        "background": "#0000AA",
        "features": ["breathe", "saccades", "auto_blink"],
        "cycles": [{
            "colors": [f"#{c:06x}" for c in FLAME],
            "period_ms": 130,
        }],
        "banks": [{
            "base": "base",
            "mouth": {
                "anchor": [30, 40],
                "shapes": {s: f"mouth_{s}" for s in "XABCDEFGH"},
            },
            "eye_left": {
                "anchor": [27, 22],
                "lids": ["eye_l_open", "eye_l_half", "eye_l_closed"],
            },
            "eye_right": {
                "anchor": [41, 22],
                "lids": ["eye_r_open", "eye_r_half", "eye_r_closed"],
            },
            "pupil_left": {
                "anchor": [31, 25], "cell": "pupil",
                "range": [2, 1], "clamp": [29, 23, 34, 27],
            },
            "pupil_right": {
                "anchor": [45, 25], "cell": "pupil",
                "range": [2, 1], "clamp": [43, 23, 48, 27],
            },
            "brow_left": {
                "anchor": [26, 16],
                "levels": ["brow_low", "brow_mid", "brow_high"],
                "max_lift": 2,
            },
            "brow_right": {
                "anchor": [41, 16],
                "levels": ["brow_low", "brow_mid", "brow_high"],
                "max_lift": 2,
            },
        }],
        "sequences": [{
            "frames": [
                {"cell": "sparkle_0", "ms": 130, "at": [12, 44]},
                {"cell": "sparkle_1", "ms": 130, "at": [14, 42]},
                {"cell": "sparkle_2", "ms": 160, "at": [16, 40]},
                {"cell": "sparkle_1", "ms": 130, "at": [18, 42]},
                {"cell": "sparkle_0", "ms": 130, "at": [20, 44]},
            ],
        }],
    }
    write_face("ega_sorcerer", cells, manifest)


# ------------------------------------------------------------------ #
# Face 2: handheld goblin (four-shade green, fallback exercise)      #
# ------------------------------------------------------------------ #

def build_handheld_gobbo() -> None:
    # Classic four-shade green-screen ramp, darkest to lightest.
    G0, G1, G2, G3 = 0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F

    W, H = 80, 60
    base = Art(W, H, fill=G3)
    base.frame(0, 0, W, H, G0)
    base.frame(1, 1, W - 2, H - 2, G1)
    # Round goblin head with big ears.
    base.disc(40, 32, 20, 19, G0)
    base.disc(40, 32, 18, 17, G2)
    base.disc(40, 30, 16, 14, G3)
    for sx, direction in ((16, -1), (64, 1)):
        base.disc(sx, 26, 6, 9, G0)
        base.disc(sx - direction, 26, 4, 7, G2)
    # Warty forehead dots and a snaggle tooth base.
    base.put(32, 18, G1)
    base.put(47, 20, G1)
    base.put(40, 24, G1)
    # Snout.
    base.disc(40, 38, 5, 3, G2)
    base.put(38, 37, G0)
    base.put(42, 37, G0)

    cells: dict[str, Art] = {"base": base}

    # Two lid levels only; the right eye is the left cell flipped.
    for state, closed in (("open", False), ("closed", True)):
        art = Art(11, 10, fill=G3)
        art.disc(5, 5, 5, 4, G0)
        if closed:
            art.disc(5, 5, 4, 3, G2)
            art.hline(2, 5, 8, G0)
        else:
            art.disc(5, 5, 4, 3, G3)
            art.disc(5, 5, 2, 2, G0)
            art.put(4, 3, G3)
        cells[f"eye_{state}"] = art

    for level, lift in (("low", 0), ("high", 2)):
        art = Art(10, 4, fill=None)
        art.line(0, 3 - lift, 9, 1, G0)
        cells[f"brow_{level}"] = art

    # Only four mouth shapes: X, C, D, F. The converter's fallback
    # chains must cover A/B/E/G/H from these.
    def gob_blank() -> Art:
        return Art(18, 11, fill=G3)

    art = gob_blank()
    art.hline(4, 5, 10, G0)
    art.put(6, 4, G0)   # snaggle tooth up
    cells["mouth_X"] = art

    art = gob_blank()
    art.disc(9, 5, 5, 2, G0)
    art.disc(9, 5, 4, 1, G1)
    art.put(6, 4, G3)   # tooth overlaps lip
    cells["mouth_C"] = art

    art = gob_blank()
    art.disc(9, 5, 6, 4, G0)
    art.disc(9, 5, 5, 3, G1)
    art.rect(6, 3, 2, 2, G3)     # snaggle tooth
    art.disc(9, 8, 3, 1, G2)     # tongue
    cells["mouth_D"] = art

    art = gob_blank()
    art.disc(9, 5, 2, 2, G0)
    art.put(9, 5, G1)
    cells["mouth_F"] = art

    # Idle act: left ear wiggle (redraws the ear region).
    for phase, lift in (("0", 0), ("1", 2)):
        art = Art(14, 20, fill=G3)
        art.disc(6, 10 - lift, 6, 9, G0)
        art.disc(7, 10 - lift, 4, 7, G2)
        cells[f"ear_{phase}"] = art

    manifest = {
        "scale": 2,
        "native_size": [W, H],
        "background": "#0F380F",
        "features": ["breathe", "auto_blink"],
        "banks": [{
            "base": "base",
            "mouth": {
                "anchor": [31, 40],
                "shapes": {
                    "X": "mouth_X", "C": "mouth_C",
                    "D": "mouth_D", "F": "mouth_F",
                },
            },
            "eye_left": {
                "anchor": [26, 24],
                "lids": ["eye_open", "eye_closed"],
            },
            "eye_right": {
                "anchor": [43, 24],
                "lids": ["eye_open", "eye_closed"],
                "flip_x": True,
            },
            "brow_left": {
                "anchor": [25, 18],
                "levels": ["brow_low", "brow_high"],
                "max_lift": 2,
            },
            "brow_right": {
                "anchor": [45, 18],
                "levels": ["brow_low", "brow_high"],
                "max_lift": 2, "flip_x": True,
            },
        }],
        "sequences": [{
            "frames": [
                {"cell": "ear_1", "ms": 90, "at": [9, 16]},
                {"cell": "ear_0", "ms": 90, "at": [9, 16]},
                {"cell": "ear_1", "ms": 90, "at": [9, 16]},
                {"cell": "ear_0", "ms": 90, "at": [9, 16]},
            ],
        }],
    }
    write_face("handheld_gobbo", cells, manifest)


# ------------------------------------------------------------------ #
# Face 3: VGA navigator (32-colour sci-fi android)                   #
# ------------------------------------------------------------------ #

def build_vga_navigator() -> None:
    BG0, BG1 = 0x10102C, 0x181840
    HULL0, HULL1, HULL2, HULL3 = 0x30344C, 0x4C5470, 0x707C9C, 0x9CA8C4
    VISOR0, VISOR1 = 0x102020, 0x184040
    EYE_GLOW, EYE_CORE = 0x40E0D0, 0xC0FFF0
    LIP0, LIP1, MOUTH_IN = 0x203050, 0x3868A0, 0x081018
    TEETH = 0xB8C8E8
    AMBER0, AMBER1 = 0xC08020, 0xF0B040
    # Reserved hues for the cycled status strip, distinct in RGB565.
    STRIP = [0x00F880, 0x00C868, 0x009850, 0x006838, 0x00A058,
             0x00D070]

    W, H = 80, 60
    base = Art(W, H, fill=BG0)
    for y in range(0, H, 4):
        base.hline(0, y, W, BG1)
    for x in range(0, W, 16):        # starfield dots
        base.put(x + 3, (x * 7) % H, HULL2)
    # Helmet silhouette.
    base.disc(40, 30, 19, 23, HULL0)
    base.disc(40, 29, 17, 21, HULL1)
    base.disc(40, 28, 15, 18, HULL2)
    # Face plate.
    base.disc(40, 32, 12, 14, HULL3)
    base.rect(28, 18, 25, 7, HULL1)  # brow ridge
    # Visor band across the eyes.
    base.rect(27, 22, 27, 10, VISOR0)
    base.rect(28, 23, 25, 8, VISOR1)
    # Cheek vents.
    for row in range(3):
        base.hline(29, 36 + row * 2, 5, HULL0)
        base.hline(47, 36 + row * 2, 5, HULL0)
    # Chin guard.
    base.rect(32, 50, 17, 4, HULL1)
    base.hline(32, 50, 17, HULL2)
    # Antenna mast (light handled by an idle sequence overlay).
    base.vline(40, 2, 6, HULL2)
    base.put(40, 1, AMBER0)
    # Status strip: cycled colours marching along the collar.
    for index, color in enumerate(STRIP):
        base.put(31 + index * 3, 56, color)
        base.put(32 + index * 3, 56, color)

    cells: dict[str, Art] = {"base": base}

    # Slit eyes inside the visor; right eye is flipped left.
    lids = (("open", 0), ("half", 2), ("closed", 5))
    for state, shut in lids:
        art = Art(10, 6, fill=VISOR1)
        art.rect(0, 0, 10, 6, VISOR1)
        glow_h = 6 - shut
        if glow_h > 1:
            art.rect(1, shut, 8, glow_h - 1, EYE_GLOW)
            art.rect(2, shut + 1, 6, max(1, glow_h - 3), EYE_CORE)
        else:
            art.hline(1, 4, 8, EYE_GLOW)
        cells[f"eye_{state}"] = art

    pupil = Art(2, 2, fill=None)
    pupil.rect(0, 0, 2, 2, 0x083838)
    cells["pupil"] = pupil

    for level, tilt in (("low", 1), ("mid", 0), ("high", -1)):
        art = Art(11, 3, fill=None)
        art.line(0, 1 + tilt, 10, 1 - tilt, HULL0)
        cells[f"brow_{level}"] = art

    mouths = draw_mouth_set(
        20, 12, outline=LIP0, lip=LIP1, interior=MOUTH_IN,
        teeth=TEETH, tongue=LIP1, skin=HULL3)
    for shape in "XABCDEF":       # seven shapes; G/H fall back
        cells[f"mouth_{shape}"] = mouths[shape]

    # Idle act: antenna beacon double-pulse.
    for phase, size in (("0", 0), ("1", 1), ("2", 2)):
        art = Art(5, 5, fill=None)
        art.put(2, 2, AMBER1)
        if size:
            art.disc(2, 2, int(size), int(size), AMBER1)
            art.put(2, 2, 0xFFFFFF)
        cells[f"beacon_{phase}"] = art

    manifest = {
        "scale": 2,
        "native_size": [W, H],
        "background": "#10102C",
        "features": ["saccades", "auto_blink"],
        "cycles": [{
            "colors": [f"#{c:06x}" for c in STRIP],
            "period_ms": 90,
        }],
        "banks": [{
            "base": "base",
            "mouth": {
                "anchor": [30, 42],
                "shapes": {s: f"mouth_{s}" for s in "XABCDEF"},
            },
            "eye_left": {
                "anchor": [29, 24],
                "lids": ["eye_open", "eye_half", "eye_closed"],
            },
            "eye_right": {
                "anchor": [42, 24],
                "lids": ["eye_open", "eye_half", "eye_closed"],
                "flip_x": True,
            },
            "pupil_left": {
                "anchor": [33, 26], "cell": "pupil",
                "range": [2, 1], "clamp": [30, 25, 36, 28],
            },
            "pupil_right": {
                "anchor": [46, 26], "cell": "pupil",
                "range": [2, 1], "clamp": [43, 25, 49, 28],
            },
            "brow_left": {
                "anchor": [28, 19],
                "levels": ["brow_low", "brow_mid", "brow_high"],
                "max_lift": 1,
            },
            "brow_right": {
                "anchor": [42, 19],
                "levels": ["brow_low", "brow_mid", "brow_high"],
                "max_lift": 1, "flip_x": True,
            },
        }],
        "sequences": [{
            "while_speaking": True,
            "frames": [
                {"cell": "beacon_1", "ms": 80, "at": [38, 0]},
                {"cell": "beacon_2", "ms": 120, "at": [38, 0]},
                {"cell": "beacon_1", "ms": 80, "at": [38, 0]},
                {"cell": "beacon_0", "ms": 400, "at": [38, 0]},
                {"cell": "beacon_1", "ms": 80, "at": [38, 0]},
                {"cell": "beacon_0", "ms": 200, "at": [38, 0]},
            ],
        }],
    }
    write_face("vga_navigator", cells, manifest)


# ------------------------------------------------------------------ #
# Face 4: terminal operator (amber phosphor mono)                    #
# ------------------------------------------------------------------ #

def build_terminal_operator() -> None:
    A0, A1, A2, A3 = 0x100800, 0x542800, 0xA85000, 0xFFB000
    # Ambers reserved for the shimmer cycle, distinct in RGB565 from
    # the static ramp so face pixels never join the cycle.
    SHIMMER = [0xFFC000, 0xB05800, 0x603000]

    W, H = 80, 60
    base = Art(W, H, fill=A0)
    for y in range(0, H, 2):
        base.hline(0, y, W, 0x1C0C00)   # baked scanlines
    base.frame(0, 0, W, H, A1)
    # Shimmering border ticks use the cycled colours.
    for index in range(0, W, 6):
        base.put(index + 2, 0, SHIMMER[(index // 6) % 3])
        base.put(index + 3, H - 1, SHIMMER[(index // 6 + 1) % 3])
    # Blocky operator head, all rectangles like character graphics.
    base.rect(24, 10, 32, 38, A1)
    base.rect(26, 12, 28, 34, A2)
    base.rect(28, 14, 24, 8, A1)     # hair band
    base.rect(28, 22, 24, 22, A2)    # face
    base.rect(38, 30, 4, 4, A3)      # nose block
    base.rect(24, 46, 32, 4, A1)     # jaw shadow
    base.rect(20, 50, 40, 10, A1)    # shoulders
    base.rect(22, 52, 36, 8, A2)
    # Prompt caret bottom-left (idle act animates it).
    base.rect(4, 54, 5, 3, A1)

    cells: dict[str, Art] = {"base": base}

    # One wide eye band covering both eyes: exercises single-slot use.
    for state, closed in (("open", False), ("closed", True)):
        art = Art(24, 6, fill=A2)
        for ex in (2, 15):
            if closed:
                art.hline(ex, 3, 7, A0)
            else:
                art.rect(ex, 1, 7, 4, A0)
                art.rect(ex + 2, 2, 3, 2, A3)
        cells[f"eyes_{state}"] = art

    for level, lift in (("low", 0), ("high", 1)):
        art = Art(24, 3, fill=None)
        art.hline(1, 2 - lift, 8, A3)
        art.hline(15, 2 - lift, 8, A3)
        cells[f"brow_{level}"] = art

    # Three mouth shapes drawn as chunky character-cell blocks.
    art = Art(16, 8, fill=A2)
    art.rect(3, 3, 10, 2, A0)
    cells["mouth_X"] = art

    art = Art(16, 8, fill=A2)
    art.rect(3, 2, 10, 4, A0)
    art.rect(5, 3, 6, 2, A1)
    cells["mouth_C"] = art

    art = Art(16, 8, fill=A2)
    art.rect(2, 1, 12, 6, A0)
    art.rect(4, 2, 8, 1, A3)
    art.rect(4, 5, 8, 2, A1)
    cells["mouth_D"] = art

    # Idle act: blinking prompt caret.
    caret_on = Art(5, 3, fill=None)
    caret_on.rect(0, 0, 5, 3, A3)
    caret_off = Art(5, 3, fill=None)
    caret_off.rect(0, 0, 5, 3, A0)
    cells["caret_on"] = caret_on
    cells["caret_off"] = caret_off

    manifest = {
        "scale": 2,
        "native_size": [W, H],
        "background": "#100800",
        "features": ["auto_blink"],
        "cycles": [{
            "colors": [f"#{c:06x}" for c in SHIMMER],
            "period_ms": 260,
        }],
        "banks": [
            {
                "base": "base",
                "mouth": {
                    "anchor": [32, 38],
                    "shapes": {
                        "X": "mouth_X", "C": "mouth_C",
                        "D": "mouth_D",
                    },
                },
                "eye_left": {
                    "anchor": [28, 24],
                    "lids": ["eyes_open", "eyes_closed"],
                },
                "brow_left": {
                    "anchor": [28, 20],
                    "levels": ["brow_low", "brow_high"],
                    "max_lift": 1,
                },
            },
            {
                # Expression 1: "standby" — no brows, lids-only eyes.
                # Exercises multi-bank selection and validation.
                "base": "base",
                "mouth": {
                    "anchor": [32, 38],
                    "shapes": {"X": "mouth_X", "C": "mouth_C"},
                },
                "eye_left": {
                    "anchor": [28, 25],
                    "lids": ["eyes_closed"],
                },
            },
        ],
        "sequences": [{
            "while_speaking": True,
            "frames": [
                {"cell": "caret_on", "ms": 320, "at": [4, 54]},
                {"cell": "caret_off", "ms": 320, "at": [4, 54]},
                {"cell": "caret_on", "ms": 320, "at": [4, 54]},
                {"cell": "caret_off", "ms": 320, "at": [4, 54]},
            ],
        }],
    }
    write_face("terminal_operator", cells, manifest)


def main() -> int:
    build_ega_sorcerer()
    build_handheld_gobbo()
    build_vga_navigator()
    build_terminal_operator()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
