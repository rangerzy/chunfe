#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_dir"

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "This package must be built on Linux x86_64." >&2
    exit 1
fi

for command in flatpak flatpak-builder python3 desktop-file-validate eu-strip eu-elfcompress; do
    command -v "$command" >/dev/null || {
        echo "Required command not found: $command" >&2
        exit 1
    }
done

version="$(python3 - <<'PY'
import pathlib
import re

source = pathlib.Path("CMakeLists.txt").read_text(encoding="utf-8")
match = re.search(
    r"(?m)^\s*project\(\s*Chunfe\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s+LANGUAGES\s+CXX\s*\)",
    source,
)
if not match:
    raise SystemExit("Cannot read the project version from CMakeLists.txt")
print(match.group(1))
PY
)"

app_id=io.github.rangerzy.chunfe
runtime_branch=6.12-beta
runtime_repo=https://flathub.org/beta-repo/flathub-beta.flatpakrepo
manifest=packaging/linux/io.github.rangerzy.chunfe.json
build_dir=build/flatpak
repo_dir=build/flatpak-repo
bundle="dist/chunfe-${version}-linux-x64.flatpak"

# The inherited KDE 6.12 runtime comes from Flathub Beta. Verify its Qt
# version on every build rather than silently falling back to older Qt.
flatpak remote-add --user --if-not-exists flathub \
    https://flathub.org/repo/flathub.flatpakrepo
flatpak remote-add --user --if-not-exists flathub-beta "$runtime_repo"
flatpak install --user --noninteractive flathub-beta \
    "org.kde.Platform/x86_64/$runtime_branch" \
    "org.kde.Sdk/x86_64/$runtime_branch"

qt_version="$(flatpak run --user --arch=x86_64 --command=pkg-config \
    "org.kde.Sdk//$runtime_branch" --modversion Qt6Core)"
if [[ ! "$qt_version" =~ ^6\.12\.[0-9]+$ ]]; then
    echo "Expected released Qt 6.12.x in the SDK; found: $qt_version" >&2
    exit 1
fi
echo "Building Chunfe $version with Qt $qt_version (KDE $runtime_branch)."

desktop-file-validate "packaging/linux/$app_id.desktop"
mkdir -p build dist
flatpak-builder --user --arch=x86_64 --force-clean \
    --disable-rofiles-fuse --rebuild-on-sdk-change \
    --repo="$repo_dir" "$build_dir" "$manifest"

test -x "$build_dir/files/bin/Chunfe"
test -f "$build_dir/files/share/applications/$app_id.desktop"
test -f "$build_dir/files/share/icons/hicolor/scalable/apps/$app_id.svg"
flatpak build-bundle --arch=x86_64 --runtime-repo="$runtime_repo" \
    "$repo_dir" "$bundle" "$app_id" stable

# Install the exported bundle and check its metadata and payload. The
# application has no automatic-exit test mode, so this does not launch it.
flatpak install --user --noninteractive --reinstall "$bundle"
installed_ref="$(flatpak info --user --arch=x86_64 --show-ref "$app_id//stable")"
if [[ "$installed_ref" != "app/$app_id/x86_64/stable" ]]; then
    echo "Installed Flatpak has an unexpected ref: $installed_ref" >&2
    exit 1
fi
installed_dir="$(flatpak info --user --arch=x86_64 --show-location "$app_id//stable")"
python3 - "$installed_dir" "$build_dir" "$app_id" "$runtime_branch" <<'PY'
import configparser
import hashlib
from pathlib import Path
import sys

installed = Path(sys.argv[1])
built = Path(sys.argv[2])
app_id, runtime_branch = sys.argv[3:]
metadata = configparser.ConfigParser(interpolation=None)
if not metadata.read(installed / "metadata", encoding="utf-8"):
    raise SystemExit("Installed Flatpak metadata is missing")
for key, expected in {
    "name": app_id,
    "command": "Chunfe",
    "runtime": f"org.kde.Platform/x86_64/{runtime_branch}",
}.items():
    if metadata.get("Application", key, fallback=None) != expected:
        raise SystemExit(f"Installed Flatpak has unexpected {key} metadata")

binary = installed / "files/bin/Chunfe"
if not binary.is_file() or not binary.stat().st_mode & 0o111:
    raise SystemExit("Installed Chunfe executable is missing or not executable")
for relative in (
    "bin/Chunfe",
    f"share/applications/{app_id}.desktop",
    f"share/icons/hicolor/scalable/apps/{app_id}.svg",
):
    expected = built / "files" / relative
    actual = installed / "files" / relative
    if not actual.is_file():
        raise SystemExit(f"Installed Flatpak is missing {relative}")
    if hashlib.sha256(expected.read_bytes()).digest() != hashlib.sha256(actual.read_bytes()).digest():
        raise SystemExit(f"Installed Flatpak payload differs: {relative}")
PY

test -s "$bundle"
echo "Verified Flatpak contents and metadata (application startup not tested): $bundle"
