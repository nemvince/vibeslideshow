#!/usr/bin/env bash
# Linux bundle: a portable tarball, plus a .deb when dpkg-deb is available.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build"
SKIP_BUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) SKIP_BUILD=1; shift ;;
        --dir)      BUILD_DIR="${2:?--dir needs a path}"; shift 2 ;;
        *)          echo "package-linux: unknown option $1" >&2; exit 2 ;;
    esac
done

VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "package-linux: cannot read VERSION from CMakeLists.txt" >&2; exit 1; }
ARCH="$(uname -m)"
NAME="vibeslideshow-$VERSION-linux-$ARCH"

[ "$SKIP_BUILD" = 1 ] || bash ./scripts/build.sh --dir "$BUILD_DIR" --release

BIN="$BUILD_DIR/vibeslideshow"
[ -x "$BIN" ] || { echo "package-linux: $BIN missing" >&2; exit 1; }

rm -rf "dist/$NAME" "dist/$NAME.tar.gz"
mkdir -p "dist/$NAME/bin" \
         "dist/$NAME/share/applications" \
         "dist/$NAME/share/icons/hicolor/scalable/apps"

install -m 755 "$BIN" "dist/$NAME/bin/vibeslideshow"
install -m 644 packaging/vibeslideshow.desktop "dist/$NAME/share/applications/"
install -m 644 packaging/vibeslideshow.svg "dist/$NAME/share/icons/hicolor/scalable/apps/"
install -m 644 README.md LICENSE "dist/$NAME/"

# A tiny launcher so the tarball works without installing anything: it puts the
# bundled Qt/qml paths first, which is what a distro install would do.
cat > "dist/$NAME/vibeslideshow.sh" <<'LAUNCHER'
#!/bin/sh
# Run vibeslideshow from this folder, without installing it.
here=$(dirname "$(readlink -f "$0")")
exec "$here/bin/vibeslideshow" "$@"
LAUNCHER
chmod 755 "dist/$NAME/vibeslideshow.sh"

tar -C dist -czf "dist/$NAME.tar.gz" "$NAME"
echo "== dist/$NAME.tar.gz"

# ---- optional .deb -------------------------------------------------------
if command -v dpkg-deb >/dev/null 2>&1; then
    PKG="vibeslideshow_${VERSION}_${ARCH}"
    rm -rf "dist/deb/$PKG"
    mkdir -p "dist/deb/$PKG/DEBIAN" "dist/deb/$PKG/usr/bin" \
             "dist/deb/$PKG/usr/share/applications" \
             "dist/deb/$PKG/usr/share/icons/hicolor/scalable/apps" \
             "dist/deb/$PKG/usr/share/doc/vibeslideshow"
    install -m 755 "$BIN" "dist/deb/$PKG/usr/bin/vibeslideshow"
    install -m 644 packaging/vibeslideshow.desktop "dist/deb/$PKG/usr/share/applications/"
    install -m 644 packaging/vibeslideshow.svg "dist/deb/$PKG/usr/share/icons/hicolor/scalable/apps/"
    install -m 644 README.md LICENSE "dist/deb/$PKG/usr/share/doc/vibeslideshow/"

    INSTALLED_KB="$(du -sk "dist/deb/$PKG" | cut -f1)"
    cat > "dist/deb/$PKG/DEBIAN/control" <<CONTROL
Package: vibeslideshow
Version: $VERSION
Section: video
Priority: optional
Architecture: $ARCH
Depends: libc6, libstdc++6, ffmpeg, libmpv2 | libmpv1,
 libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6,
 libqt6quick6t64 | libqt6quick6, libqt6quickcontrols2-6t64 | libqt6quickcontrols2-6,
 qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts,
 qml6-module-qtquick-dialogs, qml6-module-qtquick-window
Installed-Size: $INSTALLED_KB
Maintainer: vibeslideshow contributors
Homepage: https://github.com/nemvince/vibeslideshow
Description: Beat-synced slideshow editor for your own footage
 Cuts a folder of clips to a song, then lets you fix the edit shot by shot.
CONTROL

    dpkg-deb --root-owner-group --build "dist/deb/$PKG" "dist/${PKG}.deb" >/dev/null
    echo "== dist/${PKG}.deb"
else
    echo "== dpkg-deb not found, skipping .deb (tarball is enough to run it)"
fi
