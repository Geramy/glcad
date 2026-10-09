#!/bin/sh
# Cross-build the Windows installer with MinGW-w64 (requires makensis).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VCPKG_ROOT=${VCPKG_ROOT:-"$ROOT/build/tools/vcpkg"}
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
    echo "Set VCPKG_ROOT to a bootstrapped vcpkg checkout." >&2
    exit 2
fi
WINDOWS_BUILD=${GLCAD_WINDOWS_BUILD_DIR:-"$ROOT/build/windows-package"}
HOST_FLATC=$(command -v flatc)
HOST_INCLUDE=$(dirname "$(dirname "$HOST_FLATC")")/include
cmake -S "$ROOT" -B "$WINDOWS_BUILD" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-mingw64.cmake" \
      -DVCPKG_MANIFEST_DIR="$ROOT/scripts/windows-dependencies" \
      -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
      -DVCPKG_OVERLAY_PORTS="$ROOT/scripts/windows-dependencies/ports" \
      -DGLCAD_PORTABLE_UNDERLAYS=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DGLCAD_FETCH_SLANG=OFF \
      -DGLCAD_GLSLANG="$(command -v glslangValidator)" \
      -DFLATC_EXECUTABLE="$HOST_FLATC" \
      -DFLATBUFFERS_INCLUDE_DIR="${FLATBUFFERS_INCLUDE_DIR:-$HOST_INCLUDE}"
cmake --build "$WINDOWS_BUILD" -j"${GLCAD_BUILD_JOBS:-6}"
cd "$WINDOWS_BUILD" && cpack -G "${GLCAD_WINDOWS_GENERATORS:-NSIS;ZIP}"
ls -la "$ROOT/build/installers"/glcad-*-win64.zip
