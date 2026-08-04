#!/usr/bin/env python3
"""Build a portable FSPR avatar from one or more regular AI sprite sheets.

Image models are good at making attractive contact sheets but unreliable at
obeying one monolithic atlas layout.  This front-end lets expressions,
mouths, eyes, and future layers come from separate regular grids.  It keys
each source cell independently, registers every retained cell in a
deterministic boxes layout, then hands the result to FSPP for quantisation,
target derivation, validation, C emission, and browser manifests.

The implementation deliberately uses only Python's standard library and the
project-local FSPP modules.  Generated artifacts are reproducible from the
avatar JSON and source PNG bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from fspp.pipeline import build as build_fspp  # noqa: E402
from fspp.png_io import Image, decode, encode_rgba  # noqa: E402
from fspp.raster import apply_mask, key_background  # noqa: E402
from fspp.util import parse_hex_color  # noqa: E402


def _read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _write_json(path: Path, value: dict) -> None:
    path.write_text(
        json.dumps(value, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )


def _slice_bounds(length: int, count: int, index: int) -> tuple[int, int]:
    """Stable proportional grid bounds, including non-divisible sheets."""
    return length * index // count, length * (index + 1) // count


def _paste_opaque(destination: Image, source: Image, x0: int, y0: int) -> None:
    for y in range(source.height):
        for x in range(source.width):
            color = source.get(x, y)
            if color[3] >= 128:
                destination.put(x0 + x, y0 + y, color)


def _clean_cell(
    image: Image,
    columns: int,
    rows: int,
    index: int,
    inset: int,
    tolerance: int,
) -> Image:
    column = index % columns
    row = index // columns
    x0, x1 = _slice_bounds(image.width, columns, column)
    y0, y1 = _slice_bounds(image.height, rows, row)
    x0 += inset
    x1 -= inset
    y0 += inset
    y1 -= inset
    if x1 <= x0 or y1 <= y0:
        raise ValueError(
            f"source inset {inset} removes grid cell {column},{row}"
        )
    crop = image.crop(x0, y0, x1 - x0, y1 - y0)
    mask, _ = key_background(crop, "auto", tolerance)
    return apply_mask(crop, mask)


def assemble(
    config_path: Path,
    config: dict,
    generated: Path,
) -> tuple[Path, Path, dict]:
    cell_w, cell_h = config.get("assembly_cell", [400, 400])
    gap = int(config.get("assembly_gap", 24))
    sources = config.get("sources") or []
    if not sources:
        raise ValueError("avatar config needs at least one source grid")

    columns = max(int(source["grid"][0]) for source in sources)
    total_rows = sum(int(source["grid"][1]) for source in sources)
    width = gap + columns * (cell_w + gap)
    height = gap + total_rows * (cell_h + gap)
    background = parse_hex_color(config["background"])
    sheet = Image.blank(width, height, (*background, 255))

    boxes: dict[str, list[int]] = {}
    layout_rows: list[dict] = []
    source_records: list[dict] = []
    destination_row = 0

    for source in sources:
        source_path = Path(source["path"])
        if not source_path.is_absolute():
            source_path = config_path.parent / source_path
        source_bytes = source_path.read_bytes()
        image = decode(source_bytes)
        source_columns, source_rows = map(int, source["grid"])
        names = source["names"]
        expected = source_columns * source_rows
        if len(names) != expected:
            raise ValueError(
                f"{source_path}: grid has {expected} cells but names has "
                f"{len(names)} entries"
            )
        inset = int(source.get("inset", 0))
        tolerance = int(source.get("background_tolerance", 64))

        for row in range(source_rows):
            retained: list[str] = []
            for column in range(source_columns):
                index = row * source_columns + column
                name = names[index]
                if name is None:
                    continue
                cleaned = _clean_cell(
                    image,
                    source_columns,
                    source_rows,
                    index,
                    inset,
                    tolerance,
                )
                if cleaned.width > cell_w or cleaned.height > cell_h:
                    raise ValueError(
                        f"{source_path}: cleaned cell {name!r} is "
                        f"{cleaned.width}x{cleaned.height}, larger than "
                        f"assembly_cell {cell_w}x{cell_h}"
                    )
                x = gap + column * (cell_w + gap)
                y = gap + (destination_row + row) * (cell_h + gap)
                _paste_opaque(
                    sheet,
                    cleaned,
                    x + (cell_w - cleaned.width) // 2,
                    y + (cell_h - cleaned.height) // 2,
                )
                boxes[name] = [x, y, cell_w, cell_h]
                retained.append(name)
            if retained:
                layout_rows.append(
                    {"role": source["role"], "names": retained}
                )

        source_records.append(
            {
                "path": str(source_path),
                "sha256": hashlib.sha256(source_bytes).hexdigest(),
                "pixels": [image.width, image.height],
                "grid": [source_columns, source_rows],
                "role": source["role"],
                "inset": inset,
                "background_tolerance": tolerance,
            }
        )
        destination_row += source_rows

    generated.mkdir(parents=True, exist_ok=True)
    sheet_path = generated / "source-sheet.png"
    sheet_path.write_bytes(encode_rgba(sheet))

    spec = {
        "schema_version": 1,
        "id": config["id"],
        "display_name": config.get("display_name", config["id"]),
        "license": config.get("license", "project-generated"),
        "notes": config.get("notes", "")
        + " Sources assembled deterministically by avatar_pipeline.py.",
        "source": {
            "kind": "sheet",
            "sheet": sheet_path.name,
            "background": config["background"],
            "background_tolerance": 4,
            "pixel_grid": "none",
        },
        "layout": {
            "mode": "boxes",
            "rows": layout_rows,
            "boxes": boxes,
        },
        "rig": config["rig"],
        "palette": config["palette"],
        "expressions": config.get("expressions", "canonical11"),
        "flags": config.get("flags", ["breathe"]),
        "targets": config["targets"],
    }
    spec_path = generated / "fspp-spec.json"
    _write_json(spec_path, spec)

    provenance = {
        "assembler": "stackchan-avatar-pipeline-v1",
        "avatar_config": str(config_path),
        "avatar_config_sha256": hashlib.sha256(
            json.dumps(config, sort_keys=True, separators=(",", ":")).encode(
                "utf-8"
            )
        ).hexdigest(),
        "sources": source_records,
        "assembled_sheet": str(sheet_path),
        "assembled_pixels": [sheet.width, sheet.height],
    }
    _write_json(generated / "assembly-report.json", provenance)
    return sheet_path, spec_path, provenance


def command_build(args: argparse.Namespace) -> int:
    config_path = Path(args.character).resolve()
    config = _read_json(config_path)
    generated = (
        Path(args.out).resolve()
        if args.out
        else config_path.parent / "generated"
    )
    sheet_path, spec_path, _ = assemble(config_path, config, generated)
    output = build_fspp(
        spec_path,
        generated / "build",
        debug_dir=generated / "debug",
        verify_determinism=not args.skip_determinism,
    )
    print(f"assembled: {sheet_path}")
    print(f"spec:      {spec_path}")
    print(f"build:     {generated / 'build'}")
    print(f"status:    {output.status}")
    for target_id, report in sorted(output.report["targets"].items()):
        print(
            f"  {target_id}: {report['status']}, "
            f"{report['flash_bytes']} flash bytes"
        )
    return 0 if output.status != "fail" else 1


def _identifier(value: str) -> str:
    return re.sub(r"[^a-zA-Z0-9_]", "_", value).strip("_").lower()


def command_publish(args: argparse.Namespace) -> int:
    """Build every character and install one generated C/WASM catalogue."""

    experiment_dir = HERE.parent.parent
    firmware_main = experiment_dir / "firmware-ws" / "main"
    configs = sorted((HERE / "characters").glob("*/avatar.json"))
    if not configs:
        raise ValueError("no characters/*/avatar.json files found")

    includes: set[str] = set()
    entries: list[str] = []
    installed: list[str] = []
    for config_path in configs:
        config = _read_json(config_path)
        generated = config_path.parent / "generated"
        sheet_path, spec_path, _ = assemble(config_path, config, generated)
        output = build_fspp(
            spec_path,
            generated / "build",
            debug_dir=generated / "debug",
            verify_determinism=not args.skip_determinism,
        )
        if output.status == "fail":
            raise ValueError(f"{config['id']}: validation failed")
        print(f"built:     {config['id']} ({output.status}) from {sheet_path.name}")

        variants = {
            variant["id"]: variant
            for variant in config.get("palette", {}).get("variants", [])
        }
        character_id = _identifier(config["id"])
        for target in config.get("targets", []):
            target_id = str(target["id"])
            if not target_id.startswith("cores3-"):
                continue
            build_dir = generated / "build" / target_id
            sources = sorted(build_dir.glob("*_atlas.c"))
            headers = sorted(build_dir.glob("*_atlas.h"))
            if len(sources) != 1 or len(headers) != 1:
                raise ValueError(
                    f"{config['id']}/{target_id}: expected one C atlas pair"
                )
            for artifact in (sources[0], headers[0]):
                destination = firmware_main / artifact.name
                shutil.copyfile(artifact, destination)
                installed.append(artifact.name)
            includes.add(headers[0].name)

            target_identifier = _identifier(target_id)
            themes = target.get("device_themes", list(variants))
            for theme_id in themes:
                if theme_id not in variants:
                    raise ValueError(
                        f"{config['id']}/{target_id}: unknown theme {theme_id}"
                    )
                theme = variants[theme_id]
                symbol = (
                    f"face_sprite_fspp_{character_id}_{target_identifier}"
                    f"_atlas_{_identifier(theme_id)}"
                )
                base_slug = target.get(
                    "catalog_slug", f"{config['id']}-{target_id}"
                )
                slug = (
                    base_slug
                    if len(themes) == 1 or theme_id == "dark"
                    else f"{base_slug}-{theme_id}"
                )
                base_name = target.get(
                    "catalog_name",
                    f"{config.get('display_name', config['id'])} · {target_id}",
                )
                name = (
                    base_name
                    if len(themes) == 1 or theme_id == "dark"
                    else f"{base_name} · {theme.get('display_name', theme_id)}"
                )
                canvas_w, canvas_h = map(int, target["canvas"])
                entries.append(
                    "    {\n"
                    f"        {json.dumps(slug)},\n"
                    f"        {json.dumps(name)},\n"
                    f"        &{symbol},\n"
                    f"        {canvas_w}U, {canvas_h}U,\n"
                    "        FACE_RENDER_FLAG_PIXELATED |\n"
                    "            FACE_RENDER_FLAG_SPRITE_MOUTH |\n"
                    "            FACE_RENDER_FLAG_IDLE_MOTION"
                    + (
                        " |\n            FACE_RENDER_FLAG_HALF_RES"
                        if canvas_w <= 40
                        else ""
                    )
                    + ",\n"
                    "    },"
                )

    expected_artifacts = set(installed)
    stale_artifacts = sorted(
        artifact
        for pattern in ("fspp_*_atlas.c", "fspp_*_atlas.h")
        for artifact in firmware_main.glob(pattern)
        if artifact.name not in expected_artifacts
    )
    for artifact in stale_artifacts:
        artifact.unlink()

    include_text = "\n".join(
        f'#include "{header}"' for header in sorted(includes)
    )
    catalogue = (
        "/* Generated by avatar_pipeline.py publish. Do not hand-edit. */\n"
        f"{include_text}\n\n"
        "static const face_avatar_entry_t GENERATED_AVATARS[] = {\n"
        + "\n".join(entries)
        + "\n};\n"
    )
    catalog_path = firmware_main / "face_avatar_catalog_generated.inc"
    catalog_path.write_text(catalogue, encoding="utf-8")
    print(f"installed: {len(set(installed))} atlas files")
    print(f"pruned:    {len(stale_artifacts)} stale atlas files")
    print(f"catalogue: {catalog_path}")
    print(f"avatars:   {len(entries)} CoreS3 selections")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Assemble regular AI grids and compile portable avatars"
    )
    commands = parser.add_subparsers(dest="command", required=True)
    build_command = commands.add_parser("build")
    build_command.add_argument("--character", required=True)
    build_command.add_argument("--out")
    build_command.add_argument("--skip-determinism", action="store_true")
    build_command.set_defaults(handler=command_build)
    publish_command = commands.add_parser(
        "publish",
        help="build every character and install the CoreS3/WASM catalogue",
    )
    publish_command.add_argument("--skip-determinism", action="store_true")
    publish_command.set_defaults(handler=command_publish)
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (OSError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
