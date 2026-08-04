"""Validation gates.

Every gate returns pass/warn/fail with machine-readable evidence. Numeric
gates can reject an atlas but never promote one; visual acceptance stays a
human decision over the preview sheets, matching the repo's established
review discipline.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .catalog import TargetAtlas
from .util import unpackbits
from .vocab import (
    CANONICAL_EXPRESSIONS,
    CELL_NONE,
    MOUTH_SLOT_NAMES,
    VISEME_SET_SLOTS,
    action_distance,
)

PASS = "pass"
WARN = "warn"
FAIL = "fail"

_ORDER = {PASS: 0, WARN: 1, FAIL: 2}


@dataclass
class Gate:
    name: str
    status: str
    details: dict = field(default_factory=dict)

    def as_json(self) -> dict:
        return {"name": self.name, "status": self.status, **self.details}


def worst(gates: list[Gate]) -> str:
    status = PASS
    for gate in gates:
        if _ORDER[gate.status] > _ORDER[status]:
            status = gate.status
    return status


def _gate_packbits(atlas: TargetAtlas) -> Gate:
    for index, cell in enumerate(atlas.cells):
        expected = cell.width * cell.height
        if len(cell.raw) != expected:
            return Gate(
                "packbits_roundtrip",
                FAIL,
                {"cell": index, "error": "raw length mismatch"},
            )
        try:
            if cell.encoding == 1:
                decoded = unpackbits(cell.data, expected)
            else:
                decoded = cell.data
        except ValueError as error:
            return Gate(
                "packbits_roundtrip",
                FAIL,
                {"cell": index, "error": str(error)},
            )
        if decoded != cell.raw:
            return Gate(
                "packbits_roundtrip",
                FAIL,
                {"cell": index, "error": "decode differs from raw indices"},
            )
    return Gate(
        "packbits_roundtrip", PASS, {"cells": len(atlas.cells)}
    )


def _gate_player_contract(atlas: TargetAtlas) -> Gate:
    problems: list[str] = []
    width, height = atlas.canvas
    scale = atlas.target.scale
    if width * scale > 160 or height * scale > 120:
        problems.append(
            f"canvas {width}x{height} at scale {scale} exceeds 160x120"
        )
    palette_count = len(atlas.palette.entries())
    if palette_count > 256:
        problems.append(f"palette has {palette_count} entries")
    for index, cell in enumerate(atlas.cells):
        if cell.width > 160 or cell.height > 120:
            problems.append(f"cell {index} exceeds the frame")
        for value in cell.raw:
            if value >= palette_count:
                problems.append(
                    f"cell {index} references palette index {value}"
                )
                break
    for array in atlas.mouth_arrays:
        if len(array) != len(MOUTH_SLOT_NAMES):
            problems.append("mouth array is not exactly 23 slots")
        for value in array:
            if value != CELL_NONE and value >= len(atlas.cells):
                problems.append(f"mouth slot references cell {value}")
    if len(atlas.fallback_slots) != 9:
        problems.append("fallback_slots must have 9 role entries")
    elif atlas.fallback_slots[0] == 0xFF:
        problems.append("rest role has no fallback slot")
    pairs = {(s, v) for s, v, _, _ in atlas.viseme_map}
    if len(pairs) != len(atlas.viseme_map):
        problems.append("duplicate (viseme_set, viseme) pairs")
    for _, _, slot, role in atlas.viseme_map:
        if slot >= len(MOUTH_SLOT_NAMES):
            problems.append(f"viseme map references slot {slot}")
        if role != 0xFF and role >= 9:
            problems.append(f"viseme map role {role} out of range")
    if not atlas.banks:
        problems.append("atlas has no banks")
    elif atlas.banks[0].slug != "neutral":
        problems.append("bank zero is not the neutral fallback")
    for bank in atlas.banks:
        for eye in (bank.eye_left, bank.eye_right):
            if len(eye.cells) > 6:
                problems.append("eye layer exceeds 6 lid cells")
    return Gate(
        "player_contract",
        FAIL if problems else PASS,
        {"problems": problems} if problems else {},
    )


def _gate_registration(atlas: TargetAtlas) -> Gate:
    """Anchors must land on face pixels in every bank.

    Base-cell widths vary legitimately (detached decorations ride along),
    so the strict checks are baseline consistency and mouth/eye anchor
    containment; width deviation is reported for review only.
    """
    bottoms: list[int] = []
    widths: list[int] = []
    uncontained: list[str] = []
    for bank in atlas.banks:
        cell = atlas.cells[bank.base_cell]
        bottoms.append(cell.offset_y + cell.height)
        widths.append(cell.width)
        base_x0, base_y0 = cell.offset_x, cell.offset_y
        base_x1, base_y1 = base_x0 + cell.width, base_y0 + cell.height
        anchors = {
            "mouth": (bank.mouth_x, bank.mouth_y),
        }
        for side, eye in (
            ("eye_left", bank.eye_left),
            ("eye_right", bank.eye_right),
        ):
            if eye.cells:
                anchors[side] = (eye.x, eye.y)
        for label, (x, y) in anchors.items():
            if not (base_x0 <= x < base_x1 and base_y0 <= y < base_y1):
                uncontained.append(f"{bank.slug}:{label}")
    if not widths:
        return Gate("registration", FAIL, {"problems": ["no banks"]})
    median_width = sorted(widths)[len(widths) // 2]
    max_dev = max(
        abs(w - median_width) * 100 // max(1, median_width) for w in widths
    )
    bottom_spread = max(bottoms) - min(bottoms)
    details = {
        "base_bottoms": bottoms,
        "base_widths": widths,
        "width_deviation_pct": max_dev,
        "bottom_spread_px": bottom_spread,
        "anchors_outside_base": uncontained,
    }
    limit = max(2, atlas.canvas[1] // 20)
    if uncontained or bottom_spread > 2 * limit:
        return Gate("registration", FAIL, details)
    if bottom_spread > limit or max_dev > 30:
        return Gate("registration", WARN, details)
    return Gate("registration", PASS, details)


def _gate_clipping(atlas: TargetAtlas) -> Gate:
    width, height = atlas.canvas
    margin = atlas.target.margin
    outside: list[str] = []
    beyond_margin: list[str] = []

    def check(label: str, x0: int, y0: int, w: int, h: int) -> None:
        if x0 < 0 or y0 < 0 or x0 + w > width or y0 + h > height:
            outside.append(label)
        elif atlas.target.framing == "stage" and (
            x0 < margin
            or y0 < margin
            or x0 + w > width - margin
            or y0 + h > height - margin
        ):
            beyond_margin.append(label)

    for bank in atlas.banks:
        base = atlas.cells[bank.base_cell]
        check(
            f"base:{bank.slug}",
            base.offset_x,
            base.offset_y,
            base.width,
            base.height,
        )
        for slot_cell in set(atlas.mouth_arrays[bank.mouth_array]):
            if slot_cell == CELL_NONE:
                continue
            cell = atlas.cells[slot_cell]
            check(
                f"mouth:{bank.slug}:{slot_cell}",
                bank.mouth_x + cell.offset_x,
                bank.mouth_y + cell.offset_y,
                cell.width,
                cell.height,
            )
        for side, eye in (
            ("left", bank.eye_left),
            ("right", bank.eye_right),
        ):
            for lid in eye.cells:
                cell = atlas.cells[lid]
                check(
                    f"lid:{bank.slug}:{side}",
                    eye.x + cell.offset_x,
                    eye.y + cell.offset_y,
                    cell.width,
                    cell.height,
                )
    if outside:
        return Gate("clipping", FAIL, {"outside_canvas": sorted(set(outside))})
    if beyond_margin:
        return Gate(
            "clipping", WARN, {"beyond_margin": sorted(set(beyond_margin))}
        )
    return Gate("clipping", PASS, {})


def _gate_palette(atlas: TargetAtlas) -> Gate:
    spec = atlas.spec.palette
    visible = len(atlas.palette.rgb)
    details = {
        "visible_colors": visible,
        "max_colors": spec.max_colors,
        "merged_after_565": atlas.palette.merged_565,
    }
    if visible > spec.max_colors:
        return Gate("palette", FAIL, details)
    if atlas.palette.merged_565 and spec.locked is not None:
        # A locked palette losing colors to RGB565 truncation is an
        # authoring mistake; quantized palettes merge silently by design.
        return Gate("palette", WARN, details)
    return Gate("palette", PASS, details)


def _gate_coverage(atlas: TargetAtlas) -> Gate:
    authored = {bank.slug for bank in atlas.banks}
    canonical_map = {}
    worst_distance = 0
    for target in CANONICAL_EXPRESSIONS:
        nearest = min(
            atlas.banks,
            key=lambda bank: (
                action_distance(target, bank.target),
                bank.slug,
            ),
        )
        distance = action_distance(target, nearest.target)
        canonical_map[target.slug] = {
            "bank": nearest.slug,
            "distance": distance,
        }
        worst_distance = max(worst_distance, distance)
    missing = [
        slug for slug in atlas.spec.expressions if slug not in authored
    ]
    borrowed = [
        {
            "slot": MOUTH_SLOT_NAMES[slot],
            "from": MOUTH_SLOT_NAMES[source],
        }
        for slot, source in enumerate(atlas.mouth_sources)
        if source != slot
    ]
    set_counts = {}
    for viseme_set, slots in VISEME_SET_SLOTS.items():
        present = {
            v for s, v, _, _ in atlas.viseme_map if s == viseme_set
        }
        set_counts[viseme_set] = {
            "expected": len(slots),
            "mapped": len(present),
        }
    problems = [
        f"viseme set {viseme_set} maps {info['mapped']}/{info['expected']}"
        for viseme_set, info in set_counts.items()
        if info["mapped"] != info["expected"]
    ]
    unreachable = [
        MOUTH_SLOT_NAMES[slot]
        for array in atlas.mouth_arrays
        for slot, cell in enumerate(array)
        if cell == CELL_NONE
    ]
    if unreachable:
        problems.append(f"slots without pixels: {unreachable}")
    details = {
        "authored_expressions": sorted(authored),
        "missing_expressions": missing,
        "canonical_targets": canonical_map,
        "worst_canonical_distance": worst_distance,
        "borrowed_mouth_slots": borrowed,
        "viseme_sets": {
            str(k): v for k, v in sorted(set_counts.items())
        },
        "blink_stages": len(atlas.banks[0].eye_left.cells),
    }
    if problems:
        details["problems"] = problems
        return Gate("coverage", FAIL, details)
    if missing:
        return Gate("coverage", WARN, details)
    return Gate("coverage", PASS, details)


def _gate_budget(atlas: TargetAtlas) -> Gate:
    flash = atlas.flash_bytes
    limit = atlas.target.max_flash_bytes
    details = {"flash_bytes": flash, "max_flash_bytes": limit}
    if flash > limit:
        return Gate("budget", FAIL, details)
    if flash * 10 > limit * 9:
        return Gate("budget", WARN, details)
    return Gate("budget", PASS, details)


def _gate_legibility(atlas: TargetAtlas) -> Gate:
    """Minimum feature sizes after target scaling."""
    bank = atlas.banks[0]
    mouth_cells = [
        atlas.cells[c]
        for c in set(atlas.mouth_arrays[bank.mouth_array])
        if c != CELL_NONE
    ]
    mouth_h = max((c.height for c in mouth_cells), default=0)
    details = {"tallest_mouth_px": mouth_h}
    if bank.eye_left.cells:
        lid = atlas.cells[bank.eye_left.cells[-1]]
        details["lid_px"] = [lid.width, lid.height]
        if lid.width < 2 or lid.height < 2:
            return Gate("legibility", WARN, details)
    if mouth_h < 3:
        return Gate("legibility", WARN, details)
    return Gate("legibility", PASS, details)


def validate_target(atlas: TargetAtlas) -> list[Gate]:
    return [
        _gate_packbits(atlas),
        _gate_player_contract(atlas),
        _gate_registration(atlas),
        _gate_clipping(atlas),
        _gate_palette(atlas),
        _gate_coverage(atlas),
        _gate_budget(atlas),
        _gate_legibility(atlas),
    ]
