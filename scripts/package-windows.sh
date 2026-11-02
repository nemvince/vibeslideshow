#!/usr/bin/env bash
# Windows bundle: run inside an MSYS2 MINGW64 shell (that is what CI does).
# Produces a zip with the exe, Qt, libmpv and ffmpeg/ffprobe beside it.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build"
SKIP_BUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) SKIP_BUILD=1; shift ;;
        --dir)      BUILD_DIR="${2:?--dir needs a path}"; shift 2 ;;
        *)          echo "package-windows: unknown option $1" >&2; exit 2 ;;
    esac
done

VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "package-windows: cannot read VERSION from CMakeLists.txt" >&2; exit 1; }
NAME="vibeslideshow-$VERSION-windows-x86_64"
MINGW_PREFIX="${MINGW_PREFIX:-/mingw64}"

[ "$SKIP_BUILD" = 1 ] || bash ./scripts/build.sh --dir "$BUILD_DIR" --release

EXE="$BUILD_DIR/vibeslideshow.exe"
[ -f "$EXE" ] || { echo "package-windows: $EXE missing" >&2; exit 1; }

STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE"
cp -f "$EXE" "$STAGE/"
cp -f README.md LICENSE "$STAGE/"

# Qt, and the plugins/QML modules it needs.
WINDEPLOYQT="$(command -v windeployqt.exe || command -v windeployqt || true)"
[ -n "$WINDEPLOYQT" ] || { echo "package-windows: windeployqt not found" >&2; exit 1; }
"$WINDEPLOYQT" --release --no-translations --no-compiler-runtime --no-system-d3d-compiler \
               --no-opengl-sw --qmldir qml "$STAGE/vibeslideshow.exe"

# ffmpeg/ffprobe ride along: the app shells out to them, and an app started from
# Explorer never sees the user's PATH.
for tool in ffmpeg.exe ffprobe.exe; do
    src="$MINGW_PREFIX/bin/$tool"
    [ -f "$src" ] || { echo "package-windows: $src missing (pacman -S mingw-w64-x86_64-ffmpeg)" >&2; exit 1; }
    cp -f "$src" "$STAGE/"
done

# libmpv plus every non-system DLL it and ffmpeg pull in.
copy_deps() {
    local target="$1"
    ldd "$target" 2>/dev/null | awk '/=> \//{print $3}' | while read -r dll; do
        case "$(basename "$dll")" in
            KERNEL32.dll|USER32.dll|GDI32.dll|SHELL32.dll|ADVAPI32.dll|ole32.dll|OLEAUT32.dll|WS2_32.dll|mpr.dll|bcrypt.dll|dwmapi.dll|uxtheme.dll|WINMM.dll|COMDLG32.dll|IMM32.dll|msvcrt.dll|SHLWAPI.dll|VERSION.dll|d3d11.dll|D3D9.dll|dxgi.dll|secur32.dll|CRYPT32.dll|ntdll.dll|RPCRT4.dll|SETUPAPI.dll|api-ms-win*) ;;
            *) [ -f "$STAGE/$(basename "$dll")" ] || cp -f "$dll" "$STAGE/" ;;
        esac
    done
}
for dll in "$MINGW_PREFIX/bin/libmpv"*.dll; do
    [ -f "$dll" ] || { echo "package-windows: no libmpv in $MINGW_PREFIX/bin" >&2; exit 1; }
    cp -f "$dll" "$STAGE/"
    copy_deps "$dll"
done
copy_deps "$STAGE/ffmpeg.exe"
copy_deps "$STAGE/ffprobe.exe"
copy_deps "$STAGE/vibeslideshow.exe"

if command -v 7z >/dev/null 2>&1; then
    7z a -tzip -bso0 -bsp0 "dist/$NAME.zip" "$STAGE" >/dev/null
elif command -v zip >/dev/null 2>&1; then
    (cd dist && zip -qr "$NAME.zip" "$NAME")
else
    echo "package-windows: need 7z or zip" >&2; exit 1
fi
echo "== dist/$NAME.zip"
