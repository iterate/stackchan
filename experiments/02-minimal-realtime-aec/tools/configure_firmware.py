#!/usr/bin/env python3
"""Generate the gitignored firmware credential header without printing secrets."""

from __future__ import annotations

import argparse
import ast
import os
import re
import subprocess
from pathlib import Path


EXPERIMENT = Path(__file__).resolve().parents[1]
DEFAULT_LEGACY_SETTINGS = (
    EXPERIMENT
    / "upstream"
    / "esp-webrtc-solution"
    / "solutions"
    / "openai_demo"
    / "main"
    / "settings.h"
)
OUTPUT = EXPERIMENT / "local" / "stackchan_local_secrets.h"


def _legacy_define(path: Path, name: str) -> str | None:
    if not path.exists():
        return None
    pattern = re.compile(rf"^\s*#define\s+{re.escape(name)}\s+(.+?)\s*$")
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            value = ast.literal_eval(match.group(1))
            if not isinstance(value, str):
                raise ValueError(f"{path}: {name} is not a C string")
            return value
    return None


def _doppler_secret(project: str, config: str, name: str) -> str:
    result = subprocess.run(
        [
            "doppler",
            "secrets",
            "get",
            name,
            "--plain",
            "--project",
            project,
            "--config",
            config,
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    value = result.stdout.rstrip("\r\n")
    if not value:
        raise RuntimeError(f"Doppler returned an empty {name}")
    return value


def _c_string(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )
    return f'"{escaped}"'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--doppler-project",
        default=os.environ.get("STACKCHAN_DOPPLER_PROJECT", "os"),
    )
    parser.add_argument(
        "--doppler-config",
        default=os.environ.get("STACKCHAN_DOPPLER_CONFIG", "dev_jonas"),
    )
    parser.add_argument(
        "--legacy-settings",
        type=Path,
        default=DEFAULT_LEGACY_SETTINGS,
        help="optional ignored settings.h used only to migrate Wi-Fi locally",
    )
    args = parser.parse_args()

    ssid = os.environ.get("STACKCHAN_WIFI_SSID") or _legacy_define(
        args.legacy_settings, "WIFI_SSID"
    )
    password = os.environ.get("STACKCHAN_WIFI_PASSWORD") or _legacy_define(
        args.legacy_settings, "WIFI_PASSWORD"
    )
    if not ssid or not password:
        raise RuntimeError(
            "Set STACKCHAN_WIFI_SSID and STACKCHAN_WIFI_PASSWORD; "
            "no usable ignored legacy settings were found"
        )
    api_key = os.environ.get("OPENAI_API_KEY") or _doppler_secret(
        args.doppler_project, args.doppler_config, "OPENAI_API_KEY"
    )

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                "/* Generated locally; this directory is ignored by git. */",
                f"#define STACKCHAN_WIFI_SSID {_c_string(ssid)}",
                f"#define STACKCHAN_WIFI_PASSWORD {_c_string(password)}",
                f"#define STACKCHAN_OPENAI_API_KEY {_c_string(api_key)}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    os.chmod(OUTPUT, 0o600)
    print(
        f"Wrote {OUTPUT} (SSID {len(ssid)} chars, password {len(password)} "
        f"chars, API key {len(api_key)} chars; values not printed)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
