#!/usr/bin/env bash
# Turn a build into a distributable bundle for this OS.
#
#   ./scripts/package.sh               # detect the OS, build, package
#   ./scripts/package.sh --no-build    # package ./build as it stands
#   ./scripts/package.sh --linux       # force a target (linux | macos | windows)
#
# Artifacts land in ./dist. Everything is self-contained except the platform's own
# system libraries: Qt and libmpv are bundled where the platform expects them, and
# ffmpeg/ffprobe travel next to the executable.
set -euo pipefail

cd "$(dirname "$0")/.."

TARGET=""
PASSTHRU=()

while [ $# -gt 0 ]; do
    case "$1" in
        --linux|--macos|--windows) TARGET="${1#--}"; shift ;;
        *) PASSTHRU+=("$1"); shift ;;
    esac
done

if [ -z "$TARGET" ]; then
    case "$(uname -s)" in
        Linux*)                 TARGET=linux ;;
        Darwin*)                TARGET=macos ;;
        MINGW*|MSYS*|CYGWIN*)   TARGET=windows ;;
        *) echo "package.sh: unsupported platform $(uname -s)" >&2; exit 2 ;;
    esac
fi

# Invoke through bash: a Windows checkout has no executable bit, and macOS ships
# bash 3.2, where "${empty[@]}" under `set -u` is an error.
case "$TARGET" in
    linux)   exec bash ./scripts/package-linux.sh ${PASSTHRU[@]+"${PASSTHRU[@]}"} ;;
    macos)   exec bash ./scripts/package-macos.sh ${PASSTHRU[@]+"${PASSTHRU[@]}"} ;;
    windows) exec bash ./scripts/package-windows.sh ${PASSTHRU[@]+"${PASSTHRU[@]}"} ;;
    *) echo "package.sh: unknown target $TARGET" >&2; exit 2 ;;
esac
