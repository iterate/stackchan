#!/usr/bin/env python3
"""Build and run the deterministic production-renderer visual QA probe.

No device, browser, image package, or network is required.  The C probe calls
the same face_render_frame() and face_stage_cue_apply() functions used by
firmware/WASM, emits PPM contact sheets and JSON metrics, then this script
creates deterministic PNG mirrors and a navigable HTML report.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TOOLS_DIR.parent
FIRMWARE_DIR = EXPERIMENT_DIR / "firmware-ws"
MAIN_DIR = FIRMWARE_DIR / "main"
TESTS_DIR = FIRMWARE_DIR / "tests"
FEA_DIR = (
    TOOLS_DIR
    / "face-grid"
    / "contrib"
    / "fable_expression_actors_v3"
    / "src"
)

FRAME_WIDTH = 160
FRAME_HEIGHT = 120
EXPRESSION_COLUMNS = 4
EXPRESSION_LABEL_HEIGHT = 10
EXPRESSION_CARD_HEIGHT = FRAME_HEIGHT + EXPRESSION_LABEL_HEIGHT
GEOMETRY_EDGE_FRACTION = 0.08
GEOMETRY_CLEAR_DISTANCE = 0.18


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=EXPERIMENT_DIR,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}"
        )
    return result


def compile_probe(executable: Path) -> None:
    run(
        [
            os.environ.get("CC", "clang"),
            "-std=c11",
            "-O2",
            "-DSTACKCHAN_HOST_TEST",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(MAIN_DIR),
            "-I",
            str(FEA_DIR),
            str(TESTS_DIR / "face_render_quality.c"),
            str(MAIN_DIR / "face_abstract_redux.c"),
            str(MAIN_DIR / "face_closeup_toon_actors.c"),
            str(MAIN_DIR / "face_cyber_wildcards.c"),
            str(MAIN_DIR / "face_eye_actors.c"),
            str(MAIN_DIR / "face_eye_study_redux.c"),
            str(MAIN_DIR / "face_mouth_actors.c"),
            str(MAIN_DIR / "face_mouth_study_redux.c"),
            str(MAIN_DIR / "face_pixel_pack.c"),
            str(MAIN_DIR / "face_pixel_pack_core.c"),
            str(MAIN_DIR / "face_pixel_pack_ega.c"),
            str(MAIN_DIR / "face_pixel_pack_rogue.c"),
            str(MAIN_DIR / "face_pixel_pack_talkie.c"),
            str(MAIN_DIR / "face_pixel_pack_vga.c"),
            str(MAIN_DIR / "face_pixel_redux_actors.c"),
            str(MAIN_DIR / "face_performance.c"),
            str(MAIN_DIR / "face_render.c"),
            str(MAIN_DIR / "face_robot_eyes.c"),
            str(MAIN_DIR / "face_robot_eyes_behavior.c"),
            str(MAIN_DIR / "face_robot_eyes_draw.c"),
            str(MAIN_DIR / "face_robot_eyes_profiles.c"),
            str(MAIN_DIR / "face_robot_redux_actors.c"),
            str(MAIN_DIR / "face_salvage_redux_actors.c"),
            str(MAIN_DIR / "face_sprite_sheet.c"),
            str(MAIN_DIR / "face_sprite_actors.c"),
            str(MAIN_DIR / "face_sprite_redux_actors.c"),
            str(MAIN_DIR / "face_sprite_showcase.c"),
            str(MAIN_DIR / "face_sprite_mossling.c"),
            str(MAIN_DIR / "face_sprite_mossling_generated.c"),
            str(MAIN_DIR / "face_stage.c"),
            str(MAIN_DIR / "fta_act.c"),
            str(MAIN_DIR / "fta_draw.c"),
            str(MAIN_DIR / "fta_math.c"),
            str(MAIN_DIR / "fta_styles.c"),
            str(FEA_DIR / "fea_math.c"),
            str(FEA_DIR / "fea_solve.c"),
            str(FEA_DIR / "fea_draw.c"),
            str(FEA_DIR / "fea_actor_mochi.c"),
            str(FEA_DIR / "fea_actor_karakuri.c"),
            str(FEA_DIR / "fea_actor_sticker.c"),
            str(FEA_DIR / "fea_actor_wisp.c"),
            str(FEA_DIR / "fea_actor_scope.c"),
            str(FEA_DIR / "fea_registry.c"),
            "-o",
            str(executable),
        ]
    )


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def parse_probe_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    try:
        magic, dimensions, maximum, pixels = data.split(b"\n", 3)
        width_text, height_text = dimensions.split()
        width = int(width_text)
        height = int(height_text)
    except (ValueError, TypeError) as error:
        raise ValueError(f"{path} is not a probe PPM") from error
    expected = width * height * 3
    if magic != b"P6" or maximum != b"255" or len(pixels) != expected:
        raise ValueError(
            f"invalid PPM {path}: {magic=}, {maximum=}, "
            f"{len(pixels)=}, {expected=}"
        )
    return width, height, pixels


def ppm_to_png(source: Path, destination: Path) -> None:
    width, height, pixels = parse_probe_ppm(source)
    scanlines = bytearray()
    stride = width * 3
    for y in range(height):
        scanlines.append(0)
        scanlines.extend(pixels[y * stride : (y + 1) * stride])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9))
        + png_chunk(b"IEND", b"")
    )
    destination.write_bytes(png)


def expression_frames_from_sheet(path: Path, count: int) -> list[bytes]:
    """Extract the native RGB frames from the labelled expression sheet."""
    width, height, pixels = parse_probe_ppm(path)
    expected_width = FRAME_WIDTH * EXPRESSION_COLUMNS
    expected_rows = (count + EXPRESSION_COLUMNS - 1) // EXPRESSION_COLUMNS
    expected_height = EXPRESSION_CARD_HEIGHT * expected_rows
    if width != expected_width or height != expected_height:
        raise ValueError(
            f"unexpected expression sheet geometry for {path}: "
            f"{width}x{height}, expected {expected_width}x{expected_height}"
        )
    frames: list[bytes] = []
    sheet_stride = width * 3
    frame_stride = FRAME_WIDTH * 3
    for index in range(count):
        source_x = (index % EXPRESSION_COLUMNS) * FRAME_WIDTH
        source_y = (
            (index // EXPRESSION_COLUMNS) * EXPRESSION_CARD_HEIGHT
            + EXPRESSION_LABEL_HEIGHT
        )
        frame = bytearray(FRAME_WIDTH * FRAME_HEIGHT * 3)
        for y in range(FRAME_HEIGHT):
            source = (source_y + y) * sheet_stride + source_x * 3
            destination = y * frame_stride
            frame[destination : destination + frame_stride] = pixels[
                source : source + frame_stride
            ]
        frames.append(bytes(frame))
    return frames


def ranked_edge_mask(frame: bytes) -> set[int]:
    """Return a palette-resistant mask of the strongest facial edges.

    Selecting a fixed fraction by local gradient rank makes this much less
    sensitive to an emotion merely changing palette or global brightness than
    raw RGB deltas.  It remains advisory: animated noise can still move edges,
    so a passing score can never promote a renderer without visual review.
    """
    luminance = bytearray(FRAME_WIDTH * FRAME_HEIGHT)
    for index in range(FRAME_WIDTH * FRAME_HEIGHT):
        offset = index * 3
        luminance[index] = (
            frame[offset] * 77
            + frame[offset + 1] * 150
            + frame[offset + 2] * 29
        ) >> 8

    gradients: list[tuple[int, int]] = []
    for y in range(11, FRAME_HEIGHT - 11):
        row = y * FRAME_WIDTH
        for x in range(17, FRAME_WIDTH - 17):
            index = row + x
            horizontal = abs(
                luminance[index + 1] - luminance[index - 1]
            )
            vertical = abs(
                luminance[index + FRAME_WIDTH]
                - luminance[index - FRAME_WIDTH]
            )
            gradients.append((horizontal + vertical, index))
    gradients.sort(reverse=True)
    retained = int(len(gradients) * GEOMETRY_EDGE_FRACTION)
    return {
        index
        for strength, index in gradients[:retained]
        if strength > 3
    }


def edge_jaccard_distance(first: set[int], second: set[int]) -> float:
    union = first | second
    if not union:
        return 0.0
    return len(first ^ second) / len(union)


def measure_expression_geometry(
    path: Path, expression_names: list[str]
) -> dict[str, Any]:
    frames = expression_frames_from_sheet(path, len(expression_names))
    masks = [ranked_edge_mask(frame) for frame in frames]
    neutral = masks[0]
    distances = [
        edge_jaccard_distance(neutral, mask)
        for mask in masks
    ]
    nonneutral = distances[1:]
    clear = sum(
        distance >= GEOMETRY_CLEAR_DISTANCE for distance in nonneutral
    )
    mean = sum(nonneutral) / len(nonneutral)
    minimum = min(nonneutral)
    if clear < 3 or mean < 0.12:
        status = "fail"
    elif clear < 8 or mean < 0.22:
        status = "warn"
    else:
        status = "pass"
    return {
        "status": status,
        "clear_nonneutral": clear,
        "minimum_neutral_edge_distance": round(minimum, 6),
        "mean_neutral_edge_distance": round(mean, 6),
        "edge_fraction": GEOMETRY_EDGE_FRACTION,
        "clear_distance": GEOMETRY_CLEAR_DISTANCE,
        "neutral_to_expression": [
            {
                "expression": name,
                "edge_jaccard_distance": round(distance, 6),
            }
            for name, distance in zip(
                expression_names, distances, strict=True
            )
        ],
        "advisory": (
            "Palette-resistant structural diagnostic only; noise can pass it "
            "and visual review is still required."
        ),
    }


def add_geometry_metrics(
    report: dict[str, Any], expression_ppm_dir: Path
) -> None:
    expression_names = report["expression_fixture"]["expressions"]
    counts = {"pass": 0, "warn": 0, "fail": 0}
    for profile in report["profiles"]:
        geometry = measure_expression_geometry(
            expression_ppm_dir / f"{profile['slug']}.ppm",
            expression_names,
        )
        profile["geometry"] = geometry
        counts[geometry["status"]] += 1
    report["summary"]["geometry"] = counts
    report["summary"]["quality_pass"] = bool(
        report["summary"]["quality_pass"] and counts["fail"] == 0
    )


def status_class(status: str) -> str:
    return status if status in {"pass", "warn", "fail"} else "unknown"


def format_percent(value: float) -> str:
    return f"{value * 100:.2f}%"


def write_html_report(output_dir: Path, report: dict[str, Any]) -> None:
    expressions = report["expression_fixture"]["expressions"]
    profiles = report["profiles"]
    summary = report["summary"]
    legend = " · ".join(
        f"<b>{index:02d}</b> {html.escape(name)}"
        for index, name in enumerate(expressions)
    )
    profile_cards: list[str] = []
    for profile in profiles:
        expression = profile["expression"]
        geometry = profile["geometry"]
        motion = profile["motion"]
        slug = html.escape(profile["slug"])
        name = html.escape(profile["name"])
        expression_status = status_class(expression["status"])
        geometry_status = status_class(geometry["status"])
        motion_status = status_class(motion["status"])
        neutral_values = " ".join(
            f"{item['expression'][:3]}={item['roi_delta']:.3f}"
            for item in expression["neutral_to_expression"][1:]
        )
        profile_cards.append(
            f"""
            <article class="profile" id="{slug}">
              <header>
                <div>
                  <span class="index">{profile['index']:02d}</span>
                  <h2>{name}</h2>
                  <code>{slug}</code>
                </div>
                <div class="badges">
                  <span class="{expression_status}">
                    expression {expression['status']}
                    {expression['score']:.0f}
                  </span>
                  <span class="{geometry_status}">
                    geometry {geometry['status']}
                    {geometry['clear_nonneutral']}/10
                  </span>
                  <span class="{motion_status}">
                    motion {motion['status']} {motion['score']:.0f}
                  </span>
                </div>
              </header>
              <a href="expressions-png/{slug}.png">
                <img class="expression-sheet"
                     src="expressions-png/{slug}.png"
                     alt="{name} in all eleven stage expressions">
              </a>
              <div class="metrics">
                <span>{expression['clear_nonneutral']}/10 clear emotions</span>
                <span>{geometry['clear_nonneutral']}/10 structural emotions</span>
                <span>{expression['distinct_frame_hashes']}/11 exact-distinct</span>
                <span>{expression['weak_pairs']}/55 weak pairs</span>
                <span>mean Δ {format_percent(expression['mean_pair_roi_delta'])}</span>
                <span>{motion['abrupt_jumps']} abrupt jumps</span>
                <span>peak frame {motion['peak_transition_frame']}</span>
                <span>peak Δ {format_percent(motion['maximum_roi_delta'])}</span>
              </div>
              <details>
                <summary>Largest motion transition</summary>
                <a href="motion-peaks-png/{slug}.png">
                  <img class="motion-sheet"
                       src="motion-peaks-png/{slug}.png"
                       alt="Frames around the largest motion jump for {name}">
                </a>
                <p>{html.escape(neutral_values)}</p>
              </details>
            </article>
            """
        )

    expression_summary = summary["expression"]
    geometry_summary = summary["geometry"]
    motion_summary = summary["motion"]
    document = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Stack-chan face renderer quality report</title>
  <style>
    :root {{
      color-scheme: dark;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
      background: #080a0d;
      color: #e8edf2;
    }}
    * {{ box-sizing: border-box; }}
    body {{ margin: 0; padding: 24px; }}
    main {{ max-width: 1500px; margin: auto; }}
    h1 {{ margin: 0 0 8px; font-size: clamp(24px, 4vw, 48px); }}
    h2 {{ display: inline; margin: 0; font-size: 18px; }}
    p {{ color: #9da9b6; line-height: 1.5; }}
    .summary, .profile {{
      background: #10151b;
      border: 1px solid #29323b;
      border-radius: 12px;
      padding: 16px;
      margin: 16px 0;
    }}
    .summary-grid, .metrics {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }}
    .summary-grid span, .metrics span, .badges span {{
      background: #1a222b;
      border-radius: 999px;
      padding: 6px 9px;
      white-space: nowrap;
    }}
    .legend {{ line-height: 2; color: #b5c0ca; }}
    .atlas {{
      width: 100%;
      image-rendering: pixelated;
      border: 1px solid #29323b;
      background: black;
    }}
    .profile header {{
      display: flex;
      gap: 16px;
      align-items: start;
      justify-content: space-between;
      margin-bottom: 12px;
    }}
    .index {{ color: #6d7e8e; margin-right: 8px; }}
    code {{ display: block; color: #71889c; margin-top: 4px; }}
    .badges {{ display: flex; gap: 6px; flex-wrap: wrap; justify-content: end; }}
    .badges .pass {{ color: #68e3a2; background: #123323; }}
    .badges .warn {{ color: #ffd07a; background: #3b2e13; }}
    .badges .fail {{ color: #ff8787; background: #421b1f; }}
    .expression-sheet {{
      width: min(100%, 960px);
      image-rendering: pixelated;
      background: black;
      border: 1px solid #29323b;
    }}
    .motion-sheet {{
      width: min(100%, 720px);
      image-rendering: pixelated;
      background: black;
      border: 1px solid #29323b;
      margin-top: 12px;
    }}
    .metrics {{ margin: 12px 0; font-size: 12px; color: #b5c0ca; }}
    details p {{ font-size: 11px; overflow-wrap: anywhere; }}
    a {{ color: inherit; }}
    @media (max-width: 700px) {{
      body {{ padding: 10px; }}
      .profile header {{ display: block; }}
      .badges {{ justify-content: start; margin-top: 10px; }}
    }}
  </style>
</head>
<body>
<main>
  <h1>Face renderer quality report</h1>
  <p>
    The production C renderer, driven without a device at a fixed sample
    clock. Scores are diagnostic, not subjective proof of appeal. Exact
    duplicate and weakly separated expressions are failures worth inspecting.
  </p>
  <section class="summary">
    <div class="summary-grid">
      <span>{report['renderer']['profile_count']} profiles</span>
      <span>expression: {expression_summary['pass']} pass /
        {expression_summary['warn']} warn /
        {expression_summary['fail']} fail</span>
      <span>geometry: {geometry_summary['pass']} pass /
        {geometry_summary['warn']} warn /
        {geometry_summary['fail']} fail</span>
      <span>motion: {motion_summary['pass']} pass /
        {motion_summary['warn']} warn /
        {motion_summary['fail']} fail</span>
      <span>{summary['profiles_with_exact_duplicate_expressions']}
        profiles with exact expression duplicates</span>
      <span>{summary['total_abrupt_jumps']} abrupt transitions</span>
    </div>
    <p class="legend">{legend}</p>
    <a href="expression-atlas.png">
      <img class="atlas" src="expression-atlas.png"
           alt="Every renderer row across all stage expressions">
    </a>
  </section>
  {''.join(profile_cards)}
</main>
</body>
</html>
"""
    (output_dir / "index.html").write_text(document, encoding="utf-8")


def write_manifest(output_dir: Path) -> None:
    entries: list[dict[str, Any]] = []
    artifact_paths = sorted(
        path
        for path in output_dir.rglob("*")
        if path.is_file() and path.name != "artifact-manifest.json"
    )
    for path in artifact_paths:
        relative = path.relative_to(output_dir).as_posix()
        data = path.read_bytes()
        entries.append(
            {
                "path": relative,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    manifest = {"schema_version": 1, "artifacts": entries}
    (output_dir / "artifact-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Render all stage expressions and measure expression "
            "separability/motion discontinuity without a device."
        )
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=EXPERIMENT_DIR / "local" / "face-render-quality",
        help="artifact directory (default: %(default)s)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit 2 when the report's current quality thresholds do not pass",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        dest="open_report",
        help="open the generated HTML report with the macOS `open` command",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = args.output.resolve()
    expression_ppm_dir = output_dir / "expressions-ppm"
    expression_png_dir = output_dir / "expressions-png"
    motion_ppm_dir = output_dir / "motion-peaks-ppm"
    motion_png_dir = output_dir / "motion-peaks-png"
    for directory in (
        output_dir,
        expression_ppm_dir,
        expression_png_dir,
        motion_ppm_dir,
        motion_png_dir,
    ):
        directory.mkdir(parents=True, exist_ok=True)

    metrics_path = output_dir / "metrics.json"
    atlas_ppm_path = output_dir / "expression-atlas.ppm"
    status_path = output_dir / "probe-status.txt"
    with tempfile.TemporaryDirectory(prefix="stackchan-face-quality-") as temp:
        executable = Path(temp) / "face_render_quality"
        compile_probe(executable)
        run(
            [
                str(executable),
                str(metrics_path),
                str(expression_ppm_dir),
                str(motion_ppm_dir),
                str(atlas_ppm_path),
                str(status_path),
            ]
        )

    report = json.loads(metrics_path.read_text(encoding="utf-8"))
    add_geometry_metrics(report, expression_ppm_dir)
    metrics_path.write_text(
        json.dumps(report, indent=2) + "\n",
        encoding="utf-8",
    )
    ppm_to_png(atlas_ppm_path, output_dir / "expression-atlas.png")
    for profile in report["profiles"]:
        slug = profile["slug"]
        ppm_to_png(
            expression_ppm_dir / f"{slug}.ppm",
            expression_png_dir / f"{slug}.png",
        )
        ppm_to_png(
            motion_ppm_dir / f"{slug}.ppm",
            motion_png_dir / f"{slug}.png",
        )
    write_html_report(output_dir, report)
    write_manifest(output_dir)

    expression = report["summary"]["expression"]
    geometry = report["summary"]["geometry"]
    motion = report["summary"]["motion"]
    result = {
        "quality_pass": report["summary"]["quality_pass"],
        "profiles": report["renderer"]["profile_count"],
        "expression": expression,
        "geometry": geometry,
        "motion": motion,
        "exact_duplicate_profiles": report["summary"][
            "profiles_with_exact_duplicate_expressions"
        ],
        "abrupt_jumps": report["summary"]["total_abrupt_jumps"],
        "report": str(output_dir / "index.html"),
        "metrics": str(metrics_path),
    }
    print(json.dumps(result, indent=2))

    if args.open_report:
        run(["open", str(output_dir / "index.html")])
    if args.strict and not report["summary"]["quality_pass"]:
        return 2
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"face render quality probe failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
