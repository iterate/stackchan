#!/usr/bin/env bash
# Build the WASM module and verify browser output is byte-identical
# to the native renderer (aggregate CRCs + raw frame comparison).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MAIN_DIR="${STACKCHAN_MAIN_DIR:-$ROOT/../../../../firmware-ws/main}"
EMCC_BIN="${EMCC:-emcc}"
OUT_DIR="$ROOT/build/wasm"

if ! test -f "$MAIN_DIR/face_keyframe.h"; then
  echo "error: face_keyframe.h not found in $MAIN_DIR" >&2
  echo "set STACKCHAN_MAIN_DIR to the firmware-ws/main directory" >&2
  exit 1
fi
if ! test -f "$ROOT/generated/ega_sorcerer_atlas.c"; then
  echo "error: run ./build.sh first to generate the atlases" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

"$EMCC_BIN" \
  -std=c11 \
  -Oz \
  -flto \
  -DNDEBUG \
  -Wall \
  -Wextra \
  -Werror \
  -I"$ROOT/src" \
  -I"$ROOT/generated" \
  -I"$ROOT/tests" \
  -I"$MAIN_DIR" \
  "$ROOT/src/sprite_face.c" \
  "$ROOT/generated/ega_sorcerer_atlas.c" \
  "$ROOT/generated/handheld_gobbo_atlas.c" \
  "$ROOT/generated/vga_navigator_atlas.c" \
  "$ROOT/generated/terminal_operator_atlas.c" \
  "$ROOT/tests/scenario.c" \
  "$ROOT/tests/crc32.c" \
  "$HERE/wasm_bridge.c" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createSpriteFaceModule \
  -sENVIRONMENT=web,node \
  -sFILESYSTEM=0 \
  -sMALLOC=emmalloc \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=2097152 \
  -sSTACK_SIZE=65536 \
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPU16','UTF8ToString']" \
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_sprite_wasm_atlas_count','_sprite_wasm_atlas_name','_sprite_wasm_frame_bytes','_sprite_wasm_frame_width','_sprite_wasm_frame_height','_sprite_wasm_scenario_crc','_sprite_wasm_scenario_frame','_sprite_wasm_select','_sprite_wasm_render']" \
  -o "$OUT_DIR/sprite-face.mjs"

node "$HERE/check.mjs"
