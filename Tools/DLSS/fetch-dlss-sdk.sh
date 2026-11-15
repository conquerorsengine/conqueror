#!/usr/bin/env bash
# NVIDIA DLSS SDK'sını indirir (isteğe bağlı DLSS eklentisi için). SDK depoya girmez: NVIDIA RTX SDKs lisansı
# (https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt) SDK'nın açık kaynak lisansa tabi dağıtımını yasaklar. İndirip motoru
# -DCQ_DLSS_SDK_DIR=<klasör> ile derleyen kişi bu lisansı kabul etmiş olur; DLSS'li oyunu dağıtırken lisansın dağıtım koşullarına uyar.
#
#   Tools/DLSS/fetch-dlss-sdk.sh <klasör> --accept-nvidia-license
#   cmake -S . -B build -DCQ_DLSS_SDK_DIR=<klasör>
set -euo pipefail
DLSS_TAG="v310.9.16"
if [ $# -lt 2 ] || [ "$2" != "--accept-nvidia-license" ]; then
    echo "Usage: $0 <folder> --accept-nvidia-license"
    echo "Downloads the NVIDIA DLSS SDK $DLSS_TAG (headers, the NGX library and the DLSS runtime for Linux x86_64 and Windows x86_64)."
    echo "Read the NVIDIA RTX SDKs license first: https://github.com/NVIDIA/DLSS/blob/$DLSS_TAG/LICENSE.txt"
    exit 1
fi
DEST="$1"
if [ -e "$DEST" ] && [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
    echo "$DEST is not empty"
    exit 1
fi
git clone --quiet --depth 1 --branch "$DLSS_TAG" --filter=blob:none --sparse https://github.com/NVIDIA/DLSS "$DEST"
git -C "$DEST" sparse-checkout set include lib/Linux_x86_64 lib/Windows_x86_64/x64 lib/Windows_x86_64/rel
echo "NVIDIA DLSS SDK $DLSS_TAG in $DEST"
echo "Build: cmake -S . -B build -DCQ_DLSS_SDK_DIR=$(cd "$DEST" && pwd)"
