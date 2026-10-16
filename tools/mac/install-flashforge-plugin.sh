#!/bin/bash
# Installs the original (proprietary, x86_64) FlashForge network plugin into Mac Flash Studio.
#
#   ./install-flashforge-plugin.sh [path to "Mac Flash Studio.app"] [path to libFlashNetwork.dylib]
#
# Defaults: the app in /Applications and the plugin shipped with an installed FlashForge "Flash Studio".
# The plugin is not redistributed with Mac Flash Studio: it is copied from your own Flash Studio installation.
set -euo pipefail

app="${1:-/Applications/Mac Flash Studio.app}"
plugin="${2:-/Applications/Flash Studio.app/Contents/MacOS/libFlashNetwork.dylib}"

[ -d "$app" ]    || { echo "Mac Flash Studio not found: $app" >&2; exit 1; }
[ -f "$plugin" ] || { echo "FlashForge plugin not found: $plugin" >&2
                      echo "Install the original Flash Studio from FlashForge first, or pass the plugin path as 2nd argument." >&2; exit 1; }
file "$plugin" | grep -q x86_64 || { echo "Not an x86_64 plugin: $plugin" >&2; exit 1; }
[ -f "$app/Contents/MacOS/fnet_helper" ] || { echo "This app has no network bridge (fnet_helper)." >&2; exit 1; }

cp "$plugin" "$app/Contents/MacOS/libFlashNetwork_x64.dylib"
xattr -cr "$app" 2>/dev/null || true
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
echo "ok: plugin installed in $app"
