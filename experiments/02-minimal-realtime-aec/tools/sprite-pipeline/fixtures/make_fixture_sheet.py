#!/usr/bin/env python3
"""Deterministic synthetic "image-model" sprite sheet: the Bloomling.

Draws a small sprout creature at a logical pixel grid, then renders it the
way image models render fake pixel art: pitch-3 fat pixels with wobbly cell
boundaries, blended (anti-aliased) cell edges, per-pixel color noise, and a
subtly graded background. The result exercises background keying, band
segmentation, narrow-span merging (detached sparkles), grid detection and
snapping, palette quantization, despeckle, baked-mouth erase, and lid
synthesis — with zero licensing risk (procedural CC0 art, fixed seed).
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fspp.png_io import Image, save_rgba  # noqa: E402
from fspp.util import Xorshift32  # noqa: E402

PITCH = 3
SEED = 0xB100B100

BG_TOP = (247, 247, 232)
BG_BOTTOM = (238, 243, 221)

BODY = (124, 191, 106)
BODY_SHADE = (85, 145, 90)
BELLY = (166, 214, 148)
OUTLINE = (46, 77, 56)
LEAF = (63, 122, 70)
EYE = (35, 40, 30)
EYE_GLINT = (232, 244, 216)
BLUSH = (217, 140, 140)
MOUTH = (90, 43, 43)
TONGUE = (196, 106, 106)
TEETH = (244, 244, 230)
SPARK = (240, 200, 90)


class Grid:
    """Logical pixel canvas; None means background."""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.px: list[tuple[int, int, int] | None] = [None] * (
            width * height
        )

    def put(self, x: int, y: int, color) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.px[y * self.width + x] = color

    def rect(self, x: int, y: int, w: int, h: int, color) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, color)

    def hline(self, x: int, y: int, w: int, color) -> None:
        self.rect(x, y, w, 1, color)

    def disc(self, cx: int, cy: int, rx: int, ry: int, color) -> None:
        if rx <= 0 or ry <= 0:
            self.put(cx, cy, color)
            return
        limit = rx * rx * ry * ry
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if dx * dx * ry * ry + dy * dy * rx * rx <= limit:
                    self.put(cx + dx, cy + dy, color)

    def ring(self, cx: int, cy: int, rx: int, ry: int, color) -> None:
        limit = rx * rx * ry * ry
        inner = (rx - 1) * (rx - 1) * (ry - 1) * (ry - 1)
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                d = dx * dx * ry * ry + dy * dy * rx * rx
                if d <= limit:
                    di = dx * dx * (ry - 1) * (ry - 1) + dy * dy * (
                        rx - 1
                    ) * (rx - 1)
                    if di > inner:
                        self.put(cx + dx, cy + dy, color)


PW, PH = 40, 36  # portrait logical size
MW, MH = 20, 12  # mouth cel logical size


def draw_body(g: Grid) -> None:
    g.disc(20, 20, 15, 13, OUTLINE)
    g.disc(20, 20, 14, 12, BODY)
    g.disc(20, 25, 9, 7, BELLY)
    for x, y in ((11, 33), (26, 33)):
        g.rect(x, y, 4, 3, OUTLINE)
        g.rect(x + 1, y, 2, 2, BODY_SHADE)
    # leaf sprout
    g.rect(19, 3, 2, 4, OUTLINE)
    g.disc(16, 3, 3, 2, LEAF)
    g.disc(24, 2, 3, 2, LEAF)
    g.disc(20, 12, 12, 3, BODY_SHADE)
    g.disc(20, 13, 11, 2, BODY)


def draw_eye(g: Grid, cx: int, style: str) -> None:
    cy = 14
    if style == "open":
        g.disc(cx, cy, 3, 3, EYE)
        g.put(cx - 1, cy - 1, EYE_GLINT)
    elif style == "wide":
        g.disc(cx, cy, 3, 4, OUTLINE)
        g.disc(cx, cy, 2, 3, EYE)
        g.put(cx - 1, cy - 1, EYE_GLINT)
    elif style == "closed":
        g.hline(cx - 3, cy, 7, OUTLINE)
    elif style == "happy":
        g.hline(cx - 3, cy - 1, 2, OUTLINE)
        g.hline(cx - 1, cy - 2, 3, OUTLINE)
        g.hline(cx + 2, cy - 1, 2, OUTLINE)
    elif style == "half":
        g.disc(cx, cy, 3, 3, EYE)
        g.rect(cx - 3, cy - 3, 7, 3, BODY)
        g.hline(cx - 3, cy - 1, 7, OUTLINE)
    elif style == "star":
        g.disc(cx, cy, 3, 3, EYE)
        g.put(cx, cy - 2, SPARK)
        g.put(cx, cy, SPARK)
        g.put(cx - 1, cy - 1, SPARK)
        g.put(cx + 1, cy - 1, SPARK)


def draw_brow(g: Grid, cx: int, style: str) -> None:
    y = 9
    if style == "raise":
        g.hline(cx - 3, y - 2, 6, OUTLINE)
    elif style == "angry":
        side = 1 if cx > 20 else -1
        for i in range(5):
            g.put(cx - 2 + i, y + (i // 2 if side < 0 else 2 - i // 2) - 1,
                  OUTLINE)
    elif style == "worry":
        side = 1 if cx > 20 else -1
        for i in range(5):
            g.put(cx - 2 + i, y + (2 - i // 2 if side < 0 else i // 2) - 1,
                  OUTLINE)


def draw_baked_mouth(g: Grid, style: str) -> None:
    cx, cy = 20, 27
    if style == "smile":
        g.hline(cx - 4, cy, 9, MOUTH)
        g.put(cx - 5, cy - 1, MOUTH)
        g.put(cx + 5, cy - 1, MOUTH)
    elif style == "frown":
        g.hline(cx - 4, cy, 9, MOUTH)
        g.put(cx - 5, cy + 1, MOUTH)
        g.put(cx + 5, cy + 1, MOUTH)
    elif style == "open":
        g.disc(cx, cy, 4, 3, MOUTH)
        g.disc(cx, cy + 1, 2, 1, TONGUE)
    elif style == "o":
        g.ring(cx, cy, 3, 3, MOUTH)
    elif style == "flat":
        g.hline(cx - 4, cy, 9, MOUTH)


EXPRESSIONS = (
    ("neutral", "open", "none", "flat", ()),
    ("warm", "happy", "none", "smile", ("blush",)),
    ("joy", "happy", "raise", "open", ("blush",)),
    ("concern", "open", "worry", "frown", ()),
    ("surprise", "wide", "raise", "o", ()),
    ("thoughtful", "half", "raise", "flat", ()),
    ("skeptical", "half", "angry", "flat", ()),
    ("determined", "open", "angry", "smile", ()),
    ("sleepy", "closed", "none", "o", ("zzz",)),
    ("excited", "star", "raise", "open", ("sparkles",)),
    ("embarrassed", "open", "worry", "smile", ("blush", "sweat")),
)


def draw_portrait(slug: str, eye: str, brow: str, mouth: str,
                  extras: tuple) -> Grid:
    g = Grid(PW, PH)
    draw_body(g)
    draw_eye(g, 13, eye)
    draw_eye(g, 27, eye)
    if brow != "none":
        draw_brow(g, 13, brow)
        draw_brow(g, 27, brow)
    draw_baked_mouth(g, mouth)
    if "blush" in extras:
        g.rect(6, 19, 3, 2, BLUSH)
        g.rect(31, 19, 3, 2, BLUSH)
    if "sweat" in extras:
        g.put(33, 6, (150, 200, 230))
        g.rect(33, 7, 2, 3, (150, 200, 230))
    if "zzz" in extras:
        g.hline(33, 3, 3, OUTLINE)
        g.put(35, 4, OUTLINE)
        g.hline(33, 5, 3, OUTLINE)
    if "sparkles" in extras:
        # Detached marks exercise narrow-span merging in segmentation.
        g.put(2, 4, SPARK)
        g.rect(1, 5, 3, 1, SPARK)
        g.put(2, 6, SPARK)
        g.put(36, 8, SPARK)
        g.rect(35, 9, 3, 1, SPARK)
        g.put(36, 10, SPARK)
    return g


MOUTH_CELS = (
    ("sil", "flat"),
    ("pp", "press"),
    ("grin", "teeth"),
    ("tongue", "tongue"),
    ("lateral", "wide_flat"),
    ("aa", "open_big"),
    ("oh", "round"),
    ("ou", "small_o"),
    ("ih", "slit"),
    ("gasp", "dark_open"),
)


def draw_mouth_cel(style: str) -> Grid:
    g = Grid(MW, MH)
    cx, cy = MW // 2, MH // 2
    if style == "flat":
        g.hline(cx - 4, cy, 9, MOUTH)
    elif style == "press":
        g.hline(cx - 4, cy - 1, 9, MOUTH)
        g.hline(cx - 4, cy, 9, MOUTH)
    elif style == "teeth":
        g.rect(cx - 5, cy - 2, 11, 5, MOUTH)
        g.rect(cx - 4, cy - 1, 9, 2, TEETH)
    elif style == "tongue":
        g.disc(cx, cy, 5, 4, MOUTH)
        g.disc(cx, cy + 2, 3, 2, TONGUE)
    elif style == "wide_flat":
        g.hline(cx - 7, cy, 15, MOUTH)
    elif style == "open_big":
        g.disc(cx, cy, 6, 5, MOUTH)
        g.disc(cx, cy + 2, 4, 2, TONGUE)
    elif style == "round":
        g.disc(cx, cy, 4, 4, MOUTH)
        g.disc(cx, cy, 2, 2, (30, 12, 12))
    elif style == "small_o":
        g.ring(cx, cy, 2, 2, MOUTH)
    elif style == "slit":
        g.rect(cx - 3, cy - 1, 7, 3, MOUTH)
        g.hline(cx - 2, cy, 5, TEETH)
    elif style == "dark_open":
        g.disc(cx, cy, 6, 5, (30, 12, 12))
        g.ring(cx, cy, 6, 5, MOUTH)
    return g


def wobbly_edges(count: int, pitch: int, rng: Xorshift32) -> list[int]:
    """count+1 strictly increasing block boundaries with +-1 wobble."""
    edges = [0]
    for k in range(1, count + 1):
        jitter = rng.below(3) - 1
        value = k * pitch + jitter
        if value <= edges[-1]:
            value = edges[-1] + 1
        edges.append(value)
    return edges


def render_fake_pixels(
    sheet: Image, grid: Grid, origin_x: int, origin_y: int, rng: Xorshift32
) -> None:
    xs = wobbly_edges(grid.width, PITCH, rng)
    ys = wobbly_edges(grid.height, PITCH, rng)
    for ly in range(grid.height):
        for lx in range(grid.width):
            color = grid.px[ly * grid.width + lx]
            if color is None:
                continue
            for py in range(ys[ly], ys[ly + 1]):
                for px in range(xs[lx], xs[lx + 1]):
                    blend = color
                    # Anti-alias the first row/column of each fat pixel
                    # toward the neighbouring logical color.
                    if px == xs[lx] and lx > 0:
                        left = grid.px[ly * grid.width + lx - 1]
                        if left is not None and left != color:
                            blend = tuple(
                                (2 * c + n) // 3
                                for c, n in zip(blend, left)
                            )
                    if py == ys[ly] and ly > 0:
                        up = grid.px[(ly - 1) * grid.width + lx]
                        if up is not None and up != color:
                            blend = tuple(
                                (2 * c + n) // 3 for c, n in zip(blend, up)
                            )
                    noisy = tuple(
                        min(255, max(0, c + rng.below(7) - 3))
                        for c in blend
                    )
                    sheet.put(origin_x + px, origin_y + py, (*noisy, 255))


def make_sheet() -> Image:
    rng = Xorshift32(SEED)
    pad_x, pad_y = 30, 40
    portrait_w, portrait_h = PW * PITCH, PH * PITCH
    mouth_w, mouth_h = MW * PITCH, MH * PITCH
    width = 6 * portrait_w + 7 * pad_x
    rows_y = [
        pad_y,
        pad_y + portrait_h + pad_y,
        pad_y * 2 + portrait_h * 2 + pad_y,
        pad_y * 2 + portrait_h * 2 + pad_y + mouth_h + pad_y,
    ]
    height = rows_y[-1] + mouth_h + pad_y

    sheet = Image.blank(width, height)
    for y in range(height):
        base_color = tuple(
            top + (bottom - top) * y // max(1, height - 1)
            for top, bottom in zip(BG_TOP, BG_BOTTOM)
        )
        for x in range(width):
            noisy = tuple(
                min(255, max(0, c + rng.below(5) - 2)) for c in base_color
            )
            sheet.put(x, y, (*noisy, 255))

    portraits = [
        draw_portrait(slug, eye, brow, mouth, extras)
        for slug, eye, brow, mouth, extras in EXPRESSIONS
    ]
    for index, grid in enumerate(portraits[:6]):
        x = pad_x + index * (portrait_w + pad_x)
        render_fake_pixels(sheet, grid, x, rows_y[0], rng)
    for index, grid in enumerate(portraits[6:]):
        x = pad_x + index * (portrait_w + pad_x)
        render_fake_pixels(sheet, grid, x, rows_y[1], rng)

    cels = [draw_mouth_cel(style) for _, style in MOUTH_CELS]
    for index, grid in enumerate(cels[:5]):
        x = pad_x + index * (mouth_w + pad_x * 2)
        render_fake_pixels(sheet, grid, x, rows_y[2], rng)
    for index, grid in enumerate(cels[5:]):
        x = pad_x + index * (mouth_w + pad_x * 2)
        render_fake_pixels(sheet, grid, x, rows_y[3], rng)
    return sheet


def main() -> None:
    out = (
        Path(sys.argv[1])
        if len(sys.argv) > 1
        else Path(__file__).parent / "bloomling_sheet.png"
    )
    sheet = make_sheet()
    save_rgba(out, sheet)
    print(f"wrote {out} ({sheet.width}x{sheet.height})")


if __name__ == "__main__":
    main()
