#!/usr/bin/env python3
"""FSPP test suite (stdlib unittest; python3 -B tests/run_tests.py).

Covers the codecs, every analysis stage, the fallback vocabulary, and an
end-to-end build of the synthetic Bloomling fixture including the
determinism gate and FSPR structural checks on the emitted C.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

PACK = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PACK))

from fspp import png_io  # noqa: E402
from fspp.pipeline import check_determinism, compute  # noqa: E402
from fspp.quantize import (  # noqa: E402
    IndexedCell,
    build_palette,
    despeckle,
    map_cell,
)
from fspp.raster import key_background  # noqa: E402
from fspp.segment import SegmentError, segment  # noqa: E402
from fspp.snap import Axis, detect_grid, snap  # noqa: E402
from fspp.spec import SpecError, load_spec  # noqa: E402
from fspp.util import packbits, unpackbits  # noqa: E402
from fspp.vocab import (  # noqa: E402
    MOUTH_ROLES,
    MOUTH_SLOT_NAMES,
    build_viseme_map,
    fill_mouth_slots,
    resolve_fallback_slots,
)

FIXTURES = PACK / "fixtures"


def fixture_sheet_bytes() -> bytes:
    sheet = FIXTURES / "bloomling_sheet.png"
    if not sheet.exists():
        sys.path.insert(0, str(FIXTURES))
        from make_fixture_sheet import make_sheet

        png_io.save_rgba(sheet, make_sheet())
    return sheet.read_bytes()


class PngTests(unittest.TestCase):
    def test_rgba_round_trip(self) -> None:
        image = png_io.Image.blank(5, 3)
        for i in range(15):
            image.rgba[i * 4 : i * 4 + 4] = bytes(
                (i * 17 % 256, i * 29 % 256, i * 43 % 256, 255 if i % 2 else 0)
            )
        decoded = png_io.decode(png_io.encode_rgba(image))
        self.assertEqual(decoded.width, 5)
        self.assertEqual(decoded.height, 3)
        self.assertEqual(bytes(decoded.rgba), bytes(image.rgba))

    def test_indexed_round_trip(self) -> None:
        palette = [(0, 0, 0), (255, 0, 0), (0, 255, 0)]
        indices = bytes([0, 1, 2, 2, 1, 0])
        data = png_io.encode_indexed(3, 2, indices, palette, 0)
        decoded = png_io.decode(data)
        self.assertEqual(decoded.get(0, 0)[3], 0)  # transparent index
        self.assertEqual(decoded.get(1, 0), (255, 0, 0, 255))
        self.assertEqual(decoded.get(1, 1), (255, 0, 0, 255))
        self.assertEqual(decoded.get(2, 1)[3], 0)

    def test_pixel_hash_ignores_encoder(self) -> None:
        image = png_io.Image.blank(4, 4, (9, 8, 7, 255))
        self.assertEqual(
            png_io.hash_rgba(image),
            png_io.hash_rgba(png_io.decode(png_io.encode_rgba(image))),
        )


class PackbitsTests(unittest.TestCase):
    def round_trip(self, data: bytes) -> None:
        encoded = packbits(data)
        self.assertEqual(unpackbits(encoded, len(data)), data)

    def test_runs_and_literals(self) -> None:
        self.round_trip(b"\x05" * 300)
        self.round_trip(bytes(range(256)))
        self.round_trip(b"\x01\x01\x01\x02\x03\x03\x03\x03\x04")
        self.round_trip(b"\x00")
        self.round_trip(bytes([1, 2] * 200))

    def test_decoder_rejects_trailing_bytes(self) -> None:
        encoded = packbits(b"\x07" * 10) + b"\x00\x01"
        with self.assertRaises(ValueError):
            unpackbits(encoded, 10)


class SnapTests(unittest.TestCase):
    def _grid_image(self, pitch: int) -> png_io.Image:
        colors = [(200, 40, 40, 255), (40, 200, 40, 255), (40, 40, 200, 255)]
        image = png_io.Image.blank(pitch * 12, pitch * 10)
        for ly in range(10):
            for lx in range(12):
                color = colors[(lx * 7 + ly * 5) % 3]
                for y in range(ly * pitch, (ly + 1) * pitch):
                    for x in range(lx * pitch, (lx + 1) * pitch):
                        image.put(x, y, color)
        return image

    def test_detects_clean_pitch(self) -> None:
        for pitch in (3, 5, 8):
            x_axis, y_axis = detect_grid(self._grid_image(pitch))
            self.assertEqual(x_axis.pitch, pitch, f"pitch {pitch}")
            self.assertEqual(y_axis.pitch, pitch, f"pitch {pitch}")

    def test_snap_reduces_to_logical_grid(self) -> None:
        image = self._grid_image(4)
        snapped = snap(image, Axis(4, 0, 100), Axis(4, 0, 100))
        self.assertEqual((snapped.width, snapped.height), (12, 10))
        self.assertEqual(snapped.get(0, 0), image.get(1, 1))


class RasterTests(unittest.TestCase):
    def test_interior_background_color_stays_foreground(self) -> None:
        image = png_io.Image.blank(12, 12, (240, 240, 230, 255))
        for y in range(3, 9):
            for x in range(3, 9):
                image.put(x, y, (30, 90, 30, 255))
        image.put(5, 5, (240, 240, 230, 255))  # highlight inside the body
        mask, background = key_background(image, "auto", 16)
        self.assertEqual(background, (240, 240, 230))
        self.assertEqual(mask[5 * 12 + 5], 1)  # interior pixel kept
        self.assertEqual(mask[0], 0)


class SegmentTests(unittest.TestCase):
    def test_bands_and_narrow_merge(self) -> None:
        spec = load_spec(FIXTURES / "bloomling_spec.json")
        width = height = 60
        mask = bytearray(width * height)

        def blob(x0, y0, w, h):
            for y in range(y0, y0 + h):
                for x in range(x0, x0 + w):
                    mask[y * width + x] = 1

        # Row 1: two big cells, the second with a detached 2px speck.
        blob(2, 2, 14, 10)
        blob(30, 2, 14, 10)
        blob(46, 4, 2, 2)
        # Rows 2-4 to satisfy the 4-row layout.
        blob(2, 20, 14, 8)
        blob(2, 36, 10, 4)
        blob(2, 48, 10, 4)
        layout_spec = spec.layout
        rows = layout_spec.rows
        from fspp.spec import LayoutRow, LayoutSpec

        layout = LayoutSpec(
            mode="bands",
            rows=(
                LayoutRow("expression", ("neutral", "joy")),
                LayoutRow("expression", ("sleepy",)),
                LayoutRow("mouth", ("sil",)),
                LayoutRow("mouth", ("aa",)),
            ),
            boxes={},
            min_cell_span=6,
            row_gap=6,
            column_gap=6,
        )
        cells = segment(mask, width, height, layout)
        names = [c.name for c in cells]
        self.assertEqual(
            names, ["neutral", "joy", "sleepy", "sil", "aa"]
        )
        joy = next(c for c in cells if c.name == "joy")
        self.assertGreaterEqual(joy.x + joy.w, 48)  # speck merged into joy

    def test_wrong_band_count_raises(self) -> None:
        from fspp.spec import LayoutRow, LayoutSpec

        layout = LayoutSpec(
            mode="bands",
            rows=(LayoutRow("expression", ("neutral",)),) * 3,
            boxes={},
            min_cell_span=4,
            row_gap=4,
            column_gap=4,
        )
        mask = bytearray(20 * 20)
        for x in range(4, 16):
            mask[5 * 20 + x] = 1
        with self.assertRaises(SegmentError):
            segment(mask, 20, 20, layout)


class QuantizeTests(unittest.TestCase):
    def _noise_cell(self) -> png_io.Image:
        image = png_io.Image.blank(16, 16)
        for y in range(16):
            for x in range(16):
                base = 40 + ((x * 13 + y * 7) % 5)
                image.put(x, y, (base, 200 - base, 90, 255))
        return image

    def test_palette_budget_and_determinism(self) -> None:
        cells = [self._noise_cell()]
        first = build_palette(cells, 4, None)
        second = build_palette(cells, 4, None)
        self.assertEqual(first.rgb, second.rgb)
        self.assertLessEqual(len(first.rgb), 4)

    def test_locked_palette_and_mapping(self) -> None:
        locked = ((10, 10, 10), (240, 240, 240))
        palette = build_palette([self._noise_cell()], 8, locked)
        self.assertEqual(palette.rgb, list(locked))
        image = png_io.Image.blank(2, 1)
        image.put(0, 0, (0, 0, 0, 255))
        image.put(1, 0, (255, 255, 255, 255))
        cell = map_cell(image, "test", palette)
        self.assertEqual(list(cell.indices), [1, 2])

    def test_despeckle_removes_lonely_pixel(self) -> None:
        indices = bytearray([1] * 25)
        indices[12] = 2  # isolated speck in the middle
        cell = IndexedCell("test", 5, 5, indices)
        changed = despeckle(cell)
        self.assertEqual(changed, 1)
        self.assertEqual(cell.indices[12], 1)


class VocabTests(unittest.TestCase):
    def test_fill_mouth_slots_borrows_by_role(self) -> None:
        authored = {0: 100, 10: 110, 13: 113}  # sil, aa, oh
        cells, sources = fill_mouth_slots(authored)
        self.assertEqual(len(cells), len(MOUTH_SLOT_NAMES))
        self.assertEqual(cells[0], 100)
        self.assertEqual(cells[10], 110)
        # 'e' shares the wide role with 'aa'.
        self.assertEqual(cells[11], 110)
        # 'ch' has the round role; 'oh' is the only round cel.
        self.assertEqual(cells[6], 113)
        # 'pp' (press role) has no authored cel; falls back to rest.
        self.assertEqual(cells[1], 100)
        self.assertEqual(sources[1], 0)

    def test_rest_cel_is_required(self) -> None:
        with self.assertRaises(ValueError):
            resolve_fallback_slots({10, 13})

    def test_viseme_map_is_complete_and_unique(self) -> None:
        rows = build_viseme_map()
        pairs = {(s, v) for s, v, _, _ in rows}
        self.assertEqual(len(pairs), len(rows))
        self.assertEqual(len([r for r in rows if r[0] == 0]), 15)
        self.assertEqual(len([r for r in rows if r[0] == 1]), 5)
        self.assertEqual(len([r for r in rows if r[0] == 2]), 9)
        self.assertEqual(len([r for r in rows if r[0] == 3]), 22)
        for _, _, slot, role in rows:
            self.assertEqual(role, MOUTH_ROLES[slot])


class SpecTests(unittest.TestCase):
    def test_fixture_specs_load(self) -> None:
        for name in ("bloomling_spec.json", "mossling_spec.json"):
            spec = load_spec(FIXTURES / name)
            self.assertEqual(spec.schema_version, 1)
            self.assertGreaterEqual(len(spec.targets), 2)

    def test_bad_mouth_name_rejected(self) -> None:
        raw = json.loads(
            (FIXTURES / "bloomling_spec.json").read_text()
        )
        raw["layout"]["rows"][2]["names"] = ["not-a-slot"]
        bad = FIXTURES / "_bad_spec.json"
        bad.write_text(json.dumps(raw))
        try:
            with self.assertRaises(SpecError):
                load_spec(bad)
        finally:
            bad.unlink()


class EndToEndTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.spec = load_spec(FIXTURES / "bloomling_spec.json")
        cls.sheet = fixture_sheet_bytes()
        cls.gate, cls.output = check_determinism(
            cls.spec, cls.sheet, "bloomling_sheet.png"
        )

    def test_deterministic(self) -> None:
        self.assertEqual(self.gate.status, "pass", self.gate.details)

    def test_overall_status(self) -> None:
        self.assertNotEqual(self.output.status, "fail", self.output.report)

    def test_targets_pass_gates(self) -> None:
        for target_id, report in self.output.report["targets"].items():
            self.assertNotEqual(
                report["status"], "fail", (target_id, report["gates"])
            )
            self.assertLessEqual(
                report["flash_bytes"],
                next(
                    t.max_flash_bytes
                    for t in self.spec.targets
                    if t.id == target_id
                ),
            )

    def test_emitted_c_structure(self) -> None:
        c_text = self.output.artifacts[
            "cores3-face/fspp_bloomling_cores3_face_atlas.c"
        ].decode()
        for required in (
            "const face_sprite_atlas_t face_sprite_fspp_bloomling_"
            "cores3_face_atlas",
            ".magic = FACE_SPRITE_MAGIC",
            ".version = FACE_SPRITE_VERSION",
            ".selector = FACE_SPRITE_SELECTOR_DEFAULTS",
            ".timing = FACE_SPRITE_TIMING_DEFAULTS",
            "face_sprite_viseme_map_t",
            "face_sprite_bank_t",
        ):
            self.assertIn(required, c_text)
        header = self.output.artifacts[
            "cores3-face/fspp_bloomling_cores3_face_atlas.h"
        ].decode()
        self.assertIn('#include "face_sprite_sheet.h"', header)

    def test_manifest_consistency(self) -> None:
        manifest = json.loads(
            self.output.artifacts["cores3-face/manifest.json"].decode()
        )
        self.assertEqual(manifest["stats"]["bank_count"], 11)
        self.assertEqual(len(manifest["mouth_arrays"][0]), 23)
        self.assertEqual(manifest["banks"][0]["slug"], "neutral")
        import base64

        blob = base64.b64decode(manifest["blob_base64"])
        self.assertEqual(len(blob), manifest["stats"]["blob_bytes"])
        for cell in manifest["cells"]:
            self.assertLessEqual(
                cell["blob_offset"] + cell["blob_length"], len(blob)
            )
        borrowed = [
            slot for slot in manifest["mouth_slots"] if not slot["authored"]
        ]
        self.assertTrue(borrowed, "fixture authors only 10 of 23 slots")
        for slot in borrowed:
            self.assertIsNotNone(slot["borrowed_from"])

    def test_stage_target_scales_anchors(self) -> None:
        big = json.loads(
            self.output.artifacts["cores3-face/manifest.json"].decode()
        )
        small = json.loads(
            self.output.artifacts["pocket-stage/manifest.json"].decode()
        )
        self.assertEqual(small["target"]["canvas"], [40, 30])
        big_mouth = big["banks"][0]["mouth"]
        small_mouth = small["banks"][0]["mouth"]
        self.assertLess(small_mouth["x"], big_mouth["x"])
        self.assertGreaterEqual(small_mouth["x"], small["target"]["margin"])

    def test_expression_subset_still_builds(self) -> None:
        raw = json.loads(
            (FIXTURES / "bloomling_spec.json").read_text()
        )
        raw["layout"]["rows"][1]["names"] = raw["layout"]["rows"][1][
            "names"
        ][:3]
        raw["layout"]["rows"][1]["names"] = [
            "skeptical", "determined", "sleepy",
        ]
        subset = FIXTURES / "_subset_spec.json"
        subset.write_text(json.dumps(raw))
        try:
            spec = load_spec(subset)
            with self.assertRaises(SegmentError):
                # The sheet still has 5 cell-sized characters in row 2;
                # naming only 3 must refuse, not blend characters.
                compute(spec, self.sheet, "sheet")
        finally:
            subset.unlink()


if __name__ == "__main__":
    unittest.main(verbosity=2)
