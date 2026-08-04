#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
main_dir=$(CDPATH= cd -- "$script_dir/../main" && pwd)
output_dir=${1:-/tmp/stackchan-face-pixel-redux-variants}
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/face-pixel-variants-build.XXXXXX")

cleanup() {
    rm -rf -- "$build_dir"
}
trap cleanup EXIT HUP INT TERM

mkdir -p -- "$output_dir"

common_flags="-std=c11 -Wall -Wextra -Wpedantic -Werror"
renderer_sources="
    $main_dir/face_pixel_redux_actors.c
    $main_dir/face_pixel_redux_variants.c
"

clang $common_flags -O1 -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$main_dir" \
    $renderer_sources \
    "$script_dir/face_pixel_redux_variants_test.c" \
    -o "$build_dir/face_pixel_redux_variants_test"

ASAN_OPTIONS=halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$build_dir/face_pixel_redux_variants_test" \
    >"$output_dir/sanitizer-test.txt"
cat "$output_dir/sanitizer-test.txt"

clang $common_flags -O2 \
    -I"$main_dir" \
    $renderer_sources \
    "$script_dir/face_pixel_redux_variants_dump.c" \
    -o "$build_dir/face_pixel_redux_variants_dump"
"$build_dir/face_pixel_redux_variants_dump" "$output_dir"

clang $common_flags -O3 \
    -I"$main_dir" \
    $renderer_sources \
    "$script_dir/face_pixel_redux_variants_bench.c" \
    -o "$build_dir/face_pixel_redux_variants_bench"
benchmark_result=$("$build_dir/face_pixel_redux_variants_bench")
printf '%s\n' "$benchmark_result" >"$output_dir/benchmark.txt"
printf '%s\n' "$benchmark_result"

clang $common_flags -Os -ffunction-sections -fdata-sections \
    -I"$main_dir" \
    -c "$main_dir/face_pixel_redux_variants.c" \
    -o "$build_dir/face_pixel_redux_variants.o"

if nm -u "$build_dir/face_pixel_redux_variants.o" |
    grep -E '_(malloc|calloc|realloc|free)$' >/dev/null; then
    echo "variant renderer unexpectedly references heap allocation" >&2
    exit 1
fi

{
    echo "Optimized standalone variant renderer object:"
    wc -c "$build_dir/face_pixel_redux_variants.o"
    echo
    echo "Platform size breakdown:"
    size "$build_dir/face_pixel_redux_variants.o"
    echo
    echo "Undefined symbols (base resolver is expected; heap is not):"
    nm -u "$build_dir/face_pixel_redux_variants.o"
} >"$output_dir/code-size.txt"

if command -v ffmpeg >/dev/null 2>&1; then
    for ppm in "$output_dir"/*.ppm; do
        png=${ppm%.ppm}.png
        ffmpeg -y -loglevel error -i "$ppm" -frames:v 1 -update 1 "$png"
    done

    # Device-scale nearest-neighbour videos for the six strict-DMG actors.
    for ppm in \
        "$output_dir"/1[2-7]-pixel-variant-*-speech-blink-24f.ppm; do
        mp4=${ppm%.ppm}-native160-nearest4x-10fps.mp4
        ffmpeg -y -loglevel error \
            -loop 1 -framerate 30 -i "$ppm" \
            -vf "crop=160:120:x='min(23,floor(n/3))*160':y=0,scale=640:480:flags=neighbor" \
            -frames:v 72 -an -c:v libx264 -pix_fmt yuv420p "$mp4"
    done
    for ppm in \
        "$output_dir"/1[2-7]-pixel-variant-*-idle-turn-blink-32f.ppm; do
        mp4=${ppm%.ppm}-native160-nearest4x-10fps.mp4
        ffmpeg -y -loglevel error \
            -loop 1 -framerate 30 -i "$ppm" \
            -vf "crop=160:120:x='min(31,floor(n/3))*160':y=0,scale=640:480:flags=neighbor" \
            -frames:v 96 -an -c:v libx264 -pix_fmt yuv420p "$mp4"
    done
fi

python3 "$script_dir/review_face_pixel_redux_variants.py" "$output_dir"

if command -v ffmpeg >/dev/null 2>&1; then
    for ppm in "$output_dir"/labelled-*.ppm; do
        png=${ppm%.ppm}.png
        ffmpeg -y -loglevel error -i "$ppm" -frames:v 1 -update 1 "$png"
    done
fi

printf 'native variant artifacts ready: %s\n' "$output_dir"
