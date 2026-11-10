#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$(cd "$ROOT/.." && pwd)/dist"
IMAGE="${DEB_BUILD_IMAGE:-uos-dual-pane-file-manager-builder}"
docker run --rm --platform linux/arm64 \
    -v "$ROOT:/work/file-manager-native" \
    -v "$DIST:/out" \
    "$IMAGE" bash /work/file-manager-native/packaging/build_deb_inside.sh
