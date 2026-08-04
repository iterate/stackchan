#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "websockets>=15,<17",
# ]
# ///
"""Deterministic, local subset of the xAI Grok Voice WebSocket protocol.

The server accepts StackChan's real ``session.update`` and response commands,
then emits JSON lifecycle events and raw little-endian PCM16 binary frames.
It never needs an API key and can stream in realtime or as fast as possible.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import signal
import struct
from collections.abc import Iterable
from dataclasses import dataclass, field
from itertools import cycle
from typing import Any

from websockets.asyncio.server import ServerConnection, serve
from websockets.exceptions import ConnectionClosed

DEFAULT_CHUNKS_MS = (7, 23, 11, 47, 19, 31, 13, 41)
SUPPORTED_RATES = {8_000, 16_000, 22_050, 24_000, 32_000, 44_100, 48_000}
TRANSCRIPT = "The deterministic StackChan voice stream is working."


def _parse_milliseconds(value: str) -> tuple[int, ...]:
    values = tuple(int(part) for part in value.split(",") if part.strip())
    if not values or any(item <= 0 for item in values):
        raise argparse.ArgumentTypeError(
            "expected comma-separated positive millisecond values"
        )
    return values


def _triangle(sample_index: int, period: int, amplitude: int) -> int:
    phase = (sample_index % period) * 4
    if phase < period:
        value = phase
    elif phase < period * 3:
        value = period * 2 - phase
    else:
        value = phase - period * 4
    return value * amplitude // period


def deterministic_voice_pcm(sample_rate: int) -> bytes:
    """Return exact, speech-shaped PCM without floating point or randomness."""

    # silence_ms, voiced_ms, amplitude, fundamental period at 16 kHz
    segments = (
        (120, 0, 0, 80),
        (0, 180, 7_000, 80),
        (45, 0, 0, 80),
        (0, 250, 11_000, 53),
        (35, 0, 0, 80),
        (0, 130, 6_000, 17),
        (55, 0, 0, 80),
        (0, 290, 9_000, 67),
        (180, 0, 0, 80),
    )
    samples: list[int] = []
    oscillator_index = 0
    ramp_samples = max(1, sample_rate * 18 // 1_000)
    for silence_ms, voiced_ms, amplitude, period_at_16k in segments:
        if silence_ms:
            samples.extend([0] * (sample_rate * silence_ms // 1_000))
            continue
        count = sample_rate * voiced_ms // 1_000
        period = max(4, period_at_16k * sample_rate // 16_000)
        for index in range(count):
            envelope = min(index + 1, count - index, ramp_samples)
            envelope_q15 = min(32_767, envelope * 32_767 // ramp_samples)
            value = _triangle(oscillator_index, period, amplitude)
            samples.append(value * envelope_q15 // 32_767)
            oscillator_index += 1
    return struct.pack(f"<{len(samples)}h", *samples)


@dataclass(frozen=True)
class FakeGrokConfig:
    chunk_ms: tuple[int, ...] = DEFAULT_CHUNKS_MS
    time_scale: float = 1.0
    stall_after_ms: int = -1
    stall_ms: int = 0
    transcript: str = TRANSCRIPT


@dataclass
class FakeGrokServer:
    config: FakeGrokConfig
    client_finished: asyncio.Event = field(default_factory=asyncio.Event)
    connections: int = 0

    async def handle(self, socket: ServerConnection) -> None:
        self.connections += 1
        connection = _FakeGrokConnection(socket, self.config)
        try:
            await connection.run()
        finally:
            self.client_finished.set()


class _FakeGrokConnection:
    def __init__(self, socket: ServerConnection, config: FakeGrokConfig) -> None:
        self.socket = socket
        self.config = config
        self.sample_rate = 16_000
        self.session: dict[str, Any] = {}
        self.send_lock = asyncio.Lock()
        self.event_number = 0
        self.response_number = 0
        self.response_task: asyncio.Task[None] | None = None
        self.cancel_response = asyncio.Event()
        self.speech_active = False
        self.silence_samples = 0

    async def run(self) -> None:
        await self._send_event(
            "session.created",
            session={
                "id": "sess_stackchan_fake",
                "object": "realtime.session",
                "model": "grok-voice-latest",
            },
        )
        await self._send_event(
            "conversation.created",
            conversation={
                "id": "conv_stackchan_fake",
                "object": "realtime.conversation",
            },
        )
        try:
            async for message in self.socket:
                if isinstance(message, bytes):
                    await self._receive_input_pcm(message)
                else:
                    await self._receive_event(message)
        except ConnectionClosed:
            pass
        finally:
            if self.response_task is not None:
                self.response_task.cancel()
                await asyncio.gather(self.response_task, return_exceptions=True)

    async def _send_event(self, event_type: str, **fields: Any) -> None:
        self.event_number += 1
        event = {
            "event_id": f"event_fake_{self.event_number:04d}",
            "type": event_type,
            **fields,
        }
        async with self.send_lock:
            await self.socket.send(
                json.dumps(event, separators=(",", ":"), sort_keys=True)
            )

    async def _send_binary(self, pcm: bytes) -> None:
        async with self.send_lock:
            await self.socket.send(pcm)

    async def _send_error(self, message: str) -> None:
        await self._send_event(
            "error",
            error={
                "type": "invalid_request_error",
                "code": "fake_server_invalid_request",
                "message": message,
            },
        )

    async def _receive_event(self, raw_event: str) -> None:
        try:
            event = json.loads(raw_event)
        except json.JSONDecodeError:
            await self._send_error("client text frame was not valid JSON")
            return
        event_type = event.get("type")
        if event_type == "session.update":
            await self._update_session(event.get("session"))
        elif event_type == "response.create":
            self._start_response()
        elif event_type == "response.cancel":
            self.cancel_response.set()
        elif event_type in {
            "conversation.item.create",
            "input_audio_buffer.commit",
            "input_audio_buffer.clear",
        }:
            return
        else:
            await self._send_error(
                f"fake server does not implement client event {event_type!r}"
            )

    async def _update_session(self, session: Any) -> None:
        if not isinstance(session, dict):
            await self._send_error("session.update.session must be an object")
            return
        audio = session.get("audio")
        output = audio.get("output") if isinstance(audio, dict) else None
        output_format = output.get("format") if isinstance(output, dict) else None
        rate = output_format.get("rate") if isinstance(output_format, dict) else None
        transport = output.get("transport") if isinstance(output, dict) else None
        if rate not in SUPPORTED_RATES:
            await self._send_error(
                f"unsupported output PCM rate {rate!r}; "
                f"choose one of {sorted(SUPPORTED_RATES)}"
            )
            return
        if output_format.get("type") != "audio/pcm" or transport != "binary":
            await self._send_error(
                "fake Grok track requires audio/pcm with binary transport"
            )
            return
        self.sample_rate = int(rate)
        self.session = session
        await self._send_event(
            "session.updated",
            session={
                **session,
                "id": "sess_stackchan_fake",
                "object": "realtime.session",
            },
        )

    def _start_response(self) -> None:
        if self.response_task is not None and not self.response_task.done():
            return
        self.cancel_response = asyncio.Event()
        self.response_task = asyncio.create_task(self._stream_response())

    async def _stream_response(self) -> None:
        self.response_number += 1
        response_id = f"resp_fake_{self.response_number:04d}"
        item_id = f"item_fake_{self.response_number:04d}"
        await self._send_event(
            "response.created",
            response={
                "id": response_id,
                "object": "realtime.response",
                "status": "in_progress",
            },
        )
        await self._send_event(
            "response.output_audio_transcript.delta",
            response_id=response_id,
            item_id=item_id,
            output_index=0,
            content_index=0,
            delta=self.config.transcript,
        )

        pcm = deterministic_voice_pcm(self.sample_rate)
        offset = 0
        elapsed_samples = 0
        stalled = False
        loop = asyncio.get_running_loop()
        pacing_started = loop.time()
        injected_delay = 0.0
        chunks: Iterable[int] = cycle(self.config.chunk_ms)
        for chunk_ms in chunks:
            if self.cancel_response.is_set() or offset >= len(pcm):
                break
            chunk_samples = max(1, round(self.sample_rate * chunk_ms / 1_000))
            end = min(len(pcm), offset + chunk_samples * 2)
            if (end - offset) % 2:
                end -= 1
            chunk = pcm[offset:end]
            if not chunk:
                break
            await self._send_binary(chunk)
            offset = end
            elapsed_samples += len(chunk) // 2

            if (
                not stalled
                and self.config.stall_after_ms >= 0
                and elapsed_samples * 1_000 // self.sample_rate
                >= self.config.stall_after_ms
            ):
                stalled = True
                injected_delay += self.config.stall_ms / 1_000 * self.config.time_scale
            target_time = (
                pacing_started
                + elapsed_samples / self.sample_rate * self.config.time_scale
                + injected_delay
            )
            await asyncio.sleep(max(0.0, target_time - loop.time()))

        cancelled = self.cancel_response.is_set()
        await self._send_event(
            "response.output_audio.done",
            response_id=response_id,
            item_id=item_id,
            output_index=0,
            content_index=0,
        )
        await self._send_event(
            "response.output_audio_transcript.done",
            response_id=response_id,
            item_id=item_id,
            output_index=0,
            content_index=0,
            transcript=self.config.transcript,
        )
        await self._send_event(
            "response.done",
            response={
                "id": response_id,
                "object": "realtime.response",
                "status": "cancelled" if cancelled else "completed",
                "output": [],
            },
        )

    async def _receive_input_pcm(self, pcm: bytes) -> None:
        if len(pcm) < 2:
            return
        sample_count = len(pcm) // 2
        samples = struct.unpack(f"<{sample_count}h", pcm[: sample_count * 2])
        peak = max(abs(sample) for sample in samples)
        turn_detection = self.session.get("turn_detection")
        if not isinstance(turn_detection, dict):
            return
        threshold = float(turn_detection.get("threshold", 0.1))
        activation = max(512, round(threshold * 8_192))
        voiced = peak >= activation

        if voiced:
            self.silence_samples = 0
            if not self.speech_active:
                self.speech_active = True
                await self._send_event(
                    "input_audio_buffer.speech_started",
                    audio_start_ms=0,
                    item_id="item_fake_input_0001",
                )
            return

        if not self.speech_active:
            return
        self.silence_samples += sample_count
        silence_duration_ms = int(turn_detection.get("silence_duration_ms", 300))
        if self.silence_samples * 1_000 < (self.sample_rate * silence_duration_ms):
            return

        self.speech_active = False
        self.silence_samples = 0
        await self._send_event(
            "input_audio_buffer.speech_stopped",
            audio_end_ms=0,
            item_id="item_fake_input_0001",
        )
        await self._send_event(
            "conversation.item.input_audio_transcription.updated",
            item_id="item_fake_input_0001",
            transcript="Deterministic local microphone input.",
        )
        await self._send_event(
            "conversation.item.input_audio_transcription.completed",
            item_id="item_fake_input_0001",
            transcript="Deterministic local microphone input.",
        )
        self._start_response()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--chunk-ms",
        type=_parse_milliseconds,
        default=DEFAULT_CHUNKS_MS,
        help="repeating binary PCM frame sizes in ms",
    )
    parser.add_argument(
        "--time-scale",
        type=float,
        default=1.0,
        help="1=realtime, 0=send without sleeping",
    )
    parser.add_argument("--stall-after-ms", type=int, default=-1)
    parser.add_argument("--stall-ms", type=int, default=0)
    parser.add_argument(
        "--once",
        action="store_true",
        help="exit after the first client disconnects",
    )
    return parser


async def _run(args: argparse.Namespace) -> None:
    if args.time_scale < 0:
        raise ValueError("--time-scale cannot be negative")
    config = FakeGrokConfig(
        chunk_ms=args.chunk_ms,
        time_scale=args.time_scale,
        stall_after_ms=args.stall_after_ms,
        stall_ms=args.stall_ms,
    )
    fake = FakeGrokServer(config)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(signum, stop.set)
        except NotImplementedError:
            pass

    async with serve(
        fake.handle,
        args.host,
        args.port,
        max_size=None,
        ping_interval=20,
        ping_timeout=20,
    ) as server:
        actual_port = server.sockets[0].getsockname()[1]
        print(
            json.dumps(
                {
                    "status": "ready",
                    "url": (
                        f"ws://{args.host}:{actual_port}"
                        "/v1/realtime?model=grok-voice-latest"
                    ),
                },
                sort_keys=True,
            ),
            flush=True,
        )
        if args.once:
            await fake.client_finished.wait()
        else:
            await stop.wait()


def main() -> int:
    args = build_parser().parse_args()
    try:
        asyncio.run(_run(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
