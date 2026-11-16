#!/bin/sh
# .cqspace dosyalarını Conqueror Player ile açtırır (264): MIME türü + .desktop girdisi + simge, yalnız bu kullanıcı için.
# usage: Tools/Linux/register-cqspace.sh <ConquerorRuntime'ın klasörü>   (ör. Hub kurulumunun bin/ klasörü)
set -e
BIN="$(cd "${1:?give the folder of ConquerorRuntime}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -x "$BIN/ConquerorRuntime" ] || { echo "no ConquerorRuntime in $BIN" >&2; exit 1; }
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS"
sed "s|^Exec=ConquerorRuntime|Exec=\"$BIN/ConquerorRuntime\"|" "$HERE/conqueror-player.desktop" > "$APPS/conqueror-player.desktop"
xdg-mime install --novendor "$HERE/conqueror-cqspace.xml"
ICON="$BIN/Resources/Logo/AppIcon/conqueror_256.png"
[ -f "$ICON" ] && xdg-icon-resource install --novendor --size 256 "$ICON" conqueror-engine || true
xdg-mime default conqueror-player.desktop application/x-cqspace
update-desktop-database "$APPS" 2>/dev/null || true
echo ".cqspace files now open with $BIN/ConquerorRuntime"
