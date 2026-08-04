# Parallel renderer contributions

These directories are isolated scratch contributions from Claude/Fable
research-and-implementation workers. They are not linked into the production
build until the primary agent reviews licensing, complexity, visual quality,
tests, and integration.

Shared constraints:

- target one software-rendered 160×120 RGB565 face at at least 30 fps on an
  ESP32-S3/CoreS3-class device;
- use integer/fixed-point arithmetic, caller-owned buffers, and no per-frame
  allocation;
- accept the stable 12-byte `face_keyframe_t` plus a 16 kHz sample clock;
- deterministic idle motion must include thoughtful eye, lid, brow, saccade,
  breathing, anticipation, or follow-through behavior where appropriate;
- browser output must be byte-identical through WebAssembly;
- independently implement ideas; do not paste copyleft or ambiguously licensed
  source;
- place every file inside the worker's assigned directory only.
