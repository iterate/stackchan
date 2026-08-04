"""Minimal PNG codec using only the Python standard library.

Supports the subset of PNG needed by the sprite-sheet pipeline:

- reading: 8-bit RGB/RGBA/greyscale(+alpha), and palette images at bit
  depth 1/2/4/8 with optional tRNS transparency; all five scanline
  filters; no Adam7 interlacing (rejected with a clear error).
- writing: 8-bit RGBA, and 8-bit palette images with optional tRNS.

Pixels are exchanged as flat ``bytearray`` rows of RGBA (4 bytes per
pixel) so callers never depend on Pillow/numpy.
"""

from __future__ import annotations

import struct
import zlib

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(ValueError):
    pass


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter(raw: bytes, height: int, bpp: int,
              row_bytes: int) -> bytearray:
    out = bytearray(row_bytes * height)
    pos = 0
    for y in range(height):
        if pos >= len(raw):
            raise PngError("truncated image data")
        filter_type = raw[pos]
        pos += 1
        row = raw[pos:pos + row_bytes]
        if len(row) != row_bytes:
            raise PngError("truncated scanline")
        pos += row_bytes
        base = y * row_bytes
        prior_base = base - row_bytes
        if filter_type == 0:
            out[base:base + row_bytes] = row
        elif filter_type == 1:
            for x in range(row_bytes):
                left = out[base + x - bpp] if x >= bpp else 0
                out[base + x] = (row[x] + left) & 0xFF
        elif filter_type == 2:
            for x in range(row_bytes):
                up = out[prior_base + x] if y > 0 else 0
                out[base + x] = (row[x] + up) & 0xFF
        elif filter_type == 3:
            for x in range(row_bytes):
                left = out[base + x - bpp] if x >= bpp else 0
                up = out[prior_base + x] if y > 0 else 0
                out[base + x] = (row[x] + (left + up) // 2) & 0xFF
        elif filter_type == 4:
            for x in range(row_bytes):
                left = out[base + x - bpp] if x >= bpp else 0
                up = out[prior_base + x] if y > 0 else 0
                up_left = out[prior_base + x - bpp] \
                    if (y > 0 and x >= bpp) else 0
                out[base + x] = (row[x] + _paeth(left, up, up_left)) & 0xFF
        else:
            raise PngError(f"unknown filter type {filter_type}")
    return out


def _unpack_low_depth(row: bytes, width: int, bit_depth: int) -> list[int]:
    if bit_depth == 8:
        return list(row[:width])
    values: list[int] = []
    per_byte = 8 // bit_depth
    mask = (1 << bit_depth) - 1
    for byte in row:
        for slot in range(per_byte):
            shift = 8 - bit_depth * (slot + 1)
            values.append((byte >> shift) & mask)
            if len(values) == width:
                return values
    return values


def read_png(path: str) -> tuple[int, int, bytearray]:
    """Read a PNG file, returning ``(width, height, rgba)`` where rgba
    is a flat bytearray of ``width * height * 4`` bytes, row-major."""
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:8] != _PNG_SIGNATURE:
        raise PngError(f"{path}: not a PNG file")
    pos = 8
    width = height = 0
    bit_depth = color_type = interlace = 0
    palette: list[tuple[int, int, int]] = []
    trns = b""
    idat = bytearray()
    while pos + 8 <= len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        kind = blob[pos + 4:pos + 8]
        data = blob[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filt, interlace = \
                struct.unpack(">IIBBBBB", data)
        elif kind == b"PLTE":
            palette = [
                (data[i], data[i + 1], data[i + 2])
                for i in range(0, len(data) - 2, 3)
            ]
        elif kind == b"tRNS":
            trns = bytes(data)
        elif kind == b"IDAT":
            idat.extend(data)
        elif kind == b"IEND":
            break
    if width == 0 or height == 0:
        raise PngError(f"{path}: missing or empty IHDR")
    if interlace != 0:
        raise PngError(f"{path}: Adam7 interlacing unsupported; "
                       "re-export without interlacing")
    raw = zlib.decompress(bytes(idat))

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if channels is None:
        raise PngError(f"{path}: unsupported color type {color_type}")
    if color_type in (2, 4, 6) and bit_depth != 8:
        raise PngError(f"{path}: only 8-bit depth supported for "
                       f"color type {color_type}")
    if color_type in (0, 3) and bit_depth not in (1, 2, 4, 8):
        raise PngError(f"{path}: unsupported bit depth {bit_depth}")

    bits_per_pixel = channels * bit_depth
    row_bytes = (width * bits_per_pixel + 7) // 8
    bpp = max(1, bits_per_pixel // 8)
    flat = _unfilter(raw, height, bpp, row_bytes)

    rgba = bytearray(width * height * 4)
    for y in range(height):
        row = flat[y * row_bytes:(y + 1) * row_bytes]
        out_base = y * width * 4
        if color_type == 3:
            for x, index in enumerate(_unpack_low_depth(row, width,
                                                        bit_depth)):
                if index >= len(palette):
                    raise PngError(
                        f"{path}: palette index {index} out of range")
                r, g, b = palette[index]
                a = trns[index] if index < len(trns) else 255
                base = out_base + x * 4
                rgba[base:base + 4] = bytes((r, g, b, a))
        elif color_type == 0:
            scale = 255 // ((1 << bit_depth) - 1)
            for x, value in enumerate(_unpack_low_depth(row, width,
                                                        bit_depth)):
                v = value * scale
                base = out_base + x * 4
                rgba[base:base + 4] = bytes((v, v, v, 255))
        elif color_type == 4:
            for x in range(width):
                v = row[x * 2]
                a = row[x * 2 + 1]
                base = out_base + x * 4
                rgba[base:base + 4] = bytes((v, v, v, a))
        elif color_type == 2:
            for x in range(width):
                r, g, b = row[x * 3:x * 3 + 3]
                base = out_base + x * 4
                rgba[base:base + 4] = bytes((r, g, b, 255))
        else:  # color_type == 6
            for x in range(width):
                base = out_base + x * 4
                rgba[base:base + 4] = row[x * 4:x * 4 + 4]
    return width, height, rgba


def _chunk(kind: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + \
        struct.pack(">I", crc)


def write_png_rgba(path: str, width: int, height: int,
                   rgba: bytes | bytearray) -> None:
    if len(rgba) != width * height * 4:
        raise PngError("rgba buffer size mismatch")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    body = _chunk(b"IHDR", ihdr)
    body += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += _chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(_PNG_SIGNATURE + body)


def write_png_indexed(path: str, width: int, height: int,
                      palette: list[tuple[int, int, int]],
                      indices: bytes | bytearray,
                      transparent_index: int | None = None) -> None:
    if len(indices) != width * height:
        raise PngError("index buffer size mismatch")
    if not palette or len(palette) > 256:
        raise PngError("palette must hold 1..256 entries")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(color) for color in palette)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(indices[y * width:(y + 1) * width])
    body = _chunk(b"IHDR", ihdr)
    body += _chunk(b"PLTE", plte)
    if transparent_index is not None:
        trns = bytes(
            0 if i == transparent_index else 255
            for i in range(transparent_index + 1)
        )
        body += _chunk(b"tRNS", trns)
    body += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    body += _chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(_PNG_SIGNATURE + body)
