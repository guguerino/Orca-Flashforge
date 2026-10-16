#!/bin/bash
# Installs the FlashNetwork bridge into a natively built (arm64) Flash Studio app bundle.
#
#   ./package.sh <built Orca-Flashforge.app> <output .app> [path to original x86_64 libFlashNetwork.dylib]
#
# The original plugin is proprietary: it is taken from an installed Flash Studio, never rebuilt.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src_app="${1:?usage: package.sh <built .app> <output .app> [plugin dylib]}"
out_app="${2:?usage: package.sh <built .app> <output .app> [plugin dylib]}"
plugin="${3:-/Applications/Flash Studio.app/Contents/MacOS/libFlashNetwork.dylib}"

[ -f "$plugin" ] || { echo "original plugin not found: $plugin" >&2; exit 1; }
file "$plugin" | grep -q x86_64 || { echo "plugin is not x86_64: $plugin" >&2; exit 1; }

make -C "$here"

rm -rf "$out_app"
cp -R "$src_app" "$out_app"
macos="$out_app/Contents/MacOS"
cp "$here/out/libFlashNetwork.dylib" "$macos/libFlashNetwork.dylib"     # arm64 shim, loaded by the app
cp "$here/out/fnet_helper"           "$macos/fnet_helper"                # x86_64 helper (Rosetta)
cp "$plugin"                         "$macos/libFlashNetwork_x64.dylib"  # original plugin, hosted by the helper

xattr -cr "$out_app" 2>/dev/null || true
codesign --force --deep --sign - "$out_app"
codesign --verify --deep --strict "$out_app"
echo "ok: $out_app"
