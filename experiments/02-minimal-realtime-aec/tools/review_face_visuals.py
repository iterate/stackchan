#!/usr/bin/env python3
"""Ask Grok vision to review StackChan renderer screenshots.

This is deliberately a second-opinion tool, not the acceptance test. The
deterministic renderer rig remains authoritative for bounds, frame timing, and
expression-distance measurements. Grok is useful for the harder art-direction
questions: appeal, readability, coherent gaze, and whether an expression reads
at a glance.
"""

from __future__ import annotations

import argparse
import base64
import json
import mimetypes
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


EXPERIMENT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SECRET = EXPERIMENT_ROOT / "local" / "stackchan_xai_secret.h"
DEFAULT_ENDPOINT = "https://api.x.ai/v1/responses"

DEFAULT_PROMPT = """\
Act as a very demanding character-animation art director and embedded graphics
engineer. Review the attached StackChan renderer screenshots. An attachment
can be a multi-renderer contact sheet or one renderer's labelled expression
sheet. The target quality bar is the clarity, appeal, gaze coherence,
squash/stretch, lid design, and emotional readability of Anki Cozmo/Vector,
adapted to a 160x120 integer-only software renderer.

Be concrete and unsparing. Look for:
1. geometry visibly clipped, cut off, colliding, or leaving the intended face;
2. expressions that do not read or are indistinguishable across directions;
3. incoherent gaze, pupils leaving eyes, bad eyelid/brow relationships;
4. mouths that flap mechanically, deform jaggedly, or ignore emotion;
5. profiles that look like debug art rather than intentional character design;
6. the strongest profiles and specific traits worth propagating.

Important prior-art constraint: authentic Anki Cozmo and Vector display
eye-only faces. An eye-only renderer is therefore valid and must not be
penalized merely for lacking a mouth. Judge whether its eye aperture, lids,
corner bends, scale/spacing, gaze, and timing carry speech and emotion. Apply
mouth criticism only to profiles that intentionally depict a mouth.

Return JSON only with this shape:
{
  "summary": "short verdict",
  "quality_bar_gap": ["specific gap", "..."],
  "clipping_or_geometry_failures": [
    {"profile_ref": "attachment number and filename or printed profile id", "severity": "high|medium|low", "problem": "...", "fix": "..."}
  ],
  "emotion_failures": [
    {"profile_ref": "...", "directions": ["neutral", "joy"], "problem": "...", "fix": "..."}
  ],
  "gaze_and_eye_failures": [
    {"profile_ref": "...", "problem": "...", "fix": "..."}
  ],
  "mouth_failures": [
    {"profile_ref": "...", "problem": "...", "fix": "..."}
  ],
  "top_profiles": [
    {"profile_ref": "...", "reason": "...", "traits_to_propagate": ["...", "..."]}
  ],
  "bottom_profiles": [
    {"profile_ref": "...", "reason": "...", "salvage_or_drop": "salvage|drop"}
  ],
  "highest_leverage_next_changes": [
    {"rank": 1, "change": "...", "profiles_affected": "all|indices", "why": "..."}
  ]
}
Do not praise breadth or the website. Judge the face animation itself.
Never infer a renderer profile index from attachment order. Use the exact
attachment number/filename below, or a profile id visibly printed in the image.
For a single-renderer expression sheet, the small cell labels 00 through 10
are expression ids (neutral, warm, joy, concern, surprise, thoughtful,
skeptical, determined, sleepy, excited, embarrassed), not renderer profiles.
"""


def load_api_key(secret_path: Path) -> str:
    environment_key = os.environ.get("XAI_API_KEY", "").strip()
    if environment_key:
        return environment_key
    if not secret_path.exists():
        raise RuntimeError(
            "XAI_API_KEY is unset and the local firmware secret is missing"
        )
    match = re.search(
        r'STACKCHAN_XAI_API_KEY\s+"([^"]+)"',
        secret_path.read_text(encoding="utf-8"),
    )
    if not match or not match.group(1).strip():
        raise RuntimeError("The local firmware xAI secret is empty")
    return match.group(1).strip()


def image_content(path: Path, detail: str) -> dict[str, str]:
    mime = mimetypes.guess_type(path.name)[0]
    if mime not in {"image/png", "image/jpeg"}:
        raise ValueError(f"{path}: Grok accepts PNG or JPEG screenshots")
    encoded = base64.b64encode(path.read_bytes()).decode("ascii")
    return {
        "type": "input_image",
        "image_url": f"data:{mime};base64,{encoded}",
        "detail": detail,
    }


def extract_output_text(response: dict[str, Any]) -> str:
    direct = response.get("output_text")
    if isinstance(direct, str) and direct:
        return direct
    fragments: list[str] = []
    for item in response.get("output", []):
        if not isinstance(item, dict):
            continue
        for content in item.get("content", []):
            if not isinstance(content, dict):
                continue
            text = content.get("text")
            if (
                content.get("type") in {"output_text", "text"}
                and isinstance(text, str)
            ):
                fragments.append(text)
    if not fragments:
        raise RuntimeError("xAI response did not contain output text")
    return "\n".join(fragments)


def parse_review(text: str) -> Any:
    stripped = text.strip()
    if stripped.startswith("```"):
        stripped = re.sub(r"^```(?:json)?\s*", "", stripped)
        stripped = re.sub(r"\s*```$", "", stripped)
    try:
        return json.loads(stripped)
    except json.JSONDecodeError:
        # Vision models occasionally format zero-padded renderer ids as JSON
        # numbers (for example `"row": 03`).  That is a useful human-facing
        # convention but invalid JSON.  Repair only the small set of integer
        # identity fields used by our review schemas; never rewrite arbitrary
        # values or text.
        repaired = re.sub(
            r'("(?:row|profile_index|slot|rank)"\s*:\s*)0+([0-9]+)',
            r"\1\2",
            stripped,
        )
        try:
            return json.loads(repaired)
        except json.JSONDecodeError:
            return {"unparsed_text": text}


def request_review(
    images: list[Path],
    prompt: str,
    model: str,
    detail: str,
    endpoint: str,
    api_key: str,
    context: str = "",
) -> dict[str, Any]:
    attachment_manifest = "\n".join(
        f"attachment {index:02d}: {path.name}"
        for index, path in enumerate(images)
    )
    grounded_prompt = (
        f"{prompt.rstrip()}\n\n"
        "Attachment identity map (authoritative):\n"
        f"{attachment_manifest}\n"
    )
    if context.strip():
        grounded_prompt += (
            "\nAdditional review context (authoritative; use these row/profile "
            "mappings instead of guessing from attachment order):\n"
            f"{context.strip()}\n"
        )
    content: list[dict[str, str]] = [
        {"type": "input_text", "text": grounded_prompt}
    ]
    content.extend(image_content(path, detail) for path in images)
    payload = {
        "model": model,
        "store": False,
        "input": [{"role": "user", "content": content}],
    }
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=900) as response:
            envelope = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail_text = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(
            f"xAI returned HTTP {error.code}: {detail_text[:1000]}"
        ) from error
    text = extract_output_text(envelope)
    return {
        "model": model,
        "images": [str(path) for path in images],
        "review": parse_review(text),
        "usage": envelope.get("usage"),
        "response_id": envelope.get("id"),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Get a strict Grok vision review of renderer screenshots"
    )
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--model", default="grok-4.5")
    parser.add_argument("--detail", choices=("low", "high"), default="high")
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument("--secret", type=Path, default=DEFAULT_SECRET)
    parser.add_argument("--prompt", help="replace the built-in review prompt")
    parser.add_argument("--prompt-file", type=Path)
    parser.add_argument(
        "--context",
        help="append authoritative row/profile mappings or review context",
    )
    parser.add_argument(
        "--context-file",
        type=Path,
        help="append authoritative row/profile mappings from a file",
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    missing = [path for path in args.images if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"missing screenshot(s): {missing}")
    if args.prompt and args.prompt_file:
        raise ValueError("use --prompt or --prompt-file, not both")
    if args.context and args.context_file:
        raise ValueError("use --context or --context-file, not both")
    prompt = args.prompt or (
        args.prompt_file.read_text(encoding="utf-8")
        if args.prompt_file
        else DEFAULT_PROMPT
    )
    context = args.context or (
        args.context_file.read_text(encoding="utf-8")
        if args.context_file
        else ""
    )
    result = request_review(
        args.images,
        prompt,
        args.model,
        args.detail,
        args.endpoint,
        load_api_key(args.secret),
        context,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
