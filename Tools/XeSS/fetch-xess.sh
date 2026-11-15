#!/usr/bin/env bash
# Intel XeSS çalışma zamanını (libxess.dll, yalnız Windows oyunları) indirir. Intel Simplified Software License ile oyunun yanında
# dağıtılabilir (External/xess/LICENSE-binaries.txt: telif notu ve koşullar dağıtımla birlikte verilir). DLL'i editörün (build/bin)
# yanına koyun: Build Settings onu Windows oyunlarına kopyalar.
#
#   Tools/XeSS/fetch-xess.sh <klasör>
set -euo pipefail
XESS_COMMIT="de0fb9c"
DEST="${1:?usage: $0 <folder>}"
TMP="$(mktemp -d)"
trap 'rm -rf -- "$TMP"' EXIT
git clone --quiet --filter=blob:none --sparse https://github.com/intel/xess "$TMP/xess"
git -C "$TMP/xess" checkout --quiet "$XESS_COMMIT"
git -C "$TMP/xess" sparse-checkout set bin
mkdir -p "$DEST"
cp "$TMP/xess/bin/libxess.dll" "$TMP/xess/LICENSE.txt" "$DEST/"
echo "libxess.dll and its license are in $DEST"
