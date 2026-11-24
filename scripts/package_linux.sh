#!/usr/bin/env bash
# Builds the Linux packages of Atlas:
#   dist/Atlas-<version>-x86_64.AppImage
#   dist/Atlas-<version>-linux-x86_64.tar.gz   (the same files; for machines without FUSE)
#
# usage: scripts/package_linux.sh
#
# A Release build in build-package/ (tests off) is installed into an AppDir,
# then linuxdeploy and its Qt plugin bundle the Qt libraries and plugins.
# The tools are downloaded into ${XDG_CACHE_HOME:-~/.cache}/atlas-packaging,
# outside the repository. <version> is atlasVersion() with "+" made "-".
#
# A package is only as portable as the glibc it was built against: build the
# release on the oldest distribution it must run on (CI uses ubuntu-22.04).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$ROOT/build-package
DIST=$ROOT/dist
APPDIR=$BUILD/AppDir
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/atlas-packaging

# Pinned releases (override with the environment if one disappears).
LINUXDEPLOY_URL=${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage}
LINUXDEPLOY_QT_URL=${LINUXDEPLOY_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage}

die() { echo "package_linux: $*" >&2; exit 1; }

for tool in cmake ninja tar; do
    command -v "$tool" >/dev/null || die "'$tool' is required"
done
QMAKE_BIN=${QMAKE:-$(command -v qmake6 || command -v qmake || true)}
[ -n "$QMAKE_BIN" ] || die "qmake (Qt 6) not found; set QMAKE=/path/to/qmake"

# --- tools ---------------------------------------------------------------
mkdir -p "$CACHE"
fetch() {
    local url=$1 dest=$2
    if [ ! -s "$dest" ]; then
        echo "== downloading $(basename "$dest")"
        if ! curl -fL --retry 3 -o "$dest.part" "$url"; then
            rm -f "$dest.part"
            die "could not download $url
  Download it by hand into $dest, or set LINUXDEPLOY_URL / LINUXDEPLOY_QT_URL to another release."
        fi
        mv "$dest.part" "$dest"
    fi
    chmod +x "$dest"
}
LINUXDEPLOY=$CACHE/linuxdeploy-x86_64.AppImage
fetch "$LINUXDEPLOY_URL" "$LINUXDEPLOY"
fetch "$LINUXDEPLOY_QT_URL" "$CACHE/linuxdeploy-plugin-qt-x86_64.AppImage"
# linuxdeploy finds its plugins next to itself or on PATH.
export PATH="$CACHE:$PATH"
# The runners and many machines have no FUSE: let the tools unpack themselves.
export APPIMAGE_EXTRACT_AND_RUN=1

# --- build and install ---------------------------------------------------
echo "== configure (Release, tests off) in build-package/"
CONFIGURE_LOG=$(cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF \
                      -DCMAKE_INSTALL_PREFIX=/usr)
echo "$CONFIGURE_LOG" | tail -3
ATLAS_VERSION=$(echo "$CONFIGURE_LOG" | sed -n 's/^-- Atlas version //p' | tail -1)
[ -n "$ATLAS_VERSION" ] || die "the configure step did not print '-- Atlas version'"
VERSION=${ATLAS_VERSION//+/-}
echo "== version $ATLAS_VERSION (files: $VERSION)"

echo "== build"
cmake --build "$BUILD"
rm -rf "$APPDIR"
DESTDIR=$APPDIR cmake --install "$BUILD"
[ -x "$APPDIR/usr/bin/Atlas" ] && [ -x "$APPDIR/usr/bin/atlas-cli" ] || die "install did not produce usr/bin/Atlas and usr/bin/atlas-cli"

# --- what goes with it ---------------------------------------------------
mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/atlas"
cat > "$APPDIR/usr/share/applications/atlas.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Atlas
Comment=RISC-V assembly simulator for learning programming and microarchitecture
Exec=Atlas
Icon=atlas
Terminal=false
Categories=Education;Development;
EOF
cp "$ROOT/resources/icons/atlas.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/atlas.png"
cp -r "$ROOT/examples" "$APPDIR/usr/share/atlas/examples"
cp "$ROOT/LICENSE.md" "$ROOT/NOTICE.md" "$APPDIR/"
cat > "$APPDIR/README_PACKAGE.txt" <<EOF
Atlas $ATLAS_VERSION - Linux x86_64

Atlas is a RISC-V assembly simulator for learning programming and
microarchitecture.

Run
  AppImage:  chmod +x Atlas-$VERSION-x86_64.AppImage
             ./Atlas-$VERSION-x86_64.AppImage
             (no FUSE: add --appimage-extract-and-run)
  tar.gz:    tar xzf Atlas-$VERSION-linux-x86_64.tar.gz
             Atlas-$VERSION-linux-x86_64/atlas            the application
             Atlas-$VERSION-linux-x86_64/atlas-cli        the command line, e.g.
             Atlas-$VERSION-linux-x86_64/atlas-cli Atlas-$VERSION-linux-x86_64/usr/share/atlas/examples/scheduler/load_use_original.s --vm rv5-h-f

Inside
  usr/bin/Atlas, usr/bin/atlas-cli        the application and the command line
  usr/share/atlas/examples/               example programs
  usr/lib/, usr/plugins/                  the bundled Qt 6 libraries and plugins
  LICENSE.md, NOTICE.md                   Atlas's licence and origin

The Qt 6 libraries are bundled under the GNU LGPL.
EOF

# --- bundle and write the AppImage ---------------------------------------
echo "== linuxdeploy (Qt plugin)"
EXTRA=""
QT_PLUGINS_DIR=$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS)
for p in libqoffscreen.so libqwayland-egl.so libqwayland.so; do
    [ -e "$QT_PLUGINS_DIR/platforms/$p" ] && EXTRA="${EXTRA:+$EXTRA;}$p"
done
export QMAKE=$QMAKE_BIN
export EXTRA_PLATFORM_PLUGINS=$EXTRA
# linuxdeploy's bundled strip is too old for the libraries of recent
# distributions (it fails on their .relr.dyn sections, seen on Fedora 44),
# and the Qt libraries come stripped anyway. Set NO_STRIP=0 to strip.
export NO_STRIP=${NO_STRIP:-1}
# linuxdeploy rewrites each bundled file's RUNPATH with its own patchelf
# (0.15), which corrupts ELF files linked with packed relative relocations
# (DT_RELR, the default on Fedora 44): they crash while loading. It uses
# $PATCHELF instead when that is set; a patchelf on PATH is passed on.
# Ubuntu 22.04, where CI builds, does not link with DT_RELR.
if [ -z "${PATCHELF:-}" ] && command -v patchelf >/dev/null; then
    PATCHELF=$(command -v patchelf)
    export PATCHELF
fi
mkdir -p "$DIST"
rm -f "$DIST"/Atlas-*-x86_64.AppImage "$DIST"/Atlas-*-linux-x86_64.tar.gz
(
    cd "$BUILD"
    export LINUXDEPLOY_OUTPUT_VERSION=$VERSION
    "$LINUXDEPLOY" --appdir "$APPDIR" \
        --executable "$APPDIR/usr/bin/Atlas" \
        --executable "$APPDIR/usr/bin/atlas-cli" \
        --desktop-file "$APPDIR/usr/share/applications/atlas.desktop" \
        --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/atlas.png" \
        --plugin qt --output appimage
)
if [ -z "${PATCHELF:-}" ]; then
    for f in "$APPDIR"/usr/lib/*.so*; do
        if readelf -d "$f" 2>/dev/null | grep -q '(RELR)'; then
            echo "package_linux: WARNING: bundled libraries use DT_RELR and were patched by" >&2
            echo "  linuxdeploy's own patchelf 0.15; the package will probably crash. Build on" >&2
            echo "  Ubuntu 22.04 (as CI does), or set PATCHELF to a newer patchelf." >&2
            break
        fi
    done
fi
APPIMAGE=$(find "$BUILD" -maxdepth 1 -name '*.AppImage' -newer "$APPDIR/README_PACKAGE.txt" | head -1)
[ -n "$APPIMAGE" ] || die "linuxdeploy wrote no AppImage"
mv "$APPIMAGE" "$DIST/Atlas-$VERSION-x86_64.AppImage"

# --- the tar.gz: the same AppDir, with two launchers ---------------------
TOP=Atlas-$VERSION-linux-x86_64
STAGE=$BUILD/tar
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$APPDIR" "$STAGE/$TOP"
cat > "$STAGE/$TOP/atlas" <<'EOF'
#!/bin/sh
# Starts the Atlas application from this folder.
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/Atlas" "$@"
EOF
cat > "$STAGE/$TOP/atlas-cli" <<'EOF'
#!/bin/sh
# The Atlas command line from this folder (it needs no display).
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/atlas-cli" "$@"
EOF
chmod +x "$STAGE/$TOP/atlas" "$STAGE/$TOP/atlas-cli"
tar -C "$STAGE" -czf "$DIST/$TOP.tar.gz" "$TOP"

echo "== wrote"
ls -l "$DIST/Atlas-$VERSION-x86_64.AppImage" "$DIST/$TOP.tar.gz"
