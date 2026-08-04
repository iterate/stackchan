#!/usr/bin/env bash
set -euo pipefail

GRID_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPERIMENT_DIR="$(cd "$GRID_DIR/../.." && pwd)"
MAIN_DIR="$EXPERIMENT_DIR/firmware-ws/main"
HOST_DIR="$EXPERIMENT_DIR/firmware-ws/host"
OUTPUT_DIR="$GRID_DIR/dist"
CAPTURE_DIR="${STACKCHAN_CAPTURE_DIR:-$EXPERIMENT_DIR/local/grok-face-videos/20260728-viseme-matrix-showcase/assets}"
EMCC_BIN="${EMCC:-emcc}"
AVATAR_SRCS=("$MAIN_DIR"/fspp_*_cores3_*_atlas.c)

mkdir -p "$OUTPUT_DIR/audio"

"$EMCC_BIN" \
  -std=c11 \
  -Oz \
  -flto \
  -DNDEBUG \
  -Wall \
  -Wextra \
  -Werror \
  -I"$MAIN_DIR" \
  -I"$HOST_DIR" \
  "$HOST_DIR/face_host_bridge.c" \
  "$HOST_DIR/face_wasm_bridge.c" \
  "$MAIN_DIR/face_animator.c" \
  "$MAIN_DIR/face_avatar_registry.c" \
  "$MAIN_DIR/face_driver.c" \
  "$MAIN_DIR/face_spectral.c" \
  "$MAIN_DIR/face_viseme.c" \
  "$MAIN_DIR/face_geometry.c" \
  "$MAIN_DIR/face_keyframe.c" \
  "$MAIN_DIR/face_performance.c" \
  "$MAIN_DIR/face_sprite_sheet.c" \
  "$MAIN_DIR/face_sprite_mossling_generated.c" \
  "$MAIN_DIR/face_sprite_showcase.c" \
  "${AVATAR_SRCS[@]}" \
  "$MAIN_DIR/face_stage.c" \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createStackchanFaceModule \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMALLOC=emmalloc \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=2097152 \
  -sSTACK_SIZE=65536 \
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAP16']" \
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_stackchan_wasm_abi_version','_stackchan_wasm_keyframe_size','_stackchan_wasm_render_key_size','_stackchan_wasm_metrics_size','_stackchan_wasm_render_frame_bytes','_stackchan_wasm_stage_cue_size','_stackchan_wasm_render_profile_count','_stackchan_wasm_render_profile_slug','_stackchan_wasm_render_profile_name','_stackchan_wasm_render_profile_family_name','_stackchan_wasm_render_profile_info','_stackchan_wasm_create','_stackchan_wasm_destroy','_stackchan_wasm_push_pcm','_stackchan_wasm_snapshot','_stackchan_wasm_render','_stackchan_wasm_apply_stage_cue']" \
  -o "$OUTPUT_DIR/stackchan-face.mjs"

cp "$GRID_DIR/index.html" "$OUTPUT_DIR/index.html"
cp "$GRID_DIR/styles.css" "$OUTPUT_DIR/styles.css"
cp "$GRID_DIR/app.js" "$OUTPUT_DIR/app.js"
cp "$GRID_DIR/pcm-worklet.js" "$OUTPUT_DIR/pcm-worklet.js"
cp \
  "$MAIN_DIR/assets/head_audio_model_en_mixed.bin" \
  "$OUTPUT_DIR/head_audio_model_en_mixed.bin"

for voice in leo rex eve; do
  for suffix in wav json events.jsonl frames.jsonl; do
    source_path="$CAPTURE_DIR/$voice.$suffix"
    if test -f "$source_path"; then
      cp "$source_path" "$OUTPUT_DIR/audio/$voice.$suffix"
    fi
  done
done

printf 'WASM grid built at %s\n' "$OUTPUT_DIR"
