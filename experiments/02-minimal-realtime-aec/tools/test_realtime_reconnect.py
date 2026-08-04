#!/usr/bin/env python3
"""Force and verify repeatable StackChan Realtime reconnections."""

from __future__ import annotations

import argparse
import json
import time
import urllib.parse
import urllib.request
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Ask the firmware to restart its provider WebSocket, then require "
            "connected, session-ready, streaming state within a short deadline."
        )
    )
    parser.add_argument(
        "--device",
        default="http://stackchan.local",
        help="StackChan URL or IP address",
    )
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--poll", type=float, default=0.2)
    return parser.parse_args()


def normalise_device(value: str) -> str:
    value = value.strip().rstrip("/")
    if "://" not in value:
        value = f"http://{value}"
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(f"invalid device URL: {value}")
    return value


def request_json(
    base_url: str,
    endpoint: str,
    *,
    method: str = "GET",
    timeout: float = 5.0,
) -> dict[str, Any]:
    request = urllib.request.Request(
        f"{base_url}{endpoint}",
        method=method,
        headers={"User-Agent": "stackchan-reconnect-regression/1"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        decoded = json.loads(response.read().decode("utf-8"))
    if not isinstance(decoded, dict):
        raise ValueError(f"{endpoint} did not return an object")
    return decoded


def ready(snapshot: dict[str, Any]) -> bool:
    return all(
        bool(snapshot.get(field))
        for field in ("connected", "session_ready", "streaming")
    )


def main() -> int:
    args = parse_args()
    if args.cycles < 1:
        raise ValueError("--cycles must be positive")
    base_url = normalise_device(args.device)
    initial = request_json(base_url, "/api/realtime")
    if not ready(initial):
        raise RuntimeError(f"device is not ready before test: {initial}")

    previous_attempts = int(initial.get("reconnect_attempts", 0))
    timings: list[float] = []
    for cycle in range(1, args.cycles + 1):
        request_json(
            base_url,
            "/api/realtime/reconnect",
            method="POST",
        )
        started = time.monotonic()
        deadline = started + args.timeout
        snapshot: dict[str, Any] = {}
        while time.monotonic() < deadline:
            snapshot = request_json(base_url, "/api/realtime")
            attempts = int(snapshot.get("reconnect_attempts", 0))
            if ready(snapshot) and attempts > previous_attempts:
                elapsed = time.monotonic() - started
                timings.append(elapsed)
                previous_attempts = attempts
                print(
                    f"cycle {cycle}: ready in {elapsed:.2f}s "
                    f"(attempt {attempts})"
                )
                break
            time.sleep(args.poll)
        else:
            raise RuntimeError(
                f"cycle {cycle} did not recover within {args.timeout:.1f}s: "
                f"{snapshot}"
            )

    print(
        f"PASS: {len(timings)} reconnect cycles; "
        f"maximum recovery {max(timings):.2f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
