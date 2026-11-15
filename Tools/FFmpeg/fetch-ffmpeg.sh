#!/usr/bin/env bash
# Recorder (249) için ffmpeg + ffprobe indirir: depoya konmaz (büyük ve GPL; motor onu ayrı bir program olarak boru üzerinden
# çalıştırır). Varsayılan hedef Recorder'ın baktığı kullanıcı klasörüdür; sürüm paketi için editörün yanındaki Tools/ffmpeg verilebilir:
#
#   Tools/FFmpeg/fetch-ffmpeg.sh                 -> ~/.local/share/ConquerorEngine/ffmpeg (macOS: ~/Library/Application Support/...)
#   Tools/FFmpeg/fetch-ffmpeg.sh build/bin/Tools/ffmpeg
#
# Linux: John Van Sickle'ın statik sürüm derlemeleri (x86_64 / arm64), yayımlanan MD5 ile doğrulanır.
# macOS: Homebrew varsa "brew install ffmpeg" önerilir (Recorder PATH'tekini de bulur); yoksa evermeet.cx statik derlemesi (HTTPS).
# Windows: Tools/FFmpeg/fetch-ffmpeg.ps1
set -euo pipefail

case "$(uname -s)" in
    Darwin) DEFAULT="$HOME/Library/Application Support/ConquerorEngine/ffmpeg" ;;
    *)      DEFAULT="${XDG_DATA_HOME:-$HOME/.local/share}/ConquerorEngine/ffmpeg" ;;
esac
DEST="${1:-$DEFAULT}"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
mkdir -p "$DEST"

if [ "$(uname -s)" = "Darwin" ]; then
    if command -v brew >/dev/null 2>&1; then
        echo "Homebrew found: run 'brew install ffmpeg' (the Recorder also finds ffmpeg in PATH)."
    fi
    for tool in ffmpeg ffprobe; do
        curl -fsSL -o "$TMP/$tool.zip" "https://evermeet.cx/ffmpeg/getrelease/$tool/zip"
        (cd "$TMP" && unzip -oq "$tool.zip")
        install -m 755 "$TMP/$tool" "$DEST/$tool"
    done
else
    case "$(uname -m)" in
        x86_64|amd64) ARCH=amd64 ;;
        aarch64|arm64) ARCH=arm64 ;;
        *) echo "No static ffmpeg build for $(uname -m): install ffmpeg with your package manager (the Recorder finds it in PATH)." >&2; exit 1 ;;
    esac
    BASE="https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-$ARCH-static.tar.xz"
    curl -fsSL -o "$TMP/ffmpeg.tar.xz" "$BASE"
    curl -fsSL -o "$TMP/ffmpeg.tar.xz.md5" "$BASE.md5"
    WANT="$(cut -d' ' -f1 "$TMP/ffmpeg.tar.xz.md5")"
    GOT="$(md5sum "$TMP/ffmpeg.tar.xz" | cut -d' ' -f1)"
    if [ "$WANT" != "$GOT" ]; then
        echo "Checksum mismatch ($GOT, expected $WANT): download not used." >&2
        exit 1
    fi
    tar -xJf "$TMP/ffmpeg.tar.xz" -C "$TMP"
    DIR="$(find "$TMP" -maxdepth 1 -type d -name 'ffmpeg-*-static' | head -n 1)"
    install -m 755 "$DIR/ffmpeg" "$DEST/ffmpeg"
    install -m 755 "$DIR/ffprobe" "$DEST/ffprobe"
    cp "$DIR/GPLv3.txt" "$DEST/LICENSE.txt" 2>/dev/null || true
    cp "$DIR/readme.txt" "$DEST/README.txt" 2>/dev/null || true
fi
echo "ffmpeg is in $DEST"
