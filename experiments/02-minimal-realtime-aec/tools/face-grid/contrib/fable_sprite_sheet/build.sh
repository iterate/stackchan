#!/usr/bin/env bash
# Build pipeline: generate demo sheets, convert to C atlases, compile
# the native test binary, run the tests.
#
# The firmware headers (face_keyframe.h / face_pose.h) are located via
# STACKCHAN_MAIN_DIR, defaulting to the in-repo relative path.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAIN_DIR="${STACKCHAN_MAIN_DIR:-$HERE/../../../../firmware-ws/main}"

if ! test -f "$MAIN_DIR/face_keyframe.h"; then
  echo "error: face_keyframe.h not found in $MAIN_DIR" >&2
  echo "set STACKCHAN_MAIN_DIR to the firmware-ws/main directory" >&2
  exit 1
fi

python3 "$HERE/tools/gen_demo_sheets.py"

for face in ega_sorcerer handheld_gobbo vga_navigator \
    terminal_operator; do
  python3 "$HERE/tools/atlas_convert.py" \
    --manifest "$HERE/assets/$face/manifest.json" \
    --out-dir "$HERE/generated" \
    --stats "$HERE/generated/$face.stats.json"
done

mkdir -p "$HERE/build"

cc \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -Werror \
  -I"$HERE/src" \
  -I"$HERE/generated" \
  -I"$MAIN_DIR" \
  "$HERE/src/sprite_face.c" \
  "$HERE/generated/ega_sorcerer_atlas.c" \
  "$HERE/generated/handheld_gobbo_atlas.c" \
  "$HERE/generated/vga_navigator_atlas.c" \
  "$HERE/generated/terminal_operator_atlas.c" \
  "$HERE/tests/scenario.c" \
  "$HERE/tests/crc32.c" \
  "$HERE/tests/test_main.c" \
  -o "$HERE/build/test_sprite_face"

# Converter self-tests, including the Aseprite ingestion path; the
# synthetic atlas it emits must satisfy the engine's validator too.
python3 "$HERE/tools/test_converter.py"
cc \
  -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$HERE/src" \
  -I"$HERE/build/aseprite_demo" \
  -I"$MAIN_DIR" \
  "$HERE/src/sprite_face.c" \
  "$HERE/build/aseprite_demo/ase_hero_atlas.c" \
  "$HERE/tests/ase_init_check.c" \
  -o "$HERE/build/ase_init_check"
"$HERE/build/ase_init_check"

# The engine object must stay allocation-free: no malloc family, no
# stdio, nothing but memcpy/memset from the C library.
cc -std=c11 -O2 -I"$HERE/src" -I"$MAIN_DIR" \
  -c "$HERE/src/sprite_face.c" -o "$HERE/build/sprite_face.o"
if nm -u "$HERE/build/sprite_face.o" | \
    grep -E "malloc|calloc|realloc|free|printf|fopen" >/dev/null; then
  echo "error: sprite_face.o references allocation or stdio" >&2
  nm -u "$HERE/build/sprite_face.o" >&2
  exit 1
fi

"$HERE/build/test_sprite_face" --golden-dir "$HERE/tests" "$@"
