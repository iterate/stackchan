#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "websockets>=15,<17",
# ]
# ///
"""Render a named PCM-face matrix and a narrated comparison reel.

The same captured Grok Realtime 16 kHz PCM and ordered event stream is replayed
through every selected face algorithm, making the videos fair A/B comparisons.
Use ``--capture-source fake`` for deterministic, API-free local testing.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import platform
import select
import shutil
import struct
import subprocess
import sys
import wave
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from itertools import pairwise
from pathlib import Path
from typing import Any

import face_simulator

TOOLS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TOOLS_DIR.parent
VIDEO_PROJECT = TOOLS_DIR / "face-video"
LOCAL_VIDEO_DIR = EXPERIMENT_DIR / "local" / "grok-face-videos"
SAMPLE_RATE = 16_000
WINDOW_SAMPLES = SAMPLE_RATE // 100
WINDOW_BYTES = WINDOW_SAMPLES * 2
WAVEFORM_SAMPLES_PER_BIN = 40
VIDEO_FPS = 30
RELEASE_WINDOWS = 20
VISEME_MODEL_BYTES = (
    EXPERIMENT_DIR
    / "firmware-ws"
    / "main"
    / "assets"
    / "head_audio_model_en_mixed.bin"
).stat().st_size


@dataclass(frozen=True)
class Scenario:
    voice: str
    title: str
    test_case: str
    prompt: str
    style: dict[str, str]


@dataclass(frozen=True)
class AlgorithmProfile:
    key: str
    algorithm: str
    label: str
    profile: str
    properties_slug: str
    config: dict[str, int | bool]
    property_summary: str
    tradeoff: str
    narrative: str
    bullets: tuple[str, ...]
    accent: str


SCENARIOS = {
    "leo": Scenario(
        voice="leo",
        title="Grok Leo · measured older British delivery",
        test_case="measured-british-vowels",
        prompt=(
            "Say exactly this sentence, in a measured older British delivery: "
            '"Good evening. These real sixteen kilohertz Grok PCM frames are '
            "driving StackChan's face.\""
        ),
        style={
            "name": "silver",
            "hairColor": "#8797a5",
            "skinColor": "#efc8a8",
            "skinShadow": "#c98f76",
            "eyeColor": "#315b6f",
            "lipColor": "#a9435d",
            "tongueColor": "#d9687c",
            "accentColor": "#60d394",
            "backgroundNear": "#17384b",
            "backgroundFar": "#071521",
        },
    ),
    "rex": Scenario(
        voice="rex",
        title="Grok Rex · crisp plosives",
        test_case="crisp-plosive-packets",
        prompt=(
            "Say exactly this sentence, crisply and with energy: "
            '"Peter Piper packed bright plosive packets while StackChan watched '
            'every waveform."'
        ),
        style={
            "name": "copper",
            "hairColor": "#8b4d35",
            "skinColor": "#d9a47f",
            "skinShadow": "#a96855",
            "eyeColor": "#315b45",
            "lipColor": "#8f3048",
            "tongueColor": "#d05b72",
            "accentColor": "#ffb86b",
            "backgroundNear": "#3b2b2b",
            "backgroundFar": "#120e13",
        },
    ),
    "eve": Scenario(
        voice="eve",
        title="Grok Eve · soft sibilants",
        test_case="soft-sibilants-vowels",
        prompt=(
            "Say exactly this sentence, calmly and naturally: "
            '"Soft sibilants narrow the mouth while louder vowels open it wide."'
        ),
        style={
            "name": "ink",
            "hairColor": "#25273c",
            "skinColor": "#e2ac91",
            "skinShadow": "#b97169",
            "eyeColor": "#51478b",
            "lipColor": "#a63f68",
            "tongueColor": "#dd6d8d",
            "accentColor": "#c4b5fd",
            "backgroundNear": "#292747",
            "backgroundFar": "#0b0b18",
        },
    ),
}


PROFILES = {
    "envelope-fast": AlgorithmProfile(
        key="envelope-fast",
        algorithm="envelope",
        label="Amplitude envelope",
        profile="fast / lively",
        properties_slug="window10ms-attack75-release25",
        config={
            "speech_floor": 256,
            "mouth_dynamic_range": 4600,
            "attack_percent": 75,
            "release_percent": 25,
        },
        property_summary="10 ms window · attack 75% · release 25% · 60 B state",
        tradeoff=(
            "Minimal memory and immediate response, but closely follows level "
            "changes and can look jumpy."
        ),
        narrative=(
            "The baseline only measures amplitude and zero crossings. It is "
            "tiny and fast, but knows nothing about actual speech sounds."
        ),
        bullets=(
            "60 bytes of working state; no model and no FFT",
            "Fastest visual onset",
            "Most likely to twitch on jagged PCM levels",
        ),
        accent="#60d394",
    ),
    "envelope-smooth": AlgorithmProfile(
        key="envelope-smooth",
        algorithm="envelope",
        label="Amplitude envelope",
        profile="debounced / smooth",
        properties_slug="window10ms-attack38-release10",
        config={
            "speech_floor": 256,
            "mouth_dynamic_range": 4600,
            "attack_percent": 38,
            "release_percent": 10,
        },
        property_summary="10 ms window · attack 38% · release 10% · 60 B state",
        tradeoff=(
            "Calmer motion and less chatter, while still lacking phoneme or "
            "viseme identity."
        ),
        narrative=(
            "A slower attack and release tame the jumpiness without adding any "
            "memory. The cost is softer consonant timing."
        ),
        bullets=(
            "Same 60-byte implementation",
            "Simple smoothing removes much of the jitter",
            "Every sound is still essentially the same mouth",
        ),
        accent="#58a6ff",
    ),
    "viseme-responsive": AlgorithmProfile(
        key="viseme-responsive",
        algorithm="viseme",
        label="Acoustic viseme",
        profile="responsive",
        properties_slug=(
            "mfcc512-hop256-vote3-attack35-release70-shape45"
        ),
        config={
            "speaker_mean_hz": 150,
            "vad_open_level": 36,
            "vad_close_level": 18,
            "mouth_level_range": 3000,
            "attack_ms": 35,
            "release_ms": 70,
            "shape_ms": 45,
            "vad_release_hops": 4,
            "vote_window": 3,
            "prime_vote_on_speech": True,
        },
        property_summary=(
            "MFCC 512/256 · vote 3 · attack 35 ms · release 70 ms · "
            "shape 45 ms"
        ),
        tradeoff=(
            "Recognisable consonant/vowel shapes with low latency; the shorter "
            "vote and shape windows permit more motion."
        ),
        narrative=(
            "A 12-coefficient MFCC front end classifies 39 acoustic prototypes "
            "into 15 Oculus-style visemes."
        ),
        bullets=(
            "6.3 KiB working state + 14 KiB read-only model",
            "16 ms acoustic hops",
            "Distinct round, pressed, wide, teeth and open dimensions",
        ),
        accent="#ffd166",
    ),
    "viseme-balanced": AlgorithmProfile(
        key="viseme-balanced",
        algorithm="viseme",
        label="Acoustic viseme",
        profile="balanced / smooth",
        properties_slug=(
            "mfcc512-hop256-vote5-attack55-release110-shape90"
        ),
        config={
            "speaker_mean_hz": 150,
            "vad_open_level": 36,
            "vad_close_level": 18,
            "mouth_level_range": 3000,
            "attack_ms": 55,
            "release_ms": 110,
            "shape_ms": 90,
            "vad_release_hops": 4,
            "vote_window": 5,
            "prime_vote_on_speech": True,
        },
        property_summary=(
            "MFCC 512/256 · vote 5 · attack 55 ms · release 110 ms · "
            "shape 90 ms"
        ),
        tradeoff=(
            "The recommended profile: stable, expressive shapes at the cost of "
            "a small amount of extra visual lag."
        ),
        narrative=(
            "Longer shape smoothing and majority voting keep the richer mouth "
            "from becoming nervous or over-articulated."
        ),
        bullets=(
            "Same bounded memory and CPU as responsive visemes",
            "Best stability across the three voices",
            "Slightly rounds off the fastest plosives",
        ),
        accent="#f0a6ca",
    ),
}


def _run(
    command: list[str],
    *,
    cwd: Path,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        capture_output=capture_output,
        check=False,
    )
    if result.returncode != 0:
        if capture_output:
            if result.stdout:
                print(result.stdout, file=sys.stderr, end="")
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError(
            f"command failed ({result.returncode}): " + " ".join(command)
        )
    return result


def _capture_paths(assets_dir: Path, voice: str) -> tuple[Path, ...]:
    wav = assets_dir / f"{voice}.wav"
    return (
        wav,
        wav.with_suffix(".json"),
        wav.with_suffix(".frames.jsonl"),
        wav.with_suffix(".events.jsonl"),
    )


def _run_probe(
    scenario: Scenario,
    assets_dir: Path,
    *,
    url: str | None = None,
) -> dict[str, Any]:
    wav_path = assets_dir / f"{scenario.voice}.wav"
    command = [
        sys.executable,
        str(TOOLS_DIR / "realtime_probe.py"),
        "--provider",
        "xai",
        "--voice",
        scenario.voice,
        "--prompt",
        scenario.prompt,
        "--output",
        str(wav_path),
        "--timeout",
        "60",
    ]
    if url is not None:
        command.extend(["--url", url, "--no-auth"])
    _run(command, cwd=EXPERIMENT_DIR, capture_output=True)
    return json.loads(wav_path.with_suffix(".json").read_text(encoding="utf-8"))


def _capture(
    scenario: Scenario,
    assets_dir: Path,
    capture_source: str,
) -> dict[str, Any]:
    print(
        f"Capturing {capture_source} Grok stream voice={scenario.voice}...",
        flush=True,
    )
    if capture_source == "real":
        return _run_probe(scenario, assets_dir)

    process = subprocess.Popen(
        [
            sys.executable,
            str(TOOLS_DIR / "fake_grok_server.py"),
            "--port",
            "0",
            "--once",
            "--time-scale",
            "0",
            "--chunk-ms",
            "7,23,11,47,19,31,13,41",
        ],
        cwd=EXPERIMENT_DIR,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdout is not None
    try:
        readable, _, _ = select.select([process.stdout], [], [], 15)
        if not readable:
            raise TimeoutError("fake Grok server did not start within 15s")
        ready = process.stdout.readline()
        if not ready:
            assert process.stderr is not None
            raise RuntimeError(
                "fake Grok server exited early: " + process.stderr.read()
            )
        result = _run_probe(
            scenario,
            assets_dir,
            url=str(json.loads(ready)["url"]),
        )
        process.wait(timeout=10)
        return result
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


def _copy_reusable_captures(
    scenarios: list[Scenario],
    source: Path,
    assets_dir: Path,
) -> None:
    for scenario in scenarios:
        for source_path in _capture_paths(source, scenario.voice):
            if not source_path.is_file():
                raise FileNotFoundError(
                    f"missing reusable capture artifact: {source_path}"
                )
            shutil.copy2(source_path, assets_dir / source_path.name)


def _load_capture(
    scenario: Scenario,
    assets_dir: Path,
    *,
    require_real: bool,
) -> tuple[
    bytes,
    dict[str, Any],
    list[dict[str, Any]],
    list[dict[str, Any]],
]:
    wav_path, summary_path, frames_path, events_path = _capture_paths(
        assets_dir, scenario.voice
    )
    for required in (wav_path, summary_path, frames_path, events_path):
        if not required.is_file():
            raise FileNotFoundError(f"missing capture artifact: {required}")

    with wave.open(str(wav_path), "rb") as source:
        if (
            source.getnchannels() != 1
            or source.getsampwidth() != 2
            or source.getframerate() != SAMPLE_RATE
        ):
            raise ValueError(f"{wav_path} is not mono PCM16 at {SAMPLE_RATE} Hz")
        pcm = source.readframes(source.getnframes())
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    packets = [
        json.loads(line)
        for line in frames_path.read_text(encoding="utf-8").splitlines()
    ]
    events = [
        json.loads(line)
        for line in events_path.read_text(encoding="utf-8").splitlines()
    ]

    if summary["provider"] != "xai":
        raise RuntimeError("capture does not use the xAI protocol profile")
    if require_real and summary["endpoint"] != "provider_default":
        raise RuntimeError("capture did not come from the real xAI endpoint")
    if summary["transport"] != "binary" or summary["sample_rate_hz"] != SAMPLE_RATE:
        raise RuntimeError("capture did not use binary 16 kHz PCM")
    if not packets:
        raise RuntimeError("capture contains no Grok binary audio frames")
    if packets[0]["sample_start"] != 0 or packets[-1]["sample_end"] * 2 != len(pcm):
        raise RuntimeError("Grok frame manifest does not span captured PCM")
    if any(
        current["sample_end"] != following["sample_start"]
        for current, following in pairwise(packets)
    ):
        raise RuntimeError("Grok frame manifest has a gap or overlap")
    if [event["index"] for event in events] != list(range(len(events))):
        raise RuntimeError("ordered Grok event manifest has invalid indices")
    markers = [event["dispatch_playout_samples"] for event in events]
    if markers != sorted(markers) or any(
        marker < 0 or marker > len(pcm) // 2 for marker in markers
    ):
        raise RuntimeError("Grok event sample markers are not monotonic")
    if not any(
        event["protocol_event"] == "response.output_audio_transcript.delta"
        for event in events
    ):
        raise RuntimeError("capture contains no assistant transcript delta")
    return pcm, summary, packets, events


class PcmAnalyser:
    def __init__(self) -> None:
        self.last_sign = 0

    def analyse(self, pcm: bytes) -> dict[str, int]:
        samples = [sample for (sample,) in struct.iter_unpack("<h", pcm)]
        if not samples:
            return {"mean_abs": 0, "peak": 0, "rms": 0, "zero_crossings": 0}
        total_abs = 0
        total_square = 0
        peak = 0
        zero_crossings = 0
        for sample in samples:
            magnitude = abs(sample)
            total_abs += magnitude
            total_square += sample * sample
            peak = max(peak, magnitude)
            sign = 1 if sample > 0 else (-1 if sample < 0 else 0)
            if sign:
                if self.last_sign and sign != self.last_sign:
                    zero_crossings += 1
                self.last_sign = sign
        return {
            "mean_abs": total_abs // len(samples),
            "peak": peak,
            "rms": math.isqrt(total_square // len(samples)),
            "zero_crossings": zero_crossings,
        }


EVENT_TYPES = {
    "input_audio_buffer.speech_started": 0,
    "input_audio_buffer.speech_stopped": 1,
    "response.created": 2,
    "response.output_audio_transcript.delta": 3,
    "response.output_audio_transcript.done": 4,
    "response.output_audio.done": 5,
    "response.done": 6,
}


def _face_trace(
    engine: face_simulator.FaceEngine,
    pcm: bytes,
    events: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], bytes]:
    analyser = PcmAnalyser()
    padded = pcm + bytes((-len(pcm)) % WINDOW_BYTES)
    playout_pcm = padded + bytes(WINDOW_BYTES * RELEASE_WINDOWS)
    event_index = 0

    def dispatch(up_to_sample: int) -> None:
        nonlocal event_index
        while (
            event_index < len(events)
            and events[event_index]["dispatch_playout_samples"]
            <= up_to_sample
        ):
            event = events[event_index]
            event_index += 1
            mapped = EVENT_TYPES.get(event["protocol_event"])
            if mapped is None:
                continue
            engine.push_event(
                mapped,
                received_audio_samples=event["received_audio_samples"],
                dispatch_playout_samples=event["dispatch_playout_samples"],
                text=str(event.get("text", "")),
                cumulative=bool(event.get("cumulative", False)),
            )

    dispatch(0)
    trace = [
        {
            **face_simulator._frame(engine, len(playout_pcm) // 2),
            "analysis": {
                "mean_abs": 0,
                "peak": 0,
                "rms": 0,
                "zero_crossings": 0,
            },
        }
    ]
    for offset in range(0, len(playout_pcm), WINDOW_BYTES):
        chunk = playout_pcm[offset : offset + WINDOW_BYTES]
        engine.push(chunk)
        dispatch((offset + len(chunk)) // 2)
        remaining = max(0, len(playout_pcm) - offset - WINDOW_BYTES) // 2
        trace.append(
            {
                **face_simulator._frame(engine, remaining),
                "analysis": analyser.analyse(chunk),
            }
        )
    return trace, playout_pcm


def _waveform(pcm: bytes) -> list[dict[str, int]]:
    samples = [sample for (sample,) in struct.iter_unpack("<h", pcm)]
    return [
        {
            "min": min(samples[offset : offset + WAVEFORM_SAMPLES_PER_BIN], default=0),
            "max": max(samples[offset : offset + WAVEFORM_SAMPLES_PER_BIN], default=0),
        }
        for offset in range(0, len(samples), WAVEFORM_SAMPLES_PER_BIN)
    ]


def _props(
    scenario: Scenario,
    profile: AlgorithmProfile,
    pcm: bytes,
    summary: dict[str, Any],
    packets: list[dict[str, Any]],
    events: list[dict[str, Any]],
    trace: list[dict[str, Any]],
    playout_pcm: bytes,
    state_bytes: int,
) -> dict[str, Any]:
    duration_seconds = (len(trace) - 1) / 100
    return {
        "audioFile": f"{scenario.voice}.wav",
        "sourceLabel": (
            "REAL xAI GROK REALTIME"
            if summary["endpoint"] == "provider_default"
            else "DETERMINISTIC GROK-COMPATIBLE REPLAY"
        ),
        "title": scenario.title,
        "testCase": scenario.test_case,
        "voice": scenario.voice,
        "transcript": summary["transcript"].strip(),
        "prompt": scenario.prompt,
        "model": summary["model"],
        "sampleRate": SAMPLE_RATE,
        "fps": VIDEO_FPS,
        "durationInFrames": math.ceil(duration_seconds * VIDEO_FPS),
        "pcmSha256": hashlib.sha256(pcm).hexdigest(),
        "trace": trace,
        "waveform": _waveform(playout_pcm),
        "waveformSamplesPerBin": WAVEFORM_SAMPLES_PER_BIN,
        "packets": packets,
        "streamEvents": events,
        "algorithm": {
            "id": profile.algorithm,
            "label": profile.label,
            "profile": profile.profile,
            "properties": profile.config,
            "propertySummary": profile.property_summary,
            "tradeoff": profile.tradeoff,
            "stateBytes": state_bytes,
            "modelBytes": (
                VISEME_MODEL_BYTES if profile.algorithm == "viseme" else 0
            ),
        },
        "style": scenario.style,
    }


def _video_filename(
    scenario: Scenario,
    profile: AlgorithmProfile,
) -> str:
    return (
        f"algorithm-{profile.algorithm}_properties-{profile.properties_slug}_"
        f"case-{scenario.test_case}_voice-{scenario.voice}_"
        f"style-{scenario.style['name']}.mp4"
    )


def _render(
    composition: str,
    output: Path,
    assets_dir: Path,
    props_path: Path,
    *,
    concurrency: int,
) -> None:
    print(f"Rendering {output.name}...", flush=True)
    _run(
        [
            str(VIDEO_PROJECT / "node_modules" / ".bin" / "remotion"),
            "render",
            "src/index.ts",
            composition,
            str(output),
            "--props",
            str(props_path),
            "--public-dir",
            str(assets_dir),
            "--codec",
            "h264",
            "--audio-codec",
            "aac",
            "--pixel-format",
            "yuv420p",
            "--crf",
            "15",
            "--concurrency",
            str(concurrency),
        ],
        cwd=VIDEO_PROJECT,
        capture_output=True,
    )


def _probe_video(path: Path) -> dict[str, Any]:
    result = _run(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            (
                "format=duration,size:stream=index,codec_type,codec_name,"
                "width,height,r_frame_rate,sample_rate,channels"
            ),
            "-of",
            "json",
            str(path),
        ],
        cwd=EXPERIMENT_DIR,
        capture_output=True,
    )
    return json.loads(result.stdout)


def _parse_selection(
    value: str,
    available: dict[str, Any],
    label: str,
) -> list[str]:
    selected = [item.strip().lower() for item in value.split(",") if item.strip()]
    unknown = [item for item in selected if item not in available]
    if unknown:
        raise ValueError(
            f"unknown {label}(s): {', '.join(unknown)}; choose from "
            + ", ".join(available)
        )
    if not selected:
        raise ValueError(f"at least one {label} is required")
    return selected


def _slide(
    *,
    duration: int,
    kicker: str,
    title: str,
    body: str,
    bullets: list[str] | tuple[str, ...],
    accent: str,
    footer: str | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "kind": "slide",
        "durationInFrames": duration,
        "kicker": kicker,
        "title": title,
        "body": body,
        "bullets": list(bullets),
        "accentColor": accent,
    }
    if footer:
        result["footer"] = footer
    return result


def _showcase_props(
    prepared: list[dict[str, Any]],
    profile_keys: list[str],
    *,
    capture_source: str,
) -> dict[str, Any]:
    source_name = (
        "real Grok Realtime" if capture_source == "real"
        else "deterministic Grok-compatible"
    )
    strategy_count = len(profile_keys)
    voice_count = len({item["scenario"].voice for item in prepared})
    clip_count = len(prepared)
    segments: list[dict[str, Any]] = [
        _slide(
            duration=105,
            kicker="STACKCHAN PCM FACE LAB",
            title=(
                f"One stream. {strategy_count} rendering "
                f"{'strategy' if strategy_count == 1 else 'strategies'}."
            ),
            body=(
                f"{clip_count} fair A/B clips: the same {voice_count} "
                f"{source_name} "
                f"{'utterance' if voice_count == 1 else 'utterances'} replayed "
                "through every selected lightweight face algorithm."
            ),
            bullets=(
                f"{source_name.title()} binary PCM at 16,000 Hz",
                "Transcript and lifecycle events aligned to speaker samples",
                "All animation is deterministic and device-free reproducible",
            ),
            accent="#60d394",
        ),
        _slide(
            duration=90,
            kicker="THE CLOCKING RULE",
            title="Network order is not mouth time.",
            body=(
                "Every Grok event is stamped with cumulative audio received, "
                "then dispatched only when that sample reaches speaker playout."
            ),
            bullets=(
                "Jitter and prebuffering cannot move captions ahead of playout",
                "User speech events never drive the assistant mouth",
                "The exact post-speaker PCM drives both sound and expression",
            ),
            accent="#c4b5fd",
        ),
    ]
    for profile_key in profile_keys:
        profile = PROFILES[profile_key]
        clips = [
            item for item in prepared if item["profile"].key == profile_key
        ]
        segments.append(
            _slide(
                duration=78,
                kicker=f"ALGORITHM · {profile.label.upper()}",
                title=profile.profile.title(),
                body=profile.narrative,
                bullets=profile.bullets,
                accent=profile.accent,
                footer=f"NEXT: {len(clips)} VOICE TESTS · IDENTICAL CAPTURE PER VOICE",
            )
        )
        for item in clips:
            scenario: Scenario = item["scenario"]
            segments.append(
                _slide(
                    duration=36,
                    kicker=f"TEST CASE · {scenario.voice.upper()}",
                    title=scenario.title,
                    body=(
                        f"{profile.property_summary}. Watch the mouth shape, "
                        "whole-face movement, waveform and sample-clocked text."
                    ),
                    bullets=(),
                    accent=scenario.style["accentColor"],
                )
            )
            segments.append(
                {
                    "kind": "video",
                    "durationInFrames": item["props"]["durationInFrames"],
                    "file": f"showcase/{item['output'].name}",
                }
            )
    segments.append(
        _slide(
            duration=120,
            kicker="RECOMMENDATION",
            title="Use balanced visemes; keep envelope as the fallback.",
            body=(
                "The acoustic classifier makes consonants and vowels legible. "
                "A five-frame vote and 90 ms shape smoother preserve that "
                "expressiveness without the nervous motion of the early demos."
            ),
            bullets=(
                "Primary: viseme-balanced",
                "Fallback: envelope-smooth when model memory is unavailable",
                "Next: validate perceptual timing and AEC together on hardware",
            ),
            accent="#f0a6ca",
        )
    )
    return {
        "title": "StackChan PCM face algorithm showcase",
        "fps": VIDEO_FPS,
        "durationInFrames": sum(
            segment["durationInFrames"] for segment in segments
        ),
        "segments": segments,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--voices",
        default="leo,rex,eve",
        help="comma-separated scenarios (default: leo,rex,eve)",
    )
    parser.add_argument(
        "--profiles",
        default=",".join(PROFILES),
        help="comma-separated algorithm profiles",
    )
    parser.add_argument(
        "--capture-source",
        choices=("real", "fake"),
        default="real",
        help="real xAI endpoint or deterministic local fake (default: real)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="artifact directory (default: timestamp under local/)",
    )
    parser.add_argument(
        "--reuse-captures",
        action="store_true",
        help="reuse complete capture artifacts already in output-dir/assets",
    )
    parser.add_argument(
        "--reuse-captures-from",
        type=Path,
        help="copy complete WAV/JSON/frame/event captures from this directory",
    )
    parser.add_argument(
        "--capture-only",
        action="store_true",
        help="capture and generate every deterministic trace without rendering",
    )
    parser.add_argument(
        "--skip-showcase",
        action="store_true",
        help="render individual videos only",
    )
    parser.add_argument(
        "--parallel-renders",
        type=int,
        default=3,
        help="simultaneous individual Remotion renders (default: 3)",
    )
    parser.add_argument(
        "--remotion-concurrency",
        type=int,
        default=2,
        help="Chromium tabs per render (default: 2)",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="open every individual MP4 and the showcase when complete",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    voice_keys = _parse_selection(args.voices, SCENARIOS, "voice")
    profile_keys = _parse_selection(args.profiles, PROFILES, "profile")
    scenarios = [SCENARIOS[key] for key in voice_keys]
    timestamp = datetime.now(UTC).strftime("%Y%m%d-%H%M%S")
    output_dir = (args.output_dir or LOCAL_VIDEO_DIR / timestamp).resolve()
    assets_dir = output_dir / "assets"
    build_dir = output_dir / "build"
    assets_dir.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    if args.reuse_captures_from is not None:
        _copy_reusable_captures(
            scenarios,
            args.reuse_captures_from.resolve(),
            assets_dir,
        )

    for scenario in scenarios:
        capture_files = _capture_paths(assets_dir, scenario.voice)
        if not args.reuse_captures or not all(
            path.is_file() for path in capture_files
        ):
            _capture(scenario, assets_dir, args.capture_source)

    library = face_simulator._build_face_library(build_dir)
    captures = {
        scenario.voice: _load_capture(
            scenario,
            assets_dir,
            require_real=args.capture_source == "real",
        )
        for scenario in scenarios
    }
    prepared: list[dict[str, Any]] = []
    for profile_key in profile_keys:
        profile = PROFILES[profile_key]
        for scenario in scenarios:
            pcm, capture, packets, events = captures[scenario.voice]
            with face_simulator.FaceEngine(
                library,
                algorithm=profile.algorithm,
                config=profile.config,
            ) as engine:
                trace, playout_pcm = _face_trace(engine, pcm, events)
                state_bytes = engine.algorithm_state_size
            props = _props(
                scenario,
                profile,
                pcm,
                capture,
                packets,
                events,
                trace,
                playout_pcm,
                state_bytes,
            )
            stem = _video_filename(scenario, profile)[:-4]
            props_path = build_dir / f"{stem}.props.json"
            props_path.write_text(
                json.dumps(props, separators=(",", ":"), sort_keys=True),
                encoding="utf-8",
            )
            prepared.append(
                {
                    "scenario": scenario,
                    "profile": profile,
                    "capture": capture,
                    "packets": packets,
                    "events": events,
                    "props": props,
                    "props_path": props_path,
                    "output": output_dir / f"{stem}.mp4",
                }
            )

    if not args.capture_only:
        workers = max(1, min(args.parallel_renders, len(prepared)))
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(
                    _render,
                    "FacePcmOverlay",
                    item["output"],
                    assets_dir,
                    item["props_path"],
                    concurrency=max(1, args.remotion_concurrency),
                )
                for item in prepared
            ]
            for future in concurrent.futures.as_completed(futures):
                future.result()
        for item in prepared:
            item["media"] = _probe_video(item["output"])

    showcase_path: Path | None = None
    showcase_media: dict[str, Any] | None = None
    showcase_props_path: Path | None = None
    if not args.capture_only and not args.skip_showcase:
        showcase_assets = assets_dir / "showcase"
        showcase_assets.mkdir(exist_ok=True)
        for item in prepared:
            shutil.copy2(
                item["output"],
                showcase_assets / item["output"].name,
            )
        showcase_props = _showcase_props(
            prepared,
            profile_keys,
            capture_source=args.capture_source,
        )
        showcase_props_path = build_dir / "showcase.props.json"
        showcase_props_path.write_text(
            json.dumps(showcase_props, separators=(",", ":"), sort_keys=True),
            encoding="utf-8",
        )
        source_slug = "real-grok" if args.capture_source == "real" else "fake-grok"
        showcase_path = output_dir / (
            "showcase-algorithms-envelope-and-viseme_"
            f"properties-four-profiles_cases-all-voices_source-{source_slug}-"
            "16khz-pcm-events.mp4"
        )
        _render(
            "FaceShowcase",
            showcase_path,
            assets_dir,
            showcase_props_path,
            concurrency=max(1, args.remotion_concurrency),
        )
        showcase_media = _probe_video(showcase_path)

    videos: list[dict[str, Any]] = []
    for item in prepared:
        scenario: Scenario = item["scenario"]
        profile: AlgorithmProfile = item["profile"]
        videos.append(
            {
                "scenario": asdict(scenario),
                "profile": asdict(profile),
                "capture": item["capture"],
                "packets": len(item["packets"]),
                "stream_events": len(item["events"]),
                "algorithm_state_bytes": item["props"]["algorithm"]["stateBytes"],
                "model_bytes": item["props"]["algorithm"]["modelBytes"],
                "trace_frames": len(item["props"]["trace"]),
                "props": str(item["props_path"]),
                "video": (
                    str(item["output"]) if not args.capture_only else None
                ),
                "media": item.get("media"),
            }
        )
    manifest = {
        "created_at": datetime.now(UTC).isoformat(),
        "host": platform.platform(),
        "source": (
            "real xAI Grok Realtime binary PCM + ordered events"
            if args.capture_source == "real"
            else "deterministic fake Grok-compatible binary PCM + ordered events"
        ),
        "capture_source": args.capture_source,
        "sample_rate_hz": SAMPLE_RATE,
        "audio_played_during_capture": False,
        "output_dir": str(output_dir),
        "profiles": profile_keys,
        "voices": voice_keys,
        "videos": videos,
        "showcase": {
            "props": str(showcase_props_path) if showcase_props_path else None,
            "video": str(showcase_path) if showcase_path else None,
            "media": showcase_media,
        },
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    completed_paths = (
        [item["output"] for item in prepared]
        if not args.capture_only
        else []
    )
    if showcase_path is not None:
        completed_paths.append(showcase_path)
    if args.open and completed_paths:
        if sys.platform != "darwin":
            raise RuntimeError("--open currently requires macOS")
        _run(["open", *(str(path) for path in completed_paths)], cwd=output_dir)

    print(
        json.dumps(
            {
                "status": "complete",
                "manifest": str(manifest_path),
                "videos": [str(path) for path in completed_paths],
                "showcase": str(showcase_path) if showcase_path else None,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
