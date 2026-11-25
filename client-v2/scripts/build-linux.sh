#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$source_dir/build-linux"}
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DRUTOOL_STATIC_ANALYSIS=OFF \
    -DRUTOOL_WARNINGS_AS_ERRORS=ON "${@:2}"
cmake --build "$build_dir" --parallel "${RUTOOL_BUILD_JOBS:-4}"
if grep -q '^BUILD_TESTING:BOOL=ON$' "$build_dir/CMakeCache.txt"; then
    ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
fi
