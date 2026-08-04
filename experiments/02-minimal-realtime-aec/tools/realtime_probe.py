#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "websockets>=15,<17",
# ]
# ///
"""Smoke-test the selectable Realtime contract used by StackChan firmware.

Grok Voice is tested with native 16 kHz PCM in raw WebSocket binary frames.
The OpenAI compatibility track is tested with base64-wrapped 24 kHz PCM JSON.
API keys are read from the environment or Doppler without being printed. The
default probe creates one text turn. With ``--input-wav`` it streams speech in
real time and relies entirely on server VAD. Both modes capture the streamed
response and write a WAV plus a machine-readable event summary.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import os
import subprocess
import time
import wave
from collections import Counter
from pathlib import Path
from typing import Any

import websockets

PROVIDERS = {
    "xai": {
        "url": "wss://api.x.ai/v1/realtime?model={model}",
        "default_model": "grok-voice-latest",
        "default_voice": "eve",
        "sample_rate": 16_000,
        "transport": "binary",
        "environment_key": "XAI_API_KEY",
        "doppler_key": "APP_CONFIG_X_AI_API_KEY",
    },
    "openai": {
        "url": "wss://api.openai.com/v1/realtime?model={model}",
        "default_model": "gpt-realtime-2.1",
        "default_voice": "marin",
        "sample_rate": 24_000,
        "transport": "json_base64",
        "environment_key": "OPENAI_API_KEY",
        "doppler_key": "OPENAI_API_KEY",
    },
}


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
        capture_output=True,
        text=True,
    )
    value = result.stdout.rstrip("\r\n")
    if not value:
        raise RuntimeError(f"Doppler returned an empty {name}")
    return value


def _api_key(
    project: str, config: str, environment_name: str, doppler_name: str
) -> str:
    value = os.environ.get(environment_name)
    if value:
        return value
    return _doppler_secret(project, config, doppler_name)


def _write_wav(path: Path, pcm: bytes, sample_rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(pcm)


def _read_wav(path: Path, expected_sample_rate: int) -> bytes:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        sample_width = source.getsampwidth()
        sample_rate = source.getframerate()
        frames = source.readframes(source.getnframes())
    if channels != 1 or sample_width != 2 or sample_rate != expected_sample_rate:
        raise ValueError(
            f"{path} must be mono 16-bit PCM at {expected_sample_rate} Hz; "
            f"got {channels} channel(s), {sample_width * 8}-bit, "
            f"{sample_rate} Hz"
        )
    return frames


async def _send_audio_chunk(socket: Any, transport: str, pcm: bytes) -> None:
    if transport == "binary":
        await socket.send(pcm)
        return
    event = {
        "type": "input_audio_buffer.append",
        "audio": base64.b64encode(pcm).decode("ascii"),
    }
    await socket.send(json.dumps(event, separators=(",", ":")))


async def _run(args: argparse.Namespace) -> dict[str, Any]:
    provider = PROVIDERS[args.provider]
    model = args.model or provider["default_model"]
    voice = args.voice or provider["default_voice"]
    sample_rate = provider["sample_rate"]
    transport = provider["transport"]
    input_pcm = (
        _read_wav(args.input_wav, sample_rate) if args.input_wav is not None else None
    )
    turn_detection = (
        {"type": "server_vad", "threshold": args.vad_threshold}
        if input_pcm is not None
        else None
    )
    url_template = args.url or provider["url"]
    url = url_template.format(model=model)
    headers: dict[str, str] = {}
    if not args.no_auth:
        key = _api_key(
            args.doppler_project,
            args.doppler_config,
            provider["environment_key"],
            provider["doppler_key"],
        )
        headers["Authorization"] = f"Bearer {key}"
    if args.provider == "openai":
        headers["OpenAI-Safety-Identifier"] = "stackchan-device-lab"
        session: dict[str, Any] = {
            "type": "realtime",
            "model": model,
            "output_modalities": ["audio"],
            "instructions": (
                "You are StackChan, a concise friendly desktop voice assistant. "
                "Reply in one short sentence unless the user asks for detail."
            ),
            "audio": {
                "input": {
                    "format": {"type": "audio/pcm", "rate": sample_rate},
                    "turn_detection": turn_detection,
                    "noise_reduction": None,
                    "transcription": {
                        "model": "gpt-4o-mini-transcribe",
                        "language": "en",
                    },
                },
                "output": {
                    "format": {"type": "audio/pcm", "rate": sample_rate},
                    "voice": voice,
                },
            },
            "max_output_tokens": 256,
        }
    else:
        session = {
            "instructions": (
                "You are StackChan, a concise friendly desktop voice assistant. "
                "Reply in one short sentence unless the user asks for detail."
            ),
            "voice": voice,
            "reasoning": {"effort": "none"},
            "turn_detection": turn_detection,
            "audio": {
                "input": {
                    "format": {"type": "audio/pcm", "rate": sample_rate},
                    "transport": "binary",
                    "transcription": {
                        "model": "grok-transcribe",
                        "language_hint": "en",
                    },
                },
                "output": {
                    "format": {"type": "audio/pcm", "rate": sample_rate},
                    "transport": "binary",
                },
            },
        }
    session_update = {"type": "session.update", "session": session}
    item = {
        "type": "conversation.item.create",
        "item": {
            "type": "message",
            "role": "user",
            "content": [{"type": "input_text", "text": args.prompt}],
        },
    }

    pcm = bytearray()
    audio_frames: list[dict[str, Any]] = []
    stream_events: list[dict[str, Any]] = []
    transcript_parts: list[str] = []
    input_transcript = ""
    event_counts: Counter[str] = Counter()
    started = time.monotonic()
    session_ready_seconds: float | None = None
    first_audio_seconds: float | None = None
    vad_speech_started_seconds: float | None = None
    vad_speech_stopped_seconds: float | None = None

    def append_audio_frame(payload: bytes, protocol_event: str) -> None:
        nonlocal first_audio_seconds
        received_seconds = time.monotonic() - started
        if first_audio_seconds is None:
            first_audio_seconds = received_seconds
        if len(payload) % 2:
            raise RuntimeError(
                f"{protocol_event} carried an odd PCM16 byte count: {len(payload)}"
            )
        sample_start = len(pcm) // 2
        pcm.extend(payload)
        frame = {
            "index": len(audio_frames),
            "protocol_event": protocol_event,
            "received_seconds": received_seconds,
            "bytes": len(payload),
            "sample_start": sample_start,
            "sample_end": len(pcm) // 2,
        }
        audio_frames.append(frame)
        stream_events.append(
            {
                "index": len(stream_events),
                "kind": "assistant_audio",
                "protocol_event": protocol_event,
                "received_seconds": received_seconds,
                "received_audio_samples": sample_start,
                "dispatch_playout_samples": sample_start,
                "bytes": len(payload),
                "sample_start": sample_start,
                "sample_end": len(pcm) // 2,
            }
        )

    def append_json_event(
        event: dict[str, Any],
        *,
        assistant_transcript: str,
    ) -> None:
        event_type = str(event.get("type", "unknown"))
        marker = len(pcm) // 2
        record: dict[str, Any] = {
            "index": len(stream_events),
            "kind": "event",
            "protocol_event": event_type,
            "received_seconds": time.monotonic() - started,
            "received_audio_samples": marker,
            "dispatch_playout_samples": marker,
        }
        for name in (
            "event_id",
            "item_id",
            "response_id",
            "output_index",
            "content_index",
            "status",
        ):
            value = event.get(name)
            if isinstance(value, (str, int, float, bool)) or value is None:
                if value is not None:
                    record[name] = value
        if event_type == "response.output_audio_transcript.delta":
            record["text_delta"] = str(event.get("delta", ""))
            record["text"] = assistant_transcript
            record["cumulative"] = True
        elif event_type == "response.output_audio_transcript.done":
            final_text = event.get("transcript")
            record["text"] = (
                str(final_text)
                if isinstance(final_text, str)
                else assistant_transcript
            )
            record["cumulative"] = True
        elif event_type in {
            "conversation.item.input_audio_transcription.updated",
            "conversation.item.input_audio_transcription.completed",
        }:
            record["text"] = str(event.get("transcript", ""))
            record["cumulative"] = True
        stream_events.append(record)

    async with asyncio.timeout(args.timeout):
        async with websockets.connect(
            url,
            additional_headers=headers,
            max_size=None,
            ping_interval=20,
            ping_timeout=20,
        ) as socket:
            await socket.send(json.dumps(session_update, separators=(",", ":")))

            while session_ready_seconds is None:
                message = await socket.recv()
                if isinstance(message, bytes):
                    raise RuntimeError(  # noqa: TRY004 - invalid protocol phase, not API input
                        "Received output audio before session.updated"
                    )
                event = json.loads(message)
                event_type = event.get("type", "unknown")
                event_counts[event_type] += 1
                append_json_event(
                    event,
                    assistant_transcript="".join(transcript_parts),
                )
                if event_type == "error":
                    raise RuntimeError(json.dumps(event, sort_keys=True))
                if event_type == "session.updated":
                    session_ready_seconds = time.monotonic() - started

            if input_pcm is None:
                await socket.send(json.dumps(item, separators=(",", ":")))
                await socket.send('{"type":"response.create"}')
            else:
                silence = bytes(round(sample_rate * args.vad_silence_seconds) * 2)
                stream = silence + input_pcm + silence
                chunk_bytes = round(sample_rate * args.audio_chunk_ms / 1000) * 2
                for offset in range(0, len(stream), chunk_bytes):
                    chunk = stream[offset : offset + chunk_bytes]
                    await _send_audio_chunk(socket, transport, chunk)
                    await asyncio.sleep(len(chunk) / (sample_rate * 2))

            while True:
                message = await socket.recv()
                if isinstance(message, bytes):
                    append_audio_frame(message, "binary_audio_frame")
                    event_counts["binary_audio_frame"] += 1
                    continue
                event = json.loads(message)
                event_type = event.get("type", "unknown")
                event_counts[event_type] += 1
                if event_type == "error":
                    append_json_event(
                        event,
                        assistant_transcript="".join(transcript_parts),
                    )
                    raise RuntimeError(json.dumps(event, sort_keys=True))
                if event_type == "input_audio_buffer.speech_started":
                    if vad_speech_started_seconds is None:
                        vad_speech_started_seconds = time.monotonic() - started
                elif event_type == "input_audio_buffer.speech_stopped":
                    if vad_speech_stopped_seconds is None:
                        vad_speech_stopped_seconds = time.monotonic() - started
                elif event_type in {
                    "conversation.item.input_audio_transcription.updated",
                    "conversation.item.input_audio_transcription.completed",
                }:
                    input_transcript = event.get("transcript", input_transcript)
                elif event_type == "response.output_audio.delta":
                    append_audio_frame(
                        base64.b64decode(event["delta"], validate=True),
                        event_type,
                    )
                    continue
                elif event_type == "response.output_audio_transcript.delta":
                    transcript_parts.append(event.get("delta", ""))
                append_json_event(
                    event,
                    assistant_transcript="".join(transcript_parts),
                )
                if event_type == "response.done":
                    break

    _write_wav(args.output, bytes(pcm), sample_rate)
    frame_manifest = args.output.with_suffix(".frames.jsonl")
    frame_manifest.write_text(
        "".join(
            json.dumps(frame, separators=(",", ":"), sort_keys=True) + "\n"
            for frame in audio_frames
        ),
        encoding="utf-8",
    )
    stream_manifest = args.output.with_suffix(".events.jsonl")
    stream_manifest.write_text(
        "".join(
            json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n"
            for event in stream_events
        ),
        encoding="utf-8",
    )
    summary = {
        "provider": args.provider,
        "model": model,
        "voice": voice,
        "transport": transport,
        "endpoint": "override" if args.url else "provider_default",
        "input_mode": ("server_vad_audio" if input_pcm is not None else "manual_text"),
        "input_wav": str(args.input_wav) if args.input_wav else None,
        "input_audio_seconds": (
            len(input_pcm) / (sample_rate * 2) if input_pcm is not None else None
        ),
        "vad_threshold": (args.vad_threshold if input_pcm is not None else None),
        "prompt": args.prompt,
        "sample_rate_hz": sample_rate,
        "audio_bytes": len(pcm),
        "audio_seconds": len(pcm) / (sample_rate * 2),
        "audio_frame_count": len(audio_frames),
        "audio_frame_manifest": str(frame_manifest),
        "stream_event_count": len(stream_events),
        "stream_event_manifest": str(stream_manifest),
        "stream_event_clock": "assistant_pcm_received_then_speaker_playout_samples",
        "session_ready_seconds": session_ready_seconds,
        "first_audio_seconds": first_audio_seconds,
        "vad_speech_started_seconds": vad_speech_started_seconds,
        "vad_speech_stopped_seconds": vad_speech_stopped_seconds,
        "elapsed_seconds": time.monotonic() - started,
        "input_transcript": input_transcript,
        "transcript": "".join(transcript_parts),
        "event_counts": dict(sorted(event_counts.items())),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--provider",
        choices=sorted(PROVIDERS),
        default="xai",
        help="Realtime provider profile (default: xai)",
    )
    parser.add_argument(
        "--url",
        help=(
            "override the provider WebSocket URL; {model} is expanded "
            "(for example the local fake Grok server)"
        ),
    )
    parser.add_argument(
        "--no-auth",
        action="store_true",
        help="do not load or send an API key (intended for local servers)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("local/realtime-probe.wav"),
    )
    parser.add_argument(
        "--model",
        help="override the selected provider's default model",
    )
    parser.add_argument(
        "--voice",
        help="override the selected provider's default voice",
    )
    parser.add_argument(
        "--prompt",
        default="Say: StackChan Realtime WebSocket audio is working.",
    )
    parser.add_argument(
        "--input-wav",
        type=Path,
        help=(
            "stream mono 16-bit PCM at the provider sample rate and rely "
            "on server VAD instead of creating a text turn"
        ),
    )
    parser.add_argument(
        "--vad-threshold",
        type=float,
        default=0.1,
        help="server-VAD activation threshold for --input-wav (default: 0.1)",
    )
    parser.add_argument(
        "--vad-silence-seconds",
        type=float,
        default=1.0,
        help="real-time silence sent before and after --input-wav",
    )
    parser.add_argument(
        "--audio-chunk-ms",
        type=int,
        default=100,
        help="PCM streaming frame duration for --input-wav (default: 100)",
    )
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument(
        "--doppler-project",
        default=os.environ.get("STACKCHAN_DOPPLER_PROJECT", "os"),
    )
    parser.add_argument(
        "--doppler-config",
        default=os.environ.get("STACKCHAN_DOPPLER_CONFIG", "dev_jonas"),
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    summary = asyncio.run(_run(args))
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
