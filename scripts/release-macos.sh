#!/bin/sh
# Builds the macOS (arm64) binary locally and attaches it to the GitHub release for <tag>.
# macOS CI runners are expensive, so this one asset is built on a developer Mac.
#   usage: scripts/release-macos.sh v0.1.0
set -eu
tag="${1:?usage: $0 <tag, e.g. v0.1.0>}"
cd "$(dirname "$0")/.."

[ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] || { echo "run this on an Apple Silicon Mac" >&2; exit 1; }

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-release
ctest --test-dir build-release --output-on-failure

built="$(build-release/memlayout --version | awk '{print $2}')"
[ "v$built" = "$tag" ] || { echo "binary reports v$built but tag is $tag; bump project(VERSION) in CMakeLists.txt" >&2; exit 1; }

out="$(mktemp -d)"
cp build-release/memlayout "$out/memlayout"
strip "$out/memlayout"
asset="memlayout-macos-arm64.tar.gz"
tar -czf "$out/$asset" -C "$out" memlayout
(cd "$out" && shasum -a 256 "$asset" > "$asset.sha256")

gh release view "$tag" >/dev/null 2>&1 || gh release create "$tag" --generate-notes
gh release upload "$tag" "$out/$asset" "$out/$asset.sha256" --clobber
echo "uploaded $asset to $tag"
