#!/usr/bin/env python3
"""FSPP command line.

    spritepipe.py brief --spec SPEC.json --out DIR
        Emit the constrained authoring brief + layout contract for
        prompt/reference sources (pipeline steps 1-2).

    spritepipe.py build --spec SPEC.json --out DIR
        [--sheet SHEET.png] [--debug-dir DIR] [--skip-determinism]
        Run the full assembly line and write atlases, manifests,
        previews, and the validation report.

    spritepipe.py validate --spec SPEC.json [--sheet SHEET.png]
        Run the pipeline in memory and print the validation report.

Stdlib only; python3 -B spritepipe.py ... needs no environment setup.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fspp.pipeline import authoring_brief, build, check_determinism  # noqa: E402
from fspp.spec import SpecError, load_spec  # noqa: E402


def cmd_brief(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    text, contract = authoring_brief(spec)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "authoring_brief.md").write_text(text, encoding="utf-8")
    (out / "layout_contract.json").write_text(
        json.dumps(contract, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"authoring brief for {spec.id!r} written to {out}")
    return 0


def cmd_build(args: argparse.Namespace) -> int:
    output = build(
        args.spec,
        args.out,
        debug_dir=args.debug_dir,
        sheet_override=args.sheet,
        verify_determinism=not args.skip_determinism,
    )
    print(
        f"{output.spec.id}: {output.status} "
        f"({len(output.artifacts)} artifacts -> {args.out})"
    )
    for target_id, target in sorted(output.report["targets"].items()):
        print(
            f"  {target_id}: {target['status']}, "
            f"{target['flash_bytes']} flash bytes, "
            f"{target['cells']} cells, {target['banks']} banks"
        )
    for note in output.notes:
        print(f"  note: {note}")
    return 0 if output.status != "fail" else 1


def cmd_validate(args: argparse.Namespace) -> int:
    spec = load_spec(args.spec)
    sheet = (
        Path(args.sheet)
        if args.sheet
        else spec.resolve_path(spec.source.sheet)
    )
    gate, output = check_determinism(
        spec, sheet.read_bytes(), str(sheet)
    )
    output.report["gates"].append(gate.as_json())
    if gate.status == "fail":
        output.report["status"] = "fail"
    json.dump(output.report, sys.stdout, sort_keys=True, indent=2)
    print()
    return 0 if output.report["status"] != "fail" else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="spritepipe.py",
        description="FSPP: sprite-sheet assembly line for FSPR v2 atlases",
    )
    commands = parser.add_subparsers(dest="command", required=True)

    brief = commands.add_parser("brief", help="emit an authoring brief")
    brief.add_argument("--spec", required=True)
    brief.add_argument("--out", required=True)
    brief.set_defaults(handler=cmd_brief)

    build_cmd = commands.add_parser("build", help="run the assembly line")
    build_cmd.add_argument("--spec", required=True)
    build_cmd.add_argument("--out", required=True)
    build_cmd.add_argument("--sheet", default=None)
    build_cmd.add_argument("--debug-dir", default=None)
    build_cmd.add_argument("--skip-determinism", action="store_true")
    build_cmd.set_defaults(handler=cmd_build)

    validate = commands.add_parser(
        "validate", help="run in memory and print the report"
    )
    validate.add_argument("--spec", required=True)
    validate.add_argument("--sheet", default=None)
    validate.set_defaults(handler=cmd_validate)

    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except (SpecError, ValueError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
