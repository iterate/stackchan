#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "matplotlib>=3.9",
#   "numpy>=2.0",
#   "scipy>=1.14",
# ]
# ///
"""Measure StackChan acoustic echo cancellation from synchronized PCM captures.

The device-side diagnostic format is a 16-bit PCM WAV with these channels:

    0: raw microphone
    1: exact speaker reference written to the audio codec
    2: AEC-cleaned microphone

Run ``self-test`` first. It creates synthetic captures with known-good and
known-bad cancellation and proves that the pass/fail thresholds separate them.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import wave
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy import ndimage, signal


EXPECTED_SAMPLE_RATE = 16_000
EXPECTED_CHANNELS = 3
EPSILON = 1e-12


@dataclass(frozen=True)
class Capture:
    sample_rate: int
    samples: np.ndarray

    @property
    def raw(self) -> np.ndarray:
        return self.samples[:, 0]

    @property
    def reference(self) -> np.ndarray:
        return self.samples[:, 1]

    @property
    def clean(self) -> np.ndarray:
        return self.samples[:, 2]

    @property
    def duration_seconds(self) -> float:
        return len(self.samples) / self.sample_rate


@dataclass(frozen=True)
class Thresholds:
    minimum_signal_dbfs: float = -48.0
    minimum_far_erle_db: float = 12.0
    minimum_leakage_reduction_db: float = 6.0
    minimum_spectral_leakage_reduction_db: float = 6.0
    minimum_near_gain_db: float = -8.0
    maximum_near_gain_db: float = 6.0
    minimum_near_similarity: float = 0.80
    maximum_clipping_percent: float = 0.10


@dataclass
class Analysis:
    mode: str
    passed: bool
    reasons: list[str]
    sample_rate: int
    duration_seconds: float
    analysed_seconds: float
    reference_lag_ms: float | None
    near_lag_ms: float | None
    raw_dbfs: float
    reference_dbfs: float
    clean_dbfs: float
    raw_clipping_percent: float
    clean_clipping_percent: float
    erle_db: float | None
    leakage_reduction_db: float | None
    spectral_leakage_reduction_db: float | None
    near_gain_db: float | None
    near_similarity: float | None


def _rms(values: np.ndarray) -> float:
    if values.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(values, dtype=np.float64))))


def _power_db(numerator: float, denominator: float = 1.0) -> float:
    return 10.0 * math.log10((numerator + EPSILON) / (denominator + EPSILON))


def _amplitude_db(numerator: float, denominator: float = 1.0) -> float:
    return 20.0 * math.log10((abs(numerator) + EPSILON) / (abs(denominator) + EPSILON))


def _dbfs(values: np.ndarray) -> float:
    return _amplitude_db(_rms(values))


def _similarity(left: np.ndarray, right: np.ndarray) -> float:
    if left.size == 0 or right.size == 0:
        return 0.0
    left = left - np.mean(left)
    right = right - np.mean(right)
    denominator = np.linalg.norm(left) * np.linalg.norm(right)
    if denominator < EPSILON:
        return 0.0
    return float(np.dot(left, right) / denominator)


def _clipping_percent(values: np.ndarray) -> float:
    if values.size == 0:
        return 0.0
    return float(np.mean(np.abs(values) >= (32760.0 / 32768.0)) * 100.0)


def read_capture(path: Path) -> Capture:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.getnframes()
        payload = wav.readframes(frames)

    if width != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, found {width * 8}-bit")
    if channels != EXPECTED_CHANNELS:
        raise ValueError(f"{path}: expected {EXPECTED_CHANNELS} channels, found {channels}")
    if sample_rate != EXPECTED_SAMPLE_RATE:
        raise ValueError(
            f"{path}: expected {EXPECTED_SAMPLE_RATE} Hz, found {sample_rate} Hz"
        )

    pcm = np.frombuffer(payload, dtype="<i2")
    if pcm.size % channels:
        raise ValueError(f"{path}: truncated PCM payload")
    samples = pcm.reshape((-1, channels)).astype(np.float64) / 32768.0
    return Capture(sample_rate=sample_rate, samples=samples)


def write_capture(path: Path, sample_rate: int, samples: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    clipped = np.clip(samples, -1.0, 32767.0 / 32768.0)
    pcm = np.rint(clipped * 32768.0).astype("<i2")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(pcm.shape[1])
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.tobytes())


def write_mono_wav(path: Path, sample_rate: int, samples: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    clipped = np.clip(samples, -1.0, 32767.0 / 32768.0)
    pcm = np.rint(clipped * 32768.0).astype("<i2")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.tobytes())


def _window_mask(
    length: int,
    sample_rate: int,
    window: tuple[float, float] | None,
) -> np.ndarray:
    if window is None:
        return np.ones(length, dtype=bool)
    start, end = window
    if start < 0 or end <= start:
        raise ValueError(f"invalid analysis window {start}:{end}")
    mask = np.zeros(length, dtype=bool)
    first = min(length, round(start * sample_rate))
    last = min(length, round(end * sample_rate))
    mask[first:last] = True
    return mask


def _reference_activity(reference: np.ndarray, sample_rate: int) -> np.ndarray:
    frame_size = max(1, round(sample_rate * 0.020))
    frame_count = math.ceil(len(reference) / frame_size)
    padded = np.pad(reference, (0, frame_count * frame_size - len(reference)))
    frames = padded.reshape((frame_count, frame_size))
    frame_rms = np.sqrt(np.mean(np.square(frames), axis=1))
    peak = float(np.max(frame_rms, initial=0.0))
    threshold = max(10.0 ** (-52.0 / 20.0), peak * 10.0 ** (-32.0 / 20.0))
    active_frames = frame_rms >= threshold
    if active_frames.any():
        # Keep short speech gaps inside a far-end segment.
        active_frames = ndimage.binary_closing(active_frames, structure=np.ones(7))
        active_frames = ndimage.binary_dilation(active_frames, structure=np.ones(3))
    return np.repeat(active_frames, frame_size)[: len(reference)]


def _skip_adaptation(
    mask: np.ndarray,
    sample_rate: int,
    adaptation_seconds: float,
) -> np.ndarray:
    active = np.flatnonzero(mask)
    if active.size == 0:
        return mask
    skip_until = active[0] + round(adaptation_seconds * sample_rate)
    adjusted = mask.copy()
    adjusted[:skip_until] = False
    return adjusted


def estimate_reference_lag(
    reference: np.ndarray,
    raw: np.ndarray,
    sample_rate: int,
    maximum_lag_ms: float = 250.0,
) -> int:
    ref = reference - np.mean(reference)
    mic = raw - np.mean(raw)
    correlation = signal.correlate(mic, ref, mode="full", method="fft")
    lags = signal.correlation_lags(len(mic), len(ref), mode="full")
    maximum = round(maximum_lag_ms * sample_rate / 1000.0)
    allowed = (lags >= -maximum) & (lags <= maximum)
    if not allowed.any():
        return 0
    allowed_indices = np.flatnonzero(allowed)
    best = allowed_indices[int(np.argmax(np.abs(correlation[allowed])))]
    return int(lags[best])


def _delay_signal(values: np.ndarray, lag: int) -> np.ndarray:
    aligned = np.zeros_like(values)
    if lag == 0:
        aligned[:] = values
    elif lag > 0:
        aligned[lag:] = values[:-lag]
    else:
        aligned[:lag] = values[-lag:]
    return aligned


def _aligned_pair(
    reference: np.ndarray,
    observed: np.ndarray,
    lag: int,
) -> tuple[np.ndarray, np.ndarray]:
    """Return overlapping samples after applying ``lag`` to reference."""
    if lag > 0:
        return reference[:-lag], observed[lag:]
    if lag < 0:
        return reference[-lag:], observed[:lag]
    return reference, observed


def _reference_projection_gain(
    reference: np.ndarray,
    observed: np.ndarray,
) -> float:
    reference = reference - np.mean(reference)
    observed = observed - np.mean(observed)
    denominator = float(np.dot(reference, reference))
    if denominator < EPSILON:
        return 0.0
    return abs(float(np.dot(reference, observed))) / denominator


def _reference_transfer_gain(
    reference: np.ndarray,
    observed: np.ndarray,
    sample_rate: int,
) -> float:
    if len(reference) < 512:
        return 0.0
    segment = min(2048, len(reference))
    frequencies, reference_psd = signal.welch(
        reference,
        fs=sample_rate,
        nperseg=segment,
        noverlap=segment // 2,
    )
    _, cross_psd = signal.csd(
        reference,
        observed,
        fs=sample_rate,
        nperseg=segment,
        noverlap=segment // 2,
    )
    band = (frequencies >= 200.0) & (frequencies <= 6_500.0)
    if not band.any():
        return 0.0
    denominator = float(np.sum(reference_psd[band]))
    if denominator < EPSILON:
        return 0.0
    return float(np.sum(np.abs(cross_psd[band])) / denominator)


def analyse_capture(
    capture: Capture,
    mode: str,
    thresholds: Thresholds,
    window: tuple[float, float] | None = None,
    adaptation_seconds: float = 0.75,
    reference_lag_samples: int | None = None,
) -> Analysis:
    if mode not in {"far", "near", "double"}:
        raise ValueError(f"unsupported mode: {mode}")

    base_mask = _window_mask(len(capture.samples), capture.sample_rate, window)
    reference_active = _reference_activity(capture.reference, capture.sample_rate)
    lag: int | None = None
    near_lag: int | None = None

    if mode == "near":
        mask = base_mask & ~reference_active
    else:
        mask = base_mask & reference_active
        mask = _skip_adaptation(mask, capture.sample_rate, adaptation_seconds)
        if not mask.any():
            # Short fixtures should still produce an informative failure.
            mask = base_mask & reference_active

    raw = capture.raw[mask]
    reference = capture.reference[mask]
    clean = capture.clean[mask]
    raw_dbfs = _dbfs(raw)
    reference_dbfs = _dbfs(reference)
    clean_dbfs = _dbfs(clean)
    raw_clipping = _clipping_percent(raw)
    clean_clipping = _clipping_percent(clean)

    erle_db: float | None = None
    leakage_reduction_db: float | None = None
    spectral_leakage_reduction_db: float | None = None
    near_gain_db: float | None = None
    near_similarity: float | None = None

    reasons: list[str] = []
    if not mask.any():
        reasons.append("no samples matched the requested analysis window")

    if mode in {"far", "double"} and mask.any():
        if reference_dbfs < thresholds.minimum_signal_dbfs:
            reasons.append(
                f"speaker reference is too quiet ({reference_dbfs:.1f} dBFS)"
            )
        if raw_dbfs < thresholds.minimum_signal_dbfs:
            reasons.append(f"raw mic did not hear the test ({raw_dbfs:.1f} dBFS)")

        lag = (
            reference_lag_samples
            if reference_lag_samples is not None
            else estimate_reference_lag(reference, raw, capture.sample_rate)
        )
        aligned_reference = _delay_signal(reference, lag)
        raw_projection = _reference_projection_gain(aligned_reference, raw)
        clean_projection = _reference_projection_gain(aligned_reference, clean)
        erle_db = _power_db(np.mean(np.square(raw)), np.mean(np.square(clean)))
        leakage_reduction_db = _amplitude_db(raw_projection, clean_projection)
        raw_transfer = _reference_transfer_gain(
            aligned_reference, raw, capture.sample_rate
        )
        clean_transfer = _reference_transfer_gain(
            aligned_reference, clean, capture.sample_rate
        )
        spectral_leakage_reduction_db = _amplitude_db(
            raw_transfer, clean_transfer
        )

        # During double-talk, total clean-channel power should contain the
        # nearby speaker. Raw/clean power ratio is therefore not an echo
        # metric and must not be used as a pass/fail gate.
        if mode == "far" and erle_db < thresholds.minimum_far_erle_db:
            reasons.append(
                f"echo suppression is {erle_db:.1f} dB; need at least "
                f"{thresholds.minimum_far_erle_db:.1f} dB"
            )
        if leakage_reduction_db < thresholds.minimum_leakage_reduction_db:
            reasons.append(
                "speaker-reference correlation fell by only "
                f"{leakage_reduction_db:.1f} dB; need "
                f"{thresholds.minimum_leakage_reduction_db:.1f} dB"
            )
        if (
            spectral_leakage_reduction_db
            < thresholds.minimum_spectral_leakage_reduction_db
        ):
            reasons.append(
                "band-limited speaker leakage fell by only "
                f"{spectral_leakage_reduction_db:.1f} dB; need "
                f"{thresholds.minimum_spectral_leakage_reduction_db:.1f} dB"
            )

    if mode == "near" and mask.any():
        if raw_dbfs < thresholds.minimum_signal_dbfs:
            reasons.append(f"raw mic did not hear nearby speech ({raw_dbfs:.1f} dBFS)")
        near_lag = estimate_reference_lag(raw, clean, capture.sample_rate)
        aligned_raw, aligned_clean = _aligned_pair(raw, clean, near_lag)
        near_gain_db = _amplitude_db(_rms(aligned_clean), _rms(aligned_raw))
        near_similarity = _similarity(aligned_raw, aligned_clean)
        if near_gain_db < thresholds.minimum_near_gain_db:
            reasons.append(
                f"AEC suppressed nearby speech by {-near_gain_db:.1f} dB; "
                f"maximum allowed is {-thresholds.minimum_near_gain_db:.1f} dB"
            )
        if near_gain_db > thresholds.maximum_near_gain_db:
            reasons.append(
                f"AEC amplified nearby speech by {near_gain_db:.1f} dB; "
                f"maximum allowed is {thresholds.maximum_near_gain_db:.1f} dB"
            )
        if near_similarity < thresholds.minimum_near_similarity:
            reasons.append(
                f"near-speech waveform similarity is {near_similarity:.2f}; "
                f"need at least {thresholds.minimum_near_similarity:.2f}"
            )

    if clean_clipping > thresholds.maximum_clipping_percent:
        reasons.append(
            f"clean channel clips {clean_clipping:.3f}% of samples; "
            f"maximum is {thresholds.maximum_clipping_percent:.3f}%"
        )

    return Analysis(
        mode=mode,
        passed=not reasons,
        reasons=reasons,
        sample_rate=capture.sample_rate,
        duration_seconds=capture.duration_seconds,
        analysed_seconds=float(np.count_nonzero(mask) / capture.sample_rate),
        reference_lag_ms=(
            None if lag is None else lag * 1000.0 / capture.sample_rate
        ),
        near_lag_ms=(
            None
            if near_lag is None
            else near_lag * 1000.0 / capture.sample_rate
        ),
        raw_dbfs=raw_dbfs,
        reference_dbfs=reference_dbfs,
        clean_dbfs=clean_dbfs,
        raw_clipping_percent=raw_clipping,
        clean_clipping_percent=clean_clipping,
        erle_db=erle_db,
        leakage_reduction_db=leakage_reduction_db,
        spectral_leakage_reduction_db=spectral_leakage_reduction_db,
        near_gain_db=near_gain_db,
        near_similarity=near_similarity,
    )


def _metric(value: float | None, suffix: str = "", precision: int = 1) -> str:
    if value is None:
        return "n/a"
    return f"{value:.{precision}f}{suffix}"


def _write_plots(capture: Capture, output: Path) -> None:
    seconds = np.arange(len(capture.samples)) / capture.sample_rate
    figure, axes = plt.subplots(3, 1, figsize=(13, 8), sharex=True)
    labels = ("Raw microphone", "Speaker reference", "AEC output")
    colors = ("#d95f02", "#1b9e77", "#7570b3")
    for axis, values, label, color in zip(
        axes,
        (capture.raw, capture.reference, capture.clean),
        labels,
        colors,
        strict=True,
    ):
        axis.plot(seconds, values, linewidth=0.45, color=color)
        axis.set_ylabel(label)
        axis.set_ylim(-1.0, 1.0)
        axis.grid(alpha=0.15)
    axes[-1].set_xlabel("Seconds")
    figure.tight_layout()
    figure.savefig(output / "waveforms.png", dpi=150)
    plt.close(figure)

    figure, axes = plt.subplots(2, 1, figsize=(13, 7), sharex=True)
    for axis, values, label in zip(
        axes,
        (capture.raw, capture.clean),
        ("Raw microphone", "AEC output"),
        strict=True,
    ):
        axis.specgram(
            values,
            NFFT=1024,
            Fs=capture.sample_rate,
            noverlap=768,
            cmap="magma",
            scale="dB",
        )
        axis.set_ylabel(f"{label}\nHz")
        axis.set_ylim(0, 8_000)
    axes[-1].set_xlabel("Seconds")
    figure.tight_layout()
    figure.savefig(output / "spectrograms.png", dpi=150)
    plt.close(figure)


def write_report(
    capture_path: Path,
    capture: Capture,
    analysis: Analysis,
    output: Path,
) -> None:
    output.mkdir(parents=True, exist_ok=True)
    capture_copy = output / "capture.wav"
    if capture_path.resolve() != capture_copy.resolve():
        shutil.copy2(capture_path, capture_copy)
    write_mono_wav(output / "raw-mic.wav", capture.sample_rate, capture.raw)
    write_mono_wav(
        output / "speaker-reference.wav", capture.sample_rate, capture.reference
    )
    write_mono_wav(output / "aec-clean.wav", capture.sample_rate, capture.clean)
    _write_plots(capture, output)

    metrics = asdict(analysis)
    (output / "report.json").write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    verdict = "PASS" if analysis.passed else "FAIL"
    reason_items = "".join(
        f"<li>{html.escape(reason)}</li>" for reason in analysis.reasons
    )
    if not reason_items:
        reason_items = "<li>All configured checks passed.</li>"
    rows = [
        ("Mode", analysis.mode),
        ("Analysed", _metric(analysis.analysed_seconds, " s", 2)),
        ("Reference lag", _metric(analysis.reference_lag_ms, " ms", 1)),
        ("AEC near-path lag", _metric(analysis.near_lag_ms, " ms", 1)),
        ("Raw mic", _metric(analysis.raw_dbfs, " dBFS")),
        ("Speaker reference", _metric(analysis.reference_dbfs, " dBFS")),
        ("AEC output", _metric(analysis.clean_dbfs, " dBFS")),
        ("Raw microphone clipping", _metric(analysis.raw_clipping_percent, "%", 3)),
        ("ERLE", _metric(analysis.erle_db, " dB")),
        (
            "Reference-correlation reduction",
            _metric(analysis.leakage_reduction_db, " dB"),
        ),
        (
            "Band-limited reference leakage reduction",
            _metric(analysis.spectral_leakage_reduction_db, " dB"),
        ),
        ("Near-speech gain", _metric(analysis.near_gain_db, " dB")),
        ("Near-speech similarity", _metric(analysis.near_similarity, "", 2)),
        (
            "AEC clipping",
            _metric(analysis.clean_clipping_percent, "%", 3),
        ),
    ]
    table_rows = "".join(
        f"<tr><th>{html.escape(name)}</th><td>{html.escape(value)}</td></tr>"
        for name, value in rows
    )
    status_color = "#14804a" if analysis.passed else "#b42318"
    report_html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>StackChan AEC {verdict}</title>
  <style>
    body {{ font: 16px/1.45 system-ui, sans-serif; max-width: 1100px;
            margin: 2rem auto; padding: 0 1rem; color: #17202a; }}
    h1 span {{ color: {status_color}; }}
    table {{ border-collapse: collapse; min-width: 620px; }}
    th, td {{ border-bottom: 1px solid #d8dee4; padding: .5rem .8rem;
              text-align: left; }}
    th {{ width: 310px; }}
    img {{ display: block; max-width: 100%; margin: 1.2rem 0; }}
    audio {{ width: 100%; margin: .25rem 0 1rem; }}
    code {{ background: #f3f5f7; padding: .1rem .3rem; }}
  </style>
</head>
<body>
  <h1>StackChan AEC: <span>{verdict}</span></h1>
  <ul>{reason_items}</ul>
  <table>{table_rows}</table>
  <h2>Listen</h2>
  <p>Raw microphone</p><audio controls src="raw-mic.wav"></audio>
  <p>Exact speaker reference</p><audio controls src="speaker-reference.wav"></audio>
  <p>AEC-cleaned microphone</p><audio controls src="aec-clean.wav"></audio>
  <h2>Waveforms</h2>
  <img src="waveforms.png" alt="Synchronized AEC waveforms">
  <h2>Spectrograms</h2>
  <img src="spectrograms.png" alt="Raw and cleaned spectrograms">
  <p>Machine-readable metrics: <a href="report.json">report.json</a>.</p>
</body>
</html>
"""
    (output / "index.html").write_text(report_html, encoding="utf-8")


def _speech_like_signal(
    sample_rate: int,
    duration_seconds: float,
    seed: int,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    count = round(sample_rate * duration_seconds)
    noise = rng.standard_normal(count)
    sos = signal.butter(
        6,
        [180.0, 5_800.0],
        btype="bandpass",
        fs=sample_rate,
        output="sos",
    )
    coloured = signal.sosfilt(sos, noise)
    coloured /= max(np.max(np.abs(coloured)), EPSILON)

    frame = max(1, round(sample_rate * 0.120))
    envelope_points = rng.uniform(0.12, 1.0, math.ceil(count / frame) + 1)
    envelope_points[::5] *= 0.08
    envelope = np.interp(
        np.arange(count),
        np.arange(len(envelope_points)) * frame,
        envelope_points,
    )
    fade = min(count // 4, round(sample_rate * 0.08))
    if fade:
        envelope[:fade] *= np.linspace(0.0, 1.0, fade)
        envelope[-fade:] *= np.linspace(1.0, 0.0, fade)
    return coloured * envelope * 0.42


def _simulated_echo(reference: np.ndarray, sample_rate: int) -> np.ndarray:
    impulse = np.zeros(round(sample_rate * 0.18))
    impulse[round(sample_rate * 0.043)] = 0.74
    impulse[round(sample_rate * 0.057)] = -0.22
    impulse[round(sample_rate * 0.081)] = 0.15
    impulse[round(sample_rate * 0.126)] = 0.08
    return signal.fftconvolve(reference, impulse, mode="full")[: len(reference)]


def synthetic_capture(
    mode: str,
    good: bool,
    duration_seconds: float = 7.0,
    sample_rate: int = EXPECTED_SAMPLE_RATE,
) -> np.ndarray:
    rng = np.random.default_rng(730_201)
    count = round(duration_seconds * sample_rate)
    reference = np.zeros(count)
    near = np.zeros(count)

    if mode in {"far", "double"}:
        reference[:] = _speech_like_signal(sample_rate, duration_seconds, seed=42)
    if mode in {"near", "double"}:
        near[:] = _speech_like_signal(sample_rate, duration_seconds, seed=97) * 0.72

    echo = _simulated_echo(reference, sample_rate)
    ambient = rng.standard_normal(count) * 0.0008
    raw = echo + near + ambient
    if good:
        clean = echo * 0.035 + near * 0.97 + ambient
    elif mode == "near":
        clean = near * 0.12 + ambient
    else:
        clean = raw.copy()
    return np.column_stack((raw, reference, clean))


def run_self_test(output: Path | None) -> int:
    thresholds = Thresholds()
    root_cm: tempfile.TemporaryDirectory[str] | None = None
    if output is None:
        root_cm = tempfile.TemporaryDirectory(prefix="stackchan-aec-self-test-")
        root = Path(root_cm.name)
    else:
        root = output
        root.mkdir(parents=True, exist_ok=True)

    cases = [
        ("far", True, True),
        ("far", False, False),
        ("near", True, True),
        ("near", False, False),
        ("double", True, True),
        ("double", False, False),
    ]
    failures: list[str] = []
    summaries: list[dict[str, Any]] = []
    try:
        for mode, good, expected in cases:
            name = f"{mode}-{'good' if good else 'bad'}"
            path = root / f"{name}.wav"
            write_capture(path, EXPECTED_SAMPLE_RATE, synthetic_capture(mode, good))
            capture = read_capture(path)
            analysis = analyse_capture(capture, mode, thresholds)
            summaries.append(
                {
                    "case": name,
                    "expected_pass": expected,
                    "actual_pass": analysis.passed,
                    "erle_db": analysis.erle_db,
                    "near_gain_db": analysis.near_gain_db,
                    "reasons": analysis.reasons,
                }
            )
            if analysis.passed != expected:
                failures.append(
                    f"{name}: expected {'PASS' if expected else 'FAIL'}, "
                    f"got {'PASS' if analysis.passed else 'FAIL'} "
                    f"({'; '.join(analysis.reasons)})"
                )
        print(json.dumps(summaries, indent=2))
        if failures:
            for failure in failures:
                print(f"SELF-TEST FAILURE: {failure}", file=sys.stderr)
            return 1
        print("AEC analyzer self-test: PASS")
        return 0
    finally:
        if root_cm is not None:
            root_cm.cleanup()


def _parse_window(value: str | None) -> tuple[float, float] | None:
    if value is None:
        return None
    try:
        start_text, end_text = value.split(":", 1)
        return float(start_text), float(end_text)
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError(
            "window must be START:END in seconds"
        ) from error


def _request(
    method: str,
    url: str,
    body: bytes | None = None,
    headers: dict[str, str] | None = None,
    timeout: float = 30.0,
) -> bytes:
    request = urllib.request.Request(
        url,
        data=body,
        method=method,
        headers=headers or {},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"{method} {url}: HTTP {error.code}: {detail}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"{method} {url}: {error.reason}") from error


def _device_url(base: str, path: str) -> str:
    return urllib.parse.urljoin(base.rstrip("/") + "/", path.lstrip("/"))


def _upload_signal(base: str, samples: np.ndarray) -> None:
    pcm = np.rint(
        np.clip(samples, -1.0, 32767.0 / 32768.0) * 32768.0
    ).astype("<i2")
    _request(
        "POST",
        _device_url(base, "/api/diag/signal"),
        body=pcm.tobytes(),
        headers={
            "Content-Type": "application/octet-stream",
            "X-Sample-Rate": str(EXPECTED_SAMPLE_RATE),
        },
        timeout=45.0,
    )


def _start_device_capture(base: str, participant: str) -> None:
    if participant not in {"quiet", "speak"}:
        raise ValueError(f"unsupported diagnostic participant: {participant}")
    _request(
        "POST",
        _device_url(base, "/api/diag/start"),
        body=b"{}",
        headers={
            "Content-Type": "application/json",
            "X-Diagnostic-Participant": participant,
        },
    )


def _device_status(base: str) -> dict[str, Any]:
    payload = _request("GET", _device_url(base, "/api/diag/status"), timeout=5.0)
    return json.loads(payload)


def _device_app_status(base: str) -> dict[str, Any]:
    payload = _request("GET", _device_url(base, "/api/status"), timeout=5.0)
    return json.loads(payload)


def _set_device_audio_value(base: str, path: str, key: str, value: int) -> None:
    query = urllib.parse.urlencode({key: value})
    _request(
        "POST",
        _device_url(base, f"{path}?{query}"),
        body=b"{}",
        headers={"Content-Type": "application/json"},
        timeout=5.0,
    )


def _wait_for_device_state(
    base: str,
    wanted: Iterable[str],
    timeout_seconds: float,
) -> dict[str, Any]:
    wanted_set = set(wanted)
    deadline = time.monotonic() + timeout_seconds
    last: dict[str, Any] = {}
    while time.monotonic() < deadline:
        last = _device_status(base)
        if last.get("state") in wanted_set:
            return last
        time.sleep(0.08)
    raise TimeoutError(
        f"device did not reach {sorted(wanted_set)}; last status: {last}"
    )


def _download_capture(base: str, destination: Path) -> None:
    payload = _request(
        "GET",
        _device_url(base, "/api/diag/capture.wav"),
        timeout=45.0,
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)


def _make_speech(destination: Path, phrase: str) -> None:
    say = shutil.which("say")
    if say is None:
        raise RuntimeError("macOS 'say' command is unavailable")
    subprocess.run(
        [
            say,
            "--file-format=WAVE",
            "--data-format=LEI16@16000",
            "-o",
            str(destination),
            phrase,
        ],
        check=True,
    )


def _read_speech_stimulus(path: Path, duration_seconds: float) -> np.ndarray:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        payload = wav.readframes(wav.getnframes())
    if channels != 1 or sample_width != 2 or sample_rate != EXPECTED_SAMPLE_RATE:
        raise ValueError(
            f"{path}: expected mono 16-bit {EXPECTED_SAMPLE_RATE} Hz speech"
        )
    speech = np.frombuffer(payload, dtype="<i2").astype(np.float64) / 32768.0
    target_samples = round(duration_seconds * EXPECTED_SAMPLE_RATE)
    stimulus = np.zeros(target_samples, dtype=np.float64)
    to_copy = min(target_samples, len(speech))
    if to_copy:
        peak = float(np.max(np.abs(speech[:to_copy])))
        gain = 0.40 / max(peak, EPSILON)
        stimulus[:to_copy] = speech[:to_copy] * min(gain, 4.0)
    return stimulus


def _play(path: Path) -> subprocess.Popen[bytes]:
    afplay = shutil.which("afplay")
    if afplay is None:
        raise RuntimeError("macOS 'afplay' command is unavailable")
    return subprocess.Popen(
        [afplay, str(path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def run_device_suite(
    base: str,
    output: Path,
    far_phrase: str,
    phrase: str,
    duration_seconds: float,
    thresholds: Thresholds,
    modes: list[str],
    near_source: str,
) -> int:
    output.mkdir(parents=True, exist_ok=True)
    near_audio = output / "near-end-say.wav"
    far_audio = output / "far-end-stackchan.wav"
    _make_speech(near_audio, phrase)
    _make_speech(far_audio, far_phrase)
    reference = _read_speech_stimulus(
        far_audio,
        duration_seconds,
    )
    silence = np.zeros_like(reference)
    (output / "stimulus.json").write_text(
        json.dumps(
            {
                "duration_seconds": duration_seconds,
                "far_phrase_from_stackchan": far_phrase,
                "near_phrase": phrase,
                "near_source": near_source,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    all_cases = [
        ("far", reference, False),
        ("near", silence, True),
        ("double", reference, True),
    ]
    cases = [case for case in all_cases if case[0] in modes]
    suite: dict[str, Any] = {}
    all_passed = True

    for mode, playback, play_near in cases:
        human_participant = near_source == "person" and play_near
        play_near_on_mac = near_source == "mac" and play_near
        source = (
            "M5 phrase A only"
            if mode == "far"
            else f"{near_source} phrase B only"
            if mode == "near"
            else f"M5 phrase A plus {near_source} phrase B"
        )
        print(
            f"{mode}: {source}; uploading {duration_seconds:.1f}s signal",
            flush=True,
        )
        _upload_signal(base, playback)
        _start_device_capture(
            base,
            participant="speak" if human_participant else "quiet",
        )
        _wait_for_device_state(base, {"running", "ready", "error"}, 10.0)
        near_process: subprocess.Popen[bytes] | None = None
        if play_near_on_mac:
            print(f"{mode}: playing phrase B through the Mac now", flush=True)
            near_process = _play(near_audio)
        elif human_participant:
            print(
                f"{mode}: SPEAK NOW near StackChan: {phrase!r}",
                flush=True,
            )
        status = _wait_for_device_state(base, {"ready", "error"}, duration_seconds + 15)
        if near_process is not None:
            try:
                near_process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                near_process.terminate()
        if status.get("state") == "error":
            raise RuntimeError(f"device diagnostic failed: {status}")

        capture_path = output / mode / "capture.wav"
        _download_capture(base, capture_path)
        capture = read_capture(capture_path)
        analysis = analyse_capture(capture, mode, thresholds)
        write_report(capture_path, capture, analysis, output / mode)
        suite[mode] = asdict(analysis)
        all_passed &= analysis.passed
        print(
            f"{mode}: {'PASS' if analysis.passed else 'FAIL'} "
            f"(ERLE={_metric(analysis.erle_db, ' dB')}, "
            f"near gain={_metric(analysis.near_gain_db, ' dB')})",
            flush=True,
        )

    (output / "suite.json").write_text(
        json.dumps(suite, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0 if all_passed else 1


def _parse_integer_list(value: str) -> list[int]:
    try:
        parsed = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected comma-separated integers"
        ) from error
    if not parsed:
        raise argparse.ArgumentTypeError("at least one integer is required")
    return parsed


def _tuning_score(analysis: Analysis) -> float:
    erle = analysis.erle_db if analysis.erle_db is not None else -120.0
    leakage = (
        analysis.leakage_reduction_db
        if analysis.leakage_reduction_db is not None
        else -120.0
    )
    spectral = (
        analysis.spectral_leakage_reduction_db
        if analysis.spectral_leakage_reduction_db is not None
        else -120.0
    )
    clipping_penalty = max(
        0.0,
        analysis.clean_clipping_percent
        - Thresholds().maximum_clipping_percent,
    )
    return (
        0.45 * erle
        + 0.20 * leakage
        + 0.35 * spectral
        - 4.0 * clipping_penalty
    )


def _semantic_far_end_gate(
    capture_path: Path,
    output: Path,
    expected_far: str | None,
    maximum_similarity: float,
    transcription_model: str,
    doppler_project: str,
    doppler_config: str,
) -> dict[str, Any]:
    # Keep the API-dependent path optional. audio_assess imports this module for
    # capture parsing, so importing it lazily here also avoids a module cycle
    # during the normal local-only analyzer path.
    import audio_assess

    report = audio_assess.assess(
        capture_path,
        output,
        True,
        transcription_model,
        doppler_project,
        doppler_config,
        expected_far,
        None,
    )
    clean = report["channels"]["aec_clean"]
    transcripts = report.get("transcripts", {})
    clean_transcript = transcripts.get("aec_clean", {})
    metric_name = (
        "expected_far_vs_aec_clean_similarity"
        if expected_far
        else "reference_vs_clean_similarity"
    )
    similarity = report.get("semantic", {}).get(metric_name)
    clean_is_quiet = float(clean["dbfs"]) < audio_assess.QUIET_DBFS
    transcript_reliable = bool(
        clean_transcript.get("reliable_for_semantic_comparison", True)
    )
    if clean_is_quiet:
        passed = True
        reason = (
            f"clean channel is quieter than {audio_assess.QUIET_DBFS:.1f} dBFS"
        )
    elif similarity is None or not transcript_reliable:
        passed = False
        reason = "clean speech could not be compared reliably"
    else:
        passed = float(similarity) <= maximum_similarity
        reason = (
            f"{metric_name}={float(similarity):.3f}, "
            f"maximum={maximum_similarity:.3f}"
        )
    return {
        "passed": passed,
        "reason": reason,
        "metric": metric_name,
        "similarity": similarity,
        "maximum_similarity": maximum_similarity,
        "clean_dbfs": clean["dbfs"],
        "clean_text": clean_transcript.get("text", ""),
        "reference_text": transcripts.get("speaker_reference", {}).get(
            "text", ""
        ),
        "assessment": str(output / "assessment.json"),
    }


def _capture_tuning_candidate(
    base: str,
    directory: Path,
    offset_ms: int,
    gain_db: int,
    nlp_level: int,
    duration_seconds: float,
    adaptation_seconds: float,
    thresholds: Thresholds,
) -> tuple[dict[str, Any], Capture, Analysis]:
    _set_device_audio_value(
        base,
        "/api/audio/mic-gain",
        "db",
        gain_db,
    )
    _set_device_audio_value(
        base,
        "/api/audio/aec-nlp",
        "level",
        nlp_level,
    )
    # Set the offset last because that endpoint resets the firmware timing
    # counters. Gain/NLP changes can then settle before the measured window.
    _set_device_audio_value(
        base,
        "/api/audio/aec-reference-offset",
        "ms",
        offset_ms,
    )
    # Let the audio task observe the atomic setting and clear its reference
    # history before diagnostics begins.
    time.sleep(0.08)

    started = time.monotonic()
    _start_device_capture(base, participant="quiet")
    status = _wait_for_device_state(
        base,
        {"ready", "error"},
        duration_seconds + 8.0,
    )
    if status.get("state") == "error":
        raise RuntimeError(f"device diagnostic failed: {status}")
    app_status = _device_app_status(base)

    capture_path = directory / "capture.wav"
    _download_capture(base, capture_path)
    capture = read_capture(capture_path)
    analysis = analyse_capture(
        capture,
        "far",
        thresholds,
        adaptation_seconds=adaptation_seconds,
    )
    elapsed_seconds = time.monotonic() - started
    result = {
        "offset_ms": offset_ms,
        "microphone_gain_db": gain_db,
        "nlp_level": nlp_level,
        "elapsed_seconds": elapsed_seconds,
        "score": _tuning_score(analysis),
        "audio_timing": app_status.get("audio_timing"),
        "audio_read_errors": app_status.get("audio_read_errors"),
        "audio_write_errors": app_status.get("audio_write_errors"),
        **asdict(analysis),
    }
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "report.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return result, capture, analysis


def run_tuning_loop(
    base: str,
    output: Path,
    offsets_ms: list[int],
    gains_db: list[int],
    nlp_levels: list[int],
    duration_seconds: float,
    adaptation_seconds: float,
    seed: int,
    stimulus_wav: Path | None,
    thresholds: Thresholds,
    semantic_gate: bool,
    expected_far: str | None,
    maximum_clean_far_similarity: float,
    semantic_finalists: int,
    transcription_model: str,
    doppler_project: str,
    doppler_config: str,
) -> int:
    invalid_offsets = [
        value for value in offsets_ms if value < 0 or value > 64
    ]
    invalid_gains = [
        value for value in gains_db if value < 0 or value > 37
    ]
    invalid_nlp_levels = [
        value for value in nlp_levels if value < 0 or value > 2
    ]
    if invalid_offsets:
        raise ValueError(f"AEC offsets must be 0..64 ms: {invalid_offsets}")
    if invalid_gains:
        raise ValueError(f"microphone gains must be 0..37 dB: {invalid_gains}")
    if invalid_nlp_levels:
        raise ValueError(f"AEC NLP levels must be 0..2: {invalid_nlp_levels}")
    if duration_seconds <= adaptation_seconds + 0.5:
        raise ValueError(
            "duration must leave at least 0.5 seconds after AEC adaptation"
        )
    if maximum_clean_far_similarity < 0.0 or (
        maximum_clean_far_similarity > 1.0
    ):
        raise ValueError("maximum clean/far similarity must be in 0..1")
    if semantic_finalists < 1:
        raise ValueError("semantic finalists must be at least 1")
    if semantic_gate and stimulus_wav is None:
        raise ValueError(
            "the semantic gate requires --stimulus-wav containing speech"
        )

    output.mkdir(parents=True, exist_ok=True)
    initial_status = _device_app_status(base)
    initial_offset = int(initial_status.get("aec_reference_offset_ms", 0))
    initial_gain = int(initial_status.get("microphone_gain_db", 18))
    initial_nlp_level = int(initial_status.get("aec_nlp_level", 1))
    if stimulus_wav is None:
        stimulus = _speech_like_signal(
            EXPECTED_SAMPLE_RATE,
            duration_seconds,
            seed,
        )
    else:
        stimulus = _read_speech_stimulus(stimulus_wav, duration_seconds)
    write_mono_wav(
        output / "stimulus.wav",
        EXPECTED_SAMPLE_RATE,
        stimulus,
    )
    _upload_signal(base, stimulus)

    results: list[dict[str, Any]] = []
    try:
        candidate_number = 0
        total = len(offsets_ms) * len(gains_db) * len(nlp_levels)
        for gain_db in gains_db:
            for nlp_level in nlp_levels:
                for offset_ms in offsets_ms:
                    candidate_number += 1
                    directory = (
                        output
                        / "candidates"
                        / (
                            f"{candidate_number:03d}-"
                            f"offset-{offset_ms:02d}ms-"
                            f"gain-{gain_db:02d}db-"
                            f"nlp-{nlp_level}"
                        )
                    )
                    result, _, analysis = _capture_tuning_candidate(
                        base,
                        directory,
                        offset_ms,
                        gain_db,
                        nlp_level,
                        duration_seconds,
                        adaptation_seconds,
                        thresholds,
                    )
                    result["candidate"] = candidate_number
                    results.append(result)
                    print(
                        f"[{candidate_number:02d}/{total:02d}] "
                        f"offset={offset_ms:2d} ms gain={gain_db:2d} dB "
                        f"NLP={nlp_level} "
                        f"lag={_metric(analysis.reference_lag_ms, ' ms')} "
                        f"ERLE={_metric(analysis.erle_db, ' dB')} "
                        "leak="
                        f"{_metric(analysis.spectral_leakage_reduction_db, ' dB')} "
                        f"raw-clip={analysis.raw_clipping_percent:.3f}% "
                        f"score={result['score']:.2f}",
                        flush=True,
                    )

        ranked = sorted(
            results,
            key=lambda item: (
                bool(item["passed"]),
                float(item["score"]),
            ),
            reverse=True,
        )
        for rank, result in enumerate(ranked, start=1):
            result["rank"] = rank
        selected: dict[str, Any] | None = None
        semantic_checked = 0
        if semantic_gate:
            for result in ranked:
                if not bool(result["passed"]):
                    continue
                if semantic_checked >= semantic_finalists:
                    break
                semantic_checked += 1
                candidate = int(result["candidate"])
                candidate_dir = (
                    output
                    / "candidates"
                    / (
                        f"{candidate:03d}-"
                        f"offset-{int(result['offset_ms']):02d}ms-"
                        f"gain-{int(result['microphone_gain_db']):02d}db-"
                        f"nlp-{int(result['nlp_level'])}"
                    )
                )
                semantic = _semantic_far_end_gate(
                    candidate_dir / "capture.wav",
                    candidate_dir / "semantic",
                    expected_far,
                    maximum_clean_far_similarity,
                    transcription_model,
                    doppler_project,
                    doppler_config,
                )
                result["semantic_gate"] = semantic
                (candidate_dir / "report.json").write_text(
                    json.dumps(result, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                print(
                    f"semantic finalist {semantic_checked}: candidate "
                    f"{candidate}, {'PASS' if semantic['passed'] else 'FAIL'} "
                    f"({semantic['reason']}); clean={semantic['clean_text']!r}",
                    flush=True,
                )
                if semantic["passed"]:
                    selected = result
                    break
        if selected is None:
            selected = ranked[0]
        selected_offset = int(selected["offset_ms"])
        selected_gain = int(selected["microphone_gain_db"])
        selected_nlp_level = int(selected["nlp_level"])

        print(
            "confirming winner: "
            f"offset={selected_offset} ms gain={selected_gain} dB "
            f"NLP={selected_nlp_level}",
            flush=True,
        )
        confirmation_dir = output / "best"
        confirmation, confirmation_capture, confirmation_analysis = (
            _capture_tuning_candidate(
                base,
                confirmation_dir,
                selected_offset,
                selected_gain,
                selected_nlp_level,
                duration_seconds,
                adaptation_seconds,
                thresholds,
            )
        )
        write_report(
            confirmation_dir / "capture.wav",
            confirmation_capture,
            confirmation_analysis,
            confirmation_dir,
        )

        confirmation_semantic: dict[str, Any] | None = None
        selected_semantic_passed = (
            not semantic_gate
            or bool(selected.get("semantic_gate", {}).get("passed", False))
        )
        if semantic_gate:
            confirmation_semantic = _semantic_far_end_gate(
                confirmation_dir / "capture.wav",
                confirmation_dir / "semantic",
                expected_far,
                maximum_clean_far_similarity,
                transcription_model,
                doppler_project,
                doppler_config,
            )
            confirmation["semantic_gate"] = confirmation_semantic
            print(
                "confirmation semantic gate: "
                f"{'PASS' if confirmation_semantic['passed'] else 'FAIL'} "
                f"({confirmation_semantic['reason']}); "
                f"clean={confirmation_semantic['clean_text']!r}",
                flush=True,
            )
        accepted = (
            confirmation_analysis.passed
            and selected_semantic_passed
            and (
                confirmation_semantic is None
                or bool(confirmation_semantic["passed"])
            )
        )
        if not accepted:
            _set_device_audio_value(
                base,
                "/api/audio/aec-reference-offset",
                "ms",
                initial_offset,
            )
            _set_device_audio_value(
                base,
                "/api/audio/mic-gain",
                "db",
                initial_gain,
            )
            _set_device_audio_value(
                base,
                "/api/audio/aec-nlp",
                "level",
                initial_nlp_level,
            )

        summary = {
            "device": base,
            "initial_status": initial_status,
            "configuration": {
                "offsets_ms": offsets_ms,
                "microphone_gains_db": gains_db,
                "nlp_levels": nlp_levels,
                "duration_seconds": duration_seconds,
                "adaptation_seconds": adaptation_seconds,
                "seed": seed,
                "stimulus_wav": (
                    None if stimulus_wav is None else str(stimulus_wav)
                ),
                "semantic_gate": semantic_gate,
                "expected_far": expected_far,
                "maximum_clean_far_similarity": (
                    maximum_clean_far_similarity
                ),
                "semantic_finalists": semantic_finalists,
                "transcription_model": transcription_model,
                "thresholds": asdict(thresholds),
            },
            "selected": selected,
            "confirmation": confirmation,
            "accepted_and_left_active": accepted,
            "ranked_candidates": ranked,
        }
        (output / "tuning.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(
            f"AEC tuning {'PASS' if accepted else 'FAIL'}: "
            f"offset={selected_offset} ms gain={selected_gain} dB "
            f"NLP={selected_nlp_level}, "
            f"ERLE={_metric(confirmation_analysis.erle_db, ' dB')}, "
            "spectral leakage reduction="
            f"{_metric(confirmation_analysis.spectral_leakage_reduction_db, ' dB')}",
            flush=True,
        )
        if not accepted:
            print(
                "No candidate met the acceptance gate; restored the initial "
                "runtime audio settings.",
                file=sys.stderr,
            )
        return 0 if accepted else 1
    except BaseException:
        try:
            _set_device_audio_value(
                base,
                "/api/audio/aec-reference-offset",
                "ms",
                initial_offset,
            )
            _set_device_audio_value(
                base,
                "/api/audio/mic-gain",
                "db",
                initial_gain,
            )
            _set_device_audio_value(
                base,
                "/api/audio/aec-nlp",
                "level",
                initial_nlp_level,
            )
        except Exception:
            pass
        raise


def _thresholds_from_args(args: argparse.Namespace) -> Thresholds:
    return Thresholds(
        minimum_far_erle_db=args.minimum_erle,
        minimum_leakage_reduction_db=args.minimum_leakage_reduction,
        minimum_spectral_leakage_reduction_db=args.minimum_spectral_leakage_reduction,
        minimum_near_gain_db=args.minimum_near_gain,
        minimum_near_similarity=args.minimum_near_similarity,
    )


def _add_threshold_arguments(parser: argparse.ArgumentParser) -> None:
    defaults = Thresholds()
    parser.add_argument(
        "--minimum-erle",
        type=float,
        default=defaults.minimum_far_erle_db,
        help="minimum far-end echo return loss enhancement in dB",
    )
    parser.add_argument(
        "--minimum-leakage-reduction",
        type=float,
        default=defaults.minimum_leakage_reduction_db,
        help="minimum reference-correlation reduction in dB",
    )
    parser.add_argument(
        "--minimum-spectral-leakage-reduction",
        type=float,
        default=defaults.minimum_spectral_leakage_reduction_db,
        help="minimum band-limited reference transfer-gain reduction in dB",
    )
    parser.add_argument(
        "--minimum-near-gain",
        type=float,
        default=defaults.minimum_near_gain_db,
        help="lowest allowed clean/raw near-speech gain in dB",
    )
    parser.add_argument(
        "--minimum-near-similarity",
        type=float,
        default=defaults.minimum_near_similarity,
        help="lowest allowed clean/raw near-speech correlation",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    self_test = subparsers.add_parser(
        "self-test",
        help="prove the analyzer against synthetic known-good and known-bad captures",
    )
    self_test.add_argument(
        "--output",
        type=Path,
        help="optionally retain generated fixtures in this directory",
    )

    analyse = subparsers.add_parser(
        "analyze",
        help="analyze a three-channel diagnostic WAV",
    )
    analyse.add_argument("capture", type=Path)
    analyse.add_argument("--mode", choices=("far", "near", "double"), required=True)
    analyse.add_argument(
        "--window",
        type=_parse_window,
        help="optional START:END analysis window in seconds",
    )
    analyse.add_argument(
        "--adaptation-seconds",
        type=float,
        default=0.75,
        help="ignore this much initial far-end playback while AEC converges",
    )
    analyse.add_argument(
        "--reference-lag-ms",
        type=float,
        help=(
            "use a lag measured by a preceding far-only run; useful when "
            "near speech makes double-talk correlation ambiguous"
        ),
    )
    analyse.add_argument(
        "--output",
        type=Path,
        required=True,
        help="directory for HTML, JSON, plots, and channel stems",
    )
    _add_threshold_arguments(analyse)

    run = subparsers.add_parser(
        "run",
        help="run far-only, near-only, and double-talk tests against a device",
    )
    run.add_argument(
        "--device",
        default=os.environ.get("STACKCHAN_URL", "http://stackchan.local"),
        help="device base URL (default: %(default)s)",
    )
    run.add_argument("--output", type=Path, required=True)
    run.add_argument(
        "--far-phrase",
        default=(
            "Stack Chan is playing the far end test phrase. "
            "Orange robot, one two three four five."
        ),
        help="phrase A played by the M5 speaker",
    )
    run.add_argument(
        "--phrase",
        default=(
            "Computer, this nearby voice must remain clear while Stack Chan is speaking. "
            "Please preserve every word of this interruption."
        ),
    )
    run.add_argument("--duration", type=float, default=7.0)
    run.add_argument(
        "--near-source",
        choices=("mac", "person"),
        default="mac",
        help=(
            "play near speech through the Mac, or prompt a person on the "
            "StackChan screen (default: %(default)s)"
        ),
    )
    run.add_argument(
        "--modes",
        nargs="+",
        choices=("far", "near", "double"),
        default=["far", "near", "double"],
        help="one or more test phases to run",
    )
    _add_threshold_arguments(run)

    tune = subparsers.add_parser(
        "tune",
        help=(
            "rapidly sweep AEC reference timing and microphone gain against "
            "a deterministic device-played signal"
        ),
    )
    tune.add_argument(
        "--device",
        default=os.environ.get("STACKCHAN_URL", "http://stackchan.local"),
        help="device base URL (default: %(default)s)",
    )
    tune.add_argument("--output", type=Path, required=True)
    tune.add_argument(
        "--offsets",
        type=_parse_integer_list,
        default=_parse_integer_list("0,8,16,24,32,40,48,56,64"),
        help="comma-separated AEC reference offsets in ms",
    )
    tune.add_argument(
        "--gains",
        type=_parse_integer_list,
        default=[37],
        help="comma-separated microphone gains in dB (default: 37)",
    )
    tune.add_argument(
        "--nlp-levels",
        type=_parse_integer_list,
        default=[1, 2],
        help=(
            "comma-separated nonlinear suppression levels: "
            "0=normal, 1=aggressive, 2=very aggressive"
        ),
    )
    tune.add_argument(
        "--duration",
        type=float,
        default=3.0,
        help="seconds of deterministic far-end playback per candidate",
    )
    tune.add_argument(
        "--adaptation-seconds",
        type=float,
        default=0.75,
        help="initial active playback excluded while AEC converges",
    )
    tune.add_argument("--seed", type=int, default=730201)
    tune.add_argument(
        "--stimulus-wav",
        type=Path,
        help=(
            "optional mono 16-bit 16 kHz WAV; otherwise use the deterministic "
            "speech-like broadband stimulus"
        ),
    )
    tune.add_argument(
        "--semantic-gate",
        action="store_true",
        help=(
            "transcribe objective finalists and reject clean audio that still "
            "contains the far-end speech; requires --stimulus-wav"
        ),
    )
    tune.add_argument(
        "--expected-far",
        help=(
            "known text in the speech stimulus; when omitted the semantic gate "
            "compares the speaker-reference and clean transcripts"
        ),
    )
    tune.add_argument(
        "--maximum-clean-far-similarity",
        type=float,
        default=0.25,
        help=(
            "largest accepted transcript similarity between far-end speech "
            "and the AEC-clean channel (default: %(default)s)"
        ),
    )
    tune.add_argument(
        "--semantic-finalists",
        type=int,
        default=3,
        help=(
            "maximum objective finalists to transcribe before declaring the "
            "sweep a semantic failure (default: %(default)s)"
        ),
    )
    tune.add_argument(
        "--transcription-model",
        default="gpt-4o-transcribe",
        choices=(
            "gpt-4o-transcribe",
            "gpt-4o-mini-transcribe",
            "whisper-1",
        ),
    )
    tune.add_argument(
        "--doppler-project",
        default=os.environ.get("STACKCHAN_DOPPLER_PROJECT", "os"),
    )
    tune.add_argument(
        "--doppler-config",
        default=os.environ.get("STACKCHAN_DOPPLER_CONFIG", "dev_jonas"),
    )
    _add_threshold_arguments(tune)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "self-test":
        return run_self_test(args.output)
    if args.command == "analyze":
        thresholds = _thresholds_from_args(args)
        capture = read_capture(args.capture)
        analysis = analyse_capture(
            capture,
            args.mode,
            thresholds,
            window=args.window,
            adaptation_seconds=args.adaptation_seconds,
            reference_lag_samples=(
                None
                if args.reference_lag_ms is None
                else round(args.reference_lag_ms * capture.sample_rate / 1000.0)
            ),
        )
        write_report(args.capture, capture, analysis, args.output)
        print(json.dumps(asdict(analysis), indent=2, sort_keys=True))
        return 0 if analysis.passed else 1
    if args.command == "run":
        return run_device_suite(
            args.device,
            args.output,
            args.far_phrase,
            args.phrase,
            args.duration,
            _thresholds_from_args(args),
            args.modes,
            args.near_source,
        )
    if args.command == "tune":
        return run_tuning_loop(
            args.device,
            args.output,
            args.offsets,
            args.gains,
            args.nlp_levels,
            args.duration,
            args.adaptation_seconds,
            args.seed,
            args.stimulus_wav,
            _thresholds_from_args(args),
            args.semantic_gate,
            args.expected_far,
            args.maximum_clean_far_similarity,
            args.semantic_finalists,
            args.transcription_model,
            args.doppler_project,
            args.doppler_config,
        )
    raise AssertionError(args.command)


if __name__ == "__main__":
    raise SystemExit(main())
