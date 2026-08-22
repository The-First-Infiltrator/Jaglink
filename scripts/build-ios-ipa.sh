#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${1:-${project_root}/dist}"
derived_dir="$(mktemp -d "${TMPDIR:-/tmp}/jaglink-derived.XXXXXX")"
package_dir="$(mktemp -d "${TMPDIR:-/tmp}/jaglink-package.XXXXXX")"

cleanup() {
    rm -rf "${derived_dir}" "${package_dir}"
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build-ios-ipa.sh requires macOS and Xcode." >&2
    exit 1
fi
command -v xcodebuild >/dev/null
command -v ditto >/dev/null
command -v zip >/dev/null

mkdir -p "${output_dir}"

xcodebuild \
    -project "${project_root}/app/ios/JAGLINK.xcodeproj" \
    -scheme JAGLINK \
    -configuration Release \
    -sdk iphoneos \
    -destination 'generic/platform=iOS' \
    -derivedDataPath "${derived_dir}" \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_IDENTITY='' \
    build

app_path="${derived_dir}/Build/Products/Release-iphoneos/JAGLINK.app"
test -d "${app_path}"
test -x "${app_path}/JAGLINK"
/usr/bin/lipo -info "${app_path}/JAGLINK" | grep -q arm64

mkdir -p "${package_dir}/Payload"
/usr/bin/ditto "${app_path}" "${package_dir}/Payload/JAGLINK.app"
(
    cd "${package_dir}"
    /usr/bin/zip -qry "${output_dir}/JAGLINK-unsigned.ipa" Payload
)
/usr/bin/unzip -tq "${output_dir}/JAGLINK-unsigned.ipa"
(
    cd "${output_dir}"
    /usr/bin/shasum -a 256 JAGLINK-unsigned.ipa > JAGLINK-unsigned.ipa.sha256
)

echo "Created ${output_dir}/JAGLINK-unsigned.ipa"
echo "Created ${output_dir}/JAGLINK-unsigned.ipa.sha256"
