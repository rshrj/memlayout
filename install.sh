#!/bin/sh
# Installs (or updates) memlayout from the latest GitHub release.
#   curl -fsSL https://github.com/rshrj/memlayout/releases/latest/download/install.sh | sh
# Env: MEMLAYOUT_INSTALL_DIR (default ~/.local/bin), MEMLAYOUT_VERSION (default latest, e.g. v0.1.0)
set -eu

REPO="rshrj/memlayout"
dir="${MEMLAYOUT_INSTALL_DIR:-$HOME/.local/bin}"
version="${MEMLAYOUT_VERSION:-latest}"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)               target="macos-arm64" ;;
  Linux-x86_64|Linux-amd64)   target="linux-x86_64" ;;
  *) echo "memlayout: no prebuilt binary for $(uname -s)/$(uname -m); build from source (see README)." >&2; exit 1 ;;
esac

if [ "$version" = "latest" ]; then
  base="https://github.com/$REPO/releases/latest/download"
else
  base="https://github.com/$REPO/releases/download/$version"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
asset="memlayout-$target.tar.gz"

echo "Downloading $asset ($version)..."
curl -fsSL "$base/$asset" -o "$tmp/$asset"
curl -fsSL "$base/$asset.sha256" -o "$tmp/$asset.sha256"

expected="$(cut -d' ' -f1 "$tmp/$asset.sha256")"
if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$tmp/$asset" | cut -d' ' -f1)"
else actual="$(shasum -a 256 "$tmp/$asset" | cut -d' ' -f1)"; fi
if [ "$expected" != "$actual" ]; then echo "memlayout: checksum mismatch, aborting." >&2; exit 1; fi

tar -xzf "$tmp/$asset" -C "$tmp"
mkdir -p "$dir"
install -m 755 "$tmp/memlayout" "$dir/memlayout"
echo "Installed $("$dir/memlayout" --version) to $dir/memlayout"

case ":$PATH:" in
  *":$dir:"*) ;;
  *) echo "Note: $dir is not on your PATH. Add this to your shell profile:"; echo "  export PATH=\"$dir:\$PATH\"" ;;
esac
echo "Update any time with:  memlayout --self-update"
