#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# ///

"""Collect a self-contained StackChan device evidence bundle."""

from __future__ import annotations

import argparse
import array
import hashlib
import json
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import wave
import zipfile
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


ENDPOINTS = (
    ("root.json", "/", "json"),
    ("status.json", "/api/status", "json"),
    ("avatar.json", "/api/avatar", "json"),
    ("debug-state.json", "/api/debug/state", "json"),
    ("device.log", "/api/debug/logs.txt", "text"),
    (
        "realtime-events.jsonl",
        "/api/debug/realtime-events.jsonl",
        "text",
    ),
    ("screen.bmp", "/api/debug/screen.bmp", "binary"),
    ("recent-audio.wav", "/api/debug/recent.wav", "binary"),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download StackChan status, logs, real screen pixels, and the "
            "rolling synchronized mic/reference/AEC audio window."
        )
    )
    parser.add_argument(
        "--device",
        default="http://stackchan.local",
        help="Device base URL or IP address (default: http://stackchan.local)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "local" / "device-evidence",
        help="Directory under which timestamped bundles are created",
    )
    parser.add_argument(
        "--label",
        default="capture",
        help="Short filesystem-safe suffix for this capture",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="Per-endpoint timeout in seconds",
    )
    parser.add_argument(
        "--open-screen",
        action="store_true",
        help="Open the downloaded screen and avatar images in the macOS viewer",
    )
    parser.add_argument(
        "--no-avatar-sweep",
        action="store_true",
        help="Do not cycle, capture, and restore every device-valid avatar",
    )
    parser.add_argument(
        "--no-zip",
        action="store_true",
        help="Leave only the directory and do not create a ZIP archive",
    )
    parser.add_argument(
        "--no-assess",
        action="store_true",
        help="skip local signal, alignment, and timeline assessment",
    )
    parser.add_argument(
        "--transcribe",
        action="store_true",
        help="also transcribe non-quiet audio stems with the OpenAI API",
    )
    parser.add_argument(
        "--transcription-model",
        default="gpt-4o-transcribe",
        choices=("gpt-4o-transcribe", "gpt-4o-mini-transcribe", "whisper-1"),
    )
    return parser.parse_args()


def normalise_device(value: str) -> str:
    value = value.strip().rstrip("/")
    if "://" not in value:
        value = f"http://{value}"
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(f"Invalid device URL: {value}")
    return value


def safe_label(value: str) -> str:
    cleaned = "".join(
        character if character.isalnum() or character in "-_" else "-"
        for character in value.strip()
    ).strip("-")
    return cleaned or "capture"


def fetch(
    base_url: str,
    endpoint: str,
    timeout: float,
    method: str = "GET",
) -> tuple[bytes, str, int]:
    request = urllib.request.Request(
        f"{base_url}{endpoint}",
        headers={"User-Agent": "stackchan-evidence-harness/1"},
        method=method,
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return (
            response.read(),
            response.headers.get_content_type(),
            response.status,
        )


def write_payload(path: Path, payload: bytes, kind: str) -> None:
    if kind == "json":
        decoded = json.loads(payload.decode("utf-8"))
        path.write_text(
            json.dumps(decoded, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    elif kind == "text":
        path.write_text(payload.decode("utf-8", errors="replace"), encoding="utf-8")
    else:
        path.write_bytes(payload)


def decode_json(payload: bytes) -> dict[str, Any]:
    decoded = json.loads(payload.decode("utf-8"))
    if not isinstance(decoded, dict):
        raise ValueError("Expected a JSON object")
    return decoded


def capture_avatar_sweep(
    base_url: str,
    output_dir: Path,
    timeout: float,
) -> dict[str, Any]:
    """Capture each device avatar once, restoring the original selection."""

    payload, _, _ = fetch(base_url, "/api/avatar", timeout)
    initial = decode_json(payload)
    original_index = int(initial["index"])
    count = int(initial["count"])
    if count < 1 or count > 16:
        raise ValueError(f"Refusing unexpected avatar count: {count}")
    if original_index < 0 or original_index >= count:
        raise ValueError(f"Invalid current avatar index: {original_index}/{count}")

    avatar_dir = output_dir / "avatars"
    avatar_dir.mkdir()
    result: dict[str, Any] = {
        "ok": True,
        "original_index": original_index,
        "count": count,
        "captures": [],
        "restored": False,
    }
    state = initial
    seen: set[int] = set()

    try:
        while len(seen) < count:
            index = int(state["index"])
            if index in seen:
                raise RuntimeError(
                    f"Avatar cycle repeated index {index} before covering {count}"
                )
            seen.add(index)
            slug = safe_label(str(state.get("slug", f"avatar-{index}")))
            filename = f"{index:02d}-{slug}.bmp"
            screen, content_type, status = fetch(
                base_url, "/api/debug/screen.bmp", timeout
            )
            (avatar_dir / filename).write_bytes(screen)
            result["captures"].append(
                {
                    "index": index,
                    "slug": state.get("slug"),
                    "name": state.get("name"),
                    "file": f"avatars/{filename}",
                    "http_status": status,
                    "content_type": content_type,
                    "bytes": len(screen),
                    "sha256": hashlib.sha256(screen).hexdigest(),
                }
            )
            if len(seen) < count:
                payload, _, _ = fetch(
                    base_url, "/api/avatar", timeout, method="POST"
                )
                state = decode_json(payload)
    finally:
        # Re-read the authoritative state in case a POST changed the device but
        # its response was interrupted before the host received it.
        payload, _, _ = fetch(base_url, "/api/avatar", timeout)
        state = decode_json(payload)
        for _ in range(count):
            if int(state.get("index", -1)) == original_index:
                result["restored"] = True
                break
            payload, _, _ = fetch(
                base_url, "/api/avatar", timeout, method="POST"
            )
            state = decode_json(payload)
        result["final_index"] = int(state.get("index", -1))

    return result


def split_recent_audio(wav_path: Path, output_dir: Path) -> dict[str, Any]:
    with wave.open(str(wav_path), "rb") as recording:
        channels = recording.getnchannels()
        sample_width = recording.getsampwidth()
        sample_rate = recording.getframerate()
        frame_count = recording.getnframes()
        frames = recording.readframes(frame_count)

    if channels != 3 or sample_width != 2:
        raise ValueError(
            f"Expected 3-channel 16-bit WAV, got {channels} channels "
            f"and {sample_width * 8}-bit samples"
        )

    samples = array.array("h")
    samples.frombytes(frames)
    if sys.byteorder != "little":
        samples.byteswap()

    audio_dir = output_dir / "audio"
    audio_dir.mkdir()
    names = (
        "mic-raw.s16le.pcm",
        "speaker-reference.s16le.pcm",
        "aec-clean.s16le.pcm",
    )
    wav_names = ("mic-raw.wav", "speaker-reference.wav", "aec-clean.wav")
    hashes: dict[str, str] = {}
    for channel, (name, wav_name) in enumerate(zip(names, wav_names, strict=True)):
        channel_samples = array.array("h", samples[channel::channels])
        if sys.byteorder != "little":
            channel_samples.byteswap()
        payload = channel_samples.tobytes()
        (audio_dir / name).write_bytes(payload)
        hashes[name] = hashlib.sha256(payload).hexdigest()
        with wave.open(str(audio_dir / wav_name), "wb") as stem:
            stem.setnchannels(1)
            stem.setsampwidth(2)
            stem.setframerate(sample_rate)
            stem.writeframes(payload)
        hashes[wav_name] = hashlib.sha256(
            (audio_dir / wav_name).read_bytes()
        ).hexdigest()

    metadata = {
        "sample_rate_hz": sample_rate,
        "sample_format": "signed 16-bit little-endian PCM",
        "sample_frames": frame_count,
        "duration_seconds": frame_count / sample_rate if sample_rate else 0,
        "channels": [
            {
                "index": 0,
                "file": names[0],
                "wav_file": wav_names[0],
                "meaning": "raw microphone input",
            },
            {
                "index": 1,
                "file": names[1],
                "wav_file": wav_names[1],
                "meaning": "exact PCM written to the speaker codec",
            },
            {
                "index": 2,
                "file": names[2],
                "wav_file": wav_names[2],
                "meaning": "AEC-clean microphone output",
            },
        ],
        "sha256": hashes,
    }
    (audio_dir / "format.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return metadata


def make_zip(bundle_dir: Path) -> Path:
    archive = bundle_dir.with_suffix(".zip")
    with zipfile.ZipFile(
        archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6
    ) as bundle:
        for path in sorted(bundle_dir.rglob("*")):
            if path.is_file():
                bundle.write(path, path.relative_to(bundle_dir.parent))
    return archive


def main() -> int:
    args = parse_args()
    try:
        base_url = normalise_device(args.device)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    timestamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    bundle_dir = args.output.resolve() / f"{timestamp}-{safe_label(args.label)}"
    bundle_dir.mkdir(parents=True, exist_ok=False)

    manifest: dict[str, Any] = {
        "captured_at": datetime.now(UTC).isoformat(),
        "device": base_url,
        "endpoints": {},
    }
    successes = 0
    for filename, endpoint, kind in ENDPOINTS:
        record: dict[str, Any] = {"endpoint": endpoint, "file": filename}
        try:
            payload, content_type, status = fetch(base_url, endpoint, args.timeout)
            write_payload(bundle_dir / filename, payload, kind)
            record.update(
                {
                    "ok": True,
                    "http_status": status,
                    "content_type": content_type,
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                }
            )
            successes += 1
        except urllib.error.HTTPError as error:
            body = error.read(512).decode("utf-8", errors="replace")
            record.update(
                {
                    "ok": False,
                    "http_status": error.code,
                    "error": body,
                }
            )
        except Exception as error:  # Preserve all endpoint failures in the bundle.
            record.update({"ok": False, "error": f"{type(error).__name__}: {error}"})
        manifest["endpoints"][endpoint] = record

    if not args.no_avatar_sweep:
        try:
            manifest["avatar_sweep"] = capture_avatar_sweep(
                base_url, bundle_dir, args.timeout
            )
        except Exception as error:
            manifest["avatar_sweep"] = {
                "ok": False,
                "error": f"{type(error).__name__}: {error}",
            }

    recent_audio = bundle_dir / "recent-audio.wav"
    if recent_audio.exists():
        try:
            manifest["audio"] = split_recent_audio(recent_audio, bundle_dir)
        except Exception as error:
            manifest["audio_error"] = f"{type(error).__name__}: {error}"

    if recent_audio.exists() and not args.no_assess:
        assessment_dir = bundle_dir / "assessment"
        command = [
            "uv",
            "run",
            str(Path(__file__).with_name("audio_assess.py")),
            str(recent_audio),
            "--output",
            str(assessment_dir),
            "--transcription-model",
            args.transcription_model,
        ]
        if args.transcribe:
            command.append("--transcribe")
        try:
            result = subprocess.run(
                command,
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )
            manifest["assessment"] = {
                "ok": result.returncode == 0,
                "directory": "assessment",
                "report": "assessment/assessment.json",
                "transcribed": args.transcribe,
                "transcription_model": (
                    args.transcription_model if args.transcribe else None
                ),
            }
            if result.returncode != 0:
                manifest["assessment"]["error"] = result.stderr[-2048:]
        except Exception as error:
            manifest["assessment"] = {
                "ok": False,
                "error": f"{type(error).__name__}: {error}",
            }

    manifest_path = bundle_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    archive: Path | None = None
    if not args.no_zip:
        archive = make_zip(bundle_dir)

    screen = bundle_dir / "screen.bmp"
    if args.open_screen and screen.exists() and sys.platform == "darwin":
        images = [screen, *sorted((bundle_dir / "avatars").glob("*.bmp"))]
        subprocess.run(["open", *(str(image) for image in images)], check=False)

    print(f"Evidence directory: {bundle_dir}")
    if archive is not None:
        print(f"Evidence archive:   {archive}")
    print(f"Endpoints captured: {successes}/{len(ENDPOINTS)}")
    if successes == 0:
        print("Device was not reachable; see manifest.json for errors.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
