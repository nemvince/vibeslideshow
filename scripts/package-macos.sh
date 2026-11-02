#!/usr/bin/env bash
# macOS bundle: vibeslideshow.app with Qt and libmpv inside, zip + dmg.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build"
SKIP_BUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) SKIP_BUILD=1; shift ;;
        --dir)      BUILD_DIR="${2:?--dir needs a path}"; shift 2 ;;
        *)          echo "package-macos: unknown option $1" >&2; exit 2 ;;
    esac
done

VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "package-macos: cannot read VERSION from CMakeLists.txt" >&2; exit 1; }
NAME="vibeslideshow-$VERSION-macos"

[ "$SKIP_BUILD" = 1 ] || bash ./scripts/build.sh --dir "$BUILD_DIR" --release

# CMake may produce a bundle or a plain executable depending on how the target is
# set up; handle both.
APP="$BUILD_DIR/vibeslideshow.app"
if [ -d "$APP" ]; then
    MACOS_DIR="$APP/Contents/MacOS"
    FRAMEWORKS="$APP/Contents/Frameworks"
else
    PLAIN="$BUILD_DIR/vibeslideshow"
    [ -x "$PLAIN" ] || { echo "package-macos: neither $APP nor $PLAIN exists" >&2; exit 1; }
    echo "== no .app bundle; packaging $PLAIN directly"
    mkdir -p "dist/vibeslideshow-$VERSION-macos"
    cp -f "$PLAIN" "dist/vibeslideshow-$VERSION-macos/vibeslideshow"
    MACOS_DIR="dist/vibeslideshow-$VERSION-macos"
    FRAMEWORKS="$MACOS_DIR/lib"
    mkdir -p "$FRAMEWORKS"
fi

# ---- 1. ffmpeg/ffprobe next to the binary --------------------------------
# The app shells out to them, and a bundle launched from the Finder has no PATH.
for tool in ffmpeg ffprobe; do
    src="$(command -v "$tool" || true)"
    [ -n "$src" ] || { echo "package-macos: $tool not on PATH (brew install ffmpeg)" >&2; exit 1; }
    src_dir="$(cd "$(dirname "$src")" && pwd -P)"
    cp -f "$src_dir/$(basename "$src")" "$MACOS_DIR/$tool"
    chmod 755 "$MACOS_DIR/$tool"
done

# ---- 2. Qt + third-party dylibs (libmpv included) ------------------------
if [ -d "$APP" ]; then
    # The deploy tool has to be the Qt the app was built against: a macdeployqt
    # from another Qt copies that install's frameworks into the bundle, and the
    # app then loads the wrong ones (or none).
    MACDEPLOYQT=""
    if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/macdeployqt" ]; then
        MACDEPLOYQT="$QT_ROOT_DIR/bin/macdeployqt"
    fi
    [ -n "$MACDEPLOYQT" ] || MACDEPLOYQT="$(command -v macdeployqt || true)"
    [ -n "$MACDEPLOYQT" ] || MACDEPLOYQT="$(brew --prefix qt 2>/dev/null)/bin/macdeployqt"
    [ -x "$MACDEPLOYQT" ] || { echo "package-macos: macdeployqt not found (install Qt, or brew install qt)" >&2; exit 1; }
    echo "== macdeployqt: $MACDEPLOYQT"
    # -qmldir matters: the QML lives in a .qrc, so macdeployqt cannot discover the
    # QtQuick modules by scanning the binary.
    "$MACDEPLOYQT" "$APP" -always-overwrite -qmldir="$PWD/qml"
else
    echo "== no bundle: skipping macdeployqt (the build links Qt from CMAKE_PREFIX_PATH)" >&2
fi

# ---- 3. the ffmpeg binaries' own dylibs ----------------------------------
if command -v dylibbundler >/dev/null 2>&1; then
    mkdir -p "$FRAMEWORKS"
    for tool in ffmpeg ffprobe; do
        dylibbundler -of -cd -b -x "$MACOS_DIR/$tool" -d "$FRAMEWORKS" -p "@executable_path/../Frameworks" >/dev/null
    done
else
    echo "== dylibbundler not found (brew install dylibbundler): ffmpeg will need Homebrew libs" >&2
fi

# ---- 4. zip + dmg --------------------------------------------------------
# Only stale archives: "dist/$NAME" IS the payload in the no-bundle case.
rm -rf "dist/$NAME.zip" "dist/$NAME.dmg"
mkdir -p dist
if [ -d "$APP" ]; then
    ditto -c -k --sequesterRsrc --keepParent "$APP" "dist/$NAME.zip"
else
    (cd dist && ditto -c -k --sequesterRsrc "$NAME" "$NAME.zip")
fi
echo "== dist/$NAME.zip"

STAGE="dist/$NAME.dmg-root"
rm -rf "$STAGE"
mkdir -p "$STAGE"
if [ -d "$APP" ]; then cp -R "$APP" "$STAGE/"; else cp -R "dist/$NAME" "$STAGE/"; fi
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "vibeslideshow $VERSION" -srcfolder "$STAGE" -ov -format UDZO "dist/$NAME.dmg"
rm -rf "$STAGE"
echo "== dist/$NAME.dmg"
