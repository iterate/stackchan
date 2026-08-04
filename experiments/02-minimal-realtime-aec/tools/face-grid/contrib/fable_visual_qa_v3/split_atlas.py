#!/usr/bin/env python3
"""Split a probe expression-atlas PPM into horizontal PNG bands.

Reviewing a 62-row atlas in one image hides detail; 8-profile bands keep
each face readable. Usage:

    split_atlas.py ATLAS_PPM OUTPUT_DIR [ROWS_PER_BAND] [ROW_HEIGHT]
"""

from __future__ import annotations

import sys
from pathlib import Path

from make_pngs import png_chunk  # reuse the deterministic encoder
import struct
import zlib


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    atlas_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    rows_per_band = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    row_height = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    data = atlas_path.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    width, height = (int(value) for value in dimensions.split())
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{atlas_path} is not a probe PPM")
    output_dir.mkdir(parents=True, exist_ok=True)
    stride = width * 3
    band_height = rows_per_band * row_height
    total_rows = height // row_height
    band = 0
    for top in range(0, height, band_height):
        bottom = min(top + band_height, height)
        scanlines = bytearray()
        for y in range(top, bottom):
            scanlines.append(0)
            scanlines.extend(pixels[y * stride : (y + 1) * stride])
        header = struct.pack(
            ">IIBBBBB", width, bottom - top, 8, 2, 0, 0, 0)
        png = (
            b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
            + png_chunk(b"IEND", b"")
        )
        first = top // row_height
        last = min(bottom // row_height, total_rows) - 1
        name = f"band-{band:02d}-profiles-{first:02d}-{last:02d}.png"
        (output_dir / name).write_bytes(png)
        band += 1
    print(f"{band} bands in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
