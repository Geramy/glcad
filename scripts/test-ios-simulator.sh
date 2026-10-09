#!/bin/sh
# Example: scripts/test-ios-simulator.sh --device <UDID> --skip-build
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sim_build="$project_root/build/ios-simulator"
sim_device=""
sim_output=""
sim_build_enabled=1
sim_smoke_env=GLCAD_IOS_NATIVE_SMOKE
sim_report_name=ios-native-smoke.json
while [ "$#" -gt 0 ]; do
  case "$1" in
    --device) sim_device=$2; shift 2 ;;
    --build-dir) sim_build=$2; shift 2 ;;
    --output) sim_output=$2; shift 2 ;;
    --skip-build) sim_build_enabled=0; shift ;;
    --storekit) sim_smoke_env=GLCAD_IOS_STOREKIT_SMOKE; sim_report_name=ios-storekit-smoke.json; shift ;;
    --help)
      printf '%s\n' 'Usage: test-ios-simulator.sh [--device UDID] [--build-dir PATH] [--output PATH] [--skip-build] [--storekit]'
      exit 0 ;;
    *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
  esac
done
if [ -z "$sim_output" ]; then sim_output=$(mktemp -d /tmp/glcad-ios-smoke.XXXXXX); fi
mkdir -p "$sim_output"
sim_output=$(CDPATH= cd -- "$sim_output" && pwd)
if [ "$sim_build_enabled" -eq 1 ]; then
  if [ ! -f "$sim_build/CMakeCache.txt" ]; then
    printf 'Configure the simulator first: scripts/xcode-ios.sh --simulator\n' >&2
    exit 2
  fi
  cmake --build "$sim_build" --config Debug --target glcad -- -jobs 6 > "$sim_output/build.log" 2>&1
fi
sim_app=$(find "$sim_build" -type d -path '*/Debug-iphonesimulator/glcad.app' -print -quit)
if [ -z "$sim_app" ]; then printf 'No Debug simulator app found under %s\n' "$sim_build" >&2; exit 2; fi
sim_bundle=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$sim_app/Info.plist")
if [ -z "$sim_device" ]; then
  xcrun simctl list devices available -j > "$sim_output/devices.json"
  sim_device=$(python3 - "$sim_output/devices.json" <<'PY'
import json, sys
groups = json.load(open(sys.argv[1]))['devices']
devices = [d for group in groups.values() for d in group if d.get('isAvailable') and 'iPad' in d['name']]
devices.sort(key=lambda d: d['state'] != 'Booted')
if not devices:
    sys.exit('Install an iPad simulator runtime in Xcode first.')
print(devices[0]['udid'])
PY
  )
fi
# boot fails on an already-booted device; bootstatus decides success.
xcrun simctl boot "$sim_device" > "$sim_output/boot.log" 2>&1 || true
xcrun simctl bootstatus "$sim_device" -b >> "$sim_output/boot.log" 2>&1
xcrun simctl install "$sim_device" "$sim_app"
sim_container=$(xcrun simctl get_app_container "$sim_device" "$sim_bundle" data)
sim_report="$sim_container/Documents/$sim_report_name"
sim_data=${sim_container%%/Containers/Data/Application/*}
sim_log_name="glcad-smoke-$$"
rm -f "$sim_report"
sim_test_status=0
if [ "$sim_smoke_env" = GLCAD_IOS_STOREKIT_SMOKE ]; then
  # SKTestSession needs XCTest's launch configuration.
  sim_result_bundle="$sim_output/StoreKit-$(date +%s).xcresult"
  # CMake leaves Testables empty for a MODULE target; register it.
  python3 - "$sim_build/glcad.xcodeproj/xcshareddata/xcschemes/gcapp_storekit_tests.xcscheme" "$project_root" "$sim_build/glcad.xcodeproj" <<'PY'
import copy, os, sys, xml.etree.ElementTree as ET
path = sys.argv[1]
tree = ET.parse(path)
root = tree.getroot()
reference = root.find('./BuildAction/BuildActionEntries/BuildActionEntry/BuildableReference')
testables = root.find('./TestAction/Testables')
testables.clear()
testable = ET.SubElement(testables, 'TestableReference', {'skipped': 'NO', 'parallelizable': 'NO'})
testable.append(copy.deepcopy(reference))
for item in root.iter('BuildableReference'):
    item.set('ReferencedContainer', 'container:' + os.path.relpath(sys.argv[3], sys.argv[2]))
    item.set('BuildableName', 'gcapp_storekit_tests.xctest')
launch = root.find('./LaunchAction')
configuration = launch.find('StoreKitConfigurationFileReference')
if configuration is None:
    configuration = ET.SubElement(launch, 'StoreKitConfigurationFileReference')
configuration.set('identifier', os.path.join(sys.argv[2], 'glcad/src/ui/tests/GLCADLocal.storekit'))
tree.write(path, encoding='utf-8', xml_declaration=True)
PY
  xcodebuild test -project "$sim_build/glcad.xcodeproj" -scheme gcapp_storekit_tests \
    -configuration Debug -destination "platform=iOS Simulator,id=$sim_device" \
    -resultBundlePath "$sim_result_bundle" CODE_SIGNING_ALLOWED=NO \
    > "$sim_output/xctest.log" 2>&1 || sim_test_status=$?
  sim_container=$(xcrun simctl get_app_container "$sim_device" "$sim_bundle" data)
  sim_report="$sim_container/Documents/$sim_report_name"
else
env "SIMCTL_CHILD_$sim_smoke_env=1" \
  "SIMCTL_CHILD_GLCAD_CONFIG_DIR=$sim_container/Library/Caches/native-smoke-config" \
  xcrun simctl launch --terminate-running-process \
  --stdout="/tmp/$sim_log_name.stdout.log" --stderr="/tmp/$sim_log_name.stderr.log" \
  "$sim_device" "$sim_bundle" > "$sim_output/launch.log"
sim_pid=$(awk 'END {print $NF}' "$sim_output/launch.log")
sim_seconds=0
while [ ! -f "$sim_report" ] && [ "$sim_seconds" -lt 150 ]; do
  case "$sim_pid" in
    ''|*[!0-9]*) ;;
    *) if ! kill -0 "$sim_pid" 2>/dev/null; then break; fi ;;
  esac
  sleep 1
  sim_seconds=$((sim_seconds + 1))
done
fi
for sim_stream in stdout stderr; do
  if [ -f "$sim_data/tmp/$sim_log_name.$sim_stream.log" ]; then
    cp "$sim_data/tmp/$sim_log_name.$sim_stream.log" "$sim_output/$sim_stream.log"
  fi
done
xcrun simctl io "$sim_device" screenshot "$sim_output/screenshot.png" > "$sim_output/screenshot.log" 2>&1 || true
if [ ! -f "$sim_report" ]; then
  printf 'Native smoke report did not arrive. Logs: %s\n' "$sim_output" >&2
  exit 1
fi
cp "$sim_report" "$sim_output/results.json"
python3 - "$sim_output/results.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
checks = report.get('checks', [])
failed = [c for c in checks if not c.get('passed')]
for check in checks:
    print(('PASS ' if check.get('passed') else 'FAIL ') + check['name'])
print(f'{len(checks)} checks, {len(failed)} failures')
if not checks or failed or report.get('failures', 1):
    sys.exit(1)
PY
if [ "$sim_test_status" -ne 0 ]; then
  printf 'XCTest runner failed (status %s). See %s/xctest.log\n' "$sim_test_status" "$sim_output" >&2
  exit "$sim_test_status"
fi
printf 'Simulator results: %s\n' "$sim_output"
