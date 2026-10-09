#!/bin/sh
#   scripts/xcode-macos.sh   then   open build/xcode/*.xcodeproj
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build/xcode" -G Xcode
echo "open $ROOT/build/xcode/*.xcodeproj"
