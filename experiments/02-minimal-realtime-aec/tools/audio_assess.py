#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "matplotlib>=3.9",
#   "numpy>=2.0",
#   "scipy>=1.14",
# ]
# ///
"""Assess synchronized StackChan PCM locally and optionally transcribe it.

The input is the three-channel WAV produced by the device:

    0: raw microphone
    1: exact speaker-reference PCM
    2: AEC-clean microphone

Local signal checks never require a network connection. Passing ``--transcribe``
uses OpenAI's audio transcription endpoint; the API key is read from
``OPENAI_API_KEY`` or fetched from Doppler without printing it.
"""

from __future__ import annotations

import argparse
import difflib
import html
import json
import math
import mimetypes
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import numpy as np
from scipy import ndimage

import aec_lab


EPSILON = 1e-12
FRAME_SECONDS = 0.020
QUIET_DBFS = -58.0


@dataclass
class ChannelAssessment:
    dbfs: float
    peak_dbfs: float
    dc_offset: float
    clipping_percent: float
    exact_zero_percent: float
    zero_frame_percent: float
    active_percent: float
    first_active_seconds: float | None
    last_active_seconds: float | None
    activity_intervals: list[dict[str, float]]


def _db(value: float) -> float:
    return 20.0 * math.log10(max(abs(value), EPSILON))


def _rms(values: np.ndarray) -> float:
    if values.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(values, dtype=np.float64))))


def _frame_values(
    values: np.ndarray, sample_rate: int
) -> tuple[np.ndarray, np.ndarray, int]:
    frame_size = max(1, round(sample_rate * FRAME_SECONDS))
    frame_count = math.ceil(len(values) / frame_size)
    padded = np.pad(values, (0, frame_count * frame_size - len(values)))
    frames = padded.reshape((frame_count, frame_size))
    frame_rms = np.sqrt(np.mean(np.square(frames, dtype=np.float64), axis=1))
    return frames, frame_rms, frame_size


def _activity(
    values: np.ndarray, sample_rate: int
) -> tuple[np.ndarray, list[dict[str, float]], int]:
    _, frame_rms, frame_size = _frame_values(values, sample_rate)
    peak = float(np.max(frame_rms, initial=0.0))
    threshold = max(
        10.0 ** (QUIET_DBFS / 20.0),
        peak * 10.0 ** (-30.0 / 20.0),
    )
    active = frame_rms >= threshold
    if active.any():
        active = ndimage.binary_closing(active, structure=np.ones(3))

    intervals: list[dict[str, float]] = []
    starts = np.flatnonzero(active & ~np.r_[False, active[:-1]])
    ends = np.flatnonzero(active & ~np.r_[active[1:], False]) + 1
    for start, end in zip(starts, ends, strict=True):
        start_seconds = start * frame_size / sample_rate
        end_seconds = min(len(values), end * frame_size) / sample_rate
        intervals.append(
            {
                "start_seconds": round(start_seconds, 6),
                "end_seconds": round(end_seconds, 6),
                "duration_seconds": round(end_seconds - start_seconds, 6),
            }
        )
    return active, intervals, frame_size


def _assess_channel(values: np.ndarray, sample_rate: int) -> ChannelAssessment:
    frames, _, frame_size = _frame_values(values, sample_rate)
    active, intervals, _ = _activity(values, sample_rate)
    first = intervals[0]["start_seconds"] if intervals else None
    last = intervals[-1]["end_seconds"] if intervals else None
    return ChannelAssessment(
        dbfs=_db(_rms(values)),
        peak_dbfs=_db(float(np.max(np.abs(values), initial=0.0))),
        dc_offset=float(np.mean(values)) if values.size else 0.0,
        clipping_percent=float(np.mean(np.abs(values) >= 32760.0 / 32768.0) * 100.0),
        exact_zero_percent=float(np.mean(values == 0.0) * 100.0),
        zero_frame_percent=float(np.mean(np.all(frames == 0.0, axis=1)) * 100.0),
        active_percent=float(np.mean(active) * 100.0),
        first_active_seconds=first,
        last_active_seconds=last,
        activity_intervals=intervals,
    )


def _echo_metrics(capture: aec_lab.Capture) -> dict[str, float | None]:
    reference_active = aec_lab._reference_activity(
        capture.reference, capture.sample_rate
    )
    reference_active = aec_lab._skip_adaptation(
        reference_active, capture.sample_rate, 0.75
    )
    if not reference_active.any():
        return {
            "raw_reference_lag_ms": None,
            "raw_to_clean_power_reduction_db": None,
            "reference_projection_reduction_db": None,
            "spectral_reference_reduction_db": None,
            "raw_clean_similarity": None,
        }

    raw = capture.raw[reference_active]
    reference = capture.reference[reference_active]
    clean = capture.clean[reference_active]
    lag = aec_lab.estimate_reference_lag(
        reference, raw, capture.sample_rate
    )
    aligned = aec_lab._delay_signal(reference, lag)
    raw_projection = aec_lab._reference_projection_gain(aligned, raw)
    clean_projection = aec_lab._reference_projection_gain(aligned, clean)
    raw_transfer = aec_lab._reference_transfer_gain(
        aligned, raw, capture.sample_rate
    )
    clean_transfer = aec_lab._reference_transfer_gain(
        aligned, clean, capture.sample_rate
    )
    return {
        "raw_reference_lag_ms": lag * 1000.0 / capture.sample_rate,
        "raw_to_clean_power_reduction_db": aec_lab._power_db(
            float(np.mean(np.square(raw))),
            float(np.mean(np.square(clean))),
        ),
        "reference_projection_reduction_db": aec_lab._amplitude_db(
            raw_projection, clean_projection
        ),
        "spectral_reference_reduction_db": aec_lab._amplitude_db(
            raw_transfer, clean_transfer
        ),
        "raw_clean_similarity": aec_lab._similarity(raw, clean),
    }


def _response_latency_candidate(
    clean: ChannelAssessment,
    reference: ChannelAssessment,
) -> dict[str, float] | None:
    reference_start = reference.first_active_seconds
    if reference_start is None or clean.dbfs < -48.0:
        return None
    earlier_clean = [
        interval
        for interval in clean.activity_intervals
        if interval["end_seconds"] <= reference_start
        and interval["duration_seconds"] >= 0.20
    ]
    if not earlier_clean:
        return None
    speech_end = earlier_clean[-1]["end_seconds"]
    return {
        "input_activity_end_seconds": speech_end,
        "speaker_reference_start_seconds": reference_start,
        "candidate_response_latency_ms": (reference_start - speech_end) * 1000.0,
    }


def _warnings(
    channels: dict[str, ChannelAssessment],
    echo: dict[str, float | None],
) -> list[str]:
    warnings: list[str] = []
    for name, channel in channels.items():
        if channel.clipping_percent > 0.1:
            warnings.append(
                f"{name} clips {channel.clipping_percent:.3f}% of samples"
            )
        if abs(channel.dc_offset) > 0.02:
            warnings.append(
                f"{name} has a large DC offset ({channel.dc_offset:+.4f})"
            )
        if name != "speaker_reference" and channel.zero_frame_percent > 2.0:
            warnings.append(
                f"{name} contains {channel.zero_frame_percent:.2f}% exact-zero frames"
            )
    if (
        channels["raw_mic"].dbfs < QUIET_DBFS
        and channels["aec_clean"].dbfs < QUIET_DBFS
    ):
        warnings.append("both microphone channels are effectively silent")
    if (
        channels["speaker_reference"].dbfs > -48.0
        and echo["raw_reference_lag_ms"] is None
    ):
        warnings.append("speaker reference is active but no acoustic lag was measurable")
    return warnings


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


def _api_key(project: str, config: str) -> str:
    value = os.environ.get("OPENAI_API_KEY")
    if value:
        return value
    if not shutil_which("doppler"):
        raise RuntimeError(
            "OPENAI_API_KEY is unset and the Doppler CLI is unavailable"
        )
    return _doppler_secret(project, config, "OPENAI_API_KEY")


def shutil_which(command: str) -> str | None:
    paths = os.environ.get("PATH", "").split(os.pathsep)
    for directory in paths:
        candidate = Path(directory) / command
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def _multipart(
    fields: list[tuple[str, str]], file_path: Path
) -> tuple[bytes, str]:
    boundary = f"stackchan-{uuid.uuid4().hex}"
    body = bytearray()

    def add(value: bytes) -> None:
        body.extend(value)
        body.extend(b"\r\n")

    for name, value in fields:
        add(f"--{boundary}".encode())
        add(
            (
                f'Content-Disposition: form-data; name="{name}"'
            ).encode()
        )
        add(b"")
        add(value.encode())

    mime = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
    add(f"--{boundary}".encode())
    add(
        (
            'Content-Disposition: form-data; name="file"; '
            f'filename="{file_path.name}"'
        ).encode()
    )
    add(f"Content-Type: {mime}".encode())
    add(b"")
    body.extend(file_path.read_bytes())
    body.extend(b"\r\n")
    body.extend(f"--{boundary}--\r\n".encode())
    return bytes(body), boundary


def _transcribe(
    wav_path: Path,
    model: str,
    key: str,
) -> dict[str, Any]:
    fields = [("model", model)]
    if model == "whisper-1":
        fields.extend(
            [
                ("response_format", "verbose_json"),
                ("timestamp_granularities[]", "word"),
            ]
        )
    else:
        fields.extend(
            [
                ("response_format", "json"),
                ("include[]", "logprobs"),
            ]
        )
    body, boundary = _multipart(fields, wav_path)
    request = urllib.request.Request(
        "https://api.openai.com/v1/audio/transcriptions",
        data=body,
        method="POST",
        headers={
            "Authorization": f"Bearer {key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "User-Agent": "stackchan-audio-assessment/1",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=90.0) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read(2048).decode("utf-8", errors="replace")
        raise RuntimeError(
            f"OpenAI transcription failed with HTTP {error.code}: {detail}"
        ) from error


def _transcript_summary(response: dict[str, Any]) -> dict[str, Any]:
    logprobs = response.get("logprobs") or []
    numeric_logprobs = [
        item.get("logprob")
        for item in logprobs
        if isinstance(item, dict) and isinstance(item.get("logprob"), (int, float))
    ]
    average_logprob = (
        float(np.mean(numeric_logprobs)) if numeric_logprobs else None
    )
    summary: dict[str, Any] = {
        "text": str(response.get("text", "")).strip(),
        "average_token_logprob": average_logprob,
        "mean_token_probability": (
            math.exp(average_logprob) if average_logprob is not None else None
        ),
        "reliable_for_semantic_comparison": (
            average_logprob is None or average_logprob >= -1.5
        ),
    }
    if isinstance(response.get("words"), list):
        summary["words"] = response["words"]
    if isinstance(response.get("duration"), (int, float)):
        summary["duration_seconds"] = response["duration"]
    if isinstance(response.get("language"), str):
        summary["language"] = response["language"]
    if isinstance(response.get("usage"), dict):
        summary["usage"] = response["usage"]
    return summary


def _normalise_text(value: str) -> str:
    return " ".join(re.findall(r"[a-z0-9']+", value.lower()))


def _text_similarity(left: str, right: str) -> float | None:
    left = _normalise_text(left)
    right = _normalise_text(right)
    if not left or not right:
        return None
    return difflib.SequenceMatcher(None, left, right).ratio()


def _semantic_metrics(
    transcripts: dict[str, dict[str, Any]],
    expected_far: str | None,
    expected_near: str | None,
) -> dict[str, float | None]:
    texts = {
        name: (
            str(summary.get("text", ""))
            if summary.get("reliable_for_semantic_comparison", True)
            else ""
        )
        for name, summary in transcripts.items()
    }
    metrics: dict[str, float | None] = {
        "reference_vs_raw_similarity": _text_similarity(
            texts.get("speaker_reference", ""),
            texts.get("raw_mic", ""),
        ),
        "reference_vs_clean_similarity": _text_similarity(
            texts.get("speaker_reference", ""),
            texts.get("aec_clean", ""),
        ),
    }
    if expected_far:
        for name, text in texts.items():
            metrics[f"expected_far_vs_{name}_similarity"] = _text_similarity(
                expected_far, text
            )
    if expected_near:
        for name, text in texts.items():
            metrics[f"expected_near_vs_{name}_similarity"] = _text_similarity(
                expected_near, text
            )
    return metrics


def _write_html(report: dict[str, Any], output: Path) -> None:
    channels = report["channels"]
    rows = []
    for name, values in channels.items():
        rows.append(
            "<tr>"
            f"<th>{html.escape(name)}</th>"
            f"<td>{values['dbfs']:.1f}</td>"
            f"<td>{values['peak_dbfs']:.1f}</td>"
            f"<td>{values['clipping_percent']:.3f}</td>"
            f"<td>{values['zero_frame_percent']:.2f}</td>"
            "</tr>"
        )
    transcript_html = ""
    for name, summary in report.get("transcripts", {}).items():
        transcript_html += (
            f"<h3>{html.escape(name)}</h3>"
            f"<p>{html.escape(str(summary.get('text', '')))}</p>"
        )
    warning_items = "".join(
        f"<li>{html.escape(item)}</li>" for item in report["warnings"]
    ) or "<li>No local signal-integrity warnings.</li>"
    output.joinpath("index.html").write_text(
        f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>StackChan audio assessment</title>
  <style>
    body {{ font: 16px/1.45 system-ui, sans-serif; max-width: 1100px;
            margin: 2rem auto; padding: 0 1rem; color: #17202a; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border-bottom: 1px solid #d8dee4; padding: .5rem; text-align: left; }}
    img {{ display: block; max-width: 100%; margin: 1rem 0; }}
    audio {{ width: 100%; }}
  </style>
</head>
<body>
  <h1>StackChan audio assessment</h1>
  <ul>{warning_items}</ul>
  <table>
    <thead><tr><th>Channel</th><th>RMS dBFS</th><th>Peak dBFS</th>
      <th>Clipping %</th><th>Zero frames %</th></tr></thead>
    <tbody>{''.join(rows)}</tbody>
  </table>
  <h2>Listen</h2>
  <p>Raw microphone</p><audio controls src="raw-mic.wav"></audio>
  <p>Exact speaker reference</p><audio controls src="speaker-reference.wav"></audio>
  <p>AEC clean</p><audio controls src="aec-clean.wav"></audio>
  <h2>Transcripts</h2>
  {transcript_html or '<p>Transcription was not requested.</p>'}
  <h2>Waveforms</h2><img src="waveforms.png" alt="Aligned waveforms">
  <h2>Spectrograms</h2><img src="spectrograms.png" alt="Spectrograms">
  <p>All metrics, activity intervals, alignment, and latency candidates are in
     <a href="assessment.json">assessment.json</a>.</p>
</body>
</html>
""",
        encoding="utf-8",
    )


def assess(
    capture_path: Path,
    output: Path,
    transcribe: bool,
    model: str,
    doppler_project: str,
    doppler_config: str,
    expected_far: str | None,
    expected_near: str | None,
) -> dict[str, Any]:
    capture = aec_lab.read_capture(capture_path)
    output.mkdir(parents=True, exist_ok=True)
    aec_lab.write_mono_wav(
        output / "raw-mic.wav", capture.sample_rate, capture.raw
    )
    aec_lab.write_mono_wav(
        output / "speaker-reference.wav",
        capture.sample_rate,
        capture.reference,
    )
    aec_lab.write_mono_wav(
        output / "aec-clean.wav", capture.sample_rate, capture.clean
    )
    aec_lab._write_plots(capture, output)

    channel_values = {
        "raw_mic": capture.raw,
        "speaker_reference": capture.reference,
        "aec_clean": capture.clean,
    }
    channels = {
        name: _assess_channel(values, capture.sample_rate)
        for name, values in channel_values.items()
    }
    echo = _echo_metrics(capture)
    report: dict[str, Any] = {
        "capture": str(capture_path),
        "sample_rate_hz": capture.sample_rate,
        "duration_seconds": capture.duration_seconds,
        "channels": {
            name: asdict(channel) for name, channel in channels.items()
        },
        "echo": echo,
        "latency": {
            "clean_input_to_speaker_response_candidate": (
                _response_latency_candidate(
                    channels["aec_clean"],
                    channels["speaker_reference"],
                )
            )
        },
        "warnings": _warnings(channels, echo),
    }

    if transcribe:
        key = _api_key(doppler_project, doppler_config)
        transcript_dir = output / "transcripts"
        transcript_dir.mkdir(exist_ok=True)
        transcripts: dict[str, dict[str, Any]] = {}
        stem_names = {
            "raw_mic": "raw-mic.wav",
            "speaker_reference": "speaker-reference.wav",
            "aec_clean": "aec-clean.wav",
        }
        for name, filename in stem_names.items():
            if channels[name].dbfs < QUIET_DBFS:
                transcripts[name] = {
                    "text": "",
                    "skipped": f"channel quieter than {QUIET_DBFS:.1f} dBFS",
                }
                continue
            response = _transcribe(output / filename, model, key)
            transcript_dir.joinpath(f"{name}.json").write_text(
                json.dumps(response, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            transcripts[name] = _transcript_summary(response)
        report["transcription_model"] = model
        report["transcripts"] = transcripts
        report["semantic"] = _semantic_metrics(
            transcripts, expected_far, expected_near
        )

    output.joinpath("assessment.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    _write_html(report, output)
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--transcribe",
        action="store_true",
        help="transcribe non-quiet stems with the OpenAI API",
    )
    parser.add_argument(
        "--transcription-model",
        default="gpt-4o-transcribe",
        choices=("gpt-4o-transcribe", "gpt-4o-mini-transcribe", "whisper-1"),
        help=(
            "use whisper-1 when word timestamps are required; "
            "gpt-4o-transcribe is the default for semantic checks"
        ),
    )
    parser.add_argument(
        "--doppler-project",
        default=os.environ.get("STACKCHAN_DOPPLER_PROJECT", "os"),
    )
    parser.add_argument(
        "--doppler-config",
        default=os.environ.get("STACKCHAN_DOPPLER_CONFIG", "dev_jonas"),
    )
    parser.add_argument("--expected-far")
    parser.add_argument("--expected-near")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        report = assess(
            args.capture,
            args.output,
            args.transcribe,
            args.transcription_model,
            args.doppler_project,
            args.doppler_config,
            args.expected_far,
            args.expected_near,
        )
    except Exception as error:
        print(f"Audio assessment failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
