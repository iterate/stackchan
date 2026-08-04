#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "websockets>=15,<17",
# ]
# ///
"""Run all device-free PCM face tests through public production seams."""

from __future__ import annotations

import json
import os
import select
import subprocess
import sys
import tempfile
from itertools import pairwise
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
EXPERIMENT_DIR = TOOLS_DIR.parent
FIRMWARE_DIR = EXPERIMENT_DIR / "firmware-ws"
MAIN_DIR = FIRMWARE_DIR / "main"
HOST_DIR = FIRMWARE_DIR / "host"
TESTS_DIR = FIRMWARE_DIR / "tests"
FEA_DIR = (
    TOOLS_DIR
    / "face-grid"
    / "contrib"
    / "fable_expression_actors_v3"
    / "src"
)
FEA_TESTS_DIR = FEA_DIR.parent / "tests"
VISEME_MODEL = MAIN_DIR / "assets" / "head_audio_model_en_mixed.bin"

FEA_SOURCES = [
    FEA_DIR / "fea_math.c",
    FEA_DIR / "fea_solve.c",
    FEA_DIR / "fea_draw.c",
    FEA_DIR / "fea_actor_mochi.c",
    FEA_DIR / "fea_actor_karakuri.c",
    FEA_DIR / "fea_actor_sticker.c",
    FEA_DIR / "fea_actor_wisp.c",
    FEA_DIR / "fea_actor_scope.c",
    FEA_DIR / "fea_registry.c",
]


def _run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        cwd=EXPERIMENT_DIR,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        raise RuntimeError(
            f"command failed ({result.returncode}): " + " ".join(command)
        )
    return result.stdout


def _compile_and_run(
    output: Path,
    sources: list[Path],
    *,
    optimised: bool = False,
    arguments: list[str] | None = None,
) -> str:
    command = [
        os.environ.get("CC", "clang"),
        "-std=c11",
        "-O3" if optimised else "-O0",
        "-DSTACKCHAN_HOST_TEST",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(MAIN_DIR),
        "-I",
        str(HOST_DIR),
        "-I",
        str(FEA_DIR),
        *(str(source) for source in sources),
        "-lm",
        "-o",
        str(output),
    ]
    _run(command)
    return _run([str(output), *(arguments or [])])


def _run_native_tests(output_dir: Path) -> dict[str, Any]:
    cases = {
        "face_animator": [
            TESTS_DIR / "face_animator_test.c",
            MAIN_DIR / "face_animator.c",
        ],
        "face_geometry": [
            TESTS_DIR / "face_geometry_test.c",
            MAIN_DIR / "face_geometry.c",
        ],
        "face_keyframe": [
            TESTS_DIR / "face_keyframe_test.c",
            MAIN_DIR / "face_keyframe.c",
        ],
        "face_performance": [
            TESTS_DIR / "face_performance_test.c",
            MAIN_DIR / "face_performance.c",
        ],
        "face_cyber_wildcards": [
            TESTS_DIR / "face_cyber_wildcards_test.c",
            MAIN_DIR / "face_cyber_wildcards.c",
        ],
        "face_eye_actors": [
            TESTS_DIR / "face_eye_actors_test.c",
            MAIN_DIR / "face_eye_actors.c",
        ],
        "face_eye_study_redux": [
            TESTS_DIR / "face_eye_study_redux_test.c",
            MAIN_DIR / "face_eye_study_redux.c",
        ],
        "face_mouth_actors": [
            TESTS_DIR / "face_mouth_actors_test.c",
            MAIN_DIR / "face_mouth_actors.c",
        ],
        "face_mouth_study_redux": [
            TESTS_DIR / "face_mouth_study_redux_test.c",
            MAIN_DIR / "face_mouth_study_redux.c",
        ],
        "face_salvage_actors": [
            TESTS_DIR / "face_salvage_actors_test.c",
            MAIN_DIR / "face_salvage_actors.c",
        ],
        "face_salvage_redux_actors": [
            TESTS_DIR / "face_salvage_redux_actors_test.c",
            MAIN_DIR / "face_salvage_redux_actors.c",
        ],
        "face_pixel_redux_actors": [
            TESTS_DIR / "face_pixel_redux_actors_test.c",
            MAIN_DIR / "face_pixel_redux_actors.c",
        ],
        "face_robot_redux_actors": [
            TESTS_DIR / "face_robot_redux_actors_test.c",
            MAIN_DIR / "face_robot_redux_actors.c",
        ],
        "fable_expression_actors": [
            FEA_TESTS_DIR / "test_fea.c",
            *FEA_SOURCES,
            MAIN_DIR / "face_stage.c",
        ],
        "face_render": [
            TESTS_DIR / "face_render_test.c",
            MAIN_DIR / "face_abstract_redux.c",
            MAIN_DIR / "face_closeup_toon_actors.c",
            MAIN_DIR / "face_cyber_wildcards.c",
            MAIN_DIR / "face_eye_actors.c",
            MAIN_DIR / "face_eye_study_redux.c",
            MAIN_DIR / "face_mouth_actors.c",
            MAIN_DIR / "face_mouth_study_redux.c",
            MAIN_DIR / "face_pixel_pack.c",
            MAIN_DIR / "face_pixel_pack_core.c",
            MAIN_DIR / "face_pixel_pack_ega.c",
            MAIN_DIR / "face_pixel_pack_rogue.c",
            MAIN_DIR / "face_pixel_pack_talkie.c",
            MAIN_DIR / "face_pixel_pack_vga.c",
            MAIN_DIR / "face_pixel_redux_actors.c",
            MAIN_DIR / "face_performance.c",
            MAIN_DIR / "face_render.c",
            MAIN_DIR / "face_robot_eyes.c",
            MAIN_DIR / "face_robot_eyes_behavior.c",
            MAIN_DIR / "face_robot_eyes_draw.c",
            MAIN_DIR / "face_robot_eyes_profiles.c",
            MAIN_DIR / "face_robot_redux_actors.c",
            MAIN_DIR / "face_salvage_redux_actors.c",
            MAIN_DIR / "face_sprite_sheet.c",
            MAIN_DIR / "face_sprite_actors.c",
            MAIN_DIR / "face_sprite_redux_actors.c",
            MAIN_DIR / "face_sprite_showcase.c",
            MAIN_DIR / "face_sprite_mossling.c",
            MAIN_DIR / "face_sprite_mossling_generated.c",
            MAIN_DIR / "fta_act.c",
            MAIN_DIR / "fta_draw.c",
            MAIN_DIR / "fta_math.c",
            MAIN_DIR / "fta_styles.c",
            *FEA_SOURCES,
        ],
        "face_pixel_pack": [
            TESTS_DIR / "face_pixel_pack_test.c",
            MAIN_DIR / "face_pixel_pack.c",
            MAIN_DIR / "face_pixel_pack_core.c",
            MAIN_DIR / "face_pixel_pack_ega.c",
            MAIN_DIR / "face_pixel_pack_rogue.c",
            MAIN_DIR / "face_pixel_pack_talkie.c",
            MAIN_DIR / "face_pixel_pack_vga.c",
            MAIN_DIR / "face_stage.c",
        ],
        "face_closeup_toon_actors": [
            TESTS_DIR / "face_closeup_toon_actors_test.c",
            MAIN_DIR / "face_closeup_toon_actors.c",
        ],
        "face_robot_eyes": [
            TESTS_DIR / "face_robot_eyes_test.c",
            MAIN_DIR / "face_robot_eyes.c",
            MAIN_DIR / "face_robot_eyes_behavior.c",
            MAIN_DIR / "face_robot_eyes_draw.c",
            MAIN_DIR / "face_robot_eyes_profiles.c",
            MAIN_DIR / "face_stage.c",
        ],
        "face_sprite_sheet": [
            TESTS_DIR / "face_sprite_sheet_test.c",
            MAIN_DIR / "face_sprite_sheet.c",
        ],
        "face_sprite_actors": [
            TESTS_DIR / "face_sprite_actors_test.c",
            MAIN_DIR / "face_sprite_actors.c",
        ],
        "face_sprite_redux_actors": [
            TESTS_DIR / "face_sprite_redux_actors_test.c",
            MAIN_DIR / "face_sprite_redux_actors.c",
        ],
        "face_sprite_showcase": [
            TESTS_DIR / "face_sprite_showcase_test.c",
            MAIN_DIR / "face_sprite_sheet.c",
            MAIN_DIR / "face_sprite_showcase.c",
        ],
        "face_stage": [
            TESTS_DIR / "face_stage_test.c",
            MAIN_DIR / "face_stage.c",
        ],
        "face_spectral": [
            TESTS_DIR / "face_spectral_test.c",
            MAIN_DIR / "face_spectral.c",
            MAIN_DIR / "face_driver.c",
        ],
        "face_host_bridge": [
            TESTS_DIR / "face_host_bridge_test.c",
            HOST_DIR / "face_host_bridge.c",
            MAIN_DIR / "face_animator.c",
            MAIN_DIR / "face_driver.c",
            MAIN_DIR / "face_spectral.c",
            MAIN_DIR / "face_viseme.c",
            MAIN_DIR / "face_geometry.c",
        ],
        "face_driver": [
            TESTS_DIR / "face_driver_test.c",
            MAIN_DIR / "face_animator.c",
            MAIN_DIR / "face_driver.c",
        ],
        "speech_leveler": [
            TESTS_DIR / "speech_leveler_test.c",
            MAIN_DIR / "speech_leveler.c",
        ],
    }
    outputs: dict[str, str] = {}
    for name, sources in cases.items():
        outputs[name] = _compile_and_run(output_dir / name, sources).strip()
    outputs["face_viseme"] = _compile_and_run(
        output_dir / "face_viseme",
        [
            TESTS_DIR / "face_viseme_test.c",
            MAIN_DIR / "face_viseme.c",
            MAIN_DIR / "face_driver.c",
        ],
        arguments=[str(VISEME_MODEL)],
    ).strip()

    benchmark_output = _compile_and_run(
        output_dir / "face_animator_benchmark",
        [
            TESTS_DIR / "face_animator_benchmark.c",
            MAIN_DIR / "face_animator.c",
        ],
        optimised=True,
    )
    benchmark = json.loads(benchmark_output)
    assert benchmark["animator_bytes"] <= 64
    assert benchmark["state_bytes"] <= 24
    assert benchmark["realtime_factor"] > 100
    viseme_benchmark = json.loads(
        _compile_and_run(
            output_dir / "face_viseme_benchmark",
            [
                TESTS_DIR / "face_viseme_benchmark.c",
                MAIN_DIR / "face_viseme.c",
                MAIN_DIR / "face_driver.c",
            ],
            optimised=True,
            arguments=[str(VISEME_MODEL)],
        )
    )
    assert viseme_benchmark["state_bytes"] <= 8 * 1024
    assert viseme_benchmark["model_bytes"] <= 16 * 1024
    assert viseme_benchmark["realtime_factor"] > 20
    spectral_benchmark = json.loads(
        _compile_and_run(
            output_dir / "face_spectral_benchmark",
            [
                TESTS_DIR / "face_spectral_benchmark.c",
                MAIN_DIR / "face_spectral.c",
                MAIN_DIR / "face_driver.c",
            ],
            optimised=True,
        )
    )
    assert spectral_benchmark["state_bytes"] <= 128
    assert spectral_benchmark["model_bytes"] == 0
    assert spectral_benchmark["realtime_factor"] > 100
    render_benchmark = json.loads(
        _compile_and_run(
            output_dir / "face_render_benchmark",
            [
                TESTS_DIR / "face_render_benchmark.c",
                MAIN_DIR / "face_abstract_redux.c",
                MAIN_DIR / "face_closeup_toon_actors.c",
                MAIN_DIR / "face_cyber_wildcards.c",
                MAIN_DIR / "face_eye_actors.c",
                MAIN_DIR / "face_eye_study_redux.c",
                MAIN_DIR / "face_mouth_actors.c",
                MAIN_DIR / "face_mouth_study_redux.c",
                MAIN_DIR / "face_pixel_pack.c",
                MAIN_DIR / "face_pixel_pack_core.c",
                MAIN_DIR / "face_pixel_pack_ega.c",
                MAIN_DIR / "face_pixel_pack_rogue.c",
                MAIN_DIR / "face_pixel_pack_talkie.c",
                MAIN_DIR / "face_pixel_pack_vga.c",
                MAIN_DIR / "face_pixel_redux_actors.c",
                MAIN_DIR / "face_performance.c",
                MAIN_DIR / "face_render.c",
                MAIN_DIR / "face_robot_eyes.c",
                MAIN_DIR / "face_robot_eyes_behavior.c",
                MAIN_DIR / "face_robot_eyes_draw.c",
                MAIN_DIR / "face_robot_eyes_profiles.c",
                MAIN_DIR / "face_robot_redux_actors.c",
                MAIN_DIR / "face_salvage_redux_actors.c",
                MAIN_DIR / "face_sprite_sheet.c",
                MAIN_DIR / "face_sprite_actors.c",
                MAIN_DIR / "face_sprite_redux_actors.c",
                MAIN_DIR / "face_sprite_showcase.c",
                MAIN_DIR / "face_sprite_mossling.c",
                MAIN_DIR / "face_sprite_mossling_generated.c",
                MAIN_DIR / "fta_act.c",
                MAIN_DIR / "fta_draw.c",
                MAIN_DIR / "fta_math.c",
                MAIN_DIR / "fta_styles.c",
                *FEA_SOURCES,
            ],
            optimised=True,
        )
    )
    assert render_benchmark["profiles"] >= 40
    assert render_benchmark["renderer_ir_bytes"] == 40
    assert render_benchmark["framebuffer_bytes"] == 160 * 120 * 2
    assert render_benchmark["matrix_fps"] > 30
    return {
        "tests": outputs,
        "benchmark": benchmark,
        "viseme_benchmark": viseme_benchmark,
        "spectral_benchmark": spectral_benchmark,
        "render_benchmark": render_benchmark,
    }


def _run_simulator(
    output_dir: Path,
    *,
    mode: str,
    chunk_ms: str,
    stall_after_ms: int = -1,
    stall_ms: int = 0,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    command = [
        sys.executable,
        str(TOOLS_DIR / "face_simulator.py"),
        "--mode",
        mode,
        "--chunk-ms",
        chunk_ms,
        "--artifacts",
        str(output_dir),
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
    _run(command)
    summary = json.loads((output_dir / "summary.json").read_text())
    trace = [
        json.loads(line)
        for line in (output_dir / "face-trace.jsonl").read_text().splitlines()
    ]
    return summary, trace


def _run_server_vad_probe(workspace: Path, input_wav: Path) -> dict[str, Any]:
    server = subprocess.Popen(
        [
            sys.executable,
            str(TOOLS_DIR / "fake_grok_server.py"),
            "--port",
            "0",
            "--once",
            "--time-scale",
            "0",
        ],
        cwd=EXPERIMENT_DIR,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert server.stdout is not None
    try:
        readable, _, _ = select.select([server.stdout], [], [], 15)
        if not readable:
            raise TimeoutError("fake Grok server did not start within 15s")
        ready_line = server.stdout.readline()
        if not ready_line:
            assert server.stderr is not None
            raise RuntimeError(
                "fake Grok server exited before VAD probe: " + server.stderr.read()
            )
        url = json.loads(ready_line)["url"]
        output = workspace / "server-vad-response.wav"
        _run(
            [
                sys.executable,
                str(TOOLS_DIR / "realtime_probe.py"),
                "--provider",
                "xai",
                "--url",
                url,
                "--no-auth",
                "--input-wav",
                str(input_wav),
                "--vad-silence-seconds",
                "0.35",
                "--output",
                str(output),
                "--timeout",
                "10",
            ]
        )
        summary = json.loads(output.with_suffix(".json").read_text())
        counts = summary["event_counts"]
        assert summary["input_mode"] == "server_vad_audio"
        assert summary["input_transcript"] == ("Deterministic local microphone input.")
        assert counts["input_audio_buffer.speech_started"] == 1
        assert counts["input_audio_buffer.speech_stopped"] == 1
        assert counts["conversation.item.input_audio_transcription.completed"] == 1
        assert counts["response.done"] == 1
        assert counts["binary_audio_frame"] > 1
        audio_frames = [
            json.loads(line)
            for line in output.with_suffix(".frames.jsonl").read_text().splitlines()
        ]
        assert len(audio_frames) == counts["binary_audio_frame"]
        assert audio_frames[0]["sample_start"] == 0
        assert audio_frames[-1]["sample_end"] * 2 == summary["audio_bytes"]
        assert all(
            current["sample_end"] == following["sample_start"]
            for current, following in pairwise(audio_frames)
        )
        return summary
    finally:
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.terminate()
            server.wait(timeout=5)


def _assert_healthy(summary: dict[str, Any]) -> None:
    assert summary["audio_played"] is False
    assert summary["animator_bytes"] <= 64
    assert summary["sample_rate_hz"] == 16_000
    assert 0 <= summary["mouth_onset_delay_ms"] <= 20
    assert summary["max_mouth_open"] >= 128
    assert summary["transcript"] == (
        "The deterministic StackChan voice stream is working."
    )
    assert summary["event_counts"]["binary_audio_frame"] > 1
    assert summary["event_counts"]["response.done"] == 1


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="stackchan-face-rig-test-") as raw:
        workspace = Path(raw)
        native = _run_native_tests(workspace)

        fragmented, fragmented_trace = _run_simulator(
            workspace / "virtual-fragmented",
            mode="virtual",
            chunk_ms="7,23,11,47,19,31,13,41",
        )
        coarse, coarse_trace = _run_simulator(
            workspace / "virtual-coarse",
            mode="virtual",
            chunk_ms="100",
        )
        for summary in (fragmented, coarse):
            _assert_healthy(summary)
            assert summary["underrun_frames"] == 0

        assert fragmented["pcm_sha256"] == coarse["pcm_sha256"]
        assert fragmented["trace_sha256"] == coarse["trace_sha256"]
        assert fragmented_trace == coarse_trace
        assert (
            fragmented["event_counts"]["binary_audio_frame"]
            != (coarse["event_counts"]["binary_audio_frame"])
        )
        server_vad = _run_server_vad_probe(
            workspace, workspace / "virtual-fragmented" / "response.wav"
        )

        realtime, realtime_trace = _run_simulator(
            workspace / "realtime",
            mode="realtime",
            chunk_ms="7,23,11,47,19,31,13,41",
        )
        _assert_healthy(realtime)
        assert realtime["pcm_sha256"] == fragmented["pcm_sha256"]
        assert realtime["underrun_frames"] == 0
        assert not any(frame["underrun"] for frame in realtime_trace)

        stalled, stalled_trace = _run_simulator(
            workspace / "realtime-stalled",
            mode="realtime",
            chunk_ms="7,23,11,47,19,31,13,41",
            stall_after_ms=350,
            stall_ms=250,
        )
        _assert_healthy(stalled)
        assert stalled["pcm_sha256"] == fragmented["pcm_sha256"]
        assert stalled["underrun_frames"] > realtime["underrun_frames"]
        assert 20 <= stalled["underrun_frames"] <= 27
        assert any(frame["underrun"] for frame in stalled_trace)
        assert stalled_trace[-1]["state"]["mouth_open"] == 0

        result = {
            "status": "PASS",
            "audio_played": False,
            "native": native,
            "virtual_chunk_invariance": {
                "pcm_sha256": fragmented["pcm_sha256"],
                "trace_sha256": fragmented["trace_sha256"],
                "fragmented_binary_frames": fragmented["event_counts"][
                    "binary_audio_frame"
                ],
                "coarse_binary_frames": coarse["event_counts"]["binary_audio_frame"],
            },
            "mouth_onset_delay_ms": fragmented["mouth_onset_delay_ms"],
            "server_vad": {
                "input_transcript": server_vad["input_transcript"],
                "speech_started": server_vad["event_counts"][
                    "input_audio_buffer.speech_started"
                ],
                "speech_stopped": server_vad["event_counts"][
                    "input_audio_buffer.speech_stopped"
                ],
            },
            "stalled_realtime_underrun_frames": stalled["underrun_frames"],
        }
        print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
