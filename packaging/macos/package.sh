#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-$source_dir/build/macos-arm64-release}"
build_dir="$(cd "$build_dir" && pwd)"
version="$(cat "$build_dir/version.txt")"
version="${version%$'\r'}"
component='(0|[1-9][0-9]{0,4})'
if [[ ! "$version" =~ ^$component\.$component\.$component\.$component$ ]]; then
    echo "Invalid application version: $version" >&2
    exit 1
fi
IFS=. read -r -a version_parts <<< "$version"
for part in "${version_parts[@]}"; do
    if (( part > 65535 )); then
        echo "Invalid application version: $version (each component must be at most 65535)" >&2
        exit 1
    fi
done

# Stage outside the checkout: synced Documents folders can reattach Finder
# metadata while codesign is sealing an application bundle.
package_root="$(mktemp -d "${TMPDIR:-/tmp}/chunfe-package.XXXXXX")"
stage_dir="$package_root/payload"
test_dir="$package_root/test"
mount_dir="$package_root/mounted"
mounted=false
cleanup() {
    if [[ "$mounted" == true ]]; then
        hdiutil detach "$mount_dir" -quiet || true
    fi
    rm -rf "$package_root"
}
trap cleanup EXIT
dist_dir="$source_dir/dist"
mkdir -p "$stage_dir" "$dist_dir"
cmake --install "$build_dir" --prefix "$stage_dir"
app="$stage_dir/Chunfe.app"
test -x "$app/Contents/MacOS/Chunfe"
test "$(lipo -archs "$app/Contents/MacOS/Chunfe")" = arm64

# CMake's Qt QML deployment script installs only scanned modules and invokes
# macdeployqt for their runtime dependencies. A direct -qmldir deployment also
# recursively copies unrelated modules nested below QtQuick.
test -d "$app/Contents/Frameworks/QtCore.framework"
test -f "$app/Contents/PlugIns/platforms/libqcocoa.dylib"
test -f "$app/Contents/Resources/qml/QtQuick/qmldir"

# Framework copies can carry Finder metadata, which codesign rejects. Clean the
# staged bundle only, then sign its nested code before signing the app itself.
xattr -cr "$app"
while IFS= read -r -d '' library; do
    codesign --force --sign - "$library"
done < <(find "$app/Contents" -type f -name '*.dylib' -print0)
while IFS= read -r -d '' framework; do
    codesign --force --sign - "$framework"
done < <(find "$app/Contents/Frameworks" -depth -type d -name '*.framework' -print0)
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
ln -s /Applications "$stage_dir/Applications"

dmg="$dist_dir/chunfe-$version-macos-arm64.dmg"
hdiutil create -volname "春风" -srcfolder "$stage_dir" -ov -format UDZO "$dmg"
hdiutil verify "$dmg"

mkdir -p "$mount_dir" "$test_dir/installed"
hdiutil attach "$dmg" -readonly -nobrowse -mountpoint "$mount_dir" -quiet
mounted=true
ditto "$mount_dir/Chunfe.app" "$test_dir/installed/Chunfe.app"
hdiutil detach "$mount_dir" -quiet
mounted=false
codesign --verify --deep --strict "$test_dir/installed/Chunfe.app"

# Validate the copied application and its deployed runtime without changing the
# application entry point. Automated startup testing is not configured yet.
test -x "$test_dir/installed/Chunfe.app/Contents/MacOS/Chunfe"
test -d "$test_dir/installed/Chunfe.app/Contents/Frameworks/QtCore.framework"
test -f "$test_dir/installed/Chunfe.app/Contents/PlugIns/platforms/libqcocoa.dylib"
test -f "$test_dir/installed/Chunfe.app/Contents/Resources/qml/QtQuick/qmldir"
echo "Verified disk image and deployed files: $dmg"
