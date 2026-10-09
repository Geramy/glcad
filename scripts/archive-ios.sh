#!/bin/sh
# Create a signed release archive; upload is a separate step.
set -eu
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${GLCAD_DEVELOPMENT_TEAM:-}" ] && [ -f "$project_root/build/ios/CMakeCache.txt" ]; then
  GLCAD_DEVELOPMENT_TEAM=$(sed -n 's/^CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM:[^=]*=//p' "$project_root/build/ios/CMakeCache.txt" | head -1)
fi
export GLCAD_DEVELOPMENT_TEAM
: "${GLCAD_DEVELOPMENT_TEAM:?Set the Apple development team ID}"
archive_mode=${1:-release}
if [ "$archive_mode" = --development ]; then
  export GLCAD_IOS_BUNDLE_ID=${GLCAD_IOS_BUNDLE_ID:-com.glcad.app}
  "$project_root/scripts/xcode-ios.sh" -DGLCAD_PAID_BUILD=OFF -DGLCAD_PURCHASE_MODEL= \
    -DGLCAD_IOS_DEVICE_FAMILY=1,2 -DGLCAD_IOS_BUNDLE_ID="$GLCAD_IOS_BUNDLE_ID"
  archive_path=${GLCAD_ARCHIVE_PATH:-"$project_root/build/archives/GLCAD-development.xcarchive"}
  if [ -e "$archive_path" ]; then
    printf 'Archive already exists; choose a new GLCAD_ARCHIVE_PATH: %s\n' "$archive_path" >&2
    exit 2
  fi
  xcodebuild -project "$project_root/build/ios/glcad.xcodeproj" -scheme glcad \
    -configuration Release -destination 'generic/platform=iOS' \
    -archivePath "$archive_path" -allowProvisioningUpdates \
    CODE_SIGN_IDENTITY='Apple Development' archive
  export_path=${GLCAD_IOS_EXPORT_PATH:-"$project_root/build/installers/ios-development"}
  mkdir -p "$export_path"
  python3 - "$export_path/ExportOptions.plist" "$GLCAD_DEVELOPMENT_TEAM" <<'PYOPTIONS'
import plistlib, sys
with open(sys.argv[1], 'wb') as stream:
    plistlib.dump({'method': 'debugging', 'teamID': sys.argv[2],
                  'signingStyle': 'automatic'}, stream)
PYOPTIONS
  xcodebuild -exportArchive -archivePath "$archive_path" \
    -exportPath "$export_path" -exportOptionsPlist "$export_path/ExportOptions.plist" \
    -allowProvisioningUpdates
  printf 'Development-signed device package: %s\n' "$export_path"
  exit 0
fi
case "$archive_mode" in
  release) purchase_model=subscription ;;
  --upfront) purchase_model=upfront ;;
  *) printf 'Usage: archive-ios.sh [--development | --upfront]\n' >&2; exit 2 ;;
esac
: "${GLCAD_IOS_BUNDLE_ID:?Set the registered bundle identifier}"
if [ "$purchase_model" = subscription ]; then
  : "${GLCAD_SUBSCRIPTION_PRODUCT_ID:?Set the real monthly App Store product ID}"
else
  GLCAD_SUBSCRIPTION_PRODUCT_ID=
fi
export GLCAD_SUBSCRIPTION_PRODUCT_ID
: "${GLCAD_SUPPORT_URL:?Set the public support URL}"
: "${GLCAD_PRIVACY_URL:?Set the public privacy policy URL}"
: "${GLCAD_TERMS_URL:?Set the public terms URL}"
python3 - <<'PY'
import os
from urllib.parse import urlsplit
product = os.environ['GLCAD_SUBSCRIPTION_PRODUCT_ID']
if product and product.startswith(('test.', 'example.', 'com.example.')):
    raise SystemExit('A local fixture product cannot be archived for distribution.')
for key in ('GLCAD_SUPPORT_URL', 'GLCAD_PRIVACY_URL', 'GLCAD_TERMS_URL'):
    url = urlsplit(os.environ[key])
    if (url.scheme != 'https' or not url.hostname or url.username or url.password
            or url.hostname.endswith(('.invalid', '.test', '.localhost'))
            or url.hostname in ('localhost', 'example.com', 'example.org', 'example.net')):
        raise SystemExit(f'{key} must be a real public HTTPS URL.')
PY
paid_build=OFF
if [ "$purchase_model" = subscription ]; then paid_build=ON; fi
"$project_root/scripts/xcode-ios.sh" \
  -DGLCAD_PAID_BUILD="$paid_build" \
  -DGLCAD_PURCHASE_MODEL="$purchase_model" \
  -DGLCAD_IOS_DEVICE_FAMILY=2 \
  -DGLCAD_IOS_BUILD_NUMBER="${GLCAD_IOS_BUILD_NUMBER:-}" \
  -DGLCAD_IOS_BUNDLE_ID="$GLCAD_IOS_BUNDLE_ID" \
  -DGLCAD_SUBSCRIPTION_PRODUCT_ID="$GLCAD_SUBSCRIPTION_PRODUCT_ID" \
  -DGLCAD_SUPPORT_URL="$GLCAD_SUPPORT_URL" \
  -DGLCAD_PRIVACY_URL="$GLCAD_PRIVACY_URL" \
  -DGLCAD_TERMS_URL="$GLCAD_TERMS_URL"
python3 "$project_root/scripts/bundle-license-notices.py" \
  --build "$project_root/build/ios" --output "$project_root/build/ios/release-license-audit"
archive_path=${GLCAD_ARCHIVE_PATH:-"$project_root/build/archives/GLCAD.xcarchive"}
if [ -e "$archive_path" ]; then
  printf 'Archive already exists; choose a new GLCAD_ARCHIVE_PATH: %s\n' "$archive_path" >&2
  exit 2
fi
xcodebuild -project "$project_root/build/ios/glcad.xcodeproj" -scheme glcad \
  -configuration Release -destination 'generic/platform=iOS' \
  -archivePath "$archive_path" -allowProvisioningUpdates archive
if [ "$purchase_model" = upfront ]; then
  export_path=${GLCAD_IOS_EXPORT_PATH:-"$project_root/build/installers/ios-app-store"}
  mkdir -p "$export_path"
  python3 - "$export_path/ExportOptions.plist" "$GLCAD_DEVELOPMENT_TEAM" <<'PYOPTIONS'
import plistlib, sys
with open(sys.argv[1], 'wb') as stream:
    plistlib.dump({'method': 'app-store-connect', 'destination': 'export',
                   'teamID': sys.argv[2], 'signingStyle': 'automatic',
                   'uploadSymbols': True}, stream)
PYOPTIONS
  xcodebuild -exportArchive -archivePath "$archive_path" \
    -exportPath "$export_path" -exportOptionsPlist "$export_path/ExportOptions.plist" \
    -allowProvisioningUpdates
  printf 'Distribution-signed package for App Store Connect: %s\n' "$export_path"
  exit 0
fi
printf 'Archive ready for validation in Xcode Organizer: %s\n' "$archive_path"
