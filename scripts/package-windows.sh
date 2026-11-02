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
# UCRT64 is the environment MSYS2 still builds mpv for; MINGW_PREFIX is set by the
# msys2 shell, this is only the fallback.
MINGW_PREFIX="${MINGW_PREFIX:-/ucrt64}"

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
# DLLs that come from Windows itself, not from the bundle.
is_system_dll() {
    case "$(printf '%s' "$1" | tr 'A-Z' 'a-z')" in
        kernel32.dll|user32.dll|gdi32.dll|shell32.dll|advapi32.dll|ole32.dll|oleaut32.dll|ws2_32.dll|mpr.dll|bcrypt.dll|dwmapi.dll|uxtheme.dll|winmm.dll|comdlg32.dll|imm32.dll|msvcrt.dll|shlwapi.dll|version.dll|d3d11.dll|d3d9.dll|dxgi.dll|secur32.dll|crypt32.dll|ntdll.dll|rpcrt4.dll|setupapi.dll|api-ms-win*|ucrtbase.dll|msvcp_win.dll) return 0 ;;
    esac
    return 1
}

# ldd exits non-zero when a dependency is unresolved, which under `set -e -o pipefail`
# would kill the script mid-loop before it can report anything.
deps_of() {
    ldd "$1" 2>/dev/null | awk '/=> \//{print $3}' || true
}

copy_deps() {
    local target="$1" dll
    while IFS= read -r dll; do
        [ -n "$dll" ] || continue
        is_system_dll "$(basename "$dll")" && continue
        [ -f "$STAGE/$(basename "$dll")" ] || cp -f "$dll" "$STAGE/"
    done < <(deps_of "$target")
}
for dll in "$MINGW_PREFIX/bin/libmpv"*.dll; do
    [ -f "$dll" ] || { echo "package-windows: no libmpv in $MINGW_PREFIX/bin" >&2; exit 1; }
    cp -f "$dll" "$STAGE/"
    copy_deps "$dll"
done
copy_deps "$STAGE/ffmpeg.exe"
copy_deps "$STAGE/ffprobe.exe"
copy_deps "$STAGE/vibeslideshow.exe"

# windeployqt reasons from the executable, so a dependency that only a *plugin*
# needs is left out: the Qt Quick Controls 2 plugin links Qt6Widgets, which this app
# never uses itself. That was the 1.0.0 "plugin qtquickcontrols2plugin not found".
WIDGETS="$MINGW_PREFIX/bin/Qt6Widgets.dll"
if [ -f "$WIDGETS" ] && [ ! -f "$STAGE/Qt6Widgets.dll" ]; then
    cp -f "$WIDGETS" "$STAGE/"
else
    echo "package-windows: warning: $WIDGETS missing, the Controls plugin may not load" >&2
fi

# Assert the pieces this bundle has broken on before, so a regression fails the
# packaging step with a clear message instead of shipping a dead download.
for required in \
    Qt6Widgets.dll \
    Qt6QuickControls2.dll \
    qml/QtQuick/Controls/qtquickcontrols2plugin.dll \
    qml/QtQuick/Controls/qmldir \
    qml/QtQuick/qmldir; do
    if [ ! -f "$STAGE/$required" ]; then
        echo "package-windows: bundle is missing $required" >&2
        exit 1
    fi
done

if command -v 7z >/dev/null 2>&1; then
    7z a -tzip -bso0 -bsp0 "dist/$NAME.zip" "$STAGE" >/dev/null
elif command -v zip >/dev/null 2>&1; then
    (cd dist && zip -qr "$NAME.zip" "$NAME")
else
    echo "package-windows: need 7z or zip" >&2; exit 1
fi
echo "== dist/$NAME.zip"
