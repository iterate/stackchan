#!/usr/bin/env bash
# Build and test the fable_mouth_geometry contribution on the host.
#
#   tools/build.sh            build + run sprite check, tests, benchmark
#   tools/build.sh golden     additionally regenerate test/golden_crc.inc
#   tools/build.sh preview    additionally render preview/montage.ppm (+png)
#   tools/build.sh wasm       additionally verify wasm/native byte identity
#                             (skipped politely if emcc is not installed)
set -euo pipefail
cd "$(dirname "$0")/.."

CFLAGS="-std=c11 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion"
SRCS="src/*.c"
mkdir -p build

python3 tools/check_sprites.py

cc $CFLAGS $SRCS test/test_fmg.c -o build/test_fmg

if [[ "${1:-}" == "golden" ]]; then
    ./build/test_fmg --update-golden
    cc $CFLAGS $SRCS test/test_fmg.c -o build/test_fmg
fi

./build/test_fmg

cc $CFLAGS $SRCS test/bench_fmg.c -o build/bench_fmg
./build/bench_fmg

if [[ "${1:-}" == "preview" ]]; then
    cc $CFLAGS $SRCS tools/render_preview.c -o build/render_preview
    mkdir -p preview
    ./build/render_preview preview anim
    if command -v sips >/dev/null 2>&1; then
        sips -s format png preview/montage.ppm --out preview/montage.png \
            >/dev/null
        echo "wrote preview/montage.png"
    fi
fi

if [[ "${1:-}" == "wasm" ]]; then
    if ! command -v emcc >/dev/null 2>&1; then
        echo "emcc not found - skipping wasm parity check"
        exit 0
    fi
    # parent face-grid package.json declares "type: module", so emit .cjs
    emcc -std=c11 -O2 $SRCS test/test_fmg.c -o build/test_fmg.js
    mv build/test_fmg.js build/test_fmg.cjs
    node build/test_fmg.cjs
    echo "wasm parity: same golden table passed under node"
fi
