#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
main_dir=$(CDPATH= cd -- "$script_dir/../main" && pwd)
output_dir=${1:-/tmp/stackchan-face-closeup-toon-actors}
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/face-closeup-toon-build.XXXXXX")

cleanup() {
    rm -rf -- "$build_dir"
}
trap cleanup EXIT HUP INT TERM

mkdir -p -- "$output_dir"

common_flags="-std=c11 -Wall -Wextra -Wpedantic -Werror"

clang $common_flags -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$main_dir" \
    "$main_dir/face_closeup_toon_actors.c" \
    "$script_dir/face_closeup_toon_actors_test.c" \
    -o "$build_dir/face_closeup_toon_actors_test"

ASAN_OPTIONS=halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$build_dir/face_closeup_toon_actors_test" \
    >"$output_dir/sanitizer-test.txt"
cat "$output_dir/sanitizer-test.txt"

clang $common_flags -O2 \
    -I"$main_dir" \
    "$main_dir/face_closeup_toon_actors.c" \
    "$script_dir/face_closeup_toon_actors_dump.c" \
    -o "$build_dir/face_closeup_toon_actors_dump"
"$build_dir/face_closeup_toon_actors_dump" "$output_dir"

clang $common_flags -O3 \
    -I"$main_dir" \
    "$main_dir/face_closeup_toon_actors.c" \
    "$script_dir/face_closeup_toon_actors_bench.c" \
    -o "$build_dir/face_closeup_toon_actors_bench"
benchmark_result=$("$build_dir/face_closeup_toon_actors_bench")
printf '%s\n' "$benchmark_result" >"$output_dir/benchmark.txt"
printf '%s\n' "$benchmark_result"

clang $common_flags -Os -ffunction-sections -fdata-sections \
    -I"$main_dir" \
    -c "$main_dir/face_closeup_toon_actors.c" \
    -o "$build_dir/face_closeup_toon_actors.o"

if nm -u "$build_dir/face_closeup_toon_actors.o" |
    grep -E '_(malloc|calloc|realloc|free)$' >/dev/null; then
    echo "renderer object unexpectedly references heap allocation" >&2
    exit 1
fi

{
    echo "Optimized standalone renderer object:"
    wc -c "$build_dir/face_closeup_toon_actors.o"
    echo
    echo "Platform size breakdown:"
    size "$build_dir/face_closeup_toon_actors.o"
    echo
    echo "Undefined symbols (no heap allocator references permitted):"
    nm -u "$build_dir/face_closeup_toon_actors.o"
} >"$output_dir/code-size.txt"

if command -v ffmpeg >/dev/null 2>&1; then
    for ppm in "$output_dir"/*.ppm; do
        png=${ppm%.ppm}.png
        ffmpeg -y -loglevel error -i "$ppm" "$png"
    done
fi

printf 'native close-up/toon artifacts ready: %s\n' "$output_dir"
