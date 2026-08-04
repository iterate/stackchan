#!/usr/bin/env python3
"""Build and optionally review a deterministic face-motion storyboard.

The xAI chat API currently accepts images rather than arbitrary local video
files.  This tool turns the quality rig's twelve-frame motion strips into one
numbered atlas, then can send that temporal storyboard to Grok vision.  The
native frame-delta metrics remain the hard timing gate; the model is only an
art-direction second opinion.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from review_face_visuals import (
    DEFAULT_ENDPOINT,
    DEFAULT_SECRET,
    load_api_key,
    request_review,
)
from run_face_render_quality import parse_probe_ppm, ppm_to_png


TOOLS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TOOLS_DIR.parent
DEFAULT_QUALITY_DIR = EXPERIMENT_DIR / "local" / "face-render-quality"

HEADER_HEIGHT = 18
HEADER_BACKGROUND = (9, 14, 20)
HEADER_FOREGROUND = (238, 245, 251)
ATLAS_BACKGROUND = (3, 5, 8)

DIGITS = (
    (0b111, 0b101, 0b101, 0b101, 0b111),
    (0b010, 0b110, 0b010, 0b010, 0b111),
    (0b111, 0b001, 0b111, 0b100, 0b111),
    (0b111, 0b001, 0b111, 0b001, 0b111),
    (0b101, 0b101, 0b111, 0b001, 0b001),
    (0b111, 0b100, 0b111, 0b001, 0b111),
    (0b111, 0b100, 0b111, 0b101, 0b111),
    (0b111, 0b001, 0b001, 0b001, 0b001),
    (0b111, 0b101, 0b111, 0b101, 0b111),
    (0b111, 0b101, 0b111, 0b001, 0b111),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build a temporal storyboard from quality-rig motion strips and "
            "optionally ask Grok vision for an animation critique."
        )
    )
    parser.add_argument(
        "quality_dir",
        nargs="?",
        type=Path,
        default=DEFAULT_QUALITY_DIR,
    )
    parser.add_argument(
        "--profiles",
        help=(
            "comma-separated profile indices/slugs; default selects every "
            "expression or motion WARN/FAIL profile"
        ),
    )
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--max-profiles", type=int, default=24)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--review", action="store_true")
    parser.add_argument("--model", default="grok-4.5")
    parser.add_argument("--detail", choices=("low", "high"), default="high")
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument("--secret", type=Path, default=DEFAULT_SECRET)
    parser.add_argument("--review-output", type=Path)
    return parser.parse_args()


def select_profiles(
    report: dict[str, Any],
    selectors: str | None,
    maximum: int,
) -> list[dict[str, Any]]:
    profiles = report["profiles"]
    if selectors:
        lookup: dict[str, dict[str, Any]] = {}
        for profile in profiles:
            lookup[str(profile["index"])] = profile
            lookup[profile["slug"]] = profile
        selected: list[dict[str, Any]] = []
        for selector in selectors.split(","):
            key = selector.strip()
            if not key:
                continue
            if key not in lookup:
                raise ValueError(f"unknown profile selector: {key}")
            profile = lookup[key]
            if profile not in selected:
                selected.append(profile)
    else:
        selected = [
            profile
            for profile in profiles
            if profile["expression"]["status"] != "pass"
            or profile["motion"]["status"] != "pass"
        ]
        if not selected:
            selected = sorted(
                profiles,
                key=lambda item: min(
                    item["expression"]["score"],
                    item["motion"]["score"],
                ),
            )
    if maximum < 1:
        raise ValueError("--max-profiles must be positive")
    return selected[:maximum]


def fill(
    pixels: bytearray,
    width: int,
    x: int,
    y: int,
    draw_width: int,
    draw_height: int,
    color: tuple[int, int, int],
) -> None:
    row = bytes(color) * draw_width
    for draw_y in range(y, y + draw_height):
        offset = (draw_y * width + x) * 3
        pixels[offset : offset + len(row)] = row


def draw_digit(
    pixels: bytearray,
    width: int,
    x: int,
    y: int,
    digit: int,
    scale: int = 2,
) -> None:
    for row, bits in enumerate(DIGITS[digit]):
        for column in range(3):
            if bits & (1 << (2 - column)):
                fill(
                    pixels,
                    width,
                    x + column * scale,
                    y + row * scale,
                    scale,
                    scale,
                    HEADER_FOREGROUND,
                )


def draw_number(
    pixels: bytearray,
    width: int,
    x: int,
    y: int,
    value: int,
) -> None:
    text = f"{value:02d}"
    for offset, character in enumerate(text):
        draw_digit(pixels, width, x + offset * 9, y, int(character))


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + pixels)


def build_atlas(
    quality_dir: Path,
    selected: list[dict[str, Any]],
    columns: int,
    output: Path,
) -> dict[str, Any]:
    if columns < 1:
        raise ValueError("--columns must be positive")
    sheets: list[tuple[int, int, bytes]] = []
    for profile in selected:
        path = (
            quality_dir
            / "motion-peaks-ppm"
            / f"{profile['slug']}.ppm"
        )
        sheets.append(parse_probe_ppm(path))
    if not sheets:
        raise ValueError("no profiles selected")
    sheet_width, sheet_height, _ = sheets[0]
    if any(
        width != sheet_width or height != sheet_height
        for width, height, _ in sheets
    ):
        raise ValueError("motion sheets do not share one geometry")

    rows = math.ceil(len(sheets) / columns)
    card_height = HEADER_HEIGHT + sheet_height
    atlas_width = columns * sheet_width
    atlas_height = rows * card_height
    pixels = bytearray(bytes(ATLAS_BACKGROUND) * atlas_width * atlas_height)

    layout: list[dict[str, Any]] = []
    for slot, (profile, (_, _, source)) in enumerate(
        zip(selected, sheets, strict=True)
    ):
        column = slot % columns
        row = slot // columns
        x = column * sheet_width
        y = row * card_height
        fill(
            pixels,
            atlas_width,
            x,
            y,
            sheet_width,
            HEADER_HEIGHT,
            HEADER_BACKGROUND,
        )
        draw_number(pixels, atlas_width, x + 5, y + 4, slot)
        draw_number(
            pixels,
            atlas_width,
            x + sheet_width - 24,
            y + 4,
            profile["index"],
        )
        source_stride = sheet_width * 3
        for source_y in range(sheet_height):
            source_offset = source_y * source_stride
            destination_offset = (
                ((y + HEADER_HEIGHT + source_y) * atlas_width + x) * 3
            )
            pixels[
                destination_offset : destination_offset + source_stride
            ] = source[source_offset : source_offset + source_stride]
        layout.append(
            {
                "slot": slot,
                "profile_index": profile["index"],
                "slug": profile["slug"],
                "name": profile["name"],
                "expression_status": profile["expression"]["status"],
                "motion_status": profile["motion"]["status"],
                "abrupt_jumps": profile["motion"]["abrupt_jumps"],
            }
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    ppm_path = output.with_suffix(".ppm")
    write_ppm(ppm_path, atlas_width, atlas_height, bytes(pixels))
    ppm_to_png(ppm_path, output)
    return {
        "schema_version": 1,
        "atlas": str(output),
        "columns": columns,
        "rows": rows,
        "card_frames": 12,
        "layout": layout,
    }


def review_prompt(manifest: dict[str, Any]) -> str:
    mapping = "\n".join(
        (
            f"slot {item['slot']:02d} = profile "
            f"{item['profile_index']:02d} {item['slug']}"
        )
        for item in manifest["layout"]
    )
    return f"""\
Act as a demanding character-animation director. This atlas is a deterministic
motion QA storyboard. Each numbered card contains twelve chronological,
left-to-right then top-to-bottom 160x120 renderer frames surrounding that
profile's largest measured frame-to-frame transition. The small number at the
left of each card header is its slot; the number at right is the renderer
profile index.

{mapping}

Judge temporal behavior, not breadth. Look for abrupt pops, chatter, quantized
or jagged deformation, clipped mouths/eyes, pupils escaping sockets, whole-face
translation masquerading as acting, dead eyes during speech, mechanical
jaw-flap, and lack of anticipation/settle. Prefer stable Cozmo/Vector-like eye
construction and attached mouth corners. A discrete sprite hold is acceptable
when its pose changes are intentional and do not chatter.

Authentic Anki Cozmo and Vector faces are eye-only. Do not penalize an
eye-only renderer for lacking a mouth; instead judge whether aperture, lids,
corner bends, scale/spacing, gaze, anticipation, and settle carry its speech
and emotion. Apply mouth-corner criticism only where a mouth is intentionally
present.

Return JSON only:
{{
  "summary": "short harsh verdict",
  "promote": [{{"profile_index": 0, "reason": "..."}}],
  "fix": [
    {{
      "profile_index": 0,
      "severity": "high|medium|low",
      "observed_motion_problem": "...",
      "specific_change": "..."
    }}
  ],
  "drop_or_redesign": [{{"profile_index": 0, "reason": "..."}}],
  "cross_cutting_changes": [
    {{"rank": 1, "change": "...", "why": "..."}}
  ]
}}
"""


def main() -> int:
    args = parse_args()
    quality_dir = args.quality_dir.resolve()
    report = json.loads(
        (quality_dir / "metrics.json").read_text(encoding="utf-8")
    )
    selected = select_profiles(
        report,
        args.profiles,
        args.max_profiles,
    )
    output = (
        args.output.resolve()
        if args.output
        else quality_dir / "motion-review-atlas.png"
    )
    manifest = build_atlas(
        quality_dir,
        selected,
        args.columns,
        output,
    )
    manifest_path = output.with_suffix(".json")
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )

    result: dict[str, Any] = {"manifest": manifest}
    if args.review:
        review = request_review(
            [output],
            review_prompt(manifest),
            args.model,
            args.detail,
            args.endpoint,
            load_api_key(args.secret),
        )
        review_output = (
            args.review_output.resolve()
            if args.review_output
            else quality_dir / f"motion-review-{args.model}.json"
        )
        review_output.write_text(
            json.dumps(review, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        result["review"] = str(review_output)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
