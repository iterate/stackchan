#!/usr/bin/env python3
"""Mirror fvqa probe PPM sheets to PNG and write a small gallery page.

Self-contained: PNG encoding is done with zlib only, exactly like the
production quality tool, so no image package is needed.
"""

from __future__ import annotations

import json
import struct
import sys
import zlib
from pathlib import Path


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def ppm_to_png(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    width, height = (int(value) for value in dimensions.split())
    if magic != b"P6" or maximum != b"255":
        raise ValueError(f"{source} is not a probe PPM")
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"{source}: {len(pixels)} != {expected}")
    stride = width * 3
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)
        scanlines.extend(pixels[y * stride : (y + 1) * stride])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    destination.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9))
        + png_chunk(b"IEND", b"")
    )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: make_pngs.py OUTPUT_DIR", file=sys.stderr)
        return 2
    output_dir = Path(sys.argv[1])
    report = json.loads(
        (output_dir / "acceptance.json").read_text(encoding="utf-8")
    )
    png_dir = output_dir / "sheets-png"
    png_dir.mkdir(parents=True, exist_ok=True)
    cards: list[str] = []
    for profile in report["profiles"]:
        slug = profile["slug"]
        ppm_to_png(
            output_dir / "sheets-ppm" / f"{slug}.ppm",
            png_dir / f"{slug}.png",
        )
        flags = profile["flags"]
        badges = []
        if flags["dead_eyes"]:
            badges.append("dead-eyes")
        if flags["silent_mouth"]:
            badges.append("silent-mouth")
        if flags["no_mouth"]:
            badges.append("no-mouth")
        if flags["clip_suspect_emotions"]:
            badges.append(f"clip×{flags['clip_suspect_emotions']}")
        if flags["corner_detach_emotions"]:
            badges.append(
                f"corner-detach×{flags['corner_detach_emotions']}"
            )
        motion = profile["motion"]
        if motion["pops"]:
            badges.append(f"pops×{motion['pops']}")
        if motion["component_jumps"]:
            badges.append(f"topo×{motion['component_jumps']}")
        badge_html = " ".join(
            f"<span>{badge}</span>" for badge in badges
        ) or "<span class='calm'>no advisory flags</span>"
        cards.append(
            f"<article><h2>{profile['index']:02d} {profile['name']}"
            f"</h2><code>{profile['legacy_enum']} · {slug}</code>"
            f"<div class='badges'>{badge_html}</div>"
            f"<img src='sheets-png/{slug}.png' alt='{slug}'></article>"
        )
    clone_rows = "".join(
        f"<tr><td>{pair['a']}</td><td>{pair['b']}</td>"
        f"<td>{pair['distance_permille']}</td></tr>"
        for pair in report["clone_pairs"]
    )
    page = f"""<!doctype html>
<meta charset="utf-8">
<title>fvqa acceptance sheets</title>
<style>
 body {{ background:#0a0c10; color:#e8edf2; font:14px ui-monospace,monospace;
        margin:0; padding:24px; }}
 article {{ background:#10151b; border:1px solid #2a323c; border-radius:10px;
        padding:12px; margin:14px 0; }}
 img {{ width:100%; image-rendering:pixelated; background:#000; }}
 code {{ color:#71889c; display:block; margin:4px 0; }}
 .badges span {{ background:#3b2e13; color:#ffd07a; border-radius:99px;
        padding:2px 8px; margin-right:6px; font-size:12px; }}
 .badges .calm {{ background:#123323; color:#68e3a2; }}
 table {{ border-collapse:collapse; }}
 td, th {{ border:1px solid #2a323c; padding:4px 10px; }}
</style>
<h1>fvqa acceptance sheets</h1>
<p>Per profile: emotions 00–10 at half res with red border-contact
overlay, the same emotions at contact scale (4× box downscale), an
11-frame storyboard of the 6 s motion sweep, and the transition
sparkline (red = pop above threshold). All numeric flags are advisory;
promotion requires human review.</p>
<h2>Clone suspects (structural distance &lt; 260‰)</h2>
<table><tr><th>a</th><th>b</th><th>distance ‰</th></tr>{clone_rows}</table>
{''.join(cards)}
"""
    (output_dir / "index.html").write_text(page, encoding="utf-8")
    print(f"wrote {png_dir} and {output_dir / 'index.html'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
