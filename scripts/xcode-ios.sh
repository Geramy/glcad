#!/bin/sh
# Generate an iOS Xcode project. Pass --simulator for unsigned local testing.
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ios_sdk=iphoneos
build_name=ios
if [ "${1:-}" = --simulator ]; then ios_sdk=iphonesimulator; build_name=ios-simulator; shift; fi
shader_compiler=${GLCAD_GLSLANG:-$(command -v glslangValidator)}
flatc_compiler=${FLATC_EXECUTABLE:-$(command -v flatc)}
flatbuffers_include=${FLATBUFFERS_INCLUDE_DIR:-$(dirname "$(dirname "$flatc_compiler")")/include}
set -- -S "$project_root" -B "$project_root/build/$build_name" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$ios_sdk" \
  -DCMAKE_CONFIGURATION_TYPES='Release;Debug' -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DGLCAD_FETCH_SLANG=OFF \
  -DGLCAD_GLSLANG="$shader_compiler" -DFLATC_EXECUTABLE="$flatc_compiler" \
  -DFLATBUFFERS_INCLUDE_DIR="$flatbuffers_include" "$@"
if [ "$ios_sdk" = iphonesimulator ]; then
  set -- "$@" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DGLCAD_WITH_PGLITE=OFF
elif [ -n "${GLCAD_DEVELOPMENT_TEAM:-}" ]; then
  set -- "$@" -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$GLCAD_DEVELOPMENT_TEAM" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic
fi
cmake "$@"
printf 'Project: %s/build/%s/glcad.xcodeproj\n' "$project_root" "$build_name"
