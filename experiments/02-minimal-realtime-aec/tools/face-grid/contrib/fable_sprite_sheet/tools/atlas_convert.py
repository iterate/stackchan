#!/usr/bin/env python3
"""Convert sprite-sheet art into FSPR C data for the playback engine.

Two input modes, both dependency-free (stdlib only):

1. Grid manifest (``--manifest face.json``): a JSON file naming cell
   rectangles inside one PNG sheet, plus banks/sequences/cycles. This
   is the native authoring path and what the demo generator emits.

2. Aseprite export (``--aseprite data.json --map map.json``): consumes
   the JSON written by
   ``aseprite -b face.ase --sheet sheet.png --data data.json \
       --format json-array --list-tags --list-slices``
   (json-hash also accepted). Frames are full-canvas; the map file
   names which tags hold mouth/eye/brow/pupil frames and which slices
   provide the crop rectangles + anchors. See docs/FORMAT.md.

Output: ``<name>_atlas.c`` / ``<name>_atlas.h`` with palette, PackBits
cell blob, banks, sequences, and palette cycles, plus a packing report.

Mouth shapes use the Rhubarb/Preston Blair canon (X A B C D E F G H).
Sheets may provide any subset; missing shapes resolve through the
fallback chains below at build time, so a two-frame open/closed sheet
still animates.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png_io  # noqa: E402

SHAPES = "XABCDEFGH"  # index order == sprite_mouth_shape_t

FALLBACK = {
    "X": ["X", "A", "C", "B", "D"],
    "A": ["A", "X", "B", "C", "D"],
    "B": ["B", "C", "G", "X", "D"],
    "C": ["C", "B", "D", "X", "A"],
    "D": ["D", "C", "B", "X", "A"],
    "E": ["E", "F", "C", "D", "X"],
    "F": ["F", "E", "C", "B", "X"],
    "G": ["G", "B", "A", "C", "X"],
    "H": ["H", "C", "B", "D", "X"],
}

DEFAULT_SELECTOR = {
    "open_min": 20, "press_min": 128,
    "teeth_min": 160, "teeth_open": 96, "teeth_round": 140,
    "round_min": 150, "round_open": 128,
    "wide_min": 170, "wide_open": 120, "open_wide": 170,
}
SELECTOR_ORDER = [
    "open_min", "press_min", "teeth_min", "teeth_open", "teeth_round",
    "round_min", "round_open", "wide_min", "wide_open", "open_wide",
]

# Milliseconds; converted to 16 kHz samples on emission. Defaults match
# SPRITE_TIMING_DEFAULTS in sprite_sheet.h (70 ms Rhubarb shape hold,
# 120 ms close delay, 80/40/120 ms blink, 4 s blink window...).
DEFAULT_TIMING_MS = {
    "mouth_min_hold": 70, "mouth_close_delay": 120,
    "blink_close": 80, "blink_hold": 40, "blink_open": 120,
    "blink_window": 4000, "gaze_window": 1300,
    "idle_window": 6000, "breathe_period": 4200,
}
TIMING_U16 = [
    "mouth_min_hold", "mouth_close_delay",
    "blink_close", "blink_hold", "blink_open",
]
TIMING_U32 = [
    "blink_window", "gaze_window", "idle_window", "breathe_period",
]

FEATURE_FLAGS = {
    "breathe": 1 << 0,
    "saccades": 1 << 1,
    "auto_blink": 1 << 2,
}

MS_TO_SAMPLES = 16


class ConvertError(ValueError):
    pass


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def parse_color(text: str) -> int:
    text = text.lstrip("#")
    if len(text) == 3:
        text = "".join(ch * 2 for ch in text)
    if len(text) != 6:
        raise ConvertError(f"bad colour literal '#{text}'")
    value = int(text, 16)
    return rgb565(value >> 16, (value >> 8) & 0xFF, value & 0xFF)


def packbits(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out.append(257 - run)
            out.append(data[i])
            i += run
            continue
        start = i
        i += 1
        while i < n and (i - start) < 128:
            if i + 2 < n and data[i] == data[i + 1] == data[i + 2]:
                break
            i += 1
        chunk = data[start:i]
        out.append(len(chunk) - 1)
        out.extend(chunk)
    return bytes(out)


class Sheet:
    """RGBA pixel source with 565 quantisation and transparency."""

    def __init__(self, path: str):
        self.width, self.height, self.rgba = png_io.read_png(path)

    def pixel(self, x: int, y: int) -> tuple[int, bool]:
        """Return (rgb565, opaque) for a sheet coordinate."""
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return 0, False
        base = (y * self.width + x) * 4
        r, g, b, a = self.rgba[base:base + 4]
        if a < 128:
            return 0, False
        return rgb565(r, g, b), True


class AtlasBuilder:
    def __init__(self, name: str):
        self.name = name
        self.palette: list[int] = [0x0000]  # index 0 = transparent
        self.color_to_index: dict[int, int] = {}
        self.cycle_ranges: list[tuple[int, int, int]] = []
        self.cells: list[dict] = []
        self.cell_by_key: dict = {}
        self.cell_names: dict[str, int] = {}
        self.no_trim: set[str] = set()
        self.banks: list[dict] = []
        self.sequences: list[dict] = []
        self.stats_raw = 0

    def reserve_cycles(self, cycles: list[dict]) -> None:
        """Cycle colours must be contiguous palette entries, so they
        are allocated first, in manifest order."""
        for cycle in cycles:
            colors = [parse_color(c) for c in cycle["colors"]]
            if len(colors) < 2:
                raise ConvertError("palette cycle needs >= 2 colours")
            first = len(self.palette)
            for color in colors:
                if color in self.color_to_index:
                    raise ConvertError(
                        "cycle colour collides with an earlier cycle "
                        f"entry: {color:#06x}")
                self.color_to_index[color] = len(self.palette)
                self.palette.append(color)
            period_ms = int(cycle.get("period_ms", 120))
            self.cycle_ranges.append(
                (first, len(colors), period_ms * MS_TO_SAMPLES))

    def color_index(self, color: int) -> int:
        index = self.color_to_index.get(color)
        if index is None:
            if len(self.palette) >= 256:
                raise ConvertError(
                    "palette overflow: more than 255 opaque colours")
            index = len(self.palette)
            self.color_to_index[color] = index
            self.palette.append(color)
        return index

    def add_cell_pixels(
            self, name: str,
            width: int, height: int,
            pixels: list[tuple[int, bool]]) -> int:
        """Register a cell from (rgb565, opaque) pixels; trims, dedups
        and encodes. Returns the cell index."""
        indices = bytearray(width * height)
        min_x, min_y, max_x, max_y = width, height, -1, -1
        for y in range(height):
            for x in range(width):
                color, opaque = pixels[y * width + x]
                if not opaque:
                    continue
                indices[y * width + x] = self.color_index(color)
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
        if max_x < 0:
            trimmed = bytes(1)
            t_w, t_h, off_x, off_y = 1, 1, 0, 0
        elif name in self.no_trim:
            trimmed = bytes(indices)
            t_w, t_h, off_x, off_y = width, height, 0, 0
        else:
            t_w = max_x - min_x + 1
            t_h = max_y - min_y + 1
            off_x, off_y = min_x, min_y
            trimmed = b"".join(
                bytes(indices[y * width + min_x:
                              y * width + min_x + t_w])
                for y in range(min_y, max_y + 1))
        key = (t_w, t_h, off_x, off_y, trimmed)
        if key in self.cell_by_key:
            index = self.cell_by_key[key]
            self.cell_names[name] = index
            return index
        encoded = packbits(trimmed)
        encoding = 1
        if len(encoded) >= len(trimmed):
            encoded = trimmed
            encoding = 0
        index = len(self.cells)
        self.cells.append({
            "width": t_w, "height": t_h,
            "offset_x": off_x, "offset_y": off_y,
            "data": encoded, "encoding": encoding,
        })
        self.stats_raw += width * height
        self.cell_by_key[key] = index
        self.cell_names[name] = index
        return index

    def cell(self, name: str) -> int:
        if name not in self.cell_names:
            raise ConvertError(f"unknown cell '{name}'")
        return self.cell_names[name]


def load_rect_pixels(sheet: Sheet, rect: list[int]) \
        -> list[tuple[int, bool]]:
    x, y, w, h = rect
    if w <= 0 or h <= 0 or x < 0 or y < 0 or \
            x + w > sheet.width or y + h > sheet.height:
        raise ConvertError(f"cell rect {rect} escapes the sheet")
    return [
        sheet.pixel(x + cx, y + cy)
        for cy in range(h) for cx in range(w)
    ]


def resolve_mouth(builder: AtlasBuilder, mouth: dict | None) -> dict:
    slot = {
        "x": 0, "y": 0,
        "cells": ["SPRITE_CELL_NONE"] * len(SHAPES),
    }
    if mouth is None:
        return slot
    slot["x"], slot["y"] = mouth["anchor"]
    provided = {
        shape: builder.cell(cell_name)
        for shape, cell_name in mouth["shapes"].items()
    }
    unknown = set(provided) - set(SHAPES)
    if unknown:
        raise ConvertError(f"unknown mouth shapes {sorted(unknown)}")
    if not provided:
        raise ConvertError("mouth slot present but no shapes given")
    for position, shape in enumerate(SHAPES):
        for candidate in FALLBACK[shape]:
            if candidate in provided:
                slot["cells"][position] = str(provided[candidate])
                break
        else:
            # chains cover every provided set; land on any shape
            slot["cells"][position] = str(next(iter(provided.values())))
    return slot


def resolve_eye(builder: AtlasBuilder, eye: dict | None) -> dict:
    if eye is None:
        return {"x": 0, "y": 0, "cells": [], "flags": 0}
    return {
        "x": eye["anchor"][0], "y": eye["anchor"][1],
        "cells": [builder.cell(name) for name in eye["lids"]],
        "flags": 1 if eye.get("flip_x") else 0,
    }


def resolve_pupil(builder: AtlasBuilder, pupil: dict | None) -> dict:
    if pupil is None:
        return {
            "x": 0, "y": 0, "min_x": 0, "min_y": 0,
            "max_x": 0, "max_y": 0,
            "cell": "SPRITE_CELL_NONE", "range_x": 0, "range_y": 0,
        }
    x, y = pupil["anchor"]
    range_x, range_y = pupil.get("range", [2, 1])
    clamp = pupil.get(
        "clamp", [x - range_x, y - range_y, x + range_x, y + range_y])
    return {
        "x": x, "y": y,
        "min_x": clamp[0], "min_y": clamp[1],
        "max_x": clamp[2], "max_y": clamp[3],
        "cell": str(builder.cell(pupil["cell"])),
        "range_x": range_x, "range_y": range_y,
    }


def resolve_brow(builder: AtlasBuilder, brow: dict | None) -> dict:
    if brow is None:
        return {"x": 0, "y": 0, "cells": [], "max_lift": 0, "flags": 0}
    return {
        "x": brow["anchor"][0], "y": brow["anchor"][1],
        "cells": [builder.cell(name) for name in brow["levels"]],
        "max_lift": brow.get("max_lift", 2),
        "flags": 1 if brow.get("flip_x") else 0,
    }


def collect_no_trim(builder: AtlasBuilder, banks: list[dict]) -> None:
    """Cells referenced by a flipped slot must keep their full box, as
    the engine mirrors inside the cell rectangle only."""
    for bank in banks:
        for slot_name in ("eye_left", "eye_right"):
            slot = bank.get(slot_name)
            if slot and slot.get("flip_x"):
                builder.no_trim.update(slot["lids"])
        for slot_name in ("brow_left", "brow_right"):
            slot = bank.get(slot_name)
            if slot and slot.get("flip_x"):
                builder.no_trim.update(slot["levels"])


def build_from_manifest(manifest_path: str) -> tuple[AtlasBuilder, dict]:
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    base_dir = os.path.dirname(os.path.abspath(manifest_path))
    sheet = Sheet(os.path.join(base_dir, manifest["image"]))
    builder = AtlasBuilder(manifest["name"])
    builder.reserve_cycles(manifest.get("cycles", []))
    collect_no_trim(builder, manifest.get("banks", []))
    for name, rect in manifest["cells"].items():
        builder.add_cell_pixels(
            name, rect[2], rect[3], load_rect_pixels(sheet, rect))
    for bank in manifest.get("banks", []):
        builder.banks.append({
            "base_cell": str(builder.cell(bank["base"]))
            if bank.get("base") else "SPRITE_CELL_NONE",
            "mouth": resolve_mouth(builder, bank.get("mouth")),
            "eye_left": resolve_eye(builder, bank.get("eye_left")),
            "eye_right": resolve_eye(builder, bank.get("eye_right")),
            "pupil_left": resolve_pupil(builder, bank.get("pupil_left")),
            "pupil_right": resolve_pupil(
                builder, bank.get("pupil_right")),
            "brow_left": resolve_brow(builder, bank.get("brow_left")),
            "brow_right": resolve_brow(builder, bank.get("brow_right")),
        })
    if not builder.banks:
        raise ConvertError("manifest defines no banks")
    for sequence in manifest.get("sequences", []):
        builder.sequences.append({
            "frames": [
                {
                    "cell": builder.cell(frame["cell"]),
                    "duration": int(frame["ms"]) * MS_TO_SAMPLES,
                    "x": frame["at"][0], "y": frame["at"][1],
                }
                for frame in sequence["frames"]
            ],
            "flags": 1 if sequence.get("while_speaking") else 0,
        })
    return builder, manifest


# ---------------------------------------------------------------- #
# Aseprite JSON export mode                                        #
# ---------------------------------------------------------------- #

class AseFrame:
    """One exported frame; samples pixels in canvas coordinates,
    honouring Aseprite's trim offsets (spriteSourceSize)."""

    def __init__(self, sheet: Sheet, record: dict):
        if record.get("rotated"):
            raise ConvertError(
                "rotated frames unsupported; disable rotation")
        self.sheet = sheet
        frame = record["frame"]
        source = record.get("spriteSourceSize", {
            "x": 0, "y": 0, "w": frame["w"], "h": frame["h"]})
        size = record.get(
            "sourceSize", {"w": frame["w"], "h": frame["h"]})
        self.atlas_x = frame["x"]
        self.atlas_y = frame["y"]
        self.trim_x = source["x"]
        self.trim_y = source["y"]
        self.trim_w = frame["w"]
        self.trim_h = frame["h"]
        self.canvas_w = size["w"]
        self.canvas_h = size["h"]
        self.duration_ms = record.get("duration", 100)

    def canvas_pixel(self, x: int, y: int) -> tuple[int, bool]:
        tx = x - self.trim_x
        ty = y - self.trim_y
        if tx < 0 or ty < 0 or tx >= self.trim_w or ty >= self.trim_h:
            return 0, False
        return self.sheet.pixel(self.atlas_x + tx, self.atlas_y + ty)

    def crop(self, rect: list[int]) -> list[tuple[int, bool]]:
        x, y, w, h = rect
        return [
            self.canvas_pixel(x + cx, y + cy)
            for cy in range(h) for cx in range(w)
        ]


def normalise_frames(data: dict) -> list[dict]:
    frames = data["frames"]
    if isinstance(frames, dict):
        return [
            dict(record, filename=key)
            for key, record in frames.items()
        ]
    return frames


def ase_tag_frames(data: dict, name: str) -> list[int]:
    for tag in data.get("meta", {}).get("frameTags", []):
        if tag["name"] == name:
            span = list(range(tag["from"], tag["to"] + 1))
            return span[::-1] if tag.get("direction") == "reverse" \
                else span
    return []


def ase_slice_bounds(data: dict, name: str) -> list[int] | None:
    for entry in data.get("meta", {}).get("slices", []):
        if entry["name"] == name:
            bounds = entry["keys"][0]["bounds"]
            return [bounds["x"], bounds["y"], bounds["w"], bounds["h"]]
    return None


def build_from_aseprite(data_path: str, map_path: str) \
        -> tuple[AtlasBuilder, dict]:
    with open(data_path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    with open(map_path, "r", encoding="utf-8") as handle:
        mapping = json.load(handle)
    base_dir = os.path.dirname(os.path.abspath(data_path))
    image = mapping.get("image") or data["meta"]["image"]
    sheet = Sheet(os.path.join(base_dir, image))
    frames = [AseFrame(sheet, record)
              for record in normalise_frames(data)]
    if not frames:
        raise ConvertError("aseprite export holds no frames")
    canvas = [frames[0].canvas_w, frames[0].canvas_h]

    builder = AtlasBuilder(mapping["name"])
    builder.reserve_cycles(mapping.get("cycles", []))
    bank: dict = {}

    base_tag = mapping.get("base_tag", "base")
    base_frames = ase_tag_frames(data, base_tag)
    if not base_frames:
        raise ConvertError(f"missing tag '{base_tag}'")
    builder.add_cell_pixels(
        "base", canvas[0], canvas[1],
        frames[base_frames[0]].crop([0, 0, canvas[0], canvas[1]]))
    bank["base"] = "base"

    mouth_map = mapping.get("mouth")
    if mouth_map:
        bounds = ase_slice_bounds(data, mouth_map.get("slice", "mouth"))
        if bounds is None:
            raise ConvertError("mouth slice missing from export")
        shapes = {}
        prefix = mouth_map.get("tag_prefix", "mouth-")
        for shape in SHAPES:
            span = ase_tag_frames(data, prefix + shape)
            if span:
                cell_name = f"mouth_{shape}"
                builder.add_cell_pixels(
                    cell_name, bounds[2], bounds[3],
                    frames[span[0]].crop(bounds))
                shapes[shape] = cell_name
        if not shapes:
            raise ConvertError(
                f"no '{prefix}X'..'{prefix}H' tags found")
        bank["mouth"] = {"anchor": bounds[:2], "shapes": shapes}

    eyes_map = mapping.get("eyes")
    if eyes_map:
        span = ase_tag_frames(data, eyes_map.get("tag", "eyes"))
        if not span:
            raise ConvertError("eyes tag missing from export")
        for side in ("left", "right"):
            slice_name = eyes_map.get(
                f"{side}_slice", f"eye_{side}")
            bounds = ase_slice_bounds(data, slice_name)
            if bounds is None:
                continue
            lids = []
            for position, frame_index in enumerate(span):
                cell_name = f"eye_{side}_{position}"
                builder.add_cell_pixels(
                    cell_name, bounds[2], bounds[3],
                    frames[frame_index].crop(bounds))
                lids.append(cell_name)
            bank[f"eye_{side}"] = {
                "anchor": bounds[:2], "lids": lids}

    pupil_map = mapping.get("pupil")
    if pupil_map:
        span = ase_tag_frames(data, pupil_map.get("tag", "pupil"))
        if not span:
            raise ConvertError("pupil tag missing from export")
        for side in ("left", "right"):
            slice_name = pupil_map.get(
                f"{side}_slice", f"pupil_{side}")
            bounds = ase_slice_bounds(data, slice_name)
            if bounds is None:
                continue
            cell_name = f"pupil_{side}"
            builder.add_cell_pixels(
                cell_name, bounds[2], bounds[3],
                frames[span[0]].crop(bounds))
            bank[f"pupil_{side}"] = {
                "anchor": bounds[:2], "cell": cell_name,
                "range": pupil_map.get("range", [2, 1]),
            }

    brows_map = mapping.get("brows")
    if brows_map:
        span = ase_tag_frames(data, brows_map.get("tag", "brows"))
        if not span:
            raise ConvertError("brows tag missing from export")
        for side in ("left", "right"):
            slice_name = brows_map.get(
                f"{side}_slice", f"brow_{side}")
            bounds = ase_slice_bounds(data, slice_name)
            if bounds is None:
                continue
            levels = []
            for position, frame_index in enumerate(span):
                cell_name = f"brow_{side}_{position}"
                builder.add_cell_pixels(
                    cell_name, bounds[2], bounds[3],
                    frames[frame_index].crop(bounds))
                levels.append(cell_name)
            bank[f"brow_{side}"] = {
                "anchor": bounds[:2], "levels": levels,
                "max_lift": brows_map.get("max_lift", 2),
            }

    builder.banks.append({
        "base_cell": str(builder.cell("base")),
        "mouth": resolve_mouth(builder, bank.get("mouth")),
        "eye_left": resolve_eye(builder, bank.get("eye_left")),
        "eye_right": resolve_eye(builder, bank.get("eye_right")),
        "pupil_left": resolve_pupil(builder, bank.get("pupil_left")),
        "pupil_right": resolve_pupil(builder, bank.get("pupil_right")),
        "brow_left": resolve_brow(builder, bank.get("brow_left")),
        "brow_right": resolve_brow(builder, bank.get("brow_right")),
    })

    idle_prefix = mapping.get("idle_prefix", "idle-")
    for tag in data.get("meta", {}).get("frameTags", []):
        if not tag["name"].startswith(idle_prefix):
            continue
        span = ase_tag_frames(data, tag["name"])
        sequence = {"frames": [], "flags": 0}
        for position, frame_index in enumerate(span):
            cell_name = f"{tag['name']}_{position}"
            builder.add_cell_pixels(
                cell_name, canvas[0], canvas[1],
                frames[frame_index].crop(
                    [0, 0, canvas[0], canvas[1]]))
            sequence["frames"].append({
                "cell": builder.cell(cell_name),
                "duration":
                    frames[frame_index].duration_ms * MS_TO_SAMPLES,
                "x": 0, "y": 0,
            })
        builder.sequences.append(sequence)

    manifest = {
        "name": mapping["name"],
        "scale": mapping.get("scale", 2),
        "native_size": canvas,
        "background": mapping.get("background", "#000000"),
        "features": mapping.get(
            "features", ["breathe", "saccades", "auto_blink"]),
        "selector": mapping.get("selector", {}),
        "timing": mapping.get("timing", {}),
    }
    return builder, manifest


# ---------------------------------------------------------------- #
# C emission                                                       #
# ---------------------------------------------------------------- #

def emit_bytes(values: bytes, indent: str = "    ") -> str:
    lines = []
    for start in range(0, len(values), 12):
        chunk = values[start:start + 12]
        lines.append(
            indent + " ".join(f"{value}," for value in chunk))
    return "\n".join(lines)


def emit_u16_list(values: list[int], indent: str = "    ") -> str:
    lines = []
    for start in range(0, len(values), 8):
        chunk = values[start:start + 8]
        lines.append(
            indent + " ".join(f"0x{value:04x}," for value in chunk))
    return "\n".join(lines)


def emit_mouth(slot: dict) -> str:
    cells = ", ".join(slot["cells"])
    return (f"        .mouth = {{ .x = {slot['x']}, "
            f".y = {slot['y']},\n"
            f"            .cells = {{ {cells} }} }},")


def emit_eye(name: str, slot: dict) -> str:
    cells = ", ".join(str(c) for c in slot["cells"])
    cells = cells or "0"
    return (f"        .{name} = {{ .x = {slot['x']}, "
            f".y = {slot['y']}, .cell_count = {len(slot['cells'])},\n"
            f"            .cells = {{ {cells} }}, "
            f".flags = {slot['flags']} }},")


def emit_pupil(name: str, slot: dict) -> str:
    return (f"        .{name} = {{ .x = {slot['x']}, "
            f".y = {slot['y']},\n"
            f"            .min_x = {slot['min_x']}, "
            f".min_y = {slot['min_y']}, "
            f".max_x = {slot['max_x']}, .max_y = {slot['max_y']},\n"
            f"            .cell = {slot['cell']}, "
            f".range_x = {slot['range_x']}, "
            f".range_y = {slot['range_y']} }},")


def emit_brow(name: str, slot: dict) -> str:
    cells = ", ".join(str(c) for c in slot["cells"])
    cells = cells or "0"
    return (f"        .{name} = {{ .x = {slot['x']}, "
            f".y = {slot['y']}, .cell_count = {len(slot['cells'])},\n"
            f"            .cells = {{ {cells} }}, "
            f".max_lift = {slot['max_lift']}, "
            f".flags = {slot['flags']} }},")


def emit_atlas(builder: AtlasBuilder, manifest: dict,
               out_dir: str, source_note: str) -> dict:
    name = builder.name
    blob = bytearray()
    cell_rows = []
    for cell in builder.cells:
        offset = len(blob)
        blob.extend(cell["data"])
        cell_rows.append(
            f"    {{ {cell['width']}, {cell['height']}, "
            f"{cell['offset_x']}, {cell['offset_y']}, "
            f"{offset}, {len(cell['data'])}, "
            f"{cell['encoding']}, {{0, 0, 0}} }},")

    selector = dict(DEFAULT_SELECTOR)
    selector.update(manifest.get("selector", {}))
    timing_ms = dict(DEFAULT_TIMING_MS)
    timing_ms.update(manifest.get("timing", {}))

    flags = 0
    for feature in manifest.get(
            "features", ["breathe", "saccades", "auto_blink"]):
        if feature not in FEATURE_FLAGS:
            raise ConvertError(f"unknown feature '{feature}'")
        flags |= FEATURE_FLAGS[feature]

    native_w, native_h = manifest["native_size"]
    scale = manifest.get("scale", 2)
    if native_w * scale > 160 or native_h * scale > 120:
        raise ConvertError(
            f"native size {native_w}x{native_h} at scale {scale} "
            "exceeds the 160x120 output")

    parts = [f"/* Generated by atlas_convert.py from {source_note}."]
    parts.append(" * DO NOT EDIT; re-run the converter instead. */")
    parts.append('#include "sprite_sheet.h"')
    parts.append("")
    parts.append(
        f"static const uint16_t {name}_palette"
        f"[{len(builder.palette)}] = {{")
    parts.append(emit_u16_list(builder.palette))
    parts.append("};")
    parts.append("")
    parts.append(
        f"static const uint8_t {name}_blob[{len(blob)}] = {{")
    parts.append(emit_bytes(bytes(blob)))
    parts.append("};")
    parts.append("")
    parts.append(
        f"static const sprite_cell_t {name}_cells"
        f"[{len(builder.cells)}] = {{")
    parts.extend(cell_rows)
    parts.append("};")
    parts.append("")

    for index, sequence in enumerate(builder.sequences):
        parts.append(
            f"static const sprite_seq_frame_t {name}_seq{index}_frames"
            f"[{len(sequence['frames'])}] = {{")
        for frame in sequence["frames"]:
            parts.append(
                f"    {{ {frame['cell']}, {frame['duration']}, "
                f"{frame['x']}, {frame['y']} }},")
        parts.append("};")
    if builder.sequences:
        parts.append(
            f"static const sprite_sequence_t {name}_sequences"
            f"[{len(builder.sequences)}] = {{")
        for index, sequence in enumerate(builder.sequences):
            parts.append(
                f"    {{ {name}_seq{index}_frames, "
                f"{len(sequence['frames'])}, "
                f"{sequence['flags']}, 0 }},")
        parts.append("};")
        parts.append("")

    if builder.cycle_ranges:
        parts.append(
            f"static const sprite_cycle_t {name}_cycles"
            f"[{len(builder.cycle_ranges)}] = {{")
        for first, count, period in builder.cycle_ranges:
            parts.append(
                f"    {{ {first}, {count}, {{0, 0}}, {period} }},")
        parts.append("};")
        parts.append("")

    parts.append(
        f"static const sprite_bank_t {name}_banks"
        f"[{len(builder.banks)}] = {{")
    for bank in builder.banks:
        parts.append("    {")
        parts.append(f"        .base_cell = {bank['base_cell']},")
        parts.append(emit_mouth(bank["mouth"]))
        parts.append(emit_eye("eye_left", bank["eye_left"]))
        parts.append(emit_eye("eye_right", bank["eye_right"]))
        parts.append(emit_pupil("pupil_left", bank["pupil_left"]))
        parts.append(emit_pupil("pupil_right", bank["pupil_right"]))
        parts.append(emit_brow("brow_left", bank["brow_left"]))
        parts.append(emit_brow("brow_right", bank["brow_right"]))
        parts.append("    },")
    parts.append("};")
    parts.append("")

    selector_values = ", ".join(
        str(selector[key]) for key in SELECTOR_ORDER)
    timing_u16 = ", ".join(
        str(timing_ms[key] * MS_TO_SAMPLES) for key in TIMING_U16)
    timing_u32 = ", ".join(
        str(timing_ms[key] * MS_TO_SAMPLES) for key in TIMING_U32)

    parts.append(f"const sprite_atlas_t {name}_atlas = {{")
    parts.append("    .magic = SPRITE_SHEET_MAGIC,")
    parts.append("    .version = SPRITE_SHEET_VERSION,")
    parts.append(f"    .native_width = {native_w},")
    parts.append(f"    .native_height = {native_h},")
    parts.append(f"    .scale = {scale},")
    parts.append("    .transparent_index = 0,")
    parts.append(f"    .palette_count = {len(builder.palette)},")
    parts.append(f"    .cell_count = {len(builder.cells)},")
    parts.append(f"    .bank_count = {len(builder.banks)},")
    parts.append(f"    .sequence_count = {len(builder.sequences)},")
    parts.append(f"    .flags = {flags},")
    parts.append(
        f"    .background = 0x{parse_color(manifest.get('background', '#000000')):04x},")
    parts.append(f"    .selector = {{ {selector_values} }},")
    parts.append(f"    .timing = {{ {timing_u16}, 0, {timing_u32} }},")
    parts.append(f"    .palette = {name}_palette,")
    parts.append(f"    .cells = {name}_cells,")
    parts.append(f"    .blob = {name}_blob,")
    parts.append(f"    .blob_size = {len(blob)},")
    parts.append(f"    .banks = {name}_banks,")
    parts.append(
        f"    .sequences = {name}_sequences,"
        if builder.sequences else "    .sequences = 0,")
    parts.append(
        f"    .cycles = {name}_cycles,"
        if builder.cycle_ranges else "    .cycles = 0,")
    parts.append(f"    .cycle_count = {len(builder.cycle_ranges)},")
    parts.append(f'    .name = "{name}",')
    parts.append("};")
    parts.append("")

    os.makedirs(out_dir, exist_ok=True)
    c_path = os.path.join(out_dir, f"{name}_atlas.c")
    with open(c_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(parts))
    h_path = os.path.join(out_dir, f"{name}_atlas.h")
    with open(h_path, "w", encoding="utf-8") as handle:
        handle.write(
            f"/* Generated by atlas_convert.py from {source_note}. "
            "DO NOT EDIT. */\n"
            "#pragma once\n\n"
            '#include "sprite_sheet.h"\n\n'
            f"extern const sprite_atlas_t {name}_atlas;\n")

    stats = {
        "name": name,
        "cells": len(builder.cells),
        "palette_entries": len(builder.palette),
        "blob_bytes": len(blob),
        "raw_index_bytes": builder.stats_raw,
        "cell_table_bytes": len(builder.cells) * 16,
        "palette_bytes": len(builder.palette) * 2,
        "compression_ratio": round(
            len(blob) / builder.stats_raw, 3) if builder.stats_raw
        else 1.0,
        "flash_estimate_bytes": len(blob) +
        len(builder.cells) * 16 + len(builder.palette) * 2 +
        len(builder.banks) * 160 + len(builder.sequences) * 8,
    }
    return stats


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", help="grid manifest JSON")
    parser.add_argument("--aseprite", help="Aseprite export JSON")
    parser.add_argument("--map", help="role map JSON (aseprite mode)")
    parser.add_argument("--out-dir", default="generated")
    parser.add_argument("--stats", help="write packing stats JSON here")
    arguments = parser.parse_args()

    try:
        if arguments.manifest:
            builder, manifest = build_from_manifest(arguments.manifest)
            source_note = os.path.basename(arguments.manifest)
        elif arguments.aseprite:
            if not arguments.map:
                raise ConvertError("--aseprite requires --map")
            builder, manifest = build_from_aseprite(
                arguments.aseprite, arguments.map)
            source_note = os.path.basename(arguments.aseprite)
        else:
            raise ConvertError("pass --manifest or --aseprite")
        stats = emit_atlas(
            builder, manifest, arguments.out_dir, source_note)
    except (ConvertError, png_io.PngError, OSError,
            KeyError, json.JSONDecodeError) as error:
        print(f"atlas_convert: error: {error!r}", file=sys.stderr)
        return 1

    print(
        f"{stats['name']}: {stats['cells']} cells, "
        f"{stats['palette_entries']} colours, "
        f"blob {stats['blob_bytes']} B "
        f"({stats['compression_ratio']:.0%} of raw indices), "
        f"~{stats['flash_estimate_bytes']} B flash")
    if arguments.stats:
        with open(arguments.stats, "w", encoding="utf-8") as handle:
            json.dump(stats, handle, indent=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
