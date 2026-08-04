#!/usr/bin/env python3
"""Validate hand-authored ASCII sprite art in the renderer sources.

Every string literal inside a `fmg_rows*_t` / `fmg_glyph_t` array must have
exactly the declared width, rows must match the declared height, and only
palette characters may appear. Run from the contribution root:

    python3 tools/check_sprites.py
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# type name -> (width, height, allowed characters)
SPRITE_TYPES = {
    "fmg_rows_t": (26, 14, set(".olitg")),
    "fmg_rows8_t": (12, 8, set(".oritg")),
    "fmg_rows10_t": (20, 10, set(".oitg")),
    "fmg_glyph_t": (12, 7, set(".#+")),
}

ARRAY_RE = re.compile(
    r"^static\s+(fmg_(?:rows\w*|glyph)_t)\s+(\w+)\s*=\s*\{(.*?)\};",
    re.S | re.M,
)
STRING_RE = re.compile(r'"([^"]*)"')


def main() -> int:
    failures = 0
    arrays = 0
    for path in sorted(SRC.glob("fmg_r_*.c")):
        text = path.read_text()
        for match in ARRAY_RE.finditer(text):
            type_name, array_name, body = match.groups()
            if type_name not in SPRITE_TYPES:
                print(f"{path.name}: {array_name}: unknown type {type_name}")
                failures += 1
                continue
            width, height, allowed = SPRITE_TYPES[type_name]
            rows = STRING_RE.findall(body)
            arrays += 1
            if len(rows) != height:
                print(
                    f"{path.name}: {array_name}: {len(rows)} rows,"
                    f" expected {height}"
                )
                failures += 1
            for i, row in enumerate(rows):
                if len(row) != width:
                    print(
                        f"{path.name}: {array_name} row {i}:"
                        f" {len(row)} chars, expected {width}: {row!r}"
                    )
                    failures += 1
                bad = set(row) - allowed
                if bad:
                    print(
                        f"{path.name}: {array_name} row {i}:"
                        f" illegal chars {sorted(bad)}"
                    )
                    failures += 1
    print(f"checked {arrays} sprite arrays, {failures} problems")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
