#!/usr/bin/env python3
"""fbfc — fable bitmap font compiler.

Compiles a textual .fbfont glyph source into the FBF1 binary blob consumed
by src/fbf_font.c, plus a C array wrapper for flashing. Standard library
only; output is byte-deterministic for identical input.

Source format (see DESIGN.md):

    font mossbyte8
    line_height 11
    ascent 8
    descent 2
    tracking 0
    fallback U+FFFD

    glyph U+0041            # or: glyph 'A'
    adv 6                   # optional, default width+1
    bearing 0 7             # optional bx by, default 0 <height>
    rows
    .###.
    #...#
    #####
    #...#
    #...#
    end

    alias U+0061 U+0041     # new codepoint, same bitmap and metrics
    kern U+0041 U+0056 -1   # pen adjustment between two codepoints

Pixel characters: '.' transparent, '#' ink (1), '+' shade (2), '@' accent
(3). Identical bitmaps are pooled, so aliases and repeated art cost only a
16-byte glyph record.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

MAGIC = 0x31464246  # "FBF1"
VERSION = 1
HEADER_BYTES = 40
GLYPH_RECORD_BYTES = 16
KERN_RECORD_BYTES = 6
MAX_SIDE = 32
PIXEL_CHARS = {".": 0, "#": 1, "+": 2, "@": 3}


def fail(path, line_no, message):
    raise SystemExit(f"{path}:{line_no}: error: {message}")


def parse_codepoint(token, path, line_no):
    if len(token) == 3 and token[0] == "'" and token[2] == "'":
        return ord(token[1])
    if token.startswith("U+") or token.startswith("u+"):
        try:
            value = int(token[2:], 16)
        except ValueError:
            fail(path, line_no, f"bad codepoint {token!r}")
        if not 0 <= value <= 0x10FFFF:
            fail(path, line_no, f"codepoint {token} out of range")
        return value
    fail(path, line_no, f"expected U+XXXX or 'c', got {token!r}")


class Glyph:
    __slots__ = (
        "codepoint", "advance", "bearing_x", "bearing_y",
        "width", "height", "pixels", "line_no",
    )

    def __init__(self, codepoint, line_no):
        self.codepoint = codepoint
        self.advance = None
        self.bearing_x = 0
        self.bearing_y = None
        self.width = 0
        self.height = 0
        self.pixels = []  # rows of palette indices
        self.line_no = line_no


def parse_font(path: Path):
    props = {}
    glyphs = {}
    aliases = []  # (new_cp, source_cp, line_no)
    kerns = []    # (left_cp, right_cp, dx, line_no)
    fallback_cp = None

    current = None
    in_rows = False
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].rstrip() if not in_rows else raw.rstrip()
        # inside rows, '#' is a pixel, so only blank/end terminates
        if in_rows:
            if line.strip() == "end":
                in_rows = False
                g = current
                if g.pixels:
                    g.height = len(g.pixels)
                    g.width = max(len(r) for r in g.pixels)
                    for r in g.pixels:
                        r.extend([0] * (g.width - len(r)))
                if g.width > MAX_SIDE or g.height > MAX_SIDE:
                    fail(path, line_no, "glyph exceeds 32x32")
                current = None
                continue
            if not line.strip():
                fail(path, line_no, "blank line inside rows")
            try:
                current.pixels.append(
                    [PIXEL_CHARS[c] for c in line.strip()])
            except KeyError:
                fail(path, line_no, f"bad pixel char in {line.strip()!r}")
            continue

        if not line.strip():
            continue
        tokens = line.split()
        keyword = tokens[0]

        if keyword == "glyph":
            if len(tokens) != 2:
                fail(path, line_no, "glyph takes one codepoint")
            cp = parse_codepoint(tokens[1], path, line_no)
            if cp in glyphs:
                fail(path, line_no, f"duplicate glyph U+{cp:04X}")
            current = Glyph(cp, line_no)
            glyphs[cp] = current
        elif keyword == "adv":
            current.advance = int(tokens[1])
        elif keyword == "bearing":
            current.bearing_x = int(tokens[1])
            current.bearing_y = int(tokens[2])
        elif keyword == "rows":
            in_rows = True
        elif keyword == "empty":
            pass  # zero-size bitmap glyph (space)
        elif keyword == "alias":
            new_cp = parse_codepoint(tokens[1], path, line_no)
            src_cp = parse_codepoint(tokens[2], path, line_no)
            aliases.append((new_cp, src_cp, line_no))
        elif keyword == "kern":
            left = parse_codepoint(tokens[1], path, line_no)
            right = parse_codepoint(tokens[2], path, line_no)
            kerns.append((left, right, int(tokens[3]), line_no))
        elif keyword == "fallback":
            fallback_cp = parse_codepoint(tokens[1], path, line_no)
        elif keyword in ("font", "line_height", "ascent", "descent",
                         "tracking"):
            props[keyword] = tokens[1]
        else:
            fail(path, line_no, f"unknown keyword {keyword!r}")
    if in_rows:
        fail(path, 0, "unterminated rows block")

    for key in ("font", "line_height", "ascent", "descent"):
        if key not in props:
            fail(path, 0, f"missing required property {key!r}")

    for new_cp, src_cp, line_no in aliases:
        if src_cp not in glyphs:
            fail(path, line_no, f"alias source U+{src_cp:04X} undefined")
        if new_cp in glyphs:
            fail(path, line_no, f"alias target U+{new_cp:04X} already defined")
        src = glyphs[src_cp]
        g = Glyph(new_cp, line_no)
        g.advance = src.advance
        g.bearing_x = src.bearing_x
        g.bearing_y = src.bearing_y
        g.width = src.width
        g.height = src.height
        g.pixels = src.pixels
        glyphs[new_cp] = g

    for left, right, _dx, line_no in kerns:
        for cp in (left, right):
            if cp not in glyphs:
                fail(path, line_no, f"kern references undefined U+{cp:04X}")

    if fallback_cp is not None and fallback_cp not in glyphs:
        fail(path, 0, f"fallback U+{fallback_cp:04X} undefined")

    return props, glyphs, kerns, fallback_cp


def pack_bitmap(glyph):
    """2 bits per pixel, rows padded to whole bytes, LSB-first in each
    byte: pixel x uses bits (2*(x&3)) .. (2*(x&3)+1)."""
    if glyph.width == 0 or glyph.height == 0:
        return b""
    row_bytes = (glyph.width * 2 + 7) // 8
    out = bytearray()
    for row in glyph.pixels:
        packed = bytearray(row_bytes)
        for x, value in enumerate(row):
            packed[x >> 2] |= value << (2 * (x & 3))
        out += packed
    return bytes(out)


def fnv1a32(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def compile_blob(props, glyphs, kerns, fallback_cp):
    ordered = [glyphs[cp] for cp in sorted(glyphs)]
    index_of = {g.codepoint: i for i, g in enumerate(ordered)}

    pool = bytearray()
    pool_offsets = {}
    bitmap_offset_of = []
    for g in ordered:
        data = pack_bitmap(g)
        if data not in pool_offsets:
            pool_offsets[data] = len(pool)
            pool += data
        bitmap_offset_of.append(pool_offsets[data])

    glyph_table = bytearray()
    for g, bmp_off in zip(ordered, bitmap_offset_of):
        advance = g.advance if g.advance is not None else g.width + 1
        bearing_y = g.bearing_y if g.bearing_y is not None else g.height
        if not 0 <= advance <= 255:
            fail("<font>", g.line_no, f"advance {advance} out of range")
        glyph_table += struct.pack(
            "<IIBBBbbBH",
            g.codepoint, bmp_off, advance, g.width, g.height,
            g.bearing_x, bearing_y, 0, 0)

    kern_rows = sorted(
        (index_of[left], index_of[right], dx)
        for left, right, dx, _ in kerns)
    seen = set()
    kern_table = bytearray()
    for left_i, right_i, dx in kern_rows:
        if (left_i, right_i) in seen:
            raise SystemExit(f"duplicate kern pair {left_i}/{right_i}")
        seen.add((left_i, right_i))
        kern_table += struct.pack("<HHbB", left_i, right_i, dx, 0)

    glyphs_offset = HEADER_BYTES
    kern_offset = glyphs_offset + len(glyph_table)
    bitmap_offset = kern_offset + len(kern_table)
    fallback_index = (
        index_of[fallback_cp] if fallback_cp is not None else 0xFFFF)

    payload = bytes(glyph_table + kern_table + pool)
    header = struct.pack(
        "<IHHHHBBBbHHIIII",
        MAGIC, VERSION, 0,
        len(ordered), len(kern_rows),
        int(props["line_height"]), int(props["ascent"]),
        int(props["descent"]), int(props.get("tracking", "0")),
        fallback_index, 0,
        glyphs_offset, kern_offset, bitmap_offset, len(pool))
    assert len(header) == HEADER_BYTES - 4  # checksum appended below
    blob = header + struct.pack("<I", fnv1a32(payload)) + payload
    return blob, {
        "name": props["font"],
        "glyphs": len(ordered),
        "kerns": len(kern_rows),
        "glyph_table_bytes": len(glyph_table),
        "kern_table_bytes": len(kern_table),
        "bitmap_bytes": len(pool),
        "blob_bytes": HEADER_BYTES + len(payload),
        "pooled_bitmaps": len(pool_offsets),
    }


def emit_c(blob, name, c_path: Path, h_path: Path, source_name):
    symbol = f"fbf_{name}_blob"
    guard = name.upper()
    lines = [
        f"/* Generated by fbfc.py from {source_name}. Do not edit. */",
        f'#include "{h_path.name}"',
        "",
        f"const uint8_t {symbol}[] = {{",
    ]
    for i in range(0, len(blob), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 12])
        lines.append(f"    {chunk},")
    lines += [
        "};",
        "",
        f"const uint32_t {symbol}_size = sizeof({symbol});",
        "",
    ]
    c_path.write_text("\n".join(lines))
    h_path.write_text("\n".join([
        f"/* Generated by fbfc.py from {source_name}. Do not edit. */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f"enum {{ FBF_{guard}_BLOB_BYTES = {len(blob)} }};",
        "",
        f"extern const uint8_t {symbol}[];",
        f"extern const uint32_t {symbol}_size;",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path, help=".fbfont source")
    parser.add_argument("--bin", type=Path, help="write FBF1 blob here")
    parser.add_argument("--c-dir", type=Path,
                        help="write <name>_font.c/.h into this directory")
    parser.add_argument("--report", action="store_true",
                        help="print size report to stdout")
    args = parser.parse_args()

    props, glyphs, kerns, fallback_cp = parse_font(args.source)
    blob, report = compile_blob(props, glyphs, kerns, fallback_cp)
    name = report["name"]

    if args.bin:
        args.bin.parent.mkdir(parents=True, exist_ok=True)
        args.bin.write_bytes(blob)
    if args.c_dir:
        args.c_dir.mkdir(parents=True, exist_ok=True)
        emit_c(blob, name,
               args.c_dir / f"{name}_font.c",
               args.c_dir / f"{name}_font.h",
               args.source.name)
    if args.report:
        for key in ("name", "glyphs", "kerns", "pooled_bitmaps",
                    "glyph_table_bytes", "kern_table_bytes",
                    "bitmap_bytes", "blob_bytes"):
            print(f"{key:18} {report[key]}")
    if not (args.bin or args.c_dir or args.report):
        parser.error("nothing to do: pass --bin, --c-dir, or --report")


if __name__ == "__main__":
    main()
