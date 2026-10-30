#!/usr/bin/env bash
# Build release packages of sshLX: a .deb and an AppImage, written to dist/.
#
#   tools/package.sh                 # PuTTY tree at ../putty-0.85
#   PUTTY_DIR=/path/to/putty-0.85 tools/package.sh
#   SKIP_APPIMAGE=1 tools/package.sh     # .deb only
#
# The PuTTY tree is only read; all output goes to build-release/ and dist/.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
PUTTY_DIR="${PUTTY_DIR:-$SRC/../putty-0.85}"
BUILD="$SRC/build-release"
STAGE="$BUILD/stage"
DIST="$SRC/dist"
TOOLS="$BUILD/tools"
ARCH_DEB="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
ARCH_AI="$(uname -m)"

VERSION="$(sed -n 's/^project(sshLX VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }

# ---- build and stage ------------------------------------------------------------
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DSSHLX_ADS_FROM_SYSTEM=OFF \
    -DSSHLX_PUTTY_SOURCE_DIR="$PUTTY_DIR"
cmake --build "$BUILD"
rm -rf "$STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD" --component sshlx --strip
mkdir -p "$DIST"

# ---- .deb -------------------------------------------------------------------------
DEBROOT="$BUILD/deb"
rm -rf "$DEBROOT"
cp -a "$STAGE" "$DEBROOT"
chmod -R u=rwX,go=rX "$DEBROOT"
mkdir -p "$DEBROOT/DEBIAN" "$BUILD/shlibdeps/debian"
# dpkg-shlibdeps wants a debian/control in its working directory.
printf 'Source: sshlx\n\nPackage: sshlx\nArchitecture: any\n' > "$BUILD/shlibdeps/debian/control"
DEPENDS="$(cd "$BUILD/shlibdeps" &&
    dpkg-shlibdeps -O -l"$DEBROOT/usr/lib/$(gcc -print-multiarch 2>/dev/null)/sshlx" \
        -l"$DEBROOT/usr/lib/sshlx" -l"$DEBROOT/usr/lib64/sshlx" -l"$DEBROOT/usr/lib/sshlx" \
        "$DEBROOT/usr/bin/sshLX" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')"
SIZE="$(du -sk "$DEBROOT/usr" | cut -f1)"
cat > "$DEBROOT/DEBIAN/control" <<EOF
Package: sshlx
Version: $VERSION
Section: net
Priority: optional
Architecture: $ARCH_DEB
Installed-Size: $SIZE
Depends: $DEPENDS
Recommends: qt6-wayland, gnome-keyring | kwalletmanager
Maintainer: Claudiu Jitea <noreply@github.com>
Homepage: https://github.com/ClaudiuJitea/sshLX
Description: Tabbed, session-managed SSH, Telnet and serial client
 sshLX is a Qt 6 terminal client with a connections tree, dockable tabs and
 serial hot-plug detection, built on the PuTTY 0.85 core (SSH, Telnet, Rlogin,
 SUPDUP, Raw and serial back ends, terminal emulator and cryptography).
EOF
DEB="$DIST/sshlx_${VERSION}_${ARCH_DEB}.deb"
fakeroot dpkg-deb --build --root-owner-group "$DEBROOT" "$DEB"
echo "built $DEB"

# ---- AppImage ---------------------------------------------------------------------
# Downloads linuxdeploy and its Qt plugin (continuous builds) into
# build-release/tools/. Set SKIP_APPIMAGE=1 to build only the .deb.
if [ -n "${SKIP_APPIMAGE:-}" ]; then
    (cd "$DIST" && sha256sum -- *.deb > SHA256SUMS)
    exit 0
fi
mkdir -p "$TOOLS"
fetch() {
    [ -x "$TOOLS/$1" ] || { curl -fsSL -o "$TOOLS/$1" "$2"; chmod +x "$TOOLS/$1"; }
}
fetch linuxdeploy-$ARCH_AI.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH_AI.AppImage"
fetch linuxdeploy-plugin-qt-$ARCH_AI.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH_AI.AppImage"

APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
cp -a "$STAGE" "$APPDIR"
QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}"
LIBDIR="$(find "$APPDIR/usr" -type d -name sshlx -path '*/lib*' | head -n1)"
(
    cd "$BUILD"
    export QMAKE APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
    export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export EXTRA_QT_MODULES="waylandclient;dbus"
    # Wayland platform plugin(s): one libqwayland.so since Qt 6.10, separate
    # -egl/-generic plugins before. Bundle whichever this Qt has.
    PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms"
    wl=""
    for p in libqwayland.so libqwayland-egl.so libqwayland-generic.so; do
        [ -e "$PLUGINS/$p" ] && wl="${wl:+$wl;}$p"
    done
    export EXTRA_PLATFORM_PLUGINS="$wl"
    export LDAI_OUTPUT="$DIST/sshLX-$VERSION-$ARCH_AI.AppImage"
    "$TOOLS/linuxdeploy-$ARCH_AI.AppImage" --appimage-extract-and-run \
        --appdir "$APPDIR" \
        --desktop-file "$APPDIR/usr/share/applications/sshlx.desktop" \
        --icon-file "$SRC/resources/appicon/sshlx-256.png" --icon-filename sshlx \
        --plugin qt --output appimage
)
echo "built $DIST/sshLX-$VERSION-$ARCH_AI.AppImage"
(cd "$DIST" && sha256sum -- *.deb *.AppImage > SHA256SUMS)
