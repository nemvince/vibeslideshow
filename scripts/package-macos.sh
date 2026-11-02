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

[ "$SKIP_BUILD" = 1 ] || ./scripts/build.sh --dir "$BUILD_DIR" --release

APP="$BUILD_DIR/vibeslideshow.app"
[ -d "$APP" ] || { echo "package-macos: $APP missing (is MACOSX_BUNDLE set?)" >&2; exit 1; }
MACOS_DIR="$APP/Contents/MacOS"
FRAMEWORKS="$APP/Contents/Frameworks"

# ---- 1. ffmpeg/ffprobe next to the binary --------------------------------
# The app shells out to them, and a bundle launched from the Finder has no PATH.
for tool in ffmpeg ffprobe; do
    src="$(command -v "$tool" || true)"
    [ -n "$src" ] || { echo "package-macos: $tool not on PATH (brew install ffmpeg)" >&2; exit 1; }
    cp -f "$(readlink -f "$src")" "$MACOS_DIR/$tool"
    chmod 755 "$MACOS_DIR/$tool"
done

# ---- 2. Qt + third-party dylibs (libmpv included) ------------------------
MACDEPLOYQT="$(command -v macdeployqt || true)"
[ -n "$MACDEPLOYQT" ] || MACDEPLOYQT="$(brew --prefix qt 2>/dev/null)/bin/macdeployqt"
[ -x "$MACDEPLOYQT" ] || { echo "package-macos: macdeployqt not found (brew install qt)" >&2; exit 1; }
"$MACDEPLOYQT" "$APP" -always-overwrite -no-codesign

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
rm -rf "dist/$NAME" "dist/$NAME.zip" "dist/$NAME.dmg"
mkdir -p dist
ditto -c -k --sequesterRsrc --keepParent "$APP" "dist/$NAME.zip"
echo "== dist/$NAME.zip"

STAGE="dist/$NAME.dmg-root"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname "vibeslideshow $VERSION" -srcfolder "$STAGE" -ov -format UDZO "dist/$NAME.dmg"
rm -rf "$STAGE"
echo "== dist/$NAME.dmg"
