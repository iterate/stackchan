#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "websockets>=15,<17",
# ]
# ///
"""Run StackChan's production PCM face engine locally, without an ESP device.

By default this starts the deterministic fake Grok server, receives its binary
PCM, drains a speaker-like playout queue on a 10 ms clock, and leaves audio
muted. ``--open`` shows the animated face in a browser. ``--mode virtual``
runs faster than realtime and produces a byte-for-byte deterministic trace.
"""

from __future__ import annotations

import argparse
import asyncio
import ctypes
import functools
import hashlib
import http.server
import json
import os
import platform
import struct
import subprocess
import sys
import threading
import time
import wave
import webbrowser
from collections import Counter
from collections.abc import Callable
from pathlib import Path
from typing import Any, Self

from websockets.asyncio.client import connect
from websockets.asyncio.server import ServerConnection, serve

TOOLS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TOOLS_DIR.parent
FIRMWARE_DIR = EXPERIMENT_DIR / "firmware-ws"
MAIN_DIR = FIRMWARE_DIR / "main"
HOST_DIR = FIRMWARE_DIR / "host"
SAMPLE_RATE = 16_000
WINDOW_SAMPLES = SAMPLE_RATE // 100
WINDOW_BYTES = WINDOW_SAMPLES * 2
DISPLAY_WIDTH = 320
DISPLAY_HEIGHT = 240
DEFAULT_VISEME_MODEL = (
    MAIN_DIR / "assets" / "head_audio_model_en_mixed.bin"
)


class FaceState(ctypes.Structure):
    _fields_ = [
        ("frame_index", ctypes.c_uint32),
        ("playout_samples", ctypes.c_uint32),
        ("level", ctypes.c_uint16),
        ("mouth_open", ctypes.c_uint8),
        ("mouth_width", ctypes.c_uint8),
        ("mouth_round", ctypes.c_uint8),
        ("mouth_press", ctypes.c_uint8),
        ("mouth_teeth", ctypes.c_uint8),
        ("eye_open", ctypes.c_uint8),
        ("gaze_x", ctypes.c_int8),
        ("gaze_y", ctypes.c_int8),
        ("viseme", ctypes.c_uint8),
        ("phoneme", ctypes.c_uint8),
        ("confidence", ctypes.c_uint8),
        ("activity", ctypes.c_uint8),
        ("speaking", ctypes.c_bool),
    ]

    def as_dict(self) -> dict[str, Any]:
        return {
            name: bool(value) if name == "speaking" else int(value)
            for name, _ in self._fields_
            if (value := getattr(self, name)) is not None
        }


class FaceGeometry(ctypes.Structure):
    _fields_ = [
        ("left_eye_x", ctypes.c_int16),
        ("left_eye_y", ctypes.c_int16),
        ("right_eye_x", ctypes.c_int16),
        ("right_eye_y", ctypes.c_int16),
        ("eye_width", ctypes.c_uint16),
        ("eye_height", ctypes.c_uint16),
        ("pupil_size", ctypes.c_uint16),
        ("pupil_offset_x", ctypes.c_int16),
        ("pupil_offset_y", ctypes.c_int16),
        ("mouth_x", ctypes.c_int16),
        ("mouth_y", ctypes.c_int16),
        ("mouth_width", ctypes.c_uint16),
        ("mouth_height", ctypes.c_uint16),
    ]

    def as_dict(self) -> dict[str, int]:
        return {name: int(getattr(self, name)) for name, _ in self._fields_}


class FaceEnvelopeConfig(ctypes.Structure):
    _fields_ = [
        ("speech_floor", ctypes.c_uint16),
        ("mouth_dynamic_range", ctypes.c_uint16),
        ("attack_percent", ctypes.c_uint8),
        ("release_percent", ctypes.c_uint8),
    ]


class FaceSpectralConfig(ctypes.Structure):
    _fields_ = [
        ("speech_floor", ctypes.c_uint16),
        ("mouth_dynamic_range", ctypes.c_uint16),
        ("attack_percent", ctypes.c_uint8),
        ("release_percent", ctypes.c_uint8),
        ("shape_percent", ctypes.c_uint8),
        ("fricative_percent", ctypes.c_uint8),
    ]


class FaceVisemeConfig(ctypes.Structure):
    _fields_ = [
        ("model_data", ctypes.POINTER(ctypes.c_uint8)),
        ("model_size", ctypes.c_size_t),
        ("speaker_mean_hz", ctypes.c_uint16),
        ("vad_open_level", ctypes.c_uint16),
        ("vad_close_level", ctypes.c_uint16),
        ("mouth_level_range", ctypes.c_uint16),
        ("attack_ms", ctypes.c_uint16),
        ("release_ms", ctypes.c_uint16),
        ("shape_ms", ctypes.c_uint16),
        ("vad_release_hops", ctypes.c_uint8),
        ("vote_window", ctypes.c_uint8),
        ("prime_vote_on_speech", ctypes.c_bool),
    ]


class FaceStreamEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("received_audio_samples", ctypes.c_uint32),
        ("dispatch_playout_samples", ctypes.c_uint32),
        ("utf8", ctypes.c_char_p),
        ("utf8_bytes", ctypes.c_size_t),
        ("cumulative", ctypes.c_bool),
    ]


def _build_face_library(output_dir: Path) -> Path:
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    output = output_dir / f"libstackchan_face_host{suffix}"
    command = [
        os.environ.get("CC", "clang"),
        "-std=c11",
        "-O3",
        "-DNDEBUG",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fPIC",
        "-I",
        str(MAIN_DIR),
        "-I",
        str(HOST_DIR),
        str(HOST_DIR / "face_host_bridge.c"),
        str(MAIN_DIR / "face_animator.c"),
        str(MAIN_DIR / "face_driver.c"),
        str(MAIN_DIR / "face_spectral.c"),
        str(MAIN_DIR / "face_viseme.c"),
        str(MAIN_DIR / "face_geometry.c"),
        "-lm",
    ]
    command.extend(
        ["-dynamiclib", "-o", str(output)]
        if sys.platform == "darwin"
        else ["-shared", "-o", str(output)]
    )
    subprocess.run(command, check=True)
    return output


class FaceEngine:
    def __init__(
        self,
        library_path: Path,
        *,
        algorithm: str = "envelope",
        config: dict[str, Any] | None = None,
        model_path: Path | None = None,
    ) -> None:
        self.library = ctypes.CDLL(str(library_path))
        self.algorithm = algorithm
        self.config = dict(config or {})
        self._model_buffer: ctypes.Array[ctypes.c_char] | None = None
        self._config_object: ctypes.Structure | None = None
        self.library.stackchan_face_animator_size.restype = ctypes.c_size_t
        self.library.stackchan_face_state_size.restype = ctypes.c_size_t
        self.library.stackchan_face_geometry_size.restype = ctypes.c_size_t
        self.library.stackchan_face_algorithm_state_size.argtypes = [
            ctypes.c_char_p
        ]
        self.library.stackchan_face_algorithm_state_size.restype = ctypes.c_size_t
        self.animator_size = int(self.library.stackchan_face_animator_size())
        if self.animator_size > 64:
            raise RuntimeError("face animator exceeded its 64-byte RAM budget")
        if self.library.stackchan_face_state_size() != ctypes.sizeof(FaceState):
            raise RuntimeError("host/C FaceState layouts do not match")
        if self.library.stackchan_face_geometry_size() != ctypes.sizeof(FaceGeometry):
            raise RuntimeError("host/C FaceGeometry layouts do not match")

        algorithm_bytes = algorithm.encode("ascii")
        self.algorithm_state_size = int(
            self.library.stackchan_face_algorithm_state_size(algorithm_bytes)
        )
        if self.algorithm_state_size == 0:
            raise ValueError(f"unknown face algorithm: {algorithm}")
        if self.algorithm_state_size > 8 * 1024:
            raise RuntimeError(
                f"{algorithm} exceeded its 8 KiB state budget "
                f"({self.algorithm_state_size} bytes)"
            )

        config_pointer: ctypes.c_void_p | None = None
        config_size = 0
        if algorithm == "envelope":
            values = {
                "speech_floor": 256,
                "mouth_dynamic_range": 4600,
                "attack_percent": 75,
                "release_percent": 25,
                **self.config,
            }
            self._config_object = FaceEnvelopeConfig(**values)
        elif algorithm == "spectral":
            values = {
                "speech_floor": 220,
                "mouth_dynamic_range": 4400,
                "attack_percent": 72,
                "release_percent": 22,
                "shape_percent": 58,
                "fricative_percent": 34,
                **self.config,
            }
            self._config_object = FaceSpectralConfig(**values)
        elif algorithm == "viseme":
            selected_model = (model_path or DEFAULT_VISEME_MODEL).resolve()
            model_bytes = selected_model.read_bytes()
            self._model_buffer = ctypes.create_string_buffer(model_bytes)
            model_pointer = ctypes.cast(
                self._model_buffer,
                ctypes.POINTER(ctypes.c_uint8),
            )
            values = {
                "model_data": model_pointer,
                "model_size": len(model_bytes),
                "speaker_mean_hz": 150,
                "vad_open_level": 36,
                "vad_close_level": 18,
                "mouth_level_range": 3000,
                "attack_ms": 45,
                "release_ms": 85,
                "shape_ms": 60,
                "vad_release_hops": 4,
                "vote_window": 4,
                "prime_vote_on_speech": True,
                **self.config,
            }
            self._config_object = FaceVisemeConfig(**values)
        else:
            raise ValueError(f"unsupported face algorithm: {algorithm}")
        config_pointer = ctypes.cast(
            ctypes.byref(self._config_object), ctypes.c_void_p
        )
        config_size = ctypes.sizeof(self._config_object)

        self.library.stackchan_face_host_create_algorithm.argtypes = [
            ctypes.c_char_p,
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_size_t,
        ]
        self.library.stackchan_face_host_create_algorithm.restype = ctypes.c_void_p
        self.library.stackchan_face_host_destroy.argtypes = [ctypes.c_void_p]
        self.library.stackchan_face_host_algorithm_name.argtypes = [ctypes.c_void_p]
        self.library.stackchan_face_host_algorithm_name.restype = ctypes.c_char_p
        self.library.stackchan_face_host_push_pcm.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_size_t,
        ]
        self.library.stackchan_face_host_push_event.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(FaceStreamEvent),
        ]
        self.library.stackchan_face_host_snapshot.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.c_uint16,
            ctypes.POINTER(FaceState),
            ctypes.POINTER(FaceGeometry),
        ]
        self.handle = self.library.stackchan_face_host_create_algorithm(
            algorithm_bytes,
            SAMPLE_RATE,
            config_pointer,
            config_size,
        )
        if not self.handle:
            raise RuntimeError(
                f"unable to create host face engine for algorithm {algorithm}"
            )
        selected_name = self.library.stackchan_face_host_algorithm_name(
            self.handle
        ).decode("ascii")
        if selected_name != algorithm:
            raise RuntimeError(
                f"host selected {selected_name!r}, expected {algorithm!r}"
            )

    def close(self) -> None:
        if self.handle:
            self.library.stackchan_face_host_destroy(self.handle)
            self.handle = None

    def push(self, pcm: bytes) -> None:
        if len(pcm) % 2:
            raise ValueError("PCM byte count must be even")
        sample_count = len(pcm) // 2
        samples = (ctypes.c_int16 * sample_count).from_buffer_copy(pcm)
        self.library.stackchan_face_host_push_pcm(self.handle, samples, sample_count)

    def push_event(
        self,
        event_type: int,
        *,
        received_audio_samples: int,
        dispatch_playout_samples: int,
        text: str = "",
        cumulative: bool = False,
    ) -> None:
        encoded = text.encode("utf-8")
        event = FaceStreamEvent(
            type=event_type,
            received_audio_samples=received_audio_samples,
            dispatch_playout_samples=dispatch_playout_samples,
            utf8=encoded or None,
            utf8_bytes=len(encoded),
            cumulative=cumulative,
        )
        self.library.stackchan_face_host_push_event(
            self.handle, ctypes.byref(event)
        )

    def snapshot(self) -> tuple[FaceState, FaceGeometry]:
        state = FaceState()
        geometry = FaceGeometry()
        self.library.stackchan_face_host_snapshot(
            self.handle,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            ctypes.byref(state),
            ctypes.byref(geometry),
        )
        return state, geometry

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


class FaceBroadcaster:
    def __init__(self) -> None:
        self.clients: set[ServerConnection] = set()
        self.latest: str | None = None

    async def handle(self, socket: ServerConnection) -> None:
        self.clients.add(socket)
        try:
            if self.latest is not None:
                await socket.send(self.latest)
            await socket.wait_closed()
        finally:
            self.clients.discard(socket)

    async def publish(self, frame: dict[str, Any]) -> None:
        self.latest = json.dumps(frame, separators=(",", ":"), sort_keys=True)
        if self.clients:
            await asyncio.gather(
                *(client.send(self.latest) for client in tuple(self.clients)),
                return_exceptions=True,
            )


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, _format: str, *_args: object) -> None:
        return


class FaceHttpServer:
    def __init__(self) -> None:
        handler = functools.partial(_QuietHandler, directory=str(TOOLS_DIR))
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            name="stackchan-face-http",
            daemon=True,
        )

    @property
    def port(self) -> int:
        return int(self.server.server_address[1])

    def start(self) -> None:
        self.thread.start()

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


async def _spawn_fake_server(
    mode: str,
    chunk_ms: str,
    stall_after_ms: int,
    stall_ms: int,
) -> tuple[asyncio.subprocess.Process, str]:
    command = [
        "uv",
        "run",
        str(TOOLS_DIR / "fake_grok_server.py"),
        "--port",
        "0",
        "--once",
        "--time-scale",
        "0" if mode == "virtual" else "1",
        "--chunk-ms",
        chunk_ms,
    ]
    if stall_after_ms >= 0:
        command.extend(
            [
                "--stall-after-ms",
                str(stall_after_ms),
                "--stall-ms",
                str(stall_ms),
            ]
        )
    process = await asyncio.create_subprocess_exec(
        *command,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    assert process.stdout is not None
    try:
        ready_line = await asyncio.wait_for(process.stdout.readline(), timeout=15)
    except TimeoutError:
        process.terminate()
        await process.wait()
        raise RuntimeError("fake Grok server did not become ready")
    if not ready_line:
        assert process.stderr is not None
        error = (await process.stderr.read()).decode(errors="replace")
        raise RuntimeError(f"fake Grok server exited early: {error}")
    ready = json.loads(ready_line)
    return process, str(ready["url"])


async def _fetch_response(
    url: str, on_pcm: Callable[[bytes], None] | None = None
) -> tuple[bytes, dict[str, Any]]:
    session_update = {
        "type": "session.update",
        "session": {
            "instructions": "Speak one deterministic local test sentence.",
            "voice": "leo",
            "reasoning": {"effort": "none"},
            "turn_detection": {"type": "server_vad"},
            "audio": {
                "input": {
                    "format": {"type": "audio/pcm", "rate": SAMPLE_RATE},
                    "transport": "binary",
                    "transcription": {
                        "model": "grok-transcribe",
                        "language_hint": "en",
                    },
                },
                "output": {
                    "format": {"type": "audio/pcm", "rate": SAMPLE_RATE},
                    "transport": "binary",
                },
            },
        },
    }
    item = {
        "type": "conversation.item.create",
        "item": {
            "type": "message",
            "role": "user",
            "content": [{"type": "input_text", "text": "Run the local face test."}],
        },
    }
    pcm = bytearray()
    event_counts: Counter[str] = Counter()
    transcript: list[str] = []
    started = time.monotonic()
    first_audio_seconds: float | None = None

    async with connect(url, max_size=None, ping_interval=20, ping_timeout=20) as socket:
        await socket.send(json.dumps(session_update, separators=(",", ":")))
        while event_counts["session.updated"] == 0:
            message = await socket.recv()
            if isinstance(message, bytes):
                raise TypeError("audio arrived before session.updated")
            event = json.loads(message)
            event_type = str(event.get("type", "unknown"))
            event_counts[event_type] += 1
            if event_type == "error":
                raise RuntimeError(json.dumps(event, sort_keys=True))

        await socket.send(json.dumps(item, separators=(",", ":")))
        await socket.send('{"type":"response.create"}')
        while True:
            message = await socket.recv()
            if isinstance(message, bytes):
                if first_audio_seconds is None:
                    first_audio_seconds = time.monotonic() - started
                pcm.extend(message)
                event_counts["binary_audio_frame"] += 1
                if on_pcm is not None:
                    on_pcm(message)
                continue
            event = json.loads(message)
            event_type = str(event.get("type", "unknown"))
            event_counts[event_type] += 1
            if event_type == "error":
                raise RuntimeError(json.dumps(event, sort_keys=True))
            if event_type == "response.output_audio_transcript.delta":
                transcript.append(str(event.get("delta", "")))
            if event_type == "response.done":
                break

    return bytes(pcm), {
        "event_counts": dict(sorted(event_counts.items())),
        "first_audio_seconds": first_audio_seconds,
        "network_elapsed_seconds": time.monotonic() - started,
        "transcript": "".join(transcript),
    }


def _frame(
    engine: FaceEngine, queued_samples: int, *, underrun: bool = False
) -> dict[str, Any]:
    state, geometry = engine.snapshot()
    return {
        "clock_ms": state.playout_samples * 1_000 // SAMPLE_RATE,
        "queued_samples": queued_samples,
        "underrun": underrun,
        "state": state.as_dict(),
        "geometry": geometry.as_dict(),
    }


def _virtual_playout(engine: FaceEngine, pcm: bytes) -> list[dict[str, Any]]:
    trace: list[dict[str, Any]] = []
    padded = pcm + bytes((-len(pcm)) % WINDOW_BYTES)
    for offset in range(0, len(padded), WINDOW_BYTES):
        engine.push(padded[offset : offset + WINDOW_BYTES])
        remaining = max(0, len(padded) - offset - WINDOW_BYTES) // 2
        trace.append(_frame(engine, remaining))
    for _ in range(20):
        engine.push(bytes(WINDOW_BYTES))
        trace.append(_frame(engine, 0))
    return trace


async def _realtime_playout(
    engine: FaceEngine,
    url: str,
    prebuffer_ms: int,
    broadcaster: FaceBroadcaster,
) -> tuple[list[dict[str, Any]], bytes, dict[str, Any]]:
    buffer = bytearray()
    data_available = asyncio.Event()

    def receive_pcm(chunk: bytes) -> None:
        buffer.extend(chunk)
        data_available.set()

    network_task = asyncio.create_task(_fetch_response(url, receive_pcm))
    prebuffer_bytes = SAMPLE_RATE * prebuffer_ms // 1_000 * 2
    while len(buffer) < prebuffer_bytes and not network_task.done():
        data_available.clear()
        await data_available.wait()

    trace: list[dict[str, Any]] = []
    loop = asyncio.get_running_loop()
    next_tick = loop.time()
    release_windows = 0
    while True:
        network_done = network_task.done()
        if len(buffer) >= WINDOW_BYTES:
            pcm_frame = bytes(buffer[:WINDOW_BYTES])
            del buffer[:WINDOW_BYTES]
            underrun = False
        elif network_done:
            pcm_frame = bytes(buffer) + bytes(WINDOW_BYTES - len(buffer))
            buffer.clear()
            underrun = False
        else:
            pcm_frame = bytes(buffer) + bytes(WINDOW_BYTES - len(buffer))
            buffer.clear()
            underrun = True

        engine.push(pcm_frame)
        frame = _frame(engine, len(buffer) // 2, underrun=underrun)
        trace.append(frame)
        if frame["underrun"] or len(trace) % 3 == 1:
            await broadcaster.publish(frame)

        if network_done and not buffer:
            release_windows += 1
            if release_windows >= 20:
                break
        else:
            release_windows = 0
        next_tick += 0.01
        await asyncio.sleep(max(0, next_tick - loop.time()))

    pcm, network = await network_task
    await broadcaster.publish(trace[-1])
    return trace, pcm, network


def _first_nonzero_sample(pcm: bytes) -> int | None:
    for index, (sample,) in enumerate(struct.iter_unpack("<h", pcm)):
        if sample != 0:
            return index
    return None


def _trace_hash(trace: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for frame in trace:
        digest.update(json.dumps(frame, separators=(",", ":"), sort_keys=True).encode())
        digest.update(b"\n")
    return digest.hexdigest()


def _write_wav(path: Path, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm)


def _write_peak_svg(path: Path, frame: dict[str, Any]) -> None:
    geometry = frame["geometry"]
    state = frame["state"]
    pupil_y = (
        geometry["left_eye_y"]
        + (geometry["eye_height"] - geometry["pupil_size"]) // 2
        + geometry["pupil_offset_y"]
    )

    def pupil_x(eye_x: int) -> int:
        return (
            eye_x
            + (geometry["eye_width"] - geometry["pupil_size"]) // 2
            + geometry["pupil_offset_x"]
        )

    pupils = ""
    if geometry["pupil_size"] > 0:
        pupils = (
            f'<rect x="{pupil_x(geometry["left_eye_x"])}" y="{pupil_y}" '
            f'width="{geometry["pupil_size"]}" '
            f'height="{geometry["pupil_size"]}" rx="99" fill="#071521"/>'
            f'<rect x="{pupil_x(geometry["right_eye_x"])}" y="{pupil_y}" '
            f'width="{geometry["pupil_size"]}" '
            f'height="{geometry["pupil_size"]}" rx="99" fill="#071521"/>'
        )
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240" viewBox="0 0 320 240">
<rect width="320" height="240" rx="18" fill="#071521"/>
<text x="160" y="20" text-anchor="middle" font-family="monospace" font-size="11" fill="#60d394">SPEAKING · {state["mouth_open"]}/255</text>
<rect x="{geometry["left_eye_x"]}" y="{geometry["left_eye_y"]}" width="{geometry["eye_width"]}" height="{geometry["eye_height"]}" rx="99" fill="#f4f8ff"/>
<rect x="{geometry["right_eye_x"]}" y="{geometry["right_eye_y"]}" width="{geometry["eye_width"]}" height="{geometry["eye_height"]}" rx="99" fill="#f4f8ff"/>
{pupils}
<rect x="{geometry["mouth_x"]}" y="{geometry["mouth_y"]}" width="{geometry["mouth_width"]}" height="{geometry["mouth_height"]}" rx="99" fill="#ff7b72"/>
</svg>
"""
    path.write_text(svg, encoding="utf-8")


def _write_artifacts(
    output_dir: Path,
    trace: list[dict[str, Any]],
    pcm: bytes,
    network: dict[str, Any],
    mode: str,
    url: str,
    algorithm: str,
    algorithm_state_bytes: int,
    algorithm_config: dict[str, Any],
) -> dict[str, Any]:
    _write_wav(output_dir / "response.wav", pcm)
    trace_text = "".join(
        json.dumps(frame, separators=(",", ":"), sort_keys=True) + "\n"
        for frame in trace
    )
    (output_dir / "face-trace.jsonl").write_text(trace_text, encoding="utf-8")
    peak_frame = max(trace, key=lambda item: item["state"]["mouth_open"])
    _write_peak_svg(output_dir / "peak-face.svg", peak_frame)

    template = (TOOLS_DIR / "face_view.html").read_text(encoding="utf-8")
    report = template.replace(
        "/*__STACKCHAN_TRACE__*/ null",
        json.dumps(trace, separators=(",", ":"), sort_keys=True),
    )
    (output_dir / "face-report.html").write_text(report, encoding="utf-8")

    first_nonzero = _first_nonzero_sample(pcm)
    first_open = next(
        (
            frame["state"]["playout_samples"]
            for frame in trace
            if frame["state"]["mouth_open"] > 0
        ),
        None,
    )
    onset_delay_samples = (
        first_open - first_nonzero
        if first_open is not None and first_nonzero is not None
        else None
    )
    summary = {
        "mode": mode,
        "sample_rate_hz": SAMPLE_RATE,
        "audio_played": False,
        "algorithm": algorithm,
        "algorithm_state_bytes": algorithm_state_bytes,
        "algorithm_config": algorithm_config,
        "animator_bytes": algorithm_state_bytes,
        "audio_seconds": len(pcm) / (SAMPLE_RATE * 2),
        "pcm_bytes": len(pcm),
        "pcm_sha256": hashlib.sha256(pcm).hexdigest(),
        "trace_frames": len(trace),
        "trace_sha256": _trace_hash(trace),
        "underrun_frames": sum(1 for frame in trace if frame["underrun"]),
        "max_mouth_open": peak_frame["state"]["mouth_open"],
        "first_nonzero_sample": first_nonzero,
        "first_mouth_open_sample": first_open,
        "mouth_onset_delay_samples": onset_delay_samples,
        "mouth_onset_delay_ms": (
            onset_delay_samples * 1_000 / SAMPLE_RATE
            if onset_delay_samples is not None
            else None
        ),
        "server_url": url,
        "host_platform": platform.platform(),
        **network,
    }
    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


async def _wait_for_process(
    process: asyncio.subprocess.Process | None,
) -> None:
    if process is None:
        return
    try:
        await asyncio.wait_for(process.wait(), timeout=5)
    except TimeoutError:
        process.terminate()
        await process.wait()


async def _run(args: argparse.Namespace) -> tuple[Path, dict[str, Any]]:
    if args.artifacts is None:
        stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
        output_dir = EXPERIMENT_DIR / "local" / "face-rig" / stamp
    else:
        output_dir = args.artifacts.resolve()
    output_dir.mkdir(parents=True, exist_ok=False)
    library = _build_face_library(output_dir)

    fake_process: asyncio.subprocess.Process | None = None
    if args.url:
        url = args.url
    else:
        fake_process, url = await _spawn_fake_server(
            args.mode,
            args.chunk_ms,
            args.stall_after_ms,
            args.stall_ms,
        )

    broadcaster = FaceBroadcaster()
    http_server: FaceHttpServer | None = None
    try:
        algorithm_config = json.loads(args.algorithm_config)
        if not isinstance(algorithm_config, dict):
            raise ValueError("--algorithm-config must decode to a JSON object")
        with FaceEngine(
            library,
            algorithm=args.algorithm,
            config=algorithm_config,
            model_path=args.viseme_model,
        ) as engine:
            if args.mode == "virtual":
                pcm, network = await _fetch_response(url)
                trace = _virtual_playout(engine, pcm)
            else:
                async with serve(
                    broadcaster.handle, "127.0.0.1", 0, max_size=None
                ) as face_server:
                    face_port = face_server.sockets[0].getsockname()[1]
                    if args.open:
                        http_server = FaceHttpServer()
                        http_server.start()
                        live_url = (
                            f"http://127.0.0.1:{http_server.port}"
                            f"/face_view.html?ws_port={face_port}"
                        )
                        webbrowser.open(live_url)
                    else:
                        live_url = (
                            f"ws://127.0.0.1:{face_port} "
                            "(pass --open for the browser face)"
                        )
                    print(
                        json.dumps(
                            {
                                "status": "live",
                                "face": live_url,
                                "audio": "muted",
                            },
                            sort_keys=True,
                        ),
                        flush=True,
                    )
                    trace, pcm, network = await _realtime_playout(
                        engine,
                        url,
                        args.prebuffer_ms,
                        broadcaster,
                    )
                    if args.open and args.hold_seconds > 0:
                        await asyncio.sleep(args.hold_seconds)

        summary = _write_artifacts(
            output_dir,
            trace,
            pcm,
            network,
            args.mode,
            url,
            engine.algorithm,
            engine.algorithm_state_size,
            engine.config,
        )
        if args.open and args.mode == "virtual":
            webbrowser.open((output_dir / "face-report.html").resolve().as_uri())
        return output_dir, summary
    finally:
        if http_server is not None:
            http_server.close()
        await _wait_for_process(fake_process)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode",
        choices=("realtime", "virtual"),
        default="realtime",
        help="realtime speaker clock or deterministic fast replay",
    )
    parser.add_argument(
        "--url",
        help="use an already-running local Grok-compatible WebSocket",
    )
    parser.add_argument(
        "--algorithm",
        choices=("envelope", "viseme"),
        default="envelope",
        help="PCM-to-pose implementation (default: envelope)",
    )
    parser.add_argument(
        "--algorithm-config",
        default="{}",
        help="implementation-specific config overrides as a JSON object",
    )
    parser.add_argument(
        "--viseme-model",
        type=Path,
        help="HeadAudio binary model (default: checked-in English model)",
    )
    parser.add_argument(
        "--chunk-ms",
        default="7,23,11,47,19,31,13,41",
        help="fake-server binary frame pattern",
    )
    parser.add_argument("--stall-after-ms", type=int, default=-1)
    parser.add_argument("--stall-ms", type=int, default=0)
    parser.add_argument(
        "--prebuffer-ms",
        type=int,
        default=40,
        help="local speaker prebuffer in realtime mode",
    )
    parser.add_argument(
        "--artifacts",
        type=Path,
        help="new directory for WAV, JSONL, summary, SVG, and HTML",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="open the live/replay face in the default browser",
    )
    parser.add_argument(
        "--hold-seconds",
        type=float,
        default=2.0,
        help="keep the live server open briefly after playout",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    output_dir, summary = asyncio.run(_run(args))
    print(
        json.dumps(
            {
                "status": "complete",
                "artifacts": str(output_dir),
                "summary": summary,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
