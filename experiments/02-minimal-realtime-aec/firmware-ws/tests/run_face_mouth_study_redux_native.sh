#!/usr/bin/env bash
set -euo pipefail

script_directory="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1
    pwd
)"
firmware_directory="$(cd -- "${script_directory}/.." && pwd)"
main_directory="${firmware_directory}/main"
artifact_directory="${script_directory}/artifacts/face_mouth_study_redux"
build_directory="${TMPDIR:-/tmp}/stackchan-face-mouth-study-redux"

mkdir -p "${artifact_directory}" "${build_directory}"

compiler="${CC:-clang}"
common_flags=(
    -std=c11
    -O2
    -Wall
    -Wextra
    -Werror
    -pedantic
    -DSTACKCHAN_HOST_TEST
    "-I${main_directory}"
)
sources=("${main_directory}/face_mouth_study_redux.c")

"${compiler}" "${common_flags[@]}" \
    "${sources[@]}" \
    "${script_directory}/face_mouth_study_redux_test.c" \
    -o "${build_directory}/test"
"${build_directory}/test"

"${compiler}" "${common_flags[@]}" -O1 -g \
    -fno-omit-frame-pointer -fsanitize=address \
    "${sources[@]}" \
    "${script_directory}/face_mouth_study_redux_test.c" \
    -o "${build_directory}/test-asan"
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" \
    "${build_directory}/test-asan"

"${compiler}" "${common_flags[@]}" -O1 -g \
    -fno-omit-frame-pointer -fsanitize=undefined \
    "${sources[@]}" \
    "${script_directory}/face_mouth_study_redux_test.c" \
    -o "${build_directory}/test-ubsan"
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
    "${build_directory}/test-ubsan"

"${compiler}" "${common_flags[@]}" \
    "${sources[@]}" \
    "${script_directory}/face_mouth_study_redux_dump.c" \
    -o "${build_directory}/dump"
"${build_directory}/dump" "${artifact_directory}"

"${compiler}" "${common_flags[@]}" \
    "${sources[@]}" \
    "${script_directory}/face_mouth_study_redux_benchmark.c" \
    -o "${build_directory}/benchmark"
"${build_directory}/benchmark"

if command -v sips >/dev/null 2>&1; then
    for ppm in "${artifact_directory}"/*.ppm; do
        sips -s format png "${ppm}" \
            --out "${ppm%.ppm}.png" >/dev/null
    done
else
    printf '%s\n' \
        "sips not available; inspect the generated PPM artifacts directly"
fi

printf '%s\n' \
    "face_mouth_study_redux: native, ASan, UBSan, dump, and benchmark passed"
