#!/bin/sh
# WebGPU arka ucu için Dawn kaynak ağacı (Google, BSD 3-Clause / Apache 2.0): motor yalnız gölgelendirici çeviricisi Tint'i
# (SPIR-V -> WGSL) derler; Dawn'ın kendisi derlenmez. Sabitlenmiş sürüm + bağımlılıklar (DEPS, depot_tools gerekmez).
#   Tools/WebGPU/fetch-dawn.sh <hedef klasör>
#   cmake ... -DCQ_DAWN_DIR=<hedef klasör>      (ya da ortamda CQ_DAWN_DIR)
# Gerekenler: git, python3. Yaklaşık 1,3 GB.
set -eu
DEST="${1:?usage: fetch-dawn.sh <folder>}"
REV="${CQ_DAWN_REV:-12c86c40d485500ec13385b48a5224bbd3ce0f34}"
if [ ! -d "$DEST/.git" ]; then
    mkdir -p "$DEST"
    git -C "$DEST" init -q
    git -C "$DEST" remote add origin https://github.com/google/dawn.git
fi
git -C "$DEST" fetch -q --depth 1 origin "$REV"
git -C "$DEST" checkout -q FETCH_HEAD
python3 "$DEST/tools/fetch_dawn_dependencies.py" --directory "$DEST" --shallow
echo "Dawn $REV ready: $DEST (cmake -DCQ_DAWN_DIR=$DEST)"
