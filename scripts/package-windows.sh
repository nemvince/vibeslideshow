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

# Phase timings: the packaging step is one CI step and these are the numbers that
# say whether its time is going where it should.
PHASE=$SECONDS
phase() {
    echo "== $1: $((SECONDS - PHASE))s"
    PHASE=$SECONDS
}

STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.zip"
mkdir -p "$STAGE"
cp -f "$EXE" "$STAGE/"
cp -f README.md LICENSE "$STAGE/"

# Qt, and the plugins/QML modules it needs.
WINDEPLOYQT="$(command -v windeployqt.exe || command -v windeployqt || true)"
[ -n "$WINDEPLOYQT" ] || { echo "package-windows: windeployqt not found" >&2; exit 1; }
# opengl32sw.dll is kept on purpose: the app asks for the OpenGL backend, so on a
# machine whose GL driver is missing or refused (remote sessions, old GPUs, a
# CI runner with no GPU at all) Qt needs its own software renderer to fall back
# to. Without it that startup is a black window or nothing at all.
"$WINDEPLOYQT" --release --no-translations --no-compiler-runtime --no-system-d3d-compiler \
               --qmldir qml "$STAGE/vibeslideshow.exe"
phase "windeployqt"

# ffmpeg/ffprobe ride along: the app shells out to them, and an app started from
# Explorer never sees the user's PATH.
for tool in ffmpeg.exe ffprobe.exe; do
    src="$MINGW_PREFIX/bin/$tool"
    [ -f "$src" ] || { echo "package-windows: $src missing (pacman -S mingw-w64-x86_64-ffmpeg)" >&2; exit 1; }
    cp -f "$src" "$STAGE/"
done

# libmpv plus every non-system DLL it and ffmpeg pull in.
#
# A DLL that Windows ships is never bundled. This is not tidiness: MSYS2 keeps its
# own import shims for Windows components in its prefix, so ldd resolves them
# happily and the copy lands next to the exe, where it shadows the real DLL. That
# is what shipped in 1.0.1 — a DWrite.dll and a GLU32.dll from the prefix, neither
# of which exports what the system ones do — and the app died before its first
# frame with "unable to find an entry point in ...".
#
# The authoritative test is therefore the system itself: if %SystemRoot%\System32
# has a DLL of that name, the system provides it and the bundle must not.
SYSTEM32=""
if command -v cygpath >/dev/null 2>&1; then
    SYSTEM32="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}" 2>/dev/null || true)/System32"
fi

# Named as well, for the components that are not files of their own (the api-set
# forwarders) and for packaging runs that cannot see System32 at all.
is_system_dll() {
    local base="${1,,}"   # no subshell: this runs for every dependency of every DLL
    case "$base" in
        kernel32.dll|user32.dll|gdi32.dll|gdi32full.dll|shell32.dll|advapi32.dll|ole32.dll|oleaut32.dll|ws2_32.dll|mpr.dll|bcrypt.dll|bcryptprimitives.dll|dwmapi.dll|uxtheme.dll|winmm.dll|comdlg32.dll|imm32.dll|msvcrt.dll|shlwapi.dll|version.dll|d3d9.dll|d3d10.dll|d3d11.dll|d3d12.dll|d2d1.dll|dcomp.dll|d3dcompiler_47.dll|dxgi.dll|dxva2.dll|dwrite.dll|glu32.dll|opengl32.dll|gdiplus.dll|msimg32.dll|secur32.dll|crypt32.dll|ntdll.dll|rpcrt4.dll|setupapi.dll|cfgmgr32.dll|combase.dll|kernelbase.dll|apphelp.dll|authz.dll|avicap32.dll|avrt.dll|comctl32.dll|cryptbase.dll|dnsapi.dll|iphlpapi.dll|userenv.dll|powrprof.dll|psapi.dll|propsys.dll|oleacc.dll|sechost.dll|dbghelp.dll|shcore.dll|winhttp.dll|wininet.dll|urlmon.dll|wtsapi32.dll|mfplat.dll|mfreadwrite.dll|evr.dll|wmcodecdspuuid.dll|api-ms-win*|ucrtbase.dll|msvcp_win.dll) return 0 ;;
    esac
    [ -n "$SYSTEM32" ] && [ -f "$SYSTEM32/$1" ] && return 0
    return 1
}

# ldd exits non-zero when a dependency is unresolved, which under `set -e -o pipefail`
# would kill the script mid-loop before it can report anything.
#
# Parsed in bash rather than with awk, and paths split with `${x##*/}` rather than
# basename: this runs a few thousand times over the bundle, and a fork costs a
# millisecond or two on Windows — the first version spent 45 s of the CI job here.
deps_of() {
    local name arrow path rest
    while read -r name arrow path rest; do
        [ "$arrow" = "=>" ] || continue
        # ldd prints POSIX paths on MSYS2 and drive-letter paths elsewhere; accept
        # whatever actually exists.
        [ -f "$path" ] && printf '%s\n' "$path"
    done < <(ldd "$1" 2>/dev/null || true)
}

copy_deps() {
    local target="$1" dll name
    while IFS= read -r dll; do
        [ -n "$dll" ] || continue
        name="${dll##*/}"
        is_system_dll "$name" && continue
        [ -f "$STAGE/$name" ] || cp -f "$dll" "$STAGE/"
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
phase "DLLs"

# windeployqt reasons from the executable, so a dependency that only a *plugin*
# needs is left out: the Qt Quick Controls 2 plugin links Qt6Widgets, which this app
# never uses itself. That was the 1.0.0 "plugin qtquickcontrols2plugin not found".
WIDGETS="$MINGW_PREFIX/bin/Qt6Widgets.dll"
if [ -f "$WIDGETS" ] && [ ! -f "$STAGE/Qt6Widgets.dll" ]; then
    cp -f "$WIDGETS" "$STAGE/"
else
    echo "package-windows: warning: $WIDGETS missing, the Controls plugin may not load" >&2
fi

# Whatever Windows ships must not be in here, under any name. Checked against the
# real %SystemRoot%\System32 rather than a list, because the list is what failed.
if [ -n "$SYSTEM32" ] && [ -d "$SYSTEM32" ]; then
    for bundled in "$STAGE"/*.dll; do
        [ -f "$bundled" ] || continue
        name="$(basename "$bundled")"
        if [ -f "$SYSTEM32/$name" ]; then
            echo "package-windows: bundle contains the Windows system DLL $name" >&2
            exit 1
        fi
    done
else
    echo "package-windows: warning: cannot read %SYSTEMROOT%\System32, skipping the system-DLL check" >&2
fi

# Every import of every staged binary must be either bundled here or provided by
# Windows. A plugin whose dependency is missing does not say which one: Qt reports
# it as "plugin not found", which is true and useless. This names the DLL.
missing=""
check_imports() {
    local file="$1" imp
    while read -r imp; do
        [ -n "$imp" ] || continue
        [ -f "$STAGE/$imp" ] && continue
        [ -n "$SYSTEM32" ] && [ -f "$SYSTEM32/$imp" ] && continue
        missing+="  ${file#"$STAGE"/} needs $imp"$'\n'
    done < <(objdump -p "$file" 2>/dev/null | awk '/DLL Name:/{print $3}')
}
while IFS= read -r binary; do
    check_imports "$binary"
done < <(find "$STAGE" -maxdepth 4 \( -name '*.dll' -o -name '*.exe' \) -type f)
if [ -n "$missing" ]; then
    echo "package-windows: the bundle is missing dependencies:" >&2
    printf '%s' "$missing" >&2
    exit 1
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
    # -mx1: the store is already-compressed media plus a hundred DLLs, so a high
    # level spends minutes to save a few megabytes.
    7z a -tzip -mx1 -bso0 -bsp0 "dist/$NAME.zip" "$STAGE" >/dev/null
elif command -v zip >/dev/null 2>&1; then
    (cd dist && zip -qr "$NAME.zip" "$NAME")
else
    echo "package-windows: need 7z or zip" >&2; exit 1
fi
phase "zip"
echo "== dist/$NAME.zip"
