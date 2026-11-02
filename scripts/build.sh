#!/usr/bin/env bash
# Configure and build vibeslideshow.
#
#   ./scripts/build.sh                 # Release build in ./build
#   ./scripts/build.sh --debug         # Debug build
#   ./scripts/build.sh --dir build-ci  # somewhere else
#
# Honours CMAKE_PREFIX_PATH, which is how CI points at a Qt that is newer than the
# distribution's (Ubuntu 24.04 still ships Qt 6.4; this app needs 6.5).
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build"
BUILD_TYPE="Release"

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --debug)   BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --dir)     BUILD_DIR="${2:?--dir needs a path}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *)         echo "build.sh: unknown option $1" >&2; exit 2 ;;
    esac
done

JOBS="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi

echo "== configuring ($BUILD_TYPE) in $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" "${GENERATOR[@]}" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "== building with $JOBS jobs"
cmake --build "$BUILD_DIR" -j "$JOBS"

echo "== built: $BUILD_DIR"
