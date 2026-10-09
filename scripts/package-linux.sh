#!/bin/sh
# Usage: scripts/package-linux.sh [arm64|amd64]
set -e
ARCH="${1:-arm64}"
case "$ARCH" in
    arm64) PLATFORM=linux/arm64 ;;
    amd64) PLATFORM=linux/amd64 ;;
    *) echo "arch must be arm64 or amd64" >&2; exit 2 ;;
esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG=glcad-linux-build
docker build --platform "$PLATFORM" -t "$IMG-$ARCH" -f "$ROOT/scripts/linux.Dockerfile" "$ROOT/scripts"
mkdir -p "$ROOT/build/linux-$ARCH"
docker run --rm --platform "$PLATFORM" \
    -v "$ROOT":/src -w /src "$IMG-$ARCH" sh -c "
        set -e
        cmake -S . -B build/linux-$ARCH/tree -G Ninja \
              -DCMAKE_BUILD_TYPE=Release -DGLCAD_FETCH_SLANG=OFF
        cmake --build build/linux-$ARCH/tree -j${GLCAD_BUILD_JOBS:-6}
        cd build/linux-$ARCH/tree && cpack
    "
ls -la "$ROOT/build/installers/" | grep linux
