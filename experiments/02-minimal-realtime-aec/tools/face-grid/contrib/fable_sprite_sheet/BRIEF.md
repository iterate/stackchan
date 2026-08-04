# Generic sprite-sheet renderer worker

Design and implement a tiny, asset-agnostic sprite-sheet animation system for
talking faces. It must make it easy to use original, licensed, or public-domain
old-video-game-style sheets without coupling the engine to copyrighted assets.

Research classic adventure/RPG/fighting-game portrait atlases, Aseprite JSON,
TexturePacker-style metadata, indexed palettes, Preston Blair mouth sheets,
delta tiles, and MCU-friendly compression. Deliver:

- a compact atlas + animation metadata format;
- a build-time converter for PNG/Aseprite-style input into indexed/RGB565 C
  data;
- integer-only C playback driven by `face_keyframe_t` and the 16 kHz clock;
- viseme/mouth-shape selection, blink/eye layers, idle sequences,
  coarticulation/debounce, and deterministic timing;
- several tiny original demonstration sheets covering different pixel styles;
- native tests and memory/performance measurements;
- a licensing/source research note.

Target one 160×120 RGB565 software-rendered face at 30 fps on ESP32-S3/CoreS3.
Use caller-owned buffers and no per-frame allocation. Browser output must be
byte-identical through WebAssembly. Read `../README.md`, both supplied research
attachments, `firmware-ws/main/face_render.h`, and `face_keyframe.h`.

Write every file inside this directory only. Do not modify production code and
do not commit.
