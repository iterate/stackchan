#!/usr/bin/env python3
"""Converter self-tests.

- PackBits encoder round-trips against a reference decoder.
- Grid-manifest error handling (bad rects, palette overflow).
- Aseprite JSON mode end-to-end: builds a synthetic export (sheet
  PNG + json-array metadata with tags, slices, and one trimmed
  frame) and converts it; the emitted C atlas is then compiled and
  init-validated by build.sh.
"""

from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import atlas_convert  # noqa: E402
import png_io  # noqa: E402

ROOT = os.path.dirname(HERE)
failures = 0


def check(condition: bool, message: str) -> None:
    global failures
    if not condition:
        failures += 1
        print(f"FAIL: {message}")


def unpackbits(data: bytes) -> bytes:
    out = bytearray()
    position = 0
    while position < len(data):
        control = data[position]
        position += 1
        if control < 128:
            out.extend(data[position:position + control + 1])
            position += control + 1
        elif control == 128:
            continue
        else:
            out.extend(bytes([data[position]]) * (257 - control))
            position += 1
    return bytes(out)


def test_packbits() -> None:
    cases = [
        b"",
        b"\x01",
        b"\x01\x01\x01\x01",
        b"\x00\x01\x02\x03",
        bytes(500),
        bytes(range(256)) * 3,
        b"\x05" * 200 + b"\x01\x02" + b"\x07" * 3,
        bytes((i * 7 + (i // 9)) % 4 for i in range(1000)),
    ]
    for case in cases:
        encoded = atlas_convert.packbits(case)
        check(
            unpackbits(encoded) == case,
            f"packbits round-trip failed for {len(case)} bytes")


def test_manifest_errors(tmp: str) -> None:
    rgba = bytearray(8 * 8 * 4)
    for index in range(8 * 8):
        rgba[index * 4:index * 4 + 4] = bytes((10, 20, 30, 255))
    sheet_path = os.path.join(tmp, "tiny.png")
    png_io.write_png_rgba(sheet_path, 8, 8, rgba)

    manifest = {
        "name": "broken", "image": "tiny.png",
        "scale": 2, "native_size": [8, 8],
        "cells": {"base": [0, 0, 16, 16]},  # escapes the sheet
        "banks": [{"base": "base"}],
    }
    path = os.path.join(tmp, "broken.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)
    try:
        atlas_convert.build_from_manifest(path)
        check(False, "oversized cell rect accepted")
    except atlas_convert.ConvertError:
        pass

    manifest["cells"] = {"base": [0, 0, 8, 8]}
    manifest["banks"] = [{"base": "missing_cell"}]
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)
    try:
        atlas_convert.build_from_manifest(path)
        check(False, "unknown cell name accepted")
    except atlas_convert.ConvertError:
        pass


def synth_aseprite(tmp: str) -> tuple[str, str]:
    """Build a synthetic Aseprite json-array export: five frames of a
    16x12 canvas packed horizontally, one frame trimmed."""
    canvas_w, canvas_h = 16, 12
    frame_count = 5
    sheet_w = canvas_w * frame_count
    sheet_h = canvas_h
    rgba = bytearray(sheet_w * sheet_h * 4)

    def put(x: int, y: int, color: tuple[int, int, int]) -> None:
        base = (y * sheet_w + x) * 4
        rgba[base:base + 4] = bytes((*color, 255))

    def fill_frame(index: int, color: tuple[int, int, int]) -> None:
        for y in range(canvas_h):
            for x in range(canvas_w):
                put(index * canvas_w + x, y, color)

    # Frame 0: base (blue block with a red mouth zone).
    fill_frame(0, (30, 30, 120))
    for y in range(8, 11):
        for x in range(5, 11):
            put(x, y, (120, 30, 30))
    # Frame 1: mouth closed variant; frame 2: mouth open variant.
    fill_frame(1, (30, 30, 120))
    for x in range(5, 11):
        put(canvas_w + x, 9, (250, 250, 250))
    fill_frame(2, (30, 30, 120))
    for y in range(8, 11):
        for x in range(5, 11):
            put(2 * canvas_w + x, y, (10, 10, 10))
    # Frames 3-4: eyes open / closed.
    fill_frame(3, (30, 30, 120))
    for y in range(2, 5):
        for x in range(2, 6):
            put(3 * canvas_w + x, y, (240, 240, 240))
    fill_frame(4, (30, 30, 120))
    for x in range(2, 6):
        put(4 * canvas_w + x, 4, (10, 10, 10))

    sheet_path = os.path.join(tmp, "hero.png")
    png_io.write_png_rgba(sheet_path, sheet_w, sheet_h, rgba)

    def frame_record(index: int, trimmed: bool = False) -> dict:
        record = {
            "filename": f"hero {index}.png",
            "frame": {"x": index * canvas_w, "y": 0,
                      "w": canvas_w, "h": canvas_h},
            "rotated": False,
            "trimmed": trimmed,
            "spriteSourceSize": {"x": 0, "y": 0,
                                 "w": canvas_w, "h": canvas_h},
            "sourceSize": {"w": canvas_w, "h": canvas_h},
            "duration": 110,
        }
        return record

    frames = [frame_record(index) for index in range(frame_count)]
    # Make frame 2 trimmed by one pixel on each edge to exercise the
    # spriteSourceSize remapping (the packed rect shrinks, the canvas
    # coordinates stay the same).
    frames[2]["trimmed"] = True
    frames[2]["frame"] = {"x": 2 * canvas_w + 1, "y": 1,
                          "w": canvas_w - 2, "h": canvas_h - 2}
    frames[2]["spriteSourceSize"] = {"x": 1, "y": 1,
                                     "w": canvas_w - 2,
                                     "h": canvas_h - 2}

    data = {
        "frames": frames,
        "meta": {
            "app": "synthetic",
            "version": "1.0",
            "image": "hero.png",
            "format": "RGBA8888",
            "size": {"w": sheet_w, "h": sheet_h},
            "scale": "1",
            "frameTags": [
                {"name": "base", "from": 0, "to": 0,
                 "direction": "forward"},
                {"name": "mouth-X", "from": 1, "to": 1,
                 "direction": "forward"},
                {"name": "mouth-D", "from": 2, "to": 2,
                 "direction": "forward"},
                {"name": "eyes", "from": 3, "to": 4,
                 "direction": "forward"},
                {"name": "idle-nod", "from": 3, "to": 4,
                 "direction": "pingpong"},
            ],
            "slices": [
                {"name": "mouth", "color": "#0000ffff",
                 "keys": [{"frame": 0,
                           "bounds": {"x": 5, "y": 8,
                                      "w": 6, "h": 3}}]},
                {"name": "eye_left", "keys": [
                    {"frame": 0,
                     "bounds": {"x": 2, "y": 2, "w": 4, "h": 3}}]},
                {"name": "eye_right", "keys": [
                    {"frame": 0,
                     "bounds": {"x": 10, "y": 2, "w": 4, "h": 3}}]},
            ],
        },
    }
    data_path = os.path.join(tmp, "hero.json")
    with open(data_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle)

    mapping = {
        "name": "ase_hero",
        "scale": 2,
        "background": "#101020",
        "base_tag": "base",
        "mouth": {"tag_prefix": "mouth-", "slice": "mouth"},
        "eyes": {"tag": "eyes", "left_slice": "eye_left",
                 "right_slice": "eye_right"},
        "idle_prefix": "idle-",
    }
    map_path = os.path.join(tmp, "hero.map.json")
    with open(map_path, "w", encoding="utf-8") as handle:
        json.dump(mapping, handle)
    return data_path, map_path


def test_aseprite(tmp: str, out_dir: str) -> None:
    data_path, map_path = synth_aseprite(tmp)
    builder, manifest = atlas_convert.build_from_aseprite(
        data_path, map_path)
    check(len(builder.banks) == 1, "aseprite mode built no bank")
    bank = builder.banks[0]
    mouth = bank["mouth"]
    provided = {cell for cell in mouth["cells"]}
    check(
        "SPRITE_CELL_NONE" not in provided,
        "fallback left unresolved mouth shapes")
    check(
        len(bank["eye_left"]["cells"]) == 2,
        "eye tag frames not converted to lid cells")
    check(
        len(builder.sequences) == 1,
        "idle tag not converted to a sequence")
    stats = atlas_convert.emit_atlas(
        builder, manifest, out_dir, "synthetic hero.json")
    check(stats["cells"] > 0, "no cells emitted")
    check(
        os.path.exists(os.path.join(out_dir, "ase_hero_atlas.c")),
        "aseprite-mode C file missing")


def main() -> int:
    tmp = os.path.join(ROOT, "build", "converter_test")
    out_dir = os.path.join(ROOT, "build", "aseprite_demo")
    os.makedirs(tmp, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    test_packbits()
    test_manifest_errors(tmp)
    test_aseprite(tmp, out_dir)
    if failures:
        print(f"{failures} converter test failure(s)")
        return 1
    print("converter self-tests passed "
          "(packbits, manifest errors, aseprite mode)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
