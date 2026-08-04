#!/usr/bin/env bash
# Full verification: warnings-as-errors build, unit tests, UBSan+ASan
# rerun, float poisoning, and a token audit of the library sources.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

echo "== unit tests (optimized) =="
make -s clean
make -s test

echo "== unit tests (UBSan + ASan) =="
make -s clean
make -s test CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow \
    -Wdouble-promotion -Wfloat-equal \
    -fsanitize=undefined,address -fno-sanitize-recover=all"

echo "== float poisoning =="
make -s poison

echo "== float token audit (library sources, comments stripped) =="
if perl -0777 -pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g' src/*.c src/*.h |
    grep -wE 'float|double'; then
    echo "floating point token found in library sources" >&2
    exit 1
fi
echo "clean"

echo "== rebuild optimized artifacts =="
make -s clean
make -s test >/dev/null

echo "== wasm byte-identity (optional: needs emcc + node) =="
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    emcc -std=c11 -O2 -Wall -Wextra -Werror \
        -o out/test_studies.mjs tests/test_studies.c src/fable_ease.c \
        src/fable_motion.c src/fable_studies.c
    # The golden CRCs baked into test_studies.c were generated natively;
    # a passing wasm run proves byte-identical frames through wasm32.
    node out/test_studies.mjs
else
    echo "skipped (emcc or node not found)"
fi

echo "ALL CHECKS PASSED"
