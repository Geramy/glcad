#!/bin/sh
# Build local installers without publishing. Args: platform names, default all.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 1
DIST="$ROOT/build/installers"
RUN="$DIST/logs/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
if [ "$#" -eq 0 ]; then set -- macos windows linux-arm64 linux-amd64 ios; fi
failed=0
for platform in "$@"; do
    printf 'Building %s; log: %s/%s.log\n' "$platform" "$RUN" "$platform"
    case "$platform" in
        macos)
            (set -e
             cmake --preset release -DGLCAD_FETCH_SLANG=OFF
             cmake --build build/release -j"${GLCAD_BUILD_JOBS:-6}"
             cd build/release
             cpack
             if [ -n "${GLCAD_SIGN_IDENTITY:-}" ]; then
                 codesign --force --timestamp --sign "$GLCAD_SIGN_IDENTITY" "$DIST"/glcad-*-Darwin.dmg
             fi) > "$RUN/$platform.log" 2>&1 ;;
        windows) "$ROOT/scripts/package-windows.sh" > "$RUN/$platform.log" 2>&1 ;;
        linux-arm64) "$ROOT/scripts/package-linux.sh" arm64 > "$RUN/$platform.log" 2>&1 ;;
        linux-amd64) "$ROOT/scripts/package-linux.sh" amd64 > "$RUN/$platform.log" 2>&1 ;;
        ios-development) "$ROOT/scripts/archive-ios.sh" --development > "$RUN/$platform.log" 2>&1 ;;
        ios) "$ROOT/scripts/archive-ios.sh" > "$RUN/$platform.log" 2>&1 ;;
        *) printf 'Unknown platform: %s\n' "$platform" >&2; exit 2 ;;
    esac
    status=$?
    printf '%s\t%s\n' "$platform" "$status" >> "$RUN/results.tsv"
    if [ "$status" -ne 0 ]; then
        failed=1
        printf 'FAILED %s (status %s); see %s/%s.log\n' "$platform" "$status" "$RUN" "$platform"
    else
        printf 'Built %s\n' "$platform"
    fi
done
printf 'Installer directory: %s\nBuild results: %s/results.tsv\n' "$DIST" "$RUN"
exit "$failed"
