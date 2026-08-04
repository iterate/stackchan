#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
main_dir=$(CDPATH= cd -- "$script_dir/../main" && pwd)
output_dir=${1:-"$script_dir/artifacts/face_abstract_redux_actors"}
build_dir=$(mktemp -d \
    "${TMPDIR:-/tmp}/face-abstract-redux-build.XXXXXX")

cleanup() {
    rm -rf -- "$build_dir"
}
trap cleanup EXIT HUP INT TERM

mkdir -p -- "$output_dir"

common_flags="-std=c11 -Wall -Wextra -Wpedantic -Werror \
-Wconversion -Wsign-conversion -Wshadow"
source_file="$main_dir/face_abstract_redux.c"
stage_source="$main_dir/face_stage.c"
test_file="$script_dir/face_abstract_redux_actors_test.c"

clang $common_flags -Wno-sign-conversion -O2 -I"$main_dir" \
    -c "$stage_source" \
    -o "$build_dir/face-stage-native.o"
clang $common_flags -O2 -I"$main_dir" \
    "$source_file" "$test_file" \
    "$build_dir/face-stage-native.o" \
    -o "$build_dir/native-test"
"$build_dir/native-test"

clang $common_flags -Wno-sign-conversion -O1 -g \
    -fsanitize=address -fno-omit-frame-pointer \
    -I"$main_dir" \
    -c "$stage_source" \
    -o "$build_dir/face-stage-asan.o"
clang $common_flags -O1 -g \
    -fsanitize=address -fno-omit-frame-pointer \
    -I"$main_dir" \
    "$source_file" "$test_file" \
    "$build_dir/face-stage-asan.o" \
    -o "$build_dir/asan-test"
ASAN_OPTIONS=halt_on_error=1 \
    "$build_dir/asan-test"

clang $common_flags -Wno-sign-conversion -O1 -g \
    -fsanitize=undefined -fno-omit-frame-pointer \
    -I"$main_dir" \
    -c "$stage_source" \
    -o "$build_dir/face-stage-ubsan.o"
clang $common_flags -O1 -g \
    -fsanitize=undefined -fno-omit-frame-pointer \
    -I"$main_dir" \
    "$source_file" "$test_file" \
    "$build_dir/face-stage-ubsan.o" \
    -o "$build_dir/ubsan-test"
UBSAN_OPTIONS=halt_on_error=1 \
    "$build_dir/ubsan-test"

clang $common_flags -O2 -I"$main_dir" \
    "$source_file" \
    "$script_dir/face_abstract_redux_actors_dump.c" \
    -o "$build_dir/dump"
"$build_dir/dump" "$output_dir"

clang $common_flags -O3 -I"$main_dir" \
    "$source_file" \
    "$script_dir/face_abstract_redux_actors_bench.c" \
    -o "$build_dir/bench"
"$build_dir/bench"

clang $common_flags -Os -ffunction-sections -fdata-sections \
    -I"$main_dir" \
    -c "$source_file" \
    -o "$build_dir/face_abstract_redux.o"

if nm -u "$build_dir/face_abstract_redux.o" |
    rg '_(malloc|calloc|realloc|free)$' >/dev/null; then
    echo "renderer object unexpectedly references heap allocation" >&2
    exit 1
fi

if command -v ffmpeg >/dev/null 2>&1; then
    for ppm in "$output_dir"/*.ppm; do
        png=${ppm%.ppm}.png
        ffmpeg -y -loglevel error -i "$ppm" "$png"
    done
fi

printf '%s\n' \
    "face_abstract_redux_actors: native, ASan, UBSan, dump, and benchmark passed"
