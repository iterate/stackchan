#!/bin/sh
set -eu

fv_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$fv_root"

fv_build=build/favourite
fv_preview=preview/favourite-artist-pass
mkdir -p "$fv_build" "$fv_preview"

fv_sources="src/fea_math.c
src/fea_solve.c
src/fea_draw.c
src/fea_favourite_variants_common.c
src/fea_favourite_wisps.c
src/fea_favourite_karakuri.c
src/fea_favourite_stickers.c
src/fea_favourite_variants.c"
fv_common_flags="-std=c11 -g -Wall -Wextra -Werror -Wshadow
-Wconversion -Wno-sign-conversion -Wdouble-promotion
-Isrc -Icompat -Itests"

cc $fv_common_flags -O2 tests/test_fea_favourite_variants.c \
    $fv_sources compat/face_stage.c \
    -o "$fv_build/test_fea_favourite_variants"
"$fv_build/test_fea_favourite_variants"

cc $fv_common_flags -O1 -fsanitize=address,undefined \
    -fno-sanitize-recover=all tests/test_fea_favourite_variants.c \
    $fv_sources compat/face_stage.c \
    -o "$fv_build/test_fea_favourite_variants_asan"
"$fv_build/test_fea_favourite_variants_asan"

cc $fv_common_flags -O0 tests/test_fea_favourite_variants.c \
    $fv_sources compat/face_stage.c \
    -o "$fv_build/test_fea_favourite_variants_o0"
"$fv_build/test_fea_favourite_variants_o0" --dump-hashes \
    > "$fv_build/hashes_o0.txt"
"$fv_build/test_fea_favourite_variants" --dump-hashes \
    > "$fv_build/hashes_o2.txt"
cmp "$fv_build/hashes_o0.txt" "$fv_build/hashes_o2.txt"

cc $fv_common_flags -O2 tools/fea_favourite_variants_dump.c \
    $fv_sources compat/face_stage.c \
    -o "$fv_build/fea_favourite_variants_dump"
"$fv_build/fea_favourite_variants_dump" sheets "$fv_preview"
for generated in "$fv_preview"/expression-actors-v4__*.ppm; do
    [ -f "$generated" ] || continue
    filename=${generated##*/}
    suffix=${filename#expression-actors-v4}
    mv -f "$generated" "$fv_preview/favourite-artist-pass$suffix"
done

if command -v sips >/dev/null 2>&1; then
    for ppm in "$fv_preview"/*.ppm; do
        [ -f "$ppm" ] || continue
        sips -s format png "$ppm" --out "${ppm%.ppm}.png" >/dev/null
    done
fi

if command -v emcc >/dev/null 2>&1 &&
    command -v node >/dev/null 2>&1; then
    emcc $fv_common_flags -g0 -O2 -sENVIRONMENT=node \
        -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
        tests/test_fea_favourite_variants.c \
        $fv_sources compat/face_stage.c \
        -o "$fv_build/test_fea_favourite_variants_wasm.js"
    mv -f "$fv_build/test_fea_favourite_variants_wasm.js" \
        "$fv_build/test_fea_favourite_variants_wasm.cjs"
    node "$fv_build/test_fea_favourite_variants_wasm.cjs" \
        --dump-hashes > "$fv_build/hashes_wasm.txt"
    cmp "$fv_build/hashes_o2.txt" "$fv_build/hashes_wasm.txt"
    node "$fv_build/test_fea_favourite_variants_wasm.cjs"
fi

echo "favourite variants: native, sanitizers, O0/O2, previews and WASM passed"
