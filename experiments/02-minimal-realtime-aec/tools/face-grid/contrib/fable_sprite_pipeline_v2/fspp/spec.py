"""Character spec loading and validation.

The spec is the target-neutral source of truth described in
docs/sprite-avatar-pipeline.md: where the pixels come from, how the sheet is
laid out, the rig geometry, the palette budget, and the device targets. The
JSON Schema in schema/character_spec.schema.json documents the same contract;
this module is the enforced validator so the pipeline has no dependencies.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .vocab import CANONICAL_BY_SLUG, MOUTH_SLOT_NAMES


class SpecError(ValueError):
    pass


@dataclass(frozen=True)
class SourceSpec:
    kind: str  # "sheet" | "prompt" | "reference"
    sheet: str | None
    prompt: str | None
    reference: str | None
    background: str  # "auto" | "#rrggbb"
    background_tolerance: int
    pixel_grid: str | int  # "auto" | "none" | pitch


@dataclass(frozen=True)
class LayoutRow:
    role: str  # "expression" | "mouth"
    names: tuple[str, ...]


@dataclass(frozen=True)
class LayoutSpec:
    mode: str  # "bands" | "boxes"
    rows: tuple[LayoutRow, ...]
    boxes: dict[str, tuple[int, int, int, int]]
    min_cell_span: int
    row_gap: int
    column_gap: int


@dataclass(frozen=True)
class RigSpec:
    canvas: tuple[int, int]
    baseline_y: int
    max_body: tuple[int, int]
    mouth_box: tuple[int, int, int, int]
    mouth_erase: bool
    mouth_source: str  # "auto" | "isolated" | "face_crop"
    eye_left: tuple[int, int, int, int] | None
    eye_right: tuple[int, int, int, int] | None
    blink: str  # "synth" | "none"


@dataclass(frozen=True)
class PaletteSpec:
    max_colors: int
    locked: tuple[tuple[int, int, int], ...] | None
    despeckle: bool


@dataclass(frozen=True)
class TargetSpec:
    id: str
    framing: str  # "face_fill" | "stage"
    canvas: tuple[int, int]
    scale: int
    margin: int
    max_flash_bytes: int
    emit_c: bool


@dataclass(frozen=True)
class CharacterSpec:
    schema_version: int
    id: str
    display_name: str
    license: str
    source: SourceSpec
    layout: LayoutSpec
    rig: RigSpec
    palette: PaletteSpec
    expressions: tuple[str, ...]
    targets: tuple[TargetSpec, ...]
    flags: tuple[str, ...]
    spec_path: Path
    notes: str = ""
    raw: dict = field(default_factory=dict, compare=False)

    def resolve_path(self, value: str) -> Path:
        path = Path(value)
        if not path.is_absolute():
            path = self.spec_path.parent / path
        return path


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SpecError(message)


def _box(value, name: str) -> tuple[int, int, int, int]:
    _require(
        isinstance(value, list)
        and len(value) == 4
        and all(isinstance(v, int) for v in value),
        f"{name} must be [x, y, w, h] integers",
    )
    _require(value[2] > 0 and value[3] > 0, f"{name} needs positive size")
    return tuple(value)


def _pair(value, name: str) -> tuple[int, int]:
    _require(
        isinstance(value, list)
        and len(value) == 2
        and all(isinstance(v, int) and v > 0 for v in value),
        f"{name} must be [width, height] positive integers",
    )
    return tuple(value)


VALID_FLAGS = ("auto_blink", "breathe", "idle_saccades")
IDENT_CHARS = set("abcdefghijklmnopqrstuvwxyz0123456789_-")


def _ident(value, name: str) -> str:
    _require(
        isinstance(value, str)
        and value != ""
        and set(value) <= IDENT_CHARS
        and value[0].isalpha(),
        f"{name} must be a lowercase [a-z0-9_-] identifier",
    )
    return value


def load_spec(path) -> CharacterSpec:
    spec_path = Path(path).resolve()
    with open(spec_path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)

    _require(raw.get("schema_version") == 1, "schema_version must be 1")
    spec_id = _ident(raw.get("id"), "id")
    display_name = raw.get("display_name") or spec_id
    license_id = raw.get("license", "CC0-1.0")

    source_raw = raw.get("source") or {}
    kind = source_raw.get("kind")
    _require(
        kind in ("sheet", "prompt", "reference"),
        "source.kind must be sheet, prompt, or reference",
    )
    if kind == "sheet":
        _require(
            isinstance(source_raw.get("sheet"), str),
            "source.sheet path is required for sheet sources",
        )
    if kind == "prompt":
        _require(
            isinstance(source_raw.get("prompt"), str)
            and source_raw["prompt"].strip() != "",
            "source.prompt text is required for prompt sources",
        )
    background = source_raw.get("background", "auto")
    _require(
        background == "auto"
        or (isinstance(background, str) and background.startswith("#")),
        "source.background must be 'auto' or '#rrggbb'",
    )
    pixel_grid = source_raw.get("pixel_grid", "auto")
    _require(
        pixel_grid in ("auto", "none")
        or (isinstance(pixel_grid, int) and 1 <= pixel_grid <= 64),
        "source.pixel_grid must be 'auto', 'none', or a pitch 1..64",
    )
    tolerance = source_raw.get("background_tolerance", 24)
    _require(
        isinstance(tolerance, int) and 1 <= tolerance <= 128,
        "source.background_tolerance must be 1..128",
    )
    source = SourceSpec(
        kind=kind,
        sheet=source_raw.get("sheet"),
        prompt=source_raw.get("prompt"),
        reference=source_raw.get("reference"),
        background=background,
        background_tolerance=tolerance,
        pixel_grid=pixel_grid,
    )

    layout_raw = raw.get("layout") or {}
    mode = layout_raw.get("mode", "bands")
    _require(mode in ("bands", "boxes"), "layout.mode must be bands or boxes")
    rows: list[LayoutRow] = []
    for index, row in enumerate(layout_raw.get("rows", [])):
        role = row.get("role")
        _require(
            role in ("expression", "mouth"),
            f"layout.rows[{index}].role must be expression or mouth",
        )
        names = row.get("names")
        _require(
            isinstance(names, list) and names,
            f"layout.rows[{index}].names must be a non-empty list",
        )
        for name in names:
            _ident(name, f"layout.rows[{index}] cell name")
            if role == "mouth":
                _require(
                    name in MOUTH_SLOT_NAMES,
                    f"unknown mouth slot name {name!r}; "
                    f"valid: {', '.join(MOUTH_SLOT_NAMES)}",
                )
            else:
                _require(
                    name in CANONICAL_BY_SLUG,
                    f"unknown expression slug {name!r}",
                )
        rows.append(LayoutRow(role=role, names=tuple(names)))
    boxes = {
        name: _box(box, f"layout.boxes[{name}]")
        for name, box in (layout_raw.get("boxes") or {}).items()
    }
    if mode == "bands":
        _require(rows, "layout.rows is required in bands mode")
    else:
        _require(boxes, "layout.boxes is required in boxes mode")
    layout = LayoutSpec(
        mode=mode,
        rows=tuple(rows),
        boxes=boxes,
        min_cell_span=layout_raw.get("min_cell_span", 8),
        row_gap=layout_raw.get("row_gap", 8),
        column_gap=layout_raw.get("column_gap", 6),
    )

    rig_raw = raw.get("rig") or {}
    canvas = _pair(rig_raw.get("canvas", [80, 60]), "rig.canvas")
    baseline = rig_raw.get("baseline_y", canvas[1] - 4)
    _require(
        isinstance(baseline, int) and 0 < baseline <= canvas[1],
        "rig.baseline_y must sit inside the canvas",
    )
    max_body = _pair(
        rig_raw.get("max_body", [canvas[0] - 6, canvas[1] - 7]),
        "rig.max_body",
    )
    _require(
        max_body[0] <= canvas[0] and max_body[1] <= canvas[1],
        "rig.max_body must fit the canvas",
    )
    mouth_raw = rig_raw.get("mouth") or {}
    mouth_box = _box(
        mouth_raw.get(
            "box",
            [canvas[0] // 2 - 15, canvas[1] * 2 // 3, 30, 18],
        ),
        "rig.mouth.box",
    )
    mouth_source = mouth_raw.get("source", "auto")
    _require(
        mouth_source in ("auto", "isolated", "face_crop"),
        "rig.mouth.source must be auto, isolated, or face_crop",
    )
    eyes_raw = rig_raw.get("eyes") or {}
    blink = eyes_raw.get("blink", "synth" if eyes_raw else "none")
    _require(blink in ("synth", "none"), "rig.eyes.blink must be synth|none")
    eye_left = eyes_raw.get("left")
    eye_right = eyes_raw.get("right")
    if blink == "synth":
        _require(
            eye_left is not None and eye_right is not None,
            "rig.eyes.left/right boxes are required for blink synth",
        )
        eye_left = _box(eye_left, "rig.eyes.left")
        eye_right = _box(eye_right, "rig.eyes.right")
    else:
        eye_left = _box(eye_left, "rig.eyes.left") if eye_left else None
        eye_right = _box(eye_right, "rig.eyes.right") if eye_right else None
    rig = RigSpec(
        canvas=canvas,
        baseline_y=baseline,
        max_body=max_body,
        mouth_box=mouth_box,
        mouth_erase=bool(mouth_raw.get("erase", True)),
        mouth_source=mouth_source,
        eye_left=eye_left,
        eye_right=eye_right,
        blink=blink,
    )

    palette_raw = raw.get("palette") or {}
    max_colors = palette_raw.get("max_colors", 15)
    _require(
        isinstance(max_colors, int) and 2 <= max_colors <= 255,
        "palette.max_colors must be 2..255 (index 0 stays transparent)",
    )
    locked_raw = palette_raw.get("locked")
    locked = None
    if locked_raw is not None:
        _require(
            isinstance(locked_raw, list) and 2 <= len(locked_raw) <= 255,
            "palette.locked must list 2..255 #rrggbb colors",
        )
        from .util import parse_hex_color

        locked = tuple(parse_hex_color(c) for c in locked_raw)
    palette = PaletteSpec(
        max_colors=max_colors,
        locked=locked,
        despeckle=bool(palette_raw.get("despeckle", True)),
    )

    expressions_raw = raw.get("expressions", "canonical11")
    if expressions_raw == "canonical11":
        expressions = tuple(CANONICAL_BY_SLUG)
    else:
        _require(
            isinstance(expressions_raw, list) and expressions_raw,
            "expressions must be 'canonical11' or a list of slugs",
        )
        for slug in expressions_raw:
            _require(
                slug in CANONICAL_BY_SLUG,
                f"unknown expression slug {slug!r}",
            )
        expressions = tuple(expressions_raw)

    targets_raw = raw.get("targets")
    _require(
        isinstance(targets_raw, list) and targets_raw,
        "at least one target is required",
    )
    targets: list[TargetSpec] = []
    for index, target in enumerate(targets_raw):
        target_id = _ident(target.get("id"), f"targets[{index}].id")
        framing = target.get("framing", "face_fill")
        _require(
            framing in ("face_fill", "stage"),
            f"targets[{index}].framing must be face_fill or stage",
        )
        canvas_t = (
            _pair(target["canvas"], f"targets[{index}].canvas")
            if "canvas" in target
            else canvas
        )
        scale = target.get("scale", 1)
        _require(
            isinstance(scale, int) and 1 <= scale <= 8,
            f"targets[{index}].scale must be 1..8",
        )
        _require(
            canvas_t[0] * scale <= 160 and canvas_t[1] * scale <= 120,
            f"targets[{index}]: canvas x scale must fit 160x120",
        )
        margin = target.get("margin", 2 if framing == "stage" else 0)
        _require(
            isinstance(margin, int)
            and 0 <= margin * 2 < min(canvas_t),
            f"targets[{index}].margin leaves no canvas",
        )
        max_flash = target.get("max_flash_bytes", 65536)
        _require(
            isinstance(max_flash, int) and max_flash > 0,
            f"targets[{index}].max_flash_bytes must be positive",
        )
        targets.append(
            TargetSpec(
                id=target_id,
                framing=framing,
                canvas=canvas_t,
                scale=scale,
                margin=margin,
                max_flash_bytes=max_flash,
                emit_c=bool(target.get("emit_c", True)),
            )
        )
    _require(
        len({t.id for t in targets}) == len(targets),
        "target ids must be unique",
    )

    flags = tuple(raw.get("flags", ["auto_blink"]))
    for flag in flags:
        _require(flag in VALID_FLAGS, f"unknown flag {flag!r}")

    mouth_names = [
        name for row in rows if row.role == "mouth" for name in row.names
    ]
    _require(
        len(set(mouth_names)) == len(mouth_names),
        "duplicate mouth slot names in layout",
    )
    expression_names = [
        name
        for row in rows
        if row.role == "expression"
        for name in row.names
    ]
    _require(
        len(set(expression_names)) == len(expression_names),
        "duplicate expression names in layout",
    )

    return CharacterSpec(
        schema_version=1,
        id=spec_id,
        display_name=display_name,
        license=license_id,
        source=source,
        layout=layout,
        rig=rig,
        palette=palette,
        expressions=expressions,
        targets=tuple(targets),
        flags=flags,
        spec_path=spec_path,
        notes=raw.get("notes", ""),
        raw=raw,
    )
