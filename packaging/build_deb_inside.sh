#!/bin/bash
set -euo pipefail

ROOT=/work/file-manager-native
BUILD="$ROOT/build-release"
STAGE="$ROOT/packaging/.stage"
OUT=/out
VERSION=2.9.3
ARCH=arm64
PACKAGE=uos-dual-pane-file-manager
DEB="$OUT/${PACKAGE}_${VERSION}_${ARCH}.deb"

rm -rf "$BUILD" "$STAGE"
mkdir -p "$OUT"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" --target dual-pane-file-manager --parallel 2

DESTDIR="$STAGE" cmake --build "$BUILD" --target install
strip --strip-unneeded "$STAGE/usr/bin/uos-dual-pane-file-manager"

install -Dm644 "$ROOT/packaging/uos-dual-pane-file-manager.desktop" \
    "$STAGE/usr/share/applications/uos-dual-pane-file-manager.desktop"
install -Dm644 "$ROOT/packaging/mimeapps.list" \
    "$STAGE/etc/xdg/mimeapps.list"
install -Dm755 "$ROOT/packaging/dde-file-manager-shim" \
    "$STAGE/usr/lib/uos-dual-pane-file-manager/dde-file-manager-shim"
for size in 32 48 64 128 256; do
    install -Dm644 "$ROOT/packaging/icons/uos-dual-pane-file-manager-${size}.png" \
        "$STAGE/usr/share/icons/hicolor/${size}x${size}/apps/uos-dual-pane-file-manager.png"
done
install -Dm644 "$ROOT/packaging/icons/uos-dual-pane-file-manager.svg" \
    "$STAGE/usr/share/icons/hicolor/scalable/apps/uos-dual-pane-file-manager.svg"

DOCDIR="$STAGE/usr/share/doc/$PACKAGE"
install -Dm644 "$ROOT/README.md" "$DOCDIR/README.md"
install -Dm644 "$ROOT/LICENSE" "$DOCDIR/LICENSE"
install -Dm644 "$ROOT/packaging/copyright" "$DOCDIR/copyright"
install -Dm644 "$ROOT/screenshots/native-light-selected.png" "$DOCDIR/screenshots/native-light-selected.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-single.png" "$DOCDIR/screenshots/native-v2.1-single.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-filetypes.png" "$DOCDIR/screenshots/native-v2.1-filetypes.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-desktop.png" "$DOCDIR/screenshots/native-v2.1-desktop.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-office-preview.png" "$DOCDIR/screenshots/native-v2.1-office-preview.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-archive-preview.png" "$DOCDIR/screenshots/native-v2.1-archive-preview.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-wps-preview.png" "$DOCDIR/screenshots/native-v2.1-wps-preview.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-search.png" "$DOCDIR/screenshots/native-v2.1-search.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-content-search.png" "$DOCDIR/screenshots/native-v2.1-content-search.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-pdf-preview.png" "$DOCDIR/screenshots/native-v2.1-pdf-preview.png"
install -Dm644 "$ROOT/screenshots/native-v2.1-ofd-preview.png" "$DOCDIR/screenshots/native-v2.1-ofd-preview.png"
install -Dm644 "$ROOT/screenshots/native-v2.5-tree.png" "$DOCDIR/screenshots/native-v2.5-tree.png"
printf '%s (%s) unstable; urgency=medium\n\n  * Initial native Qt 5 dual-pane file manager build.\n\n -- Dual-pane File Manager Project <demo@example.com>  Sat, 07 Nov 2026 10:00:00 +0800\n' \
    "$PACKAGE" "$VERSION" | gzip -9n > "$DOCDIR/changelog.Debian.gz"
chmod 644 "$DOCDIR/changelog.Debian.gz"

find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f -exec chmod 644 {} +
chmod 755 "$STAGE/usr/bin/uos-dual-pane-file-manager"
chmod 755 "$STAGE/usr/lib/$PACKAGE/dde-file-manager-shim"

mkdir -p "$STAGE/DEBIAN"
cp "$ROOT/packaging/postinst" "$STAGE/DEBIAN/postinst"
cp "$ROOT/packaging/postrm" "$STAGE/DEBIAN/postrm"
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

INSTALLED_SIZE=$(python3 - "$STAGE" <<'PY'
import os, sys
root = sys.argv[1]
total = 0
for base, dirs, files in os.walk(root):
    if os.path.relpath(base, root).split(os.sep)[0] == 'DEBIAN':
        continue
    for name in files:
        total += os.path.getsize(os.path.join(base, name))
print(max(1, (total + 1023) // 1024))
PY
)

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6 (>= 2.28), libstdc++6 (>= 8), libgcc1, libqt5core5a (>= 5.11.3), libqt5gui5 (>= 5.11.3), libqt5widgets5 (>= 5.11.3), libqt5svg5, libqt5network5, libqt5concurrent5 (>= 5.11.3), zlib1g, libsqlite3-0, poppler-utils
Recommends: xdg-utils, libglib2.0-bin, deepin-compressor
Installed-Size: $INSTALLED_SIZE
Maintainer: Dual-pane File Manager Project <demo@example.com>
Description: Native file manager for UOS
 A native Qt 5 file manager with two independent file panes, quick access
 to home and mounted devices, background copy and move queues with pause and
 cancel, safe rename-on-conflict behavior, trash deletion, search filtering,
 properties, and light/dark themes.
 .
 The application works directly on local files and does not require a browser
 or a Python runtime. It is built for the UOS 20 / Debian 10 arm64 baseline.
CONTROL
chmod 644 "$STAGE/DEBIAN/control"

python3 - "$STAGE" > "$STAGE/DEBIAN/md5sums" <<'PY'
import hashlib, os, sys
root = sys.argv[1]
lines = []
for base, dirs, files in os.walk(root):
    dirs.sort()
    relbase = os.path.relpath(base, root)
    if relbase.split(os.sep)[0] == 'DEBIAN':
        continue
    for name in sorted(files):
        path = os.path.join(base, name)
        rel = os.path.relpath(path, root).replace(os.sep, '/')
        with open(path, 'rb') as handle:
            digest = hashlib.md5(handle.read()).hexdigest()
        lines.append('%s  %s\n' % (digest, rel))
sys.stdout.write(''.join(lines))
PY
chmod 644 "$STAGE/DEBIAN/md5sums"

rm -f "$DEB"
dpkg-deb --root-owner-group --build "$STAGE" "$DEB"
(cd "$OUT" && sha256sum "$(basename "$DEB")" > SHA256SUMS.native)
echo "BUILT $DEB"
